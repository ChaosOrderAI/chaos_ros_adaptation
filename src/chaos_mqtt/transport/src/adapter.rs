//! Core MQTT <-> ROS 2 bridge logic.
//!
//! MQTT -> ROS 2: a validated command envelope is mapped through the route
//! table and published either as the JSON bridge envelope on a
//! `std_msgs/String` topic (C++-compatible) or as a scalar message on a
//! typed route.
//!
//! ROS 2 -> MQTT: results published by ROS 2 nodes on
//! `${prefix}/command/result` (std_msgs/String) that carry
//! `"source": "mqtt"` and a known `request_id` are turned into MQTT response
//! envelopes. Pending requests expire after `response_timeout_ms` with an
//! error response, because — unlike the C++ gateway — the actual command
//! execution lives in external ROS 2 nodes which may never answer.

use std::collections::HashMap;
use std::time::Duration;

use serde_json::Value;
use tokio::sync::{mpsc, watch};
use tokio::time::Instant;
use tracing::{debug, info, warn};

use crate::config::AdapterConfig;
use crate::envelope::{
    build_event, build_response, check_clock_skew, parse_command, unix_milliseconds, MqttCommand,
};
use crate::mqtt::{MqttHandle, PublishRequest};
use crate::replay::ReplayCache;
use crate::ros2::CommandPublisher;
use crate::routes::{bridge_envelope, MsgType, RouteTable};

const MAX_PENDING: usize = 1024;

struct PendingEntry {
    command_name: String,
    inserted: Instant,
    accepted_response: Option<String>,
}

#[derive(Debug, Clone, PartialEq)]
pub struct ResultMessage {
    pub source: String,
    pub request_id: String,
    pub response: Value,
}

/// Parse a `${prefix}/command/result` message. Returns `None` when the
/// payload is not a well-formed result message.
pub fn parse_result_message(data: &str) -> Option<ResultMessage> {
    if data.is_empty() || data.len() > crate::envelope::MAX_PAYLOAD_BYTES {
        return None;
    }
    let value: Value = serde_json::from_str(data).ok()?;
    let object = value.as_object()?;
    let source = object.get("source")?.as_str()?.to_string();
    let request_id = object.get("request_id")?.as_str()?.to_string();
    let response = object.get("response")?.clone();
    Some(ResultMessage {
        source,
        request_id,
        response,
    })
}

pub struct Adapter<P> {
    client_id: String,
    response_topic: String,
    event_topic: String,
    maximum_clock_skew_ms: u64,
    routes: RouteTable,
    publishers: HashMap<String, P>,
    pending: HashMap<String, PendingEntry>,
    replay: ReplayCache,
    mqtt: MqttHandle,
    response_timeout: Duration,
    action_timeout: Duration,
}

impl<P: CommandPublisher> Adapter<P> {
    pub fn new(
        config: &AdapterConfig,
        routes: RouteTable,
        publishers: HashMap<String, P>,
        mqtt: MqttHandle,
    ) -> Self {
        Self {
            client_id: config.mqtt.client_id.clone(),
            response_topic: config.mqtt.response_topic.clone(),
            event_topic: config.mqtt.event_topic.clone(),
            maximum_clock_skew_ms: config.mqtt.maximum_clock_skew_ms,
            routes,
            publishers,
            pending: HashMap::new(),
            replay: ReplayCache::new(),
            mqtt,
            response_timeout: Duration::from_millis(config.response_timeout_ms),
            action_timeout: Duration::from_millis(config.action_timeout_ms),
        }
    }

    pub async fn run(
        mut self,
        mut command_rx: mpsc::Receiver<Vec<u8>>,
        mut result_rx: mpsc::Receiver<String>,
        mut shutdown: watch::Receiver<bool>,
    ) {
        info!("MQTT <-> ROS 2 adapter is running");
        tokio::select! {
            biased;
            _ = shutdown.changed() => info!("shutdown requested"),
            _ = self.serve(&mut command_rx, &mut result_rx) => {}
        }
        // Stop admitting commands while queued replies are drained. A full
        // outbox must not prevent SIGINT/SIGTERM from stopping the process.
        command_rx.close();
        if tokio::time::timeout(
            Duration::from_secs(2),
            self.fail_all_pending("MQTT-to-ROS 2 adapter is shutting down"),
        )
        .await
        .is_err()
        {
            warn!("timed out queueing shutdown responses");
        }
    }

