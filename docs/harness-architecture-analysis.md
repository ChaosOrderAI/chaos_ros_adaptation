# 机载 AI harness 代码框架分析：`chaos_agent` + `chaos_skill`

**分析基准**：`main` 分支 + 未提交的在途重构，2026-11-20 15:46 工作树快照（`/tmp/harness_snapshot_154613`）。
该分支正在被并行修改（分析期间 `chaos_sdk`、`chaos_mqtt`、`chaos_bringup`、`chaos_skill` 的测试都在变），本文所有行号以该快照为准。

所有结论均给出 `文件:行号`，可逐条复核。

**方法**：六个独立 reader 分别深读规划层、执行层、意图入口、线契约、集成面、文档一致性，每条候选问题再由三个不同视角的 reviewer（代码追踪 / 契约一致性 / 运行时可达性）独立裁决。全部结论在下文标注前都经过**逐条手工代码追踪复核**；其中「准入去重是否可能双执行」一条被证伪——第二次 `pending_` 检查与插入在同一临界区内，原子性成立，不会双执行。

**注意**：分析期间工作树正在被并行修改（`chaos_sdk`、`chaos_mqtt`、`chaos_bringup`、`chaos_skill` 的测试都在变）。本文以 15:46 快照为准；之后的改动可能已使部分结论过时。

---

## 1. 一句话定位

这套 harness 的承重结构是 **「离散 token → 枢纽约束执行」** 的双层夹心：

- **规划层 `chaos_agent`** 把一个**闭集词表**翻译成 LLM 的 function tools，一次决策只产出一个 token，然后包成 `SkillIntent` 发出去。
- **执行层 `chaos_skill`** 是**唯一被允许触达平台的层**：校验 token 在当前 hub 下是否可发起、按需取租约、派发到 SDK 或能力端口、再把结果折回 hub。

贯穿全层的三条不变量：

1. **LLM 不碰平台**。agent 不持有设备连接、不订阅原始传感流，只能产 token（`chaos_agent/include/chaos_agent/agent_loop.hpp:33-35`）。
2. **执行层是唯一的平台入口**。运动/导航走 `chaos_sdk::Robot`，传感/通信走 `CapabilityPorts`，不碰 broker、不碰驱动（`chaos_skill/include/chaos_skill/executor.hpp:51-53`）。
3. **契约不带传输层字段**。`SkillIntent`/`SkillEvent`/`Announcement` 里没有 topic/broker/transport 字段（`chaos_interfaces/msg/SkillIntent.msg` 头部注释）。

---

## 2. 分层与职责

| 层 | 包 | 拥有 | 禁止触碰 |
|---|---|---|---|
| 规划 | `chaos_agent` | LLM 决策、tool schema 构造、意图发布与事件关联 | 设备连接、原始传感流、直接调 SDK |
| 外部通信 | `chaos_mqtt` | 机间传输、远端命令→`SkillIntent`、事件→机间通道 | 直接调用 SDK / 直接操作机器人 |
| 执行 | `chaos_skill` | 词表、hub 状态机、租约生命周期、token→基础能力分派、能力端口 | 感知 LLM、感知传输层 |
| 契约 | `chaos_interfaces` | `SkillIntent`/`SkillEvent`/`Announcement`/`SkillToken`/`SkillRequest`/`ExecuteSkill` | 传输层字段 |
| 平台 | `chaos_runtime` + `chaos_sdk` | 租约仲裁、速度仲裁、watchdog、设备适配器、SDK 客户端 | — |

`chaos_agent` **不依赖** `chaos_skill`（`src/chaos_agent/package.xml:13-18` 只有 `chaos_core`/`chaos_interfaces`/`rclcpp`/`libcurl`）。规划层与执行层之间唯一的耦合是**词表文档 + 线协议**——这是整套设计里最值得保留的决定。

---

## 3. 运行拓扑

