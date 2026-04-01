use anyhow::{Context, Result};
use std::collections::HashMap;
use std::sync::Arc;
use std::sync::atomic::{AtomicBool, Ordering};
use tokio::net::{TcpListener, TcpStream, UdpSocket};
use tokio::time::{sleep, Duration};
use tracing::{debug, error, info, warn};
use std::thread::JoinHandle as StdJoinHandle;
use tokio::runtime::Builder;

use crate::server::MultiverseServer;
use crate::utils::should_shutdown;
use crate::protocol::{raw_tcp, raw_udp};

struct WorkerHandle {
    thread: StdJoinHandle<()>,
    shutdown: Arc<AtomicBool>,
}

struct TcpWorkerHandle {
    thread: StdJoinHandle<()>,
    shutdown: Arc<AtomicBool>,
}

pub async fn start_tcp_dispatcher(host: String, port: String) -> Result<()> {
    let addr = format!("{}:{}", host, port);
    let listener = TcpListener::bind(&addr)
        .await
        .context(format!("Failed to bind TCP listener on {}", addr))?;

    info!("[Server-TCP] Dispatcher listening on {}", addr);

    let workers: Arc<std::sync::Mutex<HashMap<String, TcpWorkerHandle>>> =
        Arc::new(std::sync::Mutex::new(HashMap::new()));

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

        // Handle handshake inline
        let workers_clone = Arc::clone(&workers);
        let host_clone = host.clone();
        let port_base: u16 = port.parse().unwrap_or(7000);

        if let Err(e) = handle_tcp_handshake(stream, workers_clone, host_clone, port_base).await {
            error!("[Server-TCP] Handshake error: {}", e);
        }
    }

    info!("[Server-TCP] Dispatcher shutting down, waiting for workers...");

    // Wait for all workers (using standard thread join)
    let mut workers = workers.lock().unwrap();
    for (addr, handle) in workers.drain() {
        info!("[Server-TCP] Waiting for worker {}", addr);
        handle.shutdown.store(true, Ordering::Release);
        let _ = handle.thread.join();
    }

    info!("[Server-TCP] Dispatcher stopped.");
    Ok(())
}

