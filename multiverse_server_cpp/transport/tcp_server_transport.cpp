#include "tcp_server_transport.hpp"

#include <cstring>
#include <stdexcept>

#include "utils/socket_utils.hpp"
#include "utils/general.hpp"
#include "utils/log_utils.hpp"

TcpServerTransport::TcpServerTransport()
    : state_(TcpServerState::Idle)
    , listen_fd_(invalid_socket())
#ifdef TCP_SERVER_USE_SYNC
    , client_fd_(invalid_socket())
#endif
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

    auto [host, port] = split_host_port(endpoint);

    if (port.empty())
        throw std::runtime_error("TcpServerTransport: missing port in endpoint \"" + endpoint + "\"");

    bool is_any_host = (host.empty() || host == "0.0.0.0" || host == "*" || host == "::");

    addrinfo hints{};
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    hints.ai_family = AF_UNSPEC;

    addrinfo* res = nullptr;
    int gai_rc = ::getaddrinfo(is_any_host ? nullptr : host.c_str(), port.c_str(), &hints, &res);
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

        set_socket_options(cand, true);

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
        state_ = TcpServerState::Listening;
        return;
    }

    throw std::runtime_error(make_socket_error_str("TcpServerTransport: bind/listen failed on " + endpoint));
}

bool TcpServerTransport::accept()
{
    if (state_ != TcpServerState::Listening && state_ != TcpServerState::Connected)
    {
        throw std::runtime_error("TcpServerTransport: accept called but not in Listening or Connected state");
    }

#ifdef TCP_SERVER_USE_ASYNC
    // Linux: Stop any existing async client
    if (async_client_.is_running())
    {
        async_client_.stop();
    }
#else
    // Windows: Close any existing client socket
    if (is_valid_socket(client_fd_))
    {
        close_socket(client_fd_);
        client_fd_ = invalid_socket();
    }
#endif

    // Use select with timeout to avoid blocking forever on accept
    // This allows us to check for shutdown periodically
    while (!ShutdownManager::is_shutdown())
    {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(listen_fd_, &rfds);

        // Wait up to 1 second for a new connection
        timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;

        int sel = select(listen_fd_ + 1, &rfds, nullptr, nullptr, &tv);
        if (sel < 0)
        {
            // Error
            return false;
        }
        if (sel == 0)
        {
            // Timeout - check for shutdown and continue waiting
            continue;
        }

        // Connection is pending, accept it
        sockaddr_storage cliaddr{};
        socklen_t len = sizeof(cliaddr);
        socket_t cfd = ::accept(listen_fd_, reinterpret_cast<sockaddr*>(&cliaddr), &len);
        if (!is_valid_socket(cfd))
        {
            return false;
        }

        set_socket_options(cfd, true);

#ifdef TCP_SERVER_USE_ASYNC
        // Linux: Start async receive thread for this client with fast polling (10ms like UDP)
        async_client_.start(cfd, 10);
#else
        // Windows: Store the client socket directly
        client_fd_ = cfd;
#endif

        state_ = TcpServerState::Connected;
        out_parts_.clear();
        in_parts_.clear();
        in_next_ = 0;

        return true;
    }

    // Shutdown was requested
    return false;
}

void TcpServerTransport::disconnect()
{
#ifdef TCP_SERVER_USE_ASYNC
    // Linux: Stop async client (also closes the socket)
    if (async_client_.is_running())
    {
        async_client_.stop();
    }
#else
    // Windows: Close client socket directly
    if (is_valid_socket(client_fd_))
    {
        close_socket(client_fd_);
        client_fd_ = invalid_socket();
    }
#endif

    if (is_valid_socket(listen_fd_))
    {
        close_socket(listen_fd_);
        listen_fd_ = invalid_socket();
    }

    out_parts_.clear();
    in_parts_.clear();
    in_next_ = 0;
    state_ = TcpServerState::Idle;
}

