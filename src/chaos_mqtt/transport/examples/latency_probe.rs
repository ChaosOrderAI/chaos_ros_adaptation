//! End-to-end latency probe for a running `mqtt_ros2_adapter`.
//!
//! Plays both sides of the bridge in one process so every timestamp comes
//! from the same monotonic clock:
//!
//! - cloud side: publishes command envelopes on `mqtt.command_topic` and
//!   waits for the matching envelope on `mqtt.response_topic`;
//! - ROS 2 side: subscribes to `${prefix}/${topic_suffix}` and answers on
//!   `${prefix}/command/result` immediately.
//!
//! Usage (adapter already running with the same config):
//!   cargo run --release --example latency_probe -- [--config config.json] [-n 200]
//!       [--command flight.takeoff] [--topic-suffix flightControl/takeoff] [--interval-ms 20]

use std::collections::HashMap;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};

use futures::StreamExt;
use rumqttc::{
    AsyncClient, Event, Incoming, MqttOptions, NetworkOptions, QoS, TlsConfiguration, Transport,
};
use serde_json::{json, Value};
use tokio::sync::mpsc;

const WARMUP: usize = 10;
const REPLY_TIMEOUT: Duration = Duration::from_secs(3);

struct Args {
    config: String,
    count: usize,
    command: String,
    topic_suffix: String,
    interval: Duration,
}

fn parse_args() -> Args {
    let mut args = Args {
        config: "config.json".to_string(),
        count: 200,
        command: "flight.takeoff".to_string(),
        topic_suffix: "flightControl/takeoff".to_string(),
        interval: Duration::from_millis(20),
    };
    let mut iter = std::env::args().skip(1);
    while let Some(flag) = iter.next() {
        let value = iter.next().unwrap_or_else(|| panic!("missing value for {flag}"));
        match flag.as_str() {
            "--config" | "-c" => args.config = value,
            "-n" | "--count" => args.count = value.parse().expect("count"),
            "--command" => args.command = value,
            "--topic-suffix" => args.topic_suffix = value,
            "--interval-ms" => args.interval = Duration::from_millis(value.parse().expect("interval")),
            _ => panic!("unknown argument {flag}"),
        }
    }
    args
}

fn unix_ms() -> u64 {
    SystemTime::now().duration_since(UNIX_EPOCH).unwrap().as_millis() as u64
}

fn text(config: &Value, pointer: &str) -> String {
    config
        .pointer(pointer)
        .and_then(Value::as_str)
        .unwrap_or_else(|| panic!("config is missing {pointer}"))
        .to_string()
}

fn read(path: &str) -> Vec<u8> {
    std::fs::read(path).unwrap_or_else(|error| panic!("cannot read {path}: {error}"))
}

struct Sample {
    to_ros: Duration,
    to_mqtt: Duration,
    round_trip: Duration,
}

fn report(label: &str, mut values: Vec<Duration>) {
    values.sort();
    let pick = |q: f64| values[((values.len() - 1) as f64 * q).round() as usize];
    let ms = |d: Duration| d.as_secs_f64() * 1000.0;
    println!(
        "{label:<22} min {:>7.3}  p50 {:>7.3}  p90 {:>7.3}  p99 {:>7.3}  max {:>7.3}  (ms)",
        ms(values[0]),
        ms(pick(0.5)),
        ms(pick(0.9)),
        ms(pick(0.99)),
        ms(values[values.len() - 1]),
    );
}

