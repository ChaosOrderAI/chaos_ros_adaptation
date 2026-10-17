use std::collections::HashMap;
use std::time::Duration;

use serde::Deserialize;

use super::{valid_mqtt_topic, valid_ros_name, AdapterConfig};

#[derive(Debug, Clone, Copy, Default, Deserialize, PartialEq, Eq)]
pub enum Access {
    #[default]
    #[serde(rename = "system")]
    System,
    #[serde(rename = "wifi")]
    Wifi,
    #[serde(rename = "4g")]
    Cellular4g,
    #[serde(rename = "5g")]
    Cellular5g,
}

#[derive(Debug, Clone, Deserialize)]
#[serde(default, deny_unknown_fields)]
pub struct NetworkConfig {
    pub access: Access,
    pub interface: Option<String>,
    pub connection_timeout_seconds: u64,
    pub tcp_nodelay: bool,
    pub reconnect_initial_ms: u64,
    pub reconnect_max_ms: u64,
}

impl Default for NetworkConfig {
    fn default() -> Self {
        Self {
            access: Access::System,
            interface: None,
            connection_timeout_seconds: 10,
            tcp_nodelay: true,
            reconnect_initial_ms: 1000,
            reconnect_max_ms: 30_000,
        }
    }
}

impl NetworkConfig {
    pub fn options(&self) -> rumqttc::NetworkOptions {
        let mut options = rumqttc::NetworkOptions::new();
        options.set_tcp_nodelay(self.tcp_nodelay);
        options.set_connection_timeout(self.connection_timeout_seconds);
        if let Some(interface) = &self.interface {
            options.set_bind_device(interface);
        }
        options
    }

    fn validate(&self) -> Result<(), String> {
        if !(1..=300).contains(&self.connection_timeout_seconds)
            || !(100..=300_000).contains(&self.reconnect_initial_ms)
            || !(self.reconnect_initial_ms..=300_000).contains(&self.reconnect_max_ms)
        {
            return Err("invalid network timeout or reconnect interval".into());
        }
        match (self.access, self.interface.as_deref()) {
            (Access::System, None) => Ok(()),
            (Access::System, Some(_)) => Err(
                "network.access=system must omit interface; select wifi, 4g or 5g to bind a device"
                    .into(),
            ),
            (_, Some(name))
                if !name.is_empty()
                    && name.len() < 16
                    && name
                        .bytes()
                        .all(|c| c.is_ascii_alphanumeric() || b"_.-".contains(&c)) =>
            {
                Ok(())
            }
            _ => {
                Err("wifi/4g/5g requires a valid Linux network interface name (1..15 bytes)".into())
            }
        }
    }
}

#[derive(Debug, Clone, Copy, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub enum Direction {
    MqttToRos2,
    Ros2ToMqtt,
}

#[derive(Debug, Clone, Copy, Default, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub enum Delivery {
    #[default]
    Queued,
    Latest,
    /// std_msgs/String JSON events: coalesce only explicit terminal=false.
    /// Missing/malformed metadata and terminal=true always remain queued.
    LatestNonterminal,
}

#[derive(Debug, Clone, Deserialize)]
#[serde(default, deny_unknown_fields)]
pub struct MqttDelivery {
    pub qos: u8,
    pub retain: bool,
}

impl Default for MqttDelivery {
    fn default() -> Self {
        Self {
            qos: 1,
            retain: false,
        }
    }
}

impl MqttDelivery {
    pub fn qos(&self) -> rumqttc::QoS {
        match self.qos {
            0 => rumqttc::QoS::AtMostOnce,
            _ => rumqttc::QoS::AtLeastOnce,
        }
    }
}

#[derive(Debug, Clone, Copy, Default, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum Reliability {
    #[default]
    Reliable,
    BestEffort,
}

#[derive(Debug, Clone, Copy, Default, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum Durability {
    #[default]
    Volatile,
    TransientLocal,
}

