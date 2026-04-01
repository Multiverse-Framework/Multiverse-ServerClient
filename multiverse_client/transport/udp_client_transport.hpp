#pragma once
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <atomic>
#include "utils/raw_udp.hpp"
#include "client_transport.hpp"

/**
 * @brief Simple async UDP receiver for unconnected UDP sockets.
 * Uses recvfrom() to accept packets from any source.
 */
class AsyncUdpClientSocket
{
public:
    AsyncUdpClientSocket();
    ~AsyncUdpClientSocket();

    void start(socket_t sock, int timeout_ms = 10);
    void stop();
    bool is_running() const
    {
        return running_;
    }
    bool wait_recv(std::vector<std::string>& out, int timeout_ms = -1);
    socket_t get_socket() const
    {
        return sock_;
    }

private:
    void recv_thread_func();

    socket_t sock_;
    std::atomic<bool> running_;
    int timeout_ms_;
    std::thread recv_thread_;
    mutable std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::queue<std::vector<std::string>> message_queue_;
};

class UdpClientTransport : public IClientTransport
{
public:
    UdpClientTransport();
    ~UdpClientTransport();

    ClientTransportType type() const override
    {
        return ClientTransportType::Udp;
    }

    void connect(const std::string& endpoint) override;
    void disconnect() override;
    void send(const void* data, size_t len, bool more) override;
    void send_text(const std::string& s, bool more) override;
    void recv(void* data, size_t len) override;
    std::string recv_text() override;
    bool recv_multipart(std::vector<std::string>& parts) override;

private:
    socket_t sock_;
    sockaddr_storage server_addr_; // Server address for sendto()
    socklen_t server_addr_len_;
    AsyncUdpClientSocket async_sock_;
    std::vector<std::string> out_parts_;
    std::vector<std::string> in_parts_;
    size_t in_next_;
};
