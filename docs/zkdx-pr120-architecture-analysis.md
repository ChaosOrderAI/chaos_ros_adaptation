# zkdx pr120 架构分析

**分析基准**：`/home/yunze/zkdx-pr120`，`pr120` 分离头指针 `6ed7779ba`（2026-11-20）。远程是 `ChaosOrderAI/zkdx`。产品名是 AI-native SwarmBrain，代码根目录仍叫 `reshake_2_0`。

同远程还有一份更早的检出在 `/home/yunze/chaos_ros_control/zkdx`（进程内黑板协同，README 自称为 bsl-v1.5）。那不是现行代码，本文只在最后一节说明它和今态的差别，不拿它的行号当证据。

**方法**：六个读者分别读 harness / Skill Engine、world 契约、三个 Target、工作台与 Gateway、设备最后一跳、TraceLab。综合之后又做了一轮「只保留有代码证据的判断」。下面带行号的结论，预览入口、Lingao broker、Origin 策略、Gateway 的裸 `/api/v1`、黑板主题表、工具目录约束和 `REAL_ENABLED` 默认值，都对照过源码。其余中低风险保留读者给出的行号。

---

## 1. 一句话定位

这是**任务侧的多进程运行时**，不是机载 ROS 节点，也不是 Go2 运控或 PX4 飞控。

- **Harness** 在每个 Target 进程里养常驻的 Brain 和 Edge 子进程，并持有任务权威。
- **World** 把指派、征求和物理效果收成工具；物理副作用只能从 Edge 的效果工具出去。
- **Target**（`the_world` / `gazebo` / `real`）决定效果落在虚拟引擎、Gazebo 还是真机。
- **工作台**是浏览器入口。它能发控制请求，但不是任务权威。
- **TraceLab 和 Skill Engine 学习进程**不在 Mission 热路径上。回放不驱动设备。

真机和仿真的最后一跳在车端桥、ROS embodiment 和仓库外的无人机网关里。

---

## 2. 分层

| 层 | 代码 | 拥有 | 明确不做 |
|---|---|---|---|
| 工作台 / Gateway | `reshake_2_0/apps`，`services/gazebo_gateway` | 浏览器、端口清单、Target 代理、Skill Hub 编辑入口 | 不持有 Mission 权威；浏览器不能指定上游 |
| Target 组合根 | `reshake_2_0/src/sim2real` | `ai-native-target --target` 三选一；虚拟引擎、Gazebo 动作日记、Real 执行开关 | CLI 自己不执行动作。`the_world` 不接 ROS/MQTT 执行器；gazebo 不建虚拟场景 |
| World | `reshake_2_0/src/ai_native_world` | 工具目录、协调 bind、动作日志、设备租约、embodiment 契约、态势投影 | 不另起一套任务权威；效果适配器只能住在 `E_A_` |
| Harness | `reshake_2_0/src/ai_native_harness` | 冻结团队、唯一活跃 Mission、常驻 Brain/Edge、黑板读模型、命令出站、终态屏障 | 不导入 World；关 Session 不停进程；不把未知物理结果再 execute |
| 设备最后一跳 | `packages/go2_edge`、`packages/robot_access_protocol`、`sim2real/real`、`sim2real/ros`、`uav_gcs` | 准入账本、入口围栏、地面 ROS、无人机控制令牌 | 已准入的效果失败不重放；转发成功不等于车已经执行 |
| 离线旁路 | `src/skill_engine`，仓库根 `tracelab/` | 学习 Worker、封存归档到 ClickHouse、只读回放 | 不进 Mission 热路径；回放不执行工具 |

包与包之间的 import 方向由 `reshake_2_0/scripts/check_architecture.py` 在 CI 里卡：harness 不得导入 world / sim2real，world 不得导入具体 Target（`sim2real.config` 除外）。**包内部的层 DAG 没有同等门禁。** 文档写的「只有 `harness_adapter` 能导入 harness」「contracts 只依赖标准库」与代码不符，见第 7 节。

---

## 3. 运行拓扑

`scripts/start-linux.sh` 只接受 `world|real|gateway|all|stop`，然后交给 `apps/web_operator/scripts/dev.mjs`。**`all` 不启动 Gazebo Target。**

默认端口（`reshake_2_0/src/sim2real/config/domains/services.py`）：

