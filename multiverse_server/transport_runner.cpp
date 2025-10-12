#include "transport_runner.h"
#include <thread>
#include <iostream>

void start_multiverse_server(const std::string& zmq_endpoint);          // ZMQ
void start_multiverse_server_tcp(const std::string &host, const std::string &port); // TCP
void start_multiverse_server_udp(const std::string &host, const std::string &port); // UDP

struct ZmqRunner final : IServerRunner {
    std::string endpoint;
    explicit ZmqRunner(std::string ep) : endpoint(std::move(ep)) {}
    const char* name() const override { return "ZMQ"; }
    void run() override {
        start_multiverse_server(endpoint);
    }
};

struct TcpRunner final : IServerRunner {
    std::string host, port;
    TcpRunner(std::string h, std::string p) : host(std::move(h)), port(std::move(p)) {}
    const char* name() const override { return "TCP"; }
    void run() override {
        start_multiverse_server_tcp(host, port);
    }
};

struct UdpRunner final : IServerRunner {
    std::string host, port;
    UdpRunner(std::string h, std::string p) : host(std::move(h)), port(std::move(p)) {}
    const char* name() const override { return "UDP"; }
    void run() override {
        start_multiverse_server_udp(host, port);
    }
};

std::unique_ptr<IServerRunner> make_runner(TransportSel sel,
                                           const std::string& bind_or_host,
                                           const std::string& port_opt) {
    switch (sel) {
        case TransportSel::Zmq:
            return std::make_unique<ZmqRunner>(bind_or_host);
        case TransportSel::Tcp:
            return std::make_unique<TcpRunner>(bind_or_host, port_opt);
        case TransportSel::Udp:
            return std::make_unique<UdpRunner>(bind_or_host, port_opt);
    }
    return nullptr;
}
