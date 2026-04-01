#include "zmq_server_transport.hpp"

extern "C" {
#include <zmq.h>
}

#include <cerrno>
#include <cstring>
#include <stdexcept>

ZmqServerTransport::ZmqServerTransport()
    : ctx_(nullptr)
    , sock_(nullptr)
{
    ctx_ = zmq_ctx_new();
    if (!ctx_)
        throw std::runtime_error("ZmqServerTransport: zmq_ctx_new failed");

    sock_ = zmq_socket(ctx_, ZMQ_REP);
    if (!sock_)
    {
        zmq_ctx_term(ctx_);
        throw std::runtime_error("ZmqServerTransport: zmq_socket(ZMQ_REP) failed");
    }

    int linger = 0;
    zmq_setsockopt(sock_, ZMQ_LINGER, &linger, sizeof(linger));
    set_timeout(1000, 1000);
}

ZmqServerTransport::~ZmqServerTransport()
{
    if (sock_)
    {
        int linger = 0;
        zmq_setsockopt(sock_, ZMQ_LINGER, &linger, sizeof(linger));
        zmq_close(sock_);
        sock_ = nullptr;
    }

    if (ctx_)
    {
        zmq_ctx_term(ctx_);
        ctx_ = nullptr;
    }
}

void ZmqServerTransport::set_timeout(int recv_timeout_ms, int send_timeout_ms)
{
    if (!sock_)
    {
        throw std::runtime_error("ZmqServerTransport: no socket");
    }

    if (zmq_setsockopt(sock_, ZMQ_RCVTIMEO, &recv_timeout_ms, sizeof(recv_timeout_ms)) != 0)
    {
        throw std::runtime_error("ZmqServerTransport: failed to set ZMQ_RCVTIMEO");
    }

    if (send_timeout_ms >= 0)
    {
        if (zmq_setsockopt(sock_, ZMQ_SNDTIMEO, &send_timeout_ms, sizeof(send_timeout_ms)) != 0)
        {
            throw std::runtime_error("ZmqServerTransport: failed to set ZMQ_SNDTIMEO");
        }
    }
}

void ZmqServerTransport::listen(const std::string& endpoint)
{
    if (!sock_)
    {
        throw std::runtime_error("ZmqServerTransport: no socket");
    }

    if (zmq_bind(sock_, endpoint.c_str()) != 0)
    {
        throw std::runtime_error("ZmqServerTransport: bind failed");
    }
}

bool ZmqServerTransport::accept()
{
    return true;
}

void ZmqServerTransport::disconnect()
{
    if (sock_)
    {
        int linger = 0;
        zmq_setsockopt(sock_, ZMQ_LINGER, &linger, sizeof(linger));
        zmq_close(sock_);
        sock_ = nullptr;
    }

    if (ctx_)
    {
        sock_ = zmq_socket(ctx_, ZMQ_REP);
        if (!sock_)
        {
            throw std::runtime_error("ZmqServerTransport: recreate zmq_socket failed");
        }
        int linger = 0;
        zmq_setsockopt(sock_, ZMQ_LINGER, &linger, sizeof(linger));
    }
}

void ZmqServerTransport::send(const void* data, size_t len, bool more)
{
    if (!sock_)
    {
        throw std::runtime_error("ZmqServerTransport: no socket");
    }

    int flags = more ? ZMQ_SNDMORE : 0;

    if (zmq_send(sock_, data, len, flags) < 0)
    {
        if (errno == EAGAIN)
        {
            throw std::runtime_error("ZmqServerTransport: send timeout");
        }
        throw std::runtime_error("ZmqServerTransport: send failed");
    }
}

void ZmqServerTransport::send_text(const std::string& text, bool more)
{
    send(text.data(), text.size(), more);
}

void ZmqServerTransport::recv(void* data, size_t len)
{
    if (!sock_)
    {
        throw std::runtime_error("ZmqServerTransport: no socket");
    }

    int rc = zmq_recv(sock_, data, len, 0);

    if (rc < 0)
    {
        if (errno == EAGAIN)
        {
            throw std::runtime_error("ZmqServerTransport: recv timeout");
        }
        throw std::runtime_error("ZmqServerTransport: recv failed");
    }
}

std::string ZmqServerTransport::recv_text()
{
    if (!sock_)
    {
        throw std::runtime_error("ZmqServerTransport: no socket");
    }

    zmq_msg_t msg;
    zmq_msg_init(&msg);

    int rc = zmq_msg_recv(&msg, sock_, 0);
    if (rc < 0)
    {
        zmq_msg_close(&msg);
        if (errno == EAGAIN)
        {
            throw std::runtime_error("ZmqServerTransport: recv_text timeout");
        }
        throw std::runtime_error("ZmqServerTransport: recv_text failed");
    }

    std::string out(static_cast<char*>(zmq_msg_data(&msg)), zmq_msg_size(&msg));

    zmq_msg_close(&msg);
    return out;
}

bool ZmqServerTransport::recv_multipart(std::vector<std::string>& parts)
{
    if (!sock_)
    {
        return false;
    }

    parts.clear();
    zmq_msg_t msg;

    while (true)
    {
        zmq_msg_init(&msg);
        int rc = zmq_msg_recv(&msg, sock_, 0);

        if (rc < 0)
        {
            zmq_msg_close(&msg);
            if (errno == EAGAIN)
            {
                return false;
            }
            return false;
        }

        parts.emplace_back(static_cast<char*>(zmq_msg_data(&msg)), zmq_msg_size(&msg));

        int more = zmq_msg_more(&msg);
        zmq_msg_close(&msg);

        if (!more)
        {
            break;
        }
    }

    return true;
}