void TcpServerTransport::send(const void* data, size_t len, bool more)
{
    if (state_ != TcpServerState::Connected)
        throw std::runtime_error("TcpServerTransport: no client connected");

    out_parts_.emplace_back(static_cast<const char*>(data), len);
    if (!more)
    {
#ifdef TCP_SERVER_USE_ASYNC
        socket_t sock = async_client_.get_socket();
#else
        socket_t sock = client_fd_;
#endif
        if (!rawtcp::send_parts(sock, out_parts_))
        {
            out_parts_.clear();
            // Don't throw during shutdown - the write may have succeeded but shutdown flag caused false return
            if (!ShutdownManager::is_shutdown())
            {
                throw std::runtime_error(make_socket_error_str("TcpServerTransport: send_parts failed"));
            }
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
    // If we've consumed all cached parts, fetch a new message
    if (in_next_ >= in_parts_.size())
    {
        in_parts_.clear();
        in_next_ = 0;

#ifdef TCP_SERVER_USE_ASYNC
        // Linux: Wait for a message from the async queue
        if (!async_client_.wait_recv(in_parts_, 5000))
        {
            // Check connection status only after failing to get data
            // (data may have been queued before disconnect was detected)
            if (!async_client_.is_running() || state_ != TcpServerState::Connected)
            {
                throw std::runtime_error("TcpServerTransport: client disconnected");
            }
            // Timeout - return without filling data
            return;
        }
#else
        // Windows: Direct blocking recv with timeout
        rawtcp::ReadResult result = rawtcp::recv_parts_ex(client_fd_, in_parts_, 5000);
        if (result != rawtcp::ReadResult::Success)
        {
            if (result == rawtcp::ReadResult::Disconnected || !is_valid_socket(client_fd_))
            {
                throw std::runtime_error("TcpServerTransport: client disconnected");
            }
            if (result == rawtcp::ReadResult::Timeout)
            {
                // Timeout - return without filling data
                return;
            }
            throw std::runtime_error("TcpServerTransport: recv error");
        }
#endif
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
    // If we've consumed all cached parts, fetch a new message
    if (in_next_ >= in_parts_.size())
    {
        in_parts_.clear();
        in_next_ = 0;

#ifdef TCP_SERVER_USE_ASYNC
        // Linux: Wait for a message from the async queue
        if (!async_client_.wait_recv(in_parts_, 5000))
        {
            // Check connection status only after failing to get data
            // (data may have been queued before disconnect was detected)
            if (!async_client_.is_running() || state_ != TcpServerState::Connected)
            {
                throw std::runtime_error("TcpServerTransport: client disconnected");
            }
            // Timeout - return empty string
            return "";
        }
#else
        // Windows: Direct blocking recv with timeout
        rawtcp::ReadResult result = rawtcp::recv_parts_ex(client_fd_, in_parts_, 5000);
        if (result != rawtcp::ReadResult::Success)
        {
            if (result == rawtcp::ReadResult::Disconnected || !is_valid_socket(client_fd_))
            {
                throw std::runtime_error("TcpServerTransport: client disconnected");
            }
            // Timeout or error - return empty string
            return "";
        }
#endif
    }

    return in_parts_[in_next_++];
}

bool TcpServerTransport::has_pending_connection() const
{
    if (!is_valid_socket(listen_fd_))
        return false;

    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(listen_fd_, &rfds);

    // Non-blocking check (0 timeout)
    timeval tv{0, 0};
    int sel = select(listen_fd_ + 1, &rfds, nullptr, nullptr, &tv);
    return sel > 0;
}

bool TcpServerTransport::recv_multipart(std::vector<std::string>& parts)
{
    if (state_ != TcpServerState::Connected)
        return false;

    // Clear cache and fetch a fresh multipart message
    in_parts_.clear();
    in_next_ = 0;

#ifdef TCP_SERVER_USE_ASYNC
    // Linux: Wait for a message from the async socket with timeout
    // Use short timeout (50ms) to check for new connections more frequently
    while (!ShutdownManager::is_shutdown())
    {
        if (async_client_.wait_recv(parts, 50))
        {
            return true;
        }

        // Timeout or error - check if client is still connected
        if (!async_client_.is_running())
        {
            // Client disconnected
            return false;
        }

        // Check if a new client is trying to connect
        // If so, return false to trigger reconnection handling
        if (has_pending_connection())
        {
            mv_log("[TcpServerTransport] New connection pending, triggering reconnection");
            return false;
        }

        // Just a timeout, continue waiting
    }

    return false;
#else
    // Windows: Direct blocking recv (sync pattern)
    // Use short timeout (100ms) to check for shutdown and new connections
    while (!ShutdownManager::is_shutdown())
    {
        rawtcp::ReadResult result = rawtcp::recv_parts_ex(client_fd_, parts, 100);
        if (result == rawtcp::ReadResult::Success)
        {
            return true;
        }

        if (result == rawtcp::ReadResult::Disconnected)
        {
            // Client disconnected
            return false;
        }

        if (result == rawtcp::ReadResult::Error)
        {
            // Socket error
            return false;
        }

        // Timeout - check if a new client is trying to connect
        if (has_pending_connection())
        {
            mv_log("[TcpServerTransport] New connection pending, triggering reconnection");
            return false;
        }

        // Just a timeout, continue waiting
    }

    return false;
#endif
}
