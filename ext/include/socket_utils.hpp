#pragma once

#include <cstdint>
#include <system_error> 

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "Ws2_32.lib")
    using socket_t = SOCKET;
#else
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <netdb.h>
    #include <unistd.h>
    #include <cerrno>
    #include <netinet/tcp.h> 
    using socket_t = int;
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

// --- Cross-platform socket utility functions ---

static inline bool is_valid_socket(socket_t s) {
#ifdef _WIN32
    return s != INVALID_SOCKET;
#else
    return s >= 0;
#endif
}

static inline socket_t invalid_socket() {
#ifdef _WIN32
    return INVALID_SOCKET;
#else
    return -1;
#endif
}

static inline int sock_errno() {
#ifdef _WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}

static inline void close_socket(socket_t s) {
    if (is_valid_socket(s)) {
#ifdef _WIN32
        ::closesocket(s);
#else
        ::close(s);
#endif
    }
}

/**
 * @brief Creates a standard cross-platform error message string.
 */
static inline std::string make_socket_error_str(const std::string& prefix) {
    return prefix + " (errno " + std::to_string(sock_errno()) + ")";
}