#[derive(Debug, Clone, Deserialize)]
#[serde(default, deny_unknown_fields)]
pub struct RosDelivery {
    pub depth: usize,
    pub reliability: Reliability,
    pub durability: Durability,
    pub deadline_ms: Option<u64>,
    pub lifespan_ms: Option<u64>,
}

impl Default for RosDelivery {
    fn default() -> Self {
        Self {
            depth: 32,
            reliability: Reliability::Reliable,
            durability: Durability::Volatile,
            deadline_ms: None,
            lifespan_ms: None,
        }
    }
}

impl RosDelivery {
    pub fn profile(&self) -> r2r::QosProfile {
        let mut qos = r2r::QosProfile::default().keep_last(self.depth);
        qos = match self.reliability {
            Reliability::Reliable => qos.reliable(),
            Reliability::BestEffort => qos.best_effort(),
        };
        qos = match self.durability {
            Durability::Volatile => qos.volatile(),
            Durability::TransientLocal => qos.transient_local(),
        };
        if let Some(ms) = self.deadline_ms {
            qos = qos.deadline(Duration::from_millis(ms));
        }
        if let Some(ms) = self.lifespan_ms {
            qos = qos.lifespan(Duration::from_millis(ms));
        }
        qos
    }
}

#[derive(Debug, Clone, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct TopicMapping {
    pub name: String,
    pub direction: Direction,
    pub mqtt_topic: String,
    pub ros_topic: String,
    pub msg_type: String,
    #[serde(default)]
    pub delivery: Delivery,
    #[serde(default)]
    pub mqtt: MqttDelivery,
    #[serde(default)]
    pub ros: RosDelivery,
    #[serde(default = "default_payload_limit")]
    pub max_payload_bytes: usize,
}

fn default_payload_limit() -> usize {
    crate::envelope::MAX_PAYLOAD_BYTES
}

