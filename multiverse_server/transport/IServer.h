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
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <functional>

enum class Transport { TCP, UDP, ZMQ };

struct PacketMeta {
    Transport transport;
    std::string remote;
};

struct Packet {
    PacketMeta meta;
    std::vector<uint8_t> data;
};

using ServerCallback = std::function<void(const Packet&)>;

class IServer {
public:
    virtual ~IServer() = default;

    // Ex for endpoint:
    //   "tcp://0.0.0.0:7777"
    //   "udp://0.0.0.0:8888"
    //   "zmq+rep://*:9999"
    virtual bool initialize(std::string_view endpoint) = 0;

    virtual void setCallback(ServerCallback cb) = 0;
    virtual void run() = 0;
    virtual void stop() = 0;
};