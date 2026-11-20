# 机载 AI 智能体层：`chaos_agent` + `chaos_skill`

2026-11-20 新增。状态：**开发中**，与 `chaos_runtime`/`chaos_sdk` 同一构建目标；已通过 mock 资产 + 桩 llama-server 的端到端闭环（agent → tool_call → ExecuteSkill → Executor → navigate），尚无真实 llama-server 与机载验证。设计参照 Heave-Ho（arXiv:2611.22275）的「离散技能词表 + 枢纽位姿」两层结构：规划层只产出 token，执行层把 token 翻译成带租约的 SDK 调用。

## 分层

```
chaos_agent        规划层：llama.cpp llama-server（Gemma 4 E4B 等）的 HTTP 客户端
                   把技能词表翻译成 OpenAI tool_calls，一次决策一个 token

chaos_skill        执行层：技能词表（vocabulary）+ 枢纽状态机 + ExecuteSkill action server
                   租约生命周期、SDK 调用、把结果折回 hub

chaos_runtime      不动：租约仲裁、速度仲裁、watchdog、设备适配器
chaos_sdk          不动：统一能力契约客户端
```

`chaos_agent` 不持有任何设备连接，不订阅原始传感器流，只能产出离散 token；`chaos_skill` 是唯一被允许调 `chaos_sdk` 的层，它把 token 的进入/退出枢纽当作前置/后置条件检查，不满足就拒发。

## 技能词表

词表是**版本化资产配置**的一部分（和 model.yaml 同级），以 JSON 文档传给两个包，保证 planner 和 executor 用的是同一份：

- `hub_states`：封闭集合，例如 `["idle","carrying","airborne","failed"]`。资产任一时刻恰好处于一个 hub。
- `skills`：每个 skill 声明 `token`、`entry_hub`、`exit_hub`、`arguments_schema`、`needs_lease`，可选 `any_entry_hub`。
- `any_entry_hub: true` 的 skill（`hold`、`stop` 这类）不切换 hub，从任何 hub 可进、退出后仍停留原 hub。

样例见 [src/chaos_skill/config/demo_vocabulary.json](../src/chaos_skill/config/demo_vocabulary.json)。

## 接口

`chaos_interfaces` 新增：

- `msg/SkillToken.msg`：`token` + `arguments_json` + `decided_at` + `source`。
- `srv/SkillRequest.srv`：校验 token 在当前 hub 下是否可发（不发执行）。
- `action/ExecuteSkill.action`：长期执行一个 token；feedback 报当前 hub，result 报成功/失败和退出 hub。

## chaos_skill

`Executor` 负责：

1. `Vocabulary::check_entry(token, current_hub)` 前置检查。
2. `needs_lease` 时经 `chaos_sdk::Robot::acquire` 获取短租约，终态后 `release`。
3. 分派到对应 SDK 调用（`navigate_to`→`navigate`，`takeoff`→`takeoff` …）。
4. 成功则把 `exit_hub` 写入当前 hub；失败保持进入时的 hub。

`chaos_skill_server` 是每资产一个的 action server，加载词表 → 建 `Executor` → 串行处理 `ExecuteSkill` 目标。

## chaos_agent

`AgentLoop::run_once()` 一个周期：

1. 构造 `snapshot`（`RobotState` + 当前 hub + 任务文本）。
2. 把词表翻译成 OpenAI `tools`（每个 skill 一个 `skill_<token>` 函数，外加 `skill_hold`/`skill_done`）。
3. 调 `LlamaClient::chat`（llama.cpp `/v1/chat/completions`）。
4. 把第一个 `tool_call` 解成 token，`done` 结束任务，`hold` 空转一拍，其余发 `ExecuteSkill` 目标。

`LlamaClient` 走 libcurl 的 OpenAI 兼容接口，无 Python/ML 依赖——llama-server 是机器上另一个进程（可裸跑，不用 docker）。

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

`vocabulary_file` 是唯一必需项：不给就不起 `chaos_skill_server`。`enable_agent`/`llama_endpoint`/`task` 决定要不要一起起 `chaos_agent_node`。`initial_hub` 缺省用词表第一个 hub。`chaos_agent_node` 默认在资产 namespace 内解析 `execute_skill` 与 `state`，`skill_ns` 参数仅在把 agent 放到资产外时需要。

也可以不带 agent 只跑技能层（`vocabulary_file` + `initial_hub`），此时 `ExecuteSkill` action 仍可由 operator 或其它进程手动驱动。

## 与 Heave-Ho 的对应关系

| Heave-Ho | 本仓库 |
|---|---|
| 技能词表 `K ∪ {hold,done}` | `Vocabulary::skills` + `hold`/`done` 控制 token |
| 枢纽位姿 S0/S1 | `hub_states` + `entry_hub`/`exit_hub` |
| VLM planner | `chaos_agent` 调 llama.cpp（Gemma 4 E4B）|
| 语音 leader-follower 同步 | （未实现，词表已留 `source`/`decided_at` 供扩展）|
| 共享残差策略 + 冻结基座 | 不属于本层——底层控制仍走 `chaos_runtime` |
