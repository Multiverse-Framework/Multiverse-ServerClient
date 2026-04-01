use anyhow::Result;
use async_trait::async_trait;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum TransportType {
    #[cfg(feature = "use-zmq")]
    Zmq,
    #[cfg(feature = "use-tcp")]
    Tcp,
    #[cfg(feature = "use-udp")]
    Udp,
}

#[async_trait]
pub trait Transport: Send + Sync {
    /// Get the transport type
    fn transport_type(&self) -> TransportType;

    /// Connect to a remote endpoint (client mode)
    async fn connect(&mut self, endpoint: &str) -> Result<()>;

    /// Disconnect from a remote endpoint
    async fn disconnect(&mut self, endpoint: &str) -> Result<()>;

    /// Listen for incoming connections (server mode)
    async fn listen(&mut self, endpoint: &str) -> Result<()>;

    /// Accept an incoming connection (server mode)
    async fn accept(&mut self) -> Result<bool>;

    /// Bind to an endpoint
    async fn bind(&mut self, endpoint: &str) -> Result<()>;

    /// Unbind from an endpoint
    async fn unbind(&mut self, endpoint: &str) -> Result<()>;

    /// Send a binary frame. If 'more' is false, the buffered message is sent.
    async fn send(&mut self, data: &[u8], more: bool) -> Result<()>;

    /// Receive a binary frame of an exact size
    async fn recv(&mut self, buf: &mut [u8]) -> Result<usize>;

    /// Send a text frame
    async fn send_text(&mut self, text: &str, more: bool) -> Result<()> {
        self.send(text.as_bytes(), more).await
    }

    /// Receive a text frame
    async fn recv_text(&mut self) -> Result<String>;

    /// Receive a full multipart message
    async fn recv_multipart(&mut self) -> Result<Vec<Vec<u8>>>;

    /// Check if there's a pending connection on the listener socket (non-blocking)
    /// Used by TCP to detect when a new client is trying to connect while waiting
    /// for data from the current client. Default returns false for transports that
    /// don't support this (ZMQ, UDP).
    fn has_pending_connection(&self) -> bool {
        false
    }
}