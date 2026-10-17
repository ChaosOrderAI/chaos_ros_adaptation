//! MQTT wire envelopes, ported from `orin_gateway/src/mqtt_transport.cpp`.
//!
//! Command envelope (broker -> device):
//! `{ "version": 1, "message_id": "...", "timestamp_ms": 123, "command": {...} }`
//!
//! Response envelope (device -> broker):
//! `{ "version": 1, "message_id": "...", "timestamp_ms": 123, "response": {...} }`
//!
//! Event envelope (device -> broker):
//! `{ "version": 1, "event_id": "<client>-<request>", "request_id": "...",
//!   "timestamp_ms": 123, "event": {...} }`

use serde_json::{json, Value};

pub const ENVELOPE_VERSION: u64 = 1;
pub const MAX_PAYLOAD_BYTES: usize = 16 * 1024;

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct MqttCommand {
    pub message_id: String,
    pub timestamp_ms: u64,
    pub command: Value,
}

#[derive(Debug, thiserror::Error, PartialEq, Eq)]
pub enum EnvelopeError {
    #[error("payload is empty")]
    Empty,
    #[error("payload exceeds {MAX_PAYLOAD_BYTES} bytes")]
    TooLarge,
    #[error("invalid JSON")]
    InvalidJson,
    #[error("unsupported envelope version")]
    UnsupportedVersion,
    #[error("command must be a JSON object")]
    MissingCommand,
    #[error("invalid message_id")]
    InvalidMessageId,
    #[error("timestamp_ms is missing")]
    MissingTimestamp,
    #[error("clock skew exceeds limit")]
    ClockSkew,
}

pub fn unix_milliseconds() -> u64 {
    std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map(|duration| duration.as_millis() as u64)
        .unwrap_or(0)
}

pub fn valid_message_id(message_id: &str) -> bool {
    message_id.len() >= 8
        && message_id.len() <= 64
        && message_id
            .bytes()
            .all(|b| b.is_ascii_alphanumeric() || b == b'-' || b == b'_')
}

/// Parse and structurally validate an incoming MQTT command payload.
///
/// Freshness is checked separately by [`check_clock_skew`] so that a late
/// broker re-delivery of an already handled command can still be answered
/// from the replay cache.
pub fn parse_command(payload: &[u8]) -> Result<MqttCommand, EnvelopeError> {
    if payload.is_empty() {
        return Err(EnvelopeError::Empty);
    }
    if payload.len() > MAX_PAYLOAD_BYTES {
        return Err(EnvelopeError::TooLarge);
    }
    let envelope: Value =
        serde_json::from_slice(payload).map_err(|_| EnvelopeError::InvalidJson)?;
    if envelope.get("version").and_then(Value::as_u64) != Some(ENVELOPE_VERSION) {
        return Err(EnvelopeError::UnsupportedVersion);
    }
    let command = match envelope.get("command") {
        Some(command @ Value::Object(_)) => command.clone(),
        _ => return Err(EnvelopeError::MissingCommand),
    };
    let message_id = envelope
        .get("message_id")
        .and_then(Value::as_str)
        .unwrap_or_default()
        .to_string();
    if !valid_message_id(&message_id) {
        return Err(EnvelopeError::InvalidMessageId);
    }
    let timestamp_ms = envelope
        .get("timestamp_ms")
        .and_then(Value::as_u64)
        .unwrap_or(0);
    if timestamp_ms == 0 {
        return Err(EnvelopeError::MissingTimestamp);
    }
    Ok(MqttCommand {
        message_id,
        timestamp_ms,
        command,
    })
}

pub fn check_clock_skew(
    timestamp_ms: u64,
    maximum_clock_skew_ms: u64,
    now_ms: u64,
) -> Result<(), EnvelopeError> {
    if now_ms.abs_diff(timestamp_ms) > maximum_clock_skew_ms {
        return Err(EnvelopeError::ClockSkew);
    }
    Ok(())
}

