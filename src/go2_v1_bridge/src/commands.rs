//! Command dispatch table: how each v1 `command` maps to a ROS 2 primitive.
//! The bridge does not interpret semantics here — it only knows which
//! channel a command arrives on, what it should do (publish a topic, call
//! nothing yet / reply cached), and which response topic answers it.

use crate::envelope::V1Command;

/// What the bridge does with a validated command before answering.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Effect {
    /// Publish geometry_msgs/Twist on cmd_vel (params: vx, vy, vyaw).
    CmdVel,
    /// Publish an empty nav_msgs/Path on the route topic to stop motion.
    StopMove,
    /// Publish nav_msgs/Path (one pose per waypoint) on the route topic.
    Waypoints,
    /// Publish geometry_msgs/PoseStamped on the single-goal topic.
    InitPose,
    /// Publish an empty Path to cancel an active goal (params: goal_id).
    NavCancel,
    /// Answer from cached state; no ROS side-effect.
    Query,
    /// Publish a std_msgs/Bool on the obstacles-enable topic.
    ObstaclesSwitch,
    /// Body posture command — not wired to a ROS primitive in this stack.
    BodyPosture,
    /// Management/gimbal/snapshot — no ROS primitive; reply unsupported.
    Unsupported,
}

pub struct Dispatch {
    pub effect: Effect,
    /// Which uplink response topic answers this command: resp / nav_resp /
    /// mgmt_resp. Determined by the channel it arrived on.
    pub response_channel: &'static str,
}

/// Channel -> response topic suffix. A command on nav_cmd answers nav_resp.
fn response_channel(channel: &str) -> &'static str {
    match channel {
        "nav_cmd" => "nav_resp",
        "mgmt_cmd" => "mgmt_resp",
        _ => "resp",
    }
}

pub fn dispatch(cmd: &V1Command) -> Dispatch {
    let effect = match (cmd.channel.as_str(), cmd.command.as_str()) {
        ("cmd", "move") => Effect::CmdVel,
        ("cmd", "stop_move") => Effect::StopMove,
        ("cmd", "stand_up") | ("cmd", "balance_stand") | ("cmd", "stand_down") => {
            Effect::BodyPosture
        }
        ("cmd", "obstacles_switch_set") => Effect::ObstaclesSwitch,
        ("cmd", "get_state") => Effect::Query,

        ("nav_cmd", "waypoints") => Effect::Waypoints,
        ("nav_cmd", "init_pose") => Effect::InitPose,
        ("nav_cmd", "nav_cancel") => Effect::NavCancel,
        ("nav_cmd", "get_state") => Effect::Query,

        ("mgmt_cmd", "workbench_state") | ("mgmt_cmd", "command_receipt") => Effect::Query,
        ("mgmt_cmd", _) => Effect::Unsupported,

        // Unknown channel/command combos still get a response on the
        // channel's own resp topic so the platform isn't left hanging.
        _ => Effect::Unsupported,
    };
    Dispatch {
        effect,
        response_channel: response_channel(&cmd.channel),
    }
}

/// Build the ROS payload(s) for a side-effecting command. Returns a Vec of
/// (topic-kind, JSON-value) the node converts into concrete ROS messages.
#[derive(Debug, Clone, PartialEq)]
pub enum RosAction {
    Twist { vx: f64, vy: f64, vyaw: f64 },
    EmptyPath, // used for stop_move and nav_cancel
    WaypointPath(Vec<(f64, f64, f64)>), // (x, y, yaw)
    GoalPose { x: f64, y: f64, yaw: f64 },
    ObstaclesEnable(bool),
    None,
}

fn num(v: &serde_json::Value, key: &str) -> Option<f64> {
    v.get(key).and_then(|n| n.as_f64()).filter(|n| n.is_finite())
}