    async fn serve(
        &mut self,
        command_rx: &mut mpsc::Receiver<Vec<u8>>,
        result_rx: &mut mpsc::Receiver<String>,
    ) {
        let mut tick = tokio::time::interval(Duration::from_secs(1));
        tick.set_missed_tick_behavior(tokio::time::MissedTickBehavior::Skip);
        let mut results_open = true;
        loop {
            tokio::select! {
                maybe_payload = command_rx.recv() => {
                    let Some(payload) = maybe_payload else { break };
                    self.handle_command_payload(&payload).await;
                }
                maybe_result = result_rx.recv(), if results_open => {
                    match maybe_result {
                        Some(result) => self.handle_result(&result).await,
                        None => {
                            warn!("ROS 2 result stream closed; pending commands will time out");
                            results_open = false;
                        }
                    }
                }
                _ = tick.tick() => self.expire_pending().await,
            }
        }
    }

    async fn handle_command_payload(&mut self, payload: &[u8]) {
        let command = match parse_command(payload) {
            Ok(command) => command,
            Err(error) => {
                warn!(%error, "dropping invalid MQTT command envelope");
                return;
            }
        };
        // Pending requests can outlive the replay window. Check globally so
        // reusing an ID for a different route cannot dispatch a second action.
        if let Some(pending) = self.pending.get(&command.message_id) {
            if let Some(payload) = pending.accepted_response.clone() {
                self.send_mqtt(&self.response_topic, payload).await;
            }
            debug!(message_id = %command.message_id, "duplicate MQTT command is still pending; ignoring");
            return;
        }
        // Replay lookup runs before the freshness check so that a late QoS 1
        // re-delivery still receives the cached answer instead of silence.
        if let Some(cached) = self.replay.lookup(&command.message_id) {
            if cached.response.is_none() && cached.event.is_none() {
                debug!(message_id = %command.message_id, "duplicate MQTT command is still pending; ignoring");
                return;
            }
            debug!(message_id = %command.message_id, "duplicate MQTT command; re-publishing cached reply");
            if let Some(payload) = cached.response {
                self.send_mqtt(&self.response_topic, payload).await;
            }
            if let Some(payload) = cached.event {
                self.send_mqtt(&self.event_topic, payload).await;
            }
            return;
        }
        if let Err(error) = check_clock_skew(
            command.timestamp_ms,
            self.maximum_clock_skew_ms,
            unix_milliseconds(),
        ) {
            warn!(%error, message_id = %command.message_id, "dropping stale MQTT command envelope");
            return;
        }
        self.replay.remember(&command.message_id);
        self.forward_command(command).await;
    }

    async fn forward_command(&mut self, command: MqttCommand) {
        let command_name = command
            .command
            .get("command")
            .and_then(Value::as_str)
            .unwrap_or_default()
            .to_string();

        let Some(msg_type) = self.routes.get(&command_name).map(|route| route.msg_type) else {
            self.reply_error(&command.message_id, "command has no ROS 2 topic mapping")
                .await;
            return;
        };
        if !self.publishers.contains_key(&command_name) {
            self.reply_error(&command.message_id, "no ROS 2 publisher for command")
                .await;
            return;
        }

        match msg_type {
            MsgType::StringBridge => {
                if self.pending.len() >= MAX_PENDING {
                    self.reply_error(&command.message_id, "too many pending MQTT-to-ROS commands")
                        .await;
                    return;
                }
                self.pending.insert(
                    command.message_id.clone(),
                    PendingEntry {
                        command_name: command_name.clone(),
                        inserted: Instant::now(),
                        accepted_response: None,
                    },
                );
                let envelope =
                    bridge_envelope(&command.message_id, command.timestamp_ms, &command.command);
                if let Err(error) =
                    self.publishers[&command_name].publish_bridge(envelope.to_string())
                {
                    self.pending.remove(&command.message_id);
                    self.reply_error(&command.message_id, &error).await;
                }
            }
            _ => {
                let value = command.command.get("value").unwrap_or(&Value::Null);
                match self.publishers[&command_name].publish_typed(value) {
                    Ok(()) => self.reply_ok(&command.message_id).await,
                    Err(error) => self.reply_error(&command.message_id, &error).await,
                }
            }
        }
    }

    async fn handle_result(&mut self, data: &str) {
        let Some(result) = parse_result_message(data) else {
            warn!("dropping malformed ROS 2 result message");
            return;
        };
        if result.source != "mqtt" {
            debug!(source = %result.source, "ignoring ROS 2 result not sourced from MQTT");
            return;
        }
        let Some(pending) = self.pending.get_mut(&result.request_id) else {
            warn!(request_id = %result.request_id, "ignoring unmatched ROS 2 result");
            return;
        };
        // Only the explicit new protocol opts into a two-phase response.
        // Legacy results without `terminal` keep their one-response behavior.
        if result.response.get("terminal").and_then(Value::as_bool) == Some(false) {
            if pending.accepted_response.is_none() {
                pending.inserted = Instant::now();
            }
            pending.accepted_response = Some(build_response(&result.request_id, &result.response));
        } else {
            self.pending.remove(&result.request_id);
        }
        self.send_response(&result.request_id, result.response)
            .await;
    }

