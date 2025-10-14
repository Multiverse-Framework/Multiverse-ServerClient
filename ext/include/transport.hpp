#pragma once
#include "socket_utils.hpp"
#include "raw_tcp.hpp"
#include "raw_udp.hpp"
#include "log_utils.hpp"
#include <string>
#include <vector>
#include <cstdint>
#include <stdexcept>
#include <chrono>
#include <thread>
#include <deque>
#include <utility>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

enum class TransportType : unsigned char {
    Zmq,
    Tcp,
    Udp
};

#define USE_ZMQ
// ---- Transport Interface --------------------------------------------------
class ITransport
{
public:
    virtual ~ITransport() = default;
    /// Connect to a remote endpoint (client mode).
    virtual void connect(const std::string &endpoint) = 0;
    /// Disconnect from a remote endpoint.
    virtual void disconnect(const std::string &endpoint) = 0;
    /// Listen for incoming connections (server mode).
    virtual void listen(const std::string &endpoint) = 0;
    /// Accept an incoming connection (server mode). Returns false on failure.
    virtual bool accept() = 0;
    virtual void bind(const std::string &endpoint) = 0;
    virtual void unbind(const std::string &endpoint) = 0;
    /// Send a binary frame. If 'more' is false, the buffered message is sent.
    virtual void send(const void *data, size_t len, bool more) = 0;
    /// Receive a binary frame of an exact size.
    virtual void recv(void *data, size_t len) = 0;
    /// Send a text frame. If 'more' is false, the buffered message is sent.
    virtual void send_text(const std::string &s, bool more) = 0;
    /// Receive a text frame.
    virtual std::string recv_text() = 0;
    /// Receive a full multipart message. Returns false on disconnect.
    virtual bool recv_multipart(std::vector<std::string> &parts) = 0;
    /// Cross-transport sleep utility.
    static void sleep_ms(int ms)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    }
    TransportType get_transport_type() const { return transport_type_; }
    void set_transport_type(TransportType t) { transport_type_ = t; }
    uint16_t get_port(const std::string& port_s) {
        if (port_s.empty())
        {
            throw std::runtime_error("TcpTransport: missing port in endpoint");
        }
        unsigned long port_ul = 0;
        try
        {
            size_t idx = 0;
            port_ul = std::stoul(port_s, &idx, 10);
            if (idx != port_s.size())
                throw std::invalid_argument("non-digit characters found");
        }
        catch (const std::exception &e)
        {
            throw std::runtime_error("TcpTransport: invalid port \"" + port_s + "\" (" + e.what() + ")");
        }

        if (port_ul == 0 || port_ul > 65535)
        {
            throw std::runtime_error("TcpTransport: port out of range (" + std::to_string(port_ul) + ")");
        }

        return static_cast<uint16_t>(port_ul);
    }
    protected:
    TransportType transport_type_ = TransportType::Zmq;
};

