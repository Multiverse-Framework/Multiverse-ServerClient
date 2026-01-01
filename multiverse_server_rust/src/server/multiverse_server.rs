use super::data_types::*;
use super::state::{MetaDataState, ServerState};
use crate::transport::{Transport, TransportType};
use anyhow::{Context, Result};
use parking_lot::Mutex;
use serde_json::{json, Value};
use std::collections::HashMap;
use std::f64::consts::PI;
use std::sync::Arc;
use std::sync::atomic::{AtomicBool, Ordering};
use std::time::Duration;
use tokio::time::sleep;
#[allow(unused_imports)]
use tracing::{debug, error, info, warn};

lazy_static::lazy_static! {
    static ref WORLDS: Arc<Mutex<HashMap<String, World>>> = Arc::new(Mutex::new(HashMap::new()));

    static ref UNIT_SCALE: HashMap<&'static str, f64> = {
        let mut m = HashMap::new();
        m.insert("m", 1.0);
        m.insert("cm", 0.01);
        m.insert("mm", 0.001);
        m.insert("rad", 1.0);
        m.insert("deg", PI / 180.0);
        m.insert("kg", 1.0);
        m.insert("g", 0.001);
        m.insert("s", 1.0);
        m.insert("ms", 0.001);
        m.insert("us", 0.000001);
        m
    };

    static ref HANDEDNESS_SCALE: HashMap<Attribute, HashMap<String, Vec<f64>>> = {
        let mut m = HashMap::new();

        let mut rhs = HashMap::new();
        rhs.insert("rhs".to_string(), vec![1.0; 6]);
        rhs.insert("lhs".to_string(), vec![1.0, -1.0, 1.0, -1.0, 1.0, -1.0]); // Example for odometry

        let mut default_rhs = HashMap::new();
        default_rhs.insert("rhs".to_string(), vec![1.0; 4]); // Max default size (quaternion)
        default_rhs.insert("lhs".to_string(), vec![1.0; 4]); // Assume 1.0 for simplicity

        let mut all_attrs = init_attribute_map_double();
        all_attrs.insert("joint_position".to_string(), (Attribute::JointPosition, vec![f64::NAN]));
        all_attrs.insert("joint_quaternion".to_string(), (Attribute::JointQuaternion, vec![f64::NAN; 4]));
        all_attrs.insert("cmd_joint_linear_position".to_string(), (Attribute::CmdJointLinearPosition, vec![f64::NAN]));
        all_attrs.insert("cmd_joint_angular_position".to_string(), (Attribute::CmdJointAngularPosition, vec![f64::NAN]));
        all_attrs.insert("cmd_joint_linear_velocity".to_string(), (Attribute::CmdJointLinearVelocity, vec![f64::NAN]));
        all_attrs.insert("cmd_joint_angular_velocity".to_string(), (Attribute::CmdJointAngularVelocity, vec![f64::NAN]));
        all_attrs.insert("cmd_joint_linear_acceleration".to_string(), (Attribute::CmdJointLinearAcceleration, vec![f64::NAN]));
        all_attrs.insert("cmd_joint_angular_acceleration".to_string(), (Attribute::CmdJointAngularAcceleration, vec![f64::NAN]));
        all_attrs.insert("cmd_joint_force".to_string(), (Attribute::CmdJointForce, vec![f64::NAN]));
        all_attrs.insert("cmd_joint_torque".to_string(), (Attribute::CmdJointTorque, vec![f64::NAN]));
        
        all_attrs.insert(
            "joint_linear_acceleration".to_string(),
            (Attribute::JointLinearAcceleration, vec![f64::NAN]),
        );
        all_attrs.insert(
            "joint_angular_acceleration".to_string(),
            (Attribute::JointAngularAcceleration, vec![f64::NAN]),
        );


        for (_, (attr, default_vec)) in all_attrs {
            let mut scales = HashMap::new();
            let size = default_vec.len();
            scales.insert("rhs".to_string(), vec![1.0; size]);

            let lhs_scale = if attr == Attribute::OdometricVelocity {
                vec![1.0, -1.0, 1.0, -1.0, 1.0, -1.0] // x, -y, z, -rx, ry, -rz
            } else {
                vec![1.0; size] // Default: no change
            };
            scales.insert("lhs".to_string(), lhs_scale);

            m.insert(attr, scales);
        }

        m
    };
}

pub struct MultiverseServer {
    transport: Box<dyn Transport>,
    #[allow(dead_code)]
    transport_type: TransportType,
    socket_addr: String,
    state: ServerState,

    // Metadata
    request_meta_data_json: serde_json::Value,
    response_meta_data_json: serde_json::Value,
    send_objects_json: serde_json::Value,
    receive_objects_json: serde_json::Value,

    // Buffers
    send_buffer: Buffer,
    receive_buffer: Buffer,
    conversion_map: ConversionMap,

    // World tracking
    world_name: String,
    simulation_name: String,
    request_world_name: String,
    request_simulation_name: String,

    // Cleanup tracking - maintains simulation info even after local state is cleared
    cleanup_world_name: String,
    cleanup_simulation_name: String,

    // Flags
    is_receive_data_sent: bool,
    #[allow(dead_code)]
    continue_state: bool,

    // Per-instance shutdown flag (for worker restart)
    instance_shutdown: Option<Arc<AtomicBool>>,
}

fn merge_json_attributes(
    target_sim_json: &mut Value,
    request_json: &Value,
    key: &str,
) -> Result<()> {
    let target_map = target_sim_json
        .get_mut(key)
        .context(format!("Target missing key {}", key))?
        .as_object_mut()
        .context(format!("Target {} is not an object", key))?;

    if let Some(request_map) = request_json.get(key).and_then(|v| v.as_object()) {
        for (obj_name, req_attrs) in request_map {
            if req_attrs.is_null() || req_attrs.as_array().map_or(false, |a| a.is_empty()) {
                if !target_map.contains_key(obj_name) {
                    target_map.insert(obj_name.clone(), json!([]));
                }
                continue;
            }

            let req_attrs_array = req_attrs
                .as_array()
                .context(format!("Request attribute {} is not an array", obj_name))?;

            let target_attrs = target_map
                .entry(obj_name.clone())
                .or_insert(json!([]))
                .as_array_mut()
                .context(format!("Target attribute {} is not an array", obj_name))?;

            for attr in req_attrs_array {
                if !target_attrs.contains(attr) {
                    target_attrs.push(attr.clone());
                }
            }
        }
    }
    Ok(())
}

impl MultiverseServer {
    #[cfg(feature = "use-zmq")]
    pub async fn new_zmq(endpoint: &str) -> Result<Self> {
        use crate::transport::ZmqTransport;

        let mut transport = ZmqTransport::new_rep()?;
        transport.bind(endpoint).await?;

        info!("[Server] Bound to ZMQ socket {}", endpoint);

        Ok(Self {
            transport: Box::new(transport),
            transport_type: TransportType::Zmq,
            socket_addr: endpoint.to_string(),
            state: ServerState::ReceiveRequestMetaData,
            request_meta_data_json: serde_json::Value::Null,
            response_meta_data_json: serde_json::Value::Null,
            send_objects_json: serde_json::Value::Null,
            receive_objects_json: serde_json::Value::Null,
            send_buffer: Buffer::default(),
            receive_buffer: Buffer::default(),
            conversion_map: ConversionMap::default(),
            world_name: String::new(),
            simulation_name: String::new(),
            request_world_name: String::new(),
            request_simulation_name: String::new(),
            cleanup_world_name: String::new(),
            cleanup_simulation_name: String::new(),
            is_receive_data_sent: false,
            continue_state: false,
            instance_shutdown: None,
        })
    }

