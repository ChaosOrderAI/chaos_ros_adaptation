# Chaos ROS 2 多机器人框架

部署在无人车、无人机或机器狗机载计算机上的 ROS 2 Jazzy 框架，面向 Ubuntu 24.04 的 x86_64 / ARM64 环境。每台设备通过自己的资产配置加载适配器、提供统一 API，并经 MQTT 与平台通信。参考 Kuavo 的版本化资产包、公共接口和后端组织方式，使用 ROS 2 原生接口实现。

机载部署使用 `edge.launch.py` 和单设备配置，见 [机载部署说明](docs/onboard-deployment.md)。下方双 mock 的 `system.launch.py` 用于开发验证。

- [架构与当前能力边界](docs/architecture.md)
- [配置、控制、遥测与训练数据流转](docs/data-flow.md)
- [C++17 迁移后的最终设计与可移植构建命令](docs/cpp-migration.md)
- [行为树节点：导航、起飞与降落](docs/behavior-tree.md)
- [Kuavo 上游参考与采用范围](docs/upstream-reference.md)
- [构建与验证记录](docs/validation.md)
- [机载部署、架构构建与 systemd](docs/onboard-deployment.md)
- [Unitree Go2 机器狗配置、驱动与部署](docs/unitree-go2.md)
- [原生人形机器人后端与 Booster T2](docs/humanoid.md)
- [Go2 / T2 MuJoCo 物理仿真、关节控制与查看器](docs/mujoco.md)
- [庭院点云 Go2 场景](docs/pcd-go2-scene.md) / [绍兴全图](docs/shaoxing-go2-scene.md) / [绍兴 LAS](docs/shaoxing-las-go2-scene.md)
- [Ubuntu 22.04 / Humble 旧车端验收](docs/humble.md)
- [传感器配置、标准接口与诊断](docs/sensors.md)
- [奥比中光 Gemini 335 官方驱动](docs/orbbec-driver.md)
- [URDF / Xacro 导入、TF 归属与 RViz](docs/robot-description.md)
- [数据采集、时间同步与离线审计](docs/data-collection.md)
- [训练数据规范、任务片段与离线导出](docs/training-data.md)
- [示例资产清单](src/chaos_assets/config/demo.yaml)
- [无人车型号](src/chaos_assets/models/lingao_vehicle/v1/model.yaml) / [无人机型号](src/chaos_assets/models/dji_m3td/v1/model.yaml) / [机器狗型号](src/chaos_assets/models/unitree_go2/v1/model.yaml)

## 开发环境构建与启动

工作空间包含 **10 个生产 ROS 包、1 个可选仿真包和 1 个基准包**，业务运行时全部为原生 C++17 实现（Python 仅保留 launch 胶水、薄绑定、第三方 Xacro 桥接及工具/测试脚本）。

运行时依赖 ROS 2 Jazzy、colcon、ament_cmake/ament_cmake_python、rclcpp、geometry_msgs、std_msgs、sensor_msgs、nav_msgs、diagnostic_msgs、tf2_ros、action_msgs、rosidl_default_generators、rosbag2_cpp、rosbag2_storage、rosbag2_transport、rosidl_typesupport_introspection_cpp、nlohmann_json、yaml-cpp、Boost 头文件、OpenSSL。`chaos_mqtt` 内置的 Rust 传输还需 Rust/Cargo 与 `build-essential`、`cmake`、`pkg-config`、`libclang-dev`。模型导入使用 robot_state_publisher、urdfdom；Xacro 模型另需 xacro。现有厂商程序无需参与 mock 构建。

下列 Ubuntu 包是本工作空间的构建与测试依赖，需要在环境准备阶段安装；仓库不自动执行安装：

