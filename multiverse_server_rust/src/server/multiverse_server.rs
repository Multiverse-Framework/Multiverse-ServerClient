use super::data_types::*;
use super::state::{MetaDataState, ServerState};
use crate::transport::{Transport, TransportType};
use anyhow::{Context, Result};
use parking_lot::Mutex;
use serde_json::{json, Value};
use std::collections::HashMap;
use std::f64::consts::PI;
use std::sync::Arc;
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

    // Flags
    is_receive_data_sent: bool,
    #[allow(dead_code)]
    continue_state: bool,
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
            is_receive_data_sent: false,
            continue_state: false,
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
            is_receive_data_sent: false,
            continue_state: false,
        })
    }

    #[cfg(feature = "use-udp")]
    pub async fn new_udp(host: &str, port: &str) -> Result<Self> {
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
            is_receive_data_sent: false,
            continue_state: false,
        })
    }

    pub async fn start(&mut self) -> Result<()> {
        while !crate::utils::should_shutdown() {
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
        let (message_spec, payloads) = match self.recv_message().await {
            Ok(msg) => msg,
            Err(e) => {
                if let Some(io_err) = e.downcast_ref::<std::io::Error>() {
                    match io_err.kind() {
                        std::io::ErrorKind::UnexpectedEof |
                        std::io::ErrorKind::ConnectionReset |
                        std::io::ErrorKind::BrokenPipe => {
                            // Log as INFO, this is a normal disconnect, not a server error
                            info!("[Server] Client at socket {} disconnected: {}", self.socket_addr, io_err.kind());
                            return Err(e); 
                        }
                        _ => {} 
                    }
                }
                error!("[Server] Receive error at socket {}: {}", self.socket_addr, e);
                return Err(e);
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
            drop(worlds);

            // Process data buffers based on message_spec
            if message_spec == 3 && payloads.len() == 2 {
                // One buffer type
                self.copy_buffer_data(&payloads[1])?;
            } else if message_spec == 4 && payloads.len() == 3 {
                // Two buffer types
                self.copy_buffer_data(&payloads[1])?;
                self.copy_buffer_data(&payloads[2])?;
            } else if message_spec == 5 && payloads.len() == 4 {
                // Three buffer types
                self.copy_buffer_data(&payloads[1])?;
                self.copy_buffer_data(&payloads[2])?;
                self.copy_buffer_data(&payloads[3])?;
            }

            return Ok(ServerState::BindSendData);
        }

        anyhow::bail!("Invalid message spec: {}", message_spec)
    }

    fn copy_buffer_data(&mut self, _data: &[u8]) -> Result<()> {
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

        // Check for simulation conflicts
        if self.simulation_name.is_empty()
            && worlds
                .get(&self.request_world_name)
                .is_some_and(|w| w.simulations.contains_key(&self.request_simulation_name))
        {
            anyhow::bail!(
                "[Server] Socket {} requires an existing simulation name ({}).",
                self.socket_addr,
                self.request_simulation_name
            );
        }

        if !self.simulation_name.is_empty()
            && !worlds
                .get(&self.request_world_name)
                .is_some_and(|w| w.simulations.contains_key(&self.request_simulation_name))
        {
            warn!(
                "[Server] Socket {} requests a non-existing simulation ({}).",
                self.socket_addr, self.request_simulation_name
            );
        }

        // --- Simulation Hand-off Logic ---
        let is_simulation_switch = !self.simulation_name.is_empty()
            && self.request_simulation_name != self.simulation_name
            && worlds
                .get(&self.request_world_name)
                .is_some_and(|w| w.simulations.contains_key(&self.request_simulation_name));

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

        // Add to response
        if let Some(response_obj) = self.response_meta_data_json.as_object_mut() {
            response_obj.insert("send".to_string(), self.send_objects_json.clone());
        }
        
        Ok(())
    }

    async fn validate_meta_data(&mut self) -> Result<()> {
        self.receive_objects_json = self
            .request_meta_data_json
            .get("receive")
            .cloned()
            .unwrap_or(serde_json::Value::Object(serde_json::Map::new()));

        // Add to response
        if let Some(response_obj) = self.response_meta_data_json.as_object_mut() {
            response_obj.insert("receive".to_string(), self.receive_objects_json.clone());
        }

        Ok(())
    }

    async fn wait_for_objects(&mut self) -> Result<()> {
        // In full implementation, wait for all required objects to be declared
        Ok(())
    }

    async fn bind_receive_objects(&mut self) -> Result<()> {
        // In full implementation, bind receive objects
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
        // Bind send data from buffers
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
        // Bind receive data to buffers
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

        let buffer_count = (has_double as i32) + (has_uint8 as i32) + (has_uint16 as i32);
        let message_spec = 2 + buffer_count;

        self.send_message(&message_spec.to_le_bytes(), true).await?;

        // Send time
        let worlds = WORLDS.lock();
        let time = worlds
            .get(&self.world_name)
            .map(|w| w.time)
            .unwrap_or(0.0);
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
        if !self.simulation_name.is_empty() {
            let mut worlds = WORLDS.lock();
            if let Some(world) = worlds.get_mut(&self.world_name) {
                if world.simulations.remove(&self.simulation_name).is_some() {
                    info!("[Server] Cleaned up simulation {} from world {} on socket {}",
                          self.simulation_name, self.world_name, self.socket_addr);
                } else {
                    warn!("[Server] Simulation {} not found in world {} for cleanup on socket {}",
                          self.simulation_name, self.world_name, self.socket_addr);
                }
            }
        } else {
            debug!("[Server] No simulation name set, no global state to clean up for socket {}", self.socket_addr);
        }

        info!("[Server] Cleanup complete for socket {}", self.socket_addr);
    }
}