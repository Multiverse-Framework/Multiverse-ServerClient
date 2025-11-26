#pragma once
#include <string>
#include <vector>
#include "utils/raw_udp.hpp"
#include "client_transport.hpp"
class UdpClientTransport : public IClientTransport
{
public:
    UdpClientTransport();
    ~UdpClientTransport();

    ClientTransportType type() const override { return ClientTransportType::Udp; }

    void connect(const std::string& endpoint) override;
    void disconnect() override;
    void send(const void* data, size_t len, bool more) override;
    void send_text(const std::string& s, bool more) override;
    void recv(void* data, size_t len) override;
    std::string recv_text() override;
    bool recv_multipart(std::vector<std::string>& parts) override;

private:
    socket_t sock_;
    std::vector<std::string> out_parts_;
    std::vector<std::string> in_parts_;
    size_t in_next_;
};
