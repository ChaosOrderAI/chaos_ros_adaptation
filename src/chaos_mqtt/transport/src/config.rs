//! Adapter configuration, ported from `orin_gateway/src/config.cpp`
//! (MQTT + ROS 2 sections). PSDK/safety/video sections are intentionally
//! absent: this crate is only the MQTT <-> ROS 2 bridge.

use std::path::{Path, PathBuf};

use serde::Deserialize;

mod communication;
pub use communication::{Delivery, Direction, NetworkConfig, TopicMapping};

#[derive(Debug, thiserror::Error)]
pub enum ConfigError {
    #[error("cannot open configuration {0}: {1}")]
    Open(PathBuf, #[source] std::io::Error),
    #[error("invalid configuration: {0}")]
    Invalid(String),
}

#[derive(Debug, Clone, Deserialize)]
#[serde(default, deny_unknown_fields)]
pub struct MqttConfig {
    pub host: String,
    pub port: u16,
    pub client_id: String,
    pub command_topic: String,
    pub response_topic: String,
    pub event_topic: String,
    pub ca_file: PathBuf,
    pub client_certificate_file: PathBuf,
    pub client_key_file: PathBuf,
    pub username: String,
    pub password_environment: String,
    pub keep_alive_seconds: u16,
    pub maximum_clock_skew_ms: u64,
}

impl Default for MqttConfig {
    fn default() -> Self {
        Self {
            host: String::new(),
            port: 8883,
            client_id: String::new(),
            command_topic: String::new(),
            response_topic: String::new(),
            event_topic: String::new(),
            ca_file: PathBuf::new(),
            client_certificate_file: PathBuf::new(),
            client_key_file: PathBuf::new(),
            username: String::new(),
            password_environment: "MQTT_PASSWORD".to_string(),
            keep_alive_seconds: 30,
            maximum_clock_skew_ms: 5000,
        }
    }
}

#[derive(Debug, Clone, Deserialize)]
#[serde(default, deny_unknown_fields)]
pub struct Ros2Config {
    pub node_name: String,
    pub topic_prefix: String,
    pub qos_depth: usize,
}

impl Default for Ros2Config {
    fn default() -> Self {
        Self {
            node_name: "m3td_mqtt_bridge".to_string(),
            topic_prefix: "m3td".to_string(),
            qos_depth: 32,
        }
    }
}

/// Optional per-route override/addition, e.g.
/// `{ "command": "flight.velocity", "topic_suffix": "flightControl/velocity", "msg_type": "std_msgs/Float64" }`.
#[derive(Debug, Clone, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct RouteSpec {
    pub command: String,
    pub topic_suffix: String,
    #[serde(default = "default_msg_type")]
    pub msg_type: String,
}

fn default_msg_type() -> String {
    "std_msgs/String".to_string()
}

#[derive(Debug, Clone, Deserialize)]
#[serde(default)]
pub struct AdapterConfig {
    pub network: NetworkConfig,
    pub mqtt: MqttConfig,
    pub ros2: Ros2Config,
    /// How long an MQTT request may wait for a matching ROS 2 result
    /// before an error response is published. The C++ gateway dispatches
    /// locally and always answers; this bridge relies on an external ROS 2
    /// node answering on `${prefix}/command/result`, so a bound is needed.
    pub response_timeout_ms: u64,
    /// Bound on observing a terminal result after an explicit nonterminal
    /// ACCEPTED response. Expiry means execution state is unknown, not stopped.
    pub action_timeout_ms: u64,
    /// Legacy drone routes remain the default for existing deployments.
    /// Custom profiles expose only the explicitly configured routes.
    pub route_profile: RouteProfile,
    pub routes: Vec<RouteSpec>,
    pub topic_mappings: Vec<TopicMapping>,
}

impl Default for AdapterConfig {
    fn default() -> Self {
        Self {
            network: NetworkConfig::default(),
            mqtt: MqttConfig::default(),
            ros2: Ros2Config::default(),
            response_timeout_ms: 30_000,
            action_timeout_ms: 360_000,
            route_profile: RouteProfile::default(),
            routes: Vec::new(),
            topic_mappings: Vec::new(),
        }
    }
}

#[derive(Debug, Clone, Copy, Default, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub enum RouteProfile {
    #[default]
    LegacyDrone,
    Custom,
}

fn check_range<T: PartialOrd + std::fmt::Display>(
    value: T,
    key: &str,
    min: T,
    max: T,
) -> Result<(), ConfigError> {
    if value < min || value > max {
        return Err(ConfigError::Invalid(format!(
            "configuration value out of range: {key} = {value} (allowed {min}..={max})"
        )));
    }
    Ok(())
}

fn valid_ros_name(name: &str) -> bool {
    let mut chars = name.chars();
    match chars.next() {
        Some(first) if first.is_ascii_alphabetic() || first == '_' => {}
        _ => return false,
    }
    name.len() <= 255 && chars.all(|c| c.is_ascii_alphanumeric() || c == '_')
}

/// Relative ROS 2 topic: `/`-separated non-empty `[A-Za-z0-9_]` segments.
/// Used for both `ros2.topic_prefix` and route `topic_suffix`.
pub fn valid_topic_prefix(prefix: &str) -> bool {
    if prefix.is_empty() || prefix.len() > 200 || prefix.starts_with('/') || prefix.ends_with('/') {
        return false;
    }
    let mut segment_start = true;
    for c in prefix.chars() {
        if c == '/' {
            if segment_start {
                return false;
            }
            segment_start = true;
            continue;
        }
        if !(c.is_ascii_alphanumeric() || c == '_') {
            return false;
        }
        segment_start = false;
    }
    !segment_start
}