    async fn expire_pending(&mut self) {
        let expired: Vec<String> = self
            .pending
            .iter()
            .filter(|(_, entry)| entry.inserted.elapsed() >= if entry.accepted_response.is_some() {
                self.action_timeout
            } else {
                self.response_timeout
            })
            .map(|(message_id, _)| message_id.clone())
            .collect();
        for message_id in expired {
            let entry = self.pending.remove(&message_id).expect("expired entry exists");
            let command_name = entry.command_name;
            warn!(%message_id, %command_name, "pending MQTT command timed out waiting for ROS 2 result");
            if entry.accepted_response.is_some() {
                self.send_response(&message_id, serde_json::json!({
                    "ok": false, "code": "RESULT_TIMEOUT", "phase": "result", "terminal": true,
                    "execution_state": "unknown", "request_id": message_id,
                    "message": "ROS 2 terminal result timeout; execution state is unknown, not stopped"
                })).await;
            } else {
                self.reply_error(&message_id, "ROS 2 response timeout").await;
            }
        }
    }

    async fn fail_all_pending(&mut self, error: &str) {
        let message_ids: Vec<String> = self
            .pending
            .drain()
            .map(|(message_id, _)| message_id)
            .collect();
        for message_id in message_ids {
            self.reply_error(&message_id, error).await;
        }
    }

    async fn send_response(&mut self, message_id: &str, response: Value) {
        let payload = build_response(message_id, &response);
        self.replay.cache_response(message_id, payload.clone());
        self.send_mqtt(&self.response_topic, payload).await;
    }

    async fn reply_ok(&mut self, message_id: &str) {
        self.send_response(message_id, serde_json::json!({"ok": true}))
            .await;
    }

    async fn reply_error(&mut self, message_id: &str, error: &str) {
        self.send_response(
            message_id,
            serde_json::json!({"ok": false, "error": error, "request_id": message_id}),
        )
        .await;
    }

    /// Keep for symmetry with the C++ gateway's event path: ROS 2 nodes can
    /// trigger MQTT events through future extensions; the cache already
    /// supports replaying them.
    #[allow(dead_code)]
    async fn send_event(&mut self, request_id: &str, event: Value) {
        if self.event_topic.is_empty() {
            return;
        }
        let payload = build_event(&self.client_id, request_id, &event);
        self.replay.cache_event(request_id, payload.clone());
        self.send_mqtt(&self.event_topic, payload).await;
    }

    async fn send_mqtt(&self, topic: &str, payload: String) {
        if !self
            .mqtt
            .connected
            .load(std::sync::atomic::Ordering::SeqCst)
        {
            debug!(topic, "MQTT is disconnected; queueing reply for reconnect");
        }
        if let Err(error) = self
            .mqtt
            .publish(PublishRequest::response(topic.to_string(), payload))
            .await
        {
            warn!(%error, topic, "MQTT manager stopped before accepting reply");
        }
    }
}

#[cfg(test)]
mod tests {
    use std::sync::atomic::AtomicBool;
    use std::sync::{Arc, Mutex};

    use serde_json::json;

    use super::*;
    use crate::config::RouteSpec;
    use crate::routes::{apply_overrides, default_routes};

    const RESPONSE_TOPIC: &str = "test/responses";
    const EVENT_TOPIC: &str = "test/events";

    #[derive(Debug, Clone, PartialEq)]
    enum Published {
        Bridge(Value),
        Typed(Value),
    }

    #[derive(Clone, Default)]
    struct FakePublisher {
        log: Arc<Mutex<Vec<Published>>>,
        failure: Arc<Mutex<Option<String>>>,
    }

    impl FakePublisher {
        fn outcome(&self, published: Published) -> Result<(), String> {
            if let Some(error) = self.failure.lock().unwrap().clone() {
                return Err(error);
            }
            self.log.lock().unwrap().push(published);
            Ok(())
        }
    }

    impl CommandPublisher for FakePublisher {
        fn publish_bridge(&self, envelope_json: String) -> Result<(), String> {
            let envelope = serde_json::from_str(&envelope_json).expect("bridge envelope is JSON");
            self.outcome(Published::Bridge(envelope))
        }

        fn publish_typed(&self, value: &Value) -> Result<(), String> {
            self.outcome(Published::Typed(value.clone()))
        }
    }

    struct Harness {
        adapter: Adapter<FakePublisher>,
        /// Shared by every route's publisher.
        ros: FakePublisher,
        mqtt_rx: mpsc::Receiver<PublishRequest>,
    }

    impl Harness {
        fn new() -> Self {
            Self::with_routes_without_publisher(&[])
        }

