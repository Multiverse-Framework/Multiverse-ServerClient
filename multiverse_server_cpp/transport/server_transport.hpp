#pragma once
#include <string>
#include <vector>

enum class ServerTransportType : unsigned char
{
    Tcp,
    Udp,
    Zmq
};

class IServerTransport
{
public:
    virtual ~IServerTransport() = default;

    virtual ServerTransportType type() const = 0;

    virtual void listen(const std::string& endpoint) = 0;
    virtual bool accept() = 0;
    virtual void disconnect() = 0;

    virtual void send(const void* data, size_t len, bool more) = 0;
    virtual void send_text(const std::string& s, bool more) = 0;

    virtual void recv(void* data, size_t len) = 0;
    virtual std::string recv_text() = 0;
    virtual bool recv_multipart(std::vector<std::string>& parts) = 0;
};
