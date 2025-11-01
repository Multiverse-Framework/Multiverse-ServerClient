use anyhow::Result;
use clap::{Parser, ValueEnum};
use multiverse_server_rs::dispatcher::{
    start_tcp_dispatcher, start_udp_dispatcher, start_zmq_dispatcher
};
use multiverse_server_rs::transport::TransportType;
use multiverse_server_rs::utils::set_shutdown;
use tokio::signal;
use tokio::task::{JoinSet, LocalSet};
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
        // Default: ZMQ on tcp://*:7000 (or TCP if ZMQ not available)
        #[cfg(feature = "use-zmq")]
        {
            specs.push(EndpointSpec {
                transport: TransportType::Zmq,
                bind_addr: "tcp://*:7000".to_string(),
            });
        }
        #[cfg(all(not(feature = "use-zmq"), feature = "use-tcp"))]
        {
            specs.push(EndpointSpec {
                transport: TransportType::Tcp,
                bind_addr: "0.0.0.0:7000".to_string(),
            });
        }
        #[cfg(all(not(feature = "use-zmq"), not(feature = "use-tcp")))]
        {
            anyhow::bail!("No transport specified and no default transport is enabled");
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

//
// --- Main Function ---
//
#[tokio::main(flavor = "current_thread")]
async fn main() -> Result<()> {
    // Initialize tracing
    tracing_subscriber::fmt()
        .with_env_filter(
            tracing_subscriber::EnvFilter::try_from_default_env()
                .unwrap_or_else(|_| tracing_subscriber::EnvFilter::new("info")),
        )
        .init();

    info!("Starting Multiverse Server (multi-transport with dispatcher/worker pattern)...");

    // Show build configuration
    #[cfg(feature = "use-zmq")]
    info!("  ZMQ: ENABLED (with dispatcher/worker)");
    #[cfg(not(feature = "use-zmq"))]
    info!("  ZMQ: DISABLED");

    #[cfg(feature = "use-tcp")]
    info!("  TCP: ENABLED (with dispatcher/worker)");
    #[cfg(not(feature = "use-tcp"))]
    info!("  TCP: DISABLED");

    #[cfg(feature = "use-udp")]
    info!("  UDP: ENABLED (with dispatcher/worker)");
    #[cfg(not(feature = "use-udp"))]
    info!("  UDP: DISABLED");

    let args = Args::parse();
    let specs = parse_endpoint_specs(args)?;

    if specs.is_empty() {
        anyhow::bail!("No valid transport specifications provided");
    }

    // --- Task Spawning ---
    let local_set = LocalSet::new();

    // Run the LocalSet until all tasks are complete
    local_set
        .run_until(async move {
            // Setup signal handler
            tokio::task::spawn_local(async move {
                if let Err(e) = signal::ctrl_c().await {
                    error!("Failed to listen for Ctrl+C: {}", e);
                    return;
                }
                info!("[Server] Caught SIGINT (Ctrl+C), shutting down...");
                set_shutdown();
            });

            // Create JoinSet *inside* the LocalSet
            let mut join_set = JoinSet::new();

            for spec in specs {
                match spec.transport {
                    #[cfg(feature = "use-zmq")]
                    TransportType::Zmq => {
                        let bind_addr = spec.bind_addr.clone();
                        info!(
                            "[Server] Launch ZMQ Dispatcher @ {} (workers on demand)",
                            bind_addr
                        );

                        join_set.spawn_local(async move {
                            if let Err(e) = start_zmq_dispatcher(bind_addr).await {
                                error!("[ZMQ Dispatcher] Error: {}", e);
                            }
                        });
                    }
                    #[cfg(feature = "use-tcp")]
                    TransportType::Tcp => {
                        let (host, port) = split_host_port(&spec.bind_addr);
                        info!(
                            "[Server] Launch TCP Dispatcher @ {}:{} (workers on demand)",
                            host, port
                        );

                        join_set.spawn_local(async move {
                            if let Err(e) = start_tcp_dispatcher(host, port).await {
                                error!("[TCP Dispatcher] Error: {}", e);
                            }
                        });
                    }
                    #[cfg(feature = "use-udp")]
                    TransportType::Udp => {
                        let (host, port) = split_host_port(&spec.bind_addr);
                        info!(
                            "[Server] Launch UDP Dispatcher @ {}:{} (workers on demand)",
                            host, port
                        );

                        join_set.spawn_local(async move {
                            if let Err(e) = start_udp_dispatcher(host, port).await {
                                error!("[UDP Dispatcher] Error: {}", e);
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
        })
        .await;

    info!("[Server] All servers stopped");
    Ok(())
}