```
                      ┌──────────── 资产 namespace ────────────┐
                      │                                        │
 llama-server ─HTTP─▶ chaos_agent_node                          │
  (gemma-4-e4b)       │  enforce_hub_precondition = true        │
                      │  control_token = 空（技能层自持租约）    │
                      │        │ publish SkillIntent            │
                      │        ▼                                │
 MQTT gateway ───────▶│  chaos_skill_server ◀──────────────────┘
  enforce_hub_precondition = false                              ▲
  control_token = 远端操作者的租约令牌                            │ publish SkillEvent
                      │        │ IntentIngress（幂等/重放/队列）   │
                      │        ▼                                │
                      │      Executor（hub FSM + 租约 + 分派）     │
                      │        │                                │
                      │        ▼                                │
                      │   ServerPorts ──▶ communication/announcement
                      │        │           （当前无订阅者）        │
                      │        ▼                                │
                      │  chaos_sdk::Robot ──▶ chaos_runtime（仲裁/watchdog/驱动）
                      └────────────────────────────────────────┘
```

---

## 4. 核心机制

### 4.1 词表是唯一真相源

`chaos_bringup/chaos_bringup/skills.py` 在 launch 期读一次 JSON，**同一份文本**同时作为参数喂给 `chaos_skill_server` 和 `chaos_agent`。planner 与 executor 因此不可能对 token 集合有分歧。

词表结构（`src/chaos_skill/config/demo_vocabulary.json`）：

- `hub_states`：闭合集合。`Vocabulary::from_json` 强制每个 skill 的 `entry_hub`/`exit_hub` 都在集合内，否则 `skill_vocab.undeclared_hub` 拒绝（`chaos_skill/src/vocabulary.cpp:94-96`）。
- `skills[]`：每个 skill 声明 `token`/`entry_hub`/`exit_hub`/`capability`（必填，`vocabulary.cpp:83`）/`needs_lease`/`arguments_schema`/`description`。
- `any_entry_hub: true` 的 skill（`hold`/`stop`/`observe`/`announce`）不切换 hub。

**闭合 hub 集合 + 首尾相接的 entry/exit 语义**是真正承重的地方：任意合法 token 序列天然可组合，规划层因此不需要懂状态机。

### 4.2 hub 状态机：两种来源，两种语义

当前树最重要的设计已经成形——`SkillOrigin`（`chaos_skill/include/chaos_skill/capability.hpp:49-67`）带两个新字段：

| 字段 | 规划来源（agent） | 操作来源（MQTT 远端操作者） |
|---|---|---|
| `enforce_hub` | `true`（`agent_loop.cpp:215`） | `false`（`chaos_mqtt/src/gateway_node.cpp`，`send_skill` 内） |
| `control_token` | 空 | 远端操作者的租约令牌 |

即：**hub 是规划层的状态机，不是操作层的门禁**。远端操作者的直接命令不被枢纽条件否决——这是正确的设计（否则操作员无法在非常规状态下救机）。

`Executor::execute` 据此分叉（`chaos_skill/src/executor.cpp:201-215`）：

- `enforce_hub` 为真 → 走 `vocab_.check_entry`，不匹配则 `skill.hub_mismatch` 拒发；
- `control_token` 非空 → 技能用**调用方的令牌**执行，技能层既不申请租约也不释放它（运行时仍然只承认一个租约持有者）；
- 否则技能层自持租约：`ensure_lease()` → `robot_->acquire("chaos_skill", 10s)`（`executor.cpp:126-144`），终态后 `release_lease()`（`executor.cpp:146-160, 253-255`）。

### 4.3 hub 回写

成功 → `hub_ = exit_hub(token)`（`executor.cpp:245-249`）；失败 → `hub_` 保持进入时的值（`executor.cpp:250-251`）。`any_entry_hub` 的技能退出时返回**当前** hub（`vocabulary.cpp:170-173`），即 pass-through。

### 4.4 意图入口的幂等语义

`IntentIngress`（`chaos_skill/src/intent_ingress.cpp`）实现了传输层需要的幂等：

- `pending_`：正在执行的 request_id，重复到达直接忽略（`:155, :205`）；
- `finished_` + `finished_order_`：有界重放缓存（默认 256），重复到达重放上次终态事件（`:101, :121-125, :158-163`）；
- `queue_`：容量 8，满了以 `skill.busy` 拒绝（`:208-211`）；
- `deadline_passed()` 用 **system_clock** 比 Unix UTC（`:20-30`）——这是对的：暂停/跳变的 sim clock 不能延长传输层已经放弃的受理窗口。

空 `request_id` 表示「来源不需要关联」，不进入 `pending_`/`finished_`，每次都真正执行。

### 4.5 能力端口（通信/传感器接缝）

