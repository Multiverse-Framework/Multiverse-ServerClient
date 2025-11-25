#pragma once
#include <memory>
#include <string>
#include <vector>
#include "transport_client.hpp"

class Client {
public:
    Client(std::unique_ptr<IClientTransport> t);

    static std::unique_ptr<Client> create(ClientTransportType type, const std::string& endpoint);

    void send_text(const std::string& s);
    std::string recv_text();
    bool recv_multipart(std::vector<std::string>& parts);

private:
    std::unique_ptr<IClientTransport> transport_;
};
