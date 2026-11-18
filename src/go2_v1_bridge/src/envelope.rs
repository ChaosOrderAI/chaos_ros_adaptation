//! Platform v1 MQTT wire envelopes (dog/vehicle), the protocol in
//! MQTT_DEVICE_PROTOCOL.md. Distinct from the drone `version/message_id`
//! envelopes in chaos_mqtt/transport/src/envelope.rs — this family keys on
//! `protocol_version`/`device_id`/`request_id` and Unix-second timestamps.
//!
//! Downlink command (platform -> device):
//! `{ "protocol_version": 1, "device_id": "...", "request_id": "...",
//!    "channel": "nav_cmd", "command": "...", "params": {...},
//!    "issued_at": <sec>, "expires_at": <sec>, "expected_boot_id": "..." }`
//!
//! Uplink response (device -> platform):
//! `{ "protocol_version": 1, "device_id": "...", "request_id": "...",
//!    "command": "...", "success": true, "timestamp": <sec>, "result": {...} }`
//!
//! Uplink telemetry/event (device -> platform):
//! `{ "protocol_version": 1, "device_id": "...", "source": "...",
//!    "boot_id": "...", "seq": <n>, "timestamp": <sec>, "sampled_at": <sec>,
//!    ...kind fields... }`

use serde_json::{json, Map, Value};

pub const PROTOCOL_VERSION: u64 = 1;
/// Protocol decoder ceiling; the broker or gateway may be stricter.
pub const MAX_PAYLOAD_BYTES: usize = 256 * 1024;
/// Clock skew the platform tolerates on `issued_at` (seconds).
pub const CLOCK_SKEW_S: f64 = 5.0;
/// Max command TTL the platform allows (seconds).
pub const MAX_TTL_S: f64 = 300.0;

#[derive(Debug, Clone, PartialEq)]
pub struct V1Command {
    pub device_id: String,
    pub request_id: String,
    pub channel: String,
    pub command: String,
    pub params: Value,
    pub issued_at: f64,
    pub expires_at: f64,
    pub expected_boot_id: Option<String>,
    pub cancel_parent_id: Option<String>,
    pub cancel_root_id: Option<String>,
}

#[derive(Debug, thiserror::Error, PartialEq, Eq)]
pub enum EnvelopeError {
    #[error("payload is empty")]
    Empty,
    #[error("payload exceeds {MAX_PAYLOAD_BYTES} bytes")]
    TooLarge,
    #[error("invalid JSON")]
    InvalidJson,
    #[error("protocol_version must be 1")]
    UnsupportedVersion,
    #[error("device_id is missing or does not match this device")]
    DeviceMismatch,
    #[error("request_id is missing or invalid")]
    InvalidRequestId,
    #[error("command must be a non-empty string")]
    MissingCommand,
    #[error("issued_at/expires_at is missing or not a finite number")]
    MissingTimestamp,
    #[error("command is expired or issued too far in the future")]
    ClockSkew,
    #[error("command TTL exceeds 300 s")]
    TtlTooLong,
}

pub fn unix_seconds() -> f64 {
    std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map(|d| d.as_secs_f64())
        .unwrap_or(0.0)
}

fn finite_seconds(v: &Value) -> Option<f64> {
    v.as_f64().filter(|n| n.is_finite())
}

/// request_id: string 1..160 chars. Keep it permissive on the charset (the
/// protocol says "string", not a restricted id) but reject empty/oversize.
pub fn valid_request_id(request_id: &str) -> bool {
    let len = request_id.len();
    len >= 1 && len <= 160
}

