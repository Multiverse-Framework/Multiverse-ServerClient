#include "udp_client_transport.hpp"
#include "utils/socket_utils.hpp"
#include "utils/raw_udp.hpp"

#include <stdexcept>
#include <cstring>
#include <chrono>
#include <thread>

namespace {

std::pair<std::string, std::string> split_host_port(const std::string &endpoint)
{
    if (endpoint.empty())
        return {"", ""};
    size_t host_end = endpoint.rfind(':');

    if (endpoint[0] == '[')
    {
        host_end = endpoint.find("]:");
        if (host_end != std::string::npos)
        {
            return {endpoint.substr(1, host_end - 1), endpoint.substr(host_end + 2)};
        }
    }

    if (host_end == std::string::npos)
        return {endpoint, ""};

    return {endpoint.substr(0, host_end), endpoint.substr(host_end + 1)};
}

int sock_errno_local()
{
#ifdef _WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}

} // namespace

UdpClientTransport::UdpClientTransport()
    : sock_(invalid_socket()),
      in_next_(0)
{
}

UdpClientTransport::~UdpClientTransport()
{
    disconnect();
}

void UdpClientTransport::connect(const std::string &endpoint)
{
    if (is_valid_socket(sock_))
        disconnect();

    auto hp = split_host_port(endpoint);
    const std::string &host = hp.first;
    const std::string &port = hp.second;

    if (port.empty())
        throw std::runtime_error("UdpClientTransport: port is missing in endpoint " + endpoint);

    addrinfo hints{};
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_family   = AF_UNSPEC;

    addrinfo *res = nullptr;
    int rc = ::getaddrinfo(host.c_str(), port.c_str(), &hints, &res);
    if (rc != 0 || !res)
    {
        int err = sock_errno_local();
        (void)err;
        throw std::runtime_error(
            make_socket_error_str("UdpClientTransport: getaddrinfo failed for " + endpoint));
    }

    struct AddrInfoGuard {
        addrinfo *p;
        ~AddrInfoGuard() { if (p) ::freeaddrinfo(p); }
    } guard{res};

    constexpr size_t kMaxAttempts = 12;
    constexpr int    kSleepMs     = 200;
    int last_err = 0;

    for (size_t attempt = 1; attempt <= kMaxAttempts; ++attempt)
    {
        for (auto *p = res; p; p = p->ai_next)
        {
            socket_t cand = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
            if (!is_valid_socket(cand))
            {
                last_err = sock_errno_local();
                continue;
            }

            int yes = 1;
            ::setsockopt(cand, SOL_SOCKET, SO_REUSEADDR,
                         reinterpret_cast<const char *>(&yes), sizeof(yes));
#ifndef _WIN32
            ::setsockopt(cand, SOL_SOCKET, SO_REUSEPORT,
                         reinterpret_cast<const char *>(&yes), sizeof(yes));
#endif

            if (::connect(cand, p->ai_addr, static_cast<socklen_t>(p->ai_addrlen)) == 0)
            {
                sock_ = cand;
                return;
            }

            last_err = sock_errno_local();
            close_socket(cand);
        }

        if (attempt < kMaxAttempts)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(kSleepMs));
        }
    }

    throw std::runtime_error(
        make_socket_error_str("UdpClientTransport: connect failed to " + endpoint +
                              " (last errno=" + std::to_string(last_err) + ")"));
}

void UdpClientTransport::disconnect()
{
    if (is_valid_socket(sock_))
    {
        close_socket(sock_);
        sock_ = invalid_socket();
    }
    out_parts_.clear();
    in_parts_.clear();
    in_next_ = 0;
}

void UdpClientTransport::send(const void *data, size_t len, bool more)
{
    if (!is_valid_socket(sock_))
        throw std::runtime_error("UdpClientTransport: not connected");

    out_parts_.emplace_back(static_cast<const char *>(data), len);
    if (!more)
    {
        if (!rawudp::send_parts(sock_, out_parts_))
        {
            out_parts_.clear();
            throw std::runtime_error(
                make_socket_error_str("UdpClientTransport: send_parts failed"));
        }
        out_parts_.clear();
    }
}

void UdpClientTransport::send_text(const std::string &s, bool more)
{
    send(s.data(), s.size(), more);
}

void UdpClientTransport::recv(void *data, size_t len)
{
    if (!is_valid_socket(sock_))
        throw std::runtime_error("UdpClientTransport: not connected");

    if (in_next_ >= in_parts_.size())
    {
        in_parts_.clear();
        in_next_ = 0;
        if (!rawudp::recv_parts(sock_, in_parts_, 5000))
        {
            throw std::runtime_error("UdpClientTransport: recv_parts failed");
        }
    }

    const std::string &frame = in_parts_[in_next_++];
    if (frame.size() != len)
    {
        throw std::runtime_error("UdpClientTransport: recv size mismatch (expected " +
                                 std::to_string(len) + ", got " +
                                 std::to_string(frame.size()) + ")");
    }
    std::memcpy(data, frame.data(), len);
}

std::string UdpClientTransport::recv_text()
{
    if (!is_valid_socket(sock_))
        throw std::runtime_error("UdpClientTransport: not connected");

    if (in_next_ >= in_parts_.size())
    {
        in_parts_.clear();
        in_next_ = 0;
        if (!rawudp::recv_parts(sock_, in_parts_, 5000))
        {
            throw std::runtime_error("UdpClientTransport: recv_parts failed");
        }
    }

    return in_parts_[in_next_++];
}

bool UdpClientTransport::recv_multipart(std::vector<std::string> &parts)
{
    if (!is_valid_socket(sock_))
        return false;

    in_parts_.clear();
    in_next_ = 0;

    if (!rawudp::recv_parts(sock_, in_parts_, -1))
        return false;

    parts = in_parts_;
    in_parts_.clear();
    in_next_ = 0;
    return true;
}