        fn with_routes_without_publisher(missing: &[&str]) -> Self {
            let mut config = AdapterConfig::default();
            config.mqtt.client_id = "test-client".to_string();
            config.mqtt.response_topic = RESPONSE_TOPIC.to_string();
            config.mqtt.event_topic = EVENT_TOPIC.to_string();

            let mut routes = default_routes();
            apply_overrides(
                &mut routes,
                &[RouteSpec {
                    command: "flight.velocity".to_string(),
                    topic_suffix: "flightControl/velocity".to_string(),
                    msg_type: "std_msgs/Float64".to_string(),
                }],
            )
            .unwrap();
            let table = RouteTable::new(routes).unwrap();

            let ros = FakePublisher::default();
            let publishers = table
                .iter()
                .filter(|route| !missing.contains(&route.command.as_str()))
                .map(|route| (route.command.clone(), ros.clone()))
                .collect();

            let (publish_tx, mqtt_rx) = mpsc::channel(4096);
            let mqtt = MqttHandle::new(Arc::new(AtomicBool::new(true)), publish_tx);
            Self {
                adapter: Adapter::new(&config, table, publishers, mqtt),
                ros,
                mqtt_rx,
            }
        }

        fn published(&self) -> Vec<Published> {
            self.ros.log.lock().unwrap().clone()
        }

        fn fail_ros_publish(&self, error: &str) {
            *self.ros.failure.lock().unwrap() = Some(error.to_string());
        }

        /// Drain MQTT replies as (topic, raw payload).
        fn raw_replies(&mut self) -> Vec<(String, String)> {
            let mut replies = Vec::new();
            while let Ok(request) = self.mqtt_rx.try_recv() {
                replies.push((request.topic, request.payload));
            }
            replies
        }

        /// Drain MQTT replies, asserting they are response envelopes.
        fn responses(&mut self) -> Vec<Value> {
            self.raw_replies()
                .into_iter()
                .map(|(topic, payload)| {
                    assert_eq!(topic, RESPONSE_TOPIC);
                    serde_json::from_str(&payload).expect("response is JSON")
                })
                .collect()
        }
    }

    fn command_at(message_id: &str, timestamp_ms: u64, body: Value) -> Vec<u8> {
        json!({
            "version": 1,
            "message_id": message_id,
            "timestamp_ms": timestamp_ms,
            "command": body,
        })
        .to_string()
        .into_bytes()
    }

    fn command(message_id: &str, name: &str) -> Vec<u8> {
        command_at(message_id, unix_milliseconds(), json!({ "command": name }))
    }

    fn result(request_id: &str, response: Value) -> String {
        json!({ "source": "mqtt", "request_id": request_id, "response": response }).to_string()
    }

    fn assert_error(response: &Value, message_id: &str, error: &str) {
        assert_eq!(response["message_id"], message_id);
        assert_eq!(response["response"]["ok"], false);
        assert_eq!(response["response"]["error"], error);
        assert_eq!(response["response"]["request_id"], message_id);
    }

    #[tokio::test]
    async fn bridge_route_publishes_envelope_and_waits_for_result() {
        let mut h = Harness::new();
        let payload = command("req-takeoff-1", "flight.takeoff");
        let timestamp = serde_json::from_slice::<Value>(&payload).unwrap()["timestamp_ms"].clone();

        h.adapter.handle_command_payload(&payload).await;

        assert_eq!(
            h.published(),
            vec![Published::Bridge(json!({
                "bridge": { "source": "mqtt", "request_id": "req-takeoff-1", "timestamp_ms": timestamp },
                "payload": { "command": "flight.takeoff" },
            }))]
        );
        assert!(
            h.responses().is_empty(),
            "string routes answer only after the ROS 2 result"
        );
        assert!(h.adapter.pending.contains_key("req-takeoff-1"));
    }

    #[tokio::test]
    async fn matching_result_is_forwarded_once() {
        let mut h = Harness::new();
        h.adapter
            .handle_command_payload(&command("req-takeoff-1", "flight.takeoff"))
            .await;

        h.adapter
            .handle_result(&result(
                "req-takeoff-1",
                json!({ "ok": true, "altitude": 1.2 }),
            ))
            .await;
        let responses = h.responses();
        assert_eq!(responses.len(), 1);
        assert_eq!(responses[0]["version"], 1);
        assert_eq!(responses[0]["message_id"], "req-takeoff-1");
        assert_eq!(
            responses[0]["response"],
            json!({ "ok": true, "altitude": 1.2 })
        );
        assert!(h.adapter.pending.is_empty());

        h.adapter
            .handle_result(&result("req-takeoff-1", json!({ "ok": true })))
            .await;
        assert!(
            h.responses().is_empty(),
            "a repeated result must not produce a second response"
        );
    }

