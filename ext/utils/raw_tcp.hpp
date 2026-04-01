#pragma once

#include "socket_utils.hpp"
#include "log_utils.hpp"
#include "general.hpp"
#include <vector>
#include <string>
#include <cstdint>

namespace rawtcp {

/**
 * @brief Result of a read operation
 */
enum class ReadResult
{
    Success,      // Data was read successfully
    Timeout,      // Select timed out (no data available)
    Disconnected, // Peer closed connection (recv returned 0)
    Error         // Socket error occurred
};

/**
 * @brief Writes exactly n bytes to a descriptor.
 * @return true on success, false on error or if connection is closed.
 */
inline bool write_full(socket_t fd, const void* buf, size_t n, int timeout_ms = 2000)
{
    const auto* p = static_cast<const uint8_t*>(buf);
    while (n > 0 && !ShutdownManager::is_shutdown())
    {
        fd_set wfds;
        FD_ZERO(&wfds);
        FD_SET(fd, &wfds);
        timeval tv{timeout_ms / 1000, (timeout_ms % 1000) * 1000};

        int sel = select(fd + 1, nullptr, &wfds, nullptr, &tv);
        if (sel <= 0)
        {
            return false;
        }

        ssize_t w = ::send(fd, reinterpret_cast<const char*>(p), n, 0);
        if (w <= 0)
            return false;
        p += w;
        n -= static_cast<size_t>(w);
    }
    return !ShutdownManager::is_shutdown();
}

/**
 * @brief Reads exactly n bytes from a descriptor with detailed result.
 * @return ReadResult indicating success, timeout, disconnect, or error.
 */
inline ReadResult read_full_ex(socket_t fd, void* buf, size_t n, int timeout_ms = -1)
{
    auto* p = static_cast<uint8_t*>(buf);
    while (n > 0 && !ShutdownManager::is_shutdown())
    {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);
        timeval tv;
        timeval* tv_ptr = nullptr;

        // If timeout_ms < 0 → wait forever
        if (timeout_ms >= 0)
        {
            tv.tv_sec = timeout_ms / 1000;
            tv.tv_usec = (timeout_ms % 1000) * 1000;
            tv_ptr = &tv;
        }

        int sel = select(fd + 1, &rfds, nullptr, nullptr, tv_ptr);
        if (sel < 0)
        {
            return ReadResult::Error;
        }
        if (sel == 0)
        {
            return ReadResult::Timeout;
        }

        ssize_t r = ::recv(fd, reinterpret_cast<char*>(p), n, 0);
        if (r == 0)
        {
            mv_log("[rawtcp] peer disconnected (recv returned 0)");
            return ReadResult::Disconnected;
        }
        if (r < 0)
        {
            return ReadResult::Error;
        }

        p += r;
        n -= static_cast<size_t>(r);
    }
    return ShutdownManager::is_shutdown() ? ReadResult::Error : ReadResult::Success;
}

/**
 * @brief Reads exactly n bytes from a descriptor.
 * @return true on success, false on error or if connection is closed.
 */
inline bool read_full(socket_t fd, void* buf, size_t n, int timeout_ms = -1)
{
    return read_full_ex(fd, buf, n, timeout_ms) == ReadResult::Success;
}

/**
 * @brief Sends a vector of strings as a multi-part message.
 *
 * Format: [part_count][size1][data1][size2][data2]...
 */
inline bool send_parts(socket_t fd, const std::vector<std::string>& parts)
{
    uint32_t num_parts = static_cast<uint32_t>(parts.size());

    if (!write_full(fd, &num_parts, sizeof(num_parts)))
    {
        return false;
    }

    for (uint32_t i = 0; i < num_parts; ++i)
    {
        const auto& s = parts[i];
        uint32_t sz = static_cast<uint32_t>(s.size());

        // Send size header
        if (!write_full(fd, &sz, sizeof(sz)))
        {
            mv_log("[rawtcp][ERROR] Failed to send size header for part %u.", i);
            return false;
        }

        // Send content
        if (sz > 0)
        {
            if (!write_full(fd, s.data(), sz))
            {
                mv_log("[rawtcp][ERROR] Failed to send data for part %u (size=%u).", i, sz);
                return false;
            }

            // Limit preview log for very large data
            const size_t preview_len = std::min<size_t>(sz, 64);
            std::string preview = s.substr(0, preview_len);
            if (sz > preview_len)
                preview += "...";

            mv_log("[rawtcp] Sent part %u successfully (size=%u, preview=\"%s\")", i, sz, preview.c_str());
        }
        else
        {
            mv_log("[rawtcp] Part %u has zero length, skipping data write.", i);
        }
    }

    return true;
}

inline ReadResult recv_parts_ex(socket_t fd, std::vector<std::string>& out, int timeout_ms = -1)
{
    out.clear();
    uint32_t num_parts = 0;

    ReadResult result = read_full_ex(fd, &num_parts, sizeof(num_parts), timeout_ms);
    if (result != ReadResult::Success)
    {
        return result;
    }

    out.reserve(num_parts);
    for (uint32_t i = 0; i < num_parts; ++i)
    {
        uint32_t sz = 0;

        result = read_full_ex(fd, &sz, sizeof(sz), timeout_ms);
        if (result != ReadResult::Success)
        {
            mv_log("[rawtcp] Failed to read size for part %u.", i);
            return result;
        }

        // Sanity check: no single part should be > 64MB
        constexpr uint32_t MAX_PART_SIZE = 64 * 1024 * 1024;
        if (sz > MAX_PART_SIZE)
        {
            mv_log("[rawtcp] ERROR: part %u has unreasonable size %u (max %u). Stream likely desynchronized.", i, sz,
                MAX_PART_SIZE);
            return ReadResult::Error;
        }

        std::string s(sz, '\0');
        if (sz > 0)
        {
            mv_log("[rawtcp] Waiting to read %u bytes for part %u...", sz, i);
            result = read_full_ex(fd, &s[0], sz);
            if (result != ReadResult::Success)
            {
                mv_log("[rawtcp] Failed to read %u bytes for part %u.", sz, i);
                return result;
            }
            // Log the received data (as a string)
            mv_log("[rawtcp] Read data for part %u:", i);
            mv_hexdump(s.c_str(), strlen(s.c_str()), 128);
        }
        else
        {
            mv_log("[rawtcp] Part %u is zero size, skipping read.", i);
        }

        out.push_back(std::move(s));
    }

    mv_log("[rawtcp] Successfully received all %u parts.", num_parts);
    return ReadResult::Success;
}

inline bool recv_parts(socket_t fd, std::vector<std::string>& out, int timeout_ms = -1)
{
    return recv_parts_ex(fd, out, timeout_ms) == ReadResult::Success;
}

} // namespace rawtcp