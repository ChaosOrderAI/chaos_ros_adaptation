# Chaos ROS 2 多机器人框架

部署在无人车或无人机机载计算机上的 ROS 2 Jazzy 框架，面向 Ubuntu 24.04 的 x86_64 / ARM64 环境。每台设备通过自己的资产配置加载适配器、提供统一 API，并经 MQTT 与平台通信。参考 Kuavo 的版本化资产包、公共接口和后端组织方式，使用 ROS 2 原生接口实现。

机载部署使用 `edge.launch.py` 和单设备配置，见 [机载部署说明](docs/onboard-deployment.md)。下方双 mock 的 `system.launch.py` 用于开发验证。

- [架构与当前能力边界](docs/architecture.md)
- [Kuavo 上游参考与采用范围](docs/upstream-reference.md)
- [构建与验证记录](docs/validation.md)
- [机载部署、架构构建与 systemd](docs/onboard-deployment.md)
- [传感器配置、标准接口与诊断](docs/sensors.md)
- [奥比中光 Gemini 335 官方驱动](docs/orbbec-driver.md)
- [URDF / Xacro 导入、TF 归属与 RViz](docs/robot-description.md)
- [示例资产清单](src/chaos_assets/config/demo.yaml)
- [无人车型号](src/chaos_assets/models/lingao_vehicle/v1/model.yaml) / [无人机型号](src/chaos_assets/models/dji_m3td/v1/model.yaml)

## 开发环境构建与启动

依赖 ROS 2 Jazzy、colcon、ament_cmake/ament_python、rclpy、geometry_msgs、std_msgs、sensor_msgs、diagnostic_msgs、tf2_ros、action_msgs、rosidl_default_generators、Python PyYAML 和 pytest。模型导入使用 robot_state_publisher、urdfdom；Xacro 模型另需 xacro。现有厂商程序无需参与 mock 构建。

在本目录执行，明确使用 `--base-paths src`，避免把旁边的 ROS 1 工程和 SDK 一起构建：

```bash
source /opt/ros/jazzy/setup.bash
colcon build --base-paths src --symlink-install
source install/setup.bash

# 先检查计划：不启动 ROS 节点、驱动或 MQTT 连接
ros2 launch chaos_bringup system.launch.py dry_run:=true

# 同时运行模拟无人车和无人机；MQTT 默认关闭
ros2 launch chaos_bringup system.launch.py
```

本轮控制服务和动作新增 `expires_at` 字段，需统一重建并重启所有使用 `chaos_interfaces` 的节点；旧、新接口产物不可混跑。MQTT 中间件也需重建，以支持任务终态响应与新的发送队列策略。

默认清单使用 ROS Domain 87，只在本机发现。另一个终端查看状态时使用相同环境：

```bash
source /opt/ros/jazzy/setup.bash
source install/setup.bash
export ROS_DOMAIN_ID=87
export ROS_AUTOMATIC_DISCOVERY_RANGE=LOCALHOST
ros2 service call /robots/vehicle_001/description chaos_interfaces/srv/GetDescription '{}'
ros2 topic echo /robots/drone_001/state
```

`description` 返回当前后端实际能力。mock 是用于验证接口的运动学模型，不是带碰撞、避障或飞行动力学的仿真器。

## 配置组织

```text
src/
  chaos_assets/      型号版本、实例清单、配置校验与解析
  chaos_interfaces/ 状态、控制租约、速度、停止、导航与飞行动作契约
  chaos_runtime/     每资产运行时与可扩展后端
  chaos_bringup/     统一 launch
  chaos_sdk/         上层 Python API
  chaos_mqtt/        MQTT 命令与 ROS 接口转换
  chaos_sensors/     模拟传感器、数据新鲜度诊断与传感器目录
drivers/
  src/OrbbecSDK_ROS2/ 固定版本的官方相机驱动，独立构建
```

`models/<model>/<version>/model.yaml` 保存型号、能力上限、运动限制、资源路径和后端；`config/*.yaml` 保存设备 ID、host、namespace、mode 以及实例覆盖。配置中的相对路径以声明它的文件为基准；`package://包名/路径` 从已安装的 ROS 包读取资源，适合 `/etc/chaos` 下的设备配置。模型资源会随包安装，通过可选的 `description` 配置导入 URDF/Xacro 并启动模型发布器；控制器仍需按机器人能力单独接入。

原有 `ROBOT_VERSION` 全局环境变量改为每个资产的 `model`、`model_version`；一份清单可以组合不同型号和版本。`schema_version` 单独描述配置格式。