// ---- ZMQ Transport Implementation ------------------------------------------
#include <zmq.h>
#include <zmq_addon.hpp>
class ZmqTransport : public ITransport
{
public:
    ZmqTransport(int type_)
    {
        set_transport_type(TransportType::Zmq);
        ctx_ = zmq_ctx_new();
        if (!ctx_)
            throw std::runtime_error("ZmqTransport: zmq_ctx_new failed");
        sock_ = zmq_socket(ctx_, type_);
        if (!sock_)
            throw std::runtime_error("ZmqTransport: zmq_socket failed");
    }
    ~ZmqTransport() override
    {
        if (sock_)
            zmq_close(sock_);
        if (ctx_)
        {
            zmq_ctx_shutdown(ctx_);
            zmq_ctx_term(ctx_);
        }
    }
    void connect(const std::string &endpoint) override
    {
        if (zmq_connect(sock_, endpoint.c_str()) != 0)
        {
            throw std::runtime_error("ZMQ connect failed: " + std::string(zmq_strerror(zmq_errno())));
        }
    }
    void disconnect(const std::string &endpoint) override
    {
        zmq_disconnect(sock_, endpoint.c_str());
    }
    void listen(const std::string & /*endpoint*/) override
    {
        throw std::runtime_error("ZmqTransport: server mode (listen/accept) not supported with REQ socket");
    }
    bool accept() override
    {
        throw std::runtime_error("ZmqTransport: server mode (listen/accept) not supported with REQ socket");
    }
    void bind(const std::string &endpoint) override
    {
        zmq_bind(sock_, endpoint.c_str());
    }
    void unbind(const std::string &endpoint) override
    {
        zmq_unbind(sock_, endpoint.c_str());
    }
    void send(const void *data, size_t len, bool more) override
    {
        if (zmq_send(sock_, data, len, more ? ZMQ_SNDMORE : 0) < 0)
        {
            throw std::runtime_error("ZMQ send failed: " + std::string(zmq_strerror(zmq_errno())));
        }
    }
    void send_text(const std::string &s, bool more) override
    {
        send(s.data(), s.size(), more);
    }
    void recv(void *data, size_t len) override
    {
        int rc = zmq_recv(sock_, data, len, 0);
        if (rc < 0)
        {
            throw std::runtime_error("ZMQ recv failed: " + std::string(zmq_strerror(zmq_errno())));
        }
        if (static_cast<size_t>(rc) != len)
        {
            throw std::runtime_error("ZMQ recv: size mismatch");
        }
    }
    std::string recv_text() override
    {
        zmq_msg_t msg;
        zmq_msg_init(&msg);
        if (zmq_msg_recv(&msg, sock_, 0) < 0)
        {
            zmq_msg_close(&msg);
            throw std::runtime_error("ZMQ recv_text failed: " + std::string(zmq_strerror(zmq_errno())));
        }
        std::string out(static_cast<char *>(zmq_msg_data(&msg)), zmq_msg_size(&msg));
        zmq_msg_close(&msg);
        return out;
    }

    bool recv_multipart(std::vector<std::string> &parts) override
    {
        zmq::socket_ref sref{zmq::from_handle, sock_};
        parts.clear();
        std::vector<zmq::message_t> msgs;
        auto ok = zmq::recv_multipart(sref, std::back_inserter(msgs), zmq::recv_flags::none);
        if (!ok)
            return false;
        parts.reserve(msgs.size());
        for (auto &m : msgs)
            parts.emplace_back(m.to_string());
        return true;
    }
    void *raw_socket() const { return sock_; }

private:
    void *ctx_{nullptr};
    void *sock_{nullptr};
    zmq::socket_t socket;
};

// ---- Raw TCP Transport Implementation -------------------------------------
class TcpTransport : public ITransport
{
private:
    enum class Mode
    {
        Idle,
        ClientConnected,
        ServerListening,
        ServerConnected
    };
    // Connection socket
    socket_t sockfd_{invalid_socket()};
    // Listening socket
    socket_t listen_fd_{invalid_socket()};
    std::string endpoint_;        // Peer endpoint description
    std::string endpoint_listen_; // Listening endpoint description
    bool dump_{true};
    Mode mode_{Mode::Idle};
    // Buffers for multipart messages
    std::vector<std::string> out_parts_;
    std::vector<std::string> in_parts_;
    size_t in_next_ = 0;

public:
    TcpTransport() {
        set_transport_type(TransportType::Tcp);
    }
    ~TcpTransport() override
    {
        disconnect("");
    }
    void set_dump(bool on) { dump_ = on; }
    void connect(const std::string &endpoint) override
    {
        disconnect("");

        // --- Simple retry ---
        constexpr size_t kMaxAttempts = 12;
        constexpr int kSleepMs = 200;
        constexpr int kConnectErrLog = 1;

        auto hp = split_host_port(endpoint);
        const std::string &host = hp.first;
        const std::string &port_s = hp.second;
        const uint16_t port = get_port(port_s);

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);

        // Try to interpret host as IPv4 first
        bool have_addr = (::inet_pton(AF_INET, host.c_str(), &addr.sin_addr) == 1);

        if (!have_addr)
        {
#ifdef _WIN32
            HOSTENT *he = ::gethostbyname(host.c_str());
            if (!he || he->h_addrtype != AF_INET)
            {
                throw std::runtime_error("TcpTransport: DNS failed for " + host);
            }
            std::memcpy(&addr.sin_addr, he->h_addr_list[0], he->h_length);
#else
            struct hostent *he = ::gethostbyname(host.c_str());
            if (!he || he->h_addrtype != AF_INET)
            {
                throw std::runtime_error("TcpTransport: DNS failed for " + host);
            }
            std::memcpy(&addr.sin_addr, he->h_addr_list[0], he->h_length);
#endif
        }