    #[cfg(feature = "use-tcp")]
    pub async fn new_tcp(host: &str, port: &str) -> Result<Self> {
        use crate::transport::TcpTransport;

        let mut transport = TcpTransport::new();
        let endpoint = format!("{}:{}", host, port);
        transport.listen(&endpoint).await?;
        transport.accept().await?;

        info!("[Server] TCP connected on {}:{}", host, port);

        Ok(Self {
            transport: Box::new(transport),
            transport_type: TransportType::Tcp,
            socket_addr: format!("rawtcp://{}:{}", host, port),
            state: ServerState::ReceiveRequestMetaData,
            request_meta_data_json: serde_json::Value::Null,
            response_meta_data_json: serde_json::Value::Null,
            send_objects_json: serde_json::Value::Null,
            receive_objects_json: serde_json::Value::Null,
            send_buffer: Buffer::default(),
            receive_buffer: Buffer::default(),
            conversion_map: ConversionMap::default(),
            world_name: String::new(),
            simulation_name: String::new(),
            request_world_name: String::new(),
            request_simulation_name: String::new(),
            cleanup_world_name: String::new(),
            cleanup_simulation_name: String::new(),
            is_receive_data_sent: false,
            continue_state: false,
            instance_shutdown: None,
        })
    }

    #[cfg(feature = "use-udp")]
    pub async fn new_udp(host: &str, port: &str, instance_shutdown: Option<Arc<AtomicBool>>) -> Result<Self> {
        use crate::transport::UdpTransport;

        let mut transport = UdpTransport::new();
        let endpoint = format!("{}:{}", host, port);
        transport.listen(&endpoint).await?;

        info!("[Server] UDP bound on {}:{}", host, port);

        Ok(Self {
            transport: Box::new(transport),
            transport_type: TransportType::Udp,
            socket_addr: format!("rawudp://{}:{}", host, port),
            state: ServerState::ReceiveRequestMetaData,
            request_meta_data_json: serde_json::Value::Null,
            response_meta_data_json: serde_json::Value::Null,
            send_objects_json: serde_json::Value::Null,
            receive_objects_json: serde_json::Value::Null,
            send_buffer: Buffer::default(),
            receive_buffer: Buffer::default(),
            conversion_map: ConversionMap::default(),
            world_name: String::new(),
            simulation_name: String::new(),
            request_world_name: String::new(),
            request_simulation_name: String::new(),
            cleanup_world_name: String::new(),
            cleanup_simulation_name: String::new(),
            is_receive_data_sent: false,
            continue_state: false,
            instance_shutdown,
        })
    }

    fn is_instance_shutdown(&self) -> bool {
        if let Some(ref shutdown) = self.instance_shutdown {
            shutdown.load(Ordering::Acquire)
        } else {
            false
        }
    }

    pub async fn start(&mut self) -> Result<()> {
        while !crate::utils::should_shutdown() && !self.is_instance_shutdown() {
            match self.state {
                ServerState::ReceiveRequestMetaData => {
                    self.send_buffer = Buffer::default();
                    self.receive_buffer = Buffer::default();
                    self.is_receive_data_sent = false;

                    self.state = self.receive_data().await?;
                }

                ServerState::BindObjects => {
                    info!("[Server] Received metadata at socket {}", self.socket_addr);
                    debug!("Metadata: {}", self.request_meta_data_json);

                    self.bind_meta_data().await?;
                    self.bind_send_objects().await?;
                    self.validate_meta_data().await?;
                    self.wait_for_objects().await?;

                    if crate::utils::should_shutdown() {
                        break;
                    }

                    self.bind_receive_objects().await?;

                    if self.request_meta_data_json.get("api_callbacks").is_some() {
                        self.wait_for_api_callbacks_response().await?;
                    }

                    self.state = ServerState::SendResponseMetaData;
                }

                ServerState::SendResponseMetaData => {
                    self.send_response_meta_data().await?;
                    self.init_send_and_receive_data();

                    self.state = ServerState::ReceiveSendData;
                }

                ServerState::ReceiveSendData => {
                    self.state = self.receive_data().await?;
                }

                ServerState::BindSendData => {
                    let mut worlds = WORLDS.lock();
                    let world = worlds
                        .entry(self.world_name.clone())
                        .or_insert_with(World::default);

                    if world.time == 0.0 {
                        info!("[Server] Reset all simulations in world {}", self.world_name);
                        for (sim_name, simulation) in &mut world.simulations {
                            info!("[Server] Reset simulation {}", sim_name);
                            simulation.meta_data_state = MetaDataState::Reset;
                        }
                    }
                    info!("[Server] Current time in world {}: {}", self.world_name, world.time);
                    drop(worlds);

                    if self.request_world_name != self.world_name
                        || self.request_simulation_name != self.simulation_name
                    {
                        self.wait_for_other_send_data().await?;
                    }

                    self.bind_send_data().await?;
                    self.state = ServerState::BindReceiveData;
                }

                ServerState::BindReceiveData => {
                    self.wait_for_receive_data().await?;
                    self.compute_cumulative_data().await?;
                    self.bind_receive_data().await?;

                    self.state = ServerState::SendReceiveData;
                }

                ServerState::SendReceiveData => {
                    let should_rebind = {
                        let worlds = WORLDS.lock();
                        if let Some(world) = worlds.get(&self.world_name) {
                            if let Some(sim) = world.simulations.get(&self.simulation_name) {
                                sim.meta_data_state == MetaDataState::WaitAfterSendReceiveData
                            } else {
                                false
                            }
                        } else {
                            false
                        }
                    };

                    if should_rebind {
                        self.receive_new_request_meta_data().await?;
                        self.state = ServerState::BindObjects;
                    } else {
                        if self.request_world_name != self.world_name
                            || self.request_simulation_name != self.simulation_name
                        {
                            self.wait_for_other_send_data().await?;
                        }

                        self.send_receive_data().await?;
                        self.state = ServerState::ReceiveSendData;

                        let mut worlds = WORLDS.lock();
                        if let Some(world) = worlds.get_mut(&self.world_name) {
                            if let Some(sim) = world.simulations.get_mut(&self.simulation_name) {
                                sim.meta_data_state = MetaDataState::Normal;
                            }
                        }
                    }
                }
            }
        }

        info!("[Server] Shutting down socket {}", self.socket_addr);
        Ok(())
    }

    async fn recv_message(&mut self) -> Result<(i32, Vec<Vec<u8>>)> {
        let frames = self.transport.recv_multipart().await?;

        if frames.is_empty() {
            anyhow::bail!("Empty message received");
        }

        if frames[0].len() != 4 {
            anyhow::bail!("Bad spec frame size: {}", frames[0].len());
        }

        let message_spec = i32::from_le_bytes([frames[0][0], frames[0][1], frames[0][2], frames[0][3]]);

        let payloads = frames[1..].to_vec();

        debug!(
            "[Server] Received message: spec={}, frames={}",
            message_spec,
            frames.len()
        );

        Ok((message_spec, payloads))
    }

    async fn send_message(&mut self, data: &[u8], more: bool) -> Result<()> {
        self.transport.send(data, more).await
    }

