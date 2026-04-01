#include "udp_client_transport.hpp"
#include "utils/socket_utils.hpp"
#include "utils/raw_udp.hpp"
#include "utils/general.hpp"
#include "utils/log_utils.hpp"

#include <stdexcept>
#include <cstring>
#include <chrono>
#include <thread>

// ============================================================================
// AsyncUdpClientSocket implementation
// ============================================================================

AsyncUdpClientSocket::AsyncUdpClientSocket()
    : sock_(invalid_socket())
    , running_(false)
    , timeout_ms_(10)
{
}

AsyncUdpClientSocket::~AsyncUdpClientSocket()
{
    stop();
}

void AsyncUdpClientSocket::start(socket_t sock, int timeout_ms)
{
    // Always stop first to ensure clean state, even if running_ is already false
    // (thread might still be joinable from previous run)
    if (running_ || recv_thread_.joinable())
    {
        stop();
    }

    sock_ = sock;
    timeout_ms_ = timeout_ms;
    running_ = true;

    recv_thread_ = std::thread(&AsyncUdpClientSocket::recv_thread_func, this);
}

void AsyncUdpClientSocket::stop()
{
    running_ = false;

    // Always notify waiters to prevent deadlock
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        queue_cv_.notify_all();
    }

    if (recv_thread_.joinable())
    {
        recv_thread_.join();
    }

    // Clear queue
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        while (!message_queue_.empty())
        {
            message_queue_.pop();
        }
    }

    sock_ = invalid_socket();
}

bool AsyncUdpClientSocket::wait_recv(std::vector<std::string>& out, int timeout_ms)
{
    std::unique_lock<std::mutex> lock(queue_mutex_);

    if (timeout_ms < 0)
    {
        queue_cv_.wait(lock, [this] { return !message_queue_.empty() || !running_; });
    }
    else
    {
        auto timeout = std::chrono::milliseconds(timeout_ms);
        if (!queue_cv_.wait_for(lock, timeout, [this] { return !message_queue_.empty() || !running_; }))
        {
            return false; // Timeout
        }
    }

    // Return data from queue even if running_ is false
    if (message_queue_.empty())
        return false;

    out = std::move(message_queue_.front());
    message_queue_.pop();
    return true;
}

void AsyncUdpClientSocket::recv_thread_func()
{
    mv_log("[AsyncUdpClientSocket] Receive thread started for socket %d", static_cast<int>(sock_));

    while (running_ && !ShutdownManager::is_shutdown())
    {
        std::vector<std::string> parts;

        // Use recvfrom to accept packets from ANY source (not just connected peer)
        // This is needed because the server may respond from a different port
        if (rawudp::recv_parts_from(sock_, parts, nullptr, nullptr, timeout_ms_))
        {
            mv_log("[AsyncUdpClientSocket] Received %zu parts", parts.size());

            // Add to queue
            {
                std::lock_guard<std::mutex> lock(queue_mutex_);
                message_queue_.push(std::move(parts));
                queue_cv_.notify_one();
            }
        }
        // Timeout is normal - just continue polling
    }

    // Notify any waiters that the thread is stopping
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        queue_cv_.notify_all();
    }

    mv_log("[AsyncUdpClientSocket] Receive thread stopped for socket %d", static_cast<int>(sock_));
}

// ============================================================================
// UdpClientTransport implementation
// ============================================================================

UdpClientTransport::UdpClientTransport()
    : sock_(invalid_socket())
    , server_addr_len_(0)
    , in_next_(0)
{
    std::memset(&server_addr_, 0, sizeof(server_addr_));
}

UdpClientTransport::~UdpClientTransport()
{
    disconnect();
}