        int last_err = 0;

        for (size_t attempt = 1; attempt <= kMaxAttempts; ++attempt)
        {
            socket_t cand = ::socket(AF_INET, SOCK_STREAM, 0);
            if (!is_valid_socket(cand))
            {
#ifdef _WIN32
                last_err = WSAGetLastError();
#else
                last_err = errno;
#endif
                if (dump_ && kConnectErrLog)
                {
                    mv_log("[tcp] socket() failed (attempt %zu/%zu), err=%d",
                           attempt, kMaxAttempts, last_err);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(kSleepMs));
                continue;
            }

            set_common_sockopts(cand);

            if (::connect(cand, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == 0)
            {
                // success
                sockfd_ = cand;
                endpoint_ = endpoint;
                mode_ = Mode::ClientConnected;
                if (dump_)
                    mv_log("[tcp] connected to %s (attempt %zu/%zu)", endpoint_.c_str(), attempt, kMaxAttempts);
                return;
            }

#ifdef _WIN32
            last_err = WSAGetLastError();
#else
            last_err = errno;
#endif
            close_socket(cand);

            if (attempt < kMaxAttempts)
            {
                if (dump_ && kConnectErrLog)
                {
#ifdef _WIN32
                    mv_log("[tcp] connect failed (attempt %zu/%zu), err=WSA%d -> retry in %d ms",
                           attempt, kMaxAttempts, last_err, kSleepMs);
#else
                    mv_log("[tcp] connect failed (attempt %zu/%zu), err=%d (%s) -> retry in %d ms",
                           attempt, kMaxAttempts, last_err, strerror(last_err), kSleepMs);
#endif
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(kSleepMs));
            }
        }

        // final failure
        throw std::runtime_error(make_socket_error_str("TcpTransport: connect failed to " + endpoint));
    }
    // ==================== SERVER MODE ====================
    void listen(const std::string &endpoint) override
    {
        disconnect(""); // Clean up previous state
        auto hp = split_host_port(endpoint);
        const std::string &host = hp.first;
        const std::string &port_s = hp.second;
        bool is_any_host = (host == "*" || host.empty() || host == "0.0.0.0" || host == "::");

        addrinfo hints{};
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_flags = AI_PASSIVE;
        hints.ai_family = AF_UNSPEC;

        addrinfo *res = nullptr;
        if (getaddrinfo(is_any_host ? nullptr : host.c_str(), port_s.c_str(), &hints, &res) != 0)
        {
            throw std::runtime_error(make_socket_error_str("TcpTransport: getaddrinfo(bind) failed for " + endpoint));
        }

        struct AddrInfoGuard
        {
            addrinfo *&addr;
            ~AddrInfoGuard()
            {
                if (addr)
                    freeaddrinfo(addr);
            }
        } guard{res};

        for (auto *p = res; p; p = p->ai_next)
        {
            socket_t cand = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
            if (!is_valid_socket(cand))
                continue;
            int yes = 1;
            ::setsockopt(cand, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&yes), sizeof(yes));
            set_common_sockopts(cand);
            if (::bind(cand, p->ai_addr, static_cast<socklen_t>(p->ai_addrlen)) == 0 && ::listen(cand, 16) == 0)
            {
                listen_fd_ = cand;
                endpoint_listen_ = endpoint;
                mode_ = Mode::ServerListening;
                if (dump_)
                    mv_log("[tcp] listening on %s", endpoint_listen_.c_str());
                return;
            }
            close_socket(cand);
        }
        throw std::runtime_error(make_socket_error_str("TcpTransport: listen failed on " + endpoint));
    }
    bool accept() override
    {
        if (!is_valid_socket(listen_fd_))
        {
            throw std::runtime_error("TcpTransport: accept called but not listening");
        }
        close_conn_socket(); // Drop previous client
        clear_buffers();
        sockaddr_storage cliaddr{};
        socklen_t len = sizeof(cliaddr);
        socket_t cfd = ::accept(listen_fd_, reinterpret_cast<sockaddr *>(&cliaddr), &len);
        if (!is_valid_socket(cfd))
        {
            if (dump_)
                mv_log("[tcp] accept failed (errno=%d)", sock_errno());
            return false;
        }
        set_common_sockopts(cfd);
        sockfd_ = cfd;
        endpoint_ = describe_peer(cliaddr);
        mode_ = Mode::ServerConnected;
        if (dump_)
            mv_log("[tcp] accepted client %s", endpoint_.c_str());
        return true;
    }
    void bind(const std::string &endpoint) override
    {
        (void)endpoint;
        throw std::runtime_error("TcpTransport: please call listen");
    }
    void unbind(const std::string &endpoint) override
    {
        (void)endpoint;
        throw std::runtime_error("TcpTransport: no support");
    }
    // ==================== COMMON API ====================
    void disconnect(const std::string & /*endpoint*/) override
    {
        close_conn_socket();
        close_listen_socket();
        clear_buffers();
        mode_ = Mode::Idle;
    }
    void send(const void *data, size_t len, bool more) override
    {
        ensure_connected_or_throw();
        out_parts_.emplace_back(static_cast<const char *>(data), len);
        if (!more)
            flush_out_parts();
    }
    void send_text(const std::string &s, bool more) override
    {
        send(s.data(), s.size(), more);
    }

    void recv(void *data, size_t len) override
    {
        ensure_in_parts();
        if (in_next_ >= in_parts_.size())
        {
            throw std::runtime_error("TcpTransport: recv called with no frames available");
        }

        const std::string &frame = in_parts_[in_next_++];
        if (frame.size() != len)
        {
            throw std::runtime_error("TcpTransport: recv size mismatch (expected " +
                                     std::to_string(len) + ", got " + std::to_string(frame.size()) + ")");
        }
        std::memcpy(data, frame.data(), len);
    }
    std::string recv_text() override
    {
        ensure_in_parts();
        if (in_next_ >= in_parts_.size())
        {
            throw std::runtime_error("TcpTransport: recv_text called with no frames available");
        }
        return std::move(in_parts_[in_next_++]);
    }
    bool recv_multipart(std::vector<std::string> &parts) override
    {
        try
        {
            ensure_in_parts();
            if (in_parts_.empty())
                return false; // Peer closed connection
            parts = std::move(in_parts_);
            in_parts_.clear();
            in_next_ = 0;
            if (dump_)
                mv_log("[tcp] recv_multipart: %zu part(s)", parts.size());
            return true;
        }
        catch (const std::runtime_error &)
        {
            return false;
        }
    }

