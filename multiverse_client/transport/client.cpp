#include "client.hpp"
#include "tcp_client_transport.hpp"
#include "udp_client_transport.hpp"
#include "zmq_client_transport.hpp"

Client::Client(std::unique_ptr<IClientTransport> t)
    : transport_(std::move(t)) {}

std::unique_ptr<Client> Client::create(ClientTransportType type, const std::string& endpoint) {
    std::unique_ptr<IClientTransport> tr;

    switch (type) {
        case ClientTransportType::Tcp: tr = std::make_unique<TcpClientTransport>(); break;
        case ClientTransportType::Udp: tr = std::make_unique<UdpClientTransport>(); break;
        case ClientTransportType::Zmq: tr = std::make_unique<ZmqClientTransport>(); break;
    }

    tr->connect(endpoint);
    return std::make_unique<Client>(std::move(tr));
}

void Client::send_text(const std::string& s) { transport_->send_text(s, false); }
std::string Client::recv_text() { return transport_->recv_text(); }
bool Client::recv_multipart(std::vector<std::string>& parts) { return transport_->recv_multipart(parts); }