`CapabilityPorts`（`capability.hpp:70-90`）把「通信」和「传感器」抽象成 `announce()` / `read_aux()`；`ServerPorts`（`server_ports.cpp`）用普通 ROS topic 实现：announcement 走 publisher（`:60-66`），aux 走只读订阅（只回 newest sample 的元信息，不回 payload）。

这条缝的意图正确：**`announce` 不绑定 MQTT**。但另一端目前没接上——见 §7 P1。

---

## 5. 契约面

| 契约 | 形态 | 生产者 | 消费者 |
|---|---|---|---|
| `SkillToken` | msg | agent / 网关 | 技能层 |
| `SkillIntent` | msg | agent、MQTT 网关（对等 peer） | `IntentIngress` |
| `SkillEvent` | msg | **仅**技能层 | agent、MQTT 网关 |
| `Announcement` | msg | `ServerPorts` | （当前无订阅者） |
| `SkillRequest` | srv | 调用方 | 技能层（只校验，不执行） |
| `ExecuteSkill` | action | 调用方 | 技能层（带 feedback） |

`SkillEvent` 的四相生命周期：`accepted` → `feedback`* → `succeeded`/`failed`。

**三条请求路径共存**（最大的契约面割裂）：

| | `ExecuteSkill` action | `SkillRequest` srv | `SkillIntent` topic |
|---|---|---|---|
| request_id | **无** | **无** | 有 |
| 幂等/重放 | 无 | 无 | 有 |
| 受理截止生效 | 否（字段在、服务端不读） | 否 | 是 |
| 事件回执 | action feedback/result | 无 | `SkillEvent` |
| 外部来源可用 | 需专门客户端 | 需专门客户端 | 任意 ROS 客户端 |

---

## 6. 在途重构

未提交的工作是一次 **「action 中心 → message 中心」** 的改形，并且已经从「单机 agent 闭环」走向「agent 与外部通信适配器对等」：

**旧形态**（`00fc6e3` 已提交）：agent → `ExecuteSkill` action → 技能层；技能层只暴露 action + service；词表无 capability 概念；Executor 无锁、无能力端口。

**新形态**（当前工作树）：
- `SkillIntent`/`SkillEvent` 成为主干，agent 与 MQTT 网关成为**对等 peer**，都发 intent、都订阅事件；
- `IntentIngress` 成为唯一意图入口，承担幂等/重放/队列；
- 新增 `Capability` 家族，把「技能层是唯一调基础功能的层」从注释变成词表里的**显式声明**（且 `vocabulary_tests.cpp` 有专门的用例守着）；
- 新增 `CapabilityPorts` 接缝；
- `SkillOrigin` 引入 `enforce_hub` + `control_token`，区分「规划决定」与「直接操作」；
- `chaos_sdk` 的 `stop/navigate/takeoff/land` 增加 `lease_token` 透传参数，使远端操作者的租约能被技能层使用；
- `chaos_mqtt` 网关新增 `skill_intent`/`skill_event` 桥接；
- 新增测试：`chaos_skill/test/vocabulary_tests.cpp`（8 例）+ `test/skill_layer_tests.cpp`（9 例），已在 `CMakeLists.txt:54-63` 注册。

**重构意图**：让「技能层是唯一调基础功能的层」这条规则在**机器之间**也成立——外部操作者的命令不再直接打 SDK，而是与机载 agent 走同一条管道、同一套幂等与回执语义。方向正确；收尾有几处没做完（见 §7）。

---

## 7. 已验证的问题

### P0 — 规划来源的 `stop` 技能必然失败；技能层没有可用的急停路径

`stop` 在出厂词表里 `needs_lease: false`（`demo_vocabulary.json`）。规划来源不带 `control_token`，于是：

1. `owns_lease = origin.control_token.empty()` → `true`（`executor.cpp:211`）；
2. `owns_lease && def->needs_lease && !ensure_lease(...)` → `needs_lease` 为假，**不申请租约**（`executor.cpp:212`）；
3. `control_token` 为空 → `run_stop_motion` 调 `robot_->stop(nullopt, nullopt, nullopt)`（`executor.cpp:385-388`）；
4. `Robot::stop()` 走 `require_token()`（`chaos_sdk/src/robot.cpp:770` → `:562`），客户端无令牌时 **throw `std::invalid_argument`**；
5. 被 `execute` 的兜底 `catch (const std::exception &)` 吞成 `skill.internal_error`（`executor.cpp:239-241`）。

