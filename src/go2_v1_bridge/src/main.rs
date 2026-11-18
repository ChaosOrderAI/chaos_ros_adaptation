//! go2_v1_bridge: platform v1 MQTT device-protocol bridge for the Go2 dog.
//!
//! Sits between the chaos_mqtt transport (dumb ROS2<->JSON<->MQTT passthrough)
//! and the robot's ROS 2 stack. Downlink: validates v1 command envelopes and
//! turns them into ROS primitives. Uplink: wraps robot state in v1 telemetry
//! envelopes with per-source boot_id + seq. See MQTT_DEVICE_PROTOCOL.md.

mod commands;
mod envelope;

use std::collections::HashMap;
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant};

use futures::StreamExt;
use r2r::QosProfile;
use serde_json::{json, Value};
use tokio::sync::watch;
use tracing::{info, warn};

use commands::{dispatch, to_ros_action, RosAction};
use envelope::{build_error_response, build_response, build_state, parse_command, unix_seconds, UplinkStamp};

#[derive(Debug, serde::Deserialize)]
#[serde(default)]
struct BridgeConfig {
    /// Platform device_id, e.g. "robot-dog-2". Must match the envelope.
    device_id: String,
    /// MQTT topic prefix (two literal segments), e.g. "go2/002".
    prefix: String,
    /// Odom topic carrying nav_msgs/Odometry for nav_state pose+velocity.
    odom_topic: String,
    /// JointState topic for state telemetry.
    joints_topic: String,
    /// ROS topics the bridge drives.
    cmd_vel_topic: String,
    route_topic: String,
    goal_topic: String,
    obstacles_topic: String,
    /// Internal topics: downlink commands in, uplink envelopes out.
    cmd_in_topic: String,
    nav_cmd_in_topic: String,
    mgmt_cmd_in_topic: String,
    uplink: UplinkTopics,
    /// How long a request_id is remembered for dedup (seconds).
    dedup_ttl_s: f64,
}

#[derive(Debug, serde::Deserialize)]
#[serde(default)]
struct UplinkTopics {
    resp: String,
    nav_resp: String,
    mgmt_resp: String,
    nav_event: String,
    state: String,
    nav_state: String,
    detections: String,
    status: String,
}

impl Default for UplinkTopics {
    fn default() -> Self {
        Self {
            resp: "/v1/resp".into(),
            nav_resp: "/v1/nav_resp".into(),
            mgmt_resp: "/v1/mgmt_resp".into(),
            nav_event: "/v1/nav_event".into(),
            state: "/v1/state".into(),
            nav_state: "/v1/nav_state".into(),
            detections: "/v1/detections".into(),
            status: "/v1/status".into(),
        }
    }
}

impl Default for BridgeConfig {
    fn default() -> Self {
        Self {
            device_id: "robot-dog-2".into(),
            prefix: "go2/002".into(),
            odom_topic: "/aft_mapped_to_init".into(),
            joints_topic: "/joint_states".into(),
            cmd_vel_topic: "/cmd_vel".into(),
            route_topic: "/initial_path".into(),
            goal_topic: "/goal_pose".into(),
            obstacles_topic: "/obstacles_switch".into(),
            cmd_in_topic: "/v1/cmd".into(),
            nav_cmd_in_topic: "/v1/nav_cmd".into(),
            mgmt_cmd_in_topic: "/v1/mgmt_cmd".into(),
            uplink: UplinkTopics::default(),
            dedup_ttl_s: 120.0,
        }
    }
}

/// request_id dedup ledger: remember the response we gave so a QoS-1 redelivery
/// of the same intent returns the same answer, and a conflicting intent under a
/// reused id is rejected.
struct DedupLedger {
    seen: HashMap<String, (Instant, Value)>,
    ttl: Duration,
}