```text
build-essential cmake pkg-config libclang-dev libxml2-dev liburdfdom-dev
libyaml-cpp-dev libssl-dev nlohmann-json3-dev libboost-dev
python3-venv python3-yaml python3-pytest
ros-jazzy-rosbag2-cpp ros-jazzy-rosbag2-storage-mcap ros-jazzy-rosbag2-storage-sqlite3
ros-jazzy-keyboard-handler ros-jazzy-rosidl-typesupport-introspection-cpp
ros-jazzy-robot-state-publisher ros-jazzy-urdfdom ros-jazzy-xacro
ros-jazzy-behaviortree-cpp
```

NumPy **不是生产依赖**：原生导出用 C++ 写出 NPY 数组，运行期不加载 Python 科学栈。只有仓库自带的 smoke test 脚本用 Python 读取 NPY 并做断言，需要 `python3-numpy`。真实时间质量检查使用 chrony 的 `chronyc`。这些依赖需要在部署环境准备，代码不会自动安装或修改系统授时服务。

首次构建时在本目录执行，明确使用 `--base-paths src`，避免把旁边的 ROS 1 工程和 SDK 一起构建。**使用全新的 build / install / log 目录**，避免旧的 `ament_python` 缓存和并列包索引残留；加载时只 source 本次安装空间的 `local_setup.bash`，不要叠加旧 overlay：

```bash
source /opt/ros/jazzy/setup.bash

# 全新构建目录：build-<tag> / install-<tag> / log-<tag>
colcon --log-base log build --base-paths src \
  --build-base build --install-base install \
  --cmake-args -DCMAKE_BUILD_TYPE=Release

# 只加载本次安装空间，不要再 source 旧的 install/setup.bash
source install/local_setup.bash

# 先检查计划：不启动 ROS 节点、驱动或 MQTT 连接
ros2 launch chaos_bringup system.launch.py dry_run:=true

# 同时运行模拟无人车和无人机；MQTT 默认关闭
ros2 launch chaos_bringup system.launch.py
```

`chaos_mqtt` 是一个 ROS 包，包含**原生 C++ 业务网关**和完整 Rust 传输实现。Cargo 默认执行锁定依赖的 release 构建，并行任务数为 2，可用 CMake 参数 `MQTT_ADAPTER_BUILD_JOBS` 调整。首次构建需要下载依赖；依赖缓存准备完成后，可在构建命令中追加 `--cmake-args -DMQTT_ADAPTER_OFFLINE=ON`。完整框架源码位于本工作空间，构建和运行不依赖旁边的 `mqtt_ros2_adapter` 目录。目标架构、消息绑定范围和生产普通安装见 [部署说明](docs/onboard-deployment.md)。

默认清单使用 ROS Domain 87，只在本机发现。另一个终端查看状态时使用相同环境：

```bash
source /opt/ros/jazzy/setup.bash
source install/local_setup.bash
export ROS_DOMAIN_ID=87
export ROS_AUTOMATIC_DISCOVERY_RANGE=LOCALHOST
ros2 service call /robots/vehicle_001/description chaos_interfaces/srv/GetDescription '{}'
ros2 topic echo /robots/drone_001/state
```

`description` 返回当前后端实际能力。mock 是用于验证接口的运动学模型，不是带碰撞、避障或飞行动力学的仿真器。

需要物理仿真时，按 [MuJoCo 说明](docs/mujoco.md) 显式启用 `chaos_sim`。它提供官方 Go2 / T2 模型、同型号多实例与混合多机仿真、关节 PD 和独立 ROS 命名空间，并可把点云或 3D Gaussian PLY 导入为静态几何代理；这不是原生 splatting 渲染。平衡策略及统一速度行走接口尚未接入。

## 配置组织

