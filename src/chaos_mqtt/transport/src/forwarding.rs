//! Configured topic forwarding uses JSON objects matching ROS message fields.
//! This path is independent of the legacy command/response envelope protocol.

use std::collections::HashMap;

use futures::{stream::BoxStream, StreamExt};
use serde_json::Value;
use tokio::sync::{mpsc, watch};
use tokio::task::JoinHandle;
use tracing::warn;

use crate::config::{Delivery, Direction, TopicMapping};
use crate::mqtt::{ForwardedMessage, MqttHandle, PublishRequest};

type RosStream = BoxStream<'static, r2r::Result<Value>>;

pub struct TopicForwarding {
    incoming: HashMap<String, (TopicMapping, r2r::PublisherUntyped)>,
    outgoing: Vec<(TopicMapping, RosStream)>,
}

fn decode(mapping: &TopicMapping, payload: &[u8]) -> Result<Value, String> {
    if payload.len() > mapping.max_payload_bytes {
        return Err("payload exceeds mapping max_payload_bytes".into());
    }
    let value: Value = serde_json::from_slice(payload).map_err(|error| error.to_string())?;
    if !value.is_object() {
        return Err("payload must be a JSON object matching the ROS message fields".into());
    }
    Ok(value)
}

fn encode(mapping: &TopicMapping, value: &Value) -> Result<PublishRequest, String> {
    let payload = serde_json::to_string(value).map_err(|error| error.to_string())?;
    if payload.len() > mapping.max_payload_bytes {
        return Err("ROS message JSON exceeds mapping max_payload_bytes".into());
    }
    Ok(PublishRequest {
        topic: mapping.mqtt_topic.clone(),
        payload,
        qos: mapping.mqtt.qos(),
        retain: mapping.mqtt.retain,
    })
}

fn latest_only(mapping: &TopicMapping, value: &Value) -> bool {
    match mapping.delivery {
        Delivery::Queued => false,
        Delivery::Latest => true,
        Delivery::LatestNonterminal => value.get("data").and_then(Value::as_str)
            .and_then(|data| serde_json::from_str::<Value>(data).ok())
            .and_then(|event| event.get("terminal").and_then(Value::as_bool)) == Some(false),
    }
}

impl TopicForwarding {
    pub fn prepare(node: &mut r2r::Node, mappings: &[TopicMapping]) -> Result<Self, String> {
        let mut incoming = HashMap::new();
        let mut outgoing = Vec::new();
        for mapping in mappings {
            let qos = mapping.ros.profile();
            let context = |error| {
                format!(
                    "mapping {:?} ({} / {}): {error}",
                    mapping.name, mapping.ros_topic, mapping.msg_type
                )
            };
            match mapping.direction {
                Direction::MqttToRos2 => {
                    let publisher = node
                        .create_publisher_untyped(&mapping.ros_topic, &mapping.msg_type, qos)
                        .map_err(context)?;
                    incoming.insert(mapping.mqtt_topic.clone(), (mapping.clone(), publisher));
                }
                Direction::Ros2ToMqtt => {
                    let stream = node
                        .subscribe_untyped(&mapping.ros_topic, &mapping.msg_type, qos)
                        .map_err(context)?;
                    outgoing.push((mapping.clone(), stream.boxed()));
                }
            }
        }
        Ok(Self { incoming, outgoing })
    }

