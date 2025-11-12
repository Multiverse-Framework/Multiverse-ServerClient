#pragma once

#include "socket_utils.hpp"
#include "log_utils.hpp"
#include "general.hpp"

#include <vector>
#include <string>
#include <cstdint>
#include <cstring>
#include <sstream>
#include <iomanip>

#ifndef MAX_UDP_PAYLOAD
#define MAX_UDP_PAYLOAD 1200u
#endif

namespace rawudp {

// -------- Little-endian conversion helpers --------
inline uint32_t htole32_custom(uint32_t host_32bits) {
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return host_32bits;
#elif defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    return __builtin_bswap32(host_32bits);
#else
    const uint32_t test = 1;
    if (*reinterpret_cast<const uint8_t*>(&test) == 1)
        return host_32bits;
    else
        return ((host_32bits & 0xFF000000u) >> 24) |
               ((host_32bits & 0x00FF0000u) >> 8)  |
               ((host_32bits & 0x0000FF00u) << 8)  |
               ((host_32bits & 0x000000FFu) << 24);
#endif
}

inline uint32_t le32toh_custom(uint32_t le_32bits) {
    return htole32_custom(le_32bits);
}

// -------- Helper: hex dump for binary data --------
inline std::string hex_dump(const std::string& data) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (size_t i = 0; i < data.size(); ++i) {
        oss << std::setw(2) << static_cast<int>(static_cast<unsigned char>(data[i]));
        if (i + 1 < data.size()) oss << ' ';
    }
    return oss.str();
}

// -------- Encode multipart --------
inline bool encode_parts(const std::vector<std::string>& parts, std::vector<uint8_t>& out) {
    const uint32_t n = static_cast<uint32_t>(parts.size());

    size_t need = sizeof(uint32_t);
    for (const auto& s : parts)
        need += sizeof(uint32_t) + s.size();

    if (need > MAX_UDP_PAYLOAD) {
        mv_log("[rawudp] Encoded payload %zu exceeds MAX_UDP_PAYLOAD=%u.", need, (unsigned)MAX_UDP_PAYLOAD);
        return false;
    }

    out.clear();
    out.resize(need);
    uint8_t* p = out.data();

    auto put_u32 = [&](uint32_t v) {
        const uint32_t le = htole32_custom(v);
        std::memcpy(p, &le, sizeof(le));
        p += sizeof(le);
    };

    mv_log("[rawudp] Handshake: Encoding part count: %u", n);
    put_u32(n);

    for (uint32_t i = 0; i < n; ++i) {
        const auto& s = parts[i];
        uint32_t sz = static_cast<uint32_t>(s.size());

        mv_log("[rawudp] Handshake: Encoding part %u size: %u", i, sz);
        put_u32(sz);

        if (sz > 0) {
            mv_log("[rawudp] Handshake: Encoding part %u data (hex): %s",
                   i, hex_dump(s).c_str());
            std::memcpy(p, s.data(), s.size());
            p += s.size();
        } else {
            mv_log("[rawudp] Handshake: Part %u is empty.", i);
        }
    }

    mv_log("[rawudp] Handshake: Encode complete. Total size: %zu", out.size());
    return true;
}

// -------- Decode multipart --------
inline bool decode_parts(const uint8_t* buf, size_t len, std::vector<std::string>& out) {
    out.clear();

    if (len < sizeof(uint32_t)) {
        mv_log("[rawudp] Packet too small for part_count (got %zu bytes).", len);
        return false;
    }

    const uint8_t* p = buf;
    const uint8_t* e = buf + len;

    auto get_u32 = [&](uint32_t& out32) -> bool {
        if (p + sizeof(uint32_t) > e) {
            mv_log("[rawudp] Buffer overflow reading u32 at offset %zu", (size_t)(p - buf));
            return false;
        }
        uint32_t le;
        std::memcpy(&le, p, sizeof(le));
        p += sizeof(le);
        out32 = le32toh_custom(le);
        return true;
    };

    uint32_t n = 0;
    mv_log("[rawudp] Handshake: Waiting to read part count (4 bytes)...");
    if (!get_u32(n)) return false;

    if (n > 1000) {
        mv_log("[rawudp] Suspicious part count: %u (possibly corrupted data)", n);
        return false;
    }

    mv_log("[rawudp] Handshake: Read part count: %u", n);
    mv_log("[rawudp] Handshake: Total packet size received: %zu bytes", len);
    out.reserve(n);

    for (uint32_t i = 0; i < n; ++i) {
        uint32_t sz = 0;
        mv_log("[rawudp] Handshake: Waiting to read size for part %u (4 bytes)...", i);
        if (!get_u32(sz)) return false;

        if (sz > MAX_UDP_PAYLOAD) {
            mv_log("[rawudp] Suspicious part size: %u (possibly corrupted data)", sz);
            return false;
        }

        mv_log("[rawudp] Handshake: Read size for part %u: %u", i, sz);

        if (p + sz > e) {
            mv_log("[rawudp] Truncated data for part %u (need %u bytes, have %zu).",
                   i, sz, (size_t)(e - p));
            return false;
        }

        std::string s(reinterpret_cast<const char*>(p), sz);
        p += sz;

        if (sz > 0) {
            mv_log("[rawudp] Handshake: Read data for part %u (hex): %s",
                   i, hex_dump(s).c_str());
        } else {
            mv_log("[rawudp] Handshake: Part %u is zero size.", i);
        }
        out.push_back(std::move(s));
    }

    mv_log("[rawudp] Handshake: Successfully received all %u parts. Used %zu/%zu bytes",
           n, (size_t)(p - buf), len);
    return true;
}