客户端为何无令牌：上一个 `needs_lease` 技能结束时 `release_lease()` 调 `robot_->release()`，成功释放后 SDK 清空缓存令牌（`chaos_sdk/src/sdk_core.cpp:132-138`）。**技能层在技能执行期间永远不持有令牌**，除非该技能自己 `needs_lease=true` 或调用方带了 `control_token`。

操作来源（MQTT）不受影响：网关带远端租约令牌进来，`robot_->stop(..., control_token)` 成立。所以**这条路径只在规划路径坏掉**——而规划路径正是 agent 的路径。

即便拿到令牌也是死路：

- `cancel_requested_` 全包**只写不读**（`executor.cpp:117` 置位、`:193` 复位，无读取点）；action 的 `on_cancel` 返回 `ACCEPT`（`skill_server_main.cpp:153-157`），但底层 SDK goal 从未被取消；
- Executor 忙时新技能一律 `skill.busy`（`executor.cpp:186-191`），`stop` 无法打断正在跑的 `navigate_to`/`takeoff`。

**后果**：agent 能表达的「停止」是一个必然失败、且无法中断运行中技能的 token。急停只能绕开技能层直接打 runtime 的 `motion/stop`——那已经不是 harness 的能力了。测试里也没有 `stop` 用例（`skill_layer_tests.cpp` 9 例中无 stop）。

### P0 — LLM 返回体类型异常会让 agent 进程整个 abort

`LlamaClient::chat` 对响应体只有一处 `try`（解析 `raw_arguments`），其余取值都是**无保护的类型转换**（`chaos_agent/src/llama_client.cpp:112-121`）：

```cpp
const auto & message = choices.front().value("message", nlohmann::json::object());
...
const auto & function = call.value("function", nlohmann::json::object());
tc.name = function.value("name", "");
tc.raw_arguments = function.value("arguments", "");
```

nlohmann 的 `value()` 在字段存在但类型不符时抛 `type_error.302`。只要服务端把 `arguments` 返回成**对象**而不是字符串（llama.cpp 正常返回字符串，但代理层/模型 quirk 都可能踩到），或 `name` 不是字符串，就抛出未捕获的 `type_error`。

异常逃出 `chat()` → 逃出 `AgentLoop::run_once()` → 逃出 `agent_main.cpp:70` 的分离线程（无 catch）→ `std::terminate()` → **agent 进程直接 abort**，而不是退避重试。同理 `choices.front().value("message", ...)` 在 `message` 是字符串时也会抛。

这正好落在 §7 P2「LLM 调用失败只 warn 不退避」的另一面：有些失败根本走不到 warn。

### P1 — ingress 在准入时用「当前 hub」校验，排队期间 hub 变了就会误拒

`on_intent` 在**准入**时用当时的 hub 做 `check_entry`（`intent_ingress.cpp:182-190`），但意图真正执行是在 worker 线程上、可能晚于其它已排队意图。安全侧没问题——`Executor::execute` 会用执行时刻的 hub 重新校验（`executor.cpp:201-206`），权威在执行侧。

问题在**可用性**：一个当时合法的意图，可能因为排在它前面的意图改变了 hub 而在准入时就被拒（`skill.hub_mismatch`）。准入检查因此是**顺序敏感**的——同样的意图序列，换个到达顺序结果就不同。规划层看到的是「我的下一步被拒」，而不是「我的下一步需要等前面的技能跑完」。

### P1 — `skill_intent` 是无鉴权的运动命令通道

`IntentIngress` 原样接收发布者给出的 `control_token` 与 `enforce_hub_precondition`（`intent_ingress.cpp:270-271`），并据此决定是否取租约、是否执行。后果：

- 任何能向 `skill_intent` 发消息的 ROS 节点，只要把 `enforce_hub_precondition` 置 `false`、`control_token` 留空，就能让技能层**自持租约**执行任意运动 token——**无凭证、无枢纽约束**；
- 反过来，拿到一个有效租约令牌的一方可以顶着该租约执行动作。

这一层集中了平台全部运动入口（这是 §4.5 的设计目标本身），但契约里没有为「谁有资格发 intent」留任何位置。ROS 2 的信任域假设在单机上成立，在**多机**场景下就要显式处理。

