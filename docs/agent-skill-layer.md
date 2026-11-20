# 机载 AI 智能体层：`chaos_agent` + `chaos_skill`

2026-11-20 新增。状态：**开发中**，与 `chaos_runtime`/`chaos_sdk` 同一构建目标；已通过 mock 资产 + 桩 llama-server 的端到端闭环（agent → 意图 → 技能层 → navigate），尚无真实 llama-server 与机载验证。设计参照 Heave-Ho（arXiv:2611.22275）的「离散技能词表 + 枢纽位姿」两层结构：规划层只产出 token，执行层把 token 翻译成带租约的 SDK 调用。

## 分层

```
chaos_mqtt        顶层来源之一：外部通信适配器。把远端命令翻译成技能意图
chaos_agent       顶层来源之一：机载规划层。把模型决定翻译成技能意图
                  两者并行，互不依赖，都只发布 SkillIntent

chaos_skill       技能层：技能词表 + 枢纽状态机 + 能力分派
                  唯一的汇聚点，唯一被允许调用基础功能的层

基础功能层        通信 | 传感器 | 运动 | 导航
                  运动与导航经 chaos_sdk；传感器与通信经 CapabilityPorts
```

**顶层来源是并行的两个**，不是上下级：`chaos_mqtt` 与 `chaos_agent` 都把意图交给技能层，谁也不依赖谁。

**技能层是唯一汇聚点**：所有进入本机的意图（机载智能体的决定、外部操作者的命令）都经同一个 `Executor`，因此都经过同一套租约生命周期、前置条件检查与枢纽记账。

**技能层是唯一调用基础功能的层**：`chaos_mqtt` 的网关不再直接调用运行时的 Action，它把能力命令翻译成技能意图。仍走直连的是**控制面**，不是能力面：

| 命令 | 归属 | 理由 |
|---|---|---|
| `navigation.to_pose`、`flight.takeoff`、`flight.land` | 技能层 | 离散任务，有前置条件与终态 |
| `motion.velocity` | 直连 | 连续遥操作流，不是离散任务 |
| `motion.stop` | 直连 | 急停不能排在正在执行的技能后面 |
| `control.acquire/renew/release` | 直连 | 控制权仲裁归运行时所有 |
| `system.describe` | 直连 | 只读内省 |
| `system.ota` | 直连 | 系统维护动作，不属于四族能力 |

## 技能词表

词表是**版本化资产配置**的一部分（和 model.yaml 同级），以 JSON 文档传给两个包，保证 planner 和 executor 用的是同一份：

- `hub_states`：封闭集合，例如 `["idle","carrying","airborne","failed"]`。资产任一时刻恰好处于一个 hub。
- `skills`：每个 skill 声明 `token`、`entry_hub`、`exit_hub`、`capability`、`arguments_schema`、`needs_lease`，可选 `any_entry_hub`。
- `capability`：**必填**，取 `motion` / `navigation` / `sensor` / `communication` 之一。词表是唯一声明"某个 token 最终调用哪一族基础功能"的地方。
- `any_entry_hub: true` 的 skill（`hold`、`stop`、`observe`、`announce` 这类）不切换 hub，从任何 hub 可进、退出后仍停留原 hub。

样例见 [src/chaos_skill/config/demo_vocabulary.json](../src/chaos_skill/config/demo_vocabulary.json)。

## 接口

`chaos_interfaces` 中：

- `msg/SkillToken.msg`：`token` + `arguments_json` + `decided_at` + `source`。
- `msg/SkillIntent.msg`：**顶层来源汇入技能层的唯一入口**。含 `SkillToken`、`request_id`、`expires_at`、`enforce_hub_precondition`、`control_token`。
- `msg/SkillEvent.msg`：技能层发布的生命周期。`phase` 取 `accepted` / `feedback` / `succeeded` / `failed`，带 `success`、`code`、`hub_state`、`progress`、`payload_json`。
- `msg/Announcement.msg`：技能层向外部通信通道发出的通告。
- `srv/SkillRequest.srv`：校验 token 在当前 hub 下是否可发（不发执行）。
- `action/ExecuteSkill.action`：本机侧的门面，供行为树、工具或操作员使用；长期执行一个 token。

`SkillIntent` 的三个非平凡字段：

- `enforce_hub_precondition`：机载智能体的决定是**规划决定**，受词表的枢纽前置条件约束；远端操作者的命令**不是**规划决定，不受约束——枢纽是规划层的状态机，不能用来否决一次直接操作。两种意图都由技能层执行，并由技能层更新枢纽。
- `control_token`：调用方持有的租约。非空时技能用该令牌执行，**不再自行申请租约，也不释放它**：运行时只承认一个租约持有者。外部通信适配器把远端操作者的租约令牌放在这里，于是远端命令以远端的控制权执行。
- `request_id`：关联标识。技能层记住它，重复的 `request_id` **不会第二次执行**技能，而是重放上一次的结果；这正是"确认丢失后重试"所需要的语义。

## chaos_skill

`Executor` 负责：

