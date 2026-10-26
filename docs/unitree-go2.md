# Unitree Go2 机器狗接入

Go2 使用独立的 [型号配置](../src/chaos_assets/models/unitree_go2/v1/model.yaml)，资产类型为 `quadruped`。本框架实现 `unitree_go2` 适配器与 `chaos_runtime` 包内的 `go2_driver`，并通过**原生 Unitree SDK2**（上游 `unitree_sdk2`，由 CMake 选项门控）访问机器狗。`system.launch.py` 和 `edge.launch.py` 在选择 Go2 的 real 后端时自动启动驱动，由同一个 launch 管理其生命周期。

real 后端提供状态、平面速度（前后、横移、转向）和软件停止；mock 另外提供运动学导航，便于验证统一接口。当前不提供实机导航、自动站起/趴下、姿态控制、视频或整机 URDF。`sbdog` 的地图、构建/安装产物及私有连接配置不参与本工作空间安装；旧网关的 MQTT、导航与控制线程也不会被启动。

## 通信和配置

通信路径为：上层 `chaos_sdk.Robot` / MQTT → 统一 runtime 的租约和命令校验 → `DriverVelocityCommand` → `go2_driver` → Unitree SDK 的运动接口；厂商 SportModeState 回调产生 ROS Odometry，再由适配器提供统一状态。驱动只在收到新的厂商状态时发布里程计，不给缓存状态反复刷新时间戳；驱动按命令携带的绝对截止时间停止输出，以处理传输延迟和 runtime 不再发令的情况。

Odometry 的 ROS 时间戳记录新 DDS 数据包的本机接收时间。设备时间戳只用于识别重复和倒序数据，不被当作 UTC 时间；这种转换不代表设备和主机已经完成硬件时间同步。