**建议**：在 ingress 侧加一个来源白名单/签名，或至少让 `control_token` 的来源与 `enforce_hub_precondition` 的放宽权限绑定到同一个受信来源。

### P1 — 词表里的 `hold` 与 agent 的控制 token 撞名，`tools` 数组出现重名函数

`AgentLoop::build_tools()` 先遍历词表 skills 生成 `skill_<token>`（`chaos_agent/src/agent_loop.cpp:136`），**再**追加控制 token `skill_hold` / `skill_done`（`:151, :159`）。而出厂词表里本身就有 `token: "hold"` 的技能。

用出厂词表复现：

```
tools = [skill_takeoff, skill_land, skill_navigate_to, skill_stop,
         skill_observe, skill_announce, skill_hold, skill_hold, skill_done]
                                  ^^^^^^^^^^^^^^^^^^^^ 重名
```

OpenAI 兼容的 function schema 要求函数名唯一；llama.cpp 的 grammar 转换对重名函数的处理不确定（报错 / 取其一 / 覆盖）。

同一处矛盾也写在头注释里：`agent_loop.hpp:37-39` 声称「`hold` 和 `done` 都是控制 token，**不会**到达技能层」，但代码只特判了 `done`（`agent_loop.cpp:199-202`）——`hold` 被当作普通技能意图发给技能层，由词表里那个 `hold` 技能执行。**注释与实现不一致，且正是重名问题的另一面。**

### P1 — 词表声明的 capability 家族**不驱动调度**

`vocabulary.cpp:83-89` 强制每个 skill 必须声明并校验 `capability`，`planner_document()` 也把它暴露给 planner（`vocabulary.cpp:119-136`，且有测试守着）。但 Executor 的分派是**硬编码的 token 字符串 if/else**（`executor.cpp:218-232`）——`grep '->capability' executor.cpp` 无命中。

后果：改一个 skill 的 `capability` 值不改变任何行为；新增一个技能要同时改三处——词表、Executor 分派、`Capability` 枚举。词表头注释宣称的「词表是唯一说明 token 最终调用哪个基础功能的地方」（`vocabulary.hpp:41-46`）与实现不符。`capability_tests` 里只有解析与暴露的用例，没有「capability 决定分派」的用例——说明当前意图也只是「声明」而非「驱动」。

### P1 — `Announcement` 没有订阅者，`announce` 技能「成功」但消息进虚空

全 `src/` 搜索：`chaos_skill` 之外**没有任何 `Announcement` 订阅者**；`communication/announcement` 只出现在 launch 参数里。`chaos_mqtt` 网关桥接的是 `skill_intent`/`skill_event`，**不是** announcement。

`ServerPorts::announce` 只 publish 不等回执，无论有没有接收方都返回 `success`（`server_ports.cpp:66-70`）。`docs/agent-skill-layer.md` 称「当前由 MQTT 适配器订阅本话题并转发到 broker」——这个适配器在树里不存在。

### P1 — agent 的 hub 视图只在成功时更新，首轮为空，且没有任何外部校正路径

- `agent_loop.cpp:224-225`：`if (event->success) hub_ = event->hub_state;`。失败事件里带着**正确的** hub_state 却不采纳。
- 首轮 `hub_` 为空 → `snapshot["current_hub"] = null`（`agent_loop.cpp:106`）。模型无从知道当前 hub。
- 唯一自愈路径是模型恰好选 `hold`（`any_entry_hub` + 恒成功）→ 事件带回当前 hub。模型选错 → 失败 → hub 不更新 → **视图卡死**，只能靠模型碰巧再选 hold。
- 外部校正路径不存在：`Executor::set_hub`（`executor.hpp:86`，注释说「遥操作接管后强制改 hub」）**全仓无调用方**。技能层无法感知资产被外部动作改变了自己的枢纽。

### P1 — 失败即回退到入口 hub，但物理状态未必回退

`Executor` 的失败策略是「hub 保持进入时的值」（`executor.cpp:250-251`）。词表头注释把这解释为「对运动技能而言，受控停机后就是入口 hub」（`executor.hpp:55-58`）——但这只在**受控停机**成立。

`takeoff` 失败在离地之后、或 `navigate_to` 失败在半路，物理状态都可能已经不是入口 hub；而运行时上报的 `RobotState` 里**没有 hub 概念**，技能层无从核对（`run_observe` 只把状态当 payload 回传）。于是 hub 可能永久停在错误的分支上，`land`（`entry_hub=airborne`）这类技能再也发不出去——hub 一旦错位，合法技能序列就断了，且没有任何复位路径。