impl DedupLedger {
    fn new(ttl_s: f64) -> Self {
        Self {
            seen: HashMap::new(),
            ttl: Duration::from_secs_f64(ttl_s.max(1.0)),
        }
    }
    fn prune(&mut self) {
        let now = Instant::now();
        self.seen.retain(|_, (t, _)| now.duration_since(*t) < self.ttl);
    }
    fn lookup(&mut self, id: &str) -> Option<Value> {
        self.seen.get(id).map(|(_, v)| v.clone())
    }
    fn record(&mut self, id: &str, response: Value) {
        self.seen.insert(id.to_string(), (Instant::now(), response));
    }
}

/// Latest robot state sampled from ROS, shared between the odom/joints
/// subscriptions and the telemetry timer.
#[derive(Default, Clone)]
struct RobotState {
    // nav_state
    pose: Option<Value>,        // {x,y,z,yaw}
    velocity: Option<Value>,    // {vx,vy,vyaw}
    localization: Option<Value>,
    pose_sampled_at: Option<f64>,
    // state
    joints: Option<Value>,
    joints_sampled_at: Option<f64>,
    // navigation lifecycle
    active_goal_id: Option<String>,
    nav_active: bool,
}

/// A raw v1 command payload forwarded by a per-channel subscription task.
#[derive(Debug)]
struct RawCmd {
    channel: &'static str,
    payload: String,
}

fn yaw_from_quat(q: &Value) -> f64 {
    let (x, y, z, w) = (
        q.get("x").and_then(Value::as_f64).unwrap_or(0.0),
        q.get("y").and_then(Value::as_f64).unwrap_or(0.0),
        q.get("z").and_then(Value::as_f64).unwrap_or(0.0),
        q.get("w").and_then(Value::as_f64).unwrap_or(1.0),
    );
    (2.0 * (w * z + x * y)).atan2(1.0 - 2.0 * (y * y + z * z))
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    tracing_subscriber::fmt()
        .with_env_filter(tracing_subscriber::EnvFilter::from_default_env())
        .init();

    let config_path = std::env::args()
        .nth(1)
        .map(|a| a.strip_prefix("--config=").map(str::to_string).unwrap_or(a))
        .unwrap_or_default();
    let config: BridgeConfig = if config_path.is_empty() {
        BridgeConfig::default()
    } else {
        let text = std::fs::read_to_string(&config_path)?;
        serde_json::from_str(&text)?
    };
    info!(device = %config.device_id, prefix = %config.prefix, "go2_v1_bridge starting");

    let rt = tokio::runtime::Builder::new_multi_thread()
        .enable_all()
        .build()?;
    rt.block_on(run(config))
}