async fn handle_tcp_handshake(
    mut stream: TcpStream,
    workers: Arc<std::sync::Mutex<HashMap<String, TcpWorkerHandle>>>,
    host: String,
    port_base: u16,
) -> Result<()> {
    let parts = match raw_tcp::recv_parts(&mut stream).await {
        Ok(parts) => parts,
        Err(e) => {
            // Check if the error is an IO error related to client disconnect
            if let Some(io_err) = e.downcast_ref::<std::io::Error>() {
                match io_err.kind() {
                    std::io::ErrorKind::UnexpectedEof => {
                        debug!(
                            "[Server-TCP] Handshake failed: client disconnected early (early eof)."
                        );
                        return Ok(()); // Not a server error, just a bad client.
                    }
                    std::io::ErrorKind::ConnectionReset => {
                        debug!("[Server-TCP] Handshake failed: client reset connection.");
                        return Ok(()); // Not a server error.
                    }
                    _ => {}
                }
            }
            // For other errors, or non-IO errors, bubble them up.
            return Err(e).context("Handshake recv_parts_tcp failed");
        }
    };

    if parts.is_empty() || parts[0].is_empty() {
        warn!("[Server-TCP] Handshake error: Empty handshake request.");
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
    info!("[Server-TCP] Received handshake request for {}", worker_addr);

    // Stop old worker if exists (must restart for TCP since worker can't detect pending connections)
    let old_handle = {
        let mut workers_map = workers.lock().unwrap();
        workers_map.remove(&worker_addr)
    };

    if let Some(old_handle) = old_handle {
        if old_handle.thread.is_finished() {
            info!("[Server-TCP] Worker for {} already finished, creating new one.", worker_addr);
        } else {
            debug!("[Server-TCP] Signaling old worker on {} to stop", worker_addr);
            old_handle.shutdown.store(true, Ordering::Release);
            // Must wait for old worker to release the port
            if let Err(e) = old_handle.thread.join() {
                warn!("[Server-TCP] Old worker on {} panicked: {:?}", worker_addr, e);
            }
            debug!("[Server-TCP] Old worker on {} stopped", worker_addr);
        }
    }

    // Create new worker
    info!("[Server-TCP] Launching worker for {}", worker_addr);
    let worker_addr_clone = worker_addr.clone();
    let worker_host = host.clone();
    let worker_port_str = worker_port.to_string();
    let worker_shutdown = Arc::new(AtomicBool::new(false));
    let worker_shutdown_clone = Arc::clone(&worker_shutdown);

    // Spawn a new OS thread, not a tokio task
    let thread = std::thread::spawn(move || {
        // Create a new tokio runtime *for this thread*
        let rt = Builder::new_current_thread()
            .enable_all()
            .build()
            .unwrap();
        let local_set = tokio::task::LocalSet::new();

        // Run the !Send worker task on this new runtime
        local_set.block_on(&rt, async move {
            if let Err(e) = run_tcp_worker(&worker_host, &worker_port_str, worker_shutdown_clone).await {
                warn!("[TCP-Worker] Error on {}: {}", worker_addr_clone, e);
            }
        });
    });

    {
        let mut workers_map = workers.lock().unwrap();
        workers_map.insert(worker_addr.clone(), TcpWorkerHandle {
            thread,
            shutdown: worker_shutdown,
        });
    }

    // Send response with worker address
    let response = vec![worker_addr.as_bytes().to_vec()];
    raw_tcp::send_parts(&mut stream, &response).await?;

    debug!("[Server-TCP] Handshake complete for {}", worker_addr);
    Ok(())
}

async fn run_tcp_worker(host: &str, port: &str, instance_shutdown: Arc<AtomicBool>) -> Result<()> {
    info!("[TCP-Worker] Starting worker on {}:{}", host, port);

    let mut server = MultiverseServer::new_tcp(host, port, Some(instance_shutdown)).await?;
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

    let workers: Arc<std::sync::Mutex<HashMap<String, WorkerHandle>>> =
        Arc::new(std::sync::Mutex::new(HashMap::new()));

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
        let parts = match raw_udp::decode_parts(&buf[..size]) {
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
        info!("[Server-UDP] Received handshake request for {}", worker_addr);

        let old_handle = {
            let mut workers_map = workers.lock().unwrap();
            workers_map.remove(&worker_addr)
        };

        if let Some(old_handle) = old_handle {
            info!("[Server-UDP] Stopping old worker on {}", worker_addr);
            old_handle.shutdown.store(true, Ordering::Release);
            if let Err(e) = old_handle.thread.join() {
                warn!("[Server-UDP] Old worker on {} panicked: {:?}", worker_addr, e);
            } else {
                info!("[Server-UDP] Old worker on {} stopped and cleaned up", worker_addr);
            }
        }

        // Now create new worker
        info!("[Server-UDP] Launching worker for {}", worker_addr);
        let worker_addr_clone = worker_addr.clone();
        let worker_host = host.clone();
        let worker_port_str = worker_port.to_string();
        let worker_shutdown = Arc::new(AtomicBool::new(false));
        let worker_shutdown_clone = Arc::clone(&worker_shutdown);

        // Spawn a new OS thread, not a tokio task
        let thread = std::thread::spawn(move || {
            // Create a new tokio runtime *for this thread*
            let rt = Builder::new_current_thread()
                .enable_all()
                .build()
                .unwrap();
            let local_set = tokio::task::LocalSet::new();

            // Run the !Send worker task on this new runtime
            local_set.block_on(&rt, async move {
                if let Err(e) = run_udp_worker(&worker_host, &worker_port_str, worker_shutdown_clone).await {
                    warn!("[UDP-Worker] Error on {}: {}", worker_addr_clone, e);
                }
            });
        });

        {
            let mut workers_map = workers.lock().unwrap();
            workers_map.insert(worker_addr.clone(), WorkerHandle {
                thread,
                shutdown: worker_shutdown,
            });
        }

        // Send response with worker address
        let response = vec![worker_addr.as_bytes().to_vec()];
        if let Ok(encoded) = raw_udp::encode_parts(&response) {
            if let Err(e) = socket.send_to(&encoded, client_addr).await {
                error!("[Server-UDP] Failed to send handshake response: {}", e);
            }
        }

        debug!("[Server-UDP] Handshake complete for {}", worker_addr);
    }

    info!("[Server-UDP] Dispatcher shutting down, waiting for workers...");

    // Wait for all workers (using standard thread join)
    let mut workers = workers.lock().unwrap();
    for (addr, handle) in workers.drain() {
        info!("[Server-UDP] Waiting for worker {}", addr);
        handle.shutdown.store(true, Ordering::Release);
        let _ = handle.thread.join();
    }

    info!("[Server-UDP] Dispatcher stopped.");
    Ok(())
}

async fn run_udp_worker(host: &str, port: &str, instance_shutdown: Arc<AtomicBool>) -> Result<()> {
    info!("[UDP-Worker] Starting worker on {}:{}", host, port);

    let mut server = MultiverseServer::new_udp(host, port, Some(instance_shutdown)).await?;
    server.start().await?;

    info!("[UDP-Worker] Worker stopped on {}:{}", host, port);
    Ok(())
}

//
// --- ZMQ Dispatcher ---
//

pub async fn start_zmq_dispatcher(bind_addr: String) -> Result<()> {
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

        // Set linger to 0 to allow immediate context termination
        socket.set_linger(0)?;

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

                    let mut workers_map = workers_clone.lock().unwrap();
                    if let Some(handle) = workers_map.get(&worker_addr) {
                        if handle.is_finished() {
                            info!("[Server-ZMQ] Worker for {} found but was finished. Removing to restart.", worker_addr);
                            workers_map.remove(&worker_addr);
                        }
                    }
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
                                // Short delay for worker setup (reduced from 500ms to 10ms)
                                sleep(Duration::from_millis(10)).await;
                                if let Err(e) = run_zmq_worker(&worker_addr_clone).await {
                                    warn!("[ZMQ-Worker] Error on {}: {}", worker_addr_clone, e);
                                }
                            });
                        });

                        workers_map.insert(worker_addr.clone(), handle);
                    } else {
                        debug!("[Server-ZMQ] Worker for {} already running.", worker_addr);
                    }

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
    // Normalize: ZMQ requires a transport scheme (e.g. tcp://) for bind
    let zmq_addr = if bind_addr.contains("://") {
        bind_addr.to_string()
    } else {
        format!("tcp://{}", bind_addr)
    };
    info!("[ZMQ-Worker] Starting worker on {}", zmq_addr);

    // new_zmq binds the REP socket for the worker
    let mut server = MultiverseServer::new_zmq(&zmq_addr).await?;

    // start() begins the server's main loop
    server.start().await?;

    info!("[ZMQ-Worker] Worker stopped on {}", bind_addr);
    Ok(())
}