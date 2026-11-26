# zkdx harness 与 chaos_ros_adaptation harness

**基准**：zkdx 是 `/home/yunze/zkdx-pr120` 的 `pr120` @ `6ed7779ba`，对象是 `reshake_2_0/src/ai_native_harness` 加上 World 里「谁可以产生物理效果」的边界。本仓库是当前工作树的 `chaos_agent` + `chaos_skill` + `chaos_interfaces`。平台 SDK、驱动、MQTT 网关只看执行层交到哪里，不算进 harness。

`docs/harness-architecture-analysis.md` 是更早的机载分析。其中「会再追加 `skill_hold`」「`hold` 恒成功」「超时后立刻重发」对不上现在的代码，下面不沿用。

---

## 1. 同一件事，不是同一层

两边做的是同一件事：**模型只能在闭集里发出函数调用，调用要到另一个进程里过门禁才变成平台效果，模型自己没有设备句柄；已经可能生效的那一次，不会因为重试或结果不明被再执行一遍。**

它们不是同一层：

| | zkdx | chaos |
|---|---|---|
| 管的对象 | 一个 Runtime 里一支冻结团队：正好一个 Brain、至少一个 Edge，同时只有一个 Mission | 一台机器上的一个资产 |
| 规范状态 | 父进程 `MissionAuthority` 的任务账本。模型只提案 | 本资产的 hub。成功才按词表改写，失败停在进入时的 hub |
| 进程 | 父进程 + 每个 Agent 一个常驻子进程。关 Mission 不停进程 | `chaos_agent_node` 与 `chaos_skill_server` 按资产常驻。没有 Mission 进程 |
| 模型产物 | Responses 的 `function_call`，可在一个回合里多条，再收成 Command | 一个 `skill_<token>` tool call。同一拍只用第一个 |
| 执行权 | grant + TaskLease（默认一天）+ 审批 / 代际 / fence，然后才 dispatch | hub 匹配 + 单飞 `busy` + 需要时向平台要 10 秒 `ControlLease` |

`chaos_agent` 的 `package.xml` 不依赖 `chaos_skill`。两边的规划层和执行层都是分开的，耦合都落在一份闭集契约上：zkdx 是阶段可见的工具 schema，chaos 是启动时读入的同一份词表 JSON。

---

## 2. 真正对齐的决定

**模型不持设备。** zkdx 的模型只活在子进程 `AgentLoop` 里，子进程工具只做本地读，`command_executor` 在父进程。chaos 的模型循环只发 `SkillIntent`，碰不到 `chaos_sdk::Robot`、租约和导航。

**纯文本不是计划。** zkdx 只取 `type == function_call` 的输出。chaos 要求名字是 `skill_` 加词表 token，参数必须是 JSON 对象；纯文本丢掉，留给下一拍。`done` 在 agent 本地吞掉，不进技能层（`agent_loop.cpp`）。

**闭集不能在运行时被说明撑大。** zkdx 的 SkillRuntime 只披露已经发布的说明，写明不能借此增加工具权限；`physical_action` 必须有空间策略，效果适配器只能住在 `E_A_`。chaos 的工具列表由词表生成，词表之外只追加 `skill_done`。没有词表文件，技能服务器和 agent 都不启动。

**结果不明不再 dispatch 这一次。** zkdx 把结果记成不确定，阶段已经是 `EFFECT_STARTED` 的精确重试只对账。chaos 超时后把这条记成 outstanding，最多再等几次然后本地放弃，下一拍向模型要一个**新** id，不把原 `request_id` 再执行一遍。

**一次明确失败不拆掉决策循环，也不自动重放这个调用。** zkdx 提交 `COMMAND_FAILED`，这条 Edge 继续，别的 Agent 不停。chaos 的 Executor 不改 hub、释放自己申请的租约、发出 failed，agent 把成败写进 `last_result` 后继续。

**模型看不到设备流。** 两边喂回去的都是终态摘要，不是点云或原始传感。

---

