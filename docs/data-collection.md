# 机器人数据采集与时间质量

`chaos_data` 是机载 ROS 2 包，由资产配置和统一 launch 启动。整条链路都是**原生 C++ 实现**：独立录制进程用 `rosbag2_cpp` 写入传感器、状态与 TF 的原始序列化消息，原生监督进程负责时间质量、磁盘空间、分段和会话元数据，不额外订阅并反序列化相机或点云。数据保存在机器人本机，支持离线回放、训练选样和故障追溯；当前不自动上传主脑或转为图片、视频、训练张量。生产运行不需要 Python 科学栈——离线导出由原生 C++ 写出 NPY，NumPy 只在仓库自带的测试脚本中用于读取和断言。

## 配置与启动

在型号或资产实例下增加 `data_collection`。默认不启用，已启用时默认要求主机时间同步。相对 `output_dir` 以声明该字段的型号/清单目录解析，部署时建议使用独立数据盘绝对路径。

```yaml
data_collection:
  enabled: true
  output_dir: /var/lib/chaos/recordings
  storage_id: mcap
  include_state: true
  include_sensors: true
  include_tf: true
  max_bagfile_duration_s: 300
  max_bagfile_size_mb: 1024
  max_cache_size_mb: 64
  min_free_space_mb: 1024
  time_sync:
    require_sync: true
    max_uncertainty_ms: 10.0
    max_reference_age_s: 120.0
    poll_interval_s: 1.0
    probe_timeout_s: 0.5
    stable_samples: 3
    jump_threshold_ms: 100.0
```

自动话题清单包含本资产的 `state`、`command_results`、`control_trace`、已启用传感器流及目录/诊断、`/tf`、`/tf_static`。`control_trace` 是脱敏的运行时发令证据，包含速度、停止与失效事件，不包含租约 token，也不代表硬件执行确认。配置机器人模型后还包含 `robot_description` 与该模型的 `joint_states_topic`。`/tf` 和 `/tf_static` 是 Domain 内公共话题，多个机器人共享 Domain 时会记录其他发布者的 TF，frame 归属需要保留。

需要精确选择时，填写非空 `topics` 列表替代自动清单。相对话题以资产 namespace 展开，绝对话题保持原样，不使用正则；例如 `topics: [state, sensors/front_camera/color/image_raw, /tf_static]`。采集器自身的 `data_collection/status` 始终加入清单。话题上限为 256，新增设备话题应同时核对驱动实际类型、QoS、带宽和存储吞吐。

```bash
source /opt/ros/jazzy/setup.bash
source install/setup.bash

# 查看配置和进程计划，不创建录制会话
ros2 launch chaos_bringup system.launch.py \
  inventory:="$PWD/src/chaos_assets/config/data_collection.demo.yaml" dry_run:=true

# 演示使用 mock 数据，并显式 require_sync: false；不宣称已完成时间同步
ros2 launch chaos_bringup system.launch.py \
  inventory:="$PWD/src/chaos_assets/config/data_collection.demo.yaml"
```

真实机器人沿用 [机载部署](onboard-deployment.md) 的 `edge.launch.py` 和 host/asset 选择。启动前需要 rosbag2 及选择的 MCAP/SQLite 存储插件；采集用户需要数据目录写权限。真实采集目前仅支持系统时钟，禁止 `use_sim_time`，不以 `/clock` 为录制时基。包支持 `storage_id: sqlite3`；默认 MCAP 便于容纳高带宽多种消息。

资产校验会拒绝模型或自管传感器内联参数中明确启用的 `use_sim_time`。外部驱动、独立参数文件或运行中参数修改的时基仍需部署者核对；主机通过 NTP 检查不能证明消息发布者使用同一时钟。

空间低于保留阈值、原生录制子进程异常或元数据写入失败时，采集器结束当前录制并报告失败，不自动删除旧数据。保留阈值至少覆盖缓存，但磁盘检查是周期性的，不能保证磁盘不会在两次检查之间写满。原生 bag 文件按大小或时长拆分，至少一个限制必须大于零；这些文件拆分与下文的时间质量分段是不同层次。

## 统一 chrony / NTP 方案

ROS 状态位于 `<namespace>/data_collection/status`（`std_msgs/msg/String` JSON，transient-local），另有 `data_collection/diagnostics` 标准诊断。启用 MQTT 时，状态单向上报 `robots/<asset_id>/data_collection/status`，沿用 String 的 `data` 包装，不提供远程开启/绕过同步门控命令。接收方需检查更新间隔；采集进程退出后 transient-local 消息不会持续更新，旧缓存不能代表当前仍在录制。

采集器故障在 launch 中报告 ERROR，机器人运行时继续运行。缺包、无存储插件或输出路径无权限等可预检错误仍在启动任何节点前拒绝整次配置；运行中的磁盘和录制异常则由采集器自行收尾。

