#pragma once
#include <string>
#include <vector>

#include "server_transport.hpp"
#include "utils/raw_udp.hpp"
#include "utils/socket_utils.hpp"

// Linux: Use async queue pattern (works well)
// Windows: Use direct blocking recv (fixes stale data issue)
#ifdef _WIN32
#define UDP_SERVER_USE_SYNC 1
#else
#define UDP_SERVER_USE_ASYNC 1
#include "utils/async_udp_socket.hpp"
#endif

/**
 * @brief State of the UDP server transport connection.
 */
enum class UdpServerState
{
    Idle,     ///< No socket created
    Bound,    ///< Socket bound and listening for first packet
    Connected ///< Peer address known, ready for communication
};

/**
 * @brief UDP Server Transport.
 *
 * On Linux: Uses AsyncUdpSocket with background receive thread (async queue pattern).
 * On Windows: Uses direct recvfrom() with select() timeout (sync pattern).
 *
 * The sync pattern on Windows fixes stale data accumulation issues observed in tests.
 */
class UdpServerTransport : public IServerTransport
{
public:
    UdpServerTransport();
    ~UdpServerTransport();

    ServerTransportType type() const override
    {
        return ServerTransportType::Udp;
    }

    void listen(const std::string& endpoint) override;
    bool accept() override;
    void disconnect() override;
    void send(const void* data, size_t len, bool more) override;
    void send_text(const std::string& s, bool more) override;
    void recv(void* data, size_t len) override;
    std::string recv_text() override;
    bool recv_multipart(std::vector<std::string>& parts) override;

    /**
     * @brief Reset peer state after client closes (like TCP disconnect).
     *
     * Call this after receiving a close signal (message_spec_int = 0) to
     * prepare for the next client. Unlike disconnect(), this keeps the
     * socket open and async thread running (on Linux).
     */
    void reset_peer();

private:
    UdpServerState state_;
    socket_t sock_;
    sockaddr_storage peer_addr_;
    socklen_t peer_len_;

#ifdef UDP_SERVER_USE_ASYNC
    AsyncUdpSocket async_sock_;
#endif

    std::vector<std::string> out_parts_;
    std::vector<std::string> in_parts_;
    size_t in_next_;
};
