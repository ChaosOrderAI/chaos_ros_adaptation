# 传感器资产与驱动

硬件采集由独立驱动进程负责，统一 launch 按资产配置启动和监督驱动。相机、雷达和 IMU 使用标准 `sensor_msgs` 接口，机器人控制继续通过 `chaos_runtime`。大数据流留在本机 ROS 网络，不默认经过 MQTT 或业务网关。`chaos_sensors` 的模拟与监测节点是原生 C++ 实现（`ros2 run chaos_sensors mock_sensor` / `sensor_monitor`），Python 侧只保留 launch 胶水。

Gemini 335 的官方 C++ 驱动源码、固定版本和部署步骤见 [奥比中光接入](orbbec-driver.md)。本层同样允许接入其他提供标准消息的 ROS 2 驱动。

现成的接入示例都在 `chaos_assets/config/`：`orbbec_gemini335.example.yaml`（RGB + 深度 + IMU）、`at600_ir_camera.example.yaml`（IRay AT600 热成像 + 可见光）、`livox_mid360s.example.yaml`（Mid-360S 点云 + IMU）、`hikvision_mvcs050.example.yaml`（海康 5 MP 千兆网相机）、`realsense_d435.example.yaml`（D435 彩色 + 深度 + IMU）。

## 配置与启动

型号文件和资产实例均可包含 `sensors` 映射，按传感器 ID 合并。同一驱动可以提供多条流，例如 Gemini 335 的 RGB、深度和 IMU，或 Livox 的点云与 IMU。无需为每条流重复启动设备驱动。

| 字段 | 含义 |
|---|---|
| `kind` | 当前支持 `camera`、`lidar`、`imu` |
| `enabled` | 是否在当前资产启用，默认 true |
| `frame_id` | 设备根坐标系，默认 `<asset_id>/<sensor_id>_link` |
| `driver.mode` | `node` 启动 ROS 可执行文件，`attach` 接入已有话题，`mock` 生成模拟数据 |
| `driver.package/executable` | node 模式的 ROS 包与可执行文件，不接受任意 shell 命令 |
| `driver.parameters` | ROS 标量或同类型非空数组，字符串按字符串传递 |
| `driver.parameter_files` | ROS 参数 YAML 文件列表，显式参数覆盖文件中的同名参数 |
| `driver.remappings` | ROS 话题重映射，禁止覆盖 namespace 或节点名 |
| `streams` | 流 ID 到接口声明的映射，包含 type/topic/frame_id/qos/rate_hz/timeout_s/monitor |
| `transform` | 可选的安装外参，parent + xyz（米）+ rpy（弧度） |
| `time_sync` | 可选的时间来源声明，见下节 |

### time_sync 时间来源

`sensors.<id>.time_sync` 声明这一路消息的时间戳从哪里来，供驱动、监测和下游对齐共同参考：

| 字段 | 取值 | 含义 |
|---|---|---|
| `source` | `board_trigger` / `device_timestamp` / `host_arrival` | 同步板 PWM 触发、设备自带时间戳、主机到达时间 |
| `board_channel` | `CAM_1..CAM_4` / `IMU_2` / `LIDAR_1` / `LIDAR_2` / `none` | 涉及的物理通道 |
| `trigger_hz` | 0.01–1000，可选 | 期望触发频率 |
| `max_age_ms` | 0.01–10000，默认 50 | 触发到帧的最大允许间隔（配对超时） |
| `exposure_offset_us` | −1000000..1000000，默认 0 | 时间戳补偿（如曝光中点） |
| `clock_domain` | `host_utc` / `board` / `device` | 时间戳所在时钟域，默认按 source 推导 |
| `requires` | `none` / `gps_sync` / `ptp` | 该路依赖的同步前提，供预检和诊断使用 |
| `image_stream` | 流 ID，可选 | `board_trigger` 相机有多条 Image 流时指定参与配对的那一条 |