1. `Vocabulary::check_entry(token, current_hub)` 前置检查（受 `enforce_hub_precondition` 控制）。
2. 租约：`needs_lease` 时经 `chaos_sdk::Robot::acquire` 获取短租约，终态后 `release`；带 `control_token` 时跳过申请与释放。
3. 分派到对应基础功能调用（`navigate_to`→导航，`takeoff`/`land`/`stop`→运动，`observe`→传感器，`announce`→通信）。
4. 成功则把 `exit_hub` 写入当前 hub；失败保持进入时的 hub。

`CapabilityPorts` 是技能层触达"通信"和"传感器"的端口：`announce()` 写通用通信通道，`read_aux()` 读辅助传感流。实现里通道只是一个 ROS 话题，机间传输订阅它并转发——**换掉传输方式不需要动技能层**。

`IntentIngress` 是技能层的入口：订阅 `skill_intent`，用**一个** `Executor` 串行执行，把生命周期发布到 `skill_event`。它负责 `request_id` 记账（运行中的忽略、已完成的重放）、过期检查与拒绝原因。

`chaos_skill_server` 是每资产一个的节点：加载词表 → 建 `Executor` → 起 `IntentIngress` → 同时保留 `ExecuteSkill` action 门面与 `SkillRequest` 服务。

## chaos_agent

`AgentLoop::run_once()` 一个周期：

1. 构造 `snapshot`（`RobotState` + 当前 hub + 任务文本）。
2. 把词表翻译成 OpenAI `tools`（每个 skill 一个 `skill_<token>` 函数，外加 `skill_hold`/`skill_done`）。
3. 调 `LlamaClient::chat`（llama.cpp `/v1/chat/completions`）。
4. 把第一个 `tool_call` 解成 token，`done` 结束任务，`hold` 空转一拍，其余发布成 `SkillIntent`（`enforce_hub_precondition=true`）。
5. 等待自己的 `skill_event` 终态，把 hub 写回快照。

`LlamaClient` 走 libcurl 的 OpenAI 兼容接口，无 Python/ML 依赖——llama-server 是机器上另一个进程（可裸跑，不用 docker）。

## chaos_mqtt

网关的职责收缩为**线路层**：envelope 解析与校验、TTL 与截止时间、请求关联与重放缓存、响应与事件发布。能力命令不再进入运行时，而是翻译成 `SkillIntent`：

| MQTT 命令 | 技能 token | 参数 |
|---|---|---|
| `navigation.to_pose` | `navigate_to` | `frame_id`、`x`、`y`、`z`、四元数 |
| `flight.takeoff` | `takeoff` | `altitude_m` |
| `flight.land` | `land` | — |

网关把远端的租约令牌放进意图的 `control_token`，因此技能以远端的控制权执行。网关发布意图后**持续重发**直到技能层受理为止；因为技能层按 `request_id` 去重，重发不会重复执行。技能层的事件被翻译回线路语义：`accepted` 变成受理回执，`feedback` 变成 `task_events`，终态变成终态回执与终态事件。

## 运行

技能层已经接入 `chaos_bringup` 的每资产 launch（`system.launch.py` / `edge.launch.py`），通过 launch 参数按资产开启：

```bash
# 1) llama-server 独立进程（不用 docker）
./llama-server -m gemma-4-E4B_q4_0-it.gguf --mmproj mmproj-...gguf --jinja --port 8000

# 2) system launch：mock 或真机，开启技能层
ros2 launch chaos_bringup system.launch.py \
  inventory:=... assets:=vehicle_001 \
  vocabulary_file:=src/chaos_skill/config/demo_vocabulary.json \
  initial_hub:=idle \
  enable_agent:=true \
  llama_endpoint:=http://127.0.0.1:8000 \
  task:="move forward"
```

`vocabulary_file` 是唯一必需项：不给就不起 `chaos_skill_server`。`enable_agent`/`llama_endpoint`/`task` 决定要不要一起起 `chaos_agent_node`。`initial_hub` 缺省用词表第一个 hub。`chaos_agent_node` 默认在资产 namespace 内解析 `skill_intent` 与 `skill_event`，`skill_ns` 参数仅在把 agent 放到资产外时需要。

**启用 MQTT 时技能层必须存在**：MQTT 的能力命令经技能层下发，因此 launch 在 `enable_mqtt:=true` 且未指定 `vocabulary_file` 时用随包发布的词表启动技能层，并在日志里说明。不想启动就关掉 MQTT。

其它 launch 参数：`intent_topic`、`event_topic`、`announcement_topic`、`sensor_cloud_topic`、`sensor_imu_topic`。

也可以不带 agent 只跑技能层（`vocabulary_file` + `initial_hub`），此时 `ExecuteSkill` action 仍可由 operator 或其它进程手动驱动。

## 与 Heave-Ho 的对应关系

| Heave-Ho | 本仓库 |
|---|---|
| 技能词表 `K ∪ {hold,done}` | `Vocabulary::skills` + `hold`/`done` 控制 token |
| 枢纽位姿 S0/S1 | `hub_states` + `entry_hub`/`exit_hub` |
| VLM planner | `chaos_agent` 调 llama.cpp（Gemma 4 E4B）|
| 语音 leader-follower 同步 | （未实现，词表已留 `source`/`decided_at` 供扩展）|
| 共享残差策略 + 冻结基座 | 不属于本层——底层控制仍走 `chaos_runtime` |
