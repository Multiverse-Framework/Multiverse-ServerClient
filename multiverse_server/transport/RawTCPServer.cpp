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

#include "RawTCPServer.h"
#include <thread>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <cstdio>
#include <utility>
#include <cstring>

namespace
{
    bool send_all(int fd, const void *buf, size_t len)
    {
        const char *p = static_cast<const char *>(buf);
        while (len)
        {
            ssize_t n = ::send(fd, p, len, 0);
            if (n <= 0)
                return false;
            p += n;
            len -= static_cast<size_t>(n);
        }
        return true;
    }
}

RawTcpServer::RawTcpServer() = default;
RawTcpServer::~RawTcpServer() { stop(); }

bool RawTcpServer::initialize(int port)
{
    m_sockfd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (m_sockfd < 0)
    {
        fprintf(stderr, "TCP ERROR: socket()\n");
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
        fprintf(stderr, "TCP ERROR: bind(%d)\n", port);
        return false;
    }
    if (::listen(m_sockfd, 16) < 0)
    {
        fprintf(stderr, "TCP ERROR: listen()\n");
        return false;
    }
    printf("TCP Server listening on %d\n", port);
    m_running.store(true);
    return true;
}

void RawTcpServer::registerCallback(TcpCallback callback) { m_callback = std::move(callback); }
void RawTcpServer::registerCallbackEx(TcpCallbackEx cb) { m_callback_ex = std::move(cb); }

void RawTcpServer::run()
{
    while (m_running.load())
    {
        sockaddr_in cli_addr{};
        socklen_t clilen = sizeof(cli_addr);
        printf("TCP: waiting for connection...\n");
        int cfd = ::accept(m_sockfd, reinterpret_cast<sockaddr *>(&cli_addr), &clilen);
        if (cfd < 0)
        {
            if (!m_running.load())
                break;
            fprintf(stderr, "TCP ERROR: accept()\n");
            continue;
        }

        std::thread([this, cfd]()
                    {
                        int yes = 1;
                        ::setsockopt(cfd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));
                        char buffer[4096];
                        while (true) {
                            ssize_t n = ::recv(cfd, buffer, sizeof(buffer), 0);
                            if (n <= 0) break;
                            if (m_callback_ex) {
                                sockaddr_in peer{}; socklen_t plen = sizeof(peer);
                                ::getpeername(cfd, reinterpret_cast<sockaddr*>(&peer), &plen);
                                try { m_callback_ex(buffer, static_cast<int>(n), peer); }
                                catch (...) { fprintf(stderr, "TCP callback_ex threw\n"); }
                            } else if (m_callback) {
                                try { m_callback(buffer, static_cast<int>(n)); }
                                catch (...) { fprintf(stderr, "TCP callback threw\n"); }
                            }
                            const char* resp = "TCP Server received your message";
                            if (!send_all(cfd, resp, std::strlen(resp))) break;
                        }
                        ::close(cfd);
                        printf("TCP: client closed.\n"); 
                    }).detach();
    }
}

void RawTcpServer::stop()
{
    bool was_running = m_running.exchange(false);
    if (was_running && m_sockfd != -1)
    {
        ::shutdown(m_sockfd, SHUT_RDWR);
        ::close(m_sockfd);
        m_sockfd = -1;
    }
}