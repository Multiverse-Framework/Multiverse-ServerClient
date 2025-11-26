#pragma once
#include <string>
#include <vector>
#include "zmq.hpp"
#include "server_transport.hpp"
class ZmqServerTransport : public IServerTransport
{
public:
    ZmqServerTransport();
    ~ZmqServerTransport();

    ServerTransportType type() const override { return ServerTransportType::Zmq; }

    void listen(const std::string& endpoint) override;
    bool accept() override; 
    void disconnect() override;
    void send(const void* data, size_t len, bool more) override;
    void send_text(const std::string& text, bool more) override;
    void recv(void* data, size_t len) override;
    std::string recv_text() override;
    bool recv_multipart(std::vector<std::string>& parts) override;
private:
    void* ctx_;
    void* sock_;
};
