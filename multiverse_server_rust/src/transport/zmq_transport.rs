#[cfg(feature = "use-zmq")]
use super::interface::{Transport, TransportType};
use anyhow::{Context, Result};
use async_trait::async_trait;
use std::sync::Arc;
use parking_lot::Mutex;
use tracing::{debug, error};
use crate::utils::logging::{hexdump, ascii_preview, dump_payloads};

#[allow(dead_code)]
pub struct ZmqTransport {
    context: Arc<zmq::Context>,
    socket: Arc<Mutex<zmq::Socket>>,
    endpoint: Option<String>,
}

impl ZmqTransport {
    pub fn new(socket_type: zmq::SocketType) -> Result<Self> {
        let context = zmq::Context::new();
        let socket = context.socket(socket_type)?;

        // Set linger to 0 to allow immediate context termination
        socket.set_linger(0)?;

        // Set receive timeout to 1000ms so we can check shutdown flag periodically
        socket.set_rcvtimeo(1000)?;

        Ok(Self {
            context: Arc::new(context),
            socket: Arc::new(Mutex::new(socket)),
            endpoint: None,
        })
    }

    pub fn new_rep() -> Result<Self> {
        Self::new(zmq::REP)
    }

    pub fn new_req() -> Result<Self> {
        Self::new(zmq::REQ)
    }
}

#[async_trait]
impl Transport for ZmqTransport {
    fn transport_type(&self) -> TransportType {
        TransportType::Zmq
    }

    async fn connect(&mut self, endpoint: &str) -> Result<()> {
        let socket = self.socket.lock();
        socket
            .connect(endpoint)
            .context("ZMQ connect failed")?;
        drop(socket);
        self.endpoint = Some(endpoint.to_string());
        debug!("[ZMQ] Connected to {}", endpoint);
        Ok(())
    }

    async fn disconnect(&mut self, endpoint: &str) -> Result<()> {
        let socket = self.socket.lock();
        // Set linger to 0 to immediately discard pending messages
        socket.set_linger(0)?;
        socket
            .disconnect(endpoint)
            .context("ZMQ disconnect failed")?;
        drop(socket);
        self.endpoint = None;
        debug!("[ZMQ] Disconnected from {}", endpoint);
        Ok(())
    }

    async fn listen(&mut self, _endpoint: &str) -> Result<()> {
        anyhow::bail!("ZMQ REP socket does not support listen/accept pattern. Use bind() instead.")
    }

    async fn accept(&mut self) -> Result<bool> {
        anyhow::bail!("ZMQ REP socket does not support listen/accept pattern.")
    }

    async fn bind(&mut self, endpoint: &str) -> Result<()> {
        let socket = self.socket.lock();
        socket.bind(endpoint).context("ZMQ bind failed")?;
        drop(socket);
        self.endpoint = Some(endpoint.to_string());
        debug!("[ZMQ] Bound to {}", endpoint);
        Ok(())
    }

    async fn unbind(&mut self, endpoint: &str) -> Result<()> {
        let socket = self.socket.lock();
        // Set linger to 0 to immediately discard pending messages
        socket.set_linger(0)?;
        socket.unbind(endpoint).context("ZMQ unbind failed")?;
        drop(socket);
        self.endpoint = None;
        debug!("[ZMQ] Unbound from {}", endpoint);
        Ok(())
    }

    async fn send(&mut self, data: &[u8], more: bool) -> Result<()> {
        let flags = if more { zmq::SNDMORE } else { 0 };

        // --- Updated Debug Logging ---
        debug!(
            "[ZMQ] Sending {} bytes (more: {}): {}",
            data.len(),
            more,
            ascii_preview(data, 64)
        );
        hexdump(data, 64);

        let socket = self.socket.lock();
        socket
            .send(data, flags)
            .context("ZMQ send failed")?;
        Ok(())
    }

    async fn recv(&mut self, buf: &mut [u8]) -> Result<usize> {
        let socket = self.socket.lock();
        let msg = socket.recv_msg(0).context("ZMQ recv failed")?;
        let len = msg.len().min(buf.len());
        buf[..len].copy_from_slice(&msg[..len]);

        let received_data = &buf[..len];
        debug!(
            "[ZMQ] Received {} bytes: {}",
            len,
            ascii_preview(received_data, 64)
        );
        hexdump(received_data, 64);
        Ok(len)
    }

    async fn recv_text(&mut self) -> Result<String> {
        let socket = self.socket.lock();
        let msg = socket.recv_msg(0).context("ZMQ recv_text failed")?;

        // --- Updated Debug Logging ---
        debug!(
            "[ZMQ] Received {} text bytes: {}",
            msg.len(),
            ascii_preview(&msg, 64)
        );
        hexdump(&msg, 64);

        String::from_utf8(msg.to_vec()).context("Invalid UTF-8 in message")
    }

    async fn recv_multipart(&mut self) -> Result<Vec<Vec<u8>>> {
        let socket = self.socket.lock();
        let mut parts = Vec::new();
        loop {
            match socket.recv_msg(0) {
                Ok(msg) => {
                    parts.push(msg.to_vec());
                    if !socket.get_rcvmore()? {
                        break;
                    }
                }
                Err(zmq::Error::EAGAIN) => {
                    // Timeout - return error so caller can check shutdown
                    drop(socket);
                    anyhow::bail!("ZMQ receive timeout");
                }
                Err(e) => {
                    drop(socket);
                    return Err(e).context("ZMQ recv_multipart failed");
                }
            }
        }
        drop(socket);
        dump_payloads(&parts);

        Ok(parts)
    }
}

impl Drop for ZmqTransport {
    fn drop(&mut self) {
        let socket = self.socket.lock();
        // Set linger to 0 to immediately discard pending messages
        if let Err(e) = socket.set_linger(0) {
            error!("[ZMQ] Error setting linger in drop: {}", e);
        }

        if let Some(endpoint) = &self.endpoint {
            if let Err(e) = socket.disconnect(endpoint) {
                error!("[ZMQ] Error disconnecting from {}: {}", endpoint, e);
            }
        }
    }
}