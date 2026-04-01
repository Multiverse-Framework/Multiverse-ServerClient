#pragma once
#include <cstdint>
#include <cstddef>
#include <stdexcept>
#include <thread>
#include <chrono>
#include <string>

#ifdef _WIN32
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#ifdef _MSC_VER
#pragma comment(lib, "ws2_32.lib")
#endif

#ifndef _SSIZE_T_DEFINED
#ifdef _WIN64
typedef __int64 ssize_t;
#else
typedef long ssize_t;
#endif
#define _SSIZE_T_DEFINED
#endif

using socket_t = SOCKET;
#define CLOSESOCK(s) closesocket(s)

inline bool is_valid_socket(socket_t s)
{
    return s != INVALID_SOCKET;
}
inline void close_socket(socket_t s)
{
    closesocket(s);
}
inline int sock_errno()
{
    return WSAGetLastError();
}
inline socket_t invalid_socket()
{
    return INVALID_SOCKET;
}

#else // POSIX / Linux / macOS
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <cerrno>

using socket_t = int;
#define CLOSESOCK(s) close(s)

inline bool is_valid_socket(socket_t s)
{
    return s >= 0;
}
inline void close_socket(socket_t s)
{
    close(s);
}
inline int sock_errno()
{
    return errno;
}
inline socket_t invalid_socket()
{
    return -1;
}

#endif

/**
 * @brief Cross-platform socket type initialization.
 *
 * RAII-style class to manage WSAStartup on Windows.
 */
class SocketPlatformInit
{
public:
    SocketPlatformInit()
    {
#ifdef _WIN32
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
        {
            throw std::runtime_error("WSAStartup failed");
        }
#endif
    }
    ~SocketPlatformInit()
    {
#ifdef _WIN32
        WSACleanup();
#endif
    }
};

/**
 * @brief Creates a standard cross-platform error message string.
 */
static inline std::string make_socket_error_str(const std::string& prefix)
{
    return prefix + " (errno " + std::to_string(sock_errno()) + ")";
}

inline void sleep_ms(int ms)
{
#ifdef _WIN32
    Sleep(ms);
#else
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
#endif
}

/**
 * @brief Split endpoint string into host and port components.
 *
 * Handles IPv4, IPv6, and hostname formats:
 * - "host:port" -> {"host", "port"}
 * - "[::1]:8080" -> {"::1", "8080"}
 * - "192.168.1.1:9000" -> {"192.168.1.1", "9000"}
 * - ":8080" -> {"", "8080"}
 *
 * @param endpoint The endpoint string to parse
 * @return Pair of (host, port) strings
 */
inline std::pair<std::string, std::string> split_host_port(const std::string& endpoint)
{
    if (endpoint.empty())
        return {"", ""};

    // Handle IPv6 format: [host]:port
    if (endpoint[0] == '[')
    {
        size_t bracket_end = endpoint.find("]:");
        if (bracket_end != std::string::npos)
        {
            return {endpoint.substr(1, bracket_end - 1), endpoint.substr(bracket_end + 2)};
        }
    }

    // Handle IPv4 and hostname: host:port
    size_t colon_pos = endpoint.rfind(':');
    if (colon_pos == std::string::npos)
        return {endpoint, ""};

    return {endpoint.substr(0, colon_pos), endpoint.substr(colon_pos + 1)};
}

/**
 * @brief Set common socket options for TCP/UDP sockets.
 *
 * Sets:
 * - SO_REUSEADDR: Allow address reuse
 * - SO_REUSEPORT: Allow port reuse (Unix only)
 * - TCP_NODELAY: Disable Nagle's algorithm for TCP (if tcp_nodelay=true)
 *
 * @param s The socket descriptor
 * @param tcp_nodelay Whether to set TCP_NODELAY option
 */
inline void set_socket_options(socket_t s, bool tcp_nodelay = false, bool is_udp = false)
{
    int yes = 1;
    ::setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof(yes));

#ifndef _WIN32
    ::setsockopt(s, SOL_SOCKET, SO_REUSEPORT, reinterpret_cast<const char*>(&yes), sizeof(yes));
#endif

    if (tcp_nodelay)
    {
        ::setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&yes), sizeof(yes));
    }

    if (is_udp)
    {
        int buf_size = 65536 * 10; // 655KB buffer
        ::setsockopt(s, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&buf_size), sizeof(buf_size));
        ::setsockopt(s, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char*>(&buf_size), sizeof(buf_size));
    }
}