```text
src/
  chaos_core/         共享值对象、错误与时间原语；无 ROS 依赖的头文件库
  chaos_interfaces/   状态、控制租约、速度、停止、导航与飞行动作契约
  chaos_assets/       型号版本、实例清单、配置校验与解析
  chaos_bringup/      统一 launch 规划器与 launch 胶水
  chaos_runtime/      每资产运行时、后端适配契约与 Go2 驱动
  chaos_sdk/          原生 C++ 异步客户端（薄 Python 绑定）
  chaos_bt/           BehaviorTree.CPP 节点：导航、起飞、降落的适配器
  chaos_mqtt/         一个 ROS 包，统一构建与安装原生 C++ 网关和 Rust 传输
    transport/        完整 Rust 传输源码及 Cargo 锁文件（导入的第三方实现，不在本轮修改范围）
  chaos_sensors/      模拟传感器、数据新鲜度诊断与传感器目录
  chaos_data/         原生 rosbag2 采集、时间质量门控、审计与离线训练数据导出
  chaos_sim/          可选 MuJoCo Go2 / T2 物理仿真；点云/3DGS 几何代理、独立关节控制与标准 ROS 观测
  chaos_benchmarks/   领域微基准（非生产入口，不参与运行）
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
| mock quadruped | 状态、控制租约、前后/横移/转向、停止、导航 Action | 无设备 |
| `lingao_ros2` | 新鲜 Odometry 状态、经校验的 Twist 速度与受控停止 | 现有底盘驱动已运行，配置实际 topic 和 frame |
| `dji_psdk_legacy` | 订阅旧网关 ROS 状态 | 旧网关已经产生 `system/status`；暂无统一飞行控制 |
| `unitree_go2` | 新鲜 Odometry 状态、平面速度与受控停止；统一 launch 启动 `go2_driver` | 原生 Unitree SDK2：以 `-DCHAOS_GO2_WITH_UNITREE_SDK=ON` 构建，配置设备网卡和 DDS domain |
| mock humanoid | 状态、控制租约、平面速度、停止、导航 Action | 无设备 |
| `humanoid`（`backend: booster_t2`） | 新鲜 Odometry 状态、平面速度与受控停止；统一 launch 启动 `humanoid_driver` | 原生 Booster Robotics SDK：以 `-DCHAOS_HUMANOID_WITH_BOOSTER_SDK=ON` 构建，配置设备网卡和 DDS domain |

[edge.example.yaml](src/chaos_assets/config/edge.example.yaml) 是接入模板，域号、话题和 frame 必须与目标设备核对。车辆和 DJI 运动后端采用 attach：底盘驱动和旧 DJI 网关仍由既有服务管理。Go2 驱动由统一 launch 管理，传感器可另选 node 模式纳入统一 launch。无人车旧部署使用 Humble，Jazzy 下的底盘驱动构建与实机验证仍需完成。

机器狗使用独立的 [go2.demo.yaml](src/chaos_assets/config/go2.demo.yaml) 和 [go2.onboard.example.yaml](src/chaos_assets/config/go2.onboard.example.yaml)，默认双 mock 示例保持不变。Go2 real 后端只提供状态、速度与停止；实机导航、站起/趴下、姿态控制、视频和整机 URDF 尚未接入。SDK 安装、控制源切换及机载验收见 [Go2 说明](docs/unitree-go2.md)。

```bash
ros2 launch chaos_bringup system.launch.py \
  inventory:="$PWD/src/chaos_assets/config/go2.demo.yaml"
```

### Unitree Go2 原生 SDK2 构建

`chaos_runtime` 的 CMake 选项 `CHAOS_GO2_WITH_UNITREE_SDK` **默认 OFF**：

- **OFF（默认）**：不链接厂商 SDK。支持 mock 模式与全部本地校验；real 模式在预检阶段显式失败并给出原因，不会伪装成 SDK 可用。
- **ON**：需要 `find_package(unitree_sdk2 CONFIG REQUIRED)` 成功，即把上游 SDK2 的安装前缀加入 `CMAKE_PREFIX_PATH`。此时后端编译并链接厂商库。

```bash
source /opt/ros/jazzy/setup.bash
colcon --log-base log-sdk build --base-paths src \
  --build-base build-sdk --install-base install-sdk --symlink-install \
  --cmake-args -DCHAOS_GO2_WITH_UNITREE_SDK=ON \
               -DCMAKE_PREFIX_PATH="$UNITREE_SDK2_PREFIX"
