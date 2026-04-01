#include "zmq_client_transport.hpp"

extern "C" {
#include <zmq.h>
}

#include <stdexcept>
#include <cstring>
#include <chrono>
#include <thread>

ZmqClientTransport::ZmqClientTransport()
    : ctx_(nullptr)
    , sock_(nullptr)
{
    ctx_ = zmq_ctx_new();
    if (!ctx_)
        throw std::runtime_error("ZmqClientTransport: zmq_ctx_new failed");

    create_socket();
}

void ZmqClientTransport::create_socket()
{
    if (sock_)
    {
        zmq_close(sock_);
        sock_ = nullptr;
    }

    sock_ = zmq_socket(ctx_, ZMQ_REQ);
    if (!sock_)
    {
        throw std::runtime_error("ZmqClientTransport: zmq_socket(ZMQ_REQ) failed");
    }

    // Set linger to 0 for immediate close without blocking
    int linger = 0;
    zmq_setsockopt(sock_, ZMQ_LINGER, &linger, sizeof(linger));

    // Set receive timeout to 5000ms (consistent with UDP timeout)
    int rcvtimeo = 5000;
    zmq_setsockopt(sock_, ZMQ_RCVTIMEO, &rcvtimeo, sizeof(rcvtimeo));

    // Set send timeout to prevent blocking forever
    int sndtimeo = 5000;
    zmq_setsockopt(sock_, ZMQ_SNDTIMEO, &sndtimeo, sizeof(sndtimeo));
}

ZmqClientTransport::~ZmqClientTransport()
{
    // Close socket first
    if (sock_)
    {
        zmq_close(sock_);
        sock_ = nullptr;
    }

    // Set context to non-blocking termination to prevent hanging
    if (ctx_)
    {
        zmq_ctx_set(ctx_, ZMQ_BLOCKY, 0);
        zmq_ctx_term(ctx_);
        ctx_ = nullptr;
    }
}

void ZmqClientTransport::connect(const std::string& endpoint)
{
    // Always create a fresh socket before connecting to ensure clean state
    // This prevents EFSM errors from stale socket state
    create_socket();

    constexpr size_t kMaxAttempts = 12;
    constexpr int kSleepMs = 200;

    for (size_t attempt = 1; attempt <= kMaxAttempts; ++attempt)
    {
        if (zmq_connect(sock_, endpoint.c_str()) == 0)
        {
            return;
        }

        if (attempt < kMaxAttempts)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(kSleepMs));
        }
    }

    throw std::runtime_error(
        "ZmqClientTransport: connect failed to " + endpoint + " after " + std::to_string(kMaxAttempts) + " attempts");
}

void ZmqClientTransport::disconnect()
{
    // Always recreate socket to ensure clean state (no EFSM issues)
    if (ctx_)
    {
        create_socket();
    }
}

void ZmqClientTransport::send(const void* data, size_t len, bool more)
{
    if (!sock_)
        throw std::runtime_error("ZmqClientTransport: no socket");

    int flags = more ? ZMQ_SNDMORE : 0;
    if (zmq_send(sock_, data, len, flags) < 0)
    {
        int err = zmq_errno();
        throw std::runtime_error(
            "ZmqClientTransport: send failed (errno " + std::to_string(err) + ": " + zmq_strerror(err) + ")");
    }
}

void ZmqClientTransport::send_text(const std::string& text, bool more)
{
    send(text.data(), text.size(), more);
}

void ZmqClientTransport::recv(void* data, size_t len)
{
    if (!sock_)
        throw std::runtime_error("ZmqClientTransport: no socket");

    int rc = zmq_recv(sock_, data, len, 0);
    if (rc < 0)
    {
        int err = zmq_errno();
        throw std::runtime_error(
            "ZmqClientTransport: recv failed (errno " + std::to_string(err) + ": " + zmq_strerror(err) + ")");
    }

    // Validate received size matches expected size (consistent with TCP/UDP)
    if (static_cast<size_t>(rc) != len)
    {
        throw std::runtime_error("ZmqClientTransport: recv size mismatch (expected " + std::to_string(len) + ", got " +
                                 std::to_string(rc) + ")");
    }
}

std::string ZmqClientTransport::recv_text()
{
    if (!sock_)
        throw std::runtime_error("ZmqClientTransport: no socket");

    zmq_msg_t msg;
    zmq_msg_init(&msg);

    int rc = zmq_msg_recv(&msg, sock_, 0);
    if (rc < 0)
    {
        int err = zmq_errno();
        zmq_msg_close(&msg);
        throw std::runtime_error(
            "ZmqClientTransport: recv_text failed (errno " + std::to_string(err) + ": " + zmq_strerror(err) + ")");
    }

    std::string out(static_cast<char*>(zmq_msg_data(&msg)), zmq_msg_size(&msg));
    zmq_msg_close(&msg);
    return out;
}

bool ZmqClientTransport::recv_multipart(std::vector<std::string>& parts)
{
    if (!sock_)
        return false;

    parts.clear();
    zmq_msg_t msg;
    int rc;

    while (true)
    {
        zmq_msg_init(&msg);
        rc = zmq_msg_recv(&msg, sock_, 0);
        if (rc < 0)
        {
            zmq_msg_close(&msg);
            return false;
        }

        parts.emplace_back(static_cast<char*>(zmq_msg_data(&msg)), zmq_msg_size(&msg));
        int more = zmq_msg_more(&msg);
        zmq_msg_close(&msg);

        if (!more)
            break;
    }

    return true;
}
