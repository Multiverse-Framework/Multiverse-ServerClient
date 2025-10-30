#[cfg(feature = "use-udp")]
use super::interface::{Transport, TransportType};
use anyhow::{Context, Result};
use async_trait::async_trait;
use std::net::SocketAddr;
use tokio::net::UdpSocket;
use tracing::{debug, info};

const MAX_UDP_PAYLOAD: usize = 1200;

// Inline UDP protocol functions
fn encode_parts(parts: &[Vec<u8>]) -> Result<Vec<u8>> {
    let num_parts = parts.len() as u32;
    let mut needed = 4;
    for part in parts {
        needed += 4 + part.len();
    }
    if needed > MAX_UDP_PAYLOAD {
        anyhow::bail!("Payload {} exceeds max {}", needed, MAX_UDP_PAYLOAD);
    }
    let mut buf = Vec::with_capacity(needed);
    buf.extend_from_slice(&num_parts.to_be_bytes());
    for part in parts {
        let size = part.len() as u32;
        buf.extend_from_slice(&size.to_be_bytes());
        buf.extend_from_slice(part);
    }
    Ok(buf)
}

fn decode_parts(data: &[u8]) -> Result<Vec<Vec<u8>>> {
    if data.len() < 4 {
        anyhow::bail!("Packet too small");
    }
    let mut offset = 0;
    let num_parts = u32::from_be_bytes([data[0], data[1], data[2], data[3]]);
    offset += 4;
    let mut parts = Vec::with_capacity(num_parts as usize);
    for i in 0..num_parts {
        if offset + 4 > data.len() {
            anyhow::bail!("Failed to read size for part {}", i);
        }
        let size = u32::from_be_bytes([data[offset], data[offset + 1], data[offset + 2], data[offset + 3]]);
        offset += 4;
        if offset + size as usize > data.len() {
            anyhow::bail!("Truncated data for part {}", i);
        }
        let part = data[offset..offset + size as usize].to_vec();
        parts.push(part);
        offset += size as usize;
    }
    Ok(parts)
}

async fn send_parts_udp(socket: &UdpSocket, parts: &[Vec<u8>], peer: Option<&SocketAddr>) -> Result<()> {
    let packet = encode_parts(parts)?;
    let sent = if let Some(addr) = peer {
        socket.send_to(&packet, addr).await?
    } else {
        socket.send(&packet).await?
    };
    if sent != packet.len() {
        anyhow::bail!("UDP send partial: {}/{}", sent, packet.len());
    }
    Ok(())
}

async fn recv_parts_udp(socket: &UdpSocket) -> Result<Vec<Vec<u8>>> {
    let mut buf = vec![0u8; MAX_UDP_PAYLOAD];
    let n = socket.recv(&mut buf).await?;
    if n == 0 {
        anyhow::bail!("Socket closed");
    }
    decode_parts(&buf[..n])
}

async fn recv_parts_from_udp(socket: &UdpSocket) -> Result<(Vec<Vec<u8>>, SocketAddr)> {
    let mut buf = vec![0u8; MAX_UDP_PAYLOAD];
    let (n, sender) = socket.recv_from(&mut buf).await?;
    if n == 0 {
        anyhow::bail!("Socket closed");
    }
    let parts = decode_parts(&buf[..n])?;
    Ok((parts, sender))
}

enum Mode {
    Idle,
    ClientReady,
    ServerBound,
    ServerConnected,
}

pub struct UdpTransport {
    mode: Mode,
    socket: Option<UdpSocket>,
    endpoint: Option<String>,
    peer: Option<SocketAddr>,
    out_parts: Vec<Vec<u8>>,
    in_parts: Vec<Vec<u8>>,
    in_next: usize,
}

impl UdpTransport {
    pub fn new() -> Self {
        Self {
            mode: Mode::Idle,
            socket: None,
            endpoint: None,
            peer: None,
            out_parts: Vec::new(),
            in_parts: Vec::new(),
            in_next: 0,
        }
    }

