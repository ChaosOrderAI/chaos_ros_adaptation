# 机群遥测上行：4G/LoRa 机器人 → 平台

机器人和平台**不在同一局域网**，中间走 **4G（蜂窝）或 LoRa**。ROS 2 的 DDS
发现机制和 100 Hz 的 `joint_states`/`odom` 在这种链路上不可行（单台
odom+joints 全量约 200 KB/s），因此采用 **MQTT broker** 做传输层，由
`chaos_mqtt` 内置的 Rust transport（`mqtt_ros2_adapter`）承担双向转发。

## 架构

```text
  机器人端                                平台端
 ┌─────────────┐                    ┌──────────────────┐
 │ chaos_sim / │                    │  pcd_gpu_viewer  │
 │ 实机驱动     │                    │  / 平台观测节点    │
 │ odom @100Hz │                    │ /fleet/<name>/…  │
 │ joints @50Hz│                    └────────┬─────────┘
 │ task_events │                             │ ROS 2 (本地 DDS)
 └──────┬──────┘                             │
        │ ROS 2 (本地 DDS)          ┌────────┴─────────┐
 ┌──────┴──────┐                    │  mqtt_transport   │
 │ mqtt_transport│                  │  (fleet downlink) │
 │ (uplink)    │                    └────────┬─────────┘
 └──────┬──────┘                             │
        │ MQTT over TLS (4G/LoRa)            │
        │ telemetry/<name>/{pose,joints,task_events}
        │ robots/<name>/{command,command/result}
        └──────────► MQTT broker ◄──────────┘
              （部署在有公网 IP 的位置）
```

- **上行（机器人 → broker）**：`ros2_to_mqtt` 映射，机器人本地 ROS 话题
  抽样后发布到 `telemetry/<robot>/…`。`delivery=latest` + `max_rate_hz` 把
  100 Hz 的 `odom`/`joint_states` 降到 5/10 Hz，旧样本不排队，断线重连只发
  最新状态。task_events 走 `latest_nonterminal`：过程反馈合并、终态必达。
- **下行（平台 → broker → 机器人）**：`mqtt_to_ros2` 映射把
  `telemetry/<robot>/…` 重新发布为本地 `/fleet/<robot>/odom` 等话题，
  `pcd_gpu_viewer` 或任何 ROS 工具可以直接订阅，不需要懂 MQTT。
- **命令通道**：`robots/<robot>/command` 复用已有的命令 envelope 协议
  （`chaos_mqtt` GatewayNode），平台侧同一份映射把命令转发到机器人的
  `mqtt/command`，结果走 `robots/<robot>/command/result` 回来。

## 带宽估算

| 链路 | 内容 | 速率 | 备注 |
|---|---|---|---|
| 4G | pose @5Hz + joints @10Hz + events | ~15 KB/s 每台 | JSON 编码；QoS 0 遥测，QoS 1 事件/命令 |
| LoRa | 仅心跳/状态摘要 | <1 KB/s | 把 `pose_rate_hz`/`joint_rate_hz` 降到 0.1–0.5，或只保留 task_events 映射 |

`max_rate_hz` 在 ROS 订阅侧按配额窗口抽样：ROS 订阅本身仍收全部样本，
相邻窗口之间新样本覆盖旧样本，因此总是**最新**状态被发出，与 `delivery`
语义一致但作用于更早的环节。两者需要同时配置：`latest` 防止弱网下旧样本
占住一半发送窗口，`max_rate_hz` 把上游速率降到链路可用范围内。

## 配置

示例文件（安装到 `share/chaos_mqtt/transport/`）：

- `config.fleet.robot.example.json` — 机器人上行（odom/joints/task_events）
- `config.fleet.platform.example.json` — 平台下行（同一 broker，全部机器人）

关键字段：

- `network.access`：`"4g"`/`"wifi"`/`"system"`；蜂窝绑定 `interface`（如 `wwan0`）。
- `topic_mappings[].max_rate_hz`：仅 `ros2_to_mqtt`，0.001–1000 Hz。
- `topic_mappings[].delivery`：`latest`（周期状态）或 `latest_nonterminal`
  （std_msgs/String JSON 事件，终态必达）。
- `mqtt`：TLS 证书为**必填**（`ca_file`/`client_certificate_file`/`client_key_file`），
  transport 不支持明文 MQTT；用户名密码用 `username` + `password_environment`。

生成配置并检查（不启动 ROS/MQTT）：

```bash
bash scripts/run_fleet_uplink.sh --robot go2-01 --broker mqtt.example.com --check
bash scripts/run_fleet_downlink.sh --robots go2-01,go2-02 --broker mqtt.example.com --check
```

## 本地验证

无公网 broker 时，可用本机 mosquitto + 自签证书跑通整条链路：

```bash
# 1) 生成测试 CA 与客户端证书
bash scripts/fleet_test_certs.sh /tmp/fleet-certs go2-01 platform

# 2) mosquitto 启用双向 TLS（ mosquitto.conf 片段 ）
#    listener 8883
#    cafile   /tmp/fleet-certs/ca.crt
#    certfile /tmp/fleet-certs/server.crt   # 需另行生成 server 证书
#    keyfile  /tmp/fleet-certs/server.key
#    require_certificate true

# 3) 平台下行（订阅 telemetry/*，发布 /fleet/*）
bash scripts/run_fleet_downlink.sh --robots go2-01 --broker 127.0.0.1 \
  --ca /tmp/fleet-certs/ca.crt --cert /tmp/fleet-certs/platform.crt --key /tmp/fleet-certs/platform.key

# 4) 机器人上行（订阅 /robot/odom 等，发布 telemetry/go2-01/*）
bash scripts/run_fleet_uplink.sh --robot go2-01 --broker 127.0.0.1 \
  --ca /tmp/fleet-certs/ca.crt --cert /tmp/fleet-certs/go2-01.crt --key /tmp/fleet-certs/go2-01.key \
  --ros-odom /sim/go2/odom --ros-joints /sim/go2/joint_states --ros-events /sim/go2/mqtt/task_events
```

真实部署时把 `--broker` 换成公网 MQTT broker 地址，证书换成正式签发的
客户端证书，`--ros-odom` 指向机器人本地的 odom 话题即可。

## 与多机仿真的关系

多机联合仿真（[多机](pcd-go2-scene.md#多机)）里 `/sim/<name>/odom` 等话题
完全可以作为本方案的输入：给每台机器人各跑一个 `run_fleet_uplink.sh`，
`--ros-odom /sim/<name>/odom --ros-joints /sim/<name>/joint_states`，
平台就能在远端实时看到同一物理场景中的每台机器人。命名空间 `flat`/`nested`
不影响 MQTT 话题名——`telemetry/<robot>/…` 始终按机器人名平铺。
