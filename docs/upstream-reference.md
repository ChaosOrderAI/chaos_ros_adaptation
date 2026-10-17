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
| `mqtt_ros2_adapter` | 完整传输源码与 Cargo 锁文件已归入 `chaos_ws/src/chaos_mqtt/transport`，由 `chaos_mqtt` 一个 ROS 包统一构建、安装和启动；原兄弟目录保留 |
| `zkdx` | 服务器端任务与协同主脑；连接本机载框架的 MQTT 设备适配器尚待实现 |
| `LeTools-dev` | 继续作为接口抽象、配置和可扩展结构的参考 |
| `chaos_ws` | 新增 ROS 2 Jazzy 统一配置、接口、运行时、启动、SDK 和协议适配 |

框架中的传输实现是独立源码副本，不通过软链接或构建时读取依赖原兄弟目录。后续传输修改在 `chaos_ws/src/chaos_mqtt/transport` 维护，与同包的 Python 业务网关共同交付；内部 Cargo crate 名仍为 `mqtt_ros2_adapter`，对外只注册 `chaos_mqtt` ROS 包，安装入口为 `ros2 run chaos_mqtt mqtt_transport`。原项目需要同步更新时单独核对变更，避免两份源码无意分叉。

上游展示的是一个机器人产品家族的控制工程。多机器人资产实例、主机选择、能力探测、统一 MQTT 命令和无人机异步动作完成语义，是本项目为当前需求增加的设计，不应称为 Kuavo 已经提供的功能。

后续如果实际引入上游源文件或模型，应记录固定提交、路径和相应许可证；本次参考记录只说明已查看的材料及设计来源。

## Kuavo Data Challenge：训练数据链路参考

核对日期：2026-10-17。用户补充的参考项目为 [LejuRobotics/kuavo_data_challenge](https://github.com/LejuRobotics/kuavo_data_challenge)。本次通过公开 GitHub API 确认并读取固定提交 [`c0ca6a48047e1396e02ac1d83df2cd8b6ede17cd`](https://github.com/LejuRobotics/kuavo_data_challenge/commit/c0ca6a48047e1396e02ac1d83df2cd8b6ede17cd)（提交时间 2026-04-03）的源文件，未执行其安装脚本、转换器或训练代码。以下结论是静态核对结果，不代表已完成上游训练复现。

### 采用其分层与配置方式

上游将数据转换、训练、部署分别放在 `kuavo_data`、`kuavo_train`、`kuavo_deploy`；转换配置选择任务描述、相机、目标采样频率及读取块大小，训练配置再选择 ACT、Diffusion 等策略。其主体 ROS 环境是 ROS 1 Noetic，转换代码依赖 `rosbag`/`rospy`。这些依赖不能直接成为 Ubuntu 24.04 / ROS 2 Jazzy 机载框架的依赖。[固定版本 README](https://github.com/LejuRobotics/kuavo_data_challenge/blob/c0ca6a48047e1396e02ac1d83df2cd8b6ede17cd/README.md)、[数据配置](https://github.com/LejuRobotics/kuavo_data_challenge/blob/c0ca6a48047e1396e02ac1d83df2cd8b6ede17cd/configs/data/KuavoRosbag2Lerobot.yaml)

本项目采用“原始采集 → 离线质检与任务划分 → 导出 → 训练 → 受控推理”的组织方式。机载 `chaos_data` 保存 ROS 2 原始消息、主机时间质量和数据来源；离线导出明确观测与控制标签，训练环境另行选择、锁定 LeRobot 和模型依赖。读取块大小是资源管理参数，不是模型预测动作块的长度。当前导出为本项目定义的中间格式，未宣称是 LeRobot 原生数据集；具体入口与边界见 [训练数据规范](training-data.md)。

### 时间对齐需要独立实现

固定版本的分块读取使用 bag 时间，覆盖消息处理器取得的 header 时间；按相机帧号步长降采样，再对各路数据取最近样本，未对离线匹配施加最大时间差。块内不能取到预选样本时还会重新选择最近样本。因此，源数据存在延迟、缺帧或不同钟域时，不能把这种最近匹配视为同步保证。[分块处理源码](https://github.com/LejuRobotics/kuavo_data_challenge/blob/c0ca6a48047e1396e02ac1d83df2cd8b6ede17cd/kuavo_data/common/chunk_process.py)

上游线上观测缓冲使用 `header.stamp` 并支持最大时间差门限；与上述离线路径存在时间语义差异。本项目要求训练导出保留源时间、接收时间和匹配偏差，明确因果可用条件，并使未来推理端复用相同预处理契约。chrony 授时用于主机质量判断，不能替代传感器采样时刻和硬件同步验证。[线上观测缓冲源码](https://github.com/LejuRobotics/kuavo_data_challenge/blob/c0ca6a48047e1396e02ac1d83df2cd8b6ede17cd/kuavo_deploy/utils/obs_buffer.py)

### 观测、动作和任务边界需要保留真实含义

上游状态与命令来自不同话题，并对 Kuavo 机械臂、末端执行器做专用切片；一个 bag 在转换后保存为一个 episode。这些维度和任务边界不能照搬到无人机、无人车。本项目显式描述特征字段、单位、坐标系及控制来源；磁盘分卷和同步质量分段都不自动定义一次任务。机器人测得的速度是观测，不能冒充控制动作标签。[原始话题映射](https://github.com/LejuRobotics/kuavo_data_challenge/blob/c0ca6a48047e1396e02ac1d83df2cd8b6ede17cd/kuavo_data/common/kuavo_dataset.py)

上游转换器将深度逐帧归一化为 uint8 图像。为保留 Gemini 335 的物理深度含义，本项目保存原始数组及编码，不做逐帧 min/max 归一化；米制换算和无效值规则由驱动与训练特征契约明确。缺失或异常样本不能通过改写时间或补零而伪装正常。[转换入口源码](https://github.com/LejuRobotics/kuavo_data_challenge/blob/c0ca6a48047e1396e02ac1d83df2cd8b6ede17cd/kuavo_data/CvtRosbag2Lerobot.py)

本轮实现训练数据准备和接口预留，不安装 GPU 训练环境、不下载模型、不启动训练或实机推理。未来的策略执行需要通过本项目 SDK、控制租约、有效期与限幅，不能绕过运行时直接连接执行器。
