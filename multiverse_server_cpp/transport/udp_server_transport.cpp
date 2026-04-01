#include "udp_server_transport.hpp"

#include <cstring>
#include <stdexcept>

#include "utils/raw_udp.hpp"
#include "utils/socket_utils.hpp"
#include "utils/general.hpp"
#include "utils/log_utils.hpp"

UdpServerTransport::UdpServerTransport()
    : state_(UdpServerState::Idle)
    , sock_(invalid_socket())
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
    // Clean up existing socket if any
    if (is_valid_socket(sock_))
        disconnect();

    auto [host, port] = split_host_port(endpoint);

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

    // Try each address until one succeeds
    for (auto* p = res; p; p = p->ai_next)
    {
        socket_t cand = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (!is_valid_socket(cand))
            continue;

        set_socket_options(cand, false, true); // UDP doesn't need TCP_NODELAY, but needs large buffer

        if (::bind(cand, p->ai_addr, static_cast<socklen_t>(p->ai_addrlen)) == 0)
        {
            sock_ = cand;
            state_ = UdpServerState::Bound;
            peer_len_ = 0;
            std::memset(&peer_addr_, 0, sizeof(peer_addr_));

#ifdef UDP_SERVER_USE_ASYNC
            // Start async receive thread (Linux)
            async_sock_.start(sock_, 10); // 10ms polling
#endif

            return;
        }

        close_socket(cand);
    }

    throw std::runtime_error(make_socket_error_str("UdpServerTransport: bind failed on " + endpoint));
}

bool UdpServerTransport::accept()
{
    // UDP: "accept" conceptually just means we'll learn the peer on first recv.
    return true;
}

void UdpServerTransport::disconnect()
{
#ifdef UDP_SERVER_USE_ASYNC
    // Stop async socket first (Linux)
    async_sock_.stop();
#endif

    if (is_valid_socket(sock_))
    {
        close_socket(sock_);
        sock_ = invalid_socket();
    }

    peer_len_ = 0;
    std::memset(&peer_addr_, 0, sizeof(peer_addr_));

    out_parts_.clear();
    in_parts_.clear();
    in_next_ = 0;
    state_ = UdpServerState::Idle;
}

void UdpServerTransport::reset_peer()
{
    // Reset peer state without closing the socket (like TCP accept after disconnect)
    // This allows the server to continue receiving from new clients

    peer_len_ = 0;
    std::memset(&peer_addr_, 0, sizeof(peer_addr_));

    out_parts_.clear();
    in_parts_.clear();
    in_next_ = 0;

    // Go back to Bound state (listening for new client)
    if (state_ != UdpServerState::Idle)
    {
        state_ = UdpServerState::Bound;
    }
}

