#include "zmq_server_transport.hpp"

extern "C" {
#include <zmq.h>
}

#include <stdexcept>
#include <cstring>

ZmqServerTransport::ZmqServerTransport()
    : ctx_(nullptr),
      sock_(nullptr)
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
}

ZmqServerTransport::~ZmqServerTransport()
{
    disconnect();
    if (ctx_)
    {
        zmq_ctx_term(ctx_);
        ctx_ = nullptr;
    }
}

void ZmqServerTransport::listen(const std::string &endpoint)
{
    if (!sock_)
        throw std::runtime_error("ZmqServerTransport: no socket");

    if (zmq_bind(sock_, endpoint.c_str()) != 0)
    {
        throw std::runtime_error("ZmqServerTransport: bind failed");
    }
}

bool ZmqServerTransport::accept()
{
    // REP sockets are always ready when a request arrives; nothing special to do.
    return true;
}

void ZmqServerTransport::disconnect()
{
    if (sock_)
    {
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
    }
}

void ZmqServerTransport::send(const void *data, size_t len, bool more)
{
    if (!sock_)
        throw std::runtime_error("ZmqServerTransport: no socket");

    int flags = more ? ZMQ_SNDMORE : 0;
    if (zmq_send(sock_, data, len, flags) < 0)
    {
        throw std::runtime_error("ZmqServerTransport: send failed");
    }
}

void ZmqServerTransport::send_text(const std::string &text, bool more)
{
    send(text.data(), text.size(), more);
}

void ZmqServerTransport::recv(void *data, size_t len)
{
    if (!sock_)
        throw std::runtime_error("ZmqServerTransport: no socket");

    int rc = zmq_recv(sock_, data, len, 0);
    if (rc < 0)
    {
        throw std::runtime_error("ZmqServerTransport: recv failed");
    }
}

std::string ZmqServerTransport::recv_text()
{
    if (!sock_)
        throw std::runtime_error("ZmqServerTransport: no socket");

    zmq_msg_t msg;
    zmq_msg_init(&msg);

    int rc = zmq_msg_recv(&msg, sock_, 0);
    if (rc < 0)
    {
        zmq_msg_close(&msg);
        throw std::runtime_error("ZmqServerTransport: recv_text failed");
    }

    std::string out(static_cast<char *>(zmq_msg_data(&msg)),
                    zmq_msg_size(&msg));
    zmq_msg_close(&msg);
    return out;
}

bool ZmqServerTransport::recv_multipart(std::vector<std::string> &parts)
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
