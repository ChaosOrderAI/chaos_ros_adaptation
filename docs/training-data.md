# 训练数据规范与离线导出

`chaos_data` 将机载采集与训练数据处理分开：机器人运行时记录原始 ROS 2 消息、控制轨迹和时间质量；离线 `data_export` 按显式任务区间导出数值 JSONL 与原始图像数组。结果是本项目的可追溯中间格式，尚不是 LeRobot 原生数据集，也不包含模型训练或实机推理实现。

本设计参考 Kuavo Data Challenge 的数据、训练、部署分层，按 ROS 2 Jazzy 的消息和时间契约重新实现。上游 ROS 1 Noetic 转换器不能直接处理本项目 rosbag2；本机载包不引入 PyTorch、CUDA、LeRobot 或 PyArrow。固定版本参考及采用范围见 [上游核对记录](upstream-reference.md#kuavo-data-challenge训练数据链路参考)，原始录制配置见 [数据采集与时间质量](data-collection.md)。

## 数据职责与动作含义

| 数据 | 含义 | 训练用途 |
|---|---|---|
| 相机、IMU、雷达等原始消息 | 驱动报告的观测，时间语义由驱动决定 | 按任务选择、解码和校验，不能把接收时刻当作物理采样时刻 |
| `RobotState` | 运行时汇总的状态；嵌套 pose 等字段可能保留不同来源时间 | 用明确字段、有效性标志、坐标系和单位组成观测 |
| `ControlTrace` | 运行时接收控制请求后，实际经过检查并向适配器发出的控制及其失效事件 | 首版速度监督标签；动作语义为 `runtime_issued` |
| `CommandResult` | 当前记录速度命令受理/拒绝结果，不包含控制值；其他动作进度和终态来自各自 Action/MQTT 通道 | 故障追溯；不替代连续动作标签 |
| 任务标注 | 任务起止、说明、成功/失败结果和原始数据定位 | 定义 episode，不从磁盘文件名推测任务 |

`runtime_issued` 表示运行时调用适配器控制接口，不证明底层执行器收到、接受或完成动作。当前没有硬件确认时间、真实执行量或闭环误差标签。机器人测得的 `state.twist` 属于观测，不能当作专家动作；MQTT 原始请求也不能绕过租约、有效期和限幅检查直接成为监督目标。

运行时在 `<namespace>/control_trace` 发布类型化 `chaos_interfaces/msg/ControlTrace`，默认采集清单包含该话题。自定义 `data_collection.topics` 时需要显式加入它。记录不包含控制租约 token。离线导出核对 trace 的资产 ID、序号、动作种类、状态和有效期；只有有效的速度控制可作为首版动作标签。

当前 trace 命令仅接受 `motion.velocity`、`motion.stop`、`navigation.pose`、`flight.takeoff` 和 `flight.land`。除有效的 `issued motion.velocity` 外，事件必须不携带 `velocity_valid` 或非零控制有效期；不认识或自相矛盾的事件会拒绝导出。

动作被拒绝时不会生成新动作，但先前尚未失效的动作可继续有效。停止、失效事件、过期和数据缺口不会自动填成零速度标签；零速度只有在它本身是有效记录的控制时才有监督含义。导航或飞行动作内部控制若没有相应速度 trace，不会自动转成可训练速度样本。

## Episode 是任务区间

采集会话、时间质量段、存储分卷、任务 episode 分别表达不同事实：

- 一次采集进程产生一个 session，包含 manifest 和时间日志。
- 同步失稳、恢复或时钟跳变可能产生多个独立 segment。
- 一个 segment 内的 MCAP/SQLite 可按大小、时长分卷。
- 一个 episode 是标注者选定的任务区间，附带任务文本及结果。

首版每个 episode 必须完全位于一个已正常关闭的 segment 内，使用 UTC 纳秒起止时间定位。不能跨时钟 epoch、可疑窗口或两个 segment 拼成连续轨迹。一次任务跨越分段时，应分别检查有效部分并明确标注含义，而不是仅靠相邻文件名合并。任务结果由标注者提供，导出程序不会从机器人停下或命令回执自动判断成功。

episode 从空观测和空动作状态开始，不复用任务窗口之前的消息或已发令动作；需等待窗口内的新样本和新发令，初始步骤可能无效。网格使用左闭右开区间 `[start_utc_ns, end_utc_ns)`，恰在结束时间的控制事件可用于闭合前面的标签。最后一条可见控制事件时刻及之后不再接受持续动作：尚有动作待延续时标记 `action_unclosed`，停止/失效后则没有有效动作。即使 TTL 尚未到期也不继续当作有效监督，避免遗漏尾部停止事件后沿用旧标签。

训练/验证/测试划分应按完整任务和采集 session 分组，避免同一轨迹的相邻帧进入不同集合；同场景、同操作者、同资产和同一天的相关性也应纳入评估。归一化统计只能从训练集合计算。首版不自动划分集合，也不拟合归一化参数。

## 导出配置与命令

导出配置独立于在线资产清单。资产配置决定记录哪些原始话题；导出配置决定哪些原始消息构成训练观测和动作。修改离线配置不改变机器人当前运行参数。

顶层配置字段：

| 字段 | 用途 |
|---|---|
| `schema_version: 1` | 本项目导出配置版本 |
| `fps` | 明确目标时间网格频率，范围为 `(0, 240]` Hz |
| `allow_unverified_time: false` | 默认拒绝未验证的主机授时质量；仅 mock 开发数据可显式豁免 |
| `features` | 命名观测通道，可使用 `observation.state`、`observation.images.front` 等名称 |
| `action` | 控制 trace 的话题、动作空间、字段、单位和时效要求 |
| `episodes` | 显式任务区间列表 |

每个 vector 特征使用 `kind: vector`，指定绝对 `topic`、完整 ROS `type`、`fields`、逐字段 `units`、`stamp_field`、`frame_field`、期望 `frame`、`clock_domain: system_utc`、`max_age_ms`，可用 `validity_fields` 指定必须为真的消息字段。每个 image 特征使用 `kind: image`、`sensor_msgs/msg/Image`，同样声明时间、坐标系、时效与 `units`。字段路径用于选择消息字段，不执行表达式或动态 Python 代码；配置拒绝未知键和重复键。

首版 RobotState 数值导出仅允许 `pose.pose.*`，必须使用 `pose.header.stamp`、`pose.header.frame_id` 和 `validity_fields: [pose_valid, connected]`。不能用 pose 时间给 twist、电池等其他分量证明采样时刻；这些通道需要后续独立定义时间语义。若源时间是运行时处理时间或驱动软件接收时间，导出不能把它升级成曝光/测距时刻。即使所有时间都可比较，也应评估测量与动作之间实际延迟。

`action` 明确 `topic`、速度 `fields`、逐字段 `units`、`frame`、`space`、`clock_domain: system_utc`、`max_age_ms`。例如地面车可声明 `space: base_velocity_2d`，并选择前向速度和偏航角速度；无人机可另行声明适配其实际控制能力的动作空间。字符串名称不构成动力学转换，导出不会把地面车动作自动转换为无人机动作。

每个 episode 声明 `id`、`session`、`segment`、`start_utc_ns`、`end_utc_ns`、`task`、`outcome`。`outcome` 可选 `success`、`failure`、`aborted`、`unknown`；session 相对路径以导出配置所在目录为基准。填写时从已完成采集的 manifest 和消息审计结果选取时间，不能照搬示例时间戳。

配置示例：[无人车速度监督](../src/chaos_data/config/vehicle_velocity.example.yaml)、[无人机速度监督](../src/chaos_data/config/drone_velocity.example.yaml)。安装后也位于 `share/chaos_data/config`。示例中的 session、时间及任务标注需要替换；无人机示例不表示现有 DJI 只读后端已经提供速度控制和有效位姿监督数据。

```bash
source /opt/ros/jazzy/setup.bash
source install/setup.bash

# profile.yaml 中填写已完成 session 的实际路径与任务时间区间
ros2 run chaos_data data_export /data/analysis/profile.yaml \
  --output /data/datasets/vehicle_navigation_v001
```

输出目录必须是新目录，父目录需要已经存在，并与原始会话分开。导出只读原始 session；配置不通过预检、控制序列损坏或转换失败时不交付部分成功的数据集。ROS 自定义消息仍需在离线导出环境中可加载。导出产物可以再供没有 ROS 的训练程序读取。

资源边界：单 episode 最长 24 小时且最多 100 万个网格点；单条序列化 ROS 消息不超过 64 MiB；保留图像缓冲总量不超过 128 MiB，每路特征最多保留 128 个尚未到网格时间的候选样本。超出边界时明确失败，应拆分任务或调整数据配置后重新导出。

配置文件不超过 1 MiB，可包含 1–32 路特征、1–1024 个 episode；每路 vector 包含 1–256 个不重复字段，`max_age_ms` 范围为 `(0, 60000]`。同一话题不能声明冲突消息类型，控制 trace 话题不能同时用作观测输入。

## 严格时间门控与因果对齐

默认要求 session 正常完成、选定 segment 正常关闭、采集策略要求同步且元数据可证明相应主机质量；带可疑污染窗口的段整体拒绝。不能因为 manifest 显示 `completed` 就认为所有消息都有效，也不能把录制器恢复后的新段用于修复旧段。

严格模式检查该段起始及原生 recorder 启动后的时钟证据，并要求日志中有该段录制状态下的同步样本。`time_sync.jsonl` 的 `clock_sample` 和 `session_end` 必须与 manifest 的 session ID、资产 ID 一致，且日志必须包含 `session_end` 收尾记录。身份不匹配或日志未完成时拒绝导出；mock 授时豁免也不会绕过这些完整性检查。这些检查验证采集记录的一致性，不是日志签名认证，也不能填补时间质量轮询之间的观测空隙。

`allow_unverified_time: true` 只允许资产 `mode: mock` 的开发数据显式豁免授时质量，仍需完整的会话和正常关闭状态。它不会把未同步数据标记成已同步，也不能用于放行真实机器人数据。chrony/NTP 的主机准入门限和设备采样同步精度是两个独立条件：Gemini 335、雷达和 IMU 的硬件时钟映射、帧内采样语义仍须单独确认。

导出在固定网格时间 `t` 上逐路选择可用消息，满足以下条件：

1. 消息源时间 `source_ns <= t`，bag 接收时间 `receive_ns <= t`；从两者都已发生的候选中选取源时间最新的样本，再检查其有效性，不通过回退旧值掩盖最新异常。
2. 样本满足该通道 `max_age_ms`，类型、字段、有效性及 frame 校验通过。单位由已核对的配置声明提供，程序不从数值自动推断物理单位。
3. `clock_domain: system_utc` 是配置者已经核对驱动后的前提声明，程序不能单凭整数时间戳鉴定硬件钟域。导出校验时间值、先后关系和数据龄期，不自动用接收时间覆盖缺失或无效源时间，也不跨钟域猜测偏移。
4. 动作是离线监督标签，按 ControlTrace 的运行时发令时间 `header.stamp` 和有效期确定，读取完整控制事件后再建立标签。晚到的停止或失效事件仍按事件源时间截断旧动作。动作记录允许 `receive_ns > t`，并保留接收延迟供审计；这些未来收到的信息只用于监督目标，不作为该时刻可用的观测输入。

这种做法限制未来信息进入观测，但不自动证明学习目标正确。相机时间、运行时发令时间和硬件响应时间仍可能存在固定或变化的延迟；需要任务实测后明确监督定义，不能为了让 loss 更低而任意平移标签。动作源时间是运行时发令时刻，不是硬件实际执行时刻。

缺少必需观测或有效动作的网格点仍写入步骤记录，并标记 `valid: false` 与原因。不会删除网格点后把余下帧压成等频连续轨迹，也不会插值、补零、静默复用过期值。模型训练侧应按无效区间切断连续序列，确保 observation history 和 action chunk 不跨缺口。读取到的特征或 trace 源时间倒序，以及 episode 内 trace 序号缺口、重复、倒退，都会导致整次导出失败，不通过排序隐藏异常。任一 episode 完全没有同时具备有效观测和有效动作的步骤时，也拒绝交付数据集。

## 输出与图像保真

```text
<output>/
├── dataset.json
└── episodes/
    └── task_0001/
        ├── episode.json
        ├── steps.jsonl
        ├── control_events.jsonl
        └── images/
            └── <feature>/
                └── <sequence>.npy
```

数据集和 episode 元数据保存配置、原始会话/分段定位及文件校验信息。源 bag、manifest 和时间日志参与来源校验，避免导出后无法确认输入版本。`control_events.jsonl` 保留选定任务区间的控制事件审计信息。

`episode.json` 提供 `rows`、`valid_rows`、`invalid_rows` 和 `invalid_reasons`。原因统计按 `action:<reason>` 或 `feature:<name>:<reason>` 计数；同一无效行可能有多个原因，因此原因总数可能大于 `invalid_rows`。完全无有效行时错误信息也给出该统计。`synthetic` 标明 mock 数据，`time_verified` 标明是否通过严格时间证据检查；两者不能互相替代。

`steps.jsonl` 每行包括 `grid_index`、`timestamp_ns`、`elapsed_ns`、`clock_epoch`、整体 `valid`、`features` 和 `action`。每路特征保留 `source_ns`、`receive_ns`、`receive_minus_source_ns`、`age_ns`、`message_index`、frame 和 clock_domain；数值通道使用 `values`，图像通道使用数组 `path` 及编码、尺寸、原始步长和字节序元数据。动作保留原 trace 的 sequence、单调时间、源/接收时间、request_id、有效期、动作空间、`runtime_issued` 语义及数值；无效项保存 `reason`。这些序号用于原数据定位，不等同于硬件帧号。

首版图像入口为未压缩的 `sensor_msgs/msg/Image`，支持 `rgb8`、`bgr8`、`mono8`、`16UC1` 和 `32FC1`。输出按 ROS 编码、尺寸、步长及字节序解码成原始数值数组，不做缩放、裁剪、RGB/BGR 变换、深度归一化或有损视频编码。不支持的编码会明确失败，需要另行增加并验证解码器。行填充字节不写入像素数组，完整 ROS 原始消息仍保存在源 bag 中。

`16UC1` 仅说明 16 位无符号单通道，不足以证明单位是毫米；`32FC1` 也不能独立证明单位是米。image 配置必须声明实际物理单位，例如 `depth_mm`，并与驱动和标定核对。无效深度值、噪声和缺测按原始数组保留；训练侧另行定义 mask、固定比例换算与物理量范围。禁止逐帧 min/max 归一化后仍将结果当成可比较的距离值。

数组文件不能取代相机标定。原始采集应同时保存适用的 CameraInfo、TF、URDF 和传感器目录；离线处理需要验证内外参版本、frame 归属，以及图像裁剪/缩放后的内参变化。首版不自动生成标定、不自动去畸变、不把多个传感器融合到新坐标系。

## 训练与机载推理的预留接口

训练服务器后续可增加独立 LeRobot 导出适配器，将经校验的 episode 中间数据转为其锁定版本要求的格式；ACT、Diffusion 等训练程序依赖安装在训练环境中。训练适配器必须明确无效点切分、图像编码、字段顺序、单位、坐标系和时间网格规则，不能只重命名字段就宣称格式兼容。当前尚未实现这个适配器、训练器或模型评估流程。

模型交付约定预留 `model manifest`，后续实现至少需要：

| 契约项 | 需要记录的内容 |
|---|---|
| 来源与版本 | 模型标识、训练代码版本、输入数据集及划分哈希、权重校验哈希 |
| 输入 | 特征名称、字段顺序、形状、dtype、单位、frame、历史长度、时间语义和容许数据龄期 |
| 输出 | 动作空间、维度、字段顺序、单位、frame、频率、预测时域和执行有效期 |
| 预处理 | 图像编码、尺寸变换、深度无效值策略、固定归一化参数及其校验哈希 |
| 运行条件 | 推理后端与版本、资源要求、超时策略、支持的资产能力 |

这是后续实现的接口约定，当前没有可执行的 model manifest 加载器或资产 `policy` 配置项。模型权重或预处理改变后，应产生新版本和校验值，不能覆盖原训练数据来源。

未来机载推理输出经 `chaos_sdk` 提交给运行时，遵守控制租约、命令有效期、限幅、停止及设备能力约束。策略进程不直接调用电机、底盘或飞控接口；观测超时、模型未就绪或推理超时必须进入明确的停止/失效处理。当前不启动训练任务、不下载权重，也不向真实设备发送模型动作。
