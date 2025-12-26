#include "zmq_client_transport.hpp"

extern "C" {
#include <zmq.h>
}

#include <stdexcept>
#include <cstring>

ZmqClientTransport::ZmqClientTransport()
    : ctx_(nullptr),
      sock_(nullptr)
{
    ctx_ = zmq_ctx_new();
    if (!ctx_)
        throw std::runtime_error("ZmqClientTransport: zmq_ctx_new failed");

    sock_ = zmq_socket(ctx_, ZMQ_REQ);
    if (!sock_)
    {
        zmq_ctx_term(ctx_);
        throw std::runtime_error("ZmqClientTransport: zmq_socket(ZMQ_REQ) failed");
    }

    // Set linger to 0 for immediate close without blocking
    int linger = 0;
    zmq_setsockopt(sock_, ZMQ_LINGER, &linger, sizeof(linger));
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

void ZmqClientTransport::connect(const std::string &endpoint)
{
    if (!sock_)
        throw std::runtime_error("ZmqClientTransport: socket not created");

    if (zmq_connect(sock_, endpoint.c_str()) != 0)
    {
        throw std::runtime_error("ZmqClientTransport: connect failed");
    }
}

void ZmqClientTransport::disconnect()
{
    if (sock_)
    {
        zmq_close(sock_);
        sock_ = nullptr;
    }

    // Recreate a new REQ socket for potential reconnection
    if (ctx_)
    {
        sock_ = zmq_socket(ctx_, ZMQ_REQ);
        if (!sock_)
        {
            throw std::runtime_error("ZmqClientTransport: recreate zmq_socket failed");
        }

        // Set linger to 0 for immediate close without blocking
        int linger = 0;
        zmq_setsockopt(sock_, ZMQ_LINGER, &linger, sizeof(linger));
    }
}

void ZmqClientTransport::send(const void *data, size_t len, bool more)
{
    if (!sock_)
        throw std::runtime_error("ZmqClientTransport: no socket");

    int flags = more ? ZMQ_SNDMORE : 0;
    if (zmq_send(sock_, data, len, flags) < 0)
    {
        throw std::runtime_error("ZmqClientTransport: send failed");
    }
}

void ZmqClientTransport::send_text(const std::string &text, bool more)
{
    send(text.data(), text.size(), more);
}

void ZmqClientTransport::recv(void *data, size_t len)
{
    if (!sock_)
        throw std::runtime_error("ZmqClientTransport: no socket");

    int rc = zmq_recv(sock_, data, len, 0);
    if (rc < 0)
    {
        throw std::runtime_error("ZmqClientTransport: recv failed");
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
        zmq_msg_close(&msg);
        throw std::runtime_error("ZmqClientTransport: recv_text failed");
    }

    std::string out(static_cast<char *>(zmq_msg_data(&msg)),
                    zmq_msg_size(&msg));
    zmq_msg_close(&msg);
    return out;
}

bool ZmqClientTransport::recv_multipart(std::vector<std::string> &parts)
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

        parts.emplace_back(static_cast<char *>(zmq_msg_data(&msg)),
                           zmq_msg_size(&msg));
        int more = zmq_msg_more(&msg);
        zmq_msg_close(&msg);

        if (!more)
            break;
    }

    return true;
}
