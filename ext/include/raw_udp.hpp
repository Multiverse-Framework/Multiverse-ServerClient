#pragma once
#include "socket_utils.hpp"
#include "log_utils.hpp"

#include <vector>
#include <string>
#include <cstdint>
#include <cstring>
#include "general.hpp"

#ifndef MAX_UDP_PAYLOAD
#define MAX_UDP_PAYLOAD 1200u
#endif

namespace rawudp {

// -------- Helpers: encode/decode multipart into a linear buffer --------

/**
 * Encode parts into a contiguous buffer:
 * Layout: [u32 part_count][u32 size1][bytes1]...[u32 sizeN][bytesN]
 * Returns true and fills 'out' on success; false if payload would exceed MAX_UDP_PAYLOAD.
 * Uses network byte order for all u32 fields.
 */
inline bool encode_parts(const std::vector<std::string>& parts, std::vector<uint8_t>& out) {
    const uint32_t n = static_cast<uint32_t>(parts.size());
    // Compute needed size
    size_t need = sizeof(uint32_t); // part_count
    for (const auto& s : parts) {
        need += sizeof(uint32_t);    // size
        need += s.size();            // data
    }
    if (need > MAX_UDP_PAYLOAD) {
        mv_log("[rawudp] Encoded payload %zu exceeds MAX_UDP_PAYLOAD=%u.", need, (unsigned)MAX_UDP_PAYLOAD);
        return false;
    }

    out.resize(need);
    uint8_t* p = out.data();

    auto put_u32 = [&](uint32_t v) {
        const uint32_t net = htonl(v);
        std::memcpy(p, &net, sizeof(net));
        p += sizeof(net);
    };

    put_u32(n);
    for (const auto& s : parts) {
        put_u32(static_cast<uint32_t>(s.size()));
        if (!s.empty()) {
            std::memcpy(p, s.data(), s.size());
            p += s.size();
        }
    }
    return true;
}

/**
 * Decode buffer into parts. Returns false if malformed.
 * Accepts a single UDP datagram payload.
 */
inline bool decode_parts(const uint8_t* buf, size_t len, std::vector<std::string>& out) {
    out.clear();
    if (len < sizeof(uint32_t)) {
        mv_log("[rawudp] Packet too small for part_count.");
        return false;
    }
    const uint8_t* p = buf;
    const uint8_t* e = buf + len;

    auto get_u32 = [&](uint32_t& out32) -> bool {
        if (p + sizeof(uint32_t) > e) return false;
        uint32_t net;
        std::memcpy(&net, p, sizeof(net));
        p += sizeof(net);
        out32 = ntohl(net);
        return true;
    };

    uint32_t n = 0;
    if (!get_u32(n)) {
        mv_log("[rawudp] Failed to read part_count.");
        return false;
    }
    out.reserve(n);
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t sz = 0;
        if (!get_u32(sz)) {
            mv_log("[rawudp] Failed to read size for part %u.", i);
            return false;
        }
        if (p + sz > e) {
            mv_log("[rawudp] Truncated data for part %u (need %u bytes).", i, sz);
            return false;
        }
        out.emplace_back(reinterpret_cast<const char*>(p), reinterpret_cast<const char*>(p) + sz);
        p += sz;
    }
    return true;
}

// -------- Send/recv using a CONNECTED UDP socket (peer set via connect()) --------

/**
 * send_parts: connected UDP socket version.
 * Packs all parts into one datagram and sends via ::send.
 */
inline bool send_parts(socket_t fd, const std::vector<std::string>& parts) {
    std::vector<uint8_t> pkt;
    if (!encode_parts(parts, pkt)) return false;

    // UDP ::send should send the entire datagram or fail.
    ssize_t w = ::send(fd, reinterpret_cast<const char*>(pkt.data()), pkt.size(), 0);
    if (w < 0 || static_cast<size_t>(w) != pkt.size()) {
        mv_log("[rawudp] send failed or partial: %zd/%zu", (ssize_t)w, pkt.size());
        return false;
    }
    return true;
}

/**
 * recv_parts: connected UDP socket version.
 * Receives one datagram via ::recv and decodes it.
 * Returns false on error or malformed packet.
 */
inline bool recv_parts(socket_t fd, std::vector<std::string>& out, int timeout_ms = 1000) {
    std::vector<uint8_t> buf(MAX_UDP_PAYLOAD);

    // Wait for readiness (timeout)
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(fd, &rfds);
    timeval tv{timeout_ms / 1000, (timeout_ms % 1000) * 1000};

    int sel = select(fd + 1, &rfds, nullptr, nullptr, &tv);
    if (sel <= 0) {
        if (ShutdownManager::is_shutdown()) return false;
        if (sel == 0) return false; // timeout
        perror("select");
        return false;
    }

#ifdef _WIN32
    int n = ::recv(fd, reinterpret_cast<char*>(buf.data()), (int)buf.size(), 0);
#else
    ssize_t n = ::recv(fd, buf.data(), buf.size(), 0);
#endif
    if (n <= 0) return false;
    return decode_parts(buf.data(), (size_t)n, out);
}

// -------- Send/recv using an UNCONNECTED UDP socket (sendto/recvfrom) --------

/**
 * send_parts_to: unconnected UDP socket.
 * Provide destination sockaddr and length.
 */
inline bool send_parts_to(socket_t fd,
                          const std::vector<std::string>& parts,
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

/**
 * recv_parts_from: unconnected UDP socket.
 * Fills 'from' with sender address (optional). Decodes one datagram.
 */
inline bool recv_parts_from(socket_t fd,
                            std::vector<std::string>& out,
                            struct sockaddr* from = nullptr, socklen_t* from_len = nullptr, int timeout_ms = 1000) {
    std::vector<uint8_t> buf(MAX_UDP_PAYLOAD);

    // Wait for readability
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(fd, &rfds);
    timeval tv{timeout_ms / 1000, (timeout_ms % 1000) * 1000};

    int sel = select(fd + 1, &rfds, nullptr, nullptr, &tv);
    if (sel <= 0) {
        if (ShutdownManager::is_shutdown()) return false;
        if (sel == 0) return false; // timeout
        perror("select");
        return false;
    }

#ifdef _WIN32
    int n = ::recvfrom(fd, reinterpret_cast<char*>(buf.data()), (int)buf.size(), 0, from, from_len);
#else
    ssize_t n = ::recvfrom(fd, buf.data(), buf.size(), 0, from, from_len);
#endif
    if (n <= 0) return false;
    return decode_parts(buf.data(), (size_t)n, out);
}

} // namespace rawudp
