#include "tcp_server_transport.hpp"

#include <cstring>
#include <stdexcept>

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

TcpServerTransport::TcpServerTransport()
    : listen_fd_(invalid_socket())
    , client_fd_(invalid_socket())
    , in_next_(0)
{
}

TcpServerTransport::~TcpServerTransport()
{
    disconnect();
}

void TcpServerTransport::listen(const std::string& endpoint)
{
    if (is_valid_socket(listen_fd_))
    {
        close_socket(listen_fd_);
        listen_fd_ = invalid_socket();
    }

    auto hp = split_host_port(endpoint);
    const std::string& host = hp.first;
    const std::string& port_s = hp.second;

    if (port_s.empty())
        throw std::runtime_error("TcpServerTransport: missing port in endpoint \"" + endpoint + "\"");

    bool is_any_host = (host.empty() || host == "0.0.0.0" || host == "*" || host == "::");

    addrinfo hints{};
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    hints.ai_family = AF_UNSPEC;

    addrinfo* res = nullptr;
    int gai_rc = ::getaddrinfo(is_any_host ? nullptr : host.c_str(), port_s.c_str(), &hints, &res);
    if (gai_rc != 0 || !res)
    {
        throw std::runtime_error(make_socket_error_str("TcpServerTransport: getaddrinfo(bind) failed for " + endpoint));
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

        set_common_sockopts(cand);

        if (::bind(cand, p->ai_addr, static_cast<socklen_t>(p->ai_addrlen)) != 0)
        {
            close_socket(cand);
            continue;
        }

        if (::listen(cand, SOMAXCONN) != 0)
        {
            close_socket(cand);
            continue;
        }

        listen_fd_ = cand;
        return;
    }

    throw std::runtime_error(make_socket_error_str("TcpServerTransport: bind/listen failed on " + endpoint));
}

bool TcpServerTransport::accept()
{
    if (!is_valid_socket(listen_fd_))
    {
        throw std::runtime_error("TcpServerTransport: accept called but not listening");
    }

    if (is_valid_socket(client_fd_))
    {
        close_socket(client_fd_);
        client_fd_ = invalid_socket();
    }

    sockaddr_storage cliaddr{};
    socklen_t len = sizeof(cliaddr);
    socket_t cfd = ::accept(listen_fd_, reinterpret_cast<sockaddr*>(&cliaddr), &len);
    if (!is_valid_socket(cfd))
    {
        int err = sock_errno_local();
        (void)err;
        return false;
    }

    set_common_sockopts(cfd);
    client_fd_ = cfd;
    in_parts_.clear();
    in_next_ = 0;
    return true;
}

void TcpServerTransport::disconnect()
{
    if (is_valid_socket(client_fd_))
    {
        close_socket(client_fd_);
        client_fd_ = invalid_socket();
    }
    if (is_valid_socket(listen_fd_))
    {
        close_socket(listen_fd_);
        listen_fd_ = invalid_socket();
    }

    out_parts_.clear();
    in_parts_.clear();
    in_next_ = 0;
}

void TcpServerTransport::send(const void* data, size_t len, bool more)
{
    if (!is_valid_socket(client_fd_))
        throw std::runtime_error("TcpServerTransport: no client connected");

    out_parts_.emplace_back(static_cast<const char*>(data), len);
    if (!more)
    {
        if (!rawtcp::send_parts(client_fd_, out_parts_))
        {
            out_parts_.clear();
            throw std::runtime_error(make_socket_error_str("TcpServerTransport: send_parts failed"));
        }
        out_parts_.clear();
    }
}

void TcpServerTransport::send_text(const std::string& s, bool more)
{
    send(s.data(), s.size(), more);
}

void TcpServerTransport::recv(void* data, size_t len)
{
    if (!is_valid_socket(client_fd_))
        throw std::runtime_error("TcpServerTransport: no client connected");

    if (in_next_ >= in_parts_.size())
    {
        in_parts_.clear();
        in_next_ = 0;
        if (!rawtcp::recv_parts(client_fd_, in_parts_, 5000))
        {
            throw std::runtime_error("TcpServerTransport: recv_parts failed");
        }
    }

    const std::string& frame = in_parts_[in_next_++];
    if (frame.size() != len)
    {
        throw std::runtime_error("TcpServerTransport: recv size mismatch (expected " + std::to_string(len) + ", got " +
                                 std::to_string(frame.size()) + ")");
    }
    std::memcpy(data, frame.data(), len);
}

std::string TcpServerTransport::recv_text()
{
    if (!is_valid_socket(client_fd_))
        throw std::runtime_error("TcpServerTransport: no client connected");

    if (in_next_ >= in_parts_.size())
    {
        in_parts_.clear();
        in_next_ = 0;
        if (!rawtcp::recv_parts(client_fd_, in_parts_, 5000))
        {
            throw std::runtime_error("TcpServerTransport: recv_parts failed");
        }
    }

    return in_parts_[in_next_++];
}

bool TcpServerTransport::recv_multipart(std::vector<std::string>& parts)
{
    if (!is_valid_socket(client_fd_))
        return false;

    in_parts_.clear();
    in_next_ = 0;

    if (!rawtcp::recv_parts(client_fd_, in_parts_, 5000))
        return false;

    parts = in_parts_;
    in_parts_.clear();
    in_next_ = 0;
    return true;
}