pub fn to_ros_action(cmd: &V1Command, effect: Effect) -> Result<RosAction, String> {
    match effect {
        Effect::CmdVel => Ok(RosAction::Twist {
            vx: num(&cmd.params, "vx").unwrap_or(0.0),
            vy: num(&cmd.params, "vy").unwrap_or(0.0),
            vyaw: num(&cmd.params, "vyaw").unwrap_or(0.0),
        }),
        Effect::StopMove | Effect::NavCancel => Ok(RosAction::EmptyPath),
        Effect::Waypoints => {
            let list = cmd
                .params
                .get("waypoints")
                .and_then(|w| w.as_array())
                .ok_or_else(|| "waypoints: missing params.waypoints array".to_string())?;
            let mut pts = Vec::new();
            for w in list {
                let x = num(w, "x").ok_or("waypoint missing x")?;
                let y = num(w, "y").ok_or("waypoint missing y")?;
                let yaw = num(w, "yaw").unwrap_or(0.0);
                pts.push((x, y, yaw));
            }
            if pts.is_empty() {
                return Err("waypoints: empty list".into());
            }
            Ok(RosAction::WaypointPath(pts))
        }
        Effect::InitPose => Ok(RosAction::GoalPose {
            x: num(&cmd.params, "x").ok_or("init_pose missing x")?,
            y: num(&cmd.params, "y").ok_or("init_pose missing y")?,
            yaw: num(&cmd.params, "yaw").unwrap_or(0.0),
        }),
        Effect::ObstaclesSwitch => Ok(RosAction::ObstaclesEnable(
            cmd.params
                .get("enable")
                .and_then(|e| e.as_bool())
                .ok_or("obstacles_switch_set missing enable")?,
        )),
        Effect::Query | Effect::BodyPosture | Effect::Unsupported => Ok(RosAction::None),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::envelope::PROTOCOL_VERSION;
    use serde_json::json;

    fn cmd(channel: &str, command: &str, params: serde_json::Value) -> V1Command {
        V1Command {
            device_id: "d".into(),
            request_id: "r".into(),
            channel: channel.into(),
            command: command.into(),
            params,
            issued_at: 1.0,
            expires_at: 2.0,
            expected_boot_id: None,
            cancel_parent_id: None,
            cancel_root_id: None,
        }
    }

    #[test]
    fn channels_map_to_their_resp_topics() {
        assert_eq!(dispatch(&cmd("cmd", "move", json!({}))).response_channel, "resp");
        assert_eq!(dispatch(&cmd("nav_cmd", "waypoints", json!({}))).response_channel, "nav_resp");
        assert_eq!(dispatch(&cmd("mgmt_cmd", "workbench_state", json!({}))).response_channel, "mgmt_resp");
        let _ = PROTOCOL_VERSION;
    }

    #[test]
    fn waypoints_build_a_path() {
        let c = cmd("nav_cmd", "waypoints", json!({"channel":"action","waypoints":[{"x":1.0,"y":2.0,"yaw":0.5}]}));
        match to_ros_action(&c, Effect::Waypoints).unwrap() {
            RosAction::WaypointPath(pts) => assert_eq!(pts, vec![(1.0, 2.0, 0.5)]),
            other => panic!("unexpected {other:?}"),
        }
        let bad = cmd("nav_cmd", "waypoints", json!({"waypoints":[]}));
        assert!(to_ros_action(&bad, Effect::Waypoints).is_err());
    }

    #[test]
    fn move_and_cancel_map_to_ros() {
        let m = cmd("cmd", "move", json!({"vx":0.5,"vy":0.0,"vyaw":0.1}));
        assert_eq!(
            to_ros_action(&m, Effect::CmdVel).unwrap(),
            RosAction::Twist { vx: 0.5, vy: 0.0, vyaw: 0.1 }
        );
        let c = cmd("nav_cmd", "nav_cancel", json!({"goal_id":"g1"}));
        assert_eq!(to_ros_action(&c, Effect::NavCancel).unwrap(), RosAction::EmptyPath);
    }
}
