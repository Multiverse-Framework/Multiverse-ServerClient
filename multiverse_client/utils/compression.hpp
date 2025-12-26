#pragma once

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
#include <chrono>

// LZ4 library (available on most systems via apt install liblz4-dev)
#include <lz4.h>
#include <lz4hc.h>

/**
 * Multiverse Compression Utilities
 *
 * Provides compression/decompression compatible with the Rust server implementation.
 * Supports LZ4 compression for real-time performance.
 *
 * Protocol specification: See COMPRESSION_PROTOCOL.md
 */

namespace multiverse {

/// Maximum uncompressed size to prevent decompression bombs (100 MB)
constexpr size_t MAX_UNCOMPRESSED_SIZE = 100 * 1024 * 1024;

/// Maximum compressed size to prevent memory issues (50 MB)
constexpr size_t MAX_COMPRESSED_SIZE = 50 * 1024 * 1024;

enum class CompressionType : uint8_t {
    None = 0,
    Lz4 = 1,
    Zstd = 2,  // Reserved for future use
};

inline const char* compression_type_str(CompressionType type) {
    switch (type) {
        case CompressionType::None: return "None";
        case CompressionType::Lz4:  return "LZ4";
        case CompressionType::Zstd: return "Zstd";
        default: return "Unknown";
    }
}

struct CompressionConfig {
    bool enabled = true;
    CompressionType method = CompressionType::Lz4;
    size_t threshold = 512;  // Minimum size to compress (bytes)
    int level = 3;           // Compression level (for future Zstd support)

    /// Load configuration from environment variables
    static CompressionConfig from_env() {
        CompressionConfig config;

        const char* enabled_env = std::getenv("MULTIVERSE_COMPRESSION_ENABLED");
        if (enabled_env) {
            config.enabled = (std::string(enabled_env) == "1" ||
                            std::string(enabled_env) == "true");
        }

        const char* type_env = std::getenv("MULTIVERSE_COMPRESSION_TYPE");
        if (type_env) {
            std::string type_str(type_env);
            if (type_str == "lz4") {
                config.method = CompressionType::Lz4;
            } else if (type_str == "none") {
                config.method = CompressionType::None;
            }
        }

        const char* threshold_env = std::getenv("MULTIVERSE_COMPRESSION_THRESHOLD");
        if (threshold_env) {
            config.threshold = static_cast<size_t>(std::atoi(threshold_env));
        }

        const char* level_env = std::getenv("MULTIVERSE_COMPRESSION_LEVEL");
        if (level_env) {
            config.level = std::atoi(level_env);
            if (config.level < 1) config.level = 1;
            if (config.level > 22) config.level = 22;
        }

        printf("[Compression] Config: enabled=%d, method=%s, threshold=%zu bytes, level=%d\n",
            config.enabled, compression_type_str(config.method), config.threshold, config.level);

        return config;
    }
};

class Compressor {
public:
    explicit Compressor(const CompressionConfig& config = CompressionConfig())
        : config_(config) {}

    /// Determine if compression should be applied based on data size
    bool should_compress(size_t data_size) const {
        return config_.enabled && data_size >= config_.threshold;
    }

    /// Get the configured compression type
    CompressionType compression_type() const {
        return config_.enabled ? config_.method : CompressionType::None;
    }

    /**
     * Compress data using LZ4 with size prepended (compatible with Rust lz4_flex::compress_prepend_size)
     *
     * Format: [uncompressed_size: 4 bytes LE][compressed_data]
     *
     * @param input Input data buffer
     * @param input_size Size of input data
     * @param output Output buffer (will be resized)
     * @param compression_type Output parameter for the compression type used
     * @return true if compression was applied, false if data sent uncompressed
     */
    bool compress_lz4(const void* input, size_t input_size,
                     std::vector<uint8_t>& output, CompressionType& compression_type) const {
        if (!should_compress(input_size)) {
            output.assign(static_cast<const uint8_t*>(input),
                         static_cast<const uint8_t*>(input) + input_size);
            compression_type = CompressionType::None;
            return false;
        }

        auto start = std::chrono::high_resolution_clock::now();

        // Calculate max compressed size
        const int max_compressed_size = LZ4_compressBound(static_cast<int>(input_size));
        if (max_compressed_size <= 0) {
            throw std::runtime_error("LZ4_compressBound failed");
        }

        // Allocate output buffer: [size: 4 bytes] + [compressed_data]
        output.resize(4 + max_compressed_size);

        // Write uncompressed size (little-endian, 4 bytes)
        uint32_t size_le = static_cast<uint32_t>(input_size);
        std::memcpy(output.data(), &size_le, 4);

        // Compress data
        const int compressed_size = LZ4_compress_default(
            static_cast<const char*>(input),
            reinterpret_cast<char*>(output.data() + 4),
            static_cast<int>(input_size),
            max_compressed_size
        );

        if (compressed_size <= 0) {
            throw std::runtime_error("LZ4 compression failed");
        }

        // Resize to actual compressed size
        output.resize(4 + compressed_size);

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

        double ratio = (output.size() * 100.0) / input_size;
        printf("[Compression] LZ4: %zu → %zu bytes (%.1f%%) in %ld μs\n",
            input_size, output.size(), ratio, duration.count());

        compression_type = CompressionType::Lz4;
        return true;
    }

