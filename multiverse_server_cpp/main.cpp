#define _USE_MATH_DEFINES
#include <csignal>
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <utility>

#include "utils/general.hpp"
#include "transport_runner.h"
#include "runner_common.h"

// -----------------------------------------------------------------------------
// Only keep supported transports (compile-time)
// -----------------------------------------------------------------------------
static TransportSel parse_transport_single(std::string t) {
    for (auto &c : t) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

#if USE_ZMQ
    if (t == "zmq") return TransportSel::Zmq;
#endif
#if USE_TCP
    if (t == "tcp") return TransportSel::Tcp;
#endif
#if USE_UDP
    if (t == "udp") return TransportSel::Udp;
#endif

    throw std::runtime_error("Unsupported or disabled transport: " + t);
}

// -----------------------------------------------------------------------------
// Helper: split host:port
// -----------------------------------------------------------------------------
#if USE_TCP || USE_UDP
static void split_host_port(const std::string &bind, std::string &host, std::string &port) {
    auto pos = bind.rfind(':');
    if (pos == std::string::npos) {
        host = "0.0.0.0";
        port = bind;
    } else {
        host = bind.substr(0, pos);
        port = bind.substr(pos + 1);
    }
}
#endif
// -----------------------------------------------------------------------------
// EndpointSpec
// -----------------------------------------------------------------------------
struct EndpointSpec {
    TransportSel t;
    std::string bind;
    bool has_bind = false;
};

// -----------------------------------------------------------------------------
// Default bindings per transport (only for compiled-in ones)
// -----------------------------------------------------------------------------
static std::string default_bind_for(TransportSel t) {
    switch (t) {
#if USE_ZMQ
        case TransportSel::Zmq: return "tcp://*:7000";
#endif
#if USE_TCP
        case TransportSel::Tcp: return "0.0.0.0:7000";
#endif
#if USE_UDP
        case TransportSel::Udp: return "0.0.0.0:7000";
#endif
        default: break;
    }
    return {};
}

// -----------------------------------------------------------------------------
// Help text
// -----------------------------------------------------------------------------
static void print_help(const char* argv0) {
    std::cout
      << "Usage:\n"
      << "  " << argv0 << " [--transport zmq|tcp|udp [--bind <addr>]]...\n\n"
      << "You may repeat --transport/--bind pairs to run multiple servers concurrently.\n"
      << "Examples:\n"
      << "  " << argv0 << " --transport zmq --bind tcp://*:7000\n"
      << "  " << argv0 << " --transport tcp --bind 127.0.0.1:8000 --transport udp --bind 127.0.0.1:9000\n"
      << "Defaults (when --bind omitted right after a transport):\n"
#if USE_ZMQ
      << "  zmq: tcp://*:7000\n"
#endif
#if USE_TCP
      << "  tcp: 0.0.0.0:7000\n"
#endif
#if USE_UDP
      << "  udp: 0.0.0.0:7000\n"
#endif
      ;
}