## 3. 结构差别

### 崩溃之后接不接着跑

zkdx 的子进程崩溃，监督线程可以换代拉起，未封存的 Checkpoint 能重开，**已经发出的效果只对账、不重放**。整个 Runtime 死掉之后，下一次启动只把旧目录标成 `INTERRUPTED_UNCLEAN`，不把没做完的 Mission 接着跑。

chaos **没有检查点**。技能进程死后，hub、在飞目标和重放缓存都丢。agent 重启用 `steady_clock` 做新的 `run_id`（`agent_loop.cpp:73`），就是为了不撞上技能层对旧 id 的重放。它不查询技能层当前 hub，本地 hub 回到词表第一项，直到自己等到一条终态。技能服务器可以在 agent 重启后继续转。

所以：一边是「旧效果只对账，新生命周期不续跑」；另一边是「状态全丢，而且重启后的本地 hub 可以和仍活着的技能服务器不一致」。

### 任务账本，还是单资产枢纽

zkdx 的执行权在 `MissionAuthority`：集中式由 Brain 写分配，分布式交接也要 Brain 结算，Edge 不能自己改执行人。全 Runtime 第二个 `start` 会碰到 `ACTIVE_MISSION_EXISTS`。物理命令的 `device_id` 必须落在该 Agent 的设备集合里。

chaos 没有 Mission、Session、Brain/Edge。`edge.launch` 只是选定这一台机器上的一个资产。Executor 持有这个资产的 hub，规划来源不满足入口枢纽则 `skill.hub_mismatch`，调用不下到平台。demo 词表里的 `failed` 没有任何技能写进去。

### 多台设备，还是一台资产上的两个发布者

zkdx 的一个 Brain 对多个 Edge。一个 Task 可以分给多个 Edge，每条物理命令仍绑一台设备。它不跨 Runtime 管机群。

chaos 的一个 `asset_id` 只创建一个 `Robot` 客户端（`skill_server_main.cpp`）。agent 和 MQTT 网关是 `skill_intent` 上的**对等发布者**，抢的是同一把单飞锁，不是在协调两台设备。网关还可以把枢纽门禁关掉（`chaos_mqtt` 的 gateway）。机间通道不在 harness 里。

把 zkdx 的多 Edge 当成 chaos 的多机，或把 MQTT 对等发布当成第二个 Edge，都对不上。

### 同一条命令靠什么认出来

zkdx 的身份是语义指纹。激活号和过程代际不参与。指纹相同就取回已有结果，不同则冲突。同一导航目标若已有完成记录，复用测量，不再发一次移动。

chaos 只对**非空** `request_id` 做有界幂等：还在 pending 就丢掉；已结束的在 256 条 FIFO 里重放上一次终态。空 id 每次都真执行。窗口滑出之后，同一个 id 可以再跑一次。agent 放弃等待后要的是新 id，原技能可能还在跑，新的会被 `skill.busy` 拒绝。

### 取消收的是什么

zkdx 的取消和 dispatch 抢同一把短锁：还没交出就拒绝且无效果；已经交出就只收集停止证据。隔离证明只挡住这台设备上的新冲突效果，不证明旧动作已经停下。

chaos 的 `request_cancel()` 只把 `cancel_requested_` 置真（`executor.cpp:140`）。下一次 `execute` 会把它清掉（216 行）。全包没有读取点，**SDK goal 不会被取消**。`request_stop` 只停 agent 的决策定时器，技能服务器继续转。

### 上下文有几种寿命

zkdx 是两份材料：父进程按这次请求现装的世界/任务快照，加上 Checkpoint 里**追加**的对话。压缩只替换已经提交的历史前缀。

chaos 每次请求只有一条 system 和一条 user（`llama_client.cpp:40`），不追加对话。user 里是 `asset_id`、agent 自己记下的 hub、任务原文，以及可选的精简 `RobotState` 和上一技能的成败。看不到 `observe` 的 payload，看不到更早的步骤，没有图像。模型名写死 `gemma-4-e4b`，`max_tokens` 64。