#[tokio::main]
async fn main() {
    let args = parse_args();
    let config: Value = serde_json::from_slice(&read(&args.config)).expect("config JSON");
    let prefix = text(&config, "/ros2/topic_prefix");
    let command_topic = text(&config, "/mqtt/command_topic");
    let response_topic = text(&config, "/mqtt/response_topic");

    // ROS 2 side: answer every bridged command as fast as possible.
    let context = r2r::Context::create().expect("ROS 2 context");
    let mut node = r2r::Node::create(context, "mqtt_latency_probe", "").expect("ROS 2 node");
    let qos = || r2r::QosProfile::default().keep_last(32).reliable();
    let mut commands = node
        .subscribe::<r2r::std_msgs::msg::String>(&format!("{prefix}/{}", args.topic_suffix), qos())
        .expect("subscribe");
    let results = node
        .create_publisher::<r2r::std_msgs::msg::String>(&format!("{prefix}/command/result"), qos())
        .expect("publisher");
    let stop = Arc::new(AtomicBool::new(false));
    let spin = {
        let stop = stop.clone();
        std::thread::spawn(move || {
            while !stop.load(Ordering::Relaxed) {
                node.spin_once(Duration::from_millis(10));
            }
        })
    };
    let ros_seen: Arc<Mutex<HashMap<String, Instant>>> = Arc::default();
    {
        let ros_seen = ros_seen.clone();
        tokio::spawn(async move {
            while let Some(message) = commands.next().await {
                let arrived = Instant::now();
                let Ok(envelope) = serde_json::from_str::<Value>(&message.data) else { continue };
                let Some(request_id) = envelope.pointer("/bridge/request_id").and_then(Value::as_str) else {
                    continue;
                };
                let reply = json!({ "source": "mqtt", "request_id": request_id, "response": { "ok": true } });
                results
                    .publish(&r2r::std_msgs::msg::String { data: reply.to_string() })
                    .expect("publish result");
                ros_seen.lock().unwrap().insert(request_id.to_string(), arrived);
            }
        });
    }

    // Cloud side.
    let mut options = MqttOptions::new(
        format!("{}-latency-probe", text(&config, "/mqtt/client_id")),
        text(&config, "/mqtt/host"),
        config.pointer("/mqtt/port").and_then(Value::as_u64).unwrap_or(8883) as u16,
    );
    options.set_keep_alive(Duration::from_secs(30));
    options.set_transport(Transport::Tls(TlsConfiguration::Simple {
        ca: read(&text(&config, "/mqtt/ca_file")),
        alpn: None,
        client_auth: Some((
            read(&text(&config, "/mqtt/client_certificate_file")),
            read(&text(&config, "/mqtt/client_key_file")),
        )),
    }));
    let (client, mut eventloop) = AsyncClient::new(options, 64);
    let mut network = NetworkOptions::new();
    network.set_tcp_nodelay(true);
    eventloop.set_network_options(network);
    client.subscribe(&response_topic, QoS::AtLeastOnce).await.unwrap();

    let (reply_tx, mut reply_rx) = mpsc::unbounded_channel::<(String, Instant)>();
    let (ready_tx, ready_rx) = tokio::sync::oneshot::channel();
    tokio::spawn(async move {
        let mut ready_tx = Some(ready_tx);
        loop {
            match eventloop.poll().await {
                Ok(Event::Incoming(Incoming::SubAck(_))) => {
                    if let Some(tx) = ready_tx.take() {
                        let _ = tx.send(());
                    }
                }
                Ok(Event::Incoming(Incoming::Publish(publish))) => {
                    let arrived = Instant::now();
                    if let Ok(envelope) = serde_json::from_slice::<Value>(&publish.payload) {
                        if let Some(id) = envelope.get("message_id").and_then(Value::as_str) {
                            let _ = reply_tx.send((id.to_string(), arrived));
                        }
                    }
                }
                Ok(_) => {}
                Err(error) => panic!("probe MQTT connection failed: {error}"),
            }
        }
    });
    tokio::time::timeout(Duration::from_secs(5), ready_rx)
        .await
        .expect("MQTT subscription acknowledged")
        .unwrap();
    // Give DDS discovery time to match the probe with the adapter.
    tokio::time::sleep(Duration::from_secs(2)).await;

    let run = unix_ms();
    let mut samples = Vec::with_capacity(args.count);
    let mut lost = 0usize;
    for index in 0..WARMUP + args.count {
        let message_id = format!("probe-{run}-{index:06}");
        let payload = json!({
            "version": 1,
            "message_id": message_id,
            "timestamp_ms": unix_ms(),
            "command": { "command": args.command },
        })
        .to_string();
        let sent = Instant::now();
        client
            .publish(&command_topic, QoS::AtLeastOnce, false, payload)
            .await
            .unwrap();

        let answered = tokio::time::timeout(REPLY_TIMEOUT, async {
            while let Some((id, arrived)) = reply_rx.recv().await {
                if id == message_id {
                    return Some(arrived);
                }
            }
            None
        })
        .await
        .ok()
        .flatten();
        let ros = ros_seen.lock().unwrap().remove(&message_id);
        match (answered, ros) {
            (Some(answered), Some(ros)) if index >= WARMUP => samples.push(Sample {
                to_ros: ros - sent,
                to_mqtt: answered - ros,
                round_trip: answered - sent,
            }),
            (Some(_), Some(_)) => {}
            _ => {
                lost += 1;
                eprintln!("no complete round trip for {message_id}");
            }
        }
        tokio::time::sleep(args.interval).await;
    }

    stop.store(true, Ordering::Relaxed);
    let _ = spin.join();

    println!("samples: {} (lost {lost}, warm-up {WARMUP} discarded)", samples.len());
    if samples.is_empty() {
        return;
    }
    report("MQTT -> ROS 2", samples.iter().map(|s| s.to_ros).collect());
    report("ROS 2 result -> MQTT", samples.iter().map(|s| s.to_mqtt).collect());
    report("round trip", samples.iter().map(|s| s.round_trip).collect());
}