    #[tokio::test(start_paused = true)]
    async fn accepted_action_survives_response_timeout_and_replays_terminal_result() {
        let mut h = Harness::new();
        let request = command("req-takeoff-1", "flight.takeoff");
        h.adapter.handle_command_payload(&request).await;
        h.adapter.handle_result(&result("req-takeoff-1", json!({
            "ok": true, "code": "ACCEPTED", "terminal": false, "phase": "accepted",
            "goal_id": "goal-1"
        }))).await;
        assert_eq!(h.responses()[0]["response"]["phase"], "accepted");
        tokio::time::advance(Duration::from_secs(31)).await;
        h.adapter.expire_pending().await;
        assert!(h.responses().is_empty());
        // Even replay-cache churn cannot lose the active request identity.
        h.adapter.replay = ReplayCache::new();
        h.adapter.handle_command_payload(&request).await;
        assert_eq!(h.responses()[0]["response"]["phase"], "accepted");
        let terminal = json!({"ok": true, "code": "OK", "terminal": true,
                             "phase": "result", "goal_id": "goal-1"});
        h.adapter.handle_result(&result("req-takeoff-1", terminal.clone())).await;
        assert_eq!(h.responses()[0]["response"], terminal);
        assert!(h.adapter.pending.is_empty());
        h.adapter.handle_command_payload(&request).await;
        assert_eq!(h.responses()[0]["response"], terminal);
        assert_eq!(h.published().len(), 1, "retries never execute the action again");
    }

    #[tokio::test(start_paused = true)]
    async fn orphan_accepted_actions_have_bounded_unknown_state_timeout() {
        let mut h = Harness::new();
        let request = command("req-takeoff-1", "flight.takeoff");
        h.adapter.handle_command_payload(&request).await;
        h.adapter.handle_result(&result("req-takeoff-1", json!({
            "ok": true, "terminal": false, "phase": "accepted"
        }))).await;
        h.responses();
        tokio::time::advance(Duration::from_secs(359)).await;
        h.adapter.expire_pending().await;
        assert!(h.responses().is_empty());
        // Repeated ACCEPTED acknowledgments must not renew the observation timeout.
        h.adapter.handle_result(&result("req-takeoff-1", json!({
            "ok": true, "terminal": false, "phase": "accepted"
        }))).await;
        h.responses();
        tokio::time::advance(Duration::from_secs(1)).await;
        h.adapter.expire_pending().await;
        let replies = h.responses();
        assert_eq!(replies[0]["response"]["code"], "RESULT_TIMEOUT");
        assert_eq!(replies[0]["response"]["execution_state"], "unknown");
        assert!(h.adapter.pending.is_empty());
        h.adapter.handle_command_payload(&request).await;
        assert_eq!(h.responses()[0]["response"], replies[0]["response"]);
        assert_eq!(h.published().len(), 1);
    }