    pub fn start(
        self,
        mqtt: MqttHandle,
        mut incoming_rx: mpsc::Receiver<ForwardedMessage>,
        shutdown: watch::Receiver<bool>,
    ) -> Vec<JoinHandle<()>> {
        let mut tasks = Vec::new();
        let mut incoming_shutdown = shutdown.clone();
        tasks.push(tokio::spawn(async move {
            loop {
                tokio::select! {
                    biased;
                    _ = incoming_shutdown.changed() => break,
                    message = incoming_rx.recv() => {
                        let Some(message) = message else { break };
                        let Some((mapping, publisher)) = self.incoming.get(&message.topic) else { continue };
                        let result = decode(mapping, &message.payload)
                            .and_then(|value| publisher.publish(value).map_err(|error| error.to_string()));
                        if let Err(error) = result {
                            warn!(mapping = %mapping.name, %error, "MQTT-to-ROS topic forwarding failed");
                        }
                    }
                }
            }
        }));
        for (mapping, mut stream) in self.outgoing {
            let mqtt = mqtt.clone();
            let mut shutdown = shutdown.clone();
            tasks.push(tokio::spawn(async move {
                tokio::select! {
                    biased;
                    _ = shutdown.changed() => {},
                    _ = async {
                        while let Some(message) = stream.next().await {
                            let request = message.map_err(|error| error.to_string()).and_then(|value| {
                                encode(&mapping, &value).map(|request| (request, latest_only(&mapping, &value)))
                            });
                            match request {
                                Ok((request, true)) => if let Err(error) = mqtt.publish_latest(request) {
                                    warn!(mapping = %mapping.name, %error, "latest MQTT publish rejected");
                                },
                                Ok((request, false)) => if mqtt.publish(request).await.is_err() { break; },
                                Err(error) => warn!(mapping = %mapping.name, %error, "ROS-to-MQTT topic forwarding failed"),
                            }
                        }
                    } => {},
                }
            }));
        }
        tasks
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    fn mapping() -> TopicMapping {
        serde_json::from_value(json!({
            "name":"temperature", "direction":"ros2_to_mqtt",
            "mqtt_topic":"telemetry/temperature", "ros_topic":"/sensors/temperature",
            "msg_type":"std_msgs/msg/Float64", "mqtt":{"qos":0,"retain":true}
        }))
        .unwrap()
    }

    #[test]
    fn json_fields_and_delivery_options_survive_round_trip() {
        let mapping = mapping();
        let value = json!({"data": 24.5});
        let request = encode(&mapping, &value).unwrap();
        assert_eq!(request.topic, mapping.mqtt_topic);
        assert_eq!(request.qos, rumqttc::QoS::AtMostOnce);
        assert!(request.retain);
        assert_eq!(decode(&mapping, request.payload.as_bytes()).unwrap(), value);
    }

    #[test]
    fn malformed_and_oversized_payloads_are_rejected() {
        let mut mapping = mapping();
        for payload in [b"not json".as_slice(), b"24.5", b"[]"] {
            assert!(decode(&mapping, payload).is_err());
        }
        mapping.max_payload_bytes = 8;
        assert!(decode(&mapping, br#"{"data":24.5}"#).is_err());
        assert!(encode(&mapping, &json!({"data":24.5})).is_err());
    }

    #[test]
    fn feedback_coalesces_but_terminal_and_legacy_events_remain_queued() {
        let mut mapping = mapping();
        mapping.delivery = Delivery::LatestNonterminal;
        assert!(latest_only(&mapping, &json!({"data": r#"{"terminal":false,"phase":"feedback"}"#})));
        for data in [r#"{"terminal":true}"#, "{}", "not-json", r#"{"terminal":"false"}"#] {
            assert!(!latest_only(&mapping, &json!({"data": data})));
        }
        mapping.delivery = Delivery::Latest;
        assert!(latest_only(&mapping, &json!({"data": 24.5})));
        mapping.delivery = Delivery::Queued;
        assert!(!latest_only(&mapping, &json!({"data": 24.5})));
    }

    #[test]
    fn configured_native_ros_type_checks_and_converts_json() {
        let native = r2r::WrappedNativeMsgUntyped::new_from("std_msgs/msg/Float64").unwrap();
        native.from_json(json!({"data":24.5})).unwrap();
        assert_eq!(native.to_json().unwrap(), json!({"data":24.5}));
        assert!(native.from_json(json!({"data":"invalid number"})).is_err());
        assert!(r2r::WrappedNativeMsgUntyped::new_from("missing_msgs/msg/Missing").is_err());
    }
}
