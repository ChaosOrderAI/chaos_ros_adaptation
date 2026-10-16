# 无人车 / 无人机机载部署

本工程运行在每台机器人上的 Linux 计算机。部署基线为 Ubuntu 24.04 + ROS 2 Jazzy，CPU 可以是 x86_64 或 ARM64。无人机侧对应伴随计算机，例如运行 PSDK 网关的 Orin；无人车侧对应车载工控机。框架为各自设备提供统一接口。

```text
无人车计算机                         无人机伴随计算机
asset.yaml（vehicle_001）             asset.yaml（drone_001）
          ↓                                     ↓
统一 runtime + 车辆适配器             统一 runtime + 无人机适配器
          ↓ 本机 ROS 2                         ↓ 本机 ROS 2
现有底盘驱动                         现有 PSDK 网关
          ↕                                     ↕
chaos_mqtt + MQTT 中间件              chaos_mqtt + MQTT 中间件
          ↘          平台 MQTT broker          ↙
```

机载框架只启动当前设备的资产。平台通过 MQTT 使用它的状态与能力；本地算法、遥控和任务程序可以直接使用 ROS 2 / Python SDK。速度 TTL、控制租约和运行中任务的检查在本机执行。远端连接中断时，中间件重连，本机运行时仍按有效期处理已有控制。

## 1. 按目标架构构建

Python 源码、ROS 接口定义与资产格式共用；生成的 ROS 消息扩展、Rust 二进制和厂商库需要匹配目标 CPU 与系统。不要把本开发机的 x86_64 二进制直接用于 ARM64。

建议在目标设备或相同架构、相同系统的构建环境中构建。每种架构只安装与该机器相关的厂商驱动；mock 和另一类机器人的 SDK 不需要在实机启动时加载。具体厂商栈支持的硬件/系统还需要各自核对，使用 Jazzy 不代表现有 Humble 驱动或 PSDK 已经完成迁移。

在目标机完成 ROS 2 Jazzy 及 README 中的构建依赖安装后，从 `chaos_ws` 执行：

```bash
source /opt/ros/jazzy/setup.bash
# /opt/chaos/ros 预先由管理员创建，并允许构建账号写入。
colcon --log-base log-onboard build --base-paths src --build-base build-onboard --merge-install \
  --install-base /opt/chaos/ros \
  --cmake-args -DCMAKE_BUILD_TYPE=Release
source /opt/chaos/ros/local_setup.bash
```

生产安装使用普通安装，在最终前缀构建；不要把当前开发用的 `--symlink-install` 目录直接复制到机载计算机。上面的新 build/log 目录用于隔离开发缓存；也可以在独立源码副本中构建。导入本机 Jazzy 后加载 `local_setup.bash`，避免带入开发机记录的其他工作空间路径。

MQTT 中间件按 [现有边端构建说明](../../mqtt_ros2_adapter/README.md) 在相同架构环境构建，将得到的可执行文件安装到 `/opt/chaos/bin/mqtt_ros2_adapter`。构建中间件无需导入 `chaos_interfaces`：当前桥接使用标准 String 消息，业务转换在 `chaos_mqtt` 中完成。检查其动态库依赖全部可用。

本轮为控制服务和动作增加了 `expires_at` 字段，并扩展 MQTT 终态响应和发送策略。升级时需要统一重建接口、runtime、gateway、SDK 及其他接口使用者，同时更新 Rust 中间件；旧、新 ROS 接口产物不能混跑。更新文件后按既有停机流程重启本机框架，新进程不会恢复旧控制租约。请求期限使用 Unix UTC，设备和平台需保持时钟同步。

## 2. 每台机器只配置自己的资产

部署目录约定：

```text
/opt/chaos/ros/                  本机架构的 ROS 安装空间
/opt/chaos/drivers/              可选的独立传感器驱动安装空间
/opt/chaos/bin/                  本机架构的 MQTT 中间件
/opt/chaos/deploy/start-edge.sh  服务启动入口
/etc/chaos/asset.yaml           当前机器人资产
/etc/chaos/edge.env             当前资产 ID、路径与可选密码
/etc/chaos/certs/               当前设备的客户端证书及 CA
/var/lib/chaos/                 服务运行目录
/var/log/chaos/                 ROS 日志
```

无人车使用 [vehicle.onboard.example.yaml](../src/chaos_assets/config/vehicle.onboard.example.yaml)，无人机使用 [drone.onboard.example.yaml](../src/chaos_assets/config/drone.onboard.example.yaml)。将适用的一个模板复制为 `/etc/chaos/asset.yaml`，设置自己的资产 ID、ROS Domain、broker 和证书。模板中的域号和 broker 仅为示例。

型号引用采用 `package://chaos_assets/models/.../model.yaml`，因此资产文件放入 `/etc/chaos` 后仍能从已安装的 ROS 包解析型号，不依赖源码目录。自定义型号可引用另一个已安装的资产包，或引用受版本管理的绝对文件路径。

需要整机模型时，在型号或实例中添加 `description.file`，引用 URDF/Xacro。目标机需安装 `robot_state_publisher`、`urdfdom`，Xacro 模型另需 `xacro`；即使 dry run 也会展开并校验模型。URDF、include、mesh 和纹理应一并部署，不使用开发机私有路径。模型与传感器的安装 TF 只能由一个节点负责，详见 [模型导入说明](robot-description.md)。