source install-sdk/local_setup.bash
```

运行时必须让厂商库的搜索路径**排在 ROS 库目录之前**，否则会解析到与厂商 `libddscxx` 不配套的 ROS `libddsc`，形成版本错配：

```bash
export LD_LIBRARY_PATH="$UNITREE_SDK2_PREFIX/lib:/opt/ros/jazzy/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
```

构建产物可用下面的命令自证能力，不需要连接机器狗：

```bash
ros2 run chaos_runtime go2_driver --build-capabilities   # 输出 {"unitree_sdk2":true}
```

SDK-ON 只验证了**编译与链接**，尚未在真实机器狗上验证过写入时序、DDS 发现延迟和硬件响应。详见 [Go2 说明](docs/unitree-go2.md)。

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

`chaos_mqtt` 统一包含 MQTT / ROS 2 传输与业务协议转换：业务网关是**原生 C++** 可执行文件，Rust 传输源码保留在包内 `transport/`（导入的第三方实现，本轮未修改），见 [包内说明](src/chaos_mqtt/README.md)。`route_profile: custom` 只加载显式路由；传输实现缺省的 `legacy_drone` 保持旧配置行为。准备 broker 与设备证书，然后填写 [mqtt.example.yaml](src/chaos_assets/config/mqtt.example.yaml)。

```bash
ros2 launch chaos_bringup system.launch.py \
  inventory:=/absolute/path/mqtt.yaml host_id:=dev enable_mqtt:=true
```

launch 为每个资产生成单独的 Rust JSON，通过 `chaos_mqtt` 的 ament 包索引找到已安装的 `mqtt_transport`，用 `--check-config` 在启动任何节点前校验真实配置，再启动同包内的 `gateway` 和 `mqtt_transport` 两个受管进程。通信配置通常省略 `executable`；旧裸名称 `mqtt_ros2_adapter` 兼容解析到同一个包内传输，已有显式可执行文件路径仍可用于外部版本。默认启动无需将二进制加入 `PATH` 或复制到 `/opt/chaos/bin`。`dry_run:=true` 仅检查计划，不要求本机存在二进制、证书，也不执行原生配置检查。

需要独立排查中间件配置时，可以使用标准 ROS 包入口：

```bash
ros2 run chaos_mqtt mqtt_transport --check-config /absolute/path/config.json
```

框架日常运行使用资产 YAML 生成这份 JSON，统一 launch 管理中间件进程。服务器端 `zkdx` 的设备适配器接入仍需单独完成；源码归并不会自动改变服务器协议。

默认 MQTT 主题为 `robots/<id>/commands`、`responses`、`state`、`task_events`。外层命令沿用原协议 `version/message_id/timestamp_ms/command`；内部命令名按能力生成。状态与任务事件经 `std_msgs/String` 转发，线上的 JSON 外层包含 `data` 字符串；格式和受理/完成区别见架构文档。

控制服务和动作将 MQTT 截止时间传到 runtime，并在获得执行锁后复检。SDK 默认接受期限为 5 秒，可通过 `Robot(..., command_ttl_s=...)` 或单次调用的 `ttl_s=...` 调整；它限制请求何时可以开始执行，不限制已开始任务的持续时间。动作最终结果同时进入 `responses` 和 `task_events`，在当前进程缓存窗口内重发原 `message_id` 可取回最终响应。状态与普通反馈只保留最新未发送样本，终态和控制响应使用可靠队列。

## 数据采集

资产的 `data_collection.enabled: true` 启用独立 `chaos_data` 包，统一启动后将本机状态、相机/雷达/IMU、TF 和模型录制为分卷 bag（存储插件由资产配置指定，默认示例为 MCAP，也支持 SQLite）。录制由**原生 rosbag2_cpp 进程**完成，监督、时钟门控、审计和离线导出也是原生实现，不依赖 Python 科学栈。默认要求 chrony 时间质量合格；失同步或检测到墙钟跳变时结束当前段，恢复稳定后重新开始，保留原消息时间戳及质量证据。10 ms 默认门限只针对主机时钟，不代表多传感器硬件同步精度。

```bash
ros2 launch chaos_bringup system.launch.py \
  inventory:="$PWD/src/chaos_assets/config/data_collection.demo.yaml"
