#include "tcp_client_transport.hpp"
#include "utils/socket_utils.hpp"

#include <stdexcept>
#include <cstring>
#include <chrono>
#include <thread>

namespace {

// Split "host:port" or "[ipv6]:port"
std::pair<std::string, std::string> split_host_port(const std::string& endpoint)
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

void set_common_sockopts(socket_t s)
{
    int yes = 1;
    ::setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof(yes));
#ifndef _WIN32
    ::setsockopt(s, SOL_SOCKET, SO_REUSEPORT, reinterpret_cast<const char*>(&yes), sizeof(yes));
#endif
    ::setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&yes), sizeof(yes));
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

TcpClientTransport::TcpClientTransport()
    : sock_(invalid_socket())
    , in_next_(0)
{
}

TcpClientTransport::~TcpClientTransport()
{
    disconnect();
}

void TcpClientTransport::connect(const std::string& endpoint)
{
    if (is_valid_socket(sock_))
        disconnect();

    auto hp = split_host_port(endpoint);
    const std::string& host = hp.first;
    const std::string& port_s = hp.second;

    if (port_s.empty())
        throw std::runtime_error("TcpClientTransport: missing port in endpoint \"" + endpoint + "\"");

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    addrinfo* res = nullptr;
    int gai_rc = ::getaddrinfo(host.c_str(), port_s.c_str(), &hints, &res);
    if (gai_rc != 0 || !res)
    {
        throw std::runtime_error(make_socket_error_str("TcpClientTransport: getaddrinfo failed for " + endpoint));
    }

    struct AddrInfoGuard
    {
        addrinfo* p;
        ~AddrInfoGuard()
        {
            if (p)
                ::freeaddrinfo(p);
        }
    } guard{res};

    constexpr size_t kMaxAttempts = 12;
    constexpr int kSleepMs = 200;
    int last_err = 0;

    for (size_t attempt = 1; attempt <= kMaxAttempts; ++attempt)
    {
        for (auto* p = res; p; p = p->ai_next)
        {
            socket_t cand = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
            if (!is_valid_socket(cand))
            {
                last_err = sock_errno_local();
                continue;
            }

            set_common_sockopts(cand);

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

    throw std::runtime_error(make_socket_error_str(
        "TcpClientTransport: connect failed to " + endpoint + " (last errno=" + std::to_string(last_err) + ")"));
}

void TcpClientTransport::disconnect()
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

void TcpClientTransport::send(const void* data, size_t len, bool more)
{
    if (!is_valid_socket(sock_))
        throw std::runtime_error("TcpClientTransport: not connected");

    out_parts_.emplace_back(static_cast<const char*>(data), len);
    if (!more)
    {
        if (!rawtcp::send_parts(sock_, out_parts_))
        {
            out_parts_.clear();
            mv_log("%s", make_socket_error_str("TcpClientTransport: send_parts failed").c_str());
        }
        out_parts_.clear();
    }
}

void TcpClientTransport::send_text(const std::string& s, bool more)
{
    send(s.data(), s.size(), more);
}

void TcpClientTransport::recv(void* data, size_t len)
{
    if (!is_valid_socket(sock_))
        throw std::runtime_error("TcpClientTransport: not connected");

    if (in_next_ >= in_parts_.size())
    {
        in_parts_.clear();
        in_next_ = 0;
        if (!rawtcp::recv_parts(sock_, in_parts_))
        {
            mv_log("%s", make_socket_error_str("TcpClientTransport: recv_parts failed").c_str());
        }
    }

    const std::string& frame = in_parts_[in_next_++];
    if (frame.size() != len)
    {
        mv_log("%s", make_socket_error_str("TcpClientTransport: recv size mismatch (expected " + std::to_string(len) +
                                           ", got " + std::to_string(frame.size()) + ")")
                         .c_str());
    }
    std::memcpy(data, frame.data(), len);
}

std::string TcpClientTransport::recv_text()
{
    if (!is_valid_socket(sock_))
        throw std::runtime_error("TcpClientTransport: not connected");

    if (in_next_ >= in_parts_.size())
    {
        in_parts_.clear();
        in_next_ = 0;
        if (!rawtcp::recv_parts(sock_, in_parts_, 5000))
        {
            mv_log("%s", make_socket_error_str("TcpClientTransport: recv_parts failed").c_str());
        }
    }

    return in_parts_[in_next_++];
}

bool TcpClientTransport::recv_multipart(std::vector<std::string>& parts)
{
    if (!is_valid_socket(sock_))
        return false;

    in_parts_.clear();
    in_next_ = 0;

    if (!rawtcp::recv_parts(sock_, in_parts_, 5000))
    {
        mv_log("%s", make_socket_error_str("TcpClientTransport: recv_parts failed").c_str());
        return false;
    }

    parts = in_parts_;
    in_parts_.clear();
    in_next_ = 0;
    return true;
}
