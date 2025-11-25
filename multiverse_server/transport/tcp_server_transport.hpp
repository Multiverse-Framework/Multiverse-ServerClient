#pragma once
#include "transport_server.hpp"
#include "utils/raw_tcp.hpp"

class TcpServerTransport : public IServerTransport {
public:
    TcpServerTransport();
    ~TcpServerTransport() override;

    ServerTransportType type() const override { return ServerTransportType::Tcp; }

    void listen(const std::string& ep) override;
    bool accept() override;
    void disconnect() override;
    void send(const void* data, size_t len, bool more) override;
    void send_text(const std::string& s, bool more) override;
    void recv(void* data, size_t len) override;
    std::string recv_text() override;
    bool recv_multipart(std::vector<std::string>& parts) override;

private:
    socket_t listen_fd_;
    socket_t client_fd_;

    std::vector<std::string> out_parts_, in_parts_;
    size_t in_next_;
};