```

此示例使用 mock 数据，显式允许未验证的时钟，输出到 `/tmp/chaos-recordings`。生产配置、统一 chrony/NTP 模板、回放与训练索引命令见 [数据采集说明](docs/data-collection.md)。启用 MQTT 后，只读状态发布至 `robots/<id>/data_collection/status`；原始数据留在本机。

参考 Kuavo Data Challenge 的数据、训练、部署分层，新增独立离线 `data_export`。它按显式任务 episode、观测字段和动作空间导出数值 JSONL、原始图像 NPY 及时间/来源信息。运行时的 `control_trace` 记录不含租约 token 的控制发令和失效事件，默认一并采集；停止或超时后不会无限保持旧动作标签。训练侧如何选择可信时段、避免未来观测泄漏以及无人车/无人机模板见 [训练数据说明](docs/training-data.md)。

```bash
ros2 run chaos_data data_export /data/analysis/profile.yaml \
  --output /data/datasets/task_v001
```

这是本项目的中间数据格式；LeRobot 格式适配、模型训练和机载推理仍为后续模块。新增 `ControlTrace` 后需重新构建并部署 `chaos_interfaces` 和运行时，离线导出环境也需要可加载相同消息接口。

## 验证与扩展

```bash
source /opt/ros/jazzy/setup.bash
source install/local_setup.bash

# 单元测试：按包选择，避免把未参与本次构建的包算进来
colcon test --packages-select chaos_core chaos_interfaces chaos_assets \
  chaos_bringup chaos_runtime chaos_sensors chaos_sdk chaos_bt chaos_mqtt chaos_data \
  --build-base build --install-base install
colcon test-result --test-result-base build --verbose

