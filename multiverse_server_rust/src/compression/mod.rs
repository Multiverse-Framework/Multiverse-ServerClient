use anyhow::{Context, Result};
use std::fmt;
use tracing::{debug, warn};

/// Maximum uncompressed size to prevent decompression bombs (100 MB)
const MAX_UNCOMPRESSED_SIZE: usize = 100 * 1024 * 1024;

/// Maximum compressed size to prevent memory issues (50 MB)
const MAX_COMPRESSED_SIZE: usize = 50 * 1024 * 1024;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum CompressionType {
    None = 0,
    Lz4 = 1,
    Zstd = 2,
}

impl CompressionType {
    pub fn from_u8(value: u8) -> Self {
        match value {
            1 => CompressionType::Lz4,
            2 => CompressionType::Zstd,
            _ => CompressionType::None,
        }
    }

    pub fn as_u8(self) -> u8 {
        self as u8
    }
}

impl fmt::Display for CompressionType {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            CompressionType::None => write!(f, "None"),
            CompressionType::Lz4 => write!(f, "LZ4"),
            CompressionType::Zstd => write!(f, "Zstd"),
        }
    }
}

#[derive(Debug, Clone)]
pub struct CompressionConfig {
    pub enabled: bool,
    pub method: CompressionType,
    /// Minimum size in bytes to trigger compression
    pub threshold: usize,
    /// Compression level (for Zstd: 1-22, lower is faster)
    pub level: i32,
}

impl Default for CompressionConfig {
    fn default() -> Self {
        Self {
            enabled: true,
            method: CompressionType::Lz4,
            threshold: 512,
            level: 3,
        }
    }
}

impl CompressionConfig {
    /// Load configuration from environment variables
    pub fn from_env() -> Self {
        let mut config = Self::default();

        if let Ok(val) = std::env::var("MULTIVERSE_COMPRESSION_ENABLED") {
            config.enabled = val == "1" || val.to_lowercase() == "true";
        }

        if let Ok(val) = std::env::var("MULTIVERSE_COMPRESSION_TYPE") {
            config.method = match val.to_lowercase().as_str() {
                "lz4" => CompressionType::Lz4,
                "zstd" => CompressionType::Zstd,
                "none" => CompressionType::None,
                _ => {
                    warn!("Unknown compression type '{}', using LZ4", val);
                    CompressionType::Lz4
                }
            };
        }

        if let Ok(val) = std::env::var("MULTIVERSE_COMPRESSION_THRESHOLD") {
            if let Ok(threshold) = val.parse::<usize>() {
                config.threshold = threshold;
            }
        }

        if let Ok(val) = std::env::var("MULTIVERSE_COMPRESSION_LEVEL") {
            if let Ok(level) = val.parse::<i32>() {
                config.level = level.clamp(1, 22);
            }
        }

        debug!("Compression config: enabled={}, method={}, threshold={} bytes, level={}",
            config.enabled, config.method, config.threshold, config.level);

        config
    }
}

pub struct Compressor {
    config: CompressionConfig,
}

impl Compressor {
    pub fn new(config: CompressionConfig) -> Self {
        Self { config }
    }

    /// Determine if compression should be applied based on data size
    pub fn should_compress(&self, data_size: usize) -> bool {
        self.config.enabled && data_size >= self.config.threshold
    }

    /// Get the configured compression type
    pub fn compression_type(&self) -> CompressionType {
        if self.config.enabled {
            self.config.method
        } else {
            CompressionType::None
        }
    }

    /// Compress data using the configured method
    /// Returns (compressed_data, compression_type)
    pub fn compress(&self, data: &[u8]) -> Result<(Vec<u8>, CompressionType)> {
        if !self.should_compress(data.len()) {
            return Ok((data.to_vec(), CompressionType::None));
        }

        match self.config.method {
            CompressionType::None => Ok((data.to_vec(), CompressionType::None)),
            CompressionType::Lz4 => self.compress_lz4(data),
            CompressionType::Zstd => self.compress_zstd(data),
        }
    }

    /// Decompress data using the specified method
    /// uncompressed_size is used for validation and pre-allocation
    pub fn decompress(
        &self,
        compressed_data: &[u8],
        compression_type: CompressionType,
        expected_uncompressed_size: usize,
    ) -> Result<Vec<u8>> {
        // Safety check: prevent decompression bombs
        if expected_uncompressed_size > MAX_UNCOMPRESSED_SIZE {
            anyhow::bail!(
                "Uncompressed size {} exceeds maximum allowed size {}",
                expected_uncompressed_size,
                MAX_UNCOMPRESSED_SIZE
            );
        }

        if compressed_data.len() > MAX_COMPRESSED_SIZE {
            anyhow::bail!(
                "Compressed size {} exceeds maximum allowed size {}",
                compressed_data.len(),
                MAX_COMPRESSED_SIZE
            );
        }

        match compression_type {
            CompressionType::None => Ok(compressed_data.to_vec()),
            CompressionType::Lz4 => self.decompress_lz4(compressed_data, expected_uncompressed_size),
            CompressionType::Zstd => self.decompress_zstd(compressed_data, expected_uncompressed_size),
        }
    }

    fn compress_lz4(&self, data: &[u8]) -> Result<(Vec<u8>, CompressionType)> {
        let start = std::time::Instant::now();
        let compressed = lz4_flex::compress_prepend_size(data);
        let duration = start.elapsed();

        let ratio = (compressed.len() as f64 / data.len() as f64) * 100.0;
        debug!(
            "[Compression] LZ4: {} → {} bytes ({:.1}%) in {:?}",
            data.len(),
            compressed.len(),
            ratio,
            duration
        );

        Ok((compressed, CompressionType::Lz4))
    }