/// Parse and structurally + temporally validate a downlink command payload.
/// `our_device_id` must match the envelope's `device_id`. `now_s` is injected
/// for testability.
pub fn parse_command(payload: &[u8], our_device_id: &str, now_s: f64) -> Result<V1Command, EnvelopeError> {
    if payload.is_empty() {
        return Err(EnvelopeError::Empty);
    }
    if payload.len() > MAX_PAYLOAD_BYTES {
        return Err(EnvelopeError::TooLarge);
    }
    let value: Value = serde_json::from_slice(payload).map_err(|_| EnvelopeError::InvalidJson)?;
    let object = value.as_object().ok_or(EnvelopeError::InvalidJson)?;

    if object.get("protocol_version").and_then(Value::as_u64) != Some(PROTOCOL_VERSION) {
        return Err(EnvelopeError::UnsupportedVersion);
    }
    let device_id = object
        .get("device_id")
        .and_then(Value::as_str)
        .ok_or(EnvelopeError::DeviceMismatch)?;
    if device_id != our_device_id {
        return Err(EnvelopeError::DeviceMismatch);
    }
    let request_id = object
        .get("request_id")
        .and_then(Value::as_str)
        .ok_or(EnvelopeError::InvalidRequestId)?;
    if !valid_request_id(request_id) {
        return Err(EnvelopeError::InvalidRequestId);
    }
    let command = object
        .get("command")
        .and_then(Value::as_str)
        .filter(|c| !c.is_empty())
        .ok_or(EnvelopeError::MissingCommand)?;
    let channel = object
        .get("channel")
        .and_then(Value::as_str)
        .unwrap_or("")
        .to_string();
    let params = object.get("params").cloned().unwrap_or_else(|| json!({}));
    let issued_at = object
        .get("issued_at")
        .and_then(finite_seconds)
        .ok_or(EnvelopeError::MissingTimestamp)?;
    let expires_at = object
        .get("expires_at")
        .and_then(finite_seconds)
        .ok_or(EnvelopeError::MissingTimestamp)?;

    if expires_at <= issued_at {
        return Err(EnvelopeError::MissingTimestamp);
    }
    if expires_at - issued_at > MAX_TTL_S {
        return Err(EnvelopeError::TtlTooLong);
    }
    if now_s > expires_at || issued_at > now_s + CLOCK_SKEW_S {
        return Err(EnvelopeError::ClockSkew);
    }

    Ok(V1Command {
        device_id: device_id.to_string(),
        request_id: request_id.to_string(),
        channel,
        command: command.to_string(),
        params,
        issued_at,
        expires_at,
        expected_boot_id: object
            .get("expected_boot_id")
            .and_then(Value::as_str)
            .map(str::to_string),
        cancel_parent_id: object
            .get("cancel_parent_id")
            .and_then(Value::as_str)
            .map(str::to_string),
        cancel_root_id: object
            .get("cancel_root_id")
            .and_then(Value::as_str)
            .map(str::to_string),
    })
}

/// Build an uplink response envelope.
pub fn build_response(
    device_id: &str,
    request_id: &str,
    command: &str,
    success: bool,
    result: Value,
) -> Value {
    json!({
        "protocol_version": PROTOCOL_VERSION,
        "device_id": device_id,
        "request_id": request_id,
        "command": command,
        "success": success,
        "timestamp": unix_seconds(),
        "result": result,
    })
}

/// Build an uplink response carrying an error code.
pub fn build_error_response(
    device_id: &str,
    request_id: &str,
    command: &str,
    error_code: &str,
    error: &str,
) -> Value {
    json!({
        "protocol_version": PROTOCOL_VERSION,
        "device_id": device_id,
        "request_id": request_id,
        "command": command,
        "success": false,
        "timestamp": unix_seconds(),
        "error_code": error_code,
        "error": error,
    })
}

/// Per-source monotonic sequence + boot identity for uplink state. A fresh
/// boot_id per process run is what lets the platform reject stale packets from
/// before a restart.
pub struct UplinkStamp {
    pub source: &'static str,
    pub boot_id: String,
    seq: u64,
}

impl UplinkStamp {
    pub fn new(source: &'static str) -> Self {
        Self {
            source,
            boot_id: uuid::Uuid::new_v4().to_string(),
            seq: 0,
        }
    }

    /// Shared envelope header for a state/event payload. Caller adds the
    /// kind-specific fields (`state`, `data`, `tracks`, ...) to the returned
    /// object before serializing.
    pub fn stamp(&mut self, device_id: &str, body: &mut Map<String, Value>) {
        self.seq += 1;
        let now = unix_seconds();
        body.insert("protocol_version".into(), json!(PROTOCOL_VERSION));
        body.insert("device_id".into(), json!(device_id));
        body.insert("source".into(), json!(self.source));
        body.insert("boot_id".into(), json!(self.boot_id));
        body.insert("seq".into(), json!(self.seq));
        body.insert("timestamp".into(), json!(now));
        body.entry("sampled_at").or_insert(json!(now));
    }
}