- ROS 速度话题默认 `/go2/cmd_vel`，类型 `chaos_interfaces/msg/DriverVelocityCommand`，使用资产 base frame。它是内部驱动接口，上层控制通过统一租约 API 发令。消息保留原始 source stamp，并携带时钟域、实际发布时间、不可延长的 `CLOCK_MONOTONIC` 截止时间及显式停止标志；停止消息六轴为零，不要求运动截止时间。
- ROS 状态话题默认 `/go2/odom`，类型 `nav_msgs/msg/Odometry`；模板使用 `dog_001/odom` 和 `dog_001/base_link`。
- 厂商 DDS 状态话题默认 `rt/sportmodestate`，DDS domain 为 `0`，连接网卡示例为 `eno1`。网卡必须改为目标机实际连接机器狗的接口。
- `velocity_frame: odom` 表示厂商状态中的线速度按世界/里程计坐标系解释，与 [Unitree 官方状态示例](https://github.com/unitreerobotics/unitree_ros2/blob/master/example/src/src/read_motion_state.cpp) 一致；驱动使用完整姿态四元数把它转换到 Odometry 的机体坐标系。部署时仍须核实设备固件的坐标约定。上层速度命令本身使用机体坐标系。
- 开发 mock 使用 ROS Domain `87`；机载模板的 ROS Domain `55` 来自旧部署，须按实际 ROS 网络核实。ROS Domain 与 Unitree SDK 的 DDS domain 是两个独立配置，不能直接用 `55` 覆盖 SDK 的 `0`。

型号限值为平面线速度 `0.3 m/s`、角速度 `0.5 rad/s`、命令超时 `0.3 s`、状态超时 `1.0 s`、最长控制租约 `30 s`。速度控制需要持续发送新样本，过期、租约失效或状态失鲜时停止输出运动命令。软件零速度/停止指令依赖设备通信链路，不是硬件急停。

多台 Go2 出现在同一 ROS Domain 时，每个资产应分别覆盖 `cmd_vel_topic`、`odom_topic`、frames、网络接口及资产 ID。默认型号中的绝对话题不能用于同域多机共享。

runtime 和驱动必须处于同一主机、同一次启动和同一个 Linux time namespace。时钟域由 boot ID 与 time namespace 身份组成，不匹配或缺失时拒绝运动。排队、校验、DDS 传输和后续续租都不能延长已发命令的截止时间。

内部消息类型变更需要一次完整迁移：从同一修订重新生成 `chaos_interfaces`，在同一安装前缀重建 runtime 和驱动，再成对部署、重启或回滚。旧 `TwistStamped` 与新消息实例不能混用。本次代码验证不包含实机部署。

## 构建与 SDK 依赖

纯 mock 和配置 dry run 不需要 Unitree SDK，也不会连接机器狗。按 [README](../README.md) 安装 ROS 2 Jazzy 与框架依赖，在 `chaos_ws` 执行：

```bash
source /opt/ros/jazzy/setup.bash
colcon --log-base log-cpp build --base-paths src \
  --build-base build-cpp --install-base install-cpp \
  --cmake-args -DCMAKE_BUILD_TYPE=Release
source install-cpp/local_setup.bash
```

### 原生 SDK2 门控

`chaos_runtime` 的 CMake 选项 `CHAOS_GO2_WITH_UNITREE_SDK` 决定 real 后端是否链接厂商 SDK，**默认 OFF**：

| 选项 | 行为 |
|---|---|
| `OFF`（默认） | 不链接厂商 SDK。支持 mock 与全部本地校验；**real 模式在启动阶段显式失败**并给出原因，不会伪装成 SDK 可用 |
| `ON` | 需要 `find_package(unitree_sdk2 CONFIG REQUIRED)` 成立，即上游 SDK2 的安装前缀在 `CMAKE_PREFIX_PATH` 中。后端编译并链接厂商库 |

SDK-OFF 时启动 real 后端会得到：

```text
[FATAL] [go2_driver]: startup failed: chaos_runtime was built without
Unitree SDK2 support (CHAOS_GO2_WITH_UNITREE_SDK=OFF); a real Go2 driver
cannot start.
```

启用 SDK 的构建：

```bash
source /opt/ros/jazzy/setup.bash

colcon --log-base log-cpp-sdk build --base-paths src \
  --build-base build-cpp-sdk --install-base install-cpp-sdk --symlink-install \
  --cmake-args -DCHAOS_GO2_WITH_UNITREE_SDK=ON \
               -DCMAKE_PREFIX_PATH="$UNITREE_SDK2_PREFIX"

source install-cpp-sdk/local_setup.bash
```

`$UNITREE_SDK2_PREFIX` 指向上游 SDK2 的安装前缀（不是系统安装位置）。SDK 通过 `find_package` 的导出 target 消费，不手写 include 或库路径。

### 运行时库搜索顺序

SDK 前缀自带 `libddsc.so.0` 与 `libddscxx.so.0`，而 ROS 2 Jazzy 自己也带一份 `libddsc.so.0`（`/opt/ros/jazzy/lib/x86_64-linux-gnu/`）。**厂商库目录必须排在 ROS 库目录之前**，否则会解析到与厂商 `libddscxx` 不配套的那一份 `libddsc`，形成版本错配：

```bash
export LD_LIBRARY_PATH="$UNITREE_SDK2_PREFIX/lib:/opt/ros/jazzy/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
```

生产部署把这个变量写进服务环境文件，见 [机载部署](onboard-deployment.md) 与 [部署模板](../deploy/edge.env.example)。

### 能力自证

构建产物可以自证是否包含厂商后端，不需要连接机器狗，也不会初始化厂商 DDS 传输：

```bash
ros2 run chaos_runtime go2_driver --build-capabilities
# SDK-ON:  {"unitree_sdk2":true}
# SDK-OFF: {"unitree_sdk2":false}
```

该查询只回答"这个二进制里有没有真实后端"，不说明硬件是否可用、网络是否可达或机器狗的状态。

**当前状态**：SDK-ON 只验证了**编译与链接**，从未在真实机器狗上运行。真实写入时序、DDS 发现延迟和硬件响应均未验证。mock 构建不要求安装 SDK。

后续示例使用 `install-cpp/local_setup.bash`；若采用独立构建目录，改为加载对应前缀的 `local_setup.bash`，不要同时叠加旧安装空间。

实机生产安装应按 [部署说明](onboard-deployment.md) 在目标架构构建普通安装。服务运行账号须能访问 SDK 前缀的库目录和机器狗网卡。

## 纯 mock 验证

独立 [go2.demo.yaml](../src/chaos_assets/config/go2.demo.yaml) 只包含 `dog_001`，原默认无人车/无人机双 mock 示例保持不变。

```bash
# 本目录为 chaos_ws；先查看解析结果和启动计划
ros2 run chaos_assets asset_config src/chaos_assets/config/go2.demo.yaml
ros2 launch chaos_bringup system.launch.py \
  inventory:="$PWD/src/chaos_assets/config/go2.demo.yaml" dry_run:=true

# 启动机器狗运动学 mock，不加载厂商 SDK
ros2 launch chaos_bringup system.launch.py \
  inventory:="$PWD/src/chaos_assets/config/go2.demo.yaml"
```

另一个终端加载相同 ROS 和工作空间环境，使用同一个域：

```bash
export ROS_DOMAIN_ID=87
export ROS_AUTOMATIC_DISCOVERY_RANGE=LOCALHOST
ros2 service call /robots/dog_001/description chaos_interfaces/srv/GetDescription '{}'
ros2 topic echo /robots/dog_001/state
```

以下 SDK 示例用于已启动的 mock，展示横移与停止。`chaos_sdk` 是原生 C++ 异步客户端，Python 只是薄绑定：不带 node 参数的构造函数会**自行拥有 ROS 上下文、节点和 executor 线程**，`Future.result()` 阻塞到请求完成，因此不需要 `rclpy.init()` 或手动 spin。`describe()` 缓存实际 base frame 和命令超时；必须等待控制租约成功后才能发布速度。发布一次 `velocity()` 只表示入队，受理结果见 `/robots/dog_001/command_results`。

```python
import time

from chaos_sdk import Robot

# 自带上下文与 executor 线程的独立客户端
robot = Robot('dog_001')


def wait(future):
    result = future.result()
    if isinstance(result, dict) and result.get('success') is False:
        raise RuntimeError(f"{result.get('code')}: {result.get('message')}")
    return result


try:
    if not robot.wait_ready():
        raise RuntimeError('runtime not discovered')
    wait(robot.describe())
    wait(robot.acquire('go2_mock_demo', duration_s=5.0))
    end = time.monotonic() + 1.0
    while time.monotonic() < end:
        robot.velocity(x=0.0, y=0.1, yaw=0.0, valid_for_s=0.2)
        time.sleep(0.05)
    wait(robot.stop())
finally:
    try:
        if robot.lease_token:
            wait(robot.release())
    finally:
        robot.close()
```

若应用已经拥有 rclpy 节点，旧签名 `Robot(node, asset_id)` 仍然可用，但传入的节点会被忽略——原生客户端始终使用自己的上下文。

`x`、`y` 单位为 m/s，`yaw` 为 rad/s，均按资产机体坐标系解释。Go2 支持平面横移，`z` 必须为零。mock 仅检查接口与运动学行为，不模拟足端接触、地形或跌倒。

构建并加载安装空间后，可运行独立 mock 集成验证。脚本自行启动与关闭测试 launch，不需要 Unitree SDK，也不连接硬件或远程 broker：

```bash
python3 scripts/go2_smoke_test.py
```

## 机载配置与启动

使用 [go2.onboard.example.yaml](../src/chaos_assets/config/go2.onboard.example.yaml) 作为 `/etc/chaos/asset.yaml`，填写设备的网卡、资产 ID、frame、域号，以及启用 MQTT 时的 broker 和证书。它使用 `package://chaos_assets/...` 引用型号，复制到源码目录之外后也能通过已安装的包解析。密码只通过模板指定的环境变量读取，不复制旧工程的连接凭据。

先用模板检查实机启动计划。dry run 不加载 SDK、不发送运动指令，也不验证真实网络连接：

```bash
ros2 launch chaos_bringup edge.launch.py \
  inventory:="$PWD/src/chaos_assets/config/go2.onboard.example.yaml" \
  asset_id:=dog_001 enable_mqtt:=false dry_run:=true
```

实机启动前，按设备接管流程退出旧 `sbdog` 网关、独立 SDK 控制程序、旧 MQTT 控制桥及导航等其他运动命令源。启动后不会自动站起或切换姿态，需通过原厂允许的操作流程使机器人处于可接受速度控制的状态。确认环境和设备状态后，在目标机启动已配置的单设备资产：

```bash
ros2 launch chaos_bringup edge.launch.py \
  inventory:=/etc/chaos/asset.yaml asset_id:=dog_001 enable_mqtt:=false
```

该命令会同时启动 `go2_driver` 和 runtime，无需再启动旧网关。启用平台通信时，在完成 broker 和证书配置后改用 `enable_mqtt:=true`；systemd 部署沿用 [现有模板](onboard-deployment.md)，将 `CHAOS_ASSET_ID` 设置为 `dog_001`。real 模式要求安装空间来自 SDK-ON 构建（`CHAOS_GO2_WITH_UNITREE_SDK=ON`），并在服务环境中把 SDK 库目录放在 ROS 库目录之前（见上文"运行时库搜索顺序"）。

查看实机 ROS 状态使用模板的 `ROS_DOMAIN_ID=55` 与相同发现范围。先检查 `/go2/odom` 和 `/robots/dog_001/state` 连续更新，再检查 `description` 只宣告 `state`、`motion.velocity`、`motion.stop`。控制路径使用 SDK 的租约与速度 API，不直接向内部驱动话题发布；真实 `navigation.pose` 会被拒绝。

配置解析和 mock 测试只覆盖软件行为。实机上仍需验收网卡/DDS 连通性、固件速度坐标约定、状态中断、控制进程中断时的停止效果，以及与其他控制源的切换；当前代码集成不代表这些实机项目已经通过。

## 本机驱动链路验证

构建并加载工作空间后，可运行 `python3 scripts/go2_driver_smoke_test.py`。该脚本在本机使用**原生 ROS 级假 SDK 测试二进制**（`test_go2_driver_ros_cpp`），通过工厂注入的假后端驱动真实的 Go2 驱动、适配器和 runtime：它验证状态转换、租约速度、实机导航拒绝、遥测失效、畸形遥测、SDK 写故障，以及暂停 runtime 定时器后驱动仍独立输出零速度。脚本同时断言 `unitree_sdk2py` 没有出现在任何输出中，即真实厂商 SDK 路径从未被导入。此测试需要本机 ROS socket 权限，不连接厂商 DDS、硬件或 MQTT。

驱动拒绝重复或倒序的运动命令时间戳，停止指令也会阻止更早的运动命令恢复。runtime 侧同样在每一租约内以整数纳秒水位拒绝相等或倒退的速度源时间戳，且被接受的 `motion.stop` 会先撤销当前租约再发出停止输出——撤销后旧 token 不能再发令，重新控制必须重新申请租约；停止回执只确认本机停止输出已下发，不代表设备已经停下。设备状态时间戳倒退（例如机器人重启）后应重启驱动；SDK 发送在初始化成功后发生错误时也会锁止运动，直到驱动重启。重启不会恢复旧租约或运动命令。验证结果见 [验证记录](validation.md)。