### P2 — 固定 30 秒技能超时会让 agent 连锁卡死

`kSkillResultTimeout = 30s` 是硬编码的（`chaos_agent/src/agent_loop.cpp:13`），与技能本身的性质无关。一次长导航、或一个排在别的意图后面的意图，很容易超过 30 秒。

超时之后：`await_terminal` 返回空 → agent 只打一条 warn 就返回 `true`（`agent_loop.cpp:217-220`）→ 下一拍**再发一条新意图**（新 request_id）。而原来那条可能还在跑或还在排队 → 技能层回 `skill.busy` → agent 再发 → 再 `skill.busy`……

结果是 agent 进入「不停发意图、每条都被拒」的循环；被超时放弃的那条意图的终态事件稍后到达，还会永久堆在 `terminal_` 里（见下条）。

### P2 — agent 每次重启从 1 重来计数，可能命中技能层的重放缓存

`request_id = asset_id + ":agent:" + ++sequence_`（`agent_loop.cpp:211`），`sequence_` 每个进程从 0 开始。agent 重启（技能层仍在跑）后第一条意图会拿到 `"<asset>:agent:1"`——这个 id 可能还在技能层 `finished_` 的 256 条重放窗口里 → 技能层**直接重放上一次的终态事件**而不执行技能（`intent_ingress.cpp:158-163`）。

agent 于是把一次**根本没执行**的技能当成成功，并把陈旧的 hub 写进自己的视图。

### P2 — 终态事件从不回灌给模型，planner 对失败是瞎的

`build_user_content` 只放 `asset_id`/`current_hub`/`task`/`state`（`agent_loop.cpp:102-127`），**不含上一步的结果**。模型永远不知道「刚才的 takeoff 失败了，code 是 X」——它只能从 hub 变化里间接猜。

配合 §7 P1「失败不更新 hub」，模型在失败后拿到的是与失败前**完全相同**的快照，于是可能重复选同一个注定失败的技能。

### P2 — agent 的分离线程捕获 `this`，关机时可能 use-after-free

`agent_main.cpp:70` 的 `std::thread([this]{ ... }).detach()` 捕获了节点指针。`rclcpp::spin()` 返回后节点被销毁，而线程可能还阻塞在 `run_once()` 里（最长 30 秒的 `await_terminal`）→ 悬垂指针 + `timer_->cancel()` 打在已析构的对象上。

### P2 — `any_entry_hub` 技能在 tool 描述里被告知了错误的入口 hub

`build_tools` 用 `"[" + entry_hub + " -> " + exit_hub + "] " + description` 拼描述（`agent_loop.cpp:137-139`）。对 `hold`/`stop`/`observe`/`announce` 这些 `any_entry_hub: true` 的技能，这写的是**声明值**（例如 `observe` 写 `[idle -> idle]`），而它们实际可以从任何 hub 发起。

模型因此被告知「observe 需要在 idle 状态」，在 airborne 状态下会拒绝使用它——而这恰恰是最需要观察的时候。`planner_document()`（`vocabulary.cpp:119-136`）也没有导出 `any_entry_hub` 与 `needs_lease`，planner 看不到这两个语义。

### P2 — `skill_ns` 用相对名比较绝对命名空间，会拼出双重命名空间

`agent_loop.cpp:59`：`skill_ns == node_->get_namespace()`。`get_namespace()` 返回**绝对**名（`/vehicle_001`），而 `skill_ns` 若传相对名（`vehicle_001`），比较必然不等 → 前缀被拼上 → 话题解析成 `/vehicle_001/vehicle_001/skill_intent`。默认值 `""` 不触发，所以目前没暴露。

### P2 — 关闭时静默丢弃排队中的意图，来源永远收不到终态

`IntentIngress::run` 的循环条件是 `while (running_.load())`（`intent_ingress.cpp:226`）。`stop()` 把 `running_` 置 false 后，worker 退出循环，队列里**剩余的意图既不执行也不发终态事件**就被丢掉。来源方只能等到自己的超时。

（执行中的那一条会跑完并正常发终态——所以问题只在「排队中」的那部分。）

### P2 — ingress 的 `queue_capacity` 实际上够不着

