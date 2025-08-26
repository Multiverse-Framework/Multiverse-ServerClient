// Copyright (c) 2023, Giang Hoang Nguyen - Institute for Artificial Intelligence, University Bremen

// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:

// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "multiverse_server.h"
#include "transport/IServer.h"
#include "transport/Server.h"
#include <thread>
#include <vector>
#include <memory>
#include <csignal>

// A global flag to signal threads to stop
std::atomic<bool> should_stop(false);

void signal_handler(int signum) {
    printf("\nInterrupt signal (%d) received. Shutting down.\n", signum);
    should_stop.store(true);
}

int start_multiverse_server(const std::string &server_socket_addr)
{
    printf("Starting Multiverse Server at %s\n", server_socket_addr.c_str());
    // Register signal handler for graceful shutdown (Ctrl+C)
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    std::vector<std::unique_ptr<IServer>> servers;
    
    servers.emplace_back(std::make_unique<TcpServerAdapter>());
    servers.emplace_back(std::make_unique<UdpServerAdapter>());
    servers.emplace_back(std::make_unique<ZmqRepAdapter>());

    if (!servers[0]->initialize("tcp://0.0.0.0:7777") ||
        !servers[1]->initialize("udp://0.0.0.0:8888") ||
        !servers[2]->initialize("tcp://*:9999")) {
        fprintf(stderr, "Failed to initialize one or more servers. Exiting.\n");
        return 1;
    }

    // --- Set Callbacks ---
    ServerCallback universal_callback = [](const Packet& packet) {
        std::string transport_name;
        switch (packet.meta.transport) {
            case Transport::TCP:    transport_name = "TCP"; break;
            case Transport::UDP:    transport_name = "UDP"; break;
            case Transport::ZMQ:    transport_name = "ZMQ"; break;
        }
        printf("[%s] Received %zu bytes from [%s]: \"%.*s\"\n",
               transport_name.c_str(),
               packet.data.size(),
               packet.meta.remote.c_str(),
               (int)packet.data.size(),
               (const char*)packet.data.data());
    };

    for (auto& server : servers) {
        server->setCallback(universal_callback);
    }

    std::vector<std::thread> server_threads;
    for (auto& server : servers) {
        server_threads.emplace_back([&]() {
            server->run();
        });
    }

    printf("All servers are running. Press Ctrl+C to stop.\n");
    
    while (!should_stop.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    for (auto& server : servers) {
        server->stop();
    }

    for (auto& t : server_threads) {
        if (t.joinable()) {
            t.join();
        }
    }

    printf("All servers stopped. Exiting.\n");
    return 0;
}