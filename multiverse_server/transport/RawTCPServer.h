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
#include <functional>
#include <atomic>
#include <cstdio>
#ifdef _WIN32
#include <winsock2.h>
#else
#include <netinet/in.h>
#endif

class RawTcpServer
{
public:
    using TcpCallback = std::function<void(const char *, int)>;
    using TcpCallbackEx = std::function<void(const char *, int, const sockaddr_in &)>; // with peer info

    RawTcpServer();
    ~RawTcpServer();

    bool initialize(int port);
    void registerCallback(TcpCallback callback);
    void registerCallbackEx(TcpCallbackEx cb);
    void run();
    void stop();

private:
#ifdef _WIN32
    SOCKET m_sockfd{INVALID_SOCKET};
#else
    int m_sockfd{-1};
#endif
    TcpCallback m_callback;
    TcpCallbackEx m_callback_ex;
    std::atomic<bool> m_running{false};
};