`edge.launch.py` 要求：清单只有一个 host、一个启用资产、显式 `mode: real`，命令行 `asset_id` 与清单一致。启用 MQTT 时必须配置 transport。它不会把开发用 mock 配置隐式切成实机。

先在机载计算机只读检查：

```bash
source /opt/ros/jazzy/setup.bash
source /opt/chaos/ros/local_setup.bash
ros2 launch chaos_bringup edge.launch.py \
  inventory:=/etc/chaos/asset.yaml asset_id:=vehicle_001 dry_run:=true
```

无人机填写实际的 `drone_001` 或自定义 ID。`dry_run` 只检查结构和启动计划；不会连接底盘、飞控或 broker。真实启动时去掉 `dry_run:=true`，MQTT 的二进制、证书文件与原生配置会在任何节点启动前检查。只需本机 ROS 功能时显式指定 `enable_mqtt:=false`。

## 3. 与已有驱动的归属

运动适配器采用 attach，底盘和 DJI 网关继续由设备上的既有服务管理。传感器可以通过资产的 `sensors.*.driver.mode: node` 交给统一 launch；已由其他服务运行的传感器则使用 attach。将驱动和本框架的 ROS Domain、发现范围、话题、frame 配置为一致。

Gemini 335 使用 [专用模板](../src/chaos_assets/config/orbbec_gemini335.example.yaml) 和 [官方驱动构建说明](orbbec-driver.md)。独立驱动安装后，在 `edge.env` 设置 `CHAOS_DRIVER_SETUP=/opt/chaos/drivers/local_setup.bash`；服务入口依次加载 ROS、驱动、框架环境。真实启动会在任何节点运行前检查受管传感器的包与可执行文件。需要相应 USB/串口权限时由目标机部署流程配置。

- 无人车：连接已有底盘的 Odometry / Twist，提供状态、速度和软件受控停止。旧 MQTT 桥、导航、跟随等其他直写底盘的控制源需要按实际接管方案管理。
- 无人机：订阅旧 PSDK 网关的状态；现阶段没有统一飞控接管。旧网关内置 MQTT 保持其现有归属，与新框架使用不同 client ID / 主题；当前 PSDK 飞行控制仍属于旧网关。
- launch 管理某资产的 Rust 中间件后，同一资产不再由独立 `mqtt_ros2_adapter.service` 启动第二份中间件。

PSDK 网关需要后续增加 ROS-only 控制入口、源时间戳和独立的任务终态接口；此后才能让新框架统一接管飞行控制。Nav2 任务封装也尚未接入。本次部署模板不会自动启动这些未集成的能力。

## 4. systemd 服务模板

提供 [start-edge.sh](../deploy/start-edge.sh)、[edge.env.example](../deploy/edge.env.example) 和 [chaos-edge.service](../deploy/chaos-edge.service)。模板使用 `chaos` 服务账号；安装前创建它，或将 User/Group 改为设备已有的适用账号，并保证该账号可读取配置、证书和安装空间。attach 模式不直接打开设备；node 模式使用同一服务账号运行驱动，需要该账号具备实际设备的访问权限。

首次创建专用账号时可使用 `sudo useradd --system --user-group --home-dir /var/lib/chaos --shell /usr/sbin/nologin chaos`；已有该账号时跳过。后续 StateDirectory/LogsDirectory 由 systemd 分配权限。

以下安装命令在目标设备执行，不会由本项目开发过程自动执行：

```bash
sudo install -d -m 0755 /opt/chaos/deploy
sudo install -m 0755 deploy/start-edge.sh /opt/chaos/deploy/start-edge.sh
sudo install -d -o root -g chaos -m 0750 /etc/chaos /etc/chaos/certs
# 首次安装时选择对应设备模板；保留后续已配置的 asset.yaml 和 edge.env。
sudo install -o root -g chaos -m 0640 \
  src/chaos_assets/config/vehicle.onboard.example.yaml /etc/chaos/asset.yaml
sudo install -o root -g chaos -m 0640 deploy/edge.env.example /etc/chaos/edge.env
sudo install -m 0644 deploy/chaos-edge.service /etc/systemd/system/chaos-edge.service
```

在 `edge.env` 中设置与资产一致的 `CHAOS_ASSET_ID`，配置 broker、证书和设备参数后，再在目标机执行：

```bash
sudo systemd-analyze verify /etc/systemd/system/chaos-edge.service
sudo systemctl daemon-reload
sudo systemctl enable --now chaos-edge.service
sudo journalctl -u chaos-edge.service -f
```

服务使用普通账号，日志目录由 systemd 创建。停止时先让 launch 关闭子进程；异常或非预期退出后会重启整个本机框架，但不会恢复旧控制 token 或重放旧动作。两分钟内连续失败达到限制后会停止重试；排查并修正配置后使用 `systemctl reset-failed chaos-edge` 再启动。`network-online.target` 仅决定启动顺序，broker 连接状态由中间件负责。

安装在开发机不等于完成目标设备部署。完成模板整理和本机计划验证后，仍需要分别在目标 x86_64 / ARM64 设备上构建并验收与厂商驱动的连接。
