#pragma once
#include <memory>
#include <string>
#include <vector>
#include "transport_server.hpp"

class Server {
public:
    Server(std::unique_ptr<IServerTransport> t);

    static std::unique_ptr<Server> create(ServerTransportType type, const std::string& endpoint);

    bool accept();
    void send_text(const std::string& s);
    std::string recv_text();
    bool recv_multipart(std::vector<std::string>& parts);

private:
    std::unique_ptr<IServerTransport> transport_;
};
