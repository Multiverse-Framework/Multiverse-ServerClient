#include "udp_server_transport.hpp"

#include <cstring>
#include <stdexcept>

#include "utils/raw_udp.hpp"
#include "utils/socket_utils.hpp"

namespace {

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

std::string describe_sockaddr(const sockaddr_storage& ss, socklen_t slen)
{
    char host[NI_MAXHOST]{};
    char serv[NI_MAXSERV]{};

    if (getnameinfo(reinterpret_cast<const sockaddr*>(&ss), slen, host, sizeof(host), serv, sizeof(serv),
            NI_NUMERICHOST | NI_NUMERICSERV) == 0)
    {
        return std::string(host) + ":" + std::string(serv);
    }
    return "<unknown>";
}

} // namespace

UdpServerTransport::UdpServerTransport()
    : sock_(invalid_socket())
    , peer_known_(false)
    , peer_len_(0)
    , in_next_(0)
{
    std::memset(&peer_addr_, 0, sizeof(peer_addr_));
}

UdpServerTransport::~UdpServerTransport()
{
    disconnect();
}

void UdpServerTransport::listen(const std::string& endpoint)
{
    if (is_valid_socket(sock_))
        disconnect();

    auto hp = split_host_port(endpoint);
    const std::string& host = hp.first;
    const std::string& port = hp.second;

    if (port.empty())
        throw std::runtime_error("UdpServerTransport: missing port in endpoint \"" + endpoint + "\"");

    bool is_any_host = (host.empty() || host == "*" || host == "0.0.0.0" || host == "::");

    addrinfo hints{};
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_PASSIVE;
    hints.ai_family = AF_UNSPEC;

    addrinfo* res = nullptr;
    int rc = ::getaddrinfo(is_any_host ? nullptr : host.c_str(), port.c_str(), &hints, &res);
    if (rc != 0 || !res)
    {
        throw std::runtime_error(make_socket_error_str("UdpServerTransport: getaddrinfo(bind) failed for " + endpoint));
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

    for (auto* p = res; p; p = p->ai_next)
    {
        socket_t cand = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (!is_valid_socket(cand))
            continue;

        int yes = 1;
        ::setsockopt(cand, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof(yes));
#ifdef SO_REUSEPORT
        ::setsockopt(cand, SOL_SOCKET, SO_REUSEPORT, reinterpret_cast<const char*>(&yes), sizeof(yes));
#endif

        if (::bind(cand, p->ai_addr, static_cast<socklen_t>(p->ai_addrlen)) == 0)
        {
            sock_ = cand;

            sockaddr_storage local{};
            socklen_t slen = sizeof(local);
            if (::getsockname(sock_, reinterpret_cast<sockaddr*>(&local), &slen) == 0)
            {
                (void)describe_sockaddr(local, slen);
            }

            peer_known_ = false;
            peer_len_ = 0;
            std::memset(&peer_addr_, 0, sizeof(peer_addr_));
            return;
        }

        close_socket(cand);
    }

    throw std::runtime_error(make_socket_error_str("UdpServerTransport: bind failed on " + endpoint));
}

bool UdpServerTransport::accept()
{
    // UDP: "accept" conceptually just means we’ll learn the peer on first recv.
    return true;
}

void UdpServerTransport::disconnect()
{
    if (is_valid_socket(sock_))
    {
        close_socket(sock_);
        sock_ = invalid_socket();
    }

    peer_known_ = false;
    peer_len_ = 0;
    std::memset(&peer_addr_, 0, sizeof(peer_addr_));

    out_parts_.clear();
    in_parts_.clear();
    in_next_ = 0;
}

void UdpServerTransport::send(const void* data, size_t len, bool more)
{
    if (!is_valid_socket(sock_))
        throw std::runtime_error("UdpServerTransport: socket not bound");

    if (!peer_known_)
        throw std::runtime_error("UdpServerTransport: peer unknown, no packet received yet");

    out_parts_.emplace_back(static_cast<const char*>(data), len);
    if (!more)
    {
        if (!rawudp::send_parts_to(sock_, out_parts_, reinterpret_cast<const sockaddr*>(&peer_addr_), peer_len_))
        {
            out_parts_.clear();
            throw std::runtime_error(make_socket_error_str("UdpServerTransport: send_parts_to failed"));
        }
        out_parts_.clear();
    }
}

void UdpServerTransport::send_text(const std::string& s, bool more)
{
    send(s.data(), s.size(), more);
}

void UdpServerTransport::recv(void* data, size_t len)
{
    if (!is_valid_socket(sock_))
        throw std::runtime_error("UdpServerTransport: socket not bound");

    if (in_next_ >= in_parts_.size())
    {
        in_parts_.clear();
        in_next_ = 0;

        if (!peer_known_)
        {
            peer_len_ = sizeof(peer_addr_);
            if (!rawudp::recv_parts_from(sock_, in_parts_, reinterpret_cast<sockaddr*>(&peer_addr_), &peer_len_, 1000))
            {
                throw std::runtime_error("UdpServerTransport: recv_parts_from failed");
            }
            peer_known_ = true;
        }
        else
        {
            if (!rawudp::recv_parts(sock_, in_parts_, 1000))
            {
                throw std::runtime_error("UdpServerTransport: recv_parts failed");
            }
        }
    }

    const std::string& frame = in_parts_[in_next_++];
    if (frame.size() != len)
    {
        throw std::runtime_error("UdpServerTransport: recv size mismatch (expected " + std::to_string(len) + ", got " +
                                 std::to_string(frame.size()) + ")");
    }
    std::memcpy(data, frame.data(), len);
}

std::string UdpServerTransport::recv_text()
{
    if (!is_valid_socket(sock_))
        throw std::runtime_error("UdpServerTransport: socket not bound");

    if (in_next_ >= in_parts_.size())
    {
        in_parts_.clear();
        in_next_ = 0;

        if (!peer_known_)
        {
            peer_len_ = sizeof(peer_addr_);
            if (!rawudp::recv_parts_from(sock_, in_parts_, reinterpret_cast<sockaddr*>(&peer_addr_), &peer_len_, 1000))
            {
                throw std::runtime_error("UdpServerTransport: recv_parts_from failed");
            }
            peer_known_ = true;
        }
        else
        {
            if (!rawudp::recv_parts(sock_, in_parts_, 1000))
            {
                throw std::runtime_error("UdpServerTransport: recv_parts failed");
            }
        }
    }

    return in_parts_[in_next_++];
}

bool UdpServerTransport::recv_multipart(std::vector<std::string>& parts)
{
    if (!is_valid_socket(sock_))
        return false;

    in_parts_.clear();
    in_next_ = 0;

    if (!peer_known_)
    {
        peer_len_ = sizeof(peer_addr_);
        if (!rawudp::recv_parts_from(sock_, in_parts_, reinterpret_cast<sockaddr*>(&peer_addr_), &peer_len_, 1000))
            return false;
        peer_known_ = true;
    }
    else
    {
        if (!rawudp::recv_parts(sock_, in_parts_, 1000))
            return false;
    }

    parts = in_parts_;
    in_parts_.clear();
    in_next_ = 0;
    return true;
}