    #[tokio::test]
    async fn foreign_unmatched_and_malformed_results_are_ignored() {
        let mut h = Harness::new();
        h.adapter
            .handle_command_payload(&command("req-takeoff-1", "flight.takeoff"))
            .await;

        h.adapter.handle_result(
            &json!({ "source": "ground_station", "request_id": "req-takeoff-1", "response": {} }).to_string(),
        ).await;
        h.adapter
            .handle_result(&result("req-unknown-1", json!({ "ok": true })))
            .await;
        h.adapter.handle_result("not json").await;
        h.adapter
            .handle_result(r#"{"source":"mqtt","request_id":"req-takeoff-1"}"#)
            .await;

        assert!(h.responses().is_empty());
        assert!(
            h.adapter.pending.contains_key("req-takeoff-1"),
            "still waiting for the real result"
        );
    }

    #[tokio::test(start_paused = true)]
    async fn pending_command_times_out_and_late_result_is_dropped() {
        let mut h = Harness::new();
        h.adapter
            .handle_command_payload(&command("req-takeoff-1", "flight.takeoff"))
            .await;

        tokio::time::advance(Duration::from_millis(29_999)).await;
        h.adapter.expire_pending().await;
        assert!(
            h.responses().is_empty(),
            "must not expire before response_timeout_ms"
        );

        tokio::time::advance(Duration::from_millis(1)).await;
        h.adapter.expire_pending().await;
        let responses = h.responses();
        assert_eq!(responses.len(), 1);
        assert_error(&responses[0], "req-takeoff-1", "ROS 2 response timeout");
        assert!(h.adapter.pending.is_empty());

        h.adapter
            .handle_result(&result("req-takeoff-1", json!({ "ok": true })))
            .await;
        assert!(
            h.responses().is_empty(),
            "late results after a timeout are ignored"
        );
    }

    #[tokio::test]
    async fn duplicate_while_pending_is_ignored() {
        let mut h = Harness::new();
        let payload = command("req-takeoff-1", "flight.takeoff");
        h.adapter.handle_command_payload(&payload).await;
        h.adapter.handle_command_payload(&payload).await;

        assert_eq!(
            h.published().len(),
            1,
            "duplicate must not reach ROS 2 again"
        );
        assert!(h.responses().is_empty());
    }

    #[tokio::test]
    async fn duplicate_after_answer_replays_cached_response() {
        let mut h = Harness::new();
        let payload = command("req-takeoff-1", "flight.takeoff");
        h.adapter.handle_command_payload(&payload).await;
        h.adapter
            .handle_result(&result("req-takeoff-1", json!({ "ok": true })))
            .await;
        let original = h.raw_replies();
        assert_eq!(original.len(), 1);

        h.adapter.handle_command_payload(&payload).await;
        assert_eq!(
            h.raw_replies(),
            original,
            "replay re-sends the identical cached payload"
        );
        assert_eq!(h.published().len(), 1);
    }

    #[tokio::test]
    async fn stale_redelivery_of_answered_command_still_gets_cached_response() {
        let mut h = Harness::new();
        let sent_at = unix_milliseconds();
        let payload = command_at(
            "req-takeoff-1",
            sent_at,
            json!({ "command": "flight.takeoff" }),
        );
        h.adapter.handle_command_payload(&payload).await;
        h.adapter
            .handle_result(&result("req-takeoff-1", json!({ "ok": true })))
            .await;
        let original = h.raw_replies();

        // Same envelope arriving after the clock-skew window, as with a
        // broker re-delivery after reconnect.
        h.adapter.maximum_clock_skew_ms = 0;
        while unix_milliseconds() == sent_at {
            std::thread::sleep(Duration::from_millis(1));
        }
        h.adapter.handle_command_payload(&payload).await;
        assert_eq!(h.raw_replies(), original);
        assert_eq!(h.published().len(), 1);
    }

    #[tokio::test]
    async fn stale_new_command_is_dropped_and_not_remembered() {
        let mut h = Harness::new();
        let stale = unix_milliseconds() - 60_000;
        h.adapter
            .handle_command_payload(&command_at(
                "req-takeoff-1",
                stale,
                json!({ "command": "flight.takeoff" }),
            ))
            .await;
        assert!(h.published().is_empty());
        assert!(h.responses().is_empty());

        // The ID was not burned by the rejected envelope.
        h.adapter
            .handle_command_payload(&command("req-takeoff-1", "flight.takeoff"))
            .await;
        assert_eq!(h.published().len(), 1);
    }

    #[tokio::test]
    async fn invalid_envelope_is_dropped_silently() {
        let mut h = Harness::new();
        h.adapter.handle_command_payload(b"not json").await;
        h.adapter
            .handle_command_payload(
                br#"{"version":1,"message_id":"bad","timestamp_ms":1,"command":{}}"#,
            )
            .await;
        assert!(h.published().is_empty());
        assert!(h.responses().is_empty());
    }

    #[tokio::test]
    async fn unknown_command_gets_error_response() {
        let mut h = Harness::new();
        h.adapter
            .handle_command_payload(&command("req-unknown-1", "flight.barrel_roll"))
            .await;
        let responses = h.responses();
        assert_eq!(responses.len(), 1);
        assert_error(
            &responses[0],
            "req-unknown-1",
            "command has no ROS 2 topic mapping",
        );
        assert!(h.published().is_empty());
    }

    #[tokio::test]
    async fn route_without_publisher_gets_error_and_no_pending() {
        let mut h = Harness::with_routes_without_publisher(&["flight.land"]);
        h.adapter
            .handle_command_payload(&command("req-land-0001", "flight.land"))
            .await;
        let responses = h.responses();
        assert_eq!(responses.len(), 1);
        assert_error(
            &responses[0],
            "req-land-0001",
            "no ROS 2 publisher for command",
        );
        assert!(h.adapter.pending.is_empty());
    }

    #[tokio::test]
    async fn bridge_publish_failure_answers_and_clears_pending() {
        let mut h = Harness::new();
        h.fail_ros_publish("publish std_msgs/String: rcl error");
        h.adapter
            .handle_command_payload(&command("req-takeoff-1", "flight.takeoff"))
            .await;

        let responses = h.responses();
        assert_eq!(responses.len(), 1);
        assert_error(
            &responses[0],
            "req-takeoff-1",
            "publish std_msgs/String: rcl error",
        );
        assert!(h.adapter.pending.is_empty());
    }

    #[tokio::test]
    async fn typed_route_publishes_value_and_answers_immediately() {
        let mut h = Harness::new();
        h.adapter
            .handle_command_payload(&command_at(
                "req-velocity-1",
                unix_milliseconds(),
                json!({ "command": "flight.velocity", "value": 1.5 }),
            ))
            .await;

        assert_eq!(h.published(), vec![Published::Typed(json!(1.5))]);
        let responses = h.responses();
        assert_eq!(responses.len(), 1);
        assert_eq!(responses[0]["message_id"], "req-velocity-1");
        assert_eq!(responses[0]["response"], json!({ "ok": true }));
        assert!(
            h.adapter.pending.is_empty(),
            "typed routes never wait for a result"
        );
    }

    #[tokio::test]
    async fn typed_route_publish_failure_is_reported() {
        let mut h = Harness::new();
        h.fail_ros_publish("value must be a number");
        h.adapter
            .handle_command_payload(&command_at(
                "req-velocity-1",
                unix_milliseconds(),
                json!({ "command": "flight.velocity", "value": "fast" }),
            ))
            .await;
        let responses = h.responses();
        assert_eq!(responses.len(), 1);
        assert_error(&responses[0], "req-velocity-1", "value must be a number");
    }

    #[tokio::test]
    async fn pending_limit_rejects_overflow() {
        let mut h = Harness::new();
        for index in 0..MAX_PENDING {
            h.adapter
                .handle_command_payload(&command(&format!("req-{index:08}"), "flight.takeoff"))
                .await;
        }
        assert_eq!(h.adapter.pending.len(), MAX_PENDING);
        assert!(h.responses().is_empty());

        h.adapter
            .handle_command_payload(&command("req-overflow", "flight.takeoff"))
            .await;
        let responses = h.responses();
        assert_eq!(responses.len(), 1);
        assert_error(
            &responses[0],
            "req-overflow",
            "too many pending MQTT-to-ROS commands",
        );
        assert_eq!(h.published().len(), MAX_PENDING);
    }

    #[tokio::test]
    async fn duplicate_evicted_from_replay_window_keeps_original_pending() {
        let mut h = Harness::new();
        let payload = command("req-takeoff-1", "flight.takeoff");
        h.adapter.handle_command_payload(&payload).await;
        let inserted = h.adapter.pending["req-takeoff-1"].inserted;

        // Push the ID out of the replay window while it is still pending.
        h.adapter.replay = ReplayCache::new();
        h.adapter.handle_command_payload(&payload).await;

        assert_eq!(h.published().len(), 1);
        assert!(h.responses().is_empty());
        assert_eq!(
            h.adapter.pending["req-takeoff-1"].inserted, inserted,
            "timeout must not be reset"
        );
    }

    #[tokio::test]
    async fn event_is_cached_and_replayed_with_response() {
        let mut h = Harness::new();
        let payload = command("req-takeoff-1", "flight.takeoff");
        h.adapter.handle_command_payload(&payload).await;
        h.adapter
            .handle_result(&result("req-takeoff-1", json!({ "ok": true })))
            .await;
        h.adapter
            .send_event("req-takeoff-1", json!({ "type": "takeoff_complete" }))
            .await;
        let original = h.raw_replies();
        assert_eq!(original.len(), 2);
        let event: Value = serde_json::from_str(&original[1].1).unwrap();
        assert_eq!(original[1].0, EVENT_TOPIC);
        assert_eq!(event["event_id"], "test-client-req-takeoff-1");

        h.adapter.handle_command_payload(&payload).await;
        assert_eq!(h.raw_replies(), original);
    }

    #[tokio::test]
    async fn evicted_pending_id_cannot_dispatch_a_different_route() {
        let mut h = Harness::new();
        h.adapter
            .handle_command_payload(&command("req-takeoff-1", "flight.takeoff"))
            .await;
        h.adapter.replay = ReplayCache::new();
        h.adapter
            .handle_command_payload(&command_at(
                "req-takeoff-1",
                unix_milliseconds(),
                json!({"command": "flight.velocity", "value": 1.5}),
            ))
            .await;
        assert_eq!(h.published().len(), 1);
        assert!(h.responses().is_empty());
        h.adapter
            .handle_result(&result("req-takeoff-1", json!({"ok": true})))
            .await;
        let original = h.raw_replies();
        h.adapter
            .handle_command_payload(&command("req-takeoff-1", "flight.takeoff"))
            .await;
        assert_eq!(
            h.raw_replies(),
            original,
            "late completion must reenter the replay cache"
        );
    }

    #[tokio::test]
    async fn shutdown_interrupts_a_backpressured_response() {
        let mut h = Harness::new();
        let (tx, mut rx) = mpsc::channel(1);
        tx.send(PublishRequest::response(
            "test/responses".into(),
            "already full".into(),
        ))
        .await
        .unwrap();
        h.adapter.mqtt = MqttHandle::new(Arc::new(AtomicBool::new(false)), tx);
        let (command_tx, command_rx) = mpsc::channel(1);
        let (_result_tx, result_rx) = mpsc::channel(1);
        let (shutdown_tx, shutdown_rx) = watch::channel(false);
        command_tx
            .send(command("req-unknown-1", "unknown"))
            .await
            .unwrap();
        let run = h.adapter.run(command_rx, result_rx, shutdown_rx);
        tokio::pin!(run);
        assert!(
            futures::poll!(&mut run).is_pending(),
            "full output queue backpressures the adapter"
        );
        assert_eq!(
            command_tx.capacity(),
            1,
            "command was consumed before blocking on its response"
        );
        shutdown_tx.send(true).unwrap();
        tokio::time::timeout(Duration::from_secs(1), run)
            .await
            .expect("shutdown interrupts the blocked send");
        assert_eq!(rx.recv().await.unwrap().payload, "already full");
    }

    #[tokio::test]
    async fn shutdown_fails_every_pending_command() {
        let mut h = Harness::new();
        h.adapter
            .handle_command_payload(&command("req-takeoff-1", "flight.takeoff"))
            .await;
        h.adapter
            .handle_command_payload(&command("req-land-0001", "flight.land"))
            .await;

        let (_command_tx, command_rx) = mpsc::channel(1);
        let (_result_tx, result_rx) = mpsc::channel(1);
        let (shutdown_tx, shutdown_rx) = watch::channel(false);
        shutdown_tx.send(true).unwrap();

        let Harness {
            adapter,
            mut mqtt_rx,
            ..
        } = h;
        tokio::time::timeout(
            Duration::from_secs(5),
            adapter.run(command_rx, result_rx, shutdown_rx),
        )
        .await
        .expect("run returns on shutdown");

        let mut ids = Vec::new();
        while let Ok(request) = mqtt_rx.try_recv() {
            let response: Value = serde_json::from_str(&request.payload).unwrap();
            assert_eq!(
                response["response"]["error"],
                "MQTT-to-ROS 2 adapter is shutting down"
            );
            ids.push(response["message_id"].as_str().unwrap().to_string());
        }
        ids.sort();
        assert_eq!(ids, ["req-land-0001", "req-takeoff-1"]);
    }

    #[tokio::test]
    async fn run_keeps_serving_commands_after_result_stream_closes() {
        let h = Harness::new();
        let Harness {
            adapter,
            mut mqtt_rx,
            ..
        } = h;
        let (command_tx, command_rx) = mpsc::channel(4);
        let (result_tx, result_rx) = mpsc::channel::<String>(1);
        let (shutdown_tx, shutdown_rx) = watch::channel(false);
        drop(result_tx);

        let driver = async {
            // Let the loop observe the closed result channel first.
            tokio::task::yield_now().await;
            command_tx
                .send(command("req-unknown-1", "no.such.command"))
                .await
                .unwrap();
            let reply = tokio::time::timeout(Duration::from_secs(5), mqtt_rx.recv())
                .await
                .expect("command answered after result stream closed")
                .expect("reply");
            shutdown_tx.send(true).unwrap();
            reply
        };
        let ((), reply) = tokio::time::timeout(Duration::from_secs(10), async {
            tokio::join!(adapter.run(command_rx, result_rx, shutdown_rx), driver)
        })
        .await
        .expect("run exits on shutdown");

        let response: Value = serde_json::from_str(&reply.payload).unwrap();
        assert_error(
            &response,
            "req-unknown-1",
            "command has no ROS 2 topic mapping",
        );
    }

    #[tokio::test(start_paused = true)]
    async fn run_loop_expires_pending_commands_on_tick() {
        let h = Harness::new();
        let Harness {
            adapter,
            mut mqtt_rx,
            ..
        } = h;
        let (command_tx, command_rx) = mpsc::channel(4);
        let (_result_tx, result_rx) = mpsc::channel::<String>(1);
        let (shutdown_tx, shutdown_rx) = watch::channel(false);

        let driver = async {
            command_tx
                .send(command("req-takeoff-1", "flight.takeoff"))
                .await
                .unwrap();
            // Paused clock: idle time auto-advances to the next timer.
            let reply = mqtt_rx.recv().await.expect("timeout reply");
            shutdown_tx.send(true).unwrap();
            reply
        };
        let started = Instant::now();
        let ((), reply) = tokio::join!(adapter.run(command_rx, result_rx, shutdown_rx), driver);

        let response: Value = serde_json::from_str(&reply.payload).unwrap();
        assert_error(&response, "req-takeoff-1", "ROS 2 response timeout");
        let waited = started.elapsed();
        assert!(
            waited >= Duration::from_secs(30) && waited <= Duration::from_secs(32),
            "expired after {waited:?}"
        );
    }

    #[tokio::test]
    async fn result_message_parsing() {
        let message = r#"{"timestamp_ms":1,"source":"mqtt","request_id":"req-1234","command":"status","response":{"ok":true}}"#;
        let parsed = parse_result_message(message).expect("valid result");
        assert_eq!(parsed.source, "mqtt");
        assert_eq!(parsed.request_id, "req-1234");
        assert_eq!(parsed.response["ok"], true);
    }

    #[tokio::test]
    async fn result_message_rejects_garbage() {
        assert!(parse_result_message("").is_none());
        assert!(parse_result_message("not json").is_none());
        assert!(parse_result_message("[1,2,3]").is_none());
        assert!(parse_result_message(r#"{"source":"mqtt"}"#).is_none());
    }
}
