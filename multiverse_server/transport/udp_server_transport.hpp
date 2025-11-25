#pragma once
#include <string>
#include <vector>
#include "utils/raw_udp.hpp"
#include "transport_server.hpp"

class UdpServerTransport : public IServerTransport
{
public:
    UdpServerTransport();
    ~UdpServerTransport();

    ServerTransportType type() const override { return ServerTransportType::Udp; }

    void listen(const std::string& endpoint) override;
    bool accept() override;
    void disconnect() override;
    void send(const void* data, size_t len, bool more) override;
    void send_text(const std::string& s, bool more) override;
    void recv(void* data, size_t len) override;
    std::string recv_text() override;
    bool recv_multipart(std::vector<std::string>& parts) override;
private:
    socket_t sock_;
    bool peer_known_;
    sockaddr_storage peer_addr_;
    socklen_t peer_len_;

    std::vector<std::string> out_parts_;
    std::vector<std::string> in_parts_;
    size_t in_next_;
};
