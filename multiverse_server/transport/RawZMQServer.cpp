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

#include "RawZMQServer.h"
#include <cstdio>
#include <utility>

ZmqRepServer::ZmqRepServer() : m_context(1), m_socket(m_context, zmq::socket_type::rep) {}
ZmqRepServer::~ZmqRepServer() { stop(); }

bool ZmqRepServer::initialize(const std::string &address)
{
    try
    {
        int linger = 0;
        m_socket.set(zmq::sockopt::linger, linger);
        m_socket.bind(address);
        m_bind = address;
        printf("ZMQ Server bound to %s\n", address.c_str());
        m_running.store(true);
        return true;
    }
    catch (const zmq::error_t &e)
    {
        fprintf(stderr, "ZMQ ERROR: bind: %s\n", e.what());
        return false;
    }
}

void ZmqRepServer::registerRawCallback(ZmqRawCallback cb) { m_raw_cb = std::move(cb); }

void ZmqRepServer::run()
{
    printf("ZMQ REP server running...\n");
    while (m_running.load())
    {
        zmq::message_t req;
        try
        {
            auto result = m_socket.recv(req, zmq::recv_flags::dontwait);
            if (!result.has_value()) {
                continue;
            }

            if (m_raw_cb)
            {
                try
                {
                    m_raw_cb(req.data(), req.size());
                }
                catch (...)
                {
                    fprintf(stderr, "ZMQ callback threw\n");
                }
            }
            static constexpr std::string_view reply = "ZMQ World";
            m_socket.send(zmq::buffer(reply), zmq::send_flags::none);
        }
        catch (const zmq::error_t &e)
        {
            if (!m_running.load())
                break;
            fprintf(stderr, "ZMQ ERROR: %s\n", e.what());
        }
    }
}

void ZmqRepServer::stop()
{
    bool was_running = m_running.exchange(false);
    if (was_running)
    {
        try
        {
            m_socket.close();
        }
        catch (...)
        {
        }
        try
        {
            m_context.shutdown();
            m_context.close();
        }
        catch (...)
        {
        }
    }
}