各机器人和主脑主机使用同一受管局域网 NTP 服务。服务器本身必须有可信上游时间源，客户端通过 chrony 驯服系统时钟。仓库提供 [客户端模板](../deploy/time-sync/chrony-client.conf.example) 和 [服务器模板](../deploy/time-sync/chrony-server.conf.example)，其中地址和网段为占位符；本次没有安装 chrony、部署 NTP 服务或修改系统时间。

部署时替换模板占位符，检查系统原有 chrony 配置、DHCP 和 `conf.d` 中是否还有其他时间源，避免只是追加文件却仍选择别的服务器。客户端模板面向受管局域网，轮询为 16–64 秒，与采集默认 120 秒的 reference freshness 门限配套。若使用系统默认最大 1024 秒轮询，必须相应调整轮询或采集门限，否则正常长轮询也会被保守判定为过期。服务器上游示例使用不短于 64 秒的轮询。[chrony 配置手册](https://chrony-project.org/doc/4.5/chrony.conf.html)

模板只允许 chronyd 启动后前 3 次更新做大幅校时（`makestep 1.0 3`），之后渐进校正。重启 chronyd 会重新进入这个阶段，应先结束采集，再重新等待稳定。不要在录制中执行 `chronyc makestep`；模板没有 `local stratum` 或孤岛授时配置，避免失去上游后仍伪装可信 UTC。只读检查命令为：

```bash
chronyc -n sources -v
chronyc -c -n tracking
```

采集器只执行第二条查询，不运行任何改时命令。它检查有效 reference、stratum 1–15、reference 更新时间及误差上界：

```text
host_error_bound = abs(system_time_correction) + root_dispersion + root_delay / 2
```

`offset_s` 保留 chrony CSV 原始符号，含义为 NTP 时钟减系统时钟；`uncertainty_s` 保存上述非负上界。默认要求上界不超过 10 ms；其成立依赖上游基准正确，不能用较小的瞬时 offset 替代整个上界。当前严格策略只接受 `Leap status = Normal`，闰秒插入/删除公告也会暂停严格录制，即使 chrony 本身仍已同步。查询缺失、超时、畸形、无参考或已知本地参考 `7F7F0101` 均不会默认为同步成功。[chronyc tracking 手册](https://chrony-project.org/doc/4.5/chronyc.html#tracking)

10 ms 是主机录制准入门限，不是相机、雷达、IMU 的硬件同步精度保证。设备硬件计数器与主机 UTC 的映射、曝光时刻、扫描起止语义、同步触发线或 PTP 能力须分别由驱动和硬件验证。Gemini 335 的设备时间映射也不能自动证明它与另一个 IMU 同时采样。

## 时间戳、失稳与数据保真

| 时间 | 用途与限制 |
|---|---|
| 消息 `header.stamp` / 顶层 `stamp` | 发布者填写的原始时间，可能来自硬件、主机或驱动转换；采集器不改写，必须核对驱动含义 |
| bag 接收时间 | 本版使用系统时间模式下原生 rosbag2/RMW 接收时间；可受主机调时影响 |
| DDS/RMW 发送时间 | 中间件发送元数据；可能为 0，也不等于曝光/测距时刻 |
| 单调时间 | 质量轮询间隔、有效期、运行时长和异常窗口；不受系统墙钟回拨影响，不用于跨主机直接对齐 |

Jazzy 原生录制器记录 RMW 接收/发送元数据，MCAP 对应 `logTime` / `publishTime`；消息内的 header 是第三种独立数据。本项目的离线审计使用 `rosbag2_cpp` 的 reader，它返回的仍是接收时间，因此审计**从原始消息单独提取源时间**，不把发送时间或接收时间当作 header。消息体通过 introspection 动态解码，因此审计不依赖每种消息类型的编译期绑定。[Jazzy Recorder 实现](https://github.com/ros2/rosbag2/blob/jazzy/rosbag2_transport/src/rosbag2_transport/recorder.cpp)、[MCAP 存储实现](https://github.com/ros2/rosbag2/blob/jazzy/rosbag2_storage_mcap/src/mcap_storage.cpp)、[C++ Reader 实现](https://github.com/ros2/rosbag2/blob/jazzy/rosbag2_cpp/src/rosbag2_cpp/reader.cpp)

严格模式先等待连续 3 次有效且不同的探测样本，再创建原生 recorder。原生进程就绪后立即再次探测，manifest 保存 `startup_clock_check`，明确进程启动期间仍存在观测间隙。同步质量变差时关闭当前独立 bag，恢复稳定后创建下一个 bag 并重新订阅，从而重新获取仍由活跃 transient-local 发布者保存的 `tf_static`、模型与标定消息；已经消失的发布者不会被凭空恢复。

监督进程比较墙钟与单调时钟的增量，超过 100 ms 的差异会产生新 epoch 并结束当前段。轮询只能在事件发生后检测，因此已经写入 bag 的尾部可能受到影响。manifest 的 `suspect_interval` 标注从最近可信观测至该段实际关闭的可能污染窗口；UTC 可能跨越回拨，判断事件顺序应查看单调时间。该窗口没有逐条硬件采样的单调时间映射，不能据此无损修复，训练数据可保守剔除整个可疑段。系统不会自动修正、重排或插值原始消息。

`require_sync: false` 用于显式接受未知时间质量的开发采集，仍保留失败原因并在检测到跳变时换段；它不会把质量状态改成正常。主机时钟变化小于检测门限可能不会触发 epoch，长期渐进漂移仍应结合误差上界和离线审计判断。

## 输出、回放与训练索引

每次启动创建独立、不覆盖的会话目录：

```text
<output_dir>/<asset_id>/<UTC_开始时间>_<随机ID>/
├── manifest.json
├── time_sync.jsonl
└── segments/
    ├── bag_0001/                 # 可独立读取的标准 rosbag2 目录
    │   ├── metadata.yaml
    │   └── ...mcap               # 或 ...db3；可能按大小/时长拆为多个文件
    └── bag_0002/
```

manifest 保存资产公开描述、模型与传感器声明、录制策略、分段和异常窗口；不复制 MQTT 密码、设备驱动秘密参数等整份资产配置。`time_sync.jsonl` 持续保存时钟状态、UTC/单调探测时刻、epoch 和采集状态，即使等待同步而尚未录制也有记录。manifest `completed` 只表示正常结束并刷新元数据，不证明每条话题一直存在或没有丢包；未正常退出而遗留 `running` 必须按中断会话处理。

在独立分析环境回放单个段，使用与运行机器人隔离的 ROS Domain：

```bash
export ROS_DOMAIN_ID=91
export ROS_AUTOMATIC_DISCOVERY_RANGE=LOCALHOST
unset ROS_LOCALHOST_ONLY
ros2 bag info /data/recordings/vehicle_001/SESSION/segments/bag_0001
ros2 bag play /data/recordings/vehicle_001/SESSION/segments/bag_0001 --clock
```

下游离线分析节点使用 `use_sim_time:=true` 以跟随回放 `/clock`；这不改变机载采集仅支持系统时间的约束。不要同时运行真实机器人控制节点。多个时钟 epoch 或质量失稳前后的段，应单独检查再决定是否组合，不能直接按 UTC 合并成连续轨迹。

离线审计和元数据索引：

```bash
ros2 run chaos_data data_audit \
  /data/recordings/vehicle_001/SESSION/segments/bag_0001 \
  --max-difference-s 0.1 \
  --index /data/analysis/vehicle_001_bag_0001.index.jsonl \
  --output /data/analysis/vehicle_001_bag_0001.audit.json
```

输出父目录需已存在，输出文件必须是新文件且位于原 bag 目录之外，不会覆盖原数据。省略 `--output` 时报告写 stdout；`--max-messages N` 可限量预览，此时报告 `complete: false` 表示仍有未审计消息。MCAP/SQLite 默认自动识别，也可使用 `--storage-id` 指定。

报告按话题统计源时间缺失、零值、无效、重复、倒序、接收异常、帧名称及接收减源时间的 min/max/mean。差值包含时钟域差异、驱动缓存、传输和调度，不能称为设备时钟真实 offset。TFMessage 包含多个独立 transform 时间戳，报告明确标记 `excluded_multi_stamp_messages`，不拿第一条作为整包源时间。无法加载消息类型或反序列化时保留 `decode_errors`，需要安装对应 ROS 接口包后再审计。

JSONL 索引逐条保存 `sequence`、`topic`、`type`、`receive_ns`、`source_ns`、`stamp_kind` 和 `frame`，不写图像/点云本体。`sequence` 仅是本次读取顺序，不是硬件帧号；存储插件若不能提供文件顺序，会在报告中标记 `storage_default`，按接收时间排序可能隐藏原始到达倒序。`clock_epoch: null` 表示仅凭 bag 无法恢复可靠 epoch，须结合会话 manifest 分段。报告限制保留的话题数和 frame 数，超过限制会计数，避免随消息总量增长占用内存。

训练处理先按 manifest 选择可信独立段，再用索引筛选话题、有效源时间和标定 frame。审计索引只有时间元数据，不包含 observation/action 值，也不能替代 RobotState 内部字段的采样时间。独立 `data_export` 已支持显式任务区间、逐通道因果采样、运行时速度标签和无损图像数组，详见 [训练数据规范与离线导出](training-data.md)。该步骤需要明确驱动时基和任务容许误差，保留无效网格点，不自动插值或修正原始时间；原始机载采集器仍只保存消息与质量证据。
