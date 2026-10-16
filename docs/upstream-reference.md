# Kuavo 参考项目核对与采用范围

核对日期：2026-10-16。参考仓库：[LejuRobotics/kuavo-ros-opensource](https://github.com/LejuRobotics/kuavo-ros-opensource)。本次查看公开 `main` 分支的网页和原始文件，未取得可靠的当前提交 SHA；以下链接不是固定提交快照。

本框架参考其资源组织、接口分层和启动组合方式，在 ROS 2 Jazzy 中重新实现无人车、无人机的统一接入。没有将上游控制器、机器人模型或硬件参数复制到本项目。

## 核对到的事实

### 1. 资产包连接型号资源与硬件配置

`kuavo_assets` 按机器人版本组织资源：配置目录是 `config/kuavo_v<version>`，模型目录是 `models/biped_s<version>`。文档还定义了环境变量、参数服务器和文件名共同使用的机器人版本编码规则。[版本规则](https://github.com/LejuRobotics/kuavo-ros-opensource/blob/main/docs/robot_version%E7%89%88%E6%9C%AC%E5%8F%B7%E8%AF%B4%E6%98%8E.md)

资产包中的 `kuavo.json` 同时服务上层模型计算和底层硬件设置。已查看的 v45 配置包括模型路径、电机数量和类型、末端执行器、运动限制和 IMU 安装信息。这说明资产配置应能选择驱动和资源，而不只是保存机器人编号。[配置说明](https://github.com/LejuRobotics/kuavo-ros-opensource/blob/main/docs/kuavo_json%E6%96%87%E6%A1%A3%E8%AF%B4%E6%98%8E.md)、[v45 配置源码](https://raw.githubusercontent.com/LejuRobotics/kuavo-ros-opensource/main/src/kuavo_assets/config/kuavo_v45/kuavo.json)

**本项目的设计选择：** 将 `model`、`model_version`、资产实例 ID 和配置 `schema_version` 分开。型号资源是可复用默认值，实例保存设备连接与部署覆盖。资源可以包含 URDF、标定和控制器参数；这些可选文件不作为所有机器人都必须拥有的前提。

### 2. 真实设备和仿真复用上层控制组织

真实和 MuJoCo 启动文件都引入 `robot_version_manager.launch`，加载控制器与接口节点。真实启动文件选择 `HardwareNodelet` 或 `HardwareNodeletCXX`；MuJoCo 启动文件引入 `mujoco_cpp/launch/nodelet_with_arm.launch`。两者也有各自专用参数和节点，并不是完全相同的启动文件。[真实启动源码](https://raw.githubusercontent.com/LejuRobotics/kuavo-ros-opensource/main/src/humanoid-control/humanoid_controllers/launch/load_kuavo_real.launch)、[MuJoCo 启动源码](https://raw.githubusercontent.com/LejuRobotics/kuavo-ros-opensource/main/src/humanoid-control/humanoid_controllers/launch/load_kuavo_mujoco_sim.launch)

**本项目的设计选择：** 统一 launch 解析型号和实例，再选择 `mock` 或 `real` 后端。上层统一接口面向能力，后端负责将能力映射到现有底盘驱动或 PSDK。首版 mock 用于接口验证，不宣称是物理仿真；Gazebo、MuJoCo 等后端可以另行增加。

### 3. SDK 以功能组织上层调用

`kuavo_sdk` 包含 `msg`、`srv`、`sdk`、`scripts`。SDK 例子按移动、相机、雷达、手臂、头部、末端执行器和传感器信息等功能分类。[SDK 包](https://github.com/LejuRobotics/kuavo-ros-opensource/tree/main/src/kuavo_sdk)、[功能示例目录](https://github.com/LejuRobotics/kuavo-ros-opensource/tree/main/src/kuavo_sdk/sdk)

**本项目的设计选择：** `chaos_interfaces` 定义 ROS 消息和调用契约，`chaos_sdk` 封装客户端使用方式。设备适配器与 SDK 分开；业务程序不直接依赖 DJI 命令编号或某一底盘话题名。

### 4. 需要重新设计的 ROS 1 假设

所查看 README 使用 `catkin build`、`roslaunch`；启动源码使用 nodelet 和全局参数。其 `ROBOT_VERSION` 默认值来自进程环境。[README](https://github.com/LejuRobotics/kuavo-ros-opensource/blob/main/readme.md)、[真实启动源码](https://raw.githubusercontent.com/LejuRobotics/kuavo-ros-opensource/main/src/humanoid-control/humanoid_controllers/launch/load_kuavo_real.launch)

**本项目的设计选择：** 使用 ament/colcon、ROS 2 launch、每资产命名空间和明确的主机配置。型号由每个资产的配置决定；同一主机上允许多个型号共存。不会把全局 `ROBOT_VERSION` 作为整个机群的型号选择开关，也不依赖 ROS 1 参数服务器实现进程间就绪协调。

## 与当前工作空间的关系

| 项目 | 复用边界 |
|---|---|
| `vehicle_access`、`4g-platform`、`Project` | 按用户给出的无人车代码归类；由设备适配器逐步对接现有驱动和业务 |
| `m3td_psdk` | 保留 PSDK 与设备控制实现；首版旧网关接入限于状态读取 |
| `mqtt_ros2_adapter` | 复用 MQTT 与 ROS 2 传输能力，通过显式路由配置选择统一协议 |
| `LeTools-dev` | 继续作为接口抽象、配置和可扩展结构的参考 |
| `chaos_ws` | 新增 ROS 2 Jazzy 统一配置、接口、运行时、启动、SDK 和协议适配 |

上游展示的是一个机器人产品家族的控制工程。多机器人资产实例、主机选择、能力探测、统一 MQTT 命令和无人机异步动作完成语义，是本项目为当前需求增加的设计，不应称为 Kuavo 已经提供的功能。

后续如果实际引入上游源文件或模型，应记录固定提交、路径和相应许可证；本次参考记录只说明已查看的材料及设计来源。
