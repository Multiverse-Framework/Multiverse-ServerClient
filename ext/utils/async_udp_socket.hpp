#pragma once

#include "raw_udp.hpp"
#include "socket_utils.hpp"
#include "log_utils.hpp"

#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <vector>
#include <string>
#include <atomic>
#include <functional>

/**
 * @brief Message received from UDP with sender address info.
 */
struct UdpMessage
{
    std::vector<std::string> parts;
    sockaddr_storage from_addr;
    socklen_t from_len;
};

/**
 * @brief Asynchronous UDP socket wrapper with background receive thread.
 *
 * This class wraps a UDP socket and provides:
 * - Background thread that continuously receives data
 * - Thread-safe message queue for incoming messages
 * - Non-blocking recv operations
 * - Tracks sender address for server-side use
 */
class AsyncUdpSocket
{
public:
    using MessageCallback = std::function<void(const UdpMessage&)>;

    AsyncUdpSocket()
        : sock_(invalid_socket())
        , running_(false)
        , timeout_ms_(10)
    {
    }

    ~AsyncUdpSocket()
    {
        stop();
    }

    // Delete copy constructor and assignment
    AsyncUdpSocket(const AsyncUdpSocket&) = delete;
    AsyncUdpSocket& operator=(const AsyncUdpSocket&) = delete;

    /**
     * @brief Start async receive thread for the given socket.
     * @param sock Socket to receive from (does NOT take ownership - caller must keep socket alive)
     * @param timeout_ms Timeout for recv operations (default 10ms for fast polling)
     */
    void start(socket_t sock, int timeout_ms = 10)
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

        recv_thread_ = std::thread(&AsyncUdpSocket::recv_thread_func, this);
    }

    /**
     * @brief Stop the receive thread.
     * Note: Does NOT close the socket - caller is responsible for that.
     */
    void stop()
    {
        running_ = false;

        // Always wake up any waiting threads to prevent deadlock
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            queue_cv_.notify_all();
        }

        // Always join thread if joinable
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

    /**
     * @brief Check if the socket is running.
     */
    bool is_running() const
    {
        return running_;
    }

    /**
     * @brief Check if there are messages available.
     */
    bool has_messages() const
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        return !message_queue_.empty();
    }

    /**
     * @brief Get the number of queued messages.
     */
    size_t queue_size() const
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        return message_queue_.size();
    }

    /**
     * @brief Clear all queued messages.
     * Call this after client disconnect to avoid processing stale messages.
     */
    void clear_queue()
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        while (!message_queue_.empty())
        {
            message_queue_.pop();
        }
    }

    /**
     * @brief Try to get a message from the queue (non-blocking).
     * @param out Output message to receive
     * @return true if a message was retrieved, false if queue is empty
     */
    bool try_recv(UdpMessage& out)
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        if (message_queue_.empty())
            return false;

        out = std::move(message_queue_.front());
        message_queue_.pop();
        return true;
    }

    /**
     * @brief Wait for a message from the queue (blocking with optional timeout).
     * @param out Output message to receive
     * @param timeout_ms Timeout in milliseconds (-1 for infinite wait)
     * @return true if a message was retrieved, false on timeout or shutdown
     */
    bool wait_recv(UdpMessage& out, int timeout_ms = -1)
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);

        if (timeout_ms < 0)
        {
            // Wait indefinitely
            queue_cv_.wait(lock, [this] { return !message_queue_.empty() || !running_; });
        }
        else
        {
            // Wait with timeout
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

    /**
     * @brief Convenience method to wait and get just the parts.
     */
    bool wait_recv(
        std::vector<std::string>& parts, sockaddr_storage& from_addr, socklen_t& from_len, int timeout_ms = -1)
    {
        UdpMessage msg;
        if (!wait_recv(msg, timeout_ms))
            return false;

        parts = std::move(msg.parts);
        from_addr = msg.from_addr;
        from_len = msg.from_len;
        return true;
    }

    /**
     * @brief Get the underlying socket (for sending).
     */
    socket_t get_socket() const
    {
        return sock_;
    }

private:
    void recv_thread_func()
    {
        mv_log("[AsyncUdpSocket] Receive thread started for socket %d", static_cast<int>(sock_));

        while (running_ && !ShutdownManager::is_shutdown())
        {
            UdpMessage msg;
            msg.from_len = sizeof(msg.from_addr);
            std::memset(&msg.from_addr, 0, sizeof(msg.from_addr));

            // Try to receive data (with short timeout for fast polling)
            if (rawudp::recv_parts_from(
                    sock_, msg.parts, reinterpret_cast<sockaddr*>(&msg.from_addr), &msg.from_len, timeout_ms_))
            {
                mv_log("[AsyncUdpSocket] Received %zu parts", msg.parts.size());

                // Add to queue
                {
                    std::lock_guard<std::mutex> lock(queue_mutex_);
                    message_queue_.push(std::move(msg));
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

        mv_log("[AsyncUdpSocket] Receive thread stopped for socket %d", static_cast<int>(sock_));
    }

    socket_t sock_;
    std::atomic<bool> running_;
    int timeout_ms_;

    std::thread recv_thread_;

    mutable std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::queue<UdpMessage> message_queue_;
};
