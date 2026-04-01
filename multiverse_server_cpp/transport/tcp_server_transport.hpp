#pragma once
#include "server_transport.hpp"
#include "utils/raw_tcp.hpp"
#include "utils/socket_utils.hpp"

// Linux: Use async queue pattern (works well)
// Windows: Use direct blocking recv (fixes stale data issue)
#ifdef _WIN32
#define TCP_SERVER_USE_SYNC 1
#else
#define TCP_SERVER_USE_ASYNC 1
#include "utils/async_tcp_socket.hpp"
#endif

/**
 * @brief State of the TCP server transport connection.
 */
enum class TcpServerState
{
    Idle,      ///< No socket created
    Listening, ///< Server socket bound and listening
    Connected  ///< Client connected and ready for communication
};

/**
 * @brief TCP Server Transport.
 *
 * On Linux: Uses AsyncTcpSocket with background receive thread (async queue pattern).
 * On Windows: Uses direct recv() with select() timeout (sync pattern).
 *
 * The sync pattern on Windows fixes stale data accumulation issues observed in tests.
 */
class TcpServerTransport : public IServerTransport
{
public:
    TcpServerTransport();
    ~TcpServerTransport() override;

    ServerTransportType type() const override
    {
        return ServerTransportType::Tcp;
    }

    void listen(const std::string& ep) override;
    bool accept() override;
    void disconnect() override;
    void send(const void* data, size_t len, bool more) override;
    void send_text(const std::string& s, bool more) override;
    void recv(void* data, size_t len) override;
    std::string recv_text() override;
    bool recv_multipart(std::vector<std::string>& parts) override;

    /// Check if there's a pending connection on the listen socket (non-blocking)
    bool has_pending_connection() const;

private:
    TcpServerState state_;
    socket_t listen_fd_;

#ifdef TCP_SERVER_USE_ASYNC
    AsyncTcpSocket async_client_;
#else
    socket_t client_fd_; // Direct client socket for sync mode
#endif

    std::vector<std::string> out_parts_;
    std::vector<std::string> in_parts_; // Cached received parts
    size_t in_next_;                    // Index of next part to consume
};