节拍也不一样。zkdx 只有 `DECIDE` 才打模型；回放、回执、恢复不发起新决策。空闲子进程只堵在 IPC 上。chaos 是墙钟定时器，默认 0.5 Hz，上一拍还在飞就跳过。超时后的新意图会和仍在跑的旧技能撞上 `skill.busy`。

### 交给平台之前的门

zkdx：先落 Checkpoint outbox，再 `process_command`。要审批而还没批准，就返回空，此时没有设备调用。旧子进程的命令被代际和 fence 拒绝。安全和租约通过之后才 `adapter.dispatch`。

chaos：`skill_intent` → 截止时间 → `request_id` 去重 → 词表与 hub → 容量 8 的队列 → Executor。没有人的审批。`ExecuteSkill` 是旁路，直接进同一个 Executor，目标上没有门禁开关，但准入仍查 hub。`SkillRequest` 只回答能否接受，不执行。需要租约的技能由执行层以 owner `chaos_skill` 向平台申请 **10 秒**（`executor.cpp:22`），终态释放，运行期间不 renew。远端可以把已有 `lease_token` 放进 `control_token`，技能层用它执行且不释放。

---

## 4. 同名不同物

| 词 | zkdx | chaos |
|---|---|---|
| skill | harness 里是不能加权限的说明。学习进程 `skill_engine` 不在 harness 里 | 启动时的静态词表。`navigate_to` / `takeoff` / `land` / `stop` / `observe` / `announce` 会变成 SDK 或能力端口调用 |
| lease | 和 TaskGrant 一起的 TaskLease，默认 86400 秒。过期先落成任务失败，再拒绝新命令 | 执行时申请的 10 秒 ControlLease，或 MQTT 带进来、用完不释放的令牌 |
| session | `open_session` 把 Mission 挂到常驻子进程，同时只挂一个 | 没有。一个任务就是 agent 的 task 字符串 |
| edge | 冻结团队里的角色，在 grant 下做 DECIDE | `edge.launch` 选定这一台机器上的一个资产 |
| 单飞 | 同一条语义 Command 在 Authority 里单飞；同时只许一个 active Mission | Executor 的 `busy`。忙则拒绝，不排队。agent 的 tick 只跳过仍在飞的上一轮决策 |
| harness | 一次不可恢复的后端生命周期 | 每个资产一套规划循环加技能服务器，没有任务账本 |

还有一处名字和绑定不一致：chaos 的系统提示写「没有合适的就选 `hold`」（`agent_loop.cpp:82`），但 `capability_of` 不认得 `hold`（`executor.cpp:88` 落到空），分派前以 `skill.unimplemented` 失败，到不了「保持不动」。

---

## 5. 不要直接对搬

- 不要把 zkdx 的 Checkpoint 重开当成 chaos 的重启恢复。chaos 没有这笔账；技能服务器可以比 agent 活得更久。也不要把 chaos 的「进程还在转」当成 zkdx 那种 `INTERRUPTED_UNCLEAN` 投影。
- 不要把 TaskLease 和 ControlLease 当成同一把锁。一个是任务账本上的授权，一个是向平台借 10 秒输出权，或者一根外面带进来的令牌。
- 不要把「全 Runtime 只许一个 Mission」写进 `edge.launch`，也不要把「关掉枢纽门禁的对等发布者」放进 zkdx。那边第二个 Mission 会被拒绝，物理命令还必须落在该 Agent 的设备集合里。
- 不要把「效果已开始则只对账」当成 chaos 的 `request_id` 缓存。缓存有界，空 id 每次都真执行，放弃等待后要的是新 id。
- 不要把 zkdx 的说明型 Skill 当成可执行词表，也不要把 chaos 的 token 当成「不能增加工具权限的说明」。
- 不要把 zkdx 的取消和隔离证明搬过来。这边的取消标志没有读取点，也没有 journal 可以只对账。`request_stop` 也不是那把短锁。