`time_sync` 本身仍不启动进程；启动开关是 host 级的 `board` 块。real 模式下每台主机启动一个 `chaos_time_sync_node`（根 namespace，发布 `/board/trigger`、`/board/imu`、`/board/diagnostics`），并为每个 `source: board_trigger` 的相机派生一个 `trigger_pair_node`（在传感器 namespace 下订阅原始 Image/CameraInfo，发布 `<sensor namespace>/image_synced`、`camera_info_synced`、`diagnostics`）。mock 资产与 `--mode mock` 不启动二者。注意 `sensor_monitor` 仍监测原始 `streams.topic`，采集也默认只录原始话题；需要同步帧时应在 `data_collection.topics` 中显式加入 `image_synced`/`camera_info_synced`/`board/imu`。

约束：`board_trigger` 必须配 `CAM_n` 或 `IMU_2`；带多条 Image 流的 `board_trigger` 相机必须用 `image_stream` 指明配对流（只有一条时可省略，加载器会自动写入）；`device_timestamp` 只允许 `none` 或 `LIDAR_n`；`host_arrival` 不允许声明通道。`board_trigger` 表示硬件触发链，`clock_domain` 通常是 `host_utc`（板卡时钟由主机 UTC 校准）；`host_arrival` 是最低精度选项，仅在设备既不能触发也没有可信时间戳时使用。声明会出现在 `sensors/description` 中（不含驱动参数），不改变驱动的实际行为——驱动仍须按声明实现。

### hosts.<id>.board 同步板

| 字段 | 默认 | 含义 |
|---|---|---|
| `ip` | 必填 | 板卡 IPv4 地址（按字面量校验） |
| `port` | `8888` | 板卡 UDP 端口 |
| `local_port` | `0` | 本机源端口，0 表示临时端口 |
| `ptp_period_ms` | `100` | 类 PTP 校时周期（毫秒） |
| `publish_imu` | `true` | 是否发布板载 IMU |

配置了 `board` 才表示这台主机有同步板。real 资产的传感器若声明 `source: board_trigger`、`requires: gps_sync/ptp`、`clock_domain: board` 或 `board_channel: LIDAR_n`，加载时会要求该主机必须有 `board` 块，否则报错；mock 资产不要求。`source: board_trigger` 的相机必须至少声明一条 `sensor_msgs/msg/Image` 流。同一主机上的多个资产共享一个板卡客户端，只有第一个被选中的 real 资产承载该进程。

传感器 namespace 固定为 `<asset namespace>/sensors/<sensor_id>`，受管驱动节点名固定为 `driver`。相对流话题和重映射目标按此 namespace 展开；绝对话题保持原样。`frame_id` 不受 ROS namespace 自动隔离，需要独立设置。

模型中的参数文件以模型目录解析，实例中的文件以清单目录解析，也支持 `package://`。参数 YAML 使用 `/**` 或完整节点名，例如 `/robots/vehicle_001/sensors/front_camera/driver`。启动前检查 ROS 参数结构、类型和节点选择器匹配，随后复制到私有临时目录，避免文件在启动过程中被替换。厂商特定参数名称、范围和设备 profile 仍由厂商驱动校验。

实例与型号保持相同 `driver.mode` 时合并配置；实例明确切换模式时替换整个 driver。资产选择 `mode: mock` 时，所有传感器强制使用模拟节点，保持原来的话题声明；真实资产禁止启用 mock 传感器。mock 不要求安装原厂驱动。旧资产未声明 sensors 时不会多启动任何传感器节点。