/// Build the JSON payload for `response_topic`.
pub fn build_response(message_id: &str, response: &Value) -> String {
    json!({
        "version": ENVELOPE_VERSION,
        "message_id": message_id,
        "timestamp_ms": unix_milliseconds(),
        "response": response,
    })
    .to_string()
}

/// Build the JSON payload for `event_topic`.
///
/// `event_id` embeds the client ID so it stays stable when the application
/// retries an event after a temporary disconnect (same rationale as the C++
/// gateway).
pub fn build_event(client_id: &str, request_id: &str, event: &Value) -> String {
    json!({
        "version": ENVELOPE_VERSION,
        "event_id": format!("{client_id}-{request_id}"),
        "request_id": request_id,
        "timestamp_ms": unix_milliseconds(),
        "event": event,
    })
    .to_string()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn valid_command_parses() {
        let payload = br#"{"version":1,"message_id":"req-1234","timestamp_ms":1000,"command":{"command":"status"}}"#;
        let command = parse_command(payload).expect("valid command");
        assert_eq!(command.message_id, "req-1234");
        assert_eq!(command.timestamp_ms, 1000);
        assert_eq!(command.command["command"], "status");
    }

    #[test]
    fn rejects_replay_of_wrong_version() {
        let payload = br#"{"version":2,"message_id":"req-1234","timestamp_ms":1000,"command":{}}"#;
        assert_eq!(parse_command(payload), Err(EnvelopeError::UnsupportedVersion));
    }

    #[test]
    fn rejects_bad_message_id() {
        for id in ["short", "", &"x".repeat(65), "bad id!", "slash/it"] {
            let payload = format!(
                r#"{{"version":1,"message_id":"{id}","timestamp_ms":1000,"command":{{}}}}"#
            );
            assert_eq!(
                parse_command(payload.as_bytes()),
                Err(EnvelopeError::InvalidMessageId),
                "id = {id:?}"
            );
        }
        // Boundary values are accepted.
        for len in [8, 64] {
            let id = "a".repeat(len);
            let payload = format!(
                r#"{{"version":1,"message_id":"{id}","timestamp_ms":1000,"command":{{}}}}"#
            );
            assert!(parse_command(payload.as_bytes()).is_ok());
        }
    }

    #[test]
    fn rejects_clock_skew() {
        assert_eq!(check_clock_skew(1000, 5000, 10_000), Err(EnvelopeError::ClockSkew));
        assert_eq!(check_clock_skew(10_000, 5000, 1000), Err(EnvelopeError::ClockSkew));
        assert!(check_clock_skew(1000, 5000, 1000 + 5000).is_ok());
    }

    #[test]
    fn rejects_oversized_and_empty() {
        assert_eq!(parse_command(&[]), Err(EnvelopeError::Empty));
        let huge = vec![b' '; MAX_PAYLOAD_BYTES + 1];
        assert_eq!(parse_command(&huge), Err(EnvelopeError::TooLarge));
    }

    #[test]
    fn response_envelope_shape() {
        let response = build_response("req-1234", &json!({"ok": true}));
        let parsed: Value = serde_json::from_str(&response).unwrap();
        assert_eq!(parsed["version"], 1);
        assert_eq!(parsed["message_id"], "req-1234");
        assert!(parsed["timestamp_ms"].as_u64().unwrap() > 0);
        assert_eq!(parsed["response"]["ok"], true);
        assert!(parsed.get("event").is_none());
    }

    #[test]
    fn event_envelope_shape() {
        let event = build_event("client-1", "req-9", &json!({"type": "takeoff"}));
        let parsed: Value = serde_json::from_str(&event).unwrap();
        assert_eq!(parsed["event_id"], "client-1-req-9");
        assert_eq!(parsed["request_id"], "req-9");
        assert_eq!(parsed["event"]["type"], "takeoff");
    }
}