```bash
# 输出完整解析结果及每个资产的 config_digest
ros2 run chaos_assets asset_config src/chaos_assets/config/demo.yaml

# 只选择本机的一台资产
ros2 launch chaos_bringup system.launch.py \
  inventory:=/absolute/path/assets.yaml host_id:=dev assets:=vehicle_001
```

`mode:=mock` 或 `mode:=real` 显式覆盖所选后端；未指定则使用清单中的 mode。无效字段、重复 ID/namespace、未知能力、缺失资源及无效限值会报错。命令输出归属和 MQTT 身份冲突按整个启用资产清单校验，避免分主机启动掩盖冲突。

启动时只解析一次资产清单，校验和进程构造共用同一份不可变计划；生成的临时解析快照和 MQTT JSON 在退出时清理。需要保留配置证据时，用 `asset_config --output /path/resolved.json` 显式导出。直接 launch 和服务启动都会清除旧的 `ROS_LOCALHOST_ONLY`，按指定发现范围运行。任一受管进程退出会结束该主机的 launch。launch 本身不自动重启；机载 systemd 模板负责重启整个本机框架，重启后不恢复旧控制租约或任务。

## 已有机器人接入

| 后端 | 当前实现 | 依赖 |
|---|---|---|
| mock UGV | 状态、控制租约、速度、停止、导航 Action | 无设备 |
| mock UAV | 上述能力及起飞、降落 Action | 无设备 |
| `lingao_ros2` | 新鲜 Odometry 状态、经校验的 Twist 速度与受控停止 | 现有底盘驱动已运行，配置实际 topic 和 frame |
| `dji_psdk_legacy` | 订阅旧网关 ROS 状态 | 旧网关已经产生 `system/status`；暂无统一飞行控制 |

[edge.example.yaml](src/chaos_assets/config/edge.example.yaml) 是接入模板，域号、话题和 frame 必须与目标设备核对。运动后端采用 attach：底盘驱动和旧 DJI 网关仍由既有服务管理。传感器可另选 node 模式纳入统一 launch。无人车旧部署使用 Humble，Jazzy 下的底盘驱动构建与实机验证仍需完成。

DJI 的旧 `system/status` 仅在状态请求时发布，没有请求时会变为离线。适配器不会伪造周期遥测或把未知 GPS 坐标转换成有效本地位姿。接入飞行控制前，需要改造旧网关的请求关联、终态事件和 ROS-only 运行模式。框架租约不替代原飞控授权。

车辆的速度仲裁仅覆盖经过新框架的输入，实际切换前须处理旧桥接、导航等直写底盘的控制源。零速度停止属于软件控制输出，不是硬件急停。

## 相机、雷达与 IMU

资产中的 `sensors` 配置支持独立 ROS 驱动、已有话题接入及模拟节点。统一使用 Image、CameraInfo、PointCloud2、LaserScan、Imu；一个设备可同时发布多种数据。传感器目录与诊断位于每资产的 `sensors/description` 和 `sensors/diagnostics`。高带宽数据留在本机 ROS，MQTT 不默认转发图像和点云。

Gemini 335 官方源码已纳入独立 `drivers` 工作空间，固定 `v2.10.6` / `296156521b60bdf813cb94c46cbb55562bbdc453`，保留厂商 SDK 和许可证。使用 [Gemini 335 模板](src/chaos_assets/config/orbbec_gemini335.example.yaml) 配置序列号、RGB、深度和内置 IMU。原厂节点本机编译尚缺部分依赖，详见 [构建与接入说明](docs/orbbec-driver.md)。

```bash
# 模拟相机、雷达与 IMU；使用默认 dev 主机和 Domain 87
ros2 launch chaos_bringup system.launch.py \
  inventory:="$PWD/src/chaos_assets/config/sensors.demo.yaml"
```

`mode:=mock` 会把配置中的所有传感器切换为合成数据；真实资产禁止 mock 传感器。实际接入前填写设备与流的 frame、时间戳约定和实测安装外参；没有配置外参时不会自动发布静态变换。完整字段和接入边界见 [传感器说明](docs/sensors.md)。

## URDF / Xacro 模型

型号或实例可通过 `description.file` 导入 URDF/Xacro。启动前展开、执行原生模型校验并检查资源和 TF 冲突，统一 launch 使用已冻结的 XML 启动每资产的 `robot_state_publisher`。默认 TF 前缀为 `<asset_id>/`；活动关节仍由实际驱动提供 `joint_states`。

```bash
ros2 launch chaos_bringup system.launch.py \
  inventory:="$PWD/src/chaos_assets/config/description.demo.yaml" dry_run:=true
# 去掉 dry_run:=true 启动双 mock 模型
```