// -----------------------------------------------------------------------------
// Main
// -----------------------------------------------------------------------------
int main(int argc, char **argv) {
    std::printf("Start Multiverse Server (multi-transport)...\n");

    // Show build config summary
#if USE_ZMQ
    std::cout << "  ZMQ: ENABLED\n";
#else
    std::cout << "  ZMQ: DISABLED\n";
#endif
#if USE_TCP
    std::cout << "  TCP: ENABLED\n";
#else
    std::cout << "  TCP: DISABLED\n";
#endif
#if USE_UDP
    std::cout << "  UDP: ENABLED\n";
#else
    std::cout << "  UDP: DISABLED\n";
#endif

    std::vector<EndpointSpec> specs;
    EndpointSpec pending{};
    bool have_pending = false;

    // -------------------------------------------------------------------------
    // Parse command-line arguments
    // -------------------------------------------------------------------------
    if (argc == 1) {
        const char* env = std::getenv("MULTIVERSE_TRANSPORT");
        TransportSel env_sel = TransportSel::Zmq;
        if (env) { try { env_sel = parse_transport_single(env); } catch (...) {} }
        specs.push_back(EndpointSpec{env_sel, default_bind_for(env_sel), true});
    } else {
        for (int i = 1; i < argc; ++i) {
            std::string k = argv[i];
            auto need_next = [&](const char* flag) {
                if (i + 1 >= argc) throw std::runtime_error(std::string(flag) + " requires a value");
            };

            if (k == "-h" || k == "--help") {
                print_help(argv[0]);
                return 0;
            } else if (k == "--transport") {
                need_next("--transport");
                if (have_pending) {
                    if (!pending.has_bind) {
                        pending.bind = default_bind_for(pending.t);
                        pending.has_bind = true;
                    }
                    specs.push_back(pending);
                    have_pending = false;
                }
                std::string tval = argv[++i];
                pending = EndpointSpec{parse_transport_single(tval), "", false};
                have_pending = true;
            } else if (k == "--bind") {
                need_next("--bind");
                std::string b = argv[++i];
                if (!have_pending) {
                    throw std::runtime_error("--bind must follow a --transport");
                }
                pending.bind = b;
                pending.has_bind = true;
            } else {
                if (!have_pending) {
                    throw std::runtime_error("Unexpected positional argument: " + k);
                }
                pending.bind = k;
                pending.has_bind = true;
            }
        }
        if (have_pending) {
            if (!pending.has_bind) {
                pending.bind = default_bind_for(pending.t);
                pending.has_bind = true;
            }
            specs.push_back(pending);
        }
        if (specs.empty()) {
            specs.push_back(EndpointSpec{TransportSel::Zmq, default_bind_for(TransportSel::Zmq), true});
        }
    }

    ShutdownManager::install_signal_handlers();

    // -------------------------------------------------------------------------
    // Launch each enabled transport
    // -------------------------------------------------------------------------
    std::vector<std::unique_ptr<IServerRunner>> runners;
    std::vector<std::thread> threads;

    for (const auto& sp : specs) {
#if USE_ZMQ
        if (sp.t == TransportSel::Zmq) {
            auto r = make_runner(sp.t, sp.bind, "");
            if (!r) { std::cerr << "[Server] ZMQ runner not available\n"; continue; }
            std::cout << "[Server] Launch ZMQ @ " << sp.bind << "\n";
            threads.emplace_back([rr = r.get()](){ rr->run(); });
            runners.emplace_back(std::move(r));
            continue;
        }
#endif
#if USE_TCP
        if (sp.t == TransportSel::Tcp) {
            std::string host, port;
            split_host_port(sp.bind, host, port);
            auto r = make_runner(sp.t, host, port);
            if (!r) { std::cerr << "[Server] TCP runner not available\n"; continue; }
            std::cout << "[Server] Launch TCP @ " << host << ":" << port << "\n";
            threads.emplace_back([rr = r.get()](){ rr->run(); });
            runners.emplace_back(std::move(r));
            continue;
        }
#endif
#if USE_UDP
        if (sp.t == TransportSel::Udp) {
            std::string host, port;
            split_host_port(sp.bind, host, port);
            auto r = make_runner(sp.t, host, port);
            if (!r) { std::cerr << "[Server] UDP runner not available\n"; continue; }
            std::cout << "[Server] Launch UDP @ " << host << ":" << port << "\n";
            threads.emplace_back([rr = r.get()](){ rr->run(); });
            runners.emplace_back(std::move(r));
            continue;
        }
#endif
        std::cerr << "[Server] Transport not compiled in, skipped.\n";
    }

    if (threads.empty()) {
        std::cerr << "[Server] No runners launched. Exiting.\n";
        return 2;
    }

    // -------------------------------------------------------------------------
    // Main wait loop
    // -------------------------------------------------------------------------
    while (!ShutdownManager::is_shutdown()) {
        sleep_ms(100);
    }

    for (auto& t : threads)
        if (t.joinable()) t.join();

    return 0;
}