```bash
source /opt/ros/jazzy/setup.bash
source install/setup.bash

# 模拟相机、雷达和 IMU；先输出计划
ros2 launch chaos_bringup system.launch.py \
  inventory:="$PWD/src/chaos_assets/config/sensors.demo.yaml" dry_run:=true

# 启动模拟数据（示例使用 ROS Domain 87）
ros2 launch chaos_bringup system.launch.py \
  inventory:="$PWD/src/chaos_assets/config/sensors.demo.yaml"

# Gemini 335 配置也可以全链路模拟；不会打开相机或运行原厂节点
ros2 launch chaos_bringup system.launch.py \
  inventory:="$PWD/src/chaos_assets/config/orbbec_gemini335.example.yaml" \
  host_id:=onboard mode:=mock
```

真实启动前预检所有受管传感器的 ROS 包、可执行文件和参数文件。任一受管驱动、监测或静态 TF 进程退出，会沿用整体 launch 的退出策略；非零退出会向服务管理器报告失败。`dry_run` 不要求已安装厂商可执行文件，也不打开设备。检查参数文件是否存在仍属于配置解析。

attach 模式只启动监测节点，不另开厂商进程、不转发或转换原始消息：必须填写已有话题的绝对名称，并保证 Domain、消息类型、QoS 和 frame 与声明一致。若需要给外部话题改名，应在其原驱动中设置重映射。Livox CustomMsg 不会通过改名变成 PointCloud2，使用该类型的算法需要保留原始通道或显式转换。

## AT600 热成像拆分与融合

IRay AT600 暴露唯一 UVC 模式：512x192 YUYV @ 25 fps，左半热成像、右半可见光，出厂已配准同一光轴。`chaos_sensors` 提供三个节点：

| 可执行 | 作用 |
|---|---|
| `at600_split_node` | 取 YUYV 的 Y 平面按 `split_ratio` 切分，发布 `thermal/image_raw` 与 `visible/image_raw`（均 mono8）。资产传感器按 `driver: {package: chaos_sensors, executable: at600_split_node}` 声明，纳入统一 launch（示例见 [at600_ir_camera.example.yaml](../src/chaos_assets/config/at600_ir_camera.example.yaml)）。 |
| `thermal_register_calib` | 独立安装的可见光相机与热成像不像素对齐时，估计可见→热成像单应（SIFT+RANSAC）并写出 ROS 2 参数文件。 |
| `thermal_fusion_node` | 融合任意两路 Image 到 `thermal/image_fused`（bgr8），`mode:=blend\|detail\|side_by_side\|thermal_color`，可选 `homography` 先配准可见光。 |

```bash
ros2 launch chaos_sensors thermal_fusion.launch.py
# mode:=blend|detail|side_by_side|thermal_color  alpha:=0.6  colormap:=inferno

# 独立可见光相机先标定再融合（AT600 两半是模块自身配准的，homography: [] 即仅缩放）
ros2 run chaos_sensors thermal_register_calib --ros-args \
  -p thermal_topic:=/thermal/image_raw -p visible_topic:=/hikvision/image_raw \
  -p output_file:=/tmp/thermal_homography.yaml -p preview_file:=/tmp/thermal_registration.png
ros2 run chaos_sensors thermal_fusion_node --ros-args \
  --params-file /tmp/thermal_homography.yaml \
  -p thermal_topic:=/thermal/image_raw -p visible_topic:=/hikvision/image_raw -p mode:=detail
```

单应只处理平面场景的旋转/平移/缩放，大基线视差下退化；相机重新安装后需重标（参数文件按节点名 `thermal_fusion_node` 取键，节点改名时给标定节点传 `node_name`）。热归一化默认 2%/98% 分位拉伸，`thermal_min`/`thermal_max` 同时设置时改用固定范围。边界：AT600 无外触发，进不了同步板硬件触发链，时间戳来自 V4L2 缓冲（CLOCK_MONOTONIC）映射主机 UTC。

## 公共数据接口

| 消息 | 用途 |
|---|---|
| `sensor_msgs/msg/Image` | RGB、红外或深度图，编码由真实驱动声明 |
| `sensor_msgs/msg/CameraInfo` | 图像标定信息 |
| `sensor_msgs/msg/PointCloud2` | 三维点云 |
| `sensor_msgs/msg/LaserScan` | 二维激光扫描 |
| `sensor_msgs/msg/Imu` | 加速度、角速度，以及设备实际提供的姿态 |

