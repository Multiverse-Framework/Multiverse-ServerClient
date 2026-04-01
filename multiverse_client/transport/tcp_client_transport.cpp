#include "tcp_client_transport.hpp"
#include "utils/socket_utils.hpp"

#include <stdexcept>
#include <cstring>

TcpClientTransport::TcpClientTransport()
    : in_next_(0)
{
}

TcpClientTransport::~TcpClientTransport()
{
    disconnect();
}

void TcpClientTransport::connect(const std::string& endpoint)
{
    if (async_sock_.is_running())
        disconnect();

    auto [host, port] = split_host_port(endpoint);

    if (port.empty())
        throw std::runtime_error("TcpClientTransport: missing port in endpoint \"" + endpoint + "\"");

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    addrinfo* res = nullptr;
    int gai_rc = ::getaddrinfo(host.c_str(), port.c_str(), &hints, &res);
    if (gai_rc != 0 || !res)
    {
        throw std::runtime_error(make_socket_error_str("TcpClientTransport: getaddrinfo failed for " + endpoint));
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

    constexpr size_t kMaxAttempts = 128;
    constexpr int kSleepMs = 10;

    socket_t connected_sock = invalid_socket();

    for (size_t attempt = 1; attempt <= kMaxAttempts; ++attempt)
    {
        for (auto* p = res; p; p = p->ai_next)
        {
            socket_t cand = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
            if (!is_valid_socket(cand))
                continue;

            set_socket_options(cand, true); // Enable TCP_NODELAY

            if (::connect(cand, p->ai_addr, static_cast<socklen_t>(p->ai_addrlen)) == 0)
            {
                connected_sock = cand;
                break;
            }

            close_socket(cand);
        }

        if (is_valid_socket(connected_sock))
            break;

        if (attempt < kMaxAttempts)
        {
            sleep_ms(kSleepMs);
        }
    }

    if (!is_valid_socket(connected_sock))
    {
        throw std::runtime_error(make_socket_error_str("TcpClientTransport: connect failed to " + endpoint + " after " +
                                                       std::to_string(kMaxAttempts) + " attempts"));
    }

    // Start async receive thread with fast polling (10ms like UDP)
    async_sock_.start(connected_sock, 10);
}

void TcpClientTransport::disconnect()
{
    // Stop async socket (also closes the socket)
    if (async_sock_.is_running())
    {
        async_sock_.stop();
    }

    out_parts_.clear();
    in_parts_.clear();
    in_next_ = 0;
}

void TcpClientTransport::send(const void* data, size_t len, bool more)
{
    if (!async_sock_.is_running())
        throw std::runtime_error("TcpClientTransport: not connected");

    out_parts_.emplace_back(static_cast<const char*>(data), len);
    if (!more)
    {
        if (!rawtcp::send_parts(async_sock_.get_socket(), out_parts_))
        {
            out_parts_.clear();
            // Don't throw during shutdown - the write may have succeeded but shutdown flag caused false return
            if (!ShutdownManager::is_shutdown())
            {
                throw std::runtime_error(make_socket_error_str("TcpClientTransport: send_parts failed"));
            }
        }
        out_parts_.clear();
    }
}

void TcpClientTransport::send_text(const std::string& s, bool more)
{
    send(s.data(), s.size(), more);
}

void TcpClientTransport::recv(void* data, size_t len)
{
    // If we've consumed all cached parts, fetch a new message from the async queue
    if (in_next_ >= in_parts_.size())
    {
        in_parts_.clear();
        in_next_ = 0;
        if (!async_sock_.wait_recv(in_parts_, 5000))
        {
            // Check connection status only after failing to get data
            // (data may have been queued before disconnect was detected)
            if (!async_sock_.is_running())
            {
                throw std::runtime_error("TcpClientTransport: connection closed");
            }
            // Timeout - return without filling data (matches original behavior)
            return;
        }
    }

    const std::string& frame = in_parts_[in_next_++];
    if (frame.size() != len)
    {
        mv_log("%s", make_socket_error_str("TcpClientTransport: recv size mismatch (expected " + std::to_string(len) +
                                           ", got " + std::to_string(frame.size()) + ")")
                         .c_str());
    }
    std::memcpy(data, frame.data(), len);
}

std::string TcpClientTransport::recv_text()
{
    // If we've consumed all cached parts, fetch a new message from the async queue
    if (in_next_ >= in_parts_.size())
    {
        in_parts_.clear();
        in_next_ = 0;
        if (!async_sock_.wait_recv(in_parts_, 5000))
        {
            // Check connection status only after failing to get data
            // (data may have been queued before disconnect was detected)
            if (!async_sock_.is_running())
            {
                throw std::runtime_error("TcpClientTransport: connection closed");
            }
            // Timeout - return empty string (matches original behavior)
            return "";
        }
    }

    return in_parts_[in_next_++];
}

bool TcpClientTransport::recv_multipart(std::vector<std::string>& parts)
{
    // Wait for a message from the async socket (blocking, like ZMQ)
    // Use infinite timeout (-1) to block until data arrives or socket closes
    // Note: wait_recv returns queued data even if socket is disconnected
    return async_sock_.wait_recv(parts, -1);
}
