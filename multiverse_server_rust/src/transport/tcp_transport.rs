#[cfg(feature = "use-tcp")]
use super::interface::{Transport, TransportType};
use anyhow::{Context, Result};
use async_trait::async_trait;
use std::time::Duration;
use tokio::net::{TcpListener, TcpStream};
use tokio::time::sleep;
use tracing::{debug, info, warn};

// Inline TCP protocol functions
async fn send_parts(stream: &mut TcpStream, parts: &[Vec<u8>]) -> Result<()> {
    use tokio::io::AsyncWriteExt;
    
    let num_parts = parts.len() as u32;
    stream.write_all(&num_parts.to_be_bytes()).await?;

    for part in parts {
        let size = part.len() as u32;
        stream.write_all(&size.to_be_bytes()).await?;
        if !part.is_empty() {
            stream.write_all(part).await?;
        }
    }

    stream.flush().await?;
    Ok(())
}

async fn recv_parts(stream: &mut TcpStream) -> Result<Vec<Vec<u8>>> {
    use tokio::io::AsyncReadExt;
    
    let mut buf = [0u8; 4];
    stream.read_exact(&mut buf).await?;
    let num_parts = u32::from_be_bytes(buf);

    let mut parts = Vec::with_capacity(num_parts as usize);

    for i in 0..num_parts {
        stream.read_exact(&mut buf).await
            .with_context(|| format!("Failed to read size for part {}", i))?;
        let size = u32::from_be_bytes(buf);

        let mut data = vec![0u8; size as usize];
        if size > 0 {
            stream.read_exact(&mut data).await
                .with_context(|| format!("Failed to read data for part {}", i))?;
        }
        parts.push(data);
    }

    Ok(parts)
}

enum Mode {
    Idle,
    ClientConnected,
    ServerListening,
    ServerConnected,
}

pub struct TcpTransport {
    mode: Mode,
    stream: Option<TcpStream>,
    listener: Option<TcpListener>,
    endpoint: Option<String>,
    out_parts: Vec<Vec<u8>>,
    in_parts: Vec<Vec<u8>>,
    in_next: usize,
}

impl TcpTransport {
    pub fn new() -> Self {
        Self {
            mode: Mode::Idle,
            stream: None,
            listener: None,
            endpoint: None,
            out_parts: Vec::new(),
            in_parts: Vec::new(),
            in_next: 0,
        }
    }

    async fn flush_out_parts(&mut self) -> Result<()> {
        if self.out_parts.is_empty() {
            return Ok(());
        }

        let stream = self.stream.as_mut().context("Not connected")?;
        send_parts(stream, &self.out_parts).await?;
        self.out_parts.clear();
        Ok(())
    }

    async fn ensure_in_parts(&mut self) -> Result<()> {
        if self.in_next < self.in_parts.len() {
            return Ok(());
        }

        self.in_parts.clear();
        self.in_next = 0;

        let stream = self.stream.as_mut().context("Not connected")?;
        self.in_parts = recv_parts(stream).await?;
        Ok(())
    }
}

#[async_trait]
impl Transport for TcpTransport {
    fn transport_type(&self) -> TransportType {
        TransportType::Tcp
    }

    async fn connect(&mut self, endpoint: &str) -> Result<()> {
        const MAX_ATTEMPTS: usize = 12;
        const SLEEP_MS: u64 = 200;

        for attempt in 1..=MAX_ATTEMPTS {
            match TcpStream::connect(endpoint).await {
                Ok(stream) => {
                    stream.set_nodelay(true)?;
                    self.stream = Some(stream);
                    self.endpoint = Some(endpoint.to_string());
                    self.mode = Mode::ClientConnected;
                    info!(
                        "[TCP] Connected to {} (attempt {}/{})",
                        endpoint, attempt, MAX_ATTEMPTS
                    );
                    return Ok(());
                }
                Err(e) => {
                    if attempt < MAX_ATTEMPTS {
                        warn!(
                            "[TCP] Connect failed (attempt {}/{}): {} -> retrying in {} ms",
                            attempt, MAX_ATTEMPTS, e, SLEEP_MS
                        );
                        sleep(Duration::from_millis(SLEEP_MS)).await;
                    } else {
                        anyhow::bail!("TCP connect failed after {} attempts: {}", MAX_ATTEMPTS, e);
                    }
                }
            }
        }

        unreachable!()
    }

    async fn disconnect(&mut self, _endpoint: &str) -> Result<()> {
        self.stream = None;
        self.listener = None;
        self.endpoint = None;
        self.out_parts.clear();
        self.in_parts.clear();
        self.in_next = 0;
        self.mode = Mode::Idle;
        debug!("[TCP] Disconnected");
        Ok(())
    }

    async fn listen(&mut self, endpoint: &str) -> Result<()> {
        let listener = TcpListener::bind(endpoint)
            .await
            .context("TCP listen failed")?;
        self.listener = Some(listener);
        self.endpoint = Some(endpoint.to_string());
        self.mode = Mode::ServerListening;
        info!("[TCP] Listening on {}", endpoint);
        Ok(())
    }

    async fn accept(&mut self) -> Result<bool> {
        let listener = self.listener.as_ref().context("Not listening")?;

        match listener.accept().await {
            Ok((stream, addr)) => {
                stream.set_nodelay(true)?;
                self.stream = Some(stream);
                self.endpoint = Some(addr.to_string());
                self.mode = Mode::ServerConnected;
                self.out_parts.clear();
                self.in_parts.clear();
                self.in_next = 0;
                info!("[TCP] Accepted connection from {}", addr);
                Ok(true)
            }
            Err(e) => {
                warn!("[TCP] Accept failed: {}", e);
                Ok(false)
            }
        }
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
            anyhow::bail!(
                "Size mismatch: expected {}, got {}",
                buf.len(),
                frame.len()
            );
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
            anyhow::bail!("Connection closed");
        }

        let parts = std::mem::take(&mut self.in_parts);
        self.in_next = 0;

        debug!("[TCP] Received {} parts", parts.len());
        Ok(parts)
    }
}