    async fn receive_data(&mut self) -> Result<ServerState> {
        loop {
            let (message_spec, payloads) = match self.recv_message().await {
                Ok(msg) => msg,
                Err(e) => {
                    // Check if it's a timeout error
                    let error_msg = e.to_string();
                    if error_msg.contains("receive timeout") {
                        // Timeout - check shutdown and continue
                        if crate::utils::should_shutdown() || self.is_instance_shutdown() {
                            return Err(e);
                        }
                        continue;
                    }

                    // Check if this is a timeout error (from tokio::time::timeout)
                    let is_timeout = e.to_string().contains("timeout");

                    // Check if this is a disconnect error
                    let is_disconnect = if let Some(io_err) = e.downcast_ref::<std::io::Error>() {
                        matches!(io_err.kind(),
                            std::io::ErrorKind::UnexpectedEof |
                            std::io::ErrorKind::ConnectionReset |
                            std::io::ErrorKind::BrokenPipe)
                    } else {
                        false
                    };

                    if (is_disconnect || is_timeout) && self.transport_type == TransportType::Tcp && !crate::utils::should_shutdown() && !self.is_instance_shutdown() {
                        info!("[Server] TCP client disconnected on {} ({}), waiting for new client...",
                              self.socket_addr,
                              if is_timeout { "timeout" } else { "connection closed" });

                        // For TCP, accept new clients in a loop like C++
                        // Clean up state from previous client ONCE (not in retry loop)
                        if !self.world_name.is_empty() && !self.simulation_name.is_empty() {
                            let mut worlds = WORLDS.lock();
                            if let Some(world) = worlds.get_mut(&self.world_name) {
                                if world.simulations.contains_key(&self.simulation_name) {
                                    info!("[Server] [{}] Removing simulation {} from world {} for new client",
                                          self.socket_addr, self.simulation_name, self.world_name);
                                    world.simulations.remove(&self.simulation_name);

                                    if world.simulations.is_empty() {
                                        info!("[Server] [{}] Dropping world '{}' from global WORLDS map (no more simulations, {} total worlds before removal)",
                                              self.socket_addr, self.world_name, worlds.len());
                                        worlds.remove(&self.world_name);
                                        info!("[Server] [{}] World '{}' dropped successfully, {} worlds remaining",
                                              self.socket_addr, self.world_name, worlds.len());
                                    } else {
                                        info!("[Server] [{}] World '{}' still has {} simulation(s), keeping it alive",
                                              self.socket_addr, self.world_name, world.simulations.len());
                                    }
                                }
                            }
                        }

                        // Reset state for new client
                        self.request_meta_data_json = serde_json::Value::Null;
                        self.response_meta_data_json = serde_json::Value::Null;
                        self.send_objects_json = serde_json::Value::Null;
                        self.receive_objects_json = serde_json::Value::Null;
                        self.send_buffer = Buffer::default();
                        self.receive_buffer = Buffer::default();
                        self.conversion_map = ConversionMap::default();
                        self.world_name.clear();
                        self.simulation_name.clear();
                        self.request_world_name.clear();
                        self.request_simulation_name.clear();
                        self.is_receive_data_sent = false;
                        // Clear cleanup fields since we already cleaned up above
                        self.cleanup_world_name.clear();
                        self.cleanup_simulation_name.clear();

                        // Keep accepting until we get a healthy connection
                        'accept_loop: loop {
                            // Accept new client connection
                            match self.transport.accept().await {
                                Ok(_) => {
                                    info!("[Server] New TCP client accepted on {}", self.socket_addr);
                                    // Connection accepted, will validate when we try to receive first message
                                    break 'accept_loop;
                                }
                                Err(accept_err) => {
                                    warn!("[Server] Accept failed on {}: {}, retrying in 100ms...", self.socket_addr, accept_err);
                                    sleep(Duration::from_millis(100)).await;
                                    if crate::utils::should_shutdown() {
                                        return Err(e);
                                    }
                                    continue 'accept_loop;
                                }
                            }
                        }

                        // Successfully accepted, continue to ReceiveRequestMetaData
                        // If the connection is dead, we'll detect it on first read/write and come back here
                        return Ok(ServerState::ReceiveRequestMetaData);
                    } else {
                        // Not TCP or shutdown requested, exit normally
                        error!("[Server] Receive error at socket {}: {}", self.socket_addr, e);
                        return Err(e);
                    }
                }
            };

        // Handle close signal
        if message_spec == 0 && payloads.is_empty() {
            info!("[Server] Received close signal at socket {}", self.socket_addr);
            self.send_response_meta_data().await?;

            let mut worlds = WORLDS.lock();
            if let Some(world) = worlds.get_mut(&self.world_name) {
                if let Some(sim) = world.simulations.get_mut(&self.simulation_name) {
                    sim.meta_data_state = MetaDataState::Normal;
                }
            }

            return Ok(ServerState::ReceiveRequestMetaData);
        }

        // Handle metadata message
        if message_spec == 1 && payloads.len() == 1 {
            let json_str = String::from_utf8(payloads[0].clone())
                .context("Invalid UTF-8 in metadata")?;

            self.request_meta_data_json =
                serde_json::from_str(&json_str).context("Invalid JSON in metadata")?;

            self.send_buffer = Buffer::default();
            self.receive_buffer = Buffer::default();

            return Ok(ServerState::BindObjects);
        }

        // Handle data messages (spec >= 2)
        if message_spec >= 2 {
            debug!(
                "[Server] Received data message: spec={}, send_sizes=[{}-{}-{}]",
                message_spec,
                self.send_buffer.buffer_double.size,
                self.send_buffer.buffer_uint8.size,
                self.send_buffer.buffer_uint16.size
            );

            if payloads.is_empty() || payloads[0].len() != 8 {
                anyhow::bail!("Invalid time payload");
            }

            let time_bytes: [u8; 8] = payloads[0][..8].try_into()?;
            let time = f64::from_le_bytes(time_bytes);

            if time < 0.0 {
                anyhow::bail!("Invalid time: {}", time);
            }

            let mut worlds = WORLDS.lock();
            let world = worlds
                .entry(self.world_name.clone())
                .or_insert_with(World::default);
            world.time = time;
            info!("[Server] Updated world {} time to {}", self.world_name, time);
            drop(worlds);

            // Process data buffers based on message_spec
            // message_spec: 3 = 1 buffer, 4 = 2 buffers, 5 = 3 buffers
            let num_buffers = (message_spec - 2) as usize;

            if payloads.len() != num_buffers + 1 {
                anyhow::bail!("Payload count mismatch: expected {}, got {}", num_buffers + 1, payloads.len());
            }

            let mut payload_idx = 1;
            let has_double = self.send_buffer.buffer_double.size > 0;
            let has_uint8 = self.send_buffer.buffer_uint8.size > 0;
            let has_uint16 = self.send_buffer.buffer_uint16.size > 0;

            // Count active buffers to validate message_spec
            let active_buffers = (has_double as usize) + (has_uint8 as usize) + (has_uint16 as usize);
            if active_buffers != num_buffers {
                anyhow::bail!("Buffer count mismatch: message_spec indicates {}, but {} buffers are active",
                    num_buffers, active_buffers);
            }

            // Copy buffers in order: double -> uint8 -> uint16
            if has_double {
                self.copy_to_buffer_double(&payloads[payload_idx])?;
                payload_idx += 1;
            }
            if has_uint8 {
                self.copy_to_buffer_uint8(&payloads[payload_idx])?;
                payload_idx += 1;
            }
            if has_uint16 {
                self.copy_to_buffer_uint16(&payloads[payload_idx])?;
            }

            return Ok(ServerState::BindSendData);
        }

        anyhow::bail!("Invalid message spec: {}", message_spec)
        }
    }

    fn copy_to_buffer_double(&mut self, data: &[u8]) -> Result<()> {
        let expected_size = self.send_buffer.buffer_double.size * std::mem::size_of::<f64>();
        if data.len() != expected_size {
            anyhow::bail!(
                "Buffer size mismatch for double: expected {} bytes, got {}",
                expected_size,
                data.len()
            );
        }

        // Allocate the buffer if needed
        if self.send_buffer.buffer_double.data.len() != self.send_buffer.buffer_double.size {
            self.send_buffer.buffer_double.data = vec![0.0; self.send_buffer.buffer_double.size];
        }

        // Copy bytes to f64 buffer
        unsafe {
            std::ptr::copy_nonoverlapping(
                data.as_ptr(),
                self.send_buffer.buffer_double.data.as_mut_ptr() as *mut u8,
                expected_size,
            );
        }

        Ok(())
    }

    fn copy_to_buffer_uint8(&mut self, data: &[u8]) -> Result<()> {
        let expected_size = self.send_buffer.buffer_uint8.size;
        if data.len() != expected_size {
            anyhow::bail!(
                "Buffer size mismatch for uint8: expected {} bytes, got {}",
                expected_size,
                data.len()
            );
        }

        // Allocate the buffer if needed
        if self.send_buffer.buffer_uint8.data.len() != self.send_buffer.buffer_uint8.size {
            self.send_buffer.buffer_uint8.data = vec![0u8; self.send_buffer.buffer_uint8.size];
        }

        // Copy bytes directly
        self.send_buffer.buffer_uint8.data.copy_from_slice(data);

        Ok(())
    }

    fn copy_to_buffer_uint16(&mut self, data: &[u8]) -> Result<()> {
        let expected_size = self.send_buffer.buffer_uint16.size * std::mem::size_of::<u16>();
        if data.len() != expected_size {
            anyhow::bail!(
                "Buffer size mismatch for uint16: expected {} bytes, got {}",
                expected_size,
                data.len()
            );
        }

        // Allocate the buffer if needed
        if self.send_buffer.buffer_uint16.data.len() != self.send_buffer.buffer_uint16.size {
            self.send_buffer.buffer_uint16.data = vec![0u16; self.send_buffer.buffer_uint16.size];
        }

        // Copy bytes to u16 buffer
        unsafe {
            std::ptr::copy_nonoverlapping(
                data.as_ptr(),
                self.send_buffer.buffer_uint16.data.as_mut_ptr() as *mut u8,
                expected_size,
            );
        }

        Ok(())
    }

    async fn bind_meta_data(&mut self) -> Result<()> {
        let meta_data = self
            .request_meta_data_json
            .get("meta_data")
            .context("No meta_data in request")?;

        self.request_world_name = meta_data
            .get("world_name")
            .and_then(|v| v.as_str())
            .context("No world_name in metadata")?
            .to_string();

        self.request_simulation_name = meta_data
            .get("simulation_name")
            .and_then(|v| v.as_str())
            .context("No simulation_name in metadata")?
            .to_string();

        let mut worlds = WORLDS.lock();

        // Helper to check if a simulation exists in a world
        let simulation_exists = worlds
            .get(&self.request_world_name)
            .and_then(|w| w.simulations.get(&self.request_simulation_name))
            .is_some();

        // Check for simulation conflicts
        if self.simulation_name.is_empty() && simulation_exists {
            anyhow::bail!(
                "[Server] Socket {} requires an existing simulation name ({}).",
                self.socket_addr,
                self.request_simulation_name
            );
        }

        if !self.simulation_name.is_empty() && !simulation_exists {
            warn!(
                "[Server] Socket {} requests a non-existing simulation ({}).",
                self.socket_addr, self.request_simulation_name
            );
        }

        // --- Simulation Hand-off Logic ---
        let is_simulation_switch = !self.simulation_name.is_empty()
            && self.request_simulation_name != self.simulation_name
            && simulation_exists;

        if is_simulation_switch {
            if self.request_meta_data_json.get("api_callbacks").is_some() {
                anyhow::bail!("[Server] Request meta data at socket {} has API callbacks while requesting a different simulation.", self.socket_addr);
            }

            info!(
                "[Server] Socket {} ({}) requests a different simulation ({}).",
                self.socket_addr, self.simulation_name, self.request_simulation_name
            );

            // Must drop the lock to await in the loop
            let req_world = self.request_world_name.clone();
            let req_sim = self.request_simulation_name.clone();
            info!("[Server] Socket {} is waiting for {} to be in the normal state.",
                  self.socket_addr, req_sim);
            drop(worlds);

            // Wait for the target simulation to be in a Normal state
            let mut start = std::time::Instant::now();
            loop {
                if crate::utils::should_shutdown() {
                    return Ok(());
                }

                let is_normal = {
                    let worlds = WORLDS.lock();
                    worlds
                        .get(&req_world)
                        .and_then(|w| w.simulations.get(&req_sim))
                        .map_or(false, |s| s.meta_data_state == MetaDataState::Normal)
                };

                if is_normal {
                    break;
                }

                if start.elapsed() > Duration::from_secs(1) {
                    info!(
                        "[Server] Socket {} is waiting for {} to be in the normal state.",
                        self.socket_addr, req_sim
                    );
                    start = std::time::Instant::now();
                }

                sleep(Duration::from_millis(100)).await;
            }

            // Re-acquire lock to merge metadata
            worlds = WORLDS.lock();
            let target_simulation = worlds
                .get_mut(&req_world)
                .and_then(|w| w.simulations.get_mut(&req_sim))
                .context("Target simulation disappeared")?;

            // Merge 'send' and 'receive' attributes
            merge_json_attributes(
                &mut target_simulation.request_meta_data_json,
                &self.request_meta_data_json,
                "send",
            )?;
            merge_json_attributes(
                &mut target_simulation.request_meta_data_json,
                &self.request_meta_data_json,
                "receive",
            )?;

            // Set target state to trigger re-bind
            target_simulation.meta_data_state = MetaDataState::WaitAfterSendReceiveData;
            self.world_name = self.request_world_name.clone();
            // self.simulation_name remains the old one for now, will be updated in next state? No, C++ updates world_name only.
        } else {
            // Normal connection: set both
            self.world_name = self.request_world_name.clone();
            self.simulation_name = self.request_simulation_name.clone();
            // NOTE: cleanup fields will be set after first successful send
        }

        // --- API Callback State Handling ---
        if let Some(api_callbacks) = self
            .request_meta_data_json
            .get("api_callbacks")
            .and_then(|v| v.as_object())
        {
            let world = worlds
                .entry(self.world_name.clone())
                .or_insert_with(World::default);

            for called_simulation_name in api_callbacks.keys() {
                if let Some(simulation) = world.simulations.get_mut(called_simulation_name) {
                    simulation.meta_data_state = MetaDataState::WaitAfterSendReceiveData;
                }
            }
        }

        // --- Store Metadata and Update Current Simulation State ---
        let simulation = worlds
            .entry(self.world_name.clone())
            .or_insert_with(World::default)
            .simulations
            .entry(self.simulation_name.clone())
            .or_insert_with(Simulation::default);

        simulation.request_meta_data_json = self.request_meta_data_json.clone();

        if self.request_simulation_name == self.simulation_name
            && simulation.meta_data_state == MetaDataState::WaitAfterOtherSendRequestMetaData
        {
            simulation.meta_data_state = MetaDataState::WaitAfterOtherNormal;
        } else if self.request_simulation_name != self.simulation_name
            || simulation.meta_data_state != MetaDataState::WaitAfterOtherNormal
        {
            simulation.meta_data_state = MetaDataState::Normal;
        }

        // --- Unit and Handedness Conversion ---
        let length_unit = meta_data
            .get("length_unit")
            .and_then(|v| v.as_str())
            .unwrap_or("m");
        let angle_unit = meta_data
            .get("angle_unit")
            .and_then(|v| v.as_str())
            .unwrap_or("rad");
        let handedness = meta_data
            .get("handedness")
            .and_then(|v| v.as_str())
            .unwrap_or("rhs");
        let mass_unit = meta_data
            .get("mass_unit")
            .and_then(|v| v.as_str())
            .unwrap_or("kg");
        let time_unit = meta_data
            .get("time_unit")
            .and_then(|v| v.as_str())
            .unwrap_or("s");

        let scale_l = *UNIT_SCALE.get(length_unit).unwrap_or(&1.0);
        let scale_a = *UNIT_SCALE.get(angle_unit).unwrap_or(&1.0);
        let scale_m = *UNIT_SCALE.get(mass_unit).unwrap_or(&1.0);
        let scale_t = *UNIT_SCALE.get(time_unit).unwrap_or(&1.0);

        self.conversion_map = ConversionMap::default();

        // --- Populate Double Conversion Map ---
        let mut all_double_attrs = init_attribute_map_double();
        all_double_attrs.insert("joint_position".to_string(), (Attribute::JointPosition, vec![f64::NAN]));
        all_double_attrs.insert("joint_quaternion".to_string(), (Attribute::JointQuaternion, vec![f64::NAN; 4]));
        all_double_attrs.insert("cmd_joint_linear_position".to_string(), (Attribute::CmdJointLinearPosition, vec![f64::NAN]));
        all_double_attrs.insert("cmd_joint_angular_position".to_string(), (Attribute::CmdJointAngularPosition, vec![f64::NAN]));
        all_double_attrs.insert("cmd_joint_linear_velocity".to_string(), (Attribute::CmdJointLinearVelocity, vec![f64::NAN]));
        all_double_attrs.insert("cmd_joint_angular_velocity".to_string(), (Attribute::CmdJointAngularVelocity, vec![f64::NAN]));
        all_double_attrs.insert("cmd_joint_linear_acceleration".to_string(), (Attribute::CmdJointLinearAcceleration, vec![f64::NAN]));
        all_double_attrs.insert("cmd_joint_angular_acceleration".to_string(), (Attribute::CmdJointAngularAcceleration, vec![f64::NAN]));
        all_double_attrs.insert("cmd_joint_force".to_string(), (Attribute::CmdJointForce, vec![f64::NAN]));
        all_double_attrs.insert("cmd_joint_torque".to_string(), (Attribute::CmdJointTorque, vec![f64::NAN]));
        
        all_double_attrs.insert(
            "joint_linear_acceleration".to_string(),
            (Attribute::JointLinearAcceleration, vec![f64::NAN]),
        );
        all_double_attrs.insert(
            "joint_angular_acceleration".to_string(),
            (Attribute::JointAngularAcceleration, vec![f64::NAN]),
        );
        
        for (_, (attr, default_vec)) in all_double_attrs {
            self.conversion_map
                .conversion_map_double
                .insert(attr, default_vec);
        }

        let cmap = &mut self.conversion_map.conversion_map_double;

        cmap.get_mut(&Attribute::Time).unwrap().fill(scale_t);
        cmap.get_mut(&Attribute::Scalar).unwrap().fill(1.0);
        cmap.get_mut(&Attribute::Position).unwrap().fill(scale_l);
        cmap.get_mut(&Attribute::Quaternion).unwrap().fill(1.0);
        cmap.get_mut(&Attribute::LinearVelocity).unwrap().fill(scale_l / scale_t);
        cmap.get_mut(&Attribute::AngularVelocity).unwrap().fill(scale_a / scale_t);
        cmap.get_mut(&Attribute::LinearAcceleration).unwrap().fill(scale_l / (scale_t * scale_t));
        cmap.get_mut(&Attribute::AngularAcceleration).unwrap().fill(scale_a / (scale_t * scale_t));
        cmap.get_mut(&Attribute::JointLinearPosition).unwrap().fill(scale_l);
        cmap.get_mut(&Attribute::JointAngularPosition).unwrap().fill(scale_a);
        cmap.get_mut(&Attribute::JointLinearVelocity).unwrap().fill(scale_l / scale_t);
        cmap.get_mut(&Attribute::JointAngularVelocity).unwrap().fill(scale_a / scale_t);
        cmap.get_mut(&Attribute::JointLinearAcceleration).unwrap().fill(scale_l / (scale_t * scale_t));
        cmap.get_mut(&Attribute::JointAngularAcceleration).unwrap().fill(scale_a / (scale_t * scale_t));
        cmap.get_mut(&Attribute::JointForce).unwrap().fill(scale_m * scale_l / (scale_t * scale_t));
        cmap.get_mut(&Attribute::JointTorque).unwrap().fill(scale_m * scale_l * scale_l / (scale_t * scale_t));
        cmap.get_mut(&Attribute::JointPosition).unwrap().fill(scale_l);
        cmap.get_mut(&Attribute::JointQuaternion).unwrap().fill(1.0);
        cmap.get_mut(&Attribute::Force).unwrap().fill(scale_m * scale_l / (scale_t * scale_t));
        cmap.get_mut(&Attribute::Torque).unwrap().fill(scale_m * scale_l * scale_l / (scale_t * scale_t));

        cmap.insert(Attribute::CmdJointAngularPosition, cmap[&Attribute::JointAngularPosition].clone());
        cmap.insert(Attribute::CmdJointLinearPosition, cmap[&Attribute::JointLinearPosition].clone());
        cmap.insert(Attribute::CmdJointLinearVelocity, cmap[&Attribute::JointLinearVelocity].clone());
        cmap.insert(Attribute::CmdJointAngularVelocity, cmap[&Attribute::JointAngularVelocity].clone());
        cmap.insert(Attribute::CmdJointLinearAcceleration, cmap[&Attribute::JointLinearAcceleration].clone());
        cmap.insert(Attribute::CmdJointAngularAcceleration, cmap[&Attribute::JointAngularAcceleration].clone());
        cmap.insert(Attribute::CmdJointForce, cmap[&Attribute::Force].clone());
        cmap.insert(Attribute::CmdJointTorque, cmap[&Attribute::Torque].clone());

        let odom_vel = cmap.get_mut(&Attribute::OdometricVelocity).unwrap();
        for i in 0..3 { odom_vel[i] = scale_l / scale_t; }
        for i in 3..6 { odom_vel[i] = scale_a / scale_t; }

        for (attr, conversion_scale) in cmap.iter_mut() {
            if let Some(handedness_map) = HANDEDNESS_SCALE.get(attr) {
                if let Some(handedness_scale) = handedness_map.get(handedness) {
                    for (i, scale_val) in conversion_scale.iter_mut().enumerate() {
                        *scale_val *= handedness_scale.get(i).unwrap_or(&1.0);
                    }
                }
            }
        }

        for (_, (attr, default_vec)) in init_attribute_map_uint8() {
            self.conversion_map
                .conversion_map_uint8
                .insert(attr, default_vec);
        }
        for (_, (attr, default_vec)) in init_attribute_map_uint16() {
            self.conversion_map
                .conversion_map_uint16
                .insert(attr, default_vec);
        }

        // --- Final Response Setup ---
        let world_time = worlds.get(&self.world_name).map_or(0.0, |w| w.time);
        self.response_meta_data_json = json!({
            "meta_data": meta_data.clone(),
            "time": world_time * scale_t
        });

        Ok(())
    }

    async fn bind_send_objects(&mut self) -> Result<()> {
        self.send_objects_json = self
            .request_meta_data_json
            .get("send")
            .cloned()
            .unwrap_or(serde_json::Value::Object(serde_json::Map::new()));

        // Clear data_vec for fresh binding
        self.send_buffer.buffer_double.data_vec.clear();
        self.send_buffer.buffer_uint8.data_vec.clear();
        self.send_buffer.buffer_uint16.data_vec.clear();

        // Build response send structure with value arrays
        let mut response_send = serde_json::Map::new();

        let attr_map_double = init_attribute_map_double();
        let attr_map_uint8 = init_attribute_map_uint8();
        let attr_map_uint16 = init_attribute_map_uint16();

        if let Some(send_obj) = self.send_objects_json.as_object() {
            for (object_name, attributes) in send_obj {
                let mut object_attrs = serde_json::Map::new();

                if let Some(attr_array) = attributes.as_array() {
                    for attr_value in attr_array {
                        if let Some(attr_name) = attr_value.as_str() {
                            // Process double attributes
                            if let Some((attr_type, default_data)) = attr_map_double.get(attr_name) {
                                if let Some(conversion_scale) = self.conversion_map.conversion_map_double.get(attr_type) {
                                    let mut values = Vec::new();
                                    for i in 0..default_data.len() {
                                        let conversion = conversion_scale.get(i).copied().unwrap_or(1.0);
                                        self.send_buffer.buffer_double.data_vec.push((default_data[i], conversion));

                                        // Add value to response (converted)
                                        let value = default_data[i] * conversion;
                                        if value.is_nan() {
                                            values.push(serde_json::Value::Null);
                                        } else {
                                            values.push(json!(value));
                                        }
                                    }
                                    object_attrs.insert(attr_name.to_string(), json!(values));
                                }
                            }

                            // Process uint8 attributes
                            if let Some((attr_type, default_data)) = attr_map_uint8.get(attr_name) {
                                if let Some(conversion_scale) = self.conversion_map.conversion_map_uint8.get(attr_type) {
                                    let mut values = Vec::new();
                                    for i in 0..default_data.len() {
                                        let conversion = conversion_scale.get(i).copied().unwrap_or(0);
                                        self.send_buffer.buffer_uint8.data_vec.push((default_data[i], conversion));

                                        // Add value to response (right shift like C++)
                                        values.push(json!(default_data[i] >> conversion));
                                    }
                                    object_attrs.insert(attr_name.to_string(), json!(values));
                                }
                            }

                            // Process uint16 attributes
                            if let Some((attr_type, default_data)) = attr_map_uint16.get(attr_name) {
                                if let Some(conversion_scale) = self.conversion_map.conversion_map_uint16.get(attr_type) {
                                    let mut values = Vec::new();
                                    for i in 0..default_data.len() {
                                        let conversion = conversion_scale.get(i).copied().unwrap_or(0);
                                        self.send_buffer.buffer_uint16.data_vec.push((default_data[i], conversion));

                                        // Add value to response (right shift like C++)
                                        values.push(json!(default_data[i] >> conversion));
                                    }
                                    object_attrs.insert(attr_name.to_string(), json!(values));
                                }
                            }
                        }
                    }
                }

                response_send.insert(object_name.clone(), json!(object_attrs));
            }
        }

        debug!(
            "[Server] bind_send_objects: data_vec sizes: double={}, uint8={}, uint16={}",
            self.send_buffer.buffer_double.data_vec.len(),
            self.send_buffer.buffer_uint8.data_vec.len(),
            self.send_buffer.buffer_uint16.data_vec.len()
        );

        // Add to response
        if let Some(response_obj) = self.response_meta_data_json.as_object_mut() {
            response_obj.insert("send".to_string(), json!(response_send));
        }

        Ok(())
    }

    async fn validate_meta_data(&mut self) -> Result<()> {
        self.receive_objects_json = self
            .request_meta_data_json
            .get("receive")
            .cloned()
            .unwrap_or(serde_json::Value::Object(serde_json::Map::new()));

        // Note: receive structure will be added to response in bind_receive_objects()

        Ok(())
    }

    async fn wait_for_objects(&mut self) -> Result<()> {
        // In full implementation, wait for all required objects to be declared
        Ok(())
    }

    async fn bind_receive_objects(&mut self) -> Result<()> {
        // Clear data_vec for fresh binding
        self.receive_buffer.buffer_double.data_vec.clear();
        self.receive_buffer.buffer_uint8.data_vec.clear();
        self.receive_buffer.buffer_uint16.data_vec.clear();

        // Build response receive structure with value arrays
        let mut response_receive = serde_json::Map::new();

        let attr_map_double = init_attribute_map_double();
        let attr_map_uint8 = init_attribute_map_uint8();
        let attr_map_uint16 = init_attribute_map_uint16();
        let cumulative_attrs = cumulative_attributes();

        // Need to work with world objects - get mutable access
        let mut worlds = WORLDS.lock();
        let world = worlds.entry(self.world_name.clone()).or_insert_with(World::default);

        if let Some(receive_obj) = self.receive_objects_json.as_object() {
            for (object_name, attributes) in receive_obj {
                let mut object_attrs = serde_json::Map::new();

                if let Some(attr_array) = attributes.as_array() {
                    for attr_value in attr_array {
                        if let Some(attr_name) = attr_value.as_str() {
                            // Get or create object and attribute in world
                            let object = world.objects.entry(object_name.clone()).or_insert_with(Object::default);
                            let attribute = object.attributes.entry(attr_name.to_string()).or_insert_with(AttributeData::default);

                            // Handle cumulative attributes (like force, torque)
                            let is_cumulative = cumulative_attrs.contains(&attr_name);

                            // Process double attributes
                            if let Some((attr_type, default_data)) = attr_map_double.get(attr_name) {
                                // Initialize attribute data if needed
                                if attribute.attribute_double.data.is_empty() {
                                    attribute.attribute_double.data = default_data.clone();
                                    if is_cumulative {
                                        attribute.attribute_double.is_sent = true;
                                    }
                                }

                                if let Some(conversion_scale) = self.conversion_map.conversion_map_double.get(attr_type) {
                                    let mut values = Vec::new();
                                    for i in 0..attribute.attribute_double.data.len() {
                                        // For receive, use inverse conversion
                                        let conversion = conversion_scale.get(i).copied().unwrap_or(1.0);
                                        let inv_conversion = if conversion != 0.0 { 1.0 / conversion } else { 1.0 };
                                        self.receive_buffer.buffer_double.data_vec.push((attribute.attribute_double.data[i], inv_conversion));

                                        // Add value to response (with inverse conversion)
                                        let value = attribute.attribute_double.data[i] * inv_conversion;
                                        if value.is_nan() {
                                            values.push(serde_json::Value::Null);
                                        } else {
                                            values.push(json!(value));
                                        }
                                    }
                                    object_attrs.insert(attr_name.to_string(), json!(values));
                                }
                            }

                            // Process uint8 attributes
                            if let Some((attr_type, default_data)) = attr_map_uint8.get(attr_name) {
                                // Initialize attribute data if needed
                                if attribute.attribute_uint8.data.is_empty() {
                                    attribute.attribute_uint8.data = default_data.clone();
                                    if is_cumulative {
                                        attribute.attribute_uint8.is_sent = true;
                                    }
                                }

                                if let Some(conversion_scale) = self.conversion_map.conversion_map_uint8.get(attr_type) {
                                    let mut values = Vec::new();
                                    for i in 0..attribute.attribute_uint8.data.len() {
                                        let conversion = conversion_scale.get(i).copied().unwrap_or(0);
                                        self.receive_buffer.buffer_uint8.data_vec.push((attribute.attribute_uint8.data[i], conversion));

                                        // Add value to response (right shift like C++)
                                        values.push(json!(attribute.attribute_uint8.data[i] >> conversion));
                                    }
                                    object_attrs.insert(attr_name.to_string(), json!(values));
                                }
                            }

                            // Process uint16 attributes
                            if let Some((attr_type, default_data)) = attr_map_uint16.get(attr_name) {
                                // Initialize attribute data if needed
                                if attribute.attribute_uint16.data.is_empty() {
                                    attribute.attribute_uint16.data = default_data.clone();
                                    if is_cumulative {
                                        attribute.attribute_uint16.is_sent = true;
                                    }
                                }

                                if let Some(conversion_scale) = self.conversion_map.conversion_map_uint16.get(attr_type) {
                                    let mut values = Vec::new();
                                    for i in 0..attribute.attribute_uint16.data.len() {
                                        let conversion = conversion_scale.get(i).copied().unwrap_or(0);
                                        self.receive_buffer.buffer_uint16.data_vec.push((attribute.attribute_uint16.data[i], conversion));

                                        // Add value to response (right shift like C++)
                                        values.push(json!(attribute.attribute_uint16.data[i] >> conversion));
                                    }
                                    object_attrs.insert(attr_name.to_string(), json!(values));
                                }
                            }
                        }
                    }
                }

                response_receive.insert(object_name.clone(), json!(object_attrs));
            }
        }

        info!("[Server] bind_receive_objects: data_vec sizes: double={}, uint8={}, uint16={}",
              self.receive_buffer.buffer_double.data_vec.len(),
              self.receive_buffer.buffer_uint8.data_vec.len(),
              self.receive_buffer.buffer_uint16.data_vec.len()
        );
        // drop(worlds);

        // Add to response
        if let Some(response_obj) = self.response_meta_data_json.as_object_mut() {
            response_obj.insert("receive".to_string(), json!(response_receive));
        }

        Ok(())
    }

    async fn wait_for_api_callbacks_response(&mut self) -> Result<()> {
        // In full implementation, wait for API callback responses
        Ok(())
    }

    async fn send_response_meta_data(&mut self) -> Result<()> {
        if crate::utils::should_shutdown() {
            let message_spec = 0i32;
            self.send_message(&message_spec.to_le_bytes(), false).await?;
        } else {
            let message_spec = 1i32;
            self.send_message(&message_spec.to_le_bytes(), true).await?;

            let json_str = serde_json::to_string_pretty(&self.response_meta_data_json)?;
            self.send_message(json_str.as_bytes(), false).await?;

            debug!("[Server] Sent response metadata");

            // Now that we've successfully sent data, set cleanup fields
            // This ensures Drop will clean up if we crash/disconnect later
            if self.cleanup_world_name.is_empty() {
                self.cleanup_world_name = self.world_name.clone();
                self.cleanup_simulation_name = self.simulation_name.clone();
                debug!("[Server] Set cleanup fields: world={}, simulation={}",
                       self.cleanup_world_name, self.cleanup_simulation_name);
            }
        }

        Ok(())
    }

    fn init_send_and_receive_data(&mut self) {
        // Initialize buffers based on data_vec sizes
        self.send_buffer.buffer_double.size = self.send_buffer.buffer_double.data_vec.len();
        self.send_buffer.buffer_uint8.size = self.send_buffer.buffer_uint8.data_vec.len();
        self.send_buffer.buffer_uint16.size = self.send_buffer.buffer_uint16.data_vec.len();

        self.receive_buffer.buffer_double.size = self.receive_buffer.buffer_double.data_vec.len();
        self.receive_buffer.buffer_uint8.size = self.receive_buffer.buffer_uint8.data_vec.len();
        self.receive_buffer.buffer_uint16.size = self.receive_buffer.buffer_uint16.data_vec.len();

        debug!(
            "[Server] init_send_and_receive_data: send=[{}-{}-{}], receive=[{}-{}-{}]",
            self.send_buffer.buffer_double.size,
            self.send_buffer.buffer_uint8.size,
            self.send_buffer.buffer_uint16.size,
            self.receive_buffer.buffer_double.size,
            self.receive_buffer.buffer_uint8.size,
            self.receive_buffer.buffer_uint16.size
        );
    }

    async fn wait_for_other_send_data(&mut self) -> Result<()> {
        // Wait for other simulation to send data
        let timeout = Duration::from_secs(30);
        let start = std::time::Instant::now();

        loop {
            if start.elapsed() > timeout {
                anyhow::bail!("Timeout waiting for other simulation");
            }

            if crate::utils::should_shutdown() {
                break;
            }

            sleep(Duration::from_millis(100)).await;

            // Check if other simulation is ready
            // Simplified - full implementation would check actual state
            break;
        }

        Ok(())
    }

    async fn bind_send_data(&mut self) -> Result<()> {
        // Copy data from send_buffer to world objects
        // This makes data available for other workers to read
        let mut worlds = WORLDS.lock();
        let world = worlds.entry(self.world_name.clone()).or_insert_with(World::default);

        // Move attribute map creation outside the loops for performance
        let attr_map_double = init_attribute_map_double();

        // Process send_objects_json to update world objects
        if let Some(send_obj) = self.send_objects_json.as_object() {
            let mut data_idx = 0;

            for (object_name, attributes) in send_obj {
                let Some(attr_array) = attributes.as_array() else { continue };

                for attr_value in attr_array {
                    let Some(attr_name) = attr_value.as_str() else { continue };

                    // Check if it's a double attribute
                    if let Some((_, default_data)) = attr_map_double.get(attr_name) {
                        // Get or create the object and attribute in world
                        let object = world.objects.entry(object_name.clone()).or_insert_with(Object::default);
                        let attribute = object.attributes.entry(attr_name.to_string()).or_insert_with(AttributeData::default);

                        // Ensure attribute data is initialized
                        if attribute.attribute_double.data.is_empty() {
                            attribute.attribute_double.data = default_data.clone();
                        }

                        // Calculate how many elements we can safely copy
                        let data_len = default_data.len().min(
                            self.send_buffer.buffer_double.size.saturating_sub(data_idx)
                        );

                        // Copy received data with conversion
                        for i in 0..data_len {
                            let received_value = self.send_buffer.buffer_double.data[data_idx];
                            let conversion = self.send_buffer.buffer_double.data_vec[data_idx].1;
                            attribute.attribute_double.data[i] = received_value * conversion;
                            data_idx += 1;
                        }
                        attribute.attribute_double.is_sent = true;
                    }
                }
            }
        }

        Ok(())
    }

    async fn wait_for_receive_data(&mut self) -> Result<()> {
        // Wait for receive data to be ready
        Ok(())
    }

    async fn compute_cumulative_data(&mut self) -> Result<()> {
        // Compute cumulative attributes like force and torque
        Ok(())
    }

    async fn bind_receive_data(&mut self) -> Result<()> {
        // Allocate receive buffer data if needed
        if self.receive_buffer.buffer_double.data.len() != self.receive_buffer.buffer_double.size {
            self.receive_buffer.buffer_double.data = vec![0.0; self.receive_buffer.buffer_double.size];
        }
        if self.receive_buffer.buffer_uint8.data.len() != self.receive_buffer.buffer_uint8.size {
            self.receive_buffer.buffer_uint8.data = vec![0u8; self.receive_buffer.buffer_uint8.size];
        }
        if self.receive_buffer.buffer_uint16.data.len() != self.receive_buffer.buffer_uint16.size {
            self.receive_buffer.buffer_uint16.data = vec![0u16; self.receive_buffer.buffer_uint16.size];
        }

        // Read data from world objects
        let worlds = WORLDS.lock();
        let Some(world) = worlds.get(&self.world_name) else {
            return Ok(());
        };

        // Move attribute map creation outside the loops for performance
        let attr_map_double = init_attribute_map_double();

        if let Some(receive_obj) = self.receive_objects_json.as_object() {
            let mut data_idx = 0;

            for (object_name, attributes) in receive_obj {
                let Some(attr_array) = attributes.as_array() else { continue };

                for attr_value in attr_array {
                    let Some(attr_name) = attr_value.as_str() else { continue };

                    // Read from world objects
                    let Some(object) = world.objects.get(object_name.as_str()) else { continue };
                    let Some(attribute) = object.attributes.get(attr_name) else { continue };

                    if let Some((_, default_data)) = attr_map_double.get(attr_name) {
                        // Copy data to receive buffer with conversion
                        for i in 0..default_data.len() {
                            if data_idx < self.receive_buffer.buffer_double.size
                                && i < attribute.attribute_double.data.len()
                            {
                                let world_value = attribute.attribute_double.data[i];
                                let conversion = if data_idx < self.receive_buffer.buffer_double.data_vec.len() {
                                    self.receive_buffer.buffer_double.data_vec[data_idx].1
                                } else {
                                    1.0
                                };
                                self.receive_buffer.buffer_double.data[data_idx] = world_value * conversion;
                                data_idx += 1;
                            }
                        }
                    }
                }
            }
        }

        Ok(())
    }

    async fn receive_new_request_meta_data(&mut self) -> Result<()> {
        info!("[Server] Receiving new request metadata");
        // Handle metadata rebinding
        Ok(())
    }

    async fn send_receive_data(&mut self) -> Result<()> {
        if crate::utils::should_shutdown() {
            let message_spec = 0i32;
            self.send_message(&message_spec.to_le_bytes(), false).await?;
            return Ok(());
        }
        
        // Calculate message spec based on buffer counts
        let has_double = self.receive_buffer.buffer_double.size > 0;
        let has_uint8 = self.receive_buffer.buffer_uint8.size > 0;
        let has_uint16 = self.receive_buffer.buffer_uint16.size > 0;

        // Count active buffers more efficiently
        let buffer_count = [has_double, has_uint8, has_uint16]
            .iter()
            .filter(|&&b| b)
            .count() as i32;
        let message_spec = 2 + buffer_count;

        self.send_message(&message_spec.to_le_bytes(), true).await?;

        // Send time
        let worlds = WORLDS.lock();
        let time = worlds
            .get(&self.world_name)
            .map(|w| w.time)
            .unwrap_or(0.0);
        info!("[Server] Sending receive data at time {}", time);
        drop(worlds);

        let has_more_data = buffer_count > 0;
        self.send_message(&time.to_le_bytes(), has_more_data).await?;

        // Prepare buffer slices before sending to avoid borrow checker issues
        // We need to create the byte slices before the mutable borrow in send_message

        if has_double {
            let more = has_uint8 || has_uint16;
            // Convert Vec<f64> to bytes - create owned Vec to avoid borrow issues
            let double_bytes: Vec<u8> = unsafe {
                std::slice::from_raw_parts(
                    self.receive_buffer.buffer_double.data.as_ptr() as *const u8,
                    self.receive_buffer.buffer_double.data.len() * std::mem::size_of::<f64>(),
                )
            }
            .to_vec();
            self.send_message(&double_bytes, more).await?;
        }

        if has_uint8 {
            let more = has_uint16;
            // Clone Vec<u8> to avoid borrow issues
            let uint8_bytes = self.receive_buffer.buffer_uint8.data.clone();
            self.send_message(&uint8_bytes, more).await?;
        }

        if has_uint16 {
            // Convert Vec<u16> to bytes - create owned Vec to avoid borrow issues
            let uint16_bytes: Vec<u8> = unsafe {
                std::slice::from_raw_parts(
                    self.receive_buffer.buffer_uint16.data.as_ptr() as *const u8,
                    self.receive_buffer.buffer_uint16.data.len() * std::mem::size_of::<u16>(),
                )
            }
            .to_vec();
            self.send_message(&uint16_bytes, false).await?;
        }

        debug!("[Server] Sent receive data");
        Ok(())
    }
}

