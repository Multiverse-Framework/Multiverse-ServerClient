#define _USE_MATH_DEFINES
#include <csignal>
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <utility>

#include "transport_runner.h"
#include "runner_common.h"

static TransportSel parse_transport_single(std::string t) {
    for (auto &c : t) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (t == "zmq") return TransportSel::Zmq;
    if (t == "tcp") return TransportSel::Tcp;
    if (t == "udp") return TransportSel::Udp;
    throw std::runtime_error("Unknown transport: " + t);
}

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

struct EndpointSpec {
    TransportSel t;
    std::string bind;
    bool has_bind = false;
};

static std::string default_bind_for(TransportSel t) {
    switch (t) {
        case TransportSel::Zmq: return "tcp://*:7000";
        case TransportSel::Tcp: return "0.0.0.0:7000";
        case TransportSel::Udp: return "0.0.0.0:7000";
    }
    return {};
}

static void print_help(const char* argv0) {
    std::cout
      << "Usage:\n"
      << "  " << argv0 << " [--transport zmq|tcp|udp [--bind <addr>]]...\n\n"
      << "You may repeat --transport/--bind pairs to run multiple servers concurrently.\n"
      << "Examples:\n"
      << "  " << argv0 << " --transport zmq --bind tcp://*:7000\n"
      << "  " << argv0 << " --transport tcp --bind 127.0.0.1:8000 --transport udp --bind 127.0.0.1:9000\n"
      << "Defaults (when --bind omitted right after a transport):\n"
      << "  zmq: tcp://*:7000\n"
      << "  tcp: 0.0.0.0:7000\n"
      << "  udp: 0.0.0.0:7000\n";
}

int main(int argc, char **argv) {
    std::printf("Start Multiverse Server (multi-transport)...\n");

    std::vector<EndpointSpec> specs;
    EndpointSpec pending{};
    bool have_pending = false;

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
                pending = EndpointSpec{parse_transport_single(tval), /*bind*/"", /*has_bind*/false};
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
                // Positional tokens: allow a shorthand ONLY if we have a pending transport
                if (!have_pending) {
                    throw std::runtime_error("Unexpected positional argument: " + k);
                }
                pending.bind = k;
                pending.has_bind = true;
            }
        }

        // Finalize trailing pending spec
        if (have_pending) {
            if (!pending.has_bind) {
                pending.bind = default_bind_for(pending.t);
                pending.has_bind = true;
            }
            specs.push_back(pending);
        }

        if (specs.empty()) {
            // Safety net: default to single ZMQ
            specs.push_back(EndpointSpec{TransportSel::Zmq, default_bind_for(TransportSel::Zmq), true});
        }
    }

    std::signal(SIGINT, [](int){
        std::printf("[Server] Caught SIGINT (Ctrl+C), shutting down...\n");
        g_should_shutdown = true;
    });

    std::vector<std::unique_ptr<IServerRunner>> runners;
    runners.reserve(specs.size());
    std::vector<std::thread> threads;
    threads.reserve(specs.size());

    for (const auto& sp : specs) {
        if (sp.t == TransportSel::Zmq) {
            auto r = make_runner(sp.t, sp.bind, "" /*unused*/);
            if (!r) { std::cerr << "[Server] Failed to create ZMQ runner for " << sp.bind << "\n"; continue; }
            std::cout << "[Server] Launch " << r->name() << " @ " << sp.bind << "\n";
            threads.emplace_back([rr = r.get()](){ rr->run(); });
            runners.emplace_back(std::move(r));
        } else {
            std::string host, port;
            split_host_port(sp.bind, host, port);
            auto r = make_runner(sp.t, host, port);
            if (!r) { std::cerr << "[Server] Failed to create runner for " << sp.bind << "\n"; continue; }
            std::cout << "[Server] Launch " << r->name() << " @ " << host << ":" << port << "\n";
            threads.emplace_back([rr = r.get()](){ rr->run(); });
            runners.emplace_back(std::move(r));
        }
    }

    if (threads.empty()) {
        std::cerr << "[Server] No runners launched. Exiting.\n";
        return 2;
    }

    while (!g_should_shutdown) {
        sleep_ms(100);
    }

    for (auto& t : threads) if (t.joinable()) t.join();
    return 0;
}