async fn run(config: BridgeConfig) -> Result<(), Box<dyn std::error::Error>> {
    let ctx = r2r::Context::create()?;
    let mut node = r2r::Node::create(ctx, "go2_v1_bridge", "")?;
    let qos = QosProfile::default().keep_last(16).reliable();

    // --- uplink publishers (std_msgs/String JSON envelopes -> mqtt_transport)
    let resp_pub = node.create_publisher_untyped(&config.uplink.resp, "std_msgs/msg/String", qos.clone())?;
    let nav_resp_pub = node.create_publisher_untyped(&config.uplink.nav_resp, "std_msgs/msg/String", qos.clone())?;
    let mgmt_resp_pub = node.create_publisher_untyped(&config.uplink.mgmt_resp, "std_msgs/msg/String", qos.clone())?;
    let nav_event_pub = node.create_publisher_untyped(&config.uplink.nav_event, "std_msgs/msg/String", qos.clone())?;
    let state_pub = node.create_publisher_untyped(&config.uplink.state, "std_msgs/msg/String", qos.clone())?;
    let nav_state_pub = node.create_publisher_untyped(&config.uplink.nav_state, "std_msgs/msg/String", qos.clone())?;
    let status_pub = node.create_publisher_untyped(&config.uplink.status, "std_msgs/msg/String", qos.clone())?;

    // --- robot-side publishers (downlink side-effects)
    let cmd_vel_pub = node.create_publisher_untyped(&config.cmd_vel_topic, "geometry_msgs/msg/Twist", qos.clone())?;
    let route_pub = node.create_publisher_untyped(&config.route_topic, "nav_msgs/msg/Path", qos.clone())?;
    let goal_pub = node.create_publisher_untyped(&config.goal_topic, "geometry_msgs/msg/PoseStamped", qos.clone())?;
    let obstacles_pub = node.create_publisher_untyped(&config.obstacles_topic, "std_msgs/msg/Bool", qos.clone())?;

    // --- downlink command subscriptions (raw v1 envelopes from mqtt)
    let cmd_rx = node.subscribe_untyped(&config.cmd_in_topic, "std_msgs/msg/String", qos.clone())?;
    let nav_cmd_rx = node.subscribe_untyped(&config.nav_cmd_in_topic, "std_msgs/msg/String", qos.clone())?;
    let mgmt_cmd_rx = node.subscribe_untyped(&config.mgmt_cmd_in_topic, "std_msgs/msg/String", qos.clone())?;

    // --- robot state subscriptions
    let mut odom_rx = node.subscribe_untyped(&config.odom_topic, "nav_msgs/msg/Odometry", qos.clone())?;
    let mut joints_rx = node.subscribe_untyped(&config.joints_topic, "sensor_msgs/msg/JointState", qos.clone())?;

    // Each downlink command subscription runs in its own task and forwards
    // validated raw payloads over a channel, exactly like the transport fans
    // its per-mapping streams into spawned tasks. This keeps one slow command
    // or a ready telemetry tick from starving the other subscriptions.
    let (cmd_tx, mut cmd_rxq) = tokio::sync::mpsc::channel::<RawCmd>(32);
    for (stream, channel) in [
        (cmd_rx, "cmd"),
        (nav_cmd_rx, "nav_cmd"),
        (mgmt_cmd_rx, "mgmt_cmd"),
    ] {
        let tx = cmd_tx.clone();
        let mut stream = stream;
        tokio::spawn(async move {
            while let Some(msg) = stream.next().await {
                if let Ok(value) = msg {
                    let payload = value.get("data").and_then(Value::as_str).unwrap_or("").to_string();
                    info!(%channel, len = payload.len(), "v1 command received");
                    if tx.send(RawCmd { channel, payload }).await.is_err() {
                        break;
                    }
                }
            }
        });
    }

    // Spin the ROS 2 node on a dedicated thread; without spin_once the
    // subscription streams never deliver samples (publishers work regardless).
    // All publishers/subscribers are already created above, so the node can be
    // moved wholesale — mirroring transport/src/main.rs.
    let spin_stop = Arc::new(std::sync::atomic::AtomicBool::new(false));
    let spin_thread = {
        let spin_stop = spin_stop.clone();
        std::thread::spawn(move || {
            while !spin_stop.load(std::sync::atomic::Ordering::Relaxed) {
                node.spin_once(Duration::from_millis(10));
            }
            node
        })
    };

    let state = Arc::new(Mutex::new(RobotState::default()));
    let mut ledger = DedupLedger::new(config.dedup_ttl_s);
    let mut nav_stamp = UplinkStamp::new("navigation");
    let mut ctrl_stamp = UplinkStamp::new("control");
    let boot_id = nav_stamp.boot_id.clone();

    // Announce online on the status channel (retained by broker config).
    let _ = status_pub.publish(json!({"data": json!({
        "protocol_version":1,"device_id":config.device_id,"source":"control",
        "boot_id":boot_id,"timestamp":unix_seconds(),"status":"online"
    }).to_string()}));
    info!("bridge online; boot_id={boot_id}");

    let (shutdown_tx, mut shutdown_rx) = watch::channel(false);
    let _shutdown_tx = shutdown_tx; // kept for future signal hookup

    // telemetry timer
    let mut telemetry = tokio::time::interval(Duration::from_millis(200)); // 5 Hz
    telemetry.set_missed_tick_behavior(tokio::time::MissedTickBehavior::Delay);

    let resp_pubs: HashMap<&str, &r2r::PublisherUntyped> = [
        ("resp", &resp_pub),
        ("nav_resp", &nav_resp_pub),
        ("mgmt_resp", &mgmt_resp_pub),
    ]
    .into_iter()
    .collect();

    loop {
        tokio::select! {
            _ = shutdown_rx.changed() => break,

            // ---- downlink commands (forwarded by per-channel tasks)
            msg = cmd_rxq.recv() => {
                if let Some(rc) = msg {
                    handle_cmd(&rc, &config, &state, &mut ledger, &resp_pubs, &cmd_vel_pub, &route_pub, &goal_pub, &obstacles_pub);
                }
            }

            // ---- robot state intake
            msg = odom_rx.next() => {
                if let Some(Ok(v)) = msg { ingest_odom(&state, &v); }
            }
            msg = joints_rx.next() => {
                if let Some(Ok(v)) = msg { ingest_joints(&state, &v); }
            }

            // ---- periodic telemetry
            _ = telemetry.tick() => {
                ledger.prune();
                publish_telemetry(&config, &state, &mut nav_stamp, &mut ctrl_stamp, &nav_state_pub, &state_pub);
            }
        }
    }

    // announce offline on shutdown
    let _ = status_pub.publish(json!({"data": json!({
        "protocol_version":1,"device_id":config.device_id,"source":"control",
        "boot_id":boot_id,"timestamp":unix_seconds(),"status":"offline"
    }).to_string()}));
    let _ = nav_event_pub; // reserved for goal_result / goal_rejected wiring

    // stop the spin thread and reclaim the node so publishers drop cleanly
    spin_stop.store(true, std::sync::atomic::Ordering::Relaxed);
    let _node = spin_thread.join().map_err(|_| "spin thread panicked")?;
    Ok(())
}