private:
    // ---- Private Helper Functions ----
    static std::pair<std::string, std::string> split_host_port(const std::string &endpoint)
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
        {
            return {endpoint, ""}; // No port found
        }
        return {endpoint.substr(0, host_end), endpoint.substr(host_end + 1)};
    }

    static std::string describe_peer(const sockaddr_storage &ss)
    {
        char host[NI_MAXHOST]{};
        char serv[NI_MAXSERV]{};
        if (getnameinfo(reinterpret_cast<const sockaddr *>(&ss), sizeof(ss),
                        host, sizeof(host), serv, sizeof(serv),
                        NI_NUMERICHOST | NI_NUMERICSERV) == 0)
        {
            return std::string(host) + ":" + std::string(serv);
        }
        return "<unknown_peer>";
    }
    static void set_common_sockopts(socket_t s)
    {
        int yes = 1;
        ::setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char *>(&yes), sizeof(yes));
        ::setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&yes), sizeof(yes));
    }

    void flush_out_parts()
    {
        ensure_connected_or_throw();
        if (out_parts_.empty())
            return;
        if (!rawtcp::send_parts(sockfd_, out_parts_))
        {
            throw std::runtime_error(make_socket_error_str("TcpTransport: send_parts failed"));
        }
        out_parts_.clear();
    }
    void ensure_in_parts()
    {
        ensure_connected_or_throw();
        if (in_next_ < in_parts_.size())
            return; // Frames still available
        in_parts_.clear();
        in_next_ = 0;

        std::vector<std::string> wire_parts;
        if (!rawtcp::recv_parts(sockfd_, wire_parts))
        {
            // Connection closed by peer or error
            close_conn_socket();
            throw std::runtime_error("TcpTransport: recv_parts failed (connection closed or error)");
        }
        in_parts_ = std::move(wire_parts);
    }
    void ensure_connected_or_throw() const
    {
        if (!is_valid_socket(sockfd_))
        {
            throw std::runtime_error("TcpTransport: not connected");
        }
    }

    void close_conn_socket()
    {
        if (is_valid_socket(sockfd_))
        {
            close_socket(sockfd_);
            sockfd_ = invalid_socket();
        }
        endpoint_.clear();
        if (mode_ == Mode::ClientConnected || mode_ == Mode::ServerConnected)
        {
            mode_ = is_valid_socket(listen_fd_) ? Mode::ServerListening : Mode::Idle;
        }
    }
    void close_listen_socket()
    {
        if (is_valid_socket(listen_fd_))
        {
            close_socket(listen_fd_);
            listen_fd_ = invalid_socket();
        }
        endpoint_listen_.clear();
        if (mode_ == Mode::ServerListening)
            mode_ = Mode::Idle;
    }

    void clear_buffers()
    {
        out_parts_.clear();
        in_parts_.clear();
        in_next_ = 0;
    }
};

