#pragma once

#include "raw_tcp.hpp"
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

#ifdef _WIN32
#include <winsock2.h>
#endif

/**
 * @brief Asynchronous TCP socket wrapper with background receive thread.
 *
 * This class wraps a socket and provides:
 * - Background thread that continuously receives data
 * - Thread-safe message queue for incoming messages
 * - Non-blocking recv operations
 * - Optional callback notification when data arrives
 */
class AsyncTcpSocket
{
public:
    using MessageCallback = std::function<void(const std::vector<std::string>&)>;

    AsyncTcpSocket()
        : sock_(invalid_socket())
        , running_(false)
        , timeout_ms_(10) // Fast polling like UDP
    {
    }

    ~AsyncTcpSocket()
    {
        stop();
    }

    // Delete copy constructor and assignment
    AsyncTcpSocket(const AsyncTcpSocket&) = delete;
    AsyncTcpSocket& operator=(const AsyncTcpSocket&) = delete;

    /**
     * @brief Start async receive thread for the given socket.
     * @param sock Socket to receive from (takes ownership)
     * @param timeout_ms Timeout for recv operations (default 10ms for fast polling)
     */
    void start(socket_t sock, int timeout_ms = 10)
    {
        if (is_valid_socket(sock_))
        {
            stop();
        }

        sock_ = sock;
        timeout_ms_ = timeout_ms;
        running_ = true;

        recv_thread_ = std::thread(&AsyncTcpSocket::recv_thread_func, this);
    }

    /**
     * @brief Stop the receive thread and release the socket.
     */
    void stop()
    {
        // Always try to join the thread first, even if running_ is false
        // (the thread may have exited due to disconnect but not been joined yet)
        bool was_running = running_.exchange(false);

        if (was_running)
        {
            // Shutdown socket to wake up any blocked recv calls
            if (is_valid_socket(sock_))
            {
#ifdef _WIN32
                ::shutdown(sock_, SD_BOTH);
#else
                ::shutdown(sock_, SHUT_RDWR);
#endif
            }

            // Wake up any waiting threads
            {
                std::lock_guard<std::mutex> lock(queue_mutex_);
                queue_cv_.notify_all();
            }
        }

        // Always join thread if joinable (even if thread exited on its own)
        if (recv_thread_.joinable())
        {
            recv_thread_.join();
        }

        // Close socket
        if (is_valid_socket(sock_))
        {
            close_socket(sock_);
            sock_ = invalid_socket();
        }

        // Clear queue
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            while (!message_queue_.empty())
            {
                message_queue_.pop();
            }
        }
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
     * @brief Try to get a message from the queue (non-blocking).
     * @param out Output vector to receive the message parts
     * @return true if a message was retrieved, false if queue is empty
     */
    bool try_recv(std::vector<std::string>& out)
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
     * @param out Output vector to receive the message parts
     * @param timeout_ms Timeout in milliseconds (-1 for infinite wait)
     * @return true if a message was retrieved, false on timeout or shutdown
     */
    bool wait_recv(std::vector<std::string>& out, int timeout_ms = -1)
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

        // Return data from queue even if running_ is false (peer disconnected)
        // The data was received before disconnect, so it's still valid
        if (message_queue_.empty())
            return false;

        out = std::move(message_queue_.front());
        message_queue_.pop();
        return true;
    }

    /**
     * @brief Set a callback to be invoked when data arrives.
     * @param callback Function to call with received message parts
     */
    void set_callback(MessageCallback callback)
    {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        callback_ = std::move(callback);
    }

    /**
     * @brief Clear the callback.
     */
    void clear_callback()
    {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        callback_ = nullptr;
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
        mv_log("[AsyncTcpSocket] Receive thread started for socket %d", static_cast<int>(sock_));

        while (running_ && !ShutdownManager::is_shutdown())
        {
            std::vector<std::string> parts;

            // Try to receive data (blocking with timeout)
            rawtcp::ReadResult result = rawtcp::recv_parts_ex(sock_, parts, timeout_ms_);
            if (result == rawtcp::ReadResult::Success)
            {
                mv_log("[AsyncTcpSocket] Received %zu parts", parts.size());

                // Check if callback is set (need to copy before moving if so)
                bool has_callback = false;
                {
                    std::lock_guard<std::mutex> lock(callback_mutex_);
                    has_callback = (callback_ != nullptr);
                }

                // Add to queue (use move for efficiency)
                {
                    std::lock_guard<std::mutex> lock(queue_mutex_);
                    if (has_callback)
                    {
                        message_queue_.push(parts); // Copy if callback needs it
                    }
                    else
                    {
                        message_queue_.push(std::move(parts)); // Move if no callback
                    }
                    queue_cv_.notify_one();
                }

                // Invoke callback if set
                if (has_callback)
                {
                    std::lock_guard<std::mutex> lock(callback_mutex_);
                    if (callback_)
                    {
                        try
                        {
                            callback_(parts);
                        }
                        catch (const std::exception& e)
                        {
                            mv_log("[AsyncTcpSocket] Exception in callback: %s", e.what());
                        }
                    }
                }
            }
            else if (result == rawtcp::ReadResult::Timeout)
            {
                // Timeout - continue waiting
                if (!running_)
                    break;
            }
            else if (result == rawtcp::ReadResult::Disconnected)
            {
                // Client disconnected (recv returned 0)
                mv_log("[AsyncTcpSocket] Peer disconnected, stopping receive thread for socket %d",
                    static_cast<int>(sock_));
                running_ = false;
                break;
            }
            else
            {
                // Error — TCP stream is likely desynchronized (e.g. partial read failed).
                // We MUST stop immediately; continuing would read garbage from the stream.
                mv_log("[AsyncTcpSocket] Stream error on socket %d, stopping receive thread (stream desynchronized)",
                    static_cast<int>(sock_));
                running_ = false;
                break;
            }
        }

        // Notify any waiters that the thread is stopping
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            queue_cv_.notify_all();
        }

        mv_log("[AsyncTcpSocket] Receive thread stopped for socket %d", static_cast<int>(sock_));
    }

    socket_t sock_;
    std::atomic<bool> running_;
    int timeout_ms_;

    std::thread recv_thread_;

    mutable std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::queue<std::vector<std::string>> message_queue_;

    std::mutex callback_mutex_;
    MessageCallback callback_;
};