// -------- Connected UDP send/recv --------
inline bool send_parts(socket_t fd, const std::vector<std::string>& parts) {
    std::vector<uint8_t> pkt;
    if (!encode_parts(parts, pkt)) return false;

    mv_log("[rawudp] Sending %zu byte packet", pkt.size());
    ssize_t w = ::send(fd, reinterpret_cast<const char*>(pkt.data()), pkt.size(), 0);
    if (w < 0) {
        perror("[rawudp] send failed");
        return false;
    }
    if (static_cast<size_t>(w) != pkt.size()) {
        mv_log("[rawudp] send partial: %zd/%zu", (ssize_t)w, pkt.size());
        return false;
    }
    mv_log("[rawudp] Successfully sent %zd bytes", (ssize_t)w);
    return true;
}

inline bool recv_parts(socket_t fd, std::vector<std::string>& out, int timeout_ms = 5000) {
    std::vector<uint8_t> buf(MAX_UDP_PAYLOAD);

    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(fd, &rfds);

    timeval tv;
    timeval* tv_ptr = nullptr;
    if (timeout_ms >= 0) {
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        tv_ptr = &tv;
    }

    int sel = select(fd + 1, &rfds, nullptr, nullptr, tv_ptr);
    if (sel <= 0) {
        if (sel == 0 && timeout_ms >= 0)
            mv_log("[rawudp] recv timeout after %d ms", timeout_ms);
        else if (sel < 0)
            perror("[rawudp] select (recv)");
        return false;
    }

#ifdef _WIN32
    int n = ::recv(fd, reinterpret_cast<char*>(buf.data()), (int)buf.size(), 0);
#else
    ssize_t n = ::recv(fd, buf.data(), buf.size(), 0);
#endif
    if (n <= 0) return false;

    mv_log("[rawudp] Received %zd bytes", (ssize_t)n);
    return decode_parts(buf.data(), (size_t)n, out);
}

// -------- Unconnected UDP send/recv --------
inline bool send_parts_to(socket_t fd, const std::vector<std::string>& parts,
                          const struct sockaddr* dest, socklen_t dest_len) {
    std::vector<uint8_t> pkt;
    if (!encode_parts(parts, pkt)) return false;

    ssize_t w = ::sendto(fd, reinterpret_cast<const char*>(pkt.data()), pkt.size(), 0, dest, dest_len);
    if (w < 0 || static_cast<size_t>(w) != pkt.size()) {
        mv_log("[rawudp] sendto failed or partial: %zd/%zu", (ssize_t)w, pkt.size());
        return false;
    }
    return true;
}

inline bool recv_parts_from(socket_t fd, std::vector<std::string>& out,
                            struct sockaddr* from = nullptr, socklen_t* from_len = nullptr,
                            int timeout_ms = 1000)
{
    std::vector<uint8_t> buf(MAX_UDP_PAYLOAD);

    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(fd, &rfds);

    timeval tv = {timeout_ms / 1000, (timeout_ms % 1000) * 1000};

    int sel = select(fd + 1, &rfds, nullptr, nullptr, &tv);
    if (sel <= 0)
        return false;

#ifdef _WIN32
    int n = ::recvfrom(fd, reinterpret_cast<char*>(buf.data()), (int)buf.size(), 0, from, from_len);
#else
    ssize_t n = ::recvfrom(fd, buf.data(), buf.size(), 0, from, from_len);
#endif
    if (n <= 0) return false;

    return decode_parts(buf.data(), (size_t)n, out);
}

} // namespace rawudp