// ======== UDP Transport ======================================================
class UdpTransport : public ITransport
{
private:
    enum class Mode
    {
        Idle,
        ClientReady,
        ServerBound,
        ServerConnected
    };
    // Socket (shared for client/server)
    socket_t sockfd_{invalid_socket()};
    std::string endpoint_;        // Peer or bind endpoint description
    bool dump_{true};
    Mode mode_{Mode::Idle};
    // Buffers for multipart messages
    std::vector<std::string> out_parts_;
    std::vector<std::string> in_parts_;
    size_t in_next_ = 0;
    // Peer info (for default after connect or first recvfrom)
    sockaddr_storage peer_{};
    socklen_t peer_len_ = 0;
    bool peer_known_ = false;

    static std::pair<std::string, std::string> split_host_port(const std::string &endpoint)
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
        {
            return {endpoint, ""}; // No port found
        }
        return {endpoint.substr(0, host_end), endpoint.substr(host_end + 1)};
    }

    static std::string describe_sockaddr(const sockaddr_storage &ss, socklen_t slen)
    {
        char host[NI_MAXHOST]{};
        char serv[NI_MAXSERV]{};
        if (getnameinfo(reinterpret_cast<const sockaddr *>(&ss), slen,
                        host, sizeof(host), serv, sizeof(serv),
                        NI_NUMERICHOST | NI_NUMERICSERV) == 0) {
            return std::string(host) + ":" + std::string(serv);
        }
        return "<unknown_peer>";
    }

    void flush_out_parts()
    {
        ensure_can_send();
        if (out_parts_.empty()) return;
        if (!rawudp::send_parts(sockfd_, out_parts_)) {
            throw std::runtime_error(make_socket_error_str("UdpTransport: send_parts failed"));
        }
        out_parts_.clear();
    }

    void ensure_in_parts()
    {
        ensure_can_recv();
        if (in_next_ < in_parts_.size())
            return; // still have frames

        in_parts_.clear();
        in_next_ = 0;

        if (peer_known_)
        {
            if (!rawudp::recv_parts(sockfd_, in_parts_))
            {
                throw std::runtime_error(make_socket_error_str("UdpTransport: recv failed"));
            }
        }
        else
        {
            sockaddr_storage sender{};
            socklen_t slen = sizeof(sender);
            if (!rawudp::recv_parts_from(sockfd_, in_parts_,
                                         reinterpret_cast<sockaddr *>(&sender), &slen))
            {
                throw std::runtime_error(make_socket_error_str("UdpTransport: recvfrom failed"));
            }

            if (::connect(sockfd_, reinterpret_cast<sockaddr *>(&sender), slen) != 0)
            {
                throw std::runtime_error(make_socket_error_str("UdpTransport: connect to peer failed"));
            }
            peer_ = sender;
            peer_len_ = slen;
            peer_known_ = true;
            endpoint_ = describe_sockaddr(peer_, peer_len_);
            if (mode_ == Mode::ServerBound)
                mode_ = Mode::ServerConnected;
            if (dump_)
                mv_log("[udp] peer established: %s", endpoint_.c_str());
        }
    }

    void ensure_ready_or_throw() const
    {
        if (mode_ != Mode::ClientReady && mode_ != Mode::ServerConnected)
        {
            throw std::runtime_error("UdpTransport: not ready");
        }
    }

    void ensure_can_send() const {
        if (mode_ != Mode::ClientReady && mode_ != Mode::ServerConnected) {
            throw std::runtime_error("UdpTransport: not ready to send");
        }
    }
    void ensure_can_recv() const {
        if (mode_ != Mode::ClientReady && mode_ != Mode::ServerConnected && mode_ != Mode::ServerBound) {
            throw std::runtime_error("UdpTransport: not ready to recv");
        }
    }

    void close_socket_internal()
    {
        if (is_valid_socket(sockfd_))
        {
            close_socket(sockfd_);
            sockfd_ = invalid_socket();
        }
        endpoint_.clear();
        peer_known_ = false;
        peer_len_ = 0;
        mode_ = Mode::Idle;
    }

    void clear_buffers()
    {
        out_parts_.clear();
        in_parts_.clear();
        in_next_ = 0;
    }

    struct AddrInfoGuard {
        addrinfo* p;
        ~AddrInfoGuard() { if (p) freeaddrinfo(p); }
    };

