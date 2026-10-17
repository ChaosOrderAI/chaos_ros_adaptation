//! ROS 2 side of the bridge: publisher creation per route (`r2r` bindings).

use serde_json::Value;

use crate::routes::{MsgType, Route};

/// What the adapter needs from a per-route ROS 2 publisher.
pub trait CommandPublisher {
    /// Publish the serialized bridge envelope on a `std_msgs/String` route.
    fn publish_bridge(&self, envelope_json: String) -> Result<(), String>;
    /// Publish the command's `value` field as a scalar message on typed routes.
    fn publish_typed(&self, value: &Value) -> Result<(), String>;
}

/// One publisher per routed command, typed by the route's `msg_type`.
pub enum TypedPublisher {
    String(r2r::Publisher<r2r::std_msgs::msg::String>),
    Bool(r2r::Publisher<r2r::std_msgs::msg::Bool>),
    Int32(r2r::Publisher<r2r::std_msgs::msg::Int32>),
    Int64(r2r::Publisher<r2r::std_msgs::msg::Int64>),
    Float64(r2r::Publisher<r2r::std_msgs::msg::Float64>),
}

impl TypedPublisher {
    pub fn create(
        node: &mut r2r::Node,
        route: &Route,
        topic: &str,
        qos: r2r::QosProfile,
    ) -> Result<Self, String> {
        match route.msg_type {
            MsgType::StringBridge => node
                .create_publisher::<r2r::std_msgs::msg::String>(topic, qos)
                .map(Self::String)
                .map_err(|error| format!("create_publisher({topic}): {error}")),
            MsgType::Bool => node
                .create_publisher::<r2r::std_msgs::msg::Bool>(topic, qos)
                .map(Self::Bool)
                .map_err(|error| format!("create_publisher({topic}): {error}")),
            MsgType::Int32 => node
                .create_publisher::<r2r::std_msgs::msg::Int32>(topic, qos)
                .map(Self::Int32)
                .map_err(|error| format!("create_publisher({topic}): {error}")),
            MsgType::Int64 => node
                .create_publisher::<r2r::std_msgs::msg::Int64>(topic, qos)
                .map(Self::Int64)
                .map_err(|error| format!("create_publisher({topic}): {error}")),
            MsgType::Float64 => node
                .create_publisher::<r2r::std_msgs::msg::Float64>(topic, qos)
                .map(Self::Float64)
                .map_err(|error| format!("create_publisher({topic}): {error}")),
        }
    }

    fn msg_type(&self) -> MsgType {
        match self {
            Self::String(_) => MsgType::StringBridge,
            Self::Bool(_) => MsgType::Bool,
            Self::Int32(_) => MsgType::Int32,
            Self::Int64(_) => MsgType::Int64,
            Self::Float64(_) => MsgType::Float64,
        }
    }
}

impl CommandPublisher for TypedPublisher {
    fn publish_bridge(&self, envelope_json: String) -> Result<(), String> {
        match self {
            Self::String(publisher) => publisher
                .publish(&r2r::std_msgs::msg::String { data: envelope_json })
                .map_err(|error| format!("publish std_msgs/String: {error}")),
            other => Err(format!(
                "route publishes {}; cannot send bridge envelope",
                other.msg_type().name()
            )),
        }
    }

    fn publish_typed(&self, value: &Value) -> Result<(), String> {
        match self {
            Self::Bool(publisher) => {
                let data = value
                    .as_bool()
                    .ok_or_else(|| "value must be a boolean".to_string())?;
                publisher
                    .publish(&r2r::std_msgs::msg::Bool { data })
                    .map_err(|error| format!("publish std_msgs/Bool: {error}"))
            }
            Self::Int32(publisher) => {
                let data = value.as_i64().and_then(|number| i32::try_from(number).ok()).ok_or_else(|| {
                    "value must be an integer in the i32 range".to_string()
                })?;
                publisher
                    .publish(&r2r::std_msgs::msg::Int32 { data })
                    .map_err(|error| format!("publish std_msgs/Int32: {error}"))
            }
            Self::Int64(publisher) => {
                let data = value
                    .as_i64()
                    .ok_or_else(|| "value must be an integer".to_string())?;
                publisher
                    .publish(&r2r::std_msgs::msg::Int64 { data })
                    .map_err(|error| format!("publish std_msgs/Int64: {error}"))
            }
            Self::Float64(publisher) => {
                let data = value
                    .as_f64()
                    .ok_or_else(|| "value must be a number".to_string())?;
                publisher
                    .publish(&r2r::std_msgs::msg::Float64 { data })
                    .map_err(|error| format!("publish std_msgs/Float64: {error}"))
            }
            Self::String(_) => Err("std_msgs/String routes use publish_bridge".to_string()),
        }
    }
}
