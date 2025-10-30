use anyhow::Result;
use clap::{Parser, ValueEnum};
use multiverse_server_rs::server::MultiverseServer;
use multiverse_server_rs::transport::TransportType;
use multiverse_server_rs::utils::{should_shutdown, set_shutdown};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;
use tokio::signal;
use tokio::task::JoinSet;
use tracing::{error, info};

#[derive(Debug, Clone, Copy, ValueEnum)]
enum TransportArg {
    #[cfg(feature = "use-zmq")]
    Zmq,
    #[cfg(feature = "use-tcp")]
    Tcp,
    #[cfg(feature = "use-udp")]
    Udp,
}

impl From<TransportArg> for TransportType {
    fn from(arg: TransportArg) -> Self {
        match arg {
            #[cfg(feature = "use-zmq")]
            TransportArg::Zmq => TransportType::Zmq,
            #[cfg(feature = "use-tcp")]
            TransportArg::Tcp => TransportType::Tcp,
            #[cfg(feature = "use-udp")]
            TransportArg::Udp => TransportType::Udp,
        }
    }
}

#[derive(Parser, Debug)]
#[command(author, version, about, long_about = None)]
struct Args {
    /// Transport and bind pairs: --transport <type> --bind <addr>
    #[arg(long)]
    transport: Vec<TransportArg>,

    /// Bind addresses (one per transport)
    #[arg(long)]
    bind: Vec<String>,
}

#[derive(Debug)]
struct EndpointSpec {
    transport: TransportType,
    bind_addr: String,
}

fn parse_endpoint_specs(args: Args) -> Result<Vec<EndpointSpec>> {
    let mut specs = Vec::new();
    let transports = args.transport;
    let binds = args.bind;

    if transports.is_empty() {
        // Default: ZMQ on tcp://*:7000
        #[cfg(feature = "use-zmq")]
        {
            specs.push(EndpointSpec {
                transport: TransportType::Zmq,
                bind_addr: "tcp://*:7000".to_string(),
            });
        }
        #[cfg(not(feature = "use-zmq"))]
        {
            anyhow::bail!("No transport specified and ZMQ is not enabled");
        }
    } else {
        for (i, transport_arg) in transports.iter().enumerate() {
            let transport = TransportType::from(*transport_arg);
            let bind_addr = if i < binds.len() {
                binds[i].clone()
            } else {
                default_bind_for(transport)
            };

            specs.push(EndpointSpec {
                transport,
                bind_addr,
            });
        }
    }

    Ok(specs)
}

fn default_bind_for(transport: TransportType) -> String {
    match transport {
        #[cfg(feature = "use-zmq")]
        TransportType::Zmq => "tcp://*:7000".to_string(),
        #[cfg(feature = "use-tcp")]
        TransportType::Tcp => "0.0.0.0:7000".to_string(),
        #[cfg(feature = "use-udp")]
        TransportType::Udp => "0.0.0.0:7000".to_string(),
    }
}

fn split_host_port(bind: &str) -> (String, String) {
    if let Some(pos) = bind.rfind(':') {
        (bind[..pos].to_string(), bind[pos + 1..].to_string())
    } else {
        ("0.0.0.0".to_string(), bind.to_string())
    }
}

#[tokio::main]
async fn main() -> Result<()> {
    // Initialize tracing
    tracing_subscriber::fmt()
        .with_env_filter(
            tracing_subscriber::EnvFilter::try_from_default_env()
                .unwrap_or_else(|_| tracing_subscriber::EnvFilter::new("info")),
        )
        .init();

    info!("Starting Multiverse Server (multi-transport)...");

    // Show build configuration
    #[cfg(feature = "use-zmq")]
    info!("  ZMQ: ENABLED");
    #[cfg(not(feature = "use-zmq"))]
    info!("  ZMQ: DISABLED");

    #[cfg(feature = "use-tcp")]
    info!("  TCP: ENABLED");
    #[cfg(not(feature = "use-tcp"))]
    info!("  TCP: DISABLED");

    #[cfg(feature = "use-udp")]
    info!("  UDP: ENABLED");
    #[cfg(not(feature = "use-udp"))]
    info!("  UDP: DISABLED");

    let args = Args::parse();
    let specs = parse_endpoint_specs(args)?;

    if specs.is_empty() {
        anyhow::bail!("No valid transport specifications provided");
    }

    // Setup signal handler
    let shutdown_flag = Arc::new(AtomicBool::new(false));
    let shutdown_flag_clone = shutdown_flag.clone();

    tokio::spawn(async move {
        if let Err(e) = signal::ctrl_c().await {
            error!("Failed to listen for Ctrl+C: {}", e);
            return;
        }
        info!("[Server] Caught SIGINT (Ctrl+C), shutting down...");
        shutdown_flag_clone.store(true, Ordering::Relaxed);
        set_shutdown();
    });

    // Launch servers
    let mut join_set = JoinSet::new();

    for spec in specs {
        match spec.transport {
            #[cfg(feature = "use-zmq")]
            TransportType::Zmq => {
                let bind_addr = spec.bind_addr.clone();
                info!("[Server] Launching ZMQ @ {}", bind_addr);
                // Spawn ZMQ server on a dedicated blocking thread since ZMQ is not Send
                join_set.spawn_blocking(move || {
                    // Create a new Tokio runtime for this thread
                    let rt = tokio::runtime::Runtime::new().unwrap();
                    rt.block_on(async move {
                        if let Err(e) = run_zmq_server(&bind_addr).await {
                            error!("[ZMQ Server] Error: {}", e);
                        }
                    });
                });
            }
            #[cfg(feature = "use-tcp")]
            TransportType::Tcp => {
                let (host, port) = split_host_port(&spec.bind_addr);
                info!("[Server] Launching TCP @ {}:{}", host, port);
                join_set.spawn(async move {
                    if let Err(e) = run_tcp_server(&host, &port).await {
                        error!("[TCP Server] Error: {}", e);
                    }
                });
            }
            #[cfg(feature = "use-udp")]
            TransportType::Udp => {
                let (host, port) = split_host_port(&spec.bind_addr);
                info!("[Server] Launching UDP @ {}:{}", host, port);
                join_set.spawn(async move {
                    if let Err(e) = run_udp_server(&host, &port).await {
                        error!("[UDP Server] Error: {}", e);
                    }
                });
            }
        }
    }

    // Wait for all servers to finish
    while let Some(result) = join_set.join_next().await {
        if let Err(e) = result {
            error!("Server task error: {}", e);
        }
    }

    info!("[Server] All servers stopped");
    Ok(())
}

#[cfg(feature = "use-zmq")]
async fn run_zmq_server(bind_addr: &str) -> Result<()> {
    let mut server = MultiverseServer::new_zmq(bind_addr).await?;
    server.start().await
}

#[cfg(feature = "use-tcp")]
async fn run_tcp_server(host: &str, port: &str) -> Result<()> {
    let mut server = MultiverseServer::new_tcp(host, port).await?;
    server.start().await
}

#[cfg(feature = "use-udp")]
async fn run_udp_server(host: &str, port: &str) -> Result<()> {
    let mut server = MultiverseServer::new_udp(host, port).await?;
    server.start().await
}