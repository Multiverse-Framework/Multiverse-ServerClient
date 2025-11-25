#include "server.hpp"
#include "tcp_server_transport.hpp"
#include "udp_server_transport.hpp"
#include "zmq_server_transport.hpp"

Server::Server(std::unique_ptr<IServerTransport> t)
    : transport_(std::move(t)) {}

std::unique_ptr<Server> Server::create(ServerTransportType type, const std::string& endpoint)
{
    std::unique_ptr<IServerTransport> tr;

    switch (type) {
        case ServerTransportType::Tcp: tr = std::make_unique<TcpServerTransport>(); break;
        case ServerTransportType::Udp: tr = std::make_unique<UdpServerTransport>(); break;
        case ServerTransportType::Zmq: tr = std::make_unique<ZmqServerTransport>(); break;
    }

    tr->listen(endpoint);
    return std::make_unique<Server>(std::move(tr));
}

bool Server::accept() { return transport_->accept(); }
void Server::send_text(const std::string& s) { transport_->send_text(s, false); }
std::string Server::recv_text() { return transport_->recv_text(); }
bool Server::recv_multipart(std::vector<std::string>& parts) { return transport_->recv_multipart(parts); }