| 进程 | 默认 | `all` 是否拉起 |
|---|---|---|
| 预览 Vite | `0.0.0.0:5177` | 是 |
| Gateway | `127.0.0.1:8880` | 是 |
| The World | `127.0.0.1:8787` | 是 |
| Real | `127.0.0.1:18794` | 是 |
| Skill Hub | `127.0.0.1:8791` | 是 |
| Gazebo Target | `127.0.0.1:8788` | 否 |

operator 模式下，预览的代理不是一条路（`apps/web_operator/vite.config.ts`）：

- `/targets`、`/api/operator` → Gateway `:8880`
- `/api/skill-hub` → Skill Hub `:8791`
- `/api/v1`、`/media`、`/surface` → **直连 Real `:18794`**，并且 `changeOrigin: false`（保留浏览器的 Host / Origin）

Gateway 上的同名裸路径 `/api/v1/*` 是另一条契约：它探测的是 SwarmBrain，失败文案写明 `Mission API requires SwarmBrain target=gazebo`（`services/gazebo_gateway/server/app.py:1082`）。所以 **`:5177` 的 `/api/v1` 是 Real，`:8880` 的 `/api/v1` 是 Gazebo**。文档把两者都叫「工作台」时，这两条路会被当成同一条。

每个已经启动的 Target 进程内部：

```text
ai-native-target --target the_world|gazebo|real
  └─ HarnessRuntime（库对象，没有自己的 console script）
       ├─ multiprocessing spawn  Brain ×1     空闲只堵 IPC，关 Mission 不杀
       ├─ multiprocessing spawn  Edge  ×N
       └─ 同进程线程：activation driver、黑板 lane、终态监视
  the_world：虚拟引擎，不接 ROS/MQTT 执行器
  gazebo  ：自己 new HarnessRuntime；地面 ROS，飞行发 EGO 目标
  real    ：REAL_ENABLED 默认 false；执行器不开
```

旁路，不在任务热路径上：

- `skill-engine` worker：只有启动器配置了学习引擎才拉，Harness 不 import、不拉起。管理 HTTP 只绑 loopback。
- TraceLab：启动器可以带上，失败不阻断 Target。查询只读。
- `gazebo/` 目录本身是 Docker 实验编排，不是设备控制实现。设备控制在 `sim2real/gazebo` 和 `sim2real/ros`。

---

## 4. 承重机制

### 4.1 常驻 Agent，同一时间一个 Mission

一个 Runtime 必须恰好一个 Brain、至少一个 Edge；`start` 之后团队冻结，不能再注册 Agent。每个 Agent 是一个活到 Runtime 关闭的子进程。打开 Mission 只挂上这一次的 checkpoint 和 Skill，关闭 Session 拆掉这些引用，不停进程。

同一 Runtime 同时只能有一个活跃 Mission，槽位先占上再启动组件，冲突是 `ACTIVE_MISSION_EXISTS`。后端重启是新的 runtime id，**不恢复**上一进程里没做完的 Mission，只把没清理干净的旧 Runtime 投影成 `INTERRUPTED_UNCLEAN`。

子进程接受 Session 时只核对显式的 runtime / agent / profile_id / revision / role，不重算 Profile 内容哈希。

### 4.2 黑板是事件账的读模型

权威事实在 SQLite Mission 事件账里。黑板是这本账的投影，不是第二份 append-only 日志。

主题在代码里是闭合的 **10** 个（`ai_native_harness/blackboard/contracts.py:40`），不是文档写的 8 个。多出来的是 `execution.tool.progress` 和 `operator.input`：

- `situation.partition.definition` / `situation.partition.state`
- `device.status`
- `mission.state` / `plan.state` / `task.state`
- `execution.tool.progress`
- `alert.event`
- `operator.input`
- `social.relation`

分流（`classify_lane`，同文件 173 行）：`mandatory` → URGENT；非强制的 `alert.event` → NORMAL；其余 → SILENT。人工输入映到 `operator.input` 且强制 URGENT。URGENT / NORMAL 会唤醒决策；NORMAL 限流只推迟唤醒，不确认、不丢事实；SILENT 默认不叫模型。mandatory 可以无视可变订阅，但不能穿过冻结 Profile 的主题边界和来源黑名单。

上下文在**父进程**组装，而且只在这一拍是 `DECIDE` 时组装一次，不追加进对话。Brain 看到的舰队拓扑、Edge 看到的分区，来自产品的 Context provider，不是 harness 自己算出来的。

