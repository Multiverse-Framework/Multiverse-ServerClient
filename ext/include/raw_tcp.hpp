#pragma once

#include "socket_utils.hpp"
#include "log_utils.hpp"

#include <vector>
#include <string>
#include <cstdint>

namespace rawtcp {

/**
 * @brief Writes exactly n bytes to a descriptor.
 * @return true on success, false on error or if connection is closed.
 */
inline bool write_full(socket_t fd, const void* buf, size_t n) {
    const auto* p = static_cast<const uint8_t*>(buf);
    while (n > 0) {
        ssize_t w = ::send(fd, reinterpret_cast<const char*>(p), n, 0);
        if (w <= 0) return false;
        p += w;
        n -= static_cast<size_t>(w);
    }
    return true;
}

/**
 * @brief Reads exactly n bytes from a descriptor.
 * @return true on success, false on error or if connection is closed.
 */
inline bool read_full(socket_t fd, void* buf, size_t n) {
    auto* p = static_cast<uint8_t*>(buf);
    while (n > 0) {
#ifdef _WIN32
        // MSG_WAITALL is not universally supported on Windows
        ssize_t r = ::recv(fd, reinterpret_cast<char*>(p), n, 0);
#else
        ssize_t r = ::recv(fd, p, n, MSG_WAITALL);
#endif
        if (r <= 0) return false;
        p += r;
        n -= static_cast<size_t>(r);
    }
    return true;
}

/**
 * @brief Sends a vector of strings as a multi-part message.
 *
 * Format: [part_count][size1][data1][size2][data2]...
 */
inline bool send_parts(socket_t fd, const std::vector<std::string>& parts) {
    uint32_t num_parts = static_cast<uint32_t>(parts.size());
    if (!write_full(fd, &num_parts, sizeof(num_parts))) return false;

    for (const auto& s : parts) {
        uint32_t sz = static_cast<uint32_t>(s.size());
        if (!write_full(fd, &sz, sizeof(sz))) return false;
        if (sz > 0 && !write_full(fd, s.data(), sz)) return false;
    }
    return true;
}

/**
 * @brief Receives a multi-part message into a vector of strings.
 */
inline bool recv_parts(socket_t fd, std::vector<std::string>& out) {
    out.clear();
    uint32_t num_parts = 0;
    if (!read_full(fd, &num_parts, sizeof(num_parts))) {
        mv_log("[rawtcp] Failed to read part count.");
        return false;
    }

    out.reserve(num_parts);
    for (uint32_t i = 0; i < num_parts; ++i) {
        uint32_t sz = 0;
        if (!read_full(fd, &sz, sizeof(sz))) {
            mv_log("[rawtcp] Failed to read size for part %u.", i);
            return false;
        }

        std::string s(sz, '\0');
        if (sz > 0 && !read_full(fd, &s[0], sz)) {
            mv_log("[rawtcp] Failed to read %u bytes for part %u.", sz, i);
            return false;
        }
        out.push_back(std::move(s));
    }
    return true;
}

} // namespace rawtcp