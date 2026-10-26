# Ubuntu 22.04 / ROS 2 Humble

默认部署环境为普通 Ubuntu 22.04 PC、ROS 2 Humble 和系统 Python 3.10。x86_64 与 ARM64 使用同一份源码，在对应架构上分别构建。模拟运行不需要底盘、无人机 SDK、相机或外部 MQTT broker。

## 准备依赖

先按照 [ROS 官方 Humble 安装说明](https://docs.ros.org/en/humble/Installation/Ubuntu-Install-Debs.html)配置软件源并安装 `ros-humble-ros-base`；需要 RViz 时使用 `ros-humble-desktop`。本项目不替换系统 Python，不需要 conda。

在目标 Ubuntu 22.04 机器安装框架依赖：

```bash
sudo apt update
sudo apt install -y \
  build-essential cmake pkg-config libclang-dev git curl ca-certificates \
  python3-colcon-common-extensions python3-rosdep python3-pytest python3-yaml python3-numpy \
  ros-humble-ament-cmake-python ros-humble-ament-cmake-pytest \
  ros-humble-rosidl-default-generators ros-humble-rclpy \
  ros-humble-geometry-msgs ros-humble-nav-msgs ros-humble-sensor-msgs \
  ros-humble-diagnostic-msgs ros-humble-tf2-ros \
  ros-humble-robot-state-publisher ros-humble-urdfdom ros-humble-xacro \
  ros-humble-rosbag2-py ros-humble-rosbag2-transport \
  ros-humble-rosbag2-storage-default-plugins ros-humble-rosbag2-storage-mcap \
  ros-humble-rmw-fastrtps-cpp chrony
```

Rust/Cargo 需要 **1.89 或更新版本**，因为当前 `Cargo.lock` 中的 `uuid` 等依赖要求较新的工具链。使用 [Rust 官方 rustup 安装方式](https://www.rust-lang.org/tools/install)准备构建工具，确认 `rustc --version` 和 `cargo --version`；不要直接使用 Ubuntu 22.04 初始仓库中的旧编译器。只运行已构建产物的设备不需要 Rust 或 libclang。

首次构建需下载锁定的 Cargo 依赖；缓存已准备好时可在 `colcon build` 命令追加 `--cmake-args -DMQTT_ADAPTER_OFFLINE=ON`。硬件驱动保持独立构建，Gemini 335 见 [驱动说明](orbbec-driver.md)。

## 干净构建与运行

打开没有加载 Jazzy 或旧工作空间的新终端，在项目根目录执行：

```bash
source /opt/ros/humble/setup.bash
colcon --log-base log-humble build --base-paths src \
  --build-base build-humble --install-base install-humble --symlink-install
source install-humble/local_setup.bash

# 检查配置和启动计划
ros2 launch chaos_bringup system.launch.py dry_run:=true

# 运行模拟无人车和无人机
ros2 launch chaos_bringup system.launch.py
```

本目录中的默认 `install/` 若来自 Jazzy，不要叠加加载。ROS 消息扩展、Python ABI 和 Rust 的 ROS 绑定必须在 Humble 下重新生成；改一下 `ROS_DISTRO` 不能转换已有二进制。

另一个终端查看模拟状态：

```bash
source /opt/ros/humble/setup.bash
source install-humble/local_setup.bash
export ROS_DOMAIN_ID=87
export ROS_LOCALHOST_ONLY=1
unset ROS_AUTOMATIC_DISCOVERY_RANGE ROS_STATIC_PEERS
ros2 topic echo /robots/vehicle_001/state
```

`system.launch.py` / `edge.launch.py` 的 `discovery_range:=LOCALHOST` 在 Humble 下映射为 `ROS_LOCALHOST_ONLY=1`；`SUBNET` 映射为 `0`。正式机载入口默认读取 `/opt/ros/humble/setup.bash`；部署账号、配置、驱动与 systemd 见 [机载部署](onboard-deployment.md)。

## 验收

依赖准备完成后，在干净终端运行：

```bash
bash scripts/validate_humble.sh
```

脚本检查 Ubuntu 22.04 与 Humble，使用独立 `build-humble` / `install-humble` / `log-humble`，执行全部包构建、Python 测试以及 MQTT 的 Python/Rust CTest。它不安装系统软件、不删除旧构建目录。

完整模拟链路还需要本机 MQTT 测试工具：

```bash
sudo apt install -y mosquitto mosquitto-clients openssl
bash scripts/validate_humble.sh --smoke
```

`--smoke` 依次执行 7 个现有 smoke 脚本，使用 localhost、测试专用 ROS Domain 和临时证书/录制目录；不连接真实机器人或远端 broker。服务、原生库加载及 rosbag 录制必须在真实 Humble 环境通过该验收后，才能将构建产物作为部署版本。

## 兼容差异与验证边界

- MQTT 构建允许 Humble；锁定的 r2r 0.9.7 已包含 Humble 绑定支持，未升级 Cargo 依赖。
- Humble 发现策略使用 `ROS_LOCALHOST_ONLY`；Jazzy 的新发现变量不会被当作 Humble 的有效隔离配置。
- Humble rosbag2 使用阻塞 `Recorder.record()` / `cancel()`；兼容层在独立进程内管理录制线程、启动确认和限时收尾。没有启用硬件或额外复制图像的数据通道。
- Humble reader 缺少部分 Jazzy 的关闭/排序 API。读取使用对应的资源释放方式，训练导出完整检查选定数据流的接收顺序，发现反序即拒绝，不通过排序掩盖时钟问题。
- Humble 的 bag 时间来自录制回调的系统时钟，MCAP `publishTime` 与 `logTime` 相同；不能解释为独立的 DDS 发送时间。参见 [数据采集时间契约](data-collection.md)。

本次适配开发机为 Ubuntu 24.04 / Jazzy；本机回归、Humble API 契约核查和兼容分支测试不等于 Ubuntu 22.04 / Humble 原生构建及 DDS 验收。具体已执行结果见 [验证记录](validation.md)。