### 4.3 协调在 Harness，物理效果只能从 Edge 出去

工具目录的两级字母是固定的（`ai_native_world/tooling/registry.py:60`）：

- 第一字母：`B` Brain、`E` Edge、`G` general
- 第二字母：`C` 集中式、`D` 分布式、`A` 效果、`G` 通用

集中指派只在 centralized 模式，分布式征求只在 distributed 模式。World 只 bind，状态机在 harness。

物理效果还有三道目录级约束：`physical_action` 必须有 `spatial_kind`；目录名必须等于 `tool_id`；异步/观测/同步效果适配器只能住在 `E_A_`。需要任务权威的世界命令必须是 Edge，并带未过期的 claim、grant、lease。Brain 不在这条名单上。

导航要声称成功，必须有**同一任务**的终态成功运动回执。观测和模型自述不能当完成。`partial` 只记一条消息，不把任务改成成功。

### 4.4 命令出站：幂等，结果不明就失败闭合

子进程把命令写入 `command_outbox`（`command_id` + 语义指纹）。同一 id 指纹不同则冲突。父进程只有新预留才真正 `execute`，已经有执行行的只按指纹 reconcile。

物理结果说不清时记 `COMMAND_EXECUTION_OUTCOME_UNKNOWN`，不把它变成一次可重试的工具失败。没有设备级隔离证明时，Runtime 失败闭合。第一次进入终态之前要先过产品的 terminal barrier；这道屏障如果要做产品 I/O，不占着 Authority 锁，失败则终态事实不提交。

设备侧同一条原则再出现一次：运动前先把准入写进账本（`robot_access_protocol` 的 ledger 先 INSERT 再执行）；执行函数抛错只回 `outcome_unknown`，不重放。入口不健康、还有旧命令没了结、或正在恢复时，拒绝新运动，停止类例外。车端急停不能绕开这本账。

### 4.5 三个 Target 共享权威，分叉执行

安装入口是 `ai-native-target --target the_world|gazebo|real`。`the_world` 和 `real` 共用 `ProductHarnessAssembly`。gazebo 不走这条装配，在 `GazeboHarnessApplication` 里直接构造 `HarnessRuntime`，也不建虚拟世界。

| | the_world | gazebo | real |
|---|---|---|---|
| 世界 | 虚拟引擎 `navigate` / `follow_path` | 不建虚拟场景 | 真机观测面 + 可选执行器 |
| 执行 | 进程内 | 地面 ROS service；飞行发 EGO 目标再进 PX4 | `REAL_ENABLED` 默认 **false**（`config/domains/real.py:36`） |
| 失败时 | 启动失败会门住，同一实例不能重启 | `running` 回执不能当成成功；停机见风险 | 未显式 enable 时拒绝连接。缺 MQTT 配置会让整个进程起不来，不是「只剩观测面」 |

解除隔离或恢复重启之前，历史上没终态的动作必须有 `resource_safe` 证明，否则 `DEVICE_CONTAINMENT_NOT_SAFE`。

真机产品目录是固定的六台：`robot-dog-1/2/3` 和 `vehicle-1/2/3`（`sim2real/real/world/live_fleet.py:23`）。README 里 Gazebo 舰队写成两辆 Lingao、两台 Go2、两架 PX4，那是仿真组合，和这张真机目录不是同一份名单。

### 4.6 Trace 与学习都在热路径外面

未封存的归档不进 ClickHouse。导出先写 events / missions / spans，**全部 ACK 之后**才写 `trace_publications`；查询只认这一代。导出是场景进程里的后台线程，`trace_warehouse.enabled` 默认关，失败不改执行库，也不堵住 Mission。

TraceLab 的回放把历史指令填成草稿，要人再点一次才创建。复测不执行工具、不驱动设备。

Skill Engine 的学习 Worker 是另一个进程。租约过期的未完成模型轮标成 uncertain 失败，不自动重放。读到已发布技能不等于拿到设备执行权。Harness 里那个同名的 SkillRuntime 只是 Profile 上的指令技能，不增加 Tool 权限。

---

## 5. 设备最后一跳

三条路，不要混：