准入顺序是「先看 `executor_.busy()` → 忙则直接拒，再看队列容量」（`intent_ingress.cpp:192-211`）。只要有技能在跑，新意图就被**拒**而不是**排队**，所以队列几乎只在「executor 空闲但 worker 还没取走」的那一瞬间有元素。默认容量 8 名不副实，实际吞吐上限约等于 1。

### P3 — 几处小问题

- `IntentIngress` 的 worker 线程没有顶层异常保护（`intent_ingress.cpp:226` 的 `execute(intent)` 未被 try 包裹）：逃逸的异常会 `std::terminate()` 掉整个技能服务器。
- `IntentIngress::replay()`（`intent_ingress.cpp:94-107`）是死代码——`on_intent` 内联做了同样的查找。
- `ServerPorts::announce` 的 `if (!publisher_)` 分支（`server_ports.cpp:54-58`）不可达：`publisher_` 在构造函数里必然创建成功。
- `AuxReading::stamp_ns` 注释写「接收时刻」，实际存的是消息头时间戳（`capability.hpp` vs `server_ports.cpp:106`）。
- 终态事件先写进重放缓存再发布（`intent_ingress.cpp:293` vs `:294`），这个窗口内到达的重试会拿到**同一个终态事件两次**——幂等，可接受。
- 「相同 request_id 不重复执行」是有界的：重放窗口 256 条 FIFO 逐出（`intent_ingress.cpp:121-125`），窗口滑过后同一个 id 会再执行一次。这是已声明的取舍，不是缺陷，但调用方不能把它当绝对保证。

### P2 — 三个契约里有两处 `expires_at` 声明了但服务端从不读

`ExecuteSkill.action` 的 goal 和 `SkillRequest.srv` 的 request 都带 `expires_at`，但技能层只在 topic 路径上检查它（`intent_ingress.cpp:141` 是全包唯一一处 `expires_at` 引用；`grep expires_at skill_server_main.cpp` 无命中）。

后果：走 action/service 路径的调用方即使发一个**已经过期**的请求，`skill_request` 也会回 `accepted=true`，action 目标也会被 `ACCEPT_AND_EXECUTE`。

### P2 — 同一个消息族里混用两套时间基准

- `SkillIntent.expires_at`：Unix UTC，用 `system_clock` 比较（`intent_ingress.cpp:20-30`）——正确。
- `SkillToken.decided_at`：`node_->now()`（ROS 时钟，`agent_loop.cpp:209`；网关同）。
- `Announcement.stamp`：`node_->now()`（`server_ports.cpp:65`），但 `Announcement.msg` 注释写的是「发布时刻（Unix UTC）」。

`use_sim_time:=true` 时三者分叉：截止时间按真实时间判，通告时间戳是仿真时间。

### P2 — agent 的终态事件表无界增长

`on_event` 把**所有**终态事件按 request_id 存入 `terminal_`（`agent_loop.cpp:83`），只有自己 `await` 的那个会被 erase（`:97-98`）。同一资产 namespace 内其它来源（MQTT 网关）产生的事件永远堆积。

### P2 — `last_state_` 跨线程无锁读写

订阅回调线程写 `last_state_`（`agent_loop.cpp:68`），worker 线程在 `build_user_content` 里读（`:109-120`）。`std::shared_ptr` 的赋值/读取非原子 → data race。

### P2 — LLM 输出的坏参数被静默吞掉

`llama_client.cpp:126`：`tool_call.arguments` 解析失败 → 置为空 object。模型给出一段坏 JSON 会退化成 `skill_args.missing`，而不是「模型输出非法」。

### P2 — 5 秒 HTTP 超时对机载小模型偏紧

`llama_client.hpp:54` 默认 5000ms。Orin 上 4B 模型首轮（prompt 处理 + tool-call 解码）容易超 → `transport.request_failed` → agent 只 warn 后下一拍重试（`agent_loop.cpp:186-190`），不降级也不退避。

### 结构性观察

