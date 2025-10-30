use anyhow::{Context, Result};
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio::net::TcpStream;
use tracing::trace;

/// Write exactly n bytes to a TCP stream
pub async fn write_full(stream: &mut TcpStream, data: &[u8]) -> Result<()> {
    stream.write_all(data).await.context("TCP write failed")?;
    trace!("[TCP] Wrote {} bytes", data.len());
    Ok(())
}

/// Read exactly n bytes from a TCP stream
pub async fn read_full(stream: &mut TcpStream, buf: &mut [u8]) -> Result<()> {
    stream.read_exact(buf).await.context("TCP read failed")?;
    trace!("[TCP] Read {} bytes", buf.len());
    Ok(())
}

/// Send a vector of byte arrays as a multi-part message
/// Format: [part_count: u32][size1: u32][data1][size2: u32][data2]...
pub async fn send_parts(stream: &mut TcpStream, parts: &[Vec<u8>]) -> Result<()> {
    let num_parts = parts.len() as u32;
    write_full(stream, &num_parts.to_be_bytes()).await?;

    for part in parts {
        let size = part.len() as u32;
        write_full(stream, &size.to_be_bytes()).await?;
        if !part.is_empty() {
            write_full(stream, part).await?;
        }
    }

    stream.flush().await.context("TCP flush failed")?;
    trace!("[TCP] Sent {} parts", num_parts);
    Ok(())
}

/// Receive a multi-part message from a TCP stream
pub async fn recv_parts(stream: &mut TcpStream) -> Result<Vec<Vec<u8>>> {
    let mut buf = [0u8; 4];
    read_full(stream, &mut buf).await?;
    let num_parts = u32::from_be_bytes(buf);

    let mut parts = Vec::with_capacity(num_parts as usize);

    for i in 0..num_parts {
        read_full(stream, &mut buf).await
            .with_context(|| format!("Failed to read size for part {}", i))?;
        let size = u32::from_be_bytes(buf);

        let mut data = vec![0u8; size as usize];
        if size > 0 {
            read_full(stream, &mut data).await
                .with_context(|| format!("Failed to read data for part {}", i))?;
        }
        parts.push(data);
    }

    trace!("[TCP] Received {} parts", num_parts);
    Ok(parts)
}