    fn decompress_lz4(&self, compressed: &[u8], expected_size: usize) -> Result<Vec<u8>> {
        let start = std::time::Instant::now();
        let decompressed = lz4_flex::decompress_size_prepended(compressed)
            .context("LZ4 decompression failed")?;
        let duration = start.elapsed();

        // Validate decompressed size matches expected
        if decompressed.len() != expected_size {
            anyhow::bail!(
                "LZ4 decompressed size mismatch: got {}, expected {}",
                decompressed.len(),
                expected_size
            );
        }

        debug!(
            "[Decompression] LZ4: {} → {} bytes in {:?}",
            compressed.len(),
            decompressed.len(),
            duration
        );

        Ok(decompressed)
    }

    fn compress_zstd(&self, data: &[u8]) -> Result<(Vec<u8>, CompressionType)> {
        let start = std::time::Instant::now();
        let compressed = zstd::encode_all(data, self.config.level)
            .context("Zstd compression failed")?;
        let duration = start.elapsed();

        let ratio = (compressed.len() as f64 / data.len() as f64) * 100.0;
        debug!(
            "[Compression] Zstd({}): {} → {} bytes ({:.1}%) in {:?}",
            self.config.level,
            data.len(),
            compressed.len(),
            ratio,
            duration
        );

        Ok((compressed, CompressionType::Zstd))
    }

    fn decompress_zstd(&self, compressed: &[u8], expected_size: usize) -> Result<Vec<u8>> {
        let start = std::time::Instant::now();
        let decompressed = zstd::decode_all(compressed)
            .context("Zstd decompression failed")?;
        let duration = start.elapsed();

        // Validate decompressed size matches expected
        if decompressed.len() != expected_size {
            anyhow::bail!(
                "Zstd decompressed size mismatch: got {}, expected {}",
                decompressed.len(),
                expected_size
            );
        }

        debug!(
            "[Decompression] Zstd: {} → {} bytes in {:?}",
            compressed.len(),
            decompressed.len(),
            duration
        );

        Ok(decompressed)
    }
}

/// Encode message spec with compression information
pub fn encode_message_spec(base_spec: i32, compression: CompressionType) -> i32 {
    let comp_bits = compression.as_u8() as i32;
    base_spec | (comp_bits << 6)
}

/// Decode message spec to extract base spec and compression type
pub fn decode_message_spec(spec: i32) -> (i32, CompressionType) {
    let base = spec & 0x3F; // Lower 6 bits (message type + buffer count)
    let comp_bits = ((spec >> 6) & 0x03) as u8; // Bits 6-7
    let compression = CompressionType::from_u8(comp_bits);
    (base, compression)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_message_spec_encoding() {
        // Test case 1: No compression
        let spec = encode_message_spec(5, CompressionType::None);
        assert_eq!(spec, 5);
        let (base, comp) = decode_message_spec(spec);
        assert_eq!(base, 5);
        assert_eq!(comp, CompressionType::None);

        // Test case 2: LZ4 compression
        let spec = encode_message_spec(5, CompressionType::Lz4);
        assert_eq!(spec, 5 | (1 << 6)); // 69
        let (base, comp) = decode_message_spec(spec);
        assert_eq!(base, 5);
        assert_eq!(comp, CompressionType::Lz4);

        // Test case 3: Zstd compression
        let spec = encode_message_spec(3, CompressionType::Zstd);
        assert_eq!(spec, 3 | (2 << 6)); // 131
        let (base, comp) = decode_message_spec(spec);
        assert_eq!(base, 3);
        assert_eq!(comp, CompressionType::Zstd);
    }

    #[test]
    fn test_lz4_compression_round_trip() {
        let config = CompressionConfig::default();
        let compressor = Compressor::new(config);

        let data = vec![42u8; 1000]; // Highly compressible
        let (compressed, comp_type) = compressor.compress(&data).unwrap();

        assert_eq!(comp_type, CompressionType::Lz4);
        assert!(compressed.len() < data.len());

        let decompressed = compressor
            .decompress(&compressed, CompressionType::Lz4, data.len())
            .unwrap();

        assert_eq!(decompressed, data);
    }

    #[test]
    fn test_compression_threshold() {
        let mut config = CompressionConfig::default();
        config.threshold = 1000;
        let compressor = Compressor::new(config);

        // Data below threshold - should not compress
        let small_data = vec![1u8; 500];
        let (compressed, comp_type) = compressor.compress(&small_data).unwrap();
        assert_eq!(comp_type, CompressionType::None);
        assert_eq!(compressed.len(), small_data.len());

        // Data above threshold - should compress
        let large_data = vec![1u8; 2000];
        let (compressed, comp_type) = compressor.compress(&large_data).unwrap();
        assert_eq!(comp_type, CompressionType::Lz4);
        assert!(compressed.len() < large_data.len());
    }

    #[test]
    fn test_decompression_bomb_protection() {
        let config = CompressionConfig::default();
        let compressor = Compressor::new(config);

        let fake_compressed = vec![0u8; 100];
        let result = compressor.decompress(
            &fake_compressed,
            CompressionType::Lz4,
            MAX_UNCOMPRESSED_SIZE + 1,
        );

        assert!(result.is_err());
        assert!(result.unwrap_err().to_string().contains("exceeds maximum"));
    }
}