每条流可选 `qos: sensor_data`（默认）或 `reliable`。此声明控制模拟发布器并描述真实驱动预期；node 模式仍须通过对应厂商参数设置实际发布 QoS，框架不伪造驱动能力。`rate_hz` 范围为 0.01–1000，控制模拟发布频率，在真实模式中是期望值，不会自动改写设备帧率；当前诊断不计算实际频率或执行低频告警。`timeout_s` 范围为 0.001–300 秒。

每台有启用传感器的机器人额外提供：

- `sensors/description`：`std_msgs/String` JSON 目录，可靠、transient local、depth 1；晚加入的客户端也能读取。包含传感器及流的类型、话题、frame、QoS 和监测开关，不包含驱动参数、序列号或凭据。
- `sensors/diagnostics`：`diagnostic_msgs/DiagnosticArray`，每秒发布一次各监测流的接收、时间戳与 frame 检查结果。

例如：

```bash
export ROS_DOMAIN_ID=87
export ROS_AUTOMATIC_DISCOVERY_RANGE=LOCALHOST
unset ROS_LOCALHOST_ONLY
ros2 topic echo /robots/vehicle_001/sensors/description --qos-durability transient_local
ros2 topic echo /robots/vehicle_001/sensors/diagnostics
```

默认监测流接收超时、源时间戳无效/不前进/过期/超前，以及 frame 不匹配。接收间隔使用单调时钟，源时间年龄使用 ROS 时钟。数据一直到达也可能源时间已过期。若旧驱动把缓存数据重新打上当前时间，框架无法据此识别真实硬件已经停更，需要修正上游的采样时间语义。

`monitor:false` 保留目录与数据输出，跳过对应订阅。监测节点使用原生 C++ 类型化订阅，同样会反序列化整条消息，因此 Gemini 模板默认仅监测 CameraInfo 与 IMU，避免监测大图像的额外负载。CameraInfo 正常不能证明每张图像有效；被关闭的流没有健康检查结果。

诊断只提供观测信息，不自动更改机器人 ready、控制租约或触发停车。attach 驱动失联会产生诊断，如何让特定传感器故障约束导航/飞行，应由对应任务和控制策略明确实现。

## 坐标系与当前边界

同一 ROS Domain 下校验传感器和流的 frame 归属、重复受管输出话题、同话题类型冲突，以及显式静态 TF 的环。机器人 base/odom/map 坐标系不能被传感器占用；机器人之间仍可共享定位坐标系。校验覆盖整份启用资产清单，独立清单之间的唯一性由部署配置保证。

只有明确填写 `transform` 才启动静态 TF 发布器，默认 parent 为机器人 base，child 为传感器根 frame。必须填实测外参；相机内部的光学坐标变换及标定由原厂驱动负责。不要重复发布同一条变换。检查器无法从流的 frame 名推断厂商内部 TF 树，尤其是把设备挂在另一设备内部 frame 时，需要核对完整树的方向和唯一父节点。

资产导入 [URDF/Xacro](robot-description.md) 后，传感器可以引用本资产模型中的非根 link。若安装 frame 已由 URDF joint 发布，则删除该传感器的 `transform`；检查器会拒绝双重发布同一 child，并检查 URDF 与显式传感器变换组合后的环。原厂相机内部 TF 仍需明确归属，不能仅凭模型中存在同名 link 判断另一驱动是否也在发布。

模拟消息仅验证话题、类型、QoS 和启动链路；图像、内参、点云及 IMU 数值均为合成数据，深度 Image 也不模拟真实测距编码。它们不能用于标定、感知精度或动力学验收。新消息类型需要扩展 schema、模拟器及监测类型表；新厂商若已有独立 ROS 节点，可继续使用 node 模式接入。
