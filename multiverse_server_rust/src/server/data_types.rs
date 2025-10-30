use serde::{Deserialize, Serialize};
use std::collections::HashMap;

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[repr(u8)]
pub enum Attribute {
    Time,
    Scalar,
    Position,
    Quaternion,
    LinearVelocity,
    AngularVelocity,
    LinearAcceleration,
    AngularAcceleration,
    OdometricVelocity,
    JointLinearPosition,
    JointAngularPosition,
    JointLinearVelocity,
    JointAngularVelocity,
    JointLinearAcceleration,
    JointAngularAcceleration,
    JointForce,
    JointTorque,
    CmdJointLinearPosition,
    CmdJointAngularPosition,
    CmdJointLinearVelocity,
    CmdJointAngularVelocity,
    CmdJointLinearAcceleration,
    CmdJointAngularAcceleration,
    CmdJointForce,
    CmdJointTorque,
    JointPosition,
    JointQuaternion,
    Force,
    Torque,
    Rgb3840x2160,
    Rgb1280x1024,
    Rgb640x480,
    Rgb128x128,
    Depth3840x2160,
    Depth1280x1024,
    Depth640x480,
    Depth128x128,
}

#[derive(Debug, Clone)]
pub struct TypedAttribute<T> {
    pub data: Vec<T>,
    pub simulation_data: HashMap<String, Vec<T>>,
    pub is_sent: bool,
}

impl<T> Default for TypedAttribute<T> {
    fn default() -> Self {
        Self {
            data: Vec::new(),
            simulation_data: HashMap::new(),
            is_sent: false,
        }
    }
}

#[derive(Debug, Clone, Default)]
pub struct AttributeData {
    pub attribute_double: TypedAttribute<f64>,
    pub attribute_uint8: TypedAttribute<u8>,
    pub attribute_uint16: TypedAttribute<u16>,
}

#[derive(Debug, Clone, Default)]
pub struct Object {
    pub attributes: HashMap<String, AttributeData>,
}

#[derive(Debug, Clone, Default)]
pub struct Simulation {
    pub objects: HashMap<String, Object>,
    pub meta_data_state: super::state::MetaDataState,
    pub request_meta_data_json: serde_json::Value,
    pub api_callbacks: Vec<HashMap<String, Vec<String>>>,
    pub api_callbacks_response: Vec<HashMap<String, Vec<String>>>,
}

#[derive(Debug, Clone, Default)]
pub struct World {
    pub objects: HashMap<String, Object>,
    pub simulations: HashMap<String, Simulation>,
    pub time: f64,
}

#[derive(Debug, Clone)]
pub struct BufferData<T> {
    pub data: Vec<T>,
    pub size: usize,
    pub data_vec: Vec<(T, T)>, // (value, conversion)
}

impl<T: Default> Default for BufferData<T> {
    fn default() -> Self {
        Self {
            data: Vec::new(),
            size: 0,
            data_vec: Vec::new(),
        }
    }
}

#[derive(Debug, Clone, Default)]
pub struct Buffer {
    pub buffer_double: BufferData<f64>,
    pub buffer_uint8: BufferData<u8>,
    pub buffer_uint16: BufferData<u16>,
}

#[derive(Debug, Clone, Default)]
pub struct ConversionMap {
    pub conversion_map_double: HashMap<Attribute, Vec<f64>>,
    pub conversion_map_uint8: HashMap<Attribute, Vec<u8>>,
    pub conversion_map_uint16: HashMap<Attribute, Vec<u16>>,
}

/// Initialize attribute maps with default values
pub fn init_attribute_map_double() -> HashMap<String, (Attribute, Vec<f64>)> {
    let mut map = HashMap::new();
    
    map.insert("time".to_string(), (Attribute::Time, vec![0.0]));
    map.insert("scalar".to_string(), (Attribute::Scalar, vec![f64::NAN]));
    map.insert("position".to_string(), (Attribute::Position, vec![f64::NAN; 3]));
    map.insert("quaternion".to_string(), (Attribute::Quaternion, vec![f64::NAN; 4]));
    map.insert("linear_velocity".to_string(), (Attribute::LinearVelocity, vec![0.0; 3]));
    map.insert("angular_velocity".to_string(), (Attribute::AngularVelocity, vec![0.0; 3]));
    map.insert("linear_acceleration".to_string(), (Attribute::LinearAcceleration, vec![0.0; 3]));
    map.insert("angular_acceleration".to_string(), (Attribute::AngularAcceleration, vec![0.0; 3]));
    map.insert("odometric_velocity".to_string(), (Attribute::OdometricVelocity, vec![0.0; 6]));
    map.insert("joint_linear_position".to_string(), (Attribute::JointLinearPosition, vec![f64::NAN]));
    map.insert("joint_angular_position".to_string(), (Attribute::JointAngularPosition, vec![f64::NAN]));
    map.insert("joint_linear_velocity".to_string(), (Attribute::JointLinearVelocity, vec![f64::NAN]));
    map.insert("joint_angular_velocity".to_string(), (Attribute::JointAngularVelocity, vec![f64::NAN]));
    map.insert("joint_force".to_string(), (Attribute::JointForce, vec![f64::NAN]));
    map.insert("joint_torque".to_string(), (Attribute::JointTorque, vec![f64::NAN]));
    map.insert("force".to_string(), (Attribute::Force, vec![0.0; 3]));
    map.insert("torque".to_string(), (Attribute::Torque, vec![0.0; 3]));
    
    map
}

pub fn init_attribute_map_uint8() -> HashMap<String, (Attribute, Vec<u8>)> {
    let mut map = HashMap::new();
    
    map.insert("rgb_3840_2160".to_string(), (Attribute::Rgb3840x2160, vec![0u8; 3840 * 2160 * 3]));
    map.insert("rgb_1280_1024".to_string(), (Attribute::Rgb1280x1024, vec![0u8; 1280 * 1024 * 3]));
    map.insert("rgb_640_480".to_string(), (Attribute::Rgb640x480, vec![0u8; 640 * 480 * 3]));
    map.insert("rgb_128_128".to_string(), (Attribute::Rgb128x128, vec![0u8; 128 * 128 * 3]));
    
    map
}

pub fn init_attribute_map_uint16() -> HashMap<String, (Attribute, Vec<u16>)> {
    let mut map = HashMap::new();
    
    map.insert("depth_3840_2160".to_string(), (Attribute::Depth3840x2160, vec![0u16; 3840 * 2160]));
    map.insert("depth_1280_1024".to_string(), (Attribute::Depth1280x1024, vec![0u16; 1280 * 1024]));
    map.insert("depth_640_480".to_string(), (Attribute::Depth640x480, vec![0u16; 640 * 480]));
    map.insert("depth_128_128".to_string(), (Attribute::Depth128x128, vec![0u16; 128 * 128]));
    
    map
}

pub fn cumulative_attributes() -> Vec<&'static str> {
    vec!["force", "torque"]
}