1. **Go2 / Lingao 真机**：世界侧 MQTT，进车端 `go2-access`。导航命令从 `go2_mqtt` 发出，不是笔记本上的 rosbridge。`fleet.py` 开头的注释仍写 rosbridge `:9090` 和 live `:8091`，和导航发送路径不是同一句话。生产 Real **不**把 ROSBridge 当执行器。
2. **Gazebo 地面**：`sim2real/ros` 的 embodiment，ROS service / 路点。取消在发出取消之后就返回 `cancelled`，不等设备终态回执。
3. **飞行**：Gazebo 里发 EGO `PoseStamped` 再交给 PX4。真机无人机走独立的 `uav_gcs`（默认不往飞机发布，起飞/航线/降落要未过期的 `control_token`）。地面站命令集里没有 `flight.brake`；`--land-command` 默认 `flight.land`，对齐文档写明这条降落命令还没被厂商文档确认。

`gazebo/` 目录（Docker 实验）不共享宿主网络、进程和内存命名空间，也不挂 Docker socket。它隔离的是实验进程，不是上面这三条控制路径。

---

## 6. 风险

### 高：预览默认把 Real 控制面露在鉴权之外

`services.preview` 默认听 `0.0.0.0:5177`。operator 预览把整段 `/api/v1` 转到本机 Real，且不改写 Host。Real 的 HTTP 中间件做 Origin 检查时 `required=False`：**没有 Origin 头就放行**（`ai_native_world/transport/middleware.py:67`，`origin.py:48`）。`origin.py` 第一行写明：这是浏览器 Origin 策略，**没有操作员鉴权**。

Real 自己的监听默认是回环，`allow_remote` 默认关，所以不是 `:18794` 直接对局域网开放。暴露面是预览代理：能连上 5177 的客户端可以不带 Origin 去打 Real 的 `/api/v1`。工作台遥控和车上 bringup / 停止走的就是这条路径，而且 Real 写操作不经过界面上的 `mutationsEnabled` 门闩。

物理执行器仍受 `REAL_ENABLED` 默认关闭约束。端口可达不等于电机一定动，但控制面本身没有第二道身份。

### 高：Lingao 兼容 broker 默认匿名，且默认绑全地址

`MqttBroker.allow_anonymous` 默认 `True`（`lingao_platform.py:136`）。连接时没有用户名、又允许匿名，就 CONNACK 成功（同文件 271 行）。`serve_lingao_8090.py` 会要求密码环境变量已经配置，但构造 broker 时**没有**传入 `allow_anonymous=False`。`real.vehicle.broker_bind` 默认 `0.0.0.0`（`config/domains/real.py:156`）。

`start-linux.sh` 的 `all` 不拉这个服务。风险发生在有人把 8090 兼容服务拉起来之后：匿名客户端可以往车辆命令话题发布，绕过世界侧的 MQTT 围栏。同一服务的 WebSocket 一接入就签发 session token，`/api/command` 只校验这个令牌，监听地址也没有强制回环。

### 中：取消被说成已经取消

地面 ROS 在 publish 取消之后立刻返回 `status=cancelled`，不等设备终态（`sim2real/ros/embodiment.py`）。飞行取消在里程计缺失、悬停目标都没发出时，仍返回已取消（`sim2real/gazebo/flight.py`）。Harness 对「结果不明」的要求是失败闭合、不许当成功。这两套回执在代码里没有汇合点，上层有可能把合成的 `cancelled` 当成物理已经停。

### 中：几何门对缺省参数放行

没有 `scope_policy` 的工具，世界几何准入直接返回通过。`move_to` 只给了分区 id、没给坐标时，缺的 x/y 会按契约默认成 `0.0`，也就是朝原点走。`follow_path`、起飞和无人机航线没有这道几何门。

### 中：诊断通道和停机窗口

Gateway 的 `/api/move` 不看 Mission，也没有诊断确认；读不到 runtime 时仍继续下发。Gazebo Target 停机最多等 uvicorn 线程 10 秒就 `close`，该线程还是 daemon，不确认监听已经退出，也不等动作终态。Gazebo 的能力检查器在安装之前默认放行，而安装发生在 `recover_unsettled` 之后，启动早期的回收窗口是 fail-open。

### 中：视觉调用堵住同一执行器上的取消

单动作下发持有执行器锁时，同步调外部视觉模型（目标匹配），超时最长约 60 秒。同一执行器上的交接和取消要等这次调用回来。

### 低

