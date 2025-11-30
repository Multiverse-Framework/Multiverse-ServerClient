#include "transport_runner.h"
#include <thread>
#include <iostream>

#if USE_ZMQ
void start_multiverse_server(const std::string& zmq_endpoint); // ZMQ
#endif

#if USE_TCP
void start_multiverse_server_tcp(const std::string &host, const std::string &port); // TCP
#endif

#if USE_UDP
void start_multiverse_server_udp(const std::string &host, const std::string &port); // UDP
#endif

// -------------------------------------------------------------
// Runner Interfaces
// -------------------------------------------------------------
struct ZmqRunner final : IServerRunner {
#if USE_ZMQ
    std::string endpoint;
    explicit ZmqRunner(std::string ep) : endpoint(std::move(ep)) {}
    const char* name() const override { return "ZMQ"; }
    void run() override { start_multiverse_server(endpoint); }
#else
    const char* name() const override { return "ZMQ (Disabled)"; }
    void run() override { std::cerr << "[ERR] ZMQ not compiled in.\n"; }
#endif
};

struct TcpRunner final : IServerRunner {
#if USE_TCP
    std::string host, port;
    TcpRunner(std::string h, std::string p) : host(std::move(h)), port(std::move(p)) {}
    const char* name() const override { return "TCP"; }
    void run() override { start_multiverse_server_tcp(host, port); }
#else
    const char* name() const override { return "TCP (Disabled)"; }
    void run() override { std::cerr << "[ERR] TCP not compiled in.\n"; }
#endif
};

struct UdpRunner final : IServerRunner {
#if USE_UDP
    std::string host, port;
    UdpRunner(std::string h, std::string p) : host(std::move(h)), port(std::move(p)) {}
    const char* name() const override { return "UDP"; }
    void run() override { start_multiverse_server_udp(host, port); }
#else
    const char* name() const override { return "UDP (Disabled)"; }
    void run() override { std::cerr << "[ERR] UDP not compiled in.\n"; }
#endif
};

std::unique_ptr<IServerRunner> make_runner(TransportSel sel,
                                           const std::string& bind_or_host,
                                           const std::string& port_opt) {
    switch (sel)
    {
#if USE_ZMQ
    case TransportSel::Zmq:
        return std::make_unique<ZmqRunner>(bind_or_host);
#endif
#if USE_TCP
    case TransportSel::Tcp:
        return std::make_unique<TcpRunner>(bind_or_host, port_opt);
#endif
#if USE_UDP
    case TransportSel::Udp:
        return std::make_unique<UdpRunner>(bind_or_host, port_opt);
#endif
    default:
        std::cerr << "[ERR] Unsupported or disabled transport selected.\n";
        return nullptr;
    }
}
