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

inline bool is_valid_socket(socket_t s) { return s != INVALID_SOCKET; }
inline void close_socket(socket_t s) { closesocket(s); }
inline int sock_errno() { return WSAGetLastError(); }
inline socket_t invalid_socket() { return INVALID_SOCKET; }

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

inline bool is_valid_socket(socket_t s) { return s >= 0; }
inline void close_socket(socket_t s) { close(s); }
inline int sock_errno() { return errno; }
inline socket_t invalid_socket() { return -1; }

#endif

/**
 * @brief Cross-platform socket type initialization.
 *
 * RAII-style class to manage WSAStartup on Windows.
 */
class SocketPlatformInit {
public:
    SocketPlatformInit() {
#ifdef _WIN32
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
            throw std::runtime_error("WSAStartup failed");
        }
#endif
    }
    ~SocketPlatformInit() {
#ifdef _WIN32
        WSACleanup();
#endif
    }
};

/**
 * @brief Creates a standard cross-platform error message string.
 */
static inline std::string make_socket_error_str(const std::string& prefix) {
    return prefix + " (errno " + std::to_string(sock_errno()) + ")";
}

inline void sleep_ms(int ms) {
#ifdef _WIN32
    Sleep(ms);
#else
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
#endif
}