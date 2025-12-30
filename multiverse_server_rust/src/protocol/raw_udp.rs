use anyhow::{Context, Result};
use std::net::SocketAddr;
use tokio::net::UdpSocket;
use tokio::time::{timeout, Duration};
use tracing::trace;

const MAX_UDP_PAYLOAD: usize = 2000;

/// Encode parts into a contiguous buffer for UDP transmission
/// Layout: [part_count: u32][size1: u32][data1]...[sizeN: u32][dataN]
/// All u32 fields use local (little-endian) byte order
pub fn encode_parts(parts: &[Vec<u8>]) -> Result<Vec<u8>> {
    let num_parts = parts.len() as u32;
    let mut needed = 4; // part_count
    for part in parts {
        needed += 4; // size
        needed += part.len(); // data
    }

    if needed > MAX_UDP_PAYLOAD {
        anyhow::bail!(
            "Encoded payload {} exceeds MAX_UDP_PAYLOAD {}",
            needed,
            MAX_UDP_PAYLOAD
        );
    }

    let mut buf = Vec::with_capacity(needed);

    // Write part count
    buf.extend_from_slice(&num_parts.to_le_bytes());

    // Write each part
    for part in parts {
        let size = part.len() as u32;
        buf.extend_from_slice(&size.to_le_bytes());
        buf.extend_from_slice(part);
    }

    Ok(buf)
}

/// Decode a UDP datagram into parts
pub fn decode_parts(data: &[u8]) -> Result<Vec<Vec<u8>>> {
    if data.len() < 4 {
        anyhow::bail!("Packet too small for part_count");
    }

    let mut offset = 0;

    // Read part count
    let num_parts = u32::from_le_bytes([data[0], data[1], data[2], data[3]]);
    offset += 4;

    let mut parts = Vec::with_capacity(num_parts as usize);

    for i in 0..num_parts {
        if offset + 4 > data.len() {
            anyhow::bail!("Failed to read size for part {}", i);
        }

        let size = u32::from_le_bytes([
            data[offset],
            data[offset + 1],
            data[offset + 2],
            data[offset + 3],
        ]);
        offset += 4;

        if offset + size as usize > data.len() {
            anyhow::bail!("Truncated data for part {} (need {} bytes)", i, size);
        }

        let part = data[offset..offset + size as usize].to_vec();
        parts.push(part);
        offset += size as usize;
    }

    Ok(parts)
}

/// Send parts over a connected UDP socket
pub async fn send_parts(
    socket: &UdpSocket,
    parts: &[Vec<u8>],
    peer: Option<&SocketAddr>,
) -> Result<()> {
    let packet = encode_parts(parts)?;

    let sent = if let Some(addr) = peer {
        socket
            .send_to(&packet, addr)
            .await
            .context("UDP sendto failed")?
    } else {
        socket.send(&packet).await.context("UDP send failed")?
    };

    if sent != packet.len() {
        anyhow::bail!("UDP send partial: {}/{}", sent, packet.len());
    }

    trace!("[UDP] Sent {} parts ({} bytes)", parts.len(), sent);
    Ok(())
}

/// Receive parts from a connected UDP socket
pub async fn recv_parts(socket: &UdpSocket) -> Result<Vec<Vec<u8>>> {
    let mut buf = vec![0u8; MAX_UDP_PAYLOAD];
    let result = timeout(Duration::from_millis(100), socket.recv(&mut buf)).await;
    let n = match result {
        Ok(Ok(n)) => n,
        Ok(Err(e)) => {
            return Err(e).context("UDP recv failed");
        }
        Err(_elapsed) => {
            trace!("[UDP] recv timeout after 1 second");
            anyhow::bail!("UDP recv timeout");
        }
    };

    if n == 0 {
        anyhow::bail!("UDP socket closed");
    }

    let parts = decode_parts(&buf[..n])?;
    trace!("[UDP] Received {} parts ({} bytes)", parts.len(), n);
    Ok(parts)
}

/// Receive parts from an unconnected UDP socket (with sender address)
pub async fn recv_parts_from(socket: &UdpSocket) -> Result<(Vec<Vec<u8>>, SocketAddr)> {
    let mut buf = vec![0u8; MAX_UDP_PAYLOAD];
    let (n, sender) = socket
        .recv_from(&mut buf)
        .await
        .context("UDP recvfrom failed")?;

    if n == 0 {
        anyhow::bail!("UDP socket closed");
    }

    let parts = decode_parts(&buf[..n])?;
    trace!(
        "[UDP] Received {} parts ({} bytes) from {}",
        parts.len(),
        n,
        sender
    );
    Ok((parts, sender))
}