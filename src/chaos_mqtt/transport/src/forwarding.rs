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

/// A ROS sample awaiting encode, with the raw JSON value so serialization work
/// only runs on the sample that will actually be published (not on every
/// sample the subscription delivers). `is_terminal` is precomputed because the
/// queued arm needs it before encode runs.
struct PendingSample {
    value: Value,
    is_terminal: bool,
}

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
                        // A std_msgs/String downlink carries the MQTT payload
                        // verbatim in `data` (e.g. a v1 protocol envelope), not
                        // as a field-mapped object. Other types keep the
                        // field-mapped decode path.
                        let result = if mapping.msg_type == "std_msgs/msg/String" {
                            match std::str::from_utf8(&message.payload) {
                                Ok(text) => publisher
                                    .publish(serde_json::json!({"data": text}))
                                    .map_err(|error| error.to_string()),
                                Err(error) => Err(format!("payload is not UTF-8: {error}")),
                            }
                        } else {
                            decode(mapping, &message.payload)
                                .and_then(|value| publisher.publish(value).map_err(|error| error.to_string()))
                        };
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
                // One loop covers both rate-limited and unthrottled mappings:
                // `quota.tick()` is only armed when max_rate_hz is set, so the
                // unthrottled path publishes each sample as it arrives (with
                // backpressure via publish().await for queued delivery), while
                // the limited path coalesces into a `pending` slot that the
                // quota window drains.
                let period = mapping.max_rate_hz.map(|rate| {
                    // Validate() clamps to [0.001, 1000]; guard against
                    // programmatically-constructed configs slipping through.
                    std::time::Duration::from_secs_f64(1.0 / rate.max(1e-3))
                });
                let mut quota = period.map(tokio::time::interval);
                if let Some(q) = &mut quota {
                    q.set_missed_tick_behavior(tokio::time::MissedTickBehavior::Delay);
                }
                // Pending slot holds the newest ROS sample awaiting publish.
                // For rate-limited mappings this is the coalescing window's
                // single slot; for unthrottled mappings it is only populated
                // transiently inside the stream branch. A terminal event is
                // kept in a separate slot so it can never be coalesced away by
                // a later nonterminal sample.
                let mut pending: Option<PendingSample> = None;
                let mut terminal_pending: Option<PendingSample> = None;

                // Publish one pending sample; returns false when the publish
                // channel is closed so the caller can exit the task rather
                // than spin on a dead manager.
                async fn flush(
                    mqtt: &MqttHandle,
                    mapping: &TopicMapping,
                    sample: PendingSample,
                    blocking: bool,
                ) -> bool {
                    let is_terminal = sample.is_terminal;
                    let request = match encode(mapping, &sample.value) {
                        Ok(request) => request,
                        Err(error) => {
                            warn!(mapping = %mapping.name, %error, "ROS-to-MQTT topic encoding failed");
                            return true;
                        }
                    };
                    let latest = if is_terminal { false } else { latest_only(mapping, &sample.value) };
                    let result = if latest {
                        mqtt.publish_latest(request).map_err(|error| error.to_string())
                    } else if blocking {
                        mqtt.publish(request).await.map_err(|error| error.to_string())
                    } else {
                        mqtt.try_publish(request).map_err(|error| error.to_string())
                    };
                    match result {
                        Ok(()) => true,
                        Err(error) => {
                            let closed = error.contains("closed") || error.contains("stopped");
                            warn!(mapping = %mapping.name, %error, "MQTT publish rejected");
                            !closed
                        }
                    }
                }

                'outer: loop {
                    tokio::select! {
                        biased;
                        _ = shutdown.changed() => break,
                        // Tick before the stream so a continuously-ready ROS
                        // subscription cannot starve the quota window — with
                        // biased ordering the armed tick wins the race.
                        _ = async {
                            match &mut quota {
                                Some(q) => q.tick().await,
                                None => std::future::pending().await,
                            }
                        } => {
                            // Terminal events always go through the blocking
                            // publish path so congestion applies backpressure
                            // rather than silent loss; coalesced samples use
                            // the non-blocking slot.
                            if let Some(sample) = terminal_pending.take() {
                                if !flush(&mqtt, &mapping, sample, true).await { break 'outer; }
                            }
                            if let Some(sample) = pending.take() {
                                if !flush(&mqtt, &mapping, sample, false).await { break 'outer; }
                            }
                        },
                        message = stream.next() => {
                            let Some(message) = message else { break 'outer };
                            match message.map_err(|error| error.to_string()) {
                                Ok(value) => {
                                    let is_terminal = matches!(mapping.delivery, Delivery::LatestNonterminal)
                                        && !latest_only(&mapping, &value);
                                    if quota.is_none() {
                                        // Unthrottled: publish inline so the
                                        // queued path applies backpressure.
                                        let sample = PendingSample { value, is_terminal };
                                        if !flush(&mqtt, &mapping, sample, true).await { break 'outer; }
                                    } else if is_terminal {
                                        // Terminal events can never be
                                        // coalesced — park them in their own
                                        // slot so a later nonterminal sample
                                        // cannot silently drop them.
                                        terminal_pending = Some(PendingSample { value, is_terminal });
                                    } else {
                                        pending = Some(PendingSample { value, is_terminal });
                                    }
                                }
                                Err(error) => warn!(mapping = %mapping.name, %error, "ROS-to-MQTT topic forwarding failed"),
                            }
                        },
                    }
                }
                // Drain the pending slots so the newest buffered state (and
                // any terminal event) is not lost at the shutdown boundary.
                if let Some(sample) = terminal_pending.take() {
                    flush(&mqtt, &mapping, sample, false).await;
                }
                if let Some(sample) = pending.take() {
                    flush(&mqtt, &mapping, sample, false).await;
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