void UdpServerTransport::send(const void* data, size_t len, bool more)
{
    if (state_ == UdpServerState::Idle)
        throw std::runtime_error("UdpServerTransport: socket not bound");

    if (state_ != UdpServerState::Connected)
        throw std::runtime_error("UdpServerTransport: peer unknown, no packet received yet");

    out_parts_.emplace_back(static_cast<const char*>(data), len);
    if (!more)
    {
        if (!rawudp::send_parts_to(sock_, out_parts_, reinterpret_cast<const sockaddr*>(&peer_addr_), peer_len_))
        {
            out_parts_.clear();
            // Don't throw during shutdown
            if (!ShutdownManager::is_shutdown())
            {
                throw std::runtime_error(make_socket_error_str("UdpServerTransport: send_parts_to failed"));
            }
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
    if (state_ == UdpServerState::Idle)
        throw std::runtime_error("UdpServerTransport: socket not bound");

    if (in_next_ >= in_parts_.size())
    {
        in_parts_.clear();
        in_next_ = 0;

#ifdef UDP_SERVER_USE_ASYNC
        // Linux: Wait for a message from the async socket
        UdpMessage msg;
        if (!async_sock_.wait_recv(msg, 1000))
        {
            if (ShutdownManager::is_shutdown() || !async_sock_.is_running())
            {
                throw std::runtime_error("UdpServerTransport: shutdown or socket closed");
            }
            // Timeout - throw
            throw std::runtime_error("UdpServerTransport: recv timeout");
        }

        // Update peer address
        peer_addr_ = msg.from_addr;
        peer_len_ = msg.from_len;
        state_ = UdpServerState::Connected;
        in_parts_ = std::move(msg.parts);
#else
        // Windows: Direct blocking recv with timeout
        sockaddr_storage from_addr;
        socklen_t from_len = sizeof(from_addr);
        std::memset(&from_addr, 0, sizeof(from_addr));

        if (!rawudp::recv_parts_from(sock_, in_parts_, reinterpret_cast<sockaddr*>(&from_addr), &from_len, 1000))
        {
            if (ShutdownManager::is_shutdown())
            {
                throw std::runtime_error("UdpServerTransport: shutdown or socket closed");
            }
            // Timeout - throw
            throw std::runtime_error("UdpServerTransport: recv timeout");
        }

        // Update peer address
        peer_addr_ = from_addr;
        peer_len_ = from_len;
        state_ = UdpServerState::Connected;
#endif
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
    if (state_ == UdpServerState::Idle)
        throw std::runtime_error("UdpServerTransport: socket not bound");

    if (in_next_ >= in_parts_.size())
    {
        in_parts_.clear();
        in_next_ = 0;

#ifdef UDP_SERVER_USE_ASYNC
        // Linux: Wait for a message from the async socket
        UdpMessage msg;
        if (!async_sock_.wait_recv(msg, 2000))
        {
            if (ShutdownManager::is_shutdown() || !async_sock_.is_running())
            {
                return ""; // Return empty on shutdown
            }
            // Timeout - return empty
            return "";
        }

        // Update peer address
        peer_addr_ = msg.from_addr;
        peer_len_ = msg.from_len;
        state_ = UdpServerState::Connected;
        in_parts_ = std::move(msg.parts);
#else
        // Windows: Direct blocking recv with timeout
        sockaddr_storage from_addr;
        socklen_t from_len = sizeof(from_addr);
        std::memset(&from_addr, 0, sizeof(from_addr));

        if (!rawudp::recv_parts_from(sock_, in_parts_, reinterpret_cast<sockaddr*>(&from_addr), &from_len, 2000))
        {
            if (ShutdownManager::is_shutdown())
            {
                return ""; // Return empty on shutdown
            }
            // Timeout - return empty
            return "";
        }

        // Update peer address
        peer_addr_ = from_addr;
        peer_len_ = from_len;
        state_ = UdpServerState::Connected;
#endif
    }

    return in_parts_[in_next_++];
}

bool UdpServerTransport::recv_multipart(std::vector<std::string>& parts)
{
    if (state_ == UdpServerState::Idle)
        return false;

    in_parts_.clear();
    in_next_ = 0;

#ifdef UDP_SERVER_USE_ASYNC
    // Linux: Wait for a message from the async socket (async queue pattern)
    while (!ShutdownManager::is_shutdown())
    {
        UdpMessage msg;
        if (async_sock_.wait_recv(msg, 2000))
        {
            // Check if peer address changed (new client)
            if (state_ == UdpServerState::Connected && peer_len_ > 0)
            {
                bool peer_changed =
                    (msg.from_len != peer_len_) || (std::memcmp(&msg.from_addr, &peer_addr_, peer_len_) != 0);
                if (peer_changed)
                {
                    mv_log("[UdpServerTransport] New peer detected, updating peer address");
                }
            }

            // Update peer address
            peer_addr_ = msg.from_addr;
            peer_len_ = msg.from_len;
            state_ = UdpServerState::Connected;
            parts = std::move(msg.parts);
            return true;
        }

        // Timeout or error - check if still running
        if (!async_sock_.is_running())
        {
            return false;
        }

        // Just a timeout, continue waiting
    }

    return false;
#else
    // Windows: Direct blocking recv (sync pattern - no stale data accumulation)
    while (!ShutdownManager::is_shutdown())
    {
        sockaddr_storage from_addr;
        socklen_t from_len = sizeof(from_addr);
        std::memset(&from_addr, 0, sizeof(from_addr));

        // Use 100ms timeout for responsive shutdown checking
        if (rawudp::recv_parts_from(sock_, in_parts_, reinterpret_cast<sockaddr*>(&from_addr), &from_len, 100))
        {
            // Check if peer address changed (new client)
            if (state_ == UdpServerState::Connected && peer_len_ > 0)
            {
                bool peer_changed = (from_len != peer_len_) || (std::memcmp(&from_addr, &peer_addr_, peer_len_) != 0);
                if (peer_changed)
                {
                    mv_log("[UdpServerTransport] New peer detected, updating peer address");
                }
            }

            // Update peer address
            peer_addr_ = from_addr;
            peer_len_ = from_len;
            state_ = UdpServerState::Connected;
            parts = std::move(in_parts_);
            return true;
        }

        // Timeout - just continue waiting (no messages queued, so no stale data issue)
    }

    return false;
#endif
}
