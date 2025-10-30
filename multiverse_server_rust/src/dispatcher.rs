use anyhow::{Context, Result};
use std::collections::HashMap;
use std::sync::Arc;
// use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio::net::{TcpListener, TcpStream, UdpSocket};
use tokio::sync::Mutex;
use tokio::task::JoinHandle;
use tokio::time::{sleep, Duration};
use tracing::{debug, error, info, warn};
use std::thread::JoinHandle as StdJoinHandle; // Use standard thread JoinHandle
use tokio::runtime::Builder; // Need the runtime Builder

use crate::server::MultiverseServer;
use crate::utils::should_shutdown;

/// Multipart message protocol for handshake
pub mod protocol {
    use anyhow::Result;
    use tokio::io::{AsyncReadExt, AsyncWriteExt};

    /// Send multipart message over TCP
    pub async fn send_parts_tcp<W>(writer: &mut W, parts: &[Vec<u8>]) -> Result<()>
    where
        W: AsyncWriteExt + Unpin,
    {
        let num_parts = parts.len() as u32;
        writer.write_all(&num_parts.to_be_bytes()).await?;

        for part in parts {
            let size = part.len() as u32;
            writer.write_all(&size.to_be_bytes()).await?;
            if size > 0 {
                writer.write_all(part).await?;
            }
        }
        writer.flush().await?;
        Ok(())
    }

    /// Receive multipart message over TCP
    pub async fn recv_parts_tcp<R>(reader: &mut R) -> Result<Vec<Vec<u8>>>
    where
        R: AsyncReadExt + Unpin,
    {
        let mut num_parts_buf = [0u8; 4];
        reader.read_exact(&mut num_parts_buf).await?;
        let num_parts = u32::from_be_bytes(num_parts_buf);

        let mut parts = Vec::with_capacity(num_parts as usize);
        for _ in 0..num_parts {
            let mut size_buf = [0u8; 4];
            reader.read_exact(&mut size_buf).await?;
            let size = u32::from_be_bytes(size_buf) as usize;

            let mut data = vec![0u8; size];
            if size > 0 {
                reader.read_exact(&mut data).await?;
            }
            parts.push(data);
        }
        Ok(parts)
    }

    /// Encode multipart message for UDP
    pub fn encode_parts_udp(parts: &[Vec<u8>]) -> Result<Vec<u8>> {
        const MAX_UDP_PAYLOAD: usize = 1200;

        let mut buf = Vec::new();
        let num_parts = parts.len() as u32;
        buf.extend_from_slice(&num_parts.to_be_bytes());

        for part in parts {
            let size = part.len() as u32;
            buf.extend_from_slice(&size.to_be_bytes());
            buf.extend_from_slice(part);
        }

        if buf.len() > MAX_UDP_PAYLOAD {
            anyhow::bail!(
                "Encoded payload {} exceeds MAX_UDP_PAYLOAD={}",
                buf.len(),
                MAX_UDP_PAYLOAD
            );
        }

        Ok(buf)
    }

    /// Decode UDP multipart message
    pub fn decode_parts_udp(buf: &[u8]) -> Result<Vec<Vec<u8>>> {
        if buf.len() < 4 {
            anyhow::bail!("Packet too small");
        }

        let mut pos = 0;
        let num_parts = u32::from_be_bytes([buf[0], buf[1], buf[2], buf[3]]) as usize;
        pos += 4;

        let mut parts = Vec::with_capacity(num_parts);
        for _ in 0..num_parts {
            if pos + 4 > buf.len() {
                anyhow::bail!("Truncated size field");
            }
            let size = u32::from_be_bytes([buf[pos], buf[pos + 1], buf[pos + 2], buf[pos + 3]])
                as usize;
            pos += 4;

            if pos + size > buf.len() {
                anyhow::bail!("Truncated data");
            }
            parts.push(buf[pos..pos + size].to_vec());
            pos += size;
        }

        Ok(parts)
    }
}

