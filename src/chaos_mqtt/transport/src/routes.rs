//! Command routing table, ported from `Ros2Adapter::routes()` in
//! `orin_gateway/src/ros2_adapter.cpp`.
//!
//! Every command name maps to a topic suffix below `ros2.topic_prefix`.
//! `msg_type` selects the ROS 2 message type published on that topic:
//!
//! - `std_msgs/String` (default, C++-compatible): the full bridge envelope
//!   `{"bridge": {...}, "payload": {...}}` is serialized as JSON.
//! - `std_msgs/Bool`, `std_msgs/Int32`, `std_msgs/Int64`, `std_msgs/Float64`:
//!   the command's `value` field is converted to the scalar message. The
//!   adapter answers MQTT immediately after publishing (no pending request).

use std::collections::HashMap;

use serde_json::Value;

use crate::config::{RouteProfile, RouteSpec};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum MsgType {
    StringBridge,
    Bool,
    Int32,
    Int64,
    Float64,
}

impl MsgType {
    pub fn parse(name: &str) -> Option<Self> {
        match name {
            "std_msgs/String" | "string" => Some(Self::StringBridge),
            "std_msgs/Bool" | "bool" => Some(Self::Bool),
            "std_msgs/Int32" | "int32" => Some(Self::Int32),
            "std_msgs/Int64" | "int64" => Some(Self::Int64),
            "std_msgs/Float64" | "float64" => Some(Self::Float64),
            _ => None,
        }
    }

    pub fn name(self) -> &'static str {
        match self {
            Self::StringBridge => "std_msgs/String",
            Self::Bool => "std_msgs/Bool",
            Self::Int32 => "std_msgs/Int32",
            Self::Int64 => "std_msgs/Int64",
            Self::Float64 => "std_msgs/Float64",
        }
    }
}

#[derive(Debug, Clone)]
pub struct Route {
    pub command: String,
    pub topic_suffix: String,
    pub msg_type: MsgType,
}

/// (command, topic suffix) pairs, identical to the C++ gateway.
const STATIC_ROUTES: &[(&str, &str)] = &[
    ("status", "system/statusRequest"),
    ("heartbeat", "system/heartbeat"),
    ("control.enable", "control/enable"),
    ("control.disable", "control/disable"),
    ("flight.takeoff", "flightControl/takeoff"),
    ("flight.land", "flightControl/land"),
    ("flight.go_home", "flightControl/goHome"),
    ("flight.cancel_go_home", "flightControl/cancelGoHome"),
    ("flight.brake", "flightControl/brake"),
    ("flight.velocity", "flightControl/velocity"),
    ("flight.fly_to_point", "flightControl/flyToPoint"),
    ("flight.fly_to_point.cancel", "flightControl/flyToPoint/cancel"),
    ("gimbal.reset", "gimbal/reset"),
    ("gimbal.rotate", "gimbal/rotate"),
    ("mission.upload", "mission/upload"),
    ("mission.start", "mission/start"),
    ("mission.pause", "mission/pause"),
    ("mission.resume", "mission/resume"),
    ("mission.stop", "mission/stop"),
    ("video.status", "video/statusRequest"),
    ("video.start", "video/start"),
    ("video.stop", "video/stop"),
    ("algorithm.enable", "algorithm/enable"),
];

pub fn default_routes() -> Vec<Route> {
    STATIC_ROUTES
        .iter()
        .map(|(command, suffix)| Route {
            command: command.to_string(),
            topic_suffix: suffix.to_string(),
            msg_type: MsgType::StringBridge,
        })
        .collect()
}

pub fn configured_routes(profile: RouteProfile, overrides: &[RouteSpec]) -> Result<Vec<Route>, String> {
    let mut routes = match profile {
        RouteProfile::LegacyDrone => default_routes(),
        RouteProfile::Custom => Vec::new(),
    };
    apply_overrides(&mut routes, overrides)?;
    Ok(routes)
}

/// Apply config `routes` entries: replace the msg_type of an existing
/// command or add a brand new route.
pub fn apply_overrides(routes: &mut Vec<Route>, overrides: &[RouteSpec]) -> Result<(), String> {
    for spec in overrides {
        let msg_type = MsgType::parse(&spec.msg_type)
            .ok_or_else(|| format!("unsupported route msg_type: {}", spec.msg_type))?;
        if let Some(existing) = routes
            .iter_mut()
            .find(|route| route.command == spec.command)
        {
            existing.topic_suffix = spec.topic_suffix.clone();
            existing.msg_type = msg_type;
        } else {
            routes.push(Route {
                command: spec.command.clone(),
                topic_suffix: spec.topic_suffix.clone(),
                msg_type,
            });
        }
    }
    Ok(())
}

pub struct RouteTable {
    routes: HashMap<String, Route>,
}