fn handle_cmd(
    rc: &RawCmd,
    config: &BridgeConfig,
    state: &Arc<Mutex<RobotState>>,
    ledger: &mut DedupLedger,
    resp_pubs: &HashMap<&str, &r2r::PublisherUntyped>,
    cmd_vel_pub: &r2r::PublisherUntyped,
    route_pub: &r2r::PublisherUntyped,
    goal_pub: &r2r::PublisherUntyped,
    obstacles_pub: &r2r::PublisherUntyped,
) {
    let channel = rc.channel;
    let payload = rc.payload.clone();
    let parsed = parse_command(payload.as_bytes(), &config.device_id, unix_seconds());
    let reply_to = |text: Value| {
        if let Some(pub_) = resp_pubs.get(match channel {
            "nav_cmd" => "nav_resp",
            "mgmt_cmd" => "mgmt_resp",
            _ => "resp",
        }) {
            let _ = pub_.publish(json!({"data": text.to_string()}));
        }
    };

    let cmd = match parsed {
        Ok(c) => c,
        Err(e) => {
            warn!(%e, %channel, "rejected v1 command");
            // best-effort error response on the channel's resp topic
            reply_to(build_error_response(
                &config.device_id, "", "", "invalid_envelope", &e.to_string(),
            ));
            return;
        }
    };

    // dedup: same request_id -> replay the recorded response
    if let Some(prev) = ledger.lookup(&cmd.request_id) {
        let _ = resp_pubs.get(match channel { "nav_cmd"=>"nav_resp","mgmt_cmd"=>"mgmt_resp",_=>"resp" })
            .map(|p| p.publish(json!({"data": prev.to_string()})));
        return;
    }

    let d = dispatch(&cmd);
    debug_assert_eq!(d.response_channel, match channel {
        "nav_cmd" => "nav_resp", "mgmt_cmd" => "mgmt_resp", _ => "resp",
    });
    let action = match to_ros_action(&cmd, d.effect) {
        Ok(a) => a,
        Err(e) => {
            let resp = build_error_response(&config.device_id, &cmd.request_id, &cmd.command, "bad_params", &e);
            ledger.record(&cmd.request_id, resp.clone());
            reply_to(resp);
            return;
        }
    };

    let response = apply_action(&action, config, state, cmd_vel_pub, route_pub, goal_pub, obstacles_pub, &cmd);
    let success = response.get("success").and_then(Value::as_bool).unwrap_or(false);
    info!(%channel, request=%cmd.request_id, command=%cmd.command, %success, "v1 command handled");
    ledger.record(&cmd.request_id, response.clone());
    reply_to(response);
}

