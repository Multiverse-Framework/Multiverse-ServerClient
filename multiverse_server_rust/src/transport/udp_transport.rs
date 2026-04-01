#[cfg(feature = "use-udp")]
use super::interface::{Transport, TransportType};
use anyhow::{Context, Result};
use async_trait::async_trait;
use std::net::SocketAddr;
use tokio::net::UdpSocket;
use tracing::{debug, info};
use crate::protocol::raw_udp; 
use crate::utils::logging::hexdump;

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
        debug!("[UDP Transport] Flushing {} parts as one packet.", self.out_parts.len());
        raw_udp::send_parts(socket, &self.out_parts, self.peer.as_ref()).await?;
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
            debug!("[UDP Transport] Waiting for packet from connected peer...");
            self.in_parts = raw_udp::recv_parts(socket).await?;
        } else {
            debug!("[UDP Transport] Waiting for packet from any peer...");
            let (parts, sender) = raw_udp::recv_parts_from(socket).await?;
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
        debug!("[UDP Transport] Adding part to send queue ({} bytes, more: {})", data.len(), more);
        hexdump(data, 64);
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