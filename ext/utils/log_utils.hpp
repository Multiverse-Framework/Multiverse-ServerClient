// ============================================================================
// my_log.h
// Lightweight debug / hexdump utilities for C++ projects
// ============================================================================

#pragma once
#include <cstdarg>
#include <cstdio>
#include <cstdint>
#include <vector>
#include <chrono>
#include <json/json.h>

#ifdef __ANDROID__
#include <android/log.h>
#define MV_LOG_TAG "MV_Client"
#endif

// Get uptime in seconds since program start
inline double mv_uptime()
{
    static auto start_time = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(now - start_time).count();
}
// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------
// Set MV_DEBUG_IO = 0 before including this header to silence logs globally.
// Example (in code):
//   #define MV_DEBUG_IO 0
//   #include "my_log.h"
// Or via compiler flag:
//   -DMV_DEBUG_IO=0

// #ifndef MV_DEBUG_IO
// #define MV_DEBUG_IO 0 // Default to OFF for production (change to 1 for debugging)
// #endif

// ---------------------------------------------------------------------------
// Logging helpers
// ---------------------------------------------------------------------------

/**
 * @brief Print a formatted log line.
 *        On Android → __android_log_print (visible via `adb logcat -s MV_Client`)
 *        On desktop → fprintf(stdout) with uptime prefix
 * Usage:
 *     mv_log("Connected to %s:%d", host, port);
 */
inline void mv_log(const char* fmt, ...)
{
#if MV_DEBUG_IO
    va_list ap;
    va_start(ap, fmt);
#ifdef __ANDROID__
    __android_log_vprint(ANDROID_LOG_DEBUG, MV_LOG_TAG, fmt, ap);
#else
    std::fprintf(stdout, "[%8.3f] ", mv_uptime());
    std::vfprintf(stdout, fmt, ap);
    std::fputc('\n', stdout);
    std::fflush(stdout);
#endif
    va_end(ap);
#else
    (void)fmt; // avoid unused warnings
#endif
}

/**
 * @brief Print a short hex preview of a buffer.
 * @param data pointer to buffer
 * @param len  total buffer length in bytes
 * @param max_show number of bytes to display (default 64)
 */
inline void mv_hexdump(const void* data, size_t len, size_t max_show = 64)
{
#if MV_DEBUG_IO
    if (!data)
    {
        std::fprintf(stdout, "  <null buffer>\n");
        return;
    }

    const auto* b = static_cast<const unsigned char*>(data);
    size_t show = (len < max_show) ? len : max_show;

    std::fprintf(stdout, "  hex(%zu): ", len);
    for (size_t i = 0; i < show; ++i)
        std::fprintf(stdout, "%02X ", b[i]);
    if (len > show)
        std::fprintf(stdout, "...(+%zu)", len - show);
    std::fputc('\n', stdout);
    std::fflush(stdout);
#else
    (void)data;
    (void)len;
    (void)max_show;
#endif
}

/**
 * @brief Dump all payloads in a vector of byte arrays.
 */
inline void mv_dump_payloads(const std::vector<std::vector<uint8_t>>& payloads)
{
#if MV_DEBUG_IO
    for (size_t i = 0; i < payloads.size(); ++i)
    {
        std::fprintf(stdout, "  payload[%zu] size=%zu\n", i, payloads[i].size());
        mv_hexdump(payloads[i].data(), payloads[i].size());
    }
    std::fflush(stdout);
#else
    (void)payloads;
#endif
}

inline std::string mv_ascii_preview(const uint8_t* data, size_t len, size_t max_show = 256)
{
    if (!data)
        return "<null>";
    size_t n = (len < max_show) ? len : max_show;
    std::string out;
    out.reserve(n);
    for (size_t i = 0; i < n; ++i)
    {
        unsigned char c = data[i];
        if (c >= 32 && c <= 126)
            out.push_back(static_cast<char>(c)); // printable
        else
            out.push_back('.');
    }
    if (len > n)
        out += "...";
    return out;
}

inline bool mv_try_parse_json_object(const uint8_t* buf, size_t len, Json::Value& out_obj, std::string& err)
{
    out_obj = Json::Value(Json::nullValue);
    err.clear();

    // quick sanity
    std::string txt(reinterpret_cast<const char*>(buf), len);

    // Build reader
    Json::CharReaderBuilder b;
    b["collectComments"] = false;
    std::unique_ptr<Json::CharReader> reader(b.newCharReader());

    const char* start = txt.data();
    const char* end = start + txt.size();
    Json::Value root;
    std::string errs;
    bool ok = reader->parse(start, end, &root, &errs);

    if (!ok)
    {
        err = "JSON parse error: " + errs;
        return false;
    }
    if (!root.isObject())
    {
        err = "Parsed JSON type is not object (type=" + std::to_string(static_cast<int>(root.type())) + ")";
        return false;
    }
    out_obj = std::move(root);
    return true;
}

inline std::string ascii_preview_(const void* data, size_t len, size_t max = 80)
{
    const auto* p = static_cast<const unsigned char*>(data);
    size_t n = (len < max) ? len : max;
    std::string s;
    s.reserve(n + 3);
    for (size_t i = 0; i < n; ++i)
    {
        unsigned char c = p[i];
        s += (c >= 32 && c <= 126) ? static_cast<char>(c) : '.';
    }
    if (len > n)
        s += "...";
    return s;
}

inline void mv_dump_parts(const std::vector<std::string>& parts)
{
    mv_log("[dump] %zu payload part(s):", parts.size());
    for (size_t i = 0; i < parts.size(); ++i)
    {
        const auto& f = parts[i];
        mv_log("  [part %zu] len=%zu \"%s\"", i, f.size(), ascii_preview_(f.data(), f.size()).c_str());
    }
}

// ============================================================================
// End of my_log.h
// ============================================================================