/// TCP Dispatcher - listens for handshake requests and spawns workers
pub async fn start_tcp_dispatcher(host: String, port: String) -> Result<()> {
    let addr = format!("{}:{}", host, port);
    let listener = TcpListener::bind(&addr)
        .await
        .context(format!("Failed to bind TCP listener on {}", addr))?;

    info!("[Server-TCP] Dispatcher listening on {}", addr);

    let workers: Arc<Mutex<HashMap<String, JoinHandle<()>>>> =
        Arc::new(Mutex::new(HashMap::new()));

    loop {
        // Check shutdown flag
        if should_shutdown() {
            break;
        }

        // Accept with timeout
        let stream = match tokio::time::timeout(Duration::from_secs(1), listener.accept()).await {
            Ok(Ok((stream, client_addr))) => {
                debug!("[Server-TCP] Client connected from {}", client_addr);
                stream
            }
            Ok(Err(e)) => {
                error!("[Server-TCP] Accept error: {}", e);
                continue;
            }
            Err(_) => {
                // Timeout - check shutdown and continue
                continue;
            }
        };

        // Handle handshake in separate task
        let workers_clone = Arc::clone(&workers);
        let host_clone = host.clone();
        let port_base: u16 = port.parse().unwrap_or(7000);

        tokio::spawn(async move {
            if let Err(e) =
                handle_tcp_handshake(stream, workers_clone, host_clone, port_base).await
            {
                error!("[Server-TCP] Handshake error: {}", e);
            }
        });
    }

    info!("[Server-TCP] Dispatcher shutting down, waiting for workers...");

    // Wait for all workers
    let mut workers = workers.lock().await;
    for (addr, handle) in workers.drain() {
        info!("[Server-TCP] Waiting for worker {}", addr);
        let _ = handle.await;
    }

    info!("[Server-TCP] Dispatcher stopped.");
    Ok(())
}

async fn handle_tcp_handshake(
    mut stream: TcpStream,
    workers: Arc<Mutex<HashMap<String, JoinHandle<()>>>>,
    host: String,
    port_base: u16,
) -> Result<()> {
    // Receive handshake request
    let parts = protocol::recv_parts_tcp(&mut stream).await?;

    if parts.is_empty() || parts[0].is_empty() {
        anyhow::bail!("Empty handshake request");
    }

    let request = String::from_utf8_lossy(&parts[0]).to_string();
    debug!("[Server-TCP] Received handshake request: {:?}", request);

    // Parse requested port from format "host:port"
    let worker_port = if let Some(pos) = request.rfind(':') {
        request[pos + 1..]
            .parse::<u16>()
            .unwrap_or(port_base + 1)
    } else {
        port_base + 1
    };

    let worker_addr = format!("{}:{}", host, worker_port);
    info!("[Server-TCP] Launching worker for {}", worker_addr);

    // Spawn worker if not already running
    let mut workers_map = workers.lock().await;
    if !workers_map.contains_key(&worker_addr) {
        let worker_host = host.clone();
        let worker_port_str = worker_port.to_string();
        let worker_addr_clone = worker_addr.clone(); // Clone before moving

        let handle = tokio::task::spawn_local(async move {
            if let Err(e) = run_tcp_worker(&worker_host, &worker_port_str).await {
                error!("[Server-TCP-Worker] Error on {}: {}", worker_addr_clone, e);
            }
        });

        workers_map.insert(worker_addr.clone(), handle);

        // Give worker time to start
        drop(workers_map);
        sleep(Duration::from_millis(500)).await;
    } else {
        drop(workers_map);
    }

    // Send response with worker address
    let response = vec![worker_addr.as_bytes().to_vec()];
    protocol::send_parts_tcp(&mut stream, &response).await?;

    debug!("[Server-TCP] Handshake complete for {}", worker_addr);
    Ok(())
}

async fn run_tcp_worker(host: &str, port: &str) -> Result<()> {
    info!("[TCP-Worker] Starting worker on {}:{}", host, port);

    let mut server = MultiverseServer::new_tcp(host, port).await?;
    server.start().await?;

    info!("[TCP-Worker] Worker stopped on {}:{}", host, port);
    Ok(())
}