- Skill Engine 的管理 POST 只认固定头 `x-skill-hub-client: editor-v1`，没有操作者身份。绑定限制在 loopback，所以是同机任意进程能改技能库，不是远程入口。发布技能不等于驱动设备。
- 动作执行器没注入资源租约库时，单动作仍会下发；批处理才会拒绝。生产组合根有注入，库本身是 fail-open。
- ClickHouse 传感器库的凭据文件对组或其他人可读时只打日志，仍用该密码连接。这是分析库，不是控制通道。

---

## 7. 文档和代码不一致

这些是文档失实，不是两份代码互相打架。

| 文档 | 代码 |
|---|---|
| 黑板闭合为 8 个主题（`ARCHITECTURE.md`） | `TOPIC_CATALOG` 有 10 个 |
| 发行包只暴露 `ai-native-target` | `pyproject.toml` 还安装 `skill-engine` 和 `skill-trace` |
| 只有 `harness_adapter` 能导入 harness；contracts 只依赖标准库；`check_architecture.py` 拒绝一切未声明边 | 脚本只查**包间**边界。`contracts`、`application`、`projection`、若干 Tool 都直接 import harness。包内还有 tooling→situation、action→tooling、application→transport 这类反向边 |
| FastAPI 组合根在 `ai_native_world/transport` | uvicorn 在 `sim2real/the_world/transport`；gazebo / real 各自有组合根 |
| operator 的 Real 只走 Gateway 的 `/targets/real` | operator 的 Real 基址是裸 `/api/v1`，由 Vite 直转 `:18794` |
| 没有执行配置时 Real 仍可看观测 | 缺 MQTT 配置时进程直接失败，不是留下观测面 |
| `--skill-root` / `SIM2REAL_SKILL_ROOT` | 这些键会 `ConfigError`；现行开关是 `--skill-library-root` |
| Gateway 启动脚本会建 Tailnet TCP 中继 | `start-linux.sh` 和 `dev.mjs` 都不调用 `tcp_relay.py` |
| 浏览器不发 `cmd_vel` | 代码确实拒绝 `cmd_vel`；`vehicle-drive` 页面文案仍写「直接发 /cmd_vel」，实际走的是 `/api/v1` teleop |
| TraceLab 页面点击会 `POST /api/runs` | 服务端还挂着 RunManager，主页面构建没有这个入口 |

---

## 8. 还没在代码里闭合的点

下面是读者之间对不上、或者根本没人覆盖到的缝，不是已证实的缺陷。

- **取消语义**：合成的 `cancelled`、`COMMAND_OUTCOME_UNKNOWN`、以及「`running` 不得当成功」分属设备、harness、gazebo，没有一个落点说明取消回执最终算哪一类。
- **急停闭环**：工作台 `dog_stop`、车端账本、`control_pool`、无人机没有 `flight.brake`，没有接成「是否进入 Harness 终态」。
- **重启之后操作员看到什么**：`INTERRUPTED_UNCLEAN`、解除隔离要 `resource_safe`、脏 ingress 拦新运动，这三件事没有合成一个操作员可见状态。
- **效果载荷**：World 工具到 MQTT、地面 ROS、EGO 目标点，中间的 embodiment 形状没有一张对照。
- **三个 Skill HTTP**：`skill-engine` 的 `/api/skill-hub/engine`（loopback）、`:8791` 的 skill-hub、world 里的 authoring app。Gateway 明确不代理 engine 前缀。它们是不是同一个发布权威，代码阅读没有等同起来。
- **Real `:18794` 自身没有操作员身份**。能确定的是 Origin 策略加上「空 Origin 放行」；预览代理把这个面带出了本机。

---

## 9. 和旧黑板协同快照的关系

`/home/yunze/chaos_ros_control/zkdx` 的 README 描述的是另一代：Agent 在**同一个平台进程**里应征、认领、写同一块黑板，协调层只做过滤和去重。那个检出的最后一批提交停在 bsl 验收和前端矩阵，不是 `pr120`。

今态能从现行代码确定的变化是：Agent 变成 Harness spawn 的常驻子进程；Mission 是进程内的一次会话而不是进程的一生；黑板不再被当作第二份事实源；物理效果必须经过 Edge、租约和父进程执行器；学习进程和 Trace 导出都不在热路径上；重启不恢复未完成 Mission。

六份现行代码阅读里都没有旧主题表，也没有进程内 Agent 队列的对照，所以「旧实现具体删了哪些主题」不能从这次阅读里证实。MQTT 作为 Go2/Lingao 真机最后一跳，是 `pr120` 里真实存在的路径；这次阅读没有看到「它是不是长期架构」的结论。