    /**
     * Decompress LZ4 data with prepended size (compatible with Rust lz4_flex::decompress_size_prepended)
     *
     * @param compressed Compressed data buffer
     * @param compressed_size Size of compressed data
     * @param expected_uncompressed_size Expected size after decompression (for validation)
     * @param output Output buffer (will be resized)
     */
    void decompress_lz4(const void* compressed, size_t compressed_size,
                       size_t expected_uncompressed_size, std::vector<uint8_t>& output) const {
        // Safety check: prevent decompression bombs
        if (expected_uncompressed_size > MAX_UNCOMPRESSED_SIZE) {
            throw std::runtime_error(
                "Uncompressed size " + std::to_string(expected_uncompressed_size) +
                " exceeds maximum " + std::to_string(MAX_UNCOMPRESSED_SIZE)
            );
        }

        if (compressed_size > MAX_COMPRESSED_SIZE) {
            throw std::runtime_error(
                "Compressed size " + std::to_string(compressed_size) +
                " exceeds maximum " + std::to_string(MAX_COMPRESSED_SIZE)
            );
        }

        if (compressed_size < 4) {
            throw std::runtime_error("Compressed data too small (< 4 bytes)");
        }

        auto start = std::chrono::high_resolution_clock::now();

        // Read uncompressed size from header (little-endian, 4 bytes)
        uint32_t uncompressed_size_le;
        std::memcpy(&uncompressed_size_le, compressed, 4);
        const size_t uncompressed_size = uncompressed_size_le;

        // Validate against expected size
        if (uncompressed_size != expected_uncompressed_size) {
            throw std::runtime_error(
                "LZ4 size mismatch: header says " + std::to_string(uncompressed_size) +
                ", expected " + std::to_string(expected_uncompressed_size)
            );
        }

        // Allocate output buffer
        output.resize(uncompressed_size);

        // Decompress
        const int decompressed_size = LZ4_decompress_safe(
            static_cast<const char*>(compressed) + 4,  // Skip 4-byte header
            reinterpret_cast<char*>(output.data()),
            static_cast<int>(compressed_size - 4),
            static_cast<int>(uncompressed_size)
        );

        if (decompressed_size < 0) {
            throw std::runtime_error("LZ4 decompression failed (corrupted data?)");
        }

        if (static_cast<size_t>(decompressed_size) != uncompressed_size) {
            throw std::runtime_error(
                "LZ4 decompressed size mismatch: got " + std::to_string(decompressed_size) +
                ", expected " + std::to_string(uncompressed_size)
            );
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

        printf("[Decompression] LZ4: %zu → %zu bytes in %ld μs\n",
            compressed_size, output.size(), duration.count());
    }

    /**
     * Generic compress method
     */
    bool compress(const void* input, size_t input_size,
                 std::vector<uint8_t>& output, CompressionType& compression_type) const {
        switch (config_.method) {
            case CompressionType::Lz4:
                return compress_lz4(input, input_size, output, compression_type);
            case CompressionType::None:
            default:
                output.assign(static_cast<const uint8_t*>(input),
                             static_cast<const uint8_t*>(input) + input_size);
                compression_type = CompressionType::None;
                return false;
        }
    }

    /**
     * Generic decompress method
     */
    void decompress(const void* compressed, size_t compressed_size,
                   CompressionType compression_type, size_t expected_uncompressed_size,
                   std::vector<uint8_t>& output) const {
        switch (compression_type) {
            case CompressionType::Lz4:
                decompress_lz4(compressed, compressed_size, expected_uncompressed_size, output);
                break;
            case CompressionType::None:
            default:
                output.assign(static_cast<const uint8_t*>(compressed),
                             static_cast<const uint8_t*>(compressed) + compressed_size);
                break;
        }
    }

private:
    CompressionConfig config_;
};

/// Encode message spec with compression information
inline int32_t encode_message_spec(int32_t base_spec, CompressionType compression) {
    int32_t comp_bits = static_cast<int32_t>(compression);
    return base_spec | (comp_bits << 6);
}

/// Decode message spec to extract base spec and compression type
inline void decode_message_spec(int32_t spec, int32_t& base_spec, CompressionType& compression) {
    base_spec = spec & 0x3F;  // Lower 6 bits
    int32_t comp_bits = (spec >> 6) & 0x03;  // Bits 6-7
    compression = static_cast<CompressionType>(comp_bits);
}

} // namespace multiverse