fn apply_action(
    action: &RosAction,
    config: &BridgeConfig,
    state: &Arc<Mutex<RobotState>>,
    cmd_vel_pub: &r2r::PublisherUntyped,
    route_pub: &r2r::PublisherUntyped,
    goal_pub: &r2r::PublisherUntyped,
    obstacles_pub: &r2r::PublisherUntyped,
    cmd: &envelope::V1Command,
) -> Value {
    let ok = |result: Value| build_response(&config.device_id, &cmd.request_id, &cmd.command, true, result);
    let fail = |code: &str, err: &str| build_error_response(&config.device_id, &cmd.request_id, &cmd.command, code, err);

    match action {
        RosAction::Twist { vx, vy, vyaw } => {
            let msg = json!({"linear":{"x":vx,"y":vy,"z":0.0},"angular":{"x":0.0,"y":0.0,"z":vyaw}});
            match cmd_vel_pub.publish(msg) {
                Ok(()) => ok(json!({"accepted":true})),
                Err(e) => fail("ros_error", &e.to_string()),
            }
        }
        RosAction::EmptyPath => {
            let msg = json!({"header":{"frame_id":"odom","stamp":{"sec":0,"nanosec":0}},"poses":[]});
            let mut st = state.lock().unwrap();
            st.active_goal_id = None;
            st.nav_active = false;
            drop(st);
            match route_pub.publish(msg) {
                Ok(()) => ok(json!({"accepted":true})),
                Err(e) => fail("ros_error", &e.to_string()),
            }
        }
        RosAction::WaypointPath(pts) => {
            let poses: Vec<Value> = pts.iter().map(|(x, y, yaw)| json!({
                "header":{"frame_id":"odom"},
                "pose":{"position":{"x":x,"y":y,"z":0.0},
                        "orientation": quat_from_yaw(*yaw)}
            })).collect();
            let msg = json!({"header":{"frame_id":"odom","stamp":{"sec":0,"nanosec":0}},"poses":poses});
            let goal_id = format!("goal-{}", uuid::Uuid::new_v4());
            let mut st = state.lock().unwrap();
            st.active_goal_id = Some(goal_id.clone());
            st.nav_active = true;
            drop(st);
            match route_pub.publish(msg) {
                Ok(()) => ok(json!({"accepted":true,"goal_id":goal_id})),
                Err(e) => fail("ros_error", &e.to_string()),
            }
        }
        RosAction::GoalPose { x, y, yaw } => {
            let msg = json!({"header":{"frame_id":"odom","stamp":{"sec":0,"nanosec":0}},
                "pose":{"position":{"x":x,"y":y,"z":0.0},"orientation":quat_from_yaw(*yaw)}});
            match goal_pub.publish(msg) {
                Ok(()) => ok(json!({"accepted":true})),
                Err(e) => fail("ros_error", &e.to_string()),
            }
        }
        RosAction::ObstaclesEnable(enable) => {
            match obstacles_pub.publish(json!({"data": enable})) {
                Ok(()) => ok(json!({"accepted":true,"enable":enable})),
                Err(e) => fail("ros_error", &e.to_string()),
            }
        }
        RosAction::None => {
            match cmd.command.as_str() {
                "get_state" | "workbench_state" => {
                    let st = state.lock().unwrap();
                    let pose_valid = st.pose.is_some();
                    let nav = json!({"active":st.nav_active,"goal_id":st.active_goal_id,"state":if st.nav_active {"navigating"} else {"idle"}});
                    let _ = pose_valid;
                    ok(json!({"navigation":nav,"online":true}))
                }
                "command_receipt" => ok(json!({"status":"unknown_request"})),
                "stand_up" | "balance_stand" | "stand_down" => {
                    fail("unsupported", "body posture control is not wired to this ROS stack")
                }
                _ => fail("unsupported", "command has no ROS primitive in this bridge"),
            }
        }
    }
}