/// Convenience: build a full state/event object under `field` with the shared
/// header already applied. `sampled_at` is the original sample time (for
/// telemetry); `None` uses now.
pub fn build_state(
    stamp: &mut UplinkStamp,
    device_id: &str,
    field: &str,
    inner: Value,
    sampled_at: Option<f64>,
) -> Value {
    let mut object = Map::new();
    if let Some(s) = sampled_at {
        object.insert("sampled_at".into(), json!(s));
    }
    object.insert(field.to_string(), inner);
    stamp.stamp(device_id, &mut object);
    Value::Object(object)
}

#[cfg(test)]
mod tests {
    use super::*;

    const DEV: &str = "robot-dog-2";
    const NOW: f64 = 1_760_000_000.0;

    fn cmd_json(extra: &[(&str, Value)]) -> Vec<u8> {
        let mut o = Map::new();
        o.insert("protocol_version".into(), json!(1));
        o.insert("device_id".into(), json!(DEV));
        o.insert("request_id".into(), json!("req-001"));
        o.insert("channel".into(), json!("nav_cmd"));
        o.insert("command".into(), json!("waypoints"));
        o.insert("params".into(), json!({}));
        o.insert("issued_at".into(), json!(NOW));
        o.insert("expires_at".into(), json!(NOW + 30.0));
        for (k, v) in extra {
            o.insert((*k).to_string(), v.clone());
        }
        serde_json::to_vec(&Value::Object(o)).unwrap()
    }

    #[test]
    fn valid_command_parses() {
        let c = parse_command(&cmd_json(&[]), DEV, NOW + 1.0).unwrap();
        assert_eq!(c.request_id, "req-001");
        assert_eq!(c.command, "waypoints");
        assert_eq!(c.channel, "nav_cmd");
    }

    #[test]
    fn rejects_wrong_device_and_version() {
        assert_eq!(
            parse_command(&cmd_json(&[]), "robot-dog-9", NOW),
            Err(EnvelopeError::DeviceMismatch)
        );
        assert_eq!(
            parse_command(&cmd_json(&[("protocol_version", json!(2))]), DEV, NOW),
            Err(EnvelopeError::UnsupportedVersion)
        );
    }

    #[test]
    fn rejects_expired_and_future_commands() {
        assert_eq!(
            parse_command(&cmd_json(&[]), DEV, NOW + 60.0),
            Err(EnvelopeError::ClockSkew)
        );
        assert_eq!(
            parse_command(
                &cmd_json(&[("issued_at", json!(NOW + 30.0)), ("expires_at", json!(NOW + 60.0))]),
                DEV, NOW,
            ),
            Err(EnvelopeError::ClockSkew)
        );
        // expires_at <= issued_at
        assert_eq!(
            parse_command(&cmd_json(&[("expires_at", json!(NOW - 1.0))]), DEV, NOW),
            Err(EnvelopeError::MissingTimestamp)
        );
    }

    #[test]
    fn rejects_oversize_ttl_and_bad_ids() {
        assert_eq!(
            parse_command(&cmd_json(&[("expires_at", json!(NOW + 400.0))]), DEV, NOW + 1.0),
            Err(EnvelopeError::TtlTooLong)
        );
        assert_eq!(
            parse_command(&cmd_json(&[("request_id", json!(""))]), DEV, NOW),
            Err(EnvelopeError::InvalidRequestId)
        );
    }

    #[test]
    fn response_and_state_envelopes_carry_v1_fields() {
        let r = build_response(DEV, "req-1", "waypoints", true, json!({"accepted":true}));
        assert_eq!(r["protocol_version"], 1);
        assert_eq!(r["device_id"], DEV);
        assert_eq!(r["request_id"], "req-1");
        assert!(r["success"].as_bool().unwrap());

        let mut stamp = UplinkStamp::new("navigation");
        let s = build_state(&mut stamp, DEV, "state", json!({"pose":{"x":1}}), None);
        assert_eq!(s["protocol_version"], 1);
        assert_eq!(s["boot_id"], stamp.boot_id);
        assert_eq!(s["seq"], 1);
        assert_eq!(s["state"]["pose"]["x"], 1);
        let s2 = build_state(&mut stamp, DEV, "state", json!({}), None);
        assert_eq!(s2["seq"], 2); // seq increments per source
    }
}