pub fn validate(config: &AdapterConfig) -> Result<(), String> {
    config.network.validate()?;
    if config.topic_mappings.len() > 128 {
        return Err("at most 128 topic_mappings are supported".into());
    }
    let mut names = std::collections::HashSet::new();
    let mut mqtt_topics = std::collections::HashSet::new();
    let mut ros_topics: HashMap<&str, (&str, Direction)> = HashMap::new();
    let legacy = crate::routes::configured_routes(config.route_profile, &config.routes)?;
    let mut types: HashMap<String, String> = legacy
        .iter()
        .map(|route| {
            (
                format!("/{}/{}", config.ros2.topic_prefix, route.topic_suffix),
                route.msg_type.name().replace("std_msgs/", "std_msgs/msg/"),
            )
        })
        .collect();
    types.insert(
        format!("/{}/command/result", config.ros2.topic_prefix),
        "std_msgs/msg/String".into(),
    );

    for mapping in &config.topic_mappings {
        let fail = |detail: &str| format!("topic mapping {:?}: {detail}", mapping.name);
        if mapping.delivery != Delivery::Queued && mapping.direction != Direction::Ros2ToMqtt {
            return Err(fail("latest delivery applies only to ros2_to_mqtt mappings"));
        }
        if mapping.delivery == Delivery::LatestNonterminal && mapping.msg_type != "std_msgs/msg/String" {
            return Err(fail("latest_nonterminal requires std_msgs/msg/String JSON events"));
        }
        if mapping.name.is_empty() || !names.insert(&mapping.name) {
            return Err(fail("name must be non-empty and unique"));
        }
        if !valid_mqtt_topic(&mapping.mqtt_topic)
            || !mqtt_topics.insert(&mapping.mqtt_topic)
            || [
                &config.mqtt.command_topic,
                &config.mqtt.response_topic,
                &config.mqtt.event_topic,
            ]
            .contains(&&mapping.mqtt_topic)
        {
            return Err(fail("MQTT topic must be a unique exact name, separate from command/response/event topics"));
        }
        if !mapping.ros_topic.starts_with('/')
            || mapping.ros_topic.len() > 240
            || !mapping.ros_topic[1..].split('/').all(valid_ros_name)
        {
            return Err(fail("ros_topic must be an absolute ROS topic with valid name segments, at most 240 bytes"));
        }
        let parts: Vec<_> = mapping.msg_type.split('/').collect();
        if parts.len() != 3
            || parts[1] != "msg"
            || !valid_ros_name(parts[0])
            || !valid_ros_name(parts[2])
        {
            return Err(fail("msg_type must use package/msg/Type syntax"));
        }
        if let Some(old) = types.insert(mapping.ros_topic.clone(), mapping.msg_type.clone()) {
            if old != mapping.msg_type {
                return Err(fail("ROS topic has conflicting message types"));
            }
        }
        if let Some((_, direction)) =
            ros_topics.insert(&mapping.ros_topic, (&mapping.msg_type, mapping.direction))
        {
            if direction != mapping.direction {
                return Err(fail(
                    "opposite directions on the same ROS topic would create an echo path",
                ));
            }
        }
        if mapping.mqtt.qos > 1 {
            return Err(fail("MQTT qos must be 0 or 1"));
        }
        if mapping.direction == Direction::MqttToRos2 && mapping.mqtt.retain {
            return Err(fail("mqtt.retain applies only to ros2_to_mqtt publishing"));
        }
        if !(1..=1024).contains(&mapping.ros.depth)
            || [mapping.ros.deadline_ms, mapping.ros.lifespan_ms]
                .into_iter()
                .flatten()
                .any(|ms| !(1..=86_400_000).contains(&ms))
            || !(1..=1024 * 1024).contains(&mapping.max_payload_bytes)
        {
            return Err(fail(
                "invalid ROS QoS depth/duration or max_payload_bytes (1..1048576)",
            ));
        }
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::{json, Value};

    fn base() -> Value {
        json!({"topic_mappings":[{
            "name":"input", "direction":"mqtt_to_ros2", "mqtt_topic":"data/input",
            "ros_topic":"/sensor/input", "msg_type":"std_msgs/msg/Float64"
        }]})
    }

    #[test]
    fn old_configs_use_system_network_and_no_mappings() {
        let config: AdapterConfig = serde_json::from_value(json!({})).unwrap();
        assert_eq!(config.network.access, Access::System);
        assert!(config.network.interface.is_none());
        assert!(config.topic_mappings.is_empty());
        validate(&config).unwrap();
    }

    #[test]
    fn example_configs_and_mapping_defaults_are_valid() {
        for text in [
            include_str!("../../config.example.json"),
            include_str!("../../config.wifi.example.json"),
            include_str!("../../config.4g.example.json"),
            include_str!("../../config.5g.example.json"),
        ] {
            let config: AdapterConfig = serde_json::from_str(text).unwrap();
            validate(&config).unwrap();
            assert_eq!(config.topic_mappings.len(), 2);
        }
        let config: AdapterConfig = serde_json::from_value(base()).unwrap();
        validate(&config).unwrap();
        assert_eq!(config.topic_mappings[0].mqtt.qos, 1);
        assert_eq!(config.topic_mappings[0].max_payload_bytes, 16 * 1024);
        assert_eq!(config.topic_mappings[0].delivery, Delivery::Queued);
    }

    #[test]
    fn latest_delivery_requires_outgoing_mapping_and_nonterminal_requires_string() {
        let mut value = base();
        value["topic_mappings"][0]["delivery"] = json!("latest");
        let config: AdapterConfig = serde_json::from_value(value.clone()).unwrap();
        assert!(validate(&config).is_err());
        value["topic_mappings"][0]["direction"] = json!("ros2_to_mqtt");
        let config: AdapterConfig = serde_json::from_value(value.clone()).unwrap();
        validate(&config).unwrap();
        value["topic_mappings"][0]["delivery"] = json!("latest_nonterminal");
        let config: AdapterConfig = serde_json::from_value(value.clone()).unwrap();
        assert!(validate(&config).is_err());
        value["topic_mappings"][0]["msg_type"] = json!("std_msgs/msg/String");
        let config: AdapterConfig = serde_json::from_value(value).unwrap();
        validate(&config).unwrap();
    }

    #[test]
    fn invalid_network_selection_and_timers_are_rejected() {
        for network in [
            json!({"access":"wifi"}),
            json!({"access":"4g","interface":"../wwan0"}),
            json!({"interface":"wlan0"}),
            json!({"connection_timeout_seconds":0}),
            json!({"reconnect_initial_ms":2000,"reconnect_max_ms":1000}),
        ] {
            let config: AdapterConfig = serde_json::from_value(json!({"network":network})).unwrap();
            assert!(validate(&config).is_err());
        }
        let config: AdapterConfig = serde_json::from_value(json!({"network":{
            "access":"5g","interface":"wwan0","connection_timeout_seconds":25
        }}))
        .unwrap();
        validate(&config).unwrap();
        assert_eq!(config.network.options().connection_timeout(), 25);
    }

    #[test]
    fn malformed_mapping_fields_are_rejected() {
        for (key, value) in [
            ("mqtt_topic", json!("data/#")),
            ("ros_topic", json!("/2bad")),
            ("ros_topic", json!("relative")),
            ("msg_type", json!("std_msgs/Float64")),
            ("mqtt", json!({"qos":2})),
            ("mqtt", json!({"retain":true})),
            ("ros", json!({"depth":0})),
            ("ros", json!({"deadline_ms":0})),
            ("max_payload_bytes", json!(1048577)),
        ] {
            let mut value_config = base();
            value_config["topic_mappings"][0][key] = value;
            let config: AdapterConfig = serde_json::from_value(value_config).unwrap();
            assert!(validate(&config).is_err(), "accepted invalid {key}");
        }
        let mut typo = base();
        typo["topic_mappings"][0]["ros"] = json!({"reliablity":"reliable"});
        assert!(serde_json::from_value::<AdapterConfig>(typo).is_err());
    }

    #[test]
    fn conflicts_and_echo_paths_are_rejected() {
        let mut duplicate = base();
        let mut second = duplicate["topic_mappings"][0].clone();
        second["name"] = json!("output");
        duplicate["topic_mappings"]
            .as_array_mut()
            .unwrap()
            .push(second.clone());
        assert!(validate(&serde_json::from_value(duplicate).unwrap()).is_err());
        second["mqtt_topic"] = json!("data/output");
        second["direction"] = json!("ros2_to_mqtt");
        let mut echo = base();
        echo["topic_mappings"].as_array_mut().unwrap().push(second);
        assert!(validate(&serde_json::from_value(echo).unwrap()).is_err());
        let mut legacy_conflict = base();
        legacy_conflict["topic_mappings"][0]["ros_topic"] = json!("/m3td/flightControl/takeoff");
        assert!(validate(&serde_json::from_value(legacy_conflict).unwrap()).is_err());
    }

    #[test]
    fn ros_qos_parameters_are_applied() {
        let ros: RosDelivery =
            serde_json::from_value(json!({"depth":7,"reliability":"best_effort",
            "durability":"transient_local","deadline_ms":200,"lifespan_ms":500}))
            .unwrap();
        let profile = ros.profile();
        assert_eq!(profile.depth, 7);
        assert_eq!(profile.reliability, r2r::qos::ReliabilityPolicy::BestEffort);
        assert_eq!(
            profile.durability,
            r2r::qos::DurabilityPolicy::TransientLocal
        );
        assert_eq!(profile.deadline, Duration::from_millis(200));
        assert_eq!(profile.lifespan, Duration::from_millis(500));
    }
}