void UdpClientTransport::connect(const std::string& endpoint)
{
    if (is_valid_socket(sock_))
        disconnect();

    auto [host, port] = split_host_port(endpoint);

    if (port.empty())
        throw std::runtime_error("UdpClientTransport: port is missing in endpoint " + endpoint);

    addrinfo hints{};
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_family = AF_UNSPEC;

    addrinfo* res = nullptr;
    int rc = ::getaddrinfo(host.c_str(), port.c_str(), &hints, &res);
    if (rc != 0 || !res)
    {
        throw std::runtime_error(make_socket_error_str("UdpClientTransport: getaddrinfo failed for " + endpoint));
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

    // Create unconnected UDP socket and store server address
    // We don't use connect() so we can receive from any source (server may respond from different port)
    for (auto* p = res; p; p = p->ai_next)
    {
        socket_t cand = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (!is_valid_socket(cand))
            continue;

        set_socket_options(cand, false, true); // UDP doesn't need TCP_NODELAY

        // Store the server address for sendto()
        std::memcpy(&server_addr_, p->ai_addr, p->ai_addrlen);
        server_addr_len_ = static_cast<socklen_t>(p->ai_addrlen);
        sock_ = cand;

        // Start async receive thread
        async_sock_.start(sock_, 10); // 10ms polling

        return;
    }

    throw std::runtime_error(make_socket_error_str("UdpClientTransport: failed to create socket for " + endpoint));
}

void UdpClientTransport::disconnect()
{
    // Stop async socket first
    async_sock_.stop();

    if (is_valid_socket(sock_))
    {
        close_socket(sock_);
        sock_ = invalid_socket();
    }
    server_addr_len_ = 0;
    std::memset(&server_addr_, 0, sizeof(server_addr_));
    out_parts_.clear();
    in_parts_.clear();
    in_next_ = 0;
}

void UdpClientTransport::send(const void* data, size_t len, bool more)
{
    if (!is_valid_socket(sock_))
        throw std::runtime_error("UdpClientTransport: not connected");

    out_parts_.emplace_back(static_cast<const char*>(data), len);
    if (!more)
    {
        // Use sendto() with stored server address
        if (!rawudp::send_parts_to(
                sock_, out_parts_, reinterpret_cast<const sockaddr*>(&server_addr_), server_addr_len_))
        {
            out_parts_.clear();
            // Don't throw during shutdown
            if (!ShutdownManager::is_shutdown())
            {
                throw std::runtime_error(make_socket_error_str("UdpClientTransport: send_parts_to failed"));
            }
        }
        out_parts_.clear();
    }
}

void UdpClientTransport::send_text(const std::string& s, bool more)
{
    send(s.data(), s.size(), more);
}

void UdpClientTransport::recv(void* data, size_t len)
{
    if (in_next_ >= in_parts_.size())
    {
        in_parts_.clear();
        in_next_ = 0;

        // Wait for a message from the async socket (allow time for network round-trip)
        if (!async_sock_.wait_recv(in_parts_, 500))
        {
            if (!async_sock_.is_running() || !is_valid_socket(sock_))
            {
                throw std::runtime_error("UdpClientTransport: not connected");
            }
            throw std::runtime_error(make_socket_error_str("UdpClientTransport: recv_parts failed"));
        }
    }

    const std::string& frame = in_parts_[in_next_++];
    if (frame.size() != len)
    {
        throw std::runtime_error("UdpClientTransport: recv size mismatch (expected " + std::to_string(len) + ", got " +
                                 std::to_string(frame.size()) + ")");
    }
    std::memcpy(data, frame.data(), len);
}

std::string UdpClientTransport::recv_text()
{
    if (in_next_ >= in_parts_.size())
    {
        in_parts_.clear();
        in_next_ = 0;

        // Wait for a message from the async socket (allow time for network round-trip)
        if (!async_sock_.wait_recv(in_parts_, 500))
        {
            if (!async_sock_.is_running() || !is_valid_socket(sock_))
            {
                throw std::runtime_error("UdpClientTransport: not connected");
            }
            throw std::runtime_error("UdpClientTransport: recv_parts failed");
        }
    }

    return in_parts_[in_next_++];
}

bool UdpClientTransport::recv_multipart(std::vector<std::string>& parts)
{
    if (!is_valid_socket(sock_))
        return false;

    in_parts_.clear();
    in_next_ = 0;

    // Wait for a message from the async socket (blocking)
    return async_sock_.wait_recv(parts, -1);
}