impl RouteTable {
    /// Fails when two routes share a topic but disagree on its message type,
    /// which ROS 2 would otherwise only reject at publisher creation.
    pub fn new(routes: Vec<Route>) -> Result<Self, String> {
        let mut topic_types: HashMap<&str, (&str, MsgType)> = HashMap::new();
        for route in &routes {
            if let Some((other, msg_type)) =
                topic_types.insert(&route.topic_suffix, (&route.command, route.msg_type))
            {
                if msg_type != route.msg_type {
                    return Err(format!(
                        "routes {other} and {} share topic {} with different msg_type ({} vs {})",
                        route.command,
                        route.topic_suffix,
                        msg_type.name(),
                        route.msg_type.name()
                    ));
                }
            }
        }
        Ok(Self {
            routes: routes
                .into_iter()
                .map(|route| (route.command.clone(), route))
                .collect(),
        })
    }

    pub fn get(&self, command: &str) -> Option<&Route> {
        self.routes.get(command)
    }

    pub fn iter(&self) -> impl Iterator<Item = &Route> {
        self.routes.values()
    }
}

/// Bridge envelope placed on the wire for `std_msgs/String` routes, matching
/// the C++ gateway byte-for-byte in field naming.
pub fn bridge_envelope(message_id: &str, timestamp_ms: u64, command: &Value) -> Value {
    serde_json::json!({
        "bridge": {
            "source": "mqtt",
            "request_id": message_id,
            "timestamp_ms": timestamp_ms,
        },
        "payload": command,
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn custom_profile_has_no_implicit_drone_routes() {
        assert!(configured_routes(RouteProfile::Custom, &[]).unwrap().is_empty());
        let routes = configured_routes(RouteProfile::Custom, &[RouteSpec {
            command: "motion.stop".into(),
            topic_suffix: "command".into(),
            msg_type: "std_msgs/String".into(),
        }]).unwrap();
        let table = RouteTable::new(routes).unwrap();
        assert!(table.get("motion.stop").is_some());
        assert!(table.get("flight.takeoff").is_none());
        assert_eq!(configured_routes(RouteProfile::LegacyDrone, &[]).unwrap().len(), STATIC_ROUTES.len());
    }

    #[test]
    fn static_table_matches_reference() {
        let routes = default_routes();
        assert_eq!(routes.len(), STATIC_ROUTES.len());
        let table = RouteTable::new(routes).expect("static routes are consistent");
        assert_eq!(
            table.get("flight.takeoff").unwrap().topic_suffix,
            "flightControl/takeoff"
        );
        assert_eq!(
            table.get("flight.fly_to_point.cancel").unwrap().topic_suffix,
            "flightControl/flyToPoint/cancel"
        );
        assert!(table.get("unknown.command").is_none());
    }

    #[test]
    fn overrides_extend_and_replace() {
        let mut routes = default_routes();
        apply_overrides(
            &mut routes,
            &[
                RouteSpec {
                    command: "flight.velocity".to_string(),
                    topic_suffix: "flightControl/velocity".to_string(),
                    msg_type: "std_msgs/Float64".to_string(),
                },
                RouteSpec {
                    command: "custom.ping".to_string(),
                    topic_suffix: "custom/ping".to_string(),
                    msg_type: "std_msgs/Bool".to_string(),
                },
            ],
        )
        .expect("valid overrides");
        let table = RouteTable::new(routes).expect("consistent routes");
        assert_eq!(
            table.get("flight.velocity").unwrap().msg_type,
            MsgType::Float64
        );
        assert_eq!(table.get("custom.ping").unwrap().msg_type, MsgType::Bool);
        // Untouched routes keep the default.
        assert_eq!(
            table.get("flight.land").unwrap().msg_type,
            MsgType::StringBridge
        );
    }

    #[test]
    fn override_rejects_unknown_type() {
        let mut routes = default_routes();
        let result = apply_overrides(
            &mut routes,
            &[RouteSpec {
                command: "x".to_string(),
                topic_suffix: "y".to_string(),
                msg_type: "geometry_msgs/Twist".to_string(),
            }],
        );
        assert!(result.is_err());
    }

    #[test]
    fn table_rejects_conflicting_topic_types() {
        let mut routes = default_routes();
        apply_overrides(
            &mut routes,
            &[RouteSpec {
                command: "custom.land".to_string(),
                topic_suffix: "flightControl/land".to_string(),
                msg_type: "std_msgs/Bool".to_string(),
            }],
        )
        .expect("valid overrides");
        assert!(RouteTable::new(routes).is_err());
    }

    #[test]
    fn bridge_envelope_shape() {
        let envelope = bridge_envelope("req-1", 42, &serde_json::json!({"command": "status"}));
        assert_eq!(envelope["bridge"]["source"], "mqtt");
        assert_eq!(envelope["bridge"]["request_id"], "req-1");
        assert_eq!(envelope["bridge"]["timestamp_ms"], 42);
        assert_eq!(envelope["payload"]["command"], "status");
    }
}
