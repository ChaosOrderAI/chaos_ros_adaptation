# go2_v1_bridge

平台 v1 MQTT 设备协议桥（机器狗）。坐在 `chaos_mqtt` transport（哑透传）
和机器人 ROS 2 栈之间，实现 `MQTT_DEVICE_PROTOCOL.md` 里的狗车 v1 协议。

## 角色

```
平台 Real Runtime
      │  v1 信封 JSON, {prefix}/{cmd,nav_cmd,mgmt_cmd,state,...}
      ▼
 mosquitto (mutual TLS)
      ▲
      │  chaos_mqtt transport: 纯 topic↔topic 透传
      │    config.platform.go2_002.example.json
      ▼
 go2_v1_bridge (本包)
      │  下行: v1 信封校验(protocol_version/device_id/request_id/expires)
      │        → 去重 → 翻译成 ROS 原语(cmd_vel/Path/GoalPose)
      │  上行: 采 odom/joints → 套 v1 信封(boot_id/seq/timestamp)
      ▼
 ROS 栈: /cmd_vel, /initial_path, /goal_pose, /aft_mapped_to_init, /joint_states
```

为什么独立一层：信封里的 `request_id`/`boot_id`/`seq`/`expires_at`/`sampled_at`
是程序生成并需状态管理的语义，不是 `topic_mappings` 能做的静态透传。

## 配置

`config.go2_002.example.json`（桥）+ `chaos_mqtt` 侧
`config.platform.go2_002.example.json`（透传映射）。`device_id`/`prefix`/
ROS topic 名都在桥配置里。注意 legacy `command_topic`/`response_topic`/
`event_topic` 必须用不撞 `topic_mappings` 的保留名（`_legacy/*`）。

## 运行

```bash
source /opt/ros/jazzy/setup.bash && source install/local_setup.bash
# 1. MQTT 透传层(robot 侧)
./install/chaos_mqtt/lib/chaos_mqtt/mqtt_transport \
  --config install/chaos_mqtt/share/chaos_mqtt/transport/config.platform.go2_002.example.json
# 2. v1 协议桥
ros2 run go2_v1_bridge go2_v1_bridge \
  install/go2_v1_bridge/share/go2_v1_bridge/config.go2_002.example.json
```

## 已验证

- `cmd/move` → `/cmd_vel` Twist;`nav_cmd/waypoints` → `/initial_path` Path;
  `nav_cmd/nav_cancel` → 空 Path;`get_state` 查询 → 回执
- `request_id` 去重：同 id 重放上次回执，冲突意图拒绝
- 上行 `state`/`nav_state` 周期信封：protocol_version/device_id/source/
  boot_id/seq/timestamp/sampled_at
- `r2r` 订阅必须靠独立 `spin_once` 线程驱动（同 transport/main.rs),否则
  订阅流永远空——这是本包的关键实现约束

## 未接的协议面（后续）

`mode_cmd`/`mode_resp`/`mode_state`(v2 跟随)、`detections`(需感知输出)、
`mgmt_cmd` 大部分命令、`nav_event` 的 goal_result 终态回报（当前
`nav_active`/`goal_id` 只在桥内维护,未接真实导航终态)。body posture
(stand_up/down)平台栈无对应原语,回 `unsupported`。
