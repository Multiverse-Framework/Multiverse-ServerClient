#define _USE_MATH_DEFINES
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <iostream>
#include <string>
#include <thread>

#include "multiverse_server.h"

#define MULTIVERSE_ENABLE_RAW_TCP

#if defined(MULTIVERSE_ENABLE_RAW_TCP)
void start_multiverse_server_tcp(const std::string &host, const std::string &port);
#endif

static bool use_zmq_transport(const char *env_val, const std::string &cli_val)
{
    std::string t = cli_val;
    if (t.empty() && env_val)
        t = env_val;
    if (t.empty())
        t = "zmq";
    for (auto &c : t)
        c = std::tolower(c);
    return (t == "zmq");
}

static void split_host_port(const std::string &bind, std::string &host, std::string &port)
{
    auto pos = bind.rfind(':');
    if (pos == std::string::npos)
    {
        host = "0.0.0.0";
        port = bind;
    }
    else
    {
        host = bind.substr(0, pos);
        port = bind.substr(pos + 1);
    }
}

int main(int argc, char **argv)
{
    std::printf("Start Multiverse Server...\n");

    // defaults
    std::string transport_cli; // "zmq" or "tcp"
    std::string bind_cli;      // zmq endpoint or host:port
    std::string zmq_default = "tcp://*:7000";
    std::string tcp_default = "0.0.0.0:7000";

    // tiny CLI
    for (int i = 1; i < argc; ++i)
    {
        std::string k = argv[i];
        auto take = [&](std::string &dst)
        { if (i+1 < argc) dst = argv[++i]; };
        if (k == "--transport")
            take(transport_cli);
        else if (k == "--bind")
            take(bind_cli);
        else if (k == "-h" || k == "--help")
        {
            std::cout << "Usage: " << argv[0] << " [--transport zmq|tcp] [--bind <addr>]\n"
                      << "  zmq mode: --bind ZMQ endpoint (default " << zmq_default << ")\n"
                      << "  tcp mode: --bind host:port     (default " << tcp_default << ")\n";
            return 0;
        }
        else
        {
            // positional: treat as bind for current default
            bind_cli = k;
        }
    }

    bool use_zmq = use_zmq_transport(std::getenv("MULTIVERSE_TRANSPORT"), transport_cli);
    const std::string bind_val = !bind_cli.empty() ? bind_cli : (use_zmq ? zmq_default : tcp_default);

    // Ctrl+C -> graceful shutdown
    std::signal(SIGINT, [](int)
                {
                    std::printf("[Server] Caught SIGINT (Ctrl+C), wait for 1s then shutdown.\n");
                    should_shut_down = true;
                    zmq_sleep(1);              // harmless in tcp mode
                    server_context.shutdown(); // safe even if unused in tcp mode
                });

    if (use_zmq)
    {
        // ZMQ broker path (original behavior)
        std::thread th(start_multiverse_server, bind_val);
        while (!should_shut_down)
            zmq_sleep(0.1);

        // wait sockets to clean up
        bool can_shut_down = true;
        do
        {
            can_shut_down = true;
            for (const auto &kv : sockets_need_clean_up)
            {
                if (kv.second)
                {
                    can_shut_down = false;
                    break;
                }
            }
        } while (!can_shut_down);

        zmq_sleep(1);
        server_context.close();
        if (th.joinable())
            th.join();
    }
    else
    {
        // RAW TCP broker path
        std::string host, port;
        split_host_port(bind_val, host, port);
#if defined(MULTIVERSE_ENABLE_RAW_TCP)
        std::thread th(start_multiverse_server_tcp, host, port);
        while (!should_shut_down)
            zmq_sleep(0.1);
        if (th.joinable())
            th.join();
#else
        std::cerr << "[Server] Raw TCP broker requested but not compiled in.\n"
                  << "        Rebuild with -DMULTIVERSE_ENABLE_RAW_TCP and provide start_multiverse_server_tcp().\n"
                  << "        Requested bind: " << host << ":" << port << std::endl;
        return 2;
#endif
    }

    return 0;
}