    async fn flush_out_parts(&mut self) -> Result<()> {
        if self.out_parts.is_empty() {
            return Ok(());
        }

        let socket = self.socket.as_ref().context("No socket")?;
        send_parts_udp(socket, &self.out_parts, self.peer.as_ref()).await?;
        self.out_parts.clear();
        Ok(())
    }

    async fn ensure_in_parts(&mut self) -> Result<()> {
        if self.in_next < self.in_parts.len() {
            return Ok(());
        }

        self.in_parts.clear();
        self.in_next = 0;

        let socket = self.socket.as_ref().context("No socket")?;

        if self.peer.is_some() {
            self.in_parts = recv_parts_udp(socket).await?;
        } else {
            let (parts, sender) = recv_parts_from_udp(socket).await?;
            self.peer = Some(sender);
            self.in_parts = parts;
            self.endpoint = Some(sender.to_string());

            if matches!(self.mode, Mode::ServerBound) {
                self.mode = Mode::ServerConnected;
            }

            info!("[UDP] Peer established: {}", sender);
        }

        Ok(())
    }
}

#[async_trait]
impl Transport for UdpTransport {
    fn transport_type(&self) -> TransportType {
        TransportType::Udp
    }

    async fn connect(&mut self, endpoint: &str) -> Result<()> {
        let socket = UdpSocket::bind("0.0.0.0:0").await?;
        socket.connect(endpoint).await?;
        
        self.peer = Some(socket.peer_addr()?);
        self.socket = Some(socket);
        self.endpoint = Some(endpoint.to_string());
        self.mode = Mode::ClientReady;
        
        info!("[UDP] Connected to {}", endpoint);
        Ok(())
    }

    async fn disconnect(&mut self, _endpoint: &str) -> Result<()> {
        self.socket = None;
        self.endpoint = None;
        self.peer = None;
        self.out_parts.clear();
        self.in_parts.clear();
        self.in_next = 0;
        self.mode = Mode::Idle;
        debug!("[UDP] Disconnected");
        Ok(())
    }

    async fn listen(&mut self, endpoint: &str) -> Result<()> {
        let socket = UdpSocket::bind(endpoint).await?;
        let local_addr = socket.local_addr()?;
        self.socket = Some(socket);
        self.endpoint = Some(local_addr.to_string());
        self.mode = Mode::ServerBound;
        self.peer = None;

        info!("[UDP] Bound on {}", local_addr);
        Ok(())
    }

    async fn accept(&mut self) -> Result<bool> {
        anyhow::bail!("UDP does not support accept(); peer is established on first recv")
    }

    async fn bind(&mut self, endpoint: &str) -> Result<()> {
        self.listen(endpoint).await
    }

    async fn unbind(&mut self, endpoint: &str) -> Result<()> {
        self.disconnect(endpoint).await
    }

    async fn send(&mut self, data: &[u8], more: bool) -> Result<()> {
        self.out_parts.push(data.to_vec());
        if !more {
            self.flush_out_parts().await?;
        }
        Ok(())
    }

    async fn recv(&mut self, buf: &mut [u8]) -> Result<usize> {
        self.ensure_in_parts().await?;

        if self.in_next >= self.in_parts.len() {
            anyhow::bail!("No frames available");
        }

        let frame = &self.in_parts[self.in_next];
        self.in_next += 1;

        if frame.len() != buf.len() {
            anyhow::bail!("Size mismatch: expected {}, got {}", buf.len(), frame.len());
        }

        buf.copy_from_slice(frame);
        Ok(frame.len())
    }

    async fn recv_text(&mut self) -> Result<String> {
        self.ensure_in_parts().await?;

        if self.in_next >= self.in_parts.len() {
            anyhow::bail!("No frames available");
        }

        let frame = &self.in_parts[self.in_next];
        self.in_next += 1;

        String::from_utf8(frame.clone()).context("Invalid UTF-8")
    }

    async fn recv_multipart(&mut self) -> Result<Vec<Vec<u8>>> {
        self.ensure_in_parts().await?;

        if self.in_parts.is_empty() {
            anyhow::bail!("No data received");
        }

        let parts = std::mem::take(&mut self.in_parts);
        self.in_next = 0;

        debug!("[UDP] Received {} parts", parts.len());
        Ok(parts)
    }
}