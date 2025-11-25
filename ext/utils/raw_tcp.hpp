#pragma once

#include "socket_utils.hpp"
#include "log_utils.hpp"
#include "general.hpp"
#include <vector>
#include <string>
#include <cstdint>

namespace rawtcp {

/**
 * @brief Writes exactly n bytes to a descriptor.
 * @return true on success, false on error or if connection is closed.
 */
inline bool write_full(socket_t fd, const void* buf, size_t n, int timeout_ms = 2000) {
    const auto* p = static_cast<const uint8_t*>(buf);
    while (n > 0 && !ShutdownManager::is_shutdown()) {
        fd_set wfds;
        FD_ZERO(&wfds);
        FD_SET(fd, &wfds);
        timeval tv{timeout_ms / 1000, (timeout_ms % 1000) * 1000};

        int sel = select(fd + 1, nullptr, &wfds, nullptr, &tv);
        if (sel <= 0) {
            if (sel == 0)
                mv_log("[rawtcp] write timeout");
            else
                perror("select (write)");
            return false;
        }

        ssize_t w = ::send(fd, reinterpret_cast<const char*>(p), n, 0);
        if (w <= 0) return false;
        p += w;
        n -= static_cast<size_t>(w);
    }
    return !ShutdownManager::is_shutdown();
}

/**
 * @brief Reads exactly n bytes from a descriptor.
 * @return true on success, false on error or if connection is closed.
 */
inline bool read_full(socket_t fd, void* buf, size_t n, int timeout_ms = -1) {
    auto* p = static_cast<uint8_t*>(buf);
    while (n > 0 && !ShutdownManager::is_shutdown()) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);
        timeval tv;
        timeval* tv_ptr = nullptr;

        // If timeout_ms < 0 → wait forever
        if (timeout_ms >= 0) {
            tv.tv_sec  = timeout_ms / 1000;
            tv.tv_usec = (timeout_ms % 1000) * 1000;
            tv_ptr = &tv;
        }

        int sel = select(fd + 1, &rfds, nullptr, nullptr, tv_ptr);
        if (sel <= 0) {
            if (sel == 0)
                mv_log("[rawtcp] read timeout");
            else
                perror("select (read)");
            return false;
        }

        ssize_t r = ::recv(fd, reinterpret_cast<char*>(p), n, 0);
        if (r <= 0) return false;

        p += r;
        n -= static_cast<size_t>(r);
    }
    return !ShutdownManager::is_shutdown();
}

/**
 * @brief Sends a vector of strings as a multi-part message.
 *
 * Format: [part_count][size1][data1][size2][data2]...
 */
inline bool send_parts(socket_t fd, const std::vector<std::string>& parts) {
    uint32_t num_parts = static_cast<uint32_t>(parts.size());
    mv_log("[rawtcp] Handshake: Preparing to send %u parts...", num_parts);

    if (!write_full(fd, &num_parts, sizeof(num_parts))) {
        mv_log("[rawtcp][ERROR] Failed to send part count (%zu bytes).", sizeof(num_parts));
        return false;
    }
    mv_log("[rawtcp] Sent part count: %u", num_parts);

    for (uint32_t i = 0; i < num_parts; ++i) {
        const auto& s = parts[i];
        uint32_t sz = static_cast<uint32_t>(s.size());

        mv_log("[rawtcp] Sending part %u: size=%u bytes", i, sz);

        // Send size header
        if (!write_full(fd, &sz, sizeof(sz))) {
            mv_log("[rawtcp][ERROR] Failed to send size header for part %u.", i);
            return false;
        }

        // Send content
        if (sz > 0) {
            if (!write_full(fd, s.data(), sz)) {
                mv_log("[rawtcp][ERROR] Failed to send data for part %u (size=%u).", i, sz);
                return false;
            }

            // Limit preview log for very large data
            const size_t preview_len = std::min<size_t>(sz, 64);
            std::string preview = s.substr(0, preview_len);
            if (sz > preview_len) preview += "...";

            mv_log("[rawtcp] Sent part %u successfully (size=%u, preview=\"%s\")",
                   i, sz, preview.c_str());
        } else {
            mv_log("[rawtcp] Part %u has zero length, skipping data write.", i);
        }
    }

    mv_log("[rawtcp] Handshake: Successfully sent all %u parts.", num_parts);
    return true;
}

/**
 * @brief Receives a multi-part message into a vector of strings.
 */
inline bool recv_parts(socket_t fd, std::vector<std::string>& out) {
    out.clear();
    uint32_t num_parts = 0;

    mv_log("[rawtcp] Handshake: Waiting to read part count (4 bytes)...");
    if (!read_full(fd, &num_parts, sizeof(num_parts))) {
        mv_log("[rawtcp] Failed to read part count.");
        return false;
    }
    mv_log("[rawtcp] Handshake: Read part count: %u", num_parts);

    out.reserve(num_parts);
    for (uint32_t i = 0; i < num_parts; ++i) {
        uint32_t sz = 0;

        mv_log("[rawtcp] Handshake: Waiting to read size for part %u (4 bytes)...", i);
        if (!read_full(fd, &sz, sizeof(sz))) {
            mv_log("[rawtcp] Failed to read size for part %u.", i);
            return false;
        }
        mv_log("[rawtcp] Handshake: Read size for part %u: %u", i, sz);

        std::string s(sz, '\0');
        if (sz > 0) {
            mv_log("[rawtcp] Handshake: Waiting to read %u bytes for part %u...", sz, i);
            if (!read_full(fd, &s[0], sz)) {
                mv_log("[rawtcp] Failed to read %u bytes for part %u.", sz, i);
                return false;
            }
            // Log the received data (as a string)
            mv_log("[rawtcp] Handshake: Read data for part %u: \"%s\"", i, s.c_str());
        } else {
             mv_log("[rawtcp] Handshake: Part %u is zero size, skipping read.", i);
        }
        
        out.push_back(std::move(s));
    }
    
    mv_log("[rawtcp] Handshake: Successfully received all %u parts.", num_parts);
    return true;
}

} // namespace rawtcp