# Python 侧 smoke test（需要 python3-numpy 读取导出的 NPY）
python3 scripts/smoke_test.py
python3 scripts/go2_smoke_test.py
python3 scripts/mqtt_smoke_test.py
python3 scripts/transport_smoke_test.py
python3 scripts/sensor_smoke_test.py
python3 scripts/description_smoke_test.py
python3 scripts/data_smoke_test.py
python3 scripts/training_data_smoke_test.py
```

`colcon test` 与 `colcon test-result` 必须指向**本次构建的目录**（`--build-base` / `--test-result-base`），否则读到的是旧安装空间的残留结果。逐项复现命令、已验证事实与仍未完成的检查见 [验证记录](docs/validation.md)。

smoke test 只构造 mock 资产，使用 localhost 和 Domain 187，启动实际 launch 后检查多机器人隔离、能力发现、控制租约、过期速度、导航完成与取消、起降和租约到期。测试会关闭其启动的所有进程，不连接厂商设备或远程 broker。

`go2_smoke_test.py` 使用独立的 Go2 mock 清单，通过统一 launch 和 SDK 验证机器狗的横移、转向、垂直运动拒绝、停止、导航及命令超时；不加载 Unitree SDK 或连接机器狗。配置与启动命令见 [Go2 说明](docs/unitree-go2.md)。

`mqtt_smoke_test.py` 使用 localhost 和 Domain 188，验证 ROS 侧 MQTT 命令门面的请求关联、去重、过期拒绝、Action 终态和未知遥测。它没有启动 MQTT broker，不作为实际 TLS 网络链路的验收。

`transport_smoke_test.py` 需要本机 `mosquitto`、`mosquitto_pub`、`mosquitto_sub`、`openssl` 和已加载的本工作空间安装环境。测试使用随框架安装的 Rust 中间件，在临时目录生成证书，使用随机 localhost 端口、双向 TLS 和 Domain 189，启动真实 broker 及 `enable_mqtt=true` 的统一 launch，验证 MQTT → Rust → 业务 gateway → mock 机器人 → 回执/任务事件的完整链路，并中断自己的临时 broker，验证恢复后的任务终态及原请求重试。退出时清理自己的进程与临时文件，不连接远程服务。

`sensor_smoke_test.py` 使用 localhost 和 Domain 190，把 Gemini 335 模板强制切为 mock，并增加模拟雷达与 IMU。验证标准数据、静态 TF、晚加入客户端读取目录、选择性监测和驱动故障时整体退出。测试不加载原厂相机节点，不打开 USB 或真实设备。

`description_smoke_test.py` 使用 localhost 和 Domain 191，验证两个模型的独立 `robot_description`、固定 TF、URDF 中的 IMU 安装点和活动关节 TF。只有测试脚本发布合成关节位置，正常启动不会自动补零关节状态。

`data_smoke_test.py` 使用 localhost 和 Domain 192，临时注入 chrony 查询结果，验证等待同步、失步关段、恢复、MCAP 分卷、相机/雷达/IMU 原始时间戳、静态 TF 与模型重订阅，以及退出刷盘和离线索引。它不调整系统时钟，也不验收真实 NTP 精度。传输 smoke 同时检查只读采集状态的 MQTT 上报。

`training_data_smoke_test.py` 使用 localhost 和 Domain 193，通过 SDK 控制 mock 机器人，验证脱敏控制 trace、停止失效、原生录制和安装版离线导出。它检查严格模式拒绝未验证授时、显式 mock 豁免、图像精度及输出不覆盖，不启动学习模型或真实设备。

新增设备：增加资产实例。新增型号：增加版本化型号文件。新增后端：实现原生 C++ 适配契约 `chaos::runtime::Adapter`（`src/chaos_runtime/include/chaos_runtime/adapter.hpp`），提供 `snapshot()`、`setVelocity()`、`stop()`、`startOperation()` / `pollOperation()` 等能力，并登记到运行时的后端工厂，再通过配置选择该后端。完整方法以该头文件与内置适配器为准；加载适配器时不得连接硬件，连接必须放在实例构造阶段。launch 通过 `chaos_bringup` 的原生预检接口调用运行时校验器，共用资产规则。

当前尚未实现真实 Nav2 Action 封装、真实飞行控制、LifecycleNode、远程部署管理和跨重启任务恢复。这些边界保留在架构文档中，后续可以逐项扩展而不改变已有资产身份与公共控制约定。

## 原生人形机器人（Booster T2）

`chaos_runtime` 新增厂商中立的人形后端契约与原生 C++17 可执行程序 `humanoid_driver`，复用现有 ROS / SDK / MQTT 主接口。真实后端当前只提供状态、平面速度与停止，mock 额外提供导航位姿。

```bash
# 双 mock 演示：dry_run 只做规划检查，去掉 dry_run:=true 才真正启动
ros2 launch chaos_bringup system.launch.py \
  inventory:="$PWD/src/chaos_assets/config/humanoid.demo.yaml" dry_run:=true

# 厂商 SDK 开关自证（默认 OFF）
ros2 run chaos_runtime humanoid_driver --build-capabilities   # {"booster_sdk":false}
```

- 实机模板：[t2.onboard.example.yaml](src/chaos_assets/config/t2.onboard.example.yaml)；型号 `src/chaos_assets/models/booster_t2/v1/model.yaml`。
- 开启厂商 SDK：`-DCHAOS_HUMANOID_WITH_BOOSTER_SDK=ON -DBOOSTER_SDK_ROOT=/absolute/sdk`。
- 能力边界、frame 选项、安全语义与未完成部分见 [人形机器人后端说明](docs/humanoid.md)。