impl Drop for MultiverseServer {
    fn drop(&mut self) {
        info!("[Server] Close socket {}. Shutting down and cleaning up...", self.socket_addr);
        // Use cleanup fields which persist even after reconnection clears the current fields
        if !self.cleanup_simulation_name.is_empty() && !self.cleanup_world_name.is_empty() {
            let mut worlds = WORLDS.lock();
            let should_remove_world = if let Some(world) = worlds.get_mut(&self.cleanup_world_name) {
                if world.simulations.remove(&self.cleanup_simulation_name).is_some() {
                    info!("[Server] [{}] Cleaned up simulation {} from world {}",
                          self.socket_addr, self.cleanup_simulation_name, self.cleanup_world_name);

                    // Check if world is now empty
                    if world.simulations.is_empty() {
                        info!("[Server] [{}] World '{}' has no more simulations, will drop it ({}  total worlds before removal)",
                              self.socket_addr, self.cleanup_world_name, worlds.len());
                        true
                    } else {
                        info!("[Server] [{}] World '{}' still has {} simulation(s) in Drop, keeping it alive",
                              self.socket_addr, self.cleanup_world_name, world.simulations.len());
                        false
                    }
                } else {
                    warn!("[Server] [{}] Simulation {} not found in world {} for cleanup",
                          self.socket_addr, self.cleanup_simulation_name, self.cleanup_world_name);
                    false
                }
            } else {
                warn!("[Server] [{}] World '{}' not found in WORLDS map for cleanup",
                      self.socket_addr, self.cleanup_world_name);
                false
            };

            // Remove world after releasing the mutable borrow
            if should_remove_world {
                worlds.remove(&self.cleanup_world_name);
                info!("[Server] [{}] World '{}' dropped successfully in Drop, {} worlds remaining",
                      self.socket_addr, self.cleanup_world_name, worlds.len());
            }
        } else {
            debug!("[Server] [{}] No simulation name set, no global state to clean up", self.socket_addr);
        }

        info!("[Server] Cleanup complete for socket {}", self.socket_addr);
    }
}