- **`docs/architecture.md` 里完全没有这一层**（grep 无命中）。唯一说明是 `docs/agent-skill-layer.md`，它描述的是重构**前**的形态——文档已落后于工作区（尤其是它宣称 MQTT 适配器订阅 announcement、以及接口集只有 SkillToken/SkillRequest/ExecuteSkill）。
- **`Vocabulary::planner_document()` 与 agent 侧的 tool schema 各写一遍契约形状**：planner_document 在 `vocabulary.cpp:119`，有测试；agent 在 `agent_loop.cpp:136` 自己从原始 JSON 遍历重建。两侧互不知道对方加了 `hold`——这正是 P1「重名工具」的温床。
- **`Executor::set_hub` 是死代码**，外部状态漂移无校正手段。
- **测试覆盖已经不错**（词表 8 例 + 技能层 9 例，含租约生命周期、hub 前置条件、调用方自带令牌、直接命令不受 hub 约束、observe/announce、ingress 生命周期、request_id 幂等、过期拒绝）。缺口集中在：**`stop` 技能、取消语义、announcement 的送达、`announce` 的下游消费方**。
- **`chaos_agent` 与 `chaos_skill` 之间零编译期依赖**，只靠词表文档 + 线协议耦合——最值得保留的决定。

---

## 8. 建议的修复顺序

1. **P0（agent 崩进程）**：给 `LlamaClient::chat` 的响应体解析加类型检查——所有 `value()` 换成带 `is_string()`/`is_object()` 判断的取值，异常兜住转成 `response.invalid_json`。同时给 `agent_main` 的分离线程加异常捕获，避免任何逃逸异常 terminate 进程。
2. **P0（stop 不可用）**：让规划路径的 `stop` 走通。两个方向：(a) `stop` 声明 `needs_lease=true`，技能层自持租约执行 stop；(b) 给 SDK 一条「无令牌急停」通道（runtime 的 `guard_.require(request.lease_token)` 目前强制鉴权，`chaos_runtime/src/coordinator.cpp:429`）。同时把 `cancel_requested_` 接进 SDK goal 的 cancel 请求，并允许 `stop` 打断运行中的技能（现在是 `skill.busy`）。补一个 `stop` 用例。
3. **P1（鉴权）**：给 `skill_intent` 加来源鉴权，或把 `control_token`/`enforce_hub_precondition` 的放宽权限绑定到受信来源。
4. **P1（重名工具）**：消除 `skill_hold` 重名——控制 token 与词表技能统一命名空间（例如控制 token 用 `agent_hold`/`agent_done`，或从词表里删掉 `hold`）；同时修正 `agent_loop.hpp:37-39` 的注释。
5. **P1（capability 不驱动分派）**：把 Executor 的 token if/else（`executor.cpp:218-232`）换成「capability → 分派表 + token 参数」的两级映射。
6. **P1（announcement 无出口）**：要么补一个 `Announcement` 的 MQTT 适配器，要么把 `announce` 技能降级为「仅发布、无成功语义」。
7. **P1（hub 视图）**：agent 无论成败都从事件里同步 hub；把 `set_hub` 接起来（外部动作改变资产状态时校正枢纽）。
8. **P1（准入顺序敏感）**：把 hub 前置条件的裁决统一收到执行侧，准入只做词表存在性与容量检查。
9. **P2（agent 循环）**：把 30 秒固定超时改成按技能/按队列位置推导，或在超时后**不**发新意图而是先查询在途意图；`request_id` 加进程/epoch 前缀；把上一步的终态结果回灌进 prompt；`build_tools` 对 `any_entry_hub` 技能给出正确描述，并把 `any_entry_hub`/`needs_lease` 导进 planner document。
10. **P2（关机与队列）**：ingress 停机时给排队中的意图补发终态事件；`queue_capacity` 要么真正用于排队（把 busy 检查放到出队时），要么改名说明它是「准入并发上限」。
11. **P2（时钟与生命周期）**：统一时间基准；给 `terminal_` 加容量上限；给 `last_state_` 加锁或改用 atomic shared_ptr；让 action/service 路径也检查 `expires_at`；修 `skill_ns` 的相对/绝对比较。
12. **P2（杂项）**：区分「LLM 输出非法」与「参数缺失」；放宽 HTTP 超时并加退避；worker 线程加顶层异常保护；清理 `replay()`、`skill.no_communication` 死代码；修 `AuxReading::stamp_ns` 的语义。
13. **文档**：把 `docs/agent-skill-layer.md` 更新到当前契约（`SkillIntent` 新增 `enforce_hub_precondition`/`control_token`，MQTT 桥接的是 skill_intent/skill_event 而非 announcement），并把这一层补进 `docs/architecture.md`。