fn quat_from_yaw(yaw: f64) -> Value {
    json!({"x":0.0,"y":0.0,"z":(yaw/2.0).sin(),"w":(yaw/2.0).cos()})
}

fn ingest_odom(state: &Arc<Mutex<RobotState>>, v: &Value) {
    let mut st = state.lock().unwrap();
    let p = &v["pose"]["pose"];
    let (x, y, z) = (
        p["position"]["x"].as_f64().unwrap_or(0.0),
        p["position"]["y"].as_f64().unwrap_or(0.0),
        p["position"]["z"].as_f64().unwrap_or(0.0),
    );
    let yaw = yaw_from_quat(&p["orientation"]);
    let t = &v["twist"]["twist"];
    st.pose = Some(json!({"valid":true,"frame_id":"map","x":x,"y":y,"z":z,"yaw":yaw}));
    st.velocity = Some(json!({"valid":true,"frame_id":"base_link",
        "vx":t["linear"]["x"].as_f64().unwrap_or(0.0),
        "vy":t["linear"]["y"].as_f64().unwrap_or(0.0),
        "vyaw":t["angular"]["z"].as_f64().unwrap_or(0.0)}));
    st.localization = Some(json!({"valid":true,"confidence":0.9}));
    st.pose_sampled_at = Some(unix_seconds());
}

fn ingest_joints(state: &Arc<Mutex<RobotState>>, v: &Value) {
    let mut st = state.lock().unwrap();
    st.joints = Some(json!({
        "valid": true,
        "names": v["name"].clone(),
        "positions": v["position"].clone(),
    }));
    st.joints_sampled_at = Some(unix_seconds());
}

fn publish_telemetry(
    config: &BridgeConfig,
    state: &Arc<Mutex<RobotState>>,
    nav_stamp: &mut UplinkStamp,
    ctrl_stamp: &mut UplinkStamp,
    nav_state_pub: &r2r::PublisherUntyped,
    state_pub: &r2r::PublisherUntyped,
) {
    let st = state.lock().unwrap().clone();

    // nav_state: pose + velocity + localization + navigation progress
    let nav_state_inner = json!({
        "pose": st.pose.clone().unwrap_or(json!({"valid":false})),
        "localization": st.localization.clone().unwrap_or(json!({"valid":false})),
        "velocity": st.velocity.clone().unwrap_or(json!({"valid":false})),
        "navigation": {"active":st.nav_active,"goal_id":st.active_goal_id,
                       "state": if st.nav_active {"navigating"} else {"idle"}},
    });
    let msg = build_state(nav_stamp, &config.device_id, "state", nav_state_inner, st.pose_sampled_at);
    let _ = nav_state_pub.publish(json!({"data": msg.to_string()}));

    // state: chassis/battery/joints
    let low_state = json!({
        "valid": st.joints.is_some(),
        "joints": st.joints.clone().unwrap_or(json!({})),
        "bms_state": {},
    });
    let msg = build_state(ctrl_stamp, &config.device_id, "low_state", low_state, st.joints_sampled_at);
    let _ = state_pub.publish(json!({"data": msg.to_string()}));
}