/// UDP Dispatcher - listens for handshake datagrams and spawns workers
pub async fn start_udp_dispatcher(host: String, port: String) -> Result<()> {
    let addr = format!("{}:{}", host, port);
    let socket = UdpSocket::bind(&addr)
        .await
        .context(format!("Failed to bind UDP socket on {}", addr))?;

    info!("[Server-UDP] Dispatcher binding on {}", addr);

    let workers: Arc<Mutex<HashMap<String, JoinHandle<()>>>> =
        Arc::new(Mutex::new(HashMap::new()));

    let mut buf = vec![0u8; 1200];

    loop {
        // Check shutdown flag
        if should_shutdown() {
            break;
        }

        // Receive with timeout
        let (size, client_addr) = match tokio::time::timeout(
            Duration::from_secs(1),
            socket.recv_from(&mut buf),
        )
        .await
        {
            Ok(Ok((size, addr))) => (size, addr),
            Ok(Err(e)) => {
                error!("[Server-UDP] Receive error: {}", e);
                continue;
            }
            Err(_) => {
                // Timeout - check shutdown and continue
                continue;
            }
        };

        // Handle handshake
        let parts = match protocol::decode_parts_udp(&buf[..size]) {
            Ok(parts) => parts,
            Err(e) => {
                error!("[Server-UDP] Failed to decode handshake: {}", e);
                continue;
            }
        };

        if parts.is_empty() || parts[0].is_empty() {
            warn!("[Server-UDP] Empty handshake request");
            continue;
        }

        let request = String::from_utf8_lossy(&parts[0]).to_string();
        debug!(
            "[Server-UDP] Received handshake request: {:?} from {}",
            request, client_addr
        );

        // Parse requested port
        let port_base: u16 = port.parse().unwrap_or(7000);
        let worker_port = if let Some(pos) = request.rfind(':') {
            request[pos + 1..]
                .parse::<u16>()
                .unwrap_or(port_base + 1)
        } else {
            port_base + 1
        };

        let worker_addr = format!("{}:{}", host, worker_port);
        info!("[Server-UDP] Launching worker for {}", worker_addr);

        // Spawn worker if not already running
        let mut workers_map = workers.lock().await;
        if !workers_map.contains_key(&worker_addr) {
            let worker_host = host.clone();
            let worker_port_str = worker_port.to_string();
            let worker_addr_clone = worker_addr.clone(); // Clone before moving

            let handle = tokio::task::spawn_local(async move {
                if let Err(e) = run_udp_worker(&worker_host, &worker_port_str).await {
                    error!(
                        "[Server-UDP-Worker] Error on {}:{}: {}",
                        worker_host, worker_port_str, e
                    );
                }
            });

            workers_map.insert(worker_addr.clone(), handle);

            // Give worker time to start
            drop(workers_map);
            sleep(Duration::from_millis(300)).await;
        } else {
            drop(workers_map);
        }

        // Send response with worker address
        let response = vec![worker_addr.as_bytes().to_vec()];
        if let Ok(encoded) = protocol::encode_parts_udp(&response) {
            if let Err(e) = socket.send_to(&encoded, client_addr).await {
                error!("[Server-UDP] Failed to send handshake response: {}", e);
            }
        }

        debug!("[Server-UDP] Handshake complete for {}", worker_addr);
    }

    info!("[Server-UDP] Dispatcher shutting down, waiting for workers...");

    // Wait for all workers
    let mut workers = workers.lock().await;
    for (addr, handle) in workers.drain() {
        info!("[Server-UDP] Waiting for worker {}", addr);
        let _ = handle.await;
    }

    info!("[Server-UDP] Dispatcher stopped.");
    Ok(())
}

async fn run_udp_worker(host: &str, port: &str) -> Result<()> {
    info!("[UDP-Worker] Starting worker on {}:{}", host, port);

    let mut server = MultiverseServer::new_udp(host, port).await?;
    server.start().await?;

    info!("[UDP-Worker] Worker stopped on {}:{}", host, port);
    Ok(())
}

//
// --- ZMQ Dispatcher ---
//