public:
    UdpTransport() {
        set_transport_type(TransportType::Udp);
    };
    ~UdpTransport() override
    {
        disconnect("");
    }
    void set_dump(bool on) { dump_ = on; }
    void connect(const std::string &endpoint) override
    {
        disconnect("");
        auto hp = split_host_port(endpoint);
        const std::string &host = hp.first;
        const std::string &port = hp.second;
        if (port.empty())
        {
            throw std::runtime_error("UdpTransport: port is missing in endpoint " + endpoint);
        }
        addrinfo hints{};
        hints.ai_socktype = SOCK_DGRAM;
        hints.ai_family   = AF_UNSPEC;
        addrinfo* res = nullptr;
        int rc = getaddrinfo(host.c_str(), port.c_str(), &hints, &res);
        if (rc != 0 || !res)
        {
#ifdef _WIN32
            int werr = WSAGetLastError();
            throw std::runtime_error("UdpTransport: getaddrinfo failed for " + endpoint +
                                     " (WSA error " + std::to_string(werr) + ")");
#else
            int gai_err = (rc ? rc : EAI_SYSTEM);
            throw std::runtime_error("UdpTransport: getaddrinfo failed for " + endpoint + ": " +
                                     gai_strerror(gai_err));
#endif
        }
        AddrInfoGuard guard{res};
        for (auto* p = res; p; p = p->ai_next)
        {
            socket_t cand = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
            if (!is_valid_socket(cand)) continue;
            // Best-effort reuse
            int yes = 1;
            ::setsockopt(cand, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof(yes));
            if (::connect(cand, p->ai_addr, static_cast<socklen_t>(p->ai_addrlen)) == 0)
            {
                sockfd_ = cand;
                peer_len_ = static_cast<socklen_t>(p->ai_addrlen);
                std::memcpy(&peer_, p->ai_addr, peer_len_);
                peer_known_ = true;
                mode_ = Mode::ClientReady;
                endpoint_ = describe_sockaddr(peer_, peer_len_);
                if (dump_) mv_log("[udp] connected (default peer) to %s", endpoint_.c_str());
                return;
            }
            close_socket(cand);
        }
        throw std::runtime_error(make_socket_error_str("UdpTransport: connect failed to " + endpoint));
    }
    // ==================== SERVER MODE ====================
    void listen(const std::string &endpoint) override
    {
        disconnect("");
    
        auto hp = split_host_port(endpoint);
        const std::string &host = hp.first;
        const std::string &port = hp.second;

        const bool is_any_host = (host == "*" || host.empty() || host == "0.0.0.0" || host == "::");

        addrinfo hints{};
        hints.ai_socktype = SOCK_DGRAM;
        hints.ai_flags = AI_PASSIVE; 
        hints.ai_family = AF_UNSPEC;

        addrinfo *res = nullptr;
        if (getaddrinfo(is_any_host ? nullptr : host.c_str(), port.c_str(), &hints, &res) != 0 || !res)
        {
            throw std::runtime_error(make_socket_error_str("UdpTransport: getaddrinfo(bind) failed for " + endpoint));
        }
        AddrInfoGuard guard{res};

        for (auto *p = res; p; p = p->ai_next)
        {
            socket_t cand = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
            if (!is_valid_socket(cand))
                continue;

            // --- Socket options (best-effort) ---
            int yes = 1;
            (void)::setsockopt(cand, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&yes), sizeof(yes));
#if defined(SO_REUSEPORT)
            (void)::setsockopt(cand, SOL_SOCKET, SO_REUSEPORT, reinterpret_cast<const char *>(&yes), sizeof(yes));
#endif
            // Enable dual-stack if IPv6 (so v4-mapped addresses work) — ignore errors if not supported
            if (p->ai_family == AF_INET6)
            {
                int v6only = 0;
                (void)::setsockopt(cand, IPPROTO_IPV6, IPV6_V6ONLY,
                                   reinterpret_cast<const char *>(&v6only), sizeof(v6only));
            }
            int rcvbuf = 1 << 20; // 1 MiB
            (void)::setsockopt(cand, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char *>(&rcvbuf), sizeof(rcvbuf));

            if (::bind(cand, p->ai_addr, static_cast<socklen_t>(p->ai_addrlen)) == 0)
            {
                sockfd_ = cand;
                sockaddr_storage local{};
                socklen_t slen = static_cast<socklen_t>(sizeof(local));
                if (::getsockname(sockfd_, reinterpret_cast<sockaddr *>(&local), &slen) == 0)
                {
                    endpoint_ = describe_sockaddr(local, slen);
                }
                else
                {
                    endpoint_ = endpoint;
                }

                mode_ = Mode::ServerBound;
                peer_known_ = false;

                if (dump_)
                    mv_log("[udp] bound on %s", endpoint_.c_str());
                return;
            }

            close_socket(cand);
        }

        throw std::runtime_error(make_socket_error_str("UdpTransport: bind failed on " + endpoint));
    }
    bool accept() override
    {
        throw std::runtime_error("UdpTransport: accept not supported; peer is established on first recv after listen");
    }
    void bind(const std::string &endpoint) override
    {
        listen(endpoint);
    }
    void unbind(const std::string & /*endpoint*/) override
    {
        disconnect("");
    }
    // ==================== COMMON API ====================
    void disconnect(const std::string & /*endpoint*/) override
    {
        if (dump_ && (mode_ != Mode::Idle))
            mv_log("[udp] disconnected");
        close_socket_internal();
        clear_buffers();
    }
    void send(const void *data, size_t len, bool more) override
    {
        ensure_ready_or_throw();
        out_parts_.emplace_back(static_cast<const char *>(data), len);
        if (!more)
            flush_out_parts();
    }
    void send_text(const std::string &s, bool more) override
    {
        send(s.data(), s.size(), more);
    }
    void recv(void *data, size_t len) override
    {
        ensure_in_parts();
        if (in_next_ >= in_parts_.size())
        {
            throw std::runtime_error("UdpTransport: recv called with no frames available");
        }
        const std::string &frame = in_parts_[in_next_++];
        if (frame.size() != len)
        {
            throw std::runtime_error("UdpTransport: recv size mismatch (expected " +
                                     std::to_string(len) + ", got " + std::to_string(frame.size()) + ")");
        }
        std::memcpy(data, frame.data(), len);
    }
    std::string recv_text() override
    {
        ensure_in_parts();
        if (in_next_ >= in_parts_.size())
        {
            throw std::runtime_error("UdpTransport: recv_text called with no frames available");
        }
        return std::move(in_parts_[in_next_++]);
    }
    bool recv_multipart(std::vector<std::string> &parts) override
    {
        try
        {
            ensure_in_parts();
            if (in_parts_.empty()) {
                return false; // Peer closed or no data
            }
            parts = std::move(in_parts_);
            in_parts_.clear();
            in_next_ = 0;
            if (dump_)
                mv_log("[udp] recv_multipart: %zu part(s)", parts.size());
            return true;
        }
        catch (const std::runtime_error &)
        {
            return false;
        }
    }
};