fn valid_mqtt_topic(topic: &str) -> bool {
    !topic.is_empty() && topic.len() <= u16::MAX as usize && !topic.contains(['\0', '+', '#'])
}

pub fn load_config(path: &Path) -> Result<AdapterConfig, ConfigError> {
    let text = std::fs::read_to_string(path)
        .map_err(|error| ConfigError::Open(path.to_path_buf(), error))?;
    let config: AdapterConfig =
        serde_json::from_str(&text).map_err(|error| ConfigError::Invalid(error.to_string()))?;
    communication::validate(&config).map_err(ConfigError::Invalid)?;

    let mqtt = &config.mqtt;
    check_range(mqtt.port, "mqtt.port", 1, u16::MAX)?;
    check_range(mqtt.keep_alive_seconds, "mqtt.keep_alive_seconds", 10, 300)?;
    check_range(
        mqtt.maximum_clock_skew_ms,
        "mqtt.maximum_clock_skew_ms",
        1000,
        30_000,
    )?;

    if mqtt.host.is_empty()
        || mqtt.client_id.is_empty()
        || mqtt.command_topic.is_empty()
        || mqtt.response_topic.is_empty()
        || mqtt.ca_file.as_os_str().is_empty()
        || mqtt.client_certificate_file.as_os_str().is_empty()
        || mqtt.client_key_file.as_os_str().is_empty()
    {
        return Err(ConfigError::Invalid(
            "mqtt host/topics/client ID and TLS certificate paths are required".to_string(),
        ));
    }
    let topics = {
        let mut topics = vec![mqtt.command_topic.clone(), mqtt.response_topic.clone()];
        if !mqtt.event_topic.is_empty() {
            topics.push(mqtt.event_topic.clone());
        }
        topics
    };
    let unique = topics.iter().collect::<std::collections::HashSet<_>>();
    if topics.iter().any(|topic| !valid_mqtt_topic(topic)) {
        return Err(ConfigError::Invalid(
            "mqtt topics must be non-empty exact topic names, without NUL or wildcards, at most 65535 bytes".to_string(),
        ));
    }
    if unique.len() != topics.len() {
        return Err(ConfigError::Invalid(
            "mqtt command/response/event topics must be distinct".to_string(),
        ));
    }
    for (label, file) in [
        ("ca_file", &mqtt.ca_file),
        ("client_certificate_file", &mqtt.client_certificate_file),
        ("client_key_file", &mqtt.client_key_file),
    ] {
        if !file.is_file() {
            return Err(ConfigError::Invalid(format!(
                "MQTT {label} does not exist: {}",
                file.display()
            )));
        }
    }

    check_range(config.ros2.qos_depth, "ros2.qos_depth", 1, 256)?;
    if !valid_ros_name(&config.ros2.node_name) || !valid_topic_prefix(&config.ros2.topic_prefix) {
        return Err(ConfigError::Invalid(
            "ros2 node_name or relative topic_prefix is invalid".to_string(),
        ));
    }

    check_range(
        config.response_timeout_ms,
        "response_timeout_ms",
        1000,
        600_000,
    )?;
    check_range(config.action_timeout_ms, "action_timeout_ms", 1000, 86_400_000)?;

    for route in &config.routes {
        if route.command.is_empty() {
            return Err(ConfigError::Invalid(
                "route entries require a non-empty command".to_string(),
            ));
        }
        if !valid_topic_prefix(&route.topic_suffix) {
            return Err(ConfigError::Invalid(format!(
                "route {} has an invalid relative topic_suffix: {:?}",
                route.command, route.topic_suffix
            )));
        }
    }

    Ok(config)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn route_profile_is_explicit_and_backwards_compatible() {
        let legacy: AdapterConfig = serde_json::from_str("{}").unwrap();
        assert_eq!(legacy.route_profile, RouteProfile::LegacyDrone);
        let custom: AdapterConfig =
            serde_json::from_str(r#"{"route_profile":"custom"}"#).unwrap();
        assert_eq!(custom.route_profile, RouteProfile::Custom);
        assert!(serde_json::from_str::<AdapterConfig>(r#"{"route_profile":"typo"}"#).is_err());
    }

    #[test]
    fn mqtt_topic_validation() {
        assert!(valid_mqtt_topic("drones/unit-1/commands"));
        assert!(valid_mqtt_topic(&"x".repeat(u16::MAX as usize)));
        for topic in ["", "drones/+", "drones/#", "bad\0topic"] {
            assert!(!valid_mqtt_topic(topic));
        }
        assert!(!valid_mqtt_topic(&"x".repeat(u16::MAX as usize + 1)));
    }

    #[test]
    fn ros_name_validation() {
        assert!(valid_ros_name("m3td_gateway"));
        assert!(valid_ros_name("_private"));
        assert!(!valid_ros_name("9abc"));
        assert!(!valid_ros_name(""));
        assert!(!valid_ros_name("with space"));
        assert!(!valid_ros_name("with-dash"));
    }

    #[test]
    fn topic_prefix_validation() {
        assert!(valid_topic_prefix("m3td"));
        assert!(valid_topic_prefix("a/b_c/d"));
        assert!(!valid_topic_prefix(""));
        assert!(!valid_topic_prefix("/leading"));
        assert!(!valid_topic_prefix("trailing/"));
        assert!(!valid_topic_prefix("double//slash"));
        assert!(!valid_topic_prefix("bad-segment"));
    }
}
