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

#pragma once
#include "IServer.h"
#include "RawTCPServer.h"
#include "RawUDPServer.h"
#include "RawZMQServer.h"
#include <zmq.h>
#include <memory>
#include <utility>
#include <arpa/inet.h>

inline int parse_port(std::string_view ep)
{
    auto pos = ep.rfind(':');
    if (pos == std::string_view::npos)
        return -1;
    return std::stoi(std::string(ep.substr(pos + 1)));
}

inline std::string to_addr_string(const sockaddr_in &addr)
{
    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(addr.sin_addr), ip_str, INET_ADDRSTRLEN);
    return std::string(ip_str) + ":" + std::to_string(ntohs(addr.sin_port));
}


// --- TCP ---
class TcpServerAdapter final : public IServer
{
public:
    bool initialize(std::string_view endpoint) override
    {
        int port = parse_port(endpoint);
        if (port <= 0)
            return false;
        srv_ = std::make_unique<RawTcpServer>();
        return srv_->initialize(port);
    }
    void setCallback(ServerCallback cb) override { cb_ = std::move(cb); }
    void run() override
    {
        srv_->registerCallbackEx([this](const char *d, int n, const sockaddr_in &peer)
                                 {
                                    if (!cb_) return;
                                    Packet p;
                                    p.meta.transport = Transport::TCP;
                                    p.meta.remote = to_addr_string(peer);
                                    p.data.assign(reinterpret_cast<const uint8_t*>(d),
                                                  reinterpret_cast<const uint8_t*>(d) + n);
                                    cb_(p); 
                                 });
        srv_->run();
    }
    void stop() override { srv_->stop(); }

private:
    std::unique_ptr<RawTcpServer> srv_;
    ServerCallback cb_;
};

// --- UDP ---
class UdpServerAdapter final : public IServer
{
public:
    bool initialize(std::string_view endpoint) override
    {
        int port = parse_port(endpoint);
        if (port <= 0)
            return false;
        srv_ = std::make_unique<RawUdpServer>();
        return srv_->initialize(port);
    }
    void setCallback(ServerCallback cb) override { cb_ = std::move(cb); }
    void run() override
    {
        srv_->registerCallbackEx([this](const char *d, int n, const sockaddr_in &from)
                                 {
                                    if (!cb_) return;
                                    Packet p;
                                    p.meta.transport = Transport::UDP;
                                    p.meta.remote = to_addr_string(from);
                                    p.data.assign(reinterpret_cast<const uint8_t*>(d),
                                                  reinterpret_cast<const uint8_t*>(d) + n);
                                    cb_(p); 
                                 });
        srv_->run();
    }
    void stop() override { srv_->stop(); }

private:
    std::unique_ptr<RawUdpServer> srv_;
    ServerCallback cb_;
};

// --- ZMQ REP ---
class ZmqRepAdapter final : public IServer
{
public:
    bool initialize(std::string_view endpoint) override
    {
        srv_ = std::make_unique<ZmqRepServer>();
        std::string address(endpoint);
        ep_ = address;
        return srv_->initialize(ep_);
    }
    void setCallback(ServerCallback cb) override { cb_ = std::move(cb); }
    void run() override
    {
        srv_->registerRawCallback([this](const void *d, size_t n)
                                  {
                                    if (!cb_) return;
                                    Packet p;
                                    p.meta.transport = Transport::ZMQ;
                                    p.meta.remote = "zmq:" + ep_;
                                    p.data.assign(static_cast<const uint8_t*>(d),
                                                  static_cast<const uint8_t*>(d) + n);
                                    cb_(p); 
                                  });
        srv_->run();
    }
    void stop() override { srv_->stop(); }

private:
    std::unique_ptr<ZmqRepServer> srv_;
    ServerCallback cb_;
    std::string ep_;
};