当前没有实机整机 URDF，示例模型的尺寸和安装点仅用于验证。替换文件、Xacro 参数、mesh 路径、相机 TF 归属及 RViz 设置见 [模型导入说明](docs/robot-description.md)。

## MQTT 接入

复用旁边的 `mqtt_ros2_adapter`。新增 `route_profile: custom` 只加载显式路由；缺省 `legacy_drone` 保持旧配置行为。按照中间件 README 构建，使用已配置的 broker 与证书，然后填写 [mqtt.example.yaml](src/chaos_assets/config/mqtt.example.yaml)。

```bash
ros2 launch chaos_bringup system.launch.py \
  inventory:=/absolute/path/mqtt.yaml host_id:=dev enable_mqtt:=true
```

launch 为每个资产生成单独的 Rust JSON，用 `--check-config` 在启动任何节点前校验真实配置，再启动业务 gateway 与中间件。`dry_run:=true` 仅检查计划，不要求本机存在二进制、证书，也不执行原生配置检查。

默认 MQTT 主题为 `robots/<id>/commands`、`responses`、`state`、`task_events`。外层命令沿用原协议 `version/message_id/timestamp_ms/command`；内部命令名按能力生成。状态与任务事件经 `std_msgs/String` 转发，线上的 JSON 外层包含 `data` 字符串；格式和受理/完成区别见架构文档。

控制服务和动作将 MQTT 截止时间传到 runtime，并在获得执行锁后复检。SDK 默认接受期限为 5 秒，可通过 `Robot(..., command_ttl_s=...)` 或单次调用的 `ttl_s=...` 调整；它限制请求何时可以开始执行，不限制已开始任务的持续时间。动作最终结果同时进入 `responses` 和 `task_events`，在当前进程缓存窗口内重发原 `message_id` 可取回最终响应。状态与普通反馈只保留最新未发送样本，终态和控制响应使用可靠队列。

## 验证与扩展

```bash
source /opt/ros/jazzy/setup.bash
source install/setup.bash
python3 -m pytest -q src
python3 scripts/smoke_test.py
python3 scripts/mqtt_smoke_test.py
python3 scripts/transport_smoke_test.py
python3 scripts/sensor_smoke_test.py
python3 scripts/description_smoke_test.py
```

smoke test 只构造 mock 资产，使用 localhost 和 Domain 187，启动实际 launch 后检查多机器人隔离、能力发现、控制租约、过期速度、导航完成与取消、起降和租约到期。测试会关闭其启动的所有进程，不连接厂商设备或远程 broker。

`mqtt_smoke_test.py` 使用 localhost 和 Domain 188，验证 ROS 侧 MQTT 命令门面的请求关联、去重、过期拒绝、Action 终态和未知遥测。它没有启动 MQTT broker，不作为实际 TLS 网络链路的验收。

`transport_smoke_test.py` 需要本机 `mosquitto`、`mosquitto_pub`、`mosquitto_sub`、`openssl` 和已构建的 Rust debug 二进制。它在临时目录生成证书，使用随机 localhost 端口、双向 TLS 和 Domain 189，启动真实 broker 及 `enable_mqtt=true` 的统一 launch，验证 MQTT → Rust → 业务 gateway → mock 机器人 → 回执/任务事件的完整链路，并中断自己的临时 broker，验证恢复后的任务终态及原请求重试。退出时清理自己的进程与临时文件，不连接远程服务。

`sensor_smoke_test.py` 使用 localhost 和 Domain 190，把 Gemini 335 模板强制切为 mock，并增加模拟雷达与 IMU。验证标准数据、静态 TF、晚加入客户端读取目录、选择性监测和驱动故障时整体退出。测试不加载原厂相机节点，不打开 USB 或真实设备。

`description_smoke_test.py` 使用 localhost 和 Domain 191，验证两个模型的独立 `robot_description`、固定 TF、URDF 中的 IMU 安装点和活动关节 TF。只有测试脚本发布合成关节位置，正常启动不会自动补零关节状态。

新增设备：增加资产实例。新增型号：增加版本化型号文件。新增后端：继承 `chaos_runtime.adapters.base.Adapter` 并注册 Python entry point `chaos.robot_adapters`，提供能力、配置校验、状态与设备操作，再通过配置选择插件。完整方法以基类与内置适配器为准；插件模块导入不得连接硬件，连接必须放在实例构造阶段。

当前尚未实现真实 Nav2 Action 封装、真实飞行控制、LifecycleNode、远程部署管理和跨重启任务恢复。这些边界保留在架构文档中，后续可以逐项扩展而不改变已有资产身份与公共控制约定。
