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

#include "RawUDPServer.h"
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <utility>

RawUdpServer::RawUdpServer() = default;
RawUdpServer::~RawUdpServer() { stop(); }

bool RawUdpServer::initialize(int port)
{
    m_sockfd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (m_sockfd < 0)
    {
        fprintf(stderr, "UDP ERROR: socket()\n");
        return false;
    }

    int yes = 1;
    ::setsockopt(m_sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
#ifdef SO_REUSEPORT
    ::setsockopt(m_sockfd, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes));
#endif

    sockaddr_in serv_addr{};
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(port);

    if (::bind(m_sockfd, reinterpret_cast<sockaddr *>(&serv_addr), sizeof(serv_addr)) < 0)
    {
        fprintf(stderr, "UDP ERROR: bind(%d)\n", port);
        return false;
    }
    printf("UDP Server listening on %d\n", port);
    m_running.store(true);
    return true;
}

void RawUdpServer::registerCallback(UdpCallback callback) { m_callback = std::move(callback); }
void RawUdpServer::registerCallbackEx(UdpCallbackEx cb) { m_callback_ex = std::move(cb); }

void RawUdpServer::run()
{
    char buffer[65536];
    sockaddr_in cli_addr{};
    while (m_running.load())
    {
        socklen_t clilen = sizeof(cli_addr);
        ssize_t n = ::recvfrom(m_sockfd, buffer, sizeof(buffer), 0,
                               reinterpret_cast<sockaddr *>(&cli_addr), &clilen);
        if (n < 0)
        {
            if (!m_running.load())
                break;
            fprintf(stderr, "UDP ERROR: recvfrom()\n");
            continue;
        }
        if (m_callback_ex)
        {
            try
            {
                m_callback_ex(buffer, static_cast<int>(n), cli_addr);
            }
            catch (...)
            {
                fprintf(stderr, "UDP callback_ex threw\n");
            }
        }
        else if (m_callback)
        {
            try
            {
                m_callback(buffer, static_cast<int>(n));
            }
            catch (...)
            {
                fprintf(stderr, "UDP callback threw\n");
            }
        }
        // Echo back the same payload length
        ssize_t s = ::sendto(m_sockfd, buffer, n, 0,
                             reinterpret_cast<sockaddr *>(&cli_addr), clilen);
        if (s < 0)
            fprintf(stderr, "UDP ERROR: sendto()\n");
    }
}

void RawUdpServer::stop()
{
    bool was_running = m_running.exchange(false);
    if (was_running && m_sockfd != -1)
    {
        ::shutdown(m_sockfd, SHUT_RDWR);
        ::close(m_sockfd);
        m_sockfd = -1;
    }
}