pub async fn start_zmq_dispatcher(bind_addr: String) -> Result<()> {
    // This dispatcher will manage its own threads.
    // We use std::sync::Mutex and std::thread::JoinHandle
    let workers: Arc<std::sync::Mutex<HashMap<String, StdJoinHandle<()>>>> =
        Arc::new(std::sync::Mutex::new(HashMap::new()));

    tokio::task::spawn_blocking(move || {
        // This closure runs on a blocking thread.
        let context = zmq::Context::new();
        let socket = match context.socket(zmq::REP) {
            Ok(s) => s,
            Err(e) => {
                error!("[Server-ZMQ] Failed to create socket: {}", e);
                return Err(e.into());
            }
        };

        if let Err(e) = socket.bind(&bind_addr) {
            error!("[Server-ZMQ] Failed to bind dispatcher on {}: {}", bind_addr, e);
            return Err(e.into());
        }
        info!("[Server-ZMQ] Dispatcher bound on {}", bind_addr);

        // Set a receive timeout so the loop can check should_shutdown()
        socket.set_rcvtimeo(1000)?; // 1 second timeout

        let workers_clone = Arc::clone(&workers);

        while !should_shutdown() {
            let mut msg = zmq::Message::new();
            match socket.recv(&mut msg, 0) {
                Ok(_) => {
                    // We received a message
                    let worker_addr = match msg.as_str() {
                        Some(addr) => addr.to_string(),
                        None => {
                            error!("[Server-ZMQ] Received non-UTF8 handshake request");
                            continue;
                        }
                    };

                    info!("[Server-ZMQ] Received handshake request for {}", worker_addr);

                    // Use a blocking std::sync::Mutex to check workers
                    let mut workers_map = workers_clone.lock().unwrap();
                    if !workers_map.contains_key(&worker_addr) {
                        info!("[Server-ZMQ] Launching worker for {}", worker_addr);
                        let worker_addr_clone = worker_addr.clone();

                        // Spawn a new OS thread, not a tokio task
                        let handle = std::thread::spawn(move || {
                            // Create a new tokio runtime *for this thread*
                            let rt = Builder::new_current_thread()
                                .enable_all()
                                .build()
                                .unwrap();
                            let local_set = tokio::task::LocalSet::new();

                            // Run the !Send worker task on this new runtime
                            local_set.block_on(&rt, async move {
                                // Wait a bit for worker setup, like C++
                                sleep(Duration::from_millis(500)).await;
                                if let Err(e) = run_zmq_worker(&worker_addr_clone).await {
                                    error!("[ZMQ-Worker] Error on {}: {}", worker_addr_clone, e);
                                }
                            });
                        });

                        workers_map.insert(worker_addr.clone(), handle);
                    }
                    // Mutex is dropped here

                    // Send reply
                    if let Err(e) = socket.send(&worker_addr, 0) {
                        error!("[Server-ZMQ] Failed to send reply: {}", e);
                        break; // Exit loop on send error
                    }
                    debug!("[Server-ZMQ] Handshake complete for {}", worker_addr);
                }
                Err(zmq::Error::EAGAIN) => {
                    // Timeout, loop again to check should_shutdown()
                    continue;
                }
                Err(e) => {
                    // Real error
                    error!("[Server-ZMQ] Dispatcher recv error: {}", e);
                    break;
                }
            }
        }

        info!("[Server-ZMQ] Dispatcher shutting down, waiting for workers...");

        // Wait for all workers to finish (using standard thread join)
        let mut workers_map = workers_clone.lock().unwrap();
        for (addr, handle) in workers_map.drain() {
            info!("[Server-ZMQ] Waiting for worker {}", addr);
            let _ = handle.join();
        }

        info!("[Server-ZMQ] Dispatcher stopped.");
        Ok(())
    })
    .await? // Wait for the spawn_blocking task to complete
}

/// ZMQ Worker - runs a MultiverseServer instance
async fn run_zmq_worker(bind_addr: &str) -> Result<()> {
    info!("[ZMQ-Worker] Starting worker on {}", bind_addr);

    // new_zmq binds the REP socket for the worker
    let mut server = MultiverseServer::new_zmq(bind_addr).await?;

    // start() begins the server's main loop
    server.start().await?;

    info!("[ZMQ-Worker] Worker stopped on {}", bind_addr);
    Ok(())
}