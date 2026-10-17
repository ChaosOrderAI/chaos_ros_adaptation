# chaos_mqtt 内部传输组件

MQTT ↔ ROS 2 消息适配层（Rust 重实现）

## 在 Chaos 框架中使用

本目录是 `chaos_mqtt` 内部维护的完整 Rust 源码，来源包含原工作树已有优化，见 [来源记录](SOURCE_PROVENANCE.md)。它与 Python 命令网关共同构成一个 ROS 包，没有独立的 `package.xml` 或启动入口。Rust crate 名称保留 `mqtt_ros2_adapter`，ROS 安装程序统一为 `chaos_mqtt/mqtt_transport`。原工作区相邻的 `mqtt_ros2_adapter/` 保留为历史来源；构建和部署不依赖该目录。后续修改本目录源码后，重新构建框架即可。

在 Ubuntu 24.04、已加载 ROS 2 Jazzy 的目标架构环境中安装 Rust/Cargo、原生 C/C++ 编译工具、libclang 和父目录 `package.xml` 声明的 ROS 依赖，然后从 `chaos_ws` 构建：

```bash
source /opt/ros/jazzy/setup.bash
colcon build --base-paths src
source install/setup.bash
ros2 pkg executables chaos_mqtt

# 使用已经生成的资产桥接配置检查；此命令不会连接 ROS 或 broker。
ros2 run chaos_mqtt mqtt_transport --check-config /path/to/generated-bridge.json
```

父包的 `ament_cmake` 调用 `cargo build --locked --release`；Rust 编译产物位于 `build/chaos_mqtt/cargo/<native-target>/release/mqtt_ros2_adapter`，安装为 `install/chaos_mqtt/lib/chaos_mqtt/mqtt_transport`（合并安装时位于统一 prefix 下）。安装产物可通过 ROS 包索引定位，统一 launch 无需依赖工作区之外的二进制路径。框架资产配置生成桥接 JSON，统一 launch 和 `chaos-edge.service` 托管此进程；下文独立部署的 `mqtt_ros2_adapter.service` 留作仅调试 Rust 传输组件时的历史模板。使用框架部署时只启用统一服务，避免同一设备重复连接。

| CMake 选项 | 默认值 | 作用 |
|---|---|---|
| `MQTT_ADAPTER_OFFLINE` | `OFF` | `ON` 为 Cargo 增加 `--offline`，需要已准备好锁定依赖缓存 |
| `MQTT_ADAPTER_BUILD_JOBS` | `2` | 控制 Rust 编译并行任务数 |
| `MQTT_ADAPTER_IDL_PACKAGES` | `std_msgs;geometry_msgs;sensor_msgs;nav_msgs;diagnostic_msgs` | 限定编入的 ROS 消息类型；r2r 自动补入其必须的基础消息包 |

例如 `colcon build --base-paths src --cmake-args -DMQTT_ADAPTER_OFFLINE=ON -DMQTT_ADAPTER_BUILD_JOBS=4`。CMake 从本机编译器探测 GCC 内置头文件路径；用户已设置的 `BINDGEN_EXTRA_CLANG_ARGS` 优先。此路径支持 x86_64 和 ARM64 原生构建，不使用原仓库 `.cargo/config.toml` 中的机器环境值。当前包装不提供交叉编译工具链。

新增自定义消息映射时，先构建并 source 对应接口包，将其名称及所有消息依赖加入 `MQTT_ADAPTER_IDL_PACKAGES`，然后重新构建本包；必须保留 `std_msgs`。运行机需要相同消息库。首次构建需要下载 Cargo/Git 依赖，源码内不包含完整 Rust 依赖缓存；安装产物无需 Rust 工具链，仍需匹配架构的 ROS 运行库。

现有 Rust 测试保留并接入 CTest，可共享 release 编译缓存运行：

```bash
colcon test --packages-select chaos_mqtt --event-handlers console_direct+
colcon test-result --verbose
```

测试使用模拟/本地回环网络，不连接实际机器人或生产 broker。`share/chaos_mqtt/transport` 安装了示例配置、README、来源清单、测试辅助脚本和独立部署模板；证书、本机配置、`.git` 和 `target` 不进入该包。`scripts/local_mqtt.py` 是开发测试工具，直接运行时会在它所在的传输目录下写入 `.local/mqtt`，请从可写源码目录使用；框架传输 smoke 会把状态和证书重定向到临时目录。

## 功能

- 订阅 MQTT 命令主题（mTLS、QoS 1、自动重连 1s→30s 退避）
- 按路由表将命令发布到 ROS 2 话题（默认 `std_msgs/String` 承载 JSON bridge envelope，与 C++ 网关字节级兼容）
- 支持类型化路由：可将命令的 `value` 字段直接转换为 `std_msgs/Bool|Int32|Int64|Float64`
- 监听 `${prefix}/command/result`，把 ROS 2 侧的执行结果回发为 MQTT response envelope
- 消息防重放（1024 窗口 + 响应缓存重发）、消息 ID 校验、时钟偏移校验
- pending 请求超时（默认 30s，可配置）——C++ 网关本地派发必然应答，本适配层依赖外部 ROS 2 节点应答，因此需要超时兜底
- 响应在 broker PUBACK 确认前保留，断线后自动补发；有界队列拥塞时等待空位
- 检查命令主题的 SUBACK，订阅被拒绝或未获 QoS 1 时退避重试
- 按配置绑定 Wi-Fi / 4G / 5G 的 Linux 网络接口，并配置连接超时和重连间隔
- 支持 MQTT ↔ ROS 2 双向话题映射：完整 ROS JSON、消息类型、MQTT QoS/retain 和 ROS 2 QoS 均可配置

## 交付语义与容量

- `mqtt.client_id` 应稳定且每个运行实例唯一；连接使用持久会话（`clean_session=false`）。命令交给业务队列后才发送 PUBACK；队列拥塞时先排空响应，再重连获取未确认命令。云端应使用 QoS 1，并用相同 `message_id` 重试未收到响应的请求。重连后新收到的命令仍受时间戳校验约束。
- MQTT 发布入口队列和待发送/确认队列各最多 256 条，同时最多提交 32 条消息。命令响应和话题转发共用这些有界队列。队列满时对生产者施加背压，网络轮询持续运行。QoS 1 允许重复交付；命令响应按 `message_id` 去重，直接话题转发的幂等处理由接收端负责。
- 防重放 ID、响应和事件共享一个最多 1024 条的缓存；超出窗口后才完成的请求会重新进入该窗口。缓存与发送队列保存在进程内存中，不跨进程重启持久化。
- 命令 JSON payload 上限为 16 KiB；传输包上限另外预留了 MQTT 主题、报文头和响应 envelope 的空间。MQTT 主题要求精确名称，不接受 `+`、`#` 通配符。
- 收到退出信号后，最多用 2 秒排入 pending 请求的失败响应，再用最多 2 秒等待 MQTT 确认；超时记录未确认响应数量后退出。强制结束或超时退出可能留下未送达响应。
- 标量路由的 `ok: true` 表示 ROS 2 发布成功；需要确认业务执行结果时，应使用 String bridge 路由。

## 单独调试传输组件

以下 Cargo 命令均在 `chaos_ws/src/chaos_mqtt/transport` 中执行，仅用于传输组件开发；框架部署使用前述统一构建与 launch。

### Cargo 构建

需要 ROS 2 Jazzy 环境（`AMENT_PREFIX_PATH` 已设置），以及 libclang：

```bash
source /opt/ros/jazzy/setup.bash
export BINDGEN_EXTRA_CLANG_ARGS="-I$(gcc -print-file-name=include)"
cargo build --locked --release
```

## 运行

```bash
cp config.example.json config.json   # 修改 broker 地址、证书路径、topic
export MQTT_PASSWORD=...             # 若配置 username
./target/release/mqtt_ros2_adapter --config config.json
```

### 本机 MQTT 测试环境

已安装 `openssl`、`mosquitto` 和 Python 3 时，可生成本机测试用的 CA、服务端证书和客户端证书：

```bash
python3 scripts/local_mqtt.py setup
python3 scripts/local_mqtt.py start
```

第一个命令创建 `config.local.json`，证书和 broker 配置保存在项目的 `.local/mqtt/` 中，已加入 Git 忽略规则。重复执行会复用已有证书；服务端和客户端证书有效期为一年。第二个命令在前台运行 broker，监听 `127.0.0.1:8883` 并要求客户端证书，按 Ctrl+C 停止。请以当前用户运行，无需 `sudo`。

另开终端运行适配器：

```bash
source /opt/ros/jazzy/setup.bash
cargo run --release -- --config config.local.json
```

如需将已有配置改为本机测试，可运行 `python3 scripts/local_mqtt.py setup --config config.json`。此命令保留话题及 ROS 配置，将 MQTT 地址、证书、认证和出口选择改为本机测试参数。测试 broker 重启后不保留会话或 retained 消息。

若启动时报 `MQTT ca_file does not exist`，检查配置中的三个证书路径。`/tmp` 中的文件可能被清理；本机测试使用上述脚本生成持久保存的测试证书，连接已有服务器时则应使用服务器信任的客户端证书及对应 CA。

## 边端部署：Ubuntu 24.04 + ROS 2 Jazzy

采用原生进程配合 systemd 托管：边端业务 ROS 2 节点通过本机 DDS 与适配器通信，适配器通过 Wi-Fi / 4G / 5G 网卡使用 mTLS 连接云端或局域网 MQTT broker。拨号、无线关联、路由、DNS 和时间同步由操作系统管理。broker 地址填写其真实域名；`127.0.0.1` 只适用于同一边端设备上的 broker。

```text
边端 ROS 2 业务节点 ← DDS → mqtt_ros2_adapter
                                  ↕ MQTT + mTLS，经系统网络或指定网卡
                         云端 / 局域网 MQTT broker ←→ 平台应用
```

Ubuntu 24.04 的 amd64 和 arm64 都在 Jazzy 的官方支持平台中，见 [ROS REP-2000](https://reps.openrobotics.org/rep-2000/)。下列步骤适用于首次部署；构建和安装命令均在边端设备或相同架构、相同系统及 ROS 环境执行。当前开发机的 x86-64 二进制不能直接运行在 ARM64 上。硬件型号确定后，需另行验证厂商驱动与 Ubuntu 24.04 的兼容性。

### 1. 准备系统与运行依赖

确认系统与架构：

```bash
cat /etc/os-release
uname -m
```

按照 [ROS 2 Jazzy 官方安装说明](https://docs.ros.org/en/jazzy/Installation/Ubuntu-Install-Debs.html)配置 UTF-8 locale 和 ROS 软件源，再安装运行依赖及原生编译工具：

```bash
sudo apt update
sudo apt install -y ros-jazzy-ros-base ros-jazzy-std-msgs \
  ros-jazzy-geometry-msgs ros-jazzy-rmw-fastrtps-cpp \
  build-essential cmake pkg-config libclang-dev git curl ca-certificates
```

在构建机通过 [Rust 官方安装方式](https://www.rust-lang.org/tools/install)安装 Rust/Cargo，并记录 `rustc --version`、`cargo --version`、ROS 包版本及源码版本。量产时固定已经验证过的 Rust 工具链和系统镜像；`--locked` 固定 Cargo 依赖，但不固定系统软件包。仅运行预编译产物的设备无需 Rust、Cargo 和 libclang，仍需全部 ROS 运行库及消息包。

让边端与云端使用同步的系统时间。执行 `timedatectl status` 检查同步服务；原有命令协议默认仅接受 5 秒以内的时钟偏差，TLS 证书也依赖正确时间。

### 2. 在目标架构构建

把完整项目源码复制到边端，进入项目目录执行：

```bash
source /opt/ros/jazzy/setup.bash
# 若使用自定义消息，先构建接口包，再加载其安装目录：
# source /opt/robot_ws/install/setup.bash

# 直接使用 Cargo 时按本机编译器选择头文件路径；colcon 构建会自动探测。
export BINDGEN_EXTRA_CLANG_ARGS="-I$(gcc -print-file-name=include)"
cargo build --locked --release --jobs 2
file target/release/mqtt_ros2_adapter
ldd target/release/mqtt_ros2_adapter
```

`ldd` 输出不能存在 `not found`。在资源充足的构建机可增加并行任务数。首次构建需要下载 Cargo 依赖；离线设备可使用同架构、同系统、同 ROS 依赖环境构建的产物，并预装匹配的运行依赖。

直接使用 Cargo 且未设置 `IDL_PACKAGE_FILTER` 时，`r2r` 默认绑定构建环境中全部可见的消息包，程序因此可能依赖未用于当前配置的消息库。框架的 colcon 构建通过上述 `MQTT_ADAPTER_IDL_PACKAGES` 明确限制范围；独立构建也建议显式设置消息过滤，或在只安装所需 ROS 包的干净环境构建。自定义消息包在构建和运行环境中必须一致，新增未编入的消息类型需要重新构建。配置 `ROS_OVERLAY_SETUP` 时将工作空间安装到 `/opt/robot_ws` 等服务可读路径；服务默认无法访问 `/home`。

### 3. 准备设备配置、证书和 MQTT 权限

复制一份示例作为待部署配置，避免复用本机测试的证书和回环地址：

```bash
cp config.example.json config.edge.json
# 要固定 5G 出口，可改用：cp config.5g.example.json config.edge.json
ip -br address
ip route
```

在 `config.edge.json` 中设置以下参数：

| 项目 | 部署值 |
|---|---|
| `mqtt.host` / `port` | broker 真实域名及 TLS 端口，通常为 8883；域名必须与服务端证书匹配 |
| `mqtt.client_id` | 每台设备唯一且稳定，例如 `edge-001`；多个实例不能共用 |
| 三个证书路径 | `/etc/mqtt_ros2_adapter/certs/ca.crt`、`edge-client.crt`、`edge-client.key` |
| `mqtt.username` | 按 broker 认证要求设置；仅使用证书身份时可设为空字符串 |
| 命令、响应、事件话题 | 使用该设备独立的话题命名空间，并同步更新所有映射中的 MQTT 话题 |
| `network` | 默认 `access: "system"`、`interface: null`；固定出口则选择 `wifi` / `4g` / `5g` 及真实网卡名 |
| `topic_mappings` | 对应实际 ROS 话题、`包名/msg/类型名`、JSON 字段和两侧 QoS |

部署用的 `ca.crt` 应信任 broker 的服务端证书，`edge-client.crt` / `edge-client.key` 是 broker 信任的该设备身份。由你的 broker/证书管理方提供这些文件；仓库本机测试脚本生成的 CA 仅适用于配套测试 broker。客户端私钥安装到设备，CA 签发私钥留在签发端。

broker 必须允许该设备订阅命令主题及所有 `mqtt_to_ros2` 主题，发布响应、事件及所有 `ros2_to_mqtt` 主题。即使只使用映射，程序也会订阅配置中的命令主题；该订阅必须获准 QoS 1。broker 应按业务要求配置持久会话保留、离线队列容量和持久化。

Wi-Fi / 蜂窝网络先通过系统网络管理工具完成连接。`network.access` 不会执行 AT 指令、设置 APN 或切换无线网络。自动主备切换应由系统管理路由，适配器使用 `system`；绑定某个网卡时不会自动切到另一网卡。DNS 使用系统解析器。

### 4. 安装文件和服务账号

以下命令假设当前目录是项目根目录；`config.edge.json` 已填好，设备证书位于 `certs/`。首次安装执行一次，升级时使用后面的升级流程。

```bash
getent group mqtt-ros2 >/dev/null || sudo groupadd --system mqtt-ros2
id -u mqtt-ros2 >/dev/null 2>&1 || sudo useradd --system --gid mqtt-ros2 \
  --home-dir /var/lib/mqtt_ros2_adapter --no-create-home \
  --shell /usr/sbin/nologin mqtt-ros2

adapter_release="$(date +%Y%m%d-%H%M%S)"
sudo install -d -m 0755 "/opt/mqtt_ros2_adapter/releases/$adapter_release"
sudo install -m 0755 target/release/mqtt_ros2_adapter deploy/start.sh \
  "/opt/mqtt_ros2_adapter/releases/$adapter_release/"
sudo ln -sfn "releases/$adapter_release" /opt/mqtt_ros2_adapter/current

sudo install -d -o root -g mqtt-ros2 -m 0750 \
  /etc/mqtt_ros2_adapter /etc/mqtt_ros2_adapter/certs
sudo install -o root -g mqtt-ros2 -m 0640 config.edge.json \
  /etc/mqtt_ros2_adapter/config.json
sudo install -o root -g mqtt-ros2 -m 0640 \
  certs/ca.crt certs/edge-client.crt certs/edge-client.key \
  /etc/mqtt_ros2_adapter/certs/
sudo install -o root -g mqtt-ros2 -m 0640 deploy/adapter.env.example \
  /etc/mqtt_ros2_adapter/adapter.env
sudo install -m 0644 deploy/mqtt_ros2_adapter.service \
  /etc/systemd/system/mqtt_ros2_adapter.service
```

安装后的目录：

```text
/opt/mqtt_ros2_adapter/releases/<版本>/   二进制和启动脚本
/opt/mqtt_ros2_adapter/current           指向当前版本
/etc/mqtt_ros2_adapter/config.json       网络、broker、话题映射
/etc/mqtt_ros2_adapter/adapter.env       ROS 环境与可选 MQTT 密码
/etc/mqtt_ros2_adapter/certs/            设备证书
/var/lib/mqtt_ros2_adapter/              systemd 创建的运行目录
/var/log/mqtt_ros2_adapter/              ROS 日志目录
```

程序的主要日志写入 journald。按设备磁盘容量配置 journald 日志保留和 ROS 日志清理；这两个目录不会为 MQTT 队列提供磁盘持久化。

### 5. 配置 ROS 环境并启用服务

```bash
sudoedit /etc/mqtt_ros2_adapter/adapter.env
```

`ROS_DOMAIN_ID` 与业务 ROS 2 节点保持一致；`RMW_IMPLEMENTATION` 默认使用已安装的 `rmw_fastrtps_cpp`。自定义消息工作空间填入 `ROS_OVERLAY_SETUP`。需要 MQTT 密码时设置 `MQTT_PASSWORD`，其变量名需与 JSON 中的 `password_environment` 一致。环境文件使用 `KEY=value`，不写 `export`，不使用 `~` 或 `$变量` 展开。

```bash
sudo systemd-analyze verify /etc/systemd/system/mqtt_ros2_adapter.service
sudo systemctl daemon-reload
sudo systemctl enable --now mqtt_ros2_adapter.service
sudo systemctl status mqtt_ros2_adapter.service --no-pager
sudo journalctl -u mqtt_ros2_adapter.service -n 100 --no-pager
```

正常就绪日志为 `MQTT connected; all subscriptions ready`。`systemctl is-active` 仅代表进程存活，不能证明 broker 或 ROS 业务链路可用。程序断线会自行重连；进程异常退出或启动时网卡尚未出现时，systemd 每 5 秒尝试重启。启动顺序中的 `network-online.target` 不持续监测互联网可达性，见 [systemd 网络就绪说明](https://systemd.io/NETWORK_ONLINE/)。

如果固定网卡模式报 `Operation not permitted`，且确认来自网卡绑定，可安装最小能力的可选配置：

```bash
sudo install -d -m 0755 /etc/systemd/system/mqtt_ros2_adapter.service.d
sudo install -m 0644 deploy/bind-interface.conf \
  /etc/systemd/system/mqtt_ros2_adapter.service.d/bind-interface.conf
sudo systemctl daemon-reload
sudo systemctl restart mqtt_ros2_adapter.service
```

该配置仅增加 `CAP_NET_RAW`，服务仍以 `mqtt-ros2` 账号运行。网卡不存在、DNS 或路由错误需要修正对应配置，增加能力不会解决这些问题。

### 6. 验收通信与故障恢复

使用独立的诊断话题验证双向转发，可临时向 `topic_mappings` 添加：

```json
{
  "name": "deployment_check_in",
  "direction": "mqtt_to_ros2",
  "mqtt_topic": "devices/edge-001/diag/in",
  "ros_topic": "/deployment_check/from_mqtt",
  "msg_type": "std_msgs/msg/String",
  "mqtt": { "qos": 1, "retain": false },
  "ros": { "depth": 10, "reliability": "reliable", "durability": "volatile" }
},
{
  "name": "deployment_check_out",
  "direction": "ros2_to_mqtt",
  "mqtt_topic": "devices/edge-001/diag/out",
  "ros_topic": "/deployment_check/to_mqtt",
  "msg_type": "std_msgs/msg/String",
  "mqtt": { "qos": 1, "retain": false },
  "ros": { "depth": 10, "reliability": "reliable", "durability": "volatile" }
}
```

上面是两个数组元素，加入现有数组时注意逗号。为诊断 MQTT 话题配置 broker 权限，重启服务后执行：

```bash
source /opt/ros/jazzy/setup.bash
export ROS_DOMAIN_ID=0   # 与 adapter.env 中的实际值一致
export RMW_IMPLEMENTATION=rmw_fastrtps_cpp
ros2 topic echo /deployment_check/from_mqtt std_msgs/msg/String
```

在已认证的云端 MQTT 客户端向 `devices/edge-001/diag/in` 发布 `{"data":"mqtt-to-ros-ok"}`，确认 ROS 收到相同内容。再让云端先订阅 `devices/edge-001/diag/out`，在边端另一个加载相同 ROS 环境的终端执行：

```bash
ros2 topic pub --once /deployment_check/to_mqtt std_msgs/msg/String \
  '{data: "ros-to-mqtt-ok"}'
```

云端应收到 `{"data":"ros-to-mqtt-ok"}`。随后验证业务话题类型和 QoS、重启设备后的自动启动，以及在有本地控制手段时断开/恢复网络后的自动重连。测试完成后移除诊断映射并重启。当前进程内缓存不跨重启保存，QoS 1 仍可能重复；要求断电不丢数据时还需增加磁盘队列和恢复机制。

### 7. 升级、回滚与排查

新版本先按第 2 步构建，安装到一个新的 `releases/<版本>` 目录；保留旧目录。切换前保存当前 `config.json`、`adapter.env` 及版本信息到受控备份位置。完成业务侧暂停后执行：

```bash
sudo systemctl stop mqtt_ros2_adapter.service
sudo ln -sfn releases/新版本目录名 /opt/mqtt_ros2_adapter/current
sudo systemctl start mqtt_ros2_adapter.service
sudo journalctl -u mqtt_ros2_adapter.service -n 100 --no-pager
```

若验收失败，停止服务、将 `current` 指回旧版本、恢复与旧版本兼容的配置和接口包，再启动并重复验收。升级保留设备身份和证书；配置和证书变更均需重启生效。停止服务会触发有时限的发送队列清理，升级应选择业务允许的维护窗口。

| 现象 | 检查方法 |
|---|---|
| `ca_file does not exist` / 文件不可读 | 检查 `/etc` 中三个路径；用 `sudo -u mqtt-ros2 test -r 文件路径` 验证权限 |
| TLS 校验失败 | 核对域名、CA、客户端证书/私钥、证书有效期及系统时间 |
| 反复重连 / 订阅被拒绝 | 检查 DNS、网卡、路由、端口、重复 client ID、broker ACL 和订阅 QoS |
| 找不到 ROS 动态库 | 加载 ROS/自定义工作空间后运行 `ldd`，补装与构建环境一致的消息包 |
| 不支持配置的 ROS 类型 | 确认类型已包含在构建时环境；仅安装新的消息包不够，需要重编译 |
| MQTT 正常但 ROS 无消息 | 比对 `ROS_DOMAIN_ID`、发现范围、实际话题类型、QoS 和业务节点状态 |
| 服务启动失败 | `journalctl -u mqtt_ros2_adapter.service -b`；检查服务账号及环境文件路径 |

## 延迟

本机 broker 场景下（release 构建、TLS、QoS 1），实测 MQTT 命令 → ROS 2 → 结果 → MQTT 响应的往返时间 p50 ≈ 1.4 ms、p99 ≈ 2.2 ms。前提：

- 适配器已对 MQTT 连接开启 `TCP_NODELAY`；**broker 也必须开启**，否则 broker 下发的每条消息都可能被 Nagle + 延迟 ACK 拖慢约 40 ms。mosquitto 需在配置里加 `set_tcp_nodelay true`（默认 false）。
- 使用 `--release` 构建运行（debug 构建每次往返约多 0.4 ms）。
- 云端 MQTT 客户端同样应开启 `TCP_NODELAY`。

测量工具（适配器需已用同一配置运行）：

```bash
cargo run --release --example latency_probe -- --config config.json -n 300
```

探针同时扮演云端和一个立即应答的 ROS 2 节点，分别输出 MQTT→ROS 2、ROS 2→MQTT 和往返时间的 min/p50/p90/p99/max。

## 配置

### 网络接入

4G/5G 模块已完成拨号、Wi-Fi 已关联并取得地址时，程序通过对应 Linux 网卡连接 MQTT。选择一份示例，修改 broker、证书及实际网卡名后运行：

| 配置文件 | 接入方式 | 示例网卡 |
|---|---|---|
| `config.example.json` | 系统路由表选择出口 | 不绑定网卡 |
| `config.wifi.example.json` | Wi-Fi | `wlan0` |
| `config.4g.example.json` | 4G | `wwan0` |
| `config.5g.example.json` | 5G | `wwan0` |

```bash
./target/release/mqtt_ros2_adapter --config config.5g.example.json
```

```json
"network": {
  "access": "5g",
  "interface": "wwan0",
  "connection_timeout_seconds": 30,
  "tcp_nodelay": true,
  "reconnect_initial_ms": 2000,
  "reconnect_max_ms": 60000
}
```

`access` 可为 `system`、`wifi`、`4g`、`5g`。后三种必须填写网卡名，程序使用 socket 设备绑定让 MQTT 数据经该网卡传输；网卡不存在或绑定失败会报错。`system` 使用系统路由，`interface` 省略或设为 `null`。单实例一次选择一个出口，修改后重启生效。

连接超时范围为 1–300 秒；重连初始间隔至少 100 毫秒，上限不低于初始间隔且不超过 300000 毫秒。MQTT 心跳仍由 `mqtt.keep_alive_seconds` 配置。网络的拨号、SSID、DHCP、路由和 DNS 由系统管理；DNS 查询使用系统解析器。

旧配置省略 `network` 时，默认使用系统路由、10 秒连接超时、TCP_NODELAY 开启、1–30 秒退避。

### 双向话题映射

在 `topic_mappings` 中配置每一条对应关系。下面是 MQTT → ROS 2 的示例：

```json
{
  "name": "temperature_input",
  "direction": "mqtt_to_ros2",
  "mqtt_topic": "device/sensors/temperature",
  "ros_topic": "/sensors/temperature",
  "msg_type": "std_msgs/msg/Float64",
  "mqtt": { "qos": 1, "retain": false },
  "ros": {
    "depth": 10,
    "reliability": "reliable",
    "durability": "volatile",
    "deadline_ms": null,
    "lifespan_ms": 1000
  },
  "max_payload_bytes": 4096
}
```

| 字段 | 含义 / 可选值 |
|---|---|
| `name` | 唯一映射名称，用于日志 |
| `direction` | `mqtt_to_ros2`：订阅 MQTT 并发布 ROS；`ros2_to_mqtt`：订阅 ROS 并发布 MQTT |
| `mqtt_topic` | 精确 MQTT 话题名；不得重复或与命令/响应/事件话题重叠 |
| `ros_topic` | 以 `/` 开头的完整 ROS 2 话题名 |
| `msg_type` | `包名/msg/类型名`；类型必须存在于构建时加载的 ROS 2 环境 |
| `delivery` | 出站队列策略：`queued`（默认）、`latest`、`latest_nonterminal`；后两者只允许 `ros2_to_mqtt` |
| `mqtt.qos` | 0 或 1，默认 1；入站为订阅 QoS，出站为发布 QoS |
| `mqtt.retain` | 默认 false；只有 `ros2_to_mqtt` 可设置 true |
| `ros.depth` | DDS KeepLast 深度，1–1024，默认 32 |
| `ros.reliability` | `reliable`（默认）或 `best_effort` |
| `ros.durability` | `volatile`（默认）或 `transient_local` |
| `ros.deadline_ms` / `ros.lifespan_ms` | 可选 DDS 参数，1–86400000 毫秒；null/省略使用中间件默认值 |
| `max_payload_bytes` | JSON 消息上限，1–1048576 字节，默认 16384 |

MQTT payload 是与 ROS 消息字段一致的 JSON 对象。例如 `std_msgs/msg/Float64` 为 `{"data": 24.5}`，`std_msgs/msg/String` 为 `{"data": "hello"}`，`geometry_msgs/msg/Twist` 为：

```json
{
  "linear": { "x": 0.1, "y": 0.0, "z": 0.0 },
  "angular": { "x": 0.0, "y": 0.0, "z": 0.2 }
}
```

反向映射把 `direction` 设置为 `ros2_to_mqtt`，ROS 消息会按同样的 JSON 字段发布到 MQTT。示例配置已包含速度设定值入站与温度遥测出站两条映射。

最多配置 128 条映射。同一 ROS 话题不能配置相反方向，避免转发回环；共享 ROS 话题的数据类型必须一致。所有订阅确认成功后连接才标记为就绪，任一主题被拒绝或 QoS 不匹配都会退避重试。

QoS 0 在网络断开或队列拥塞时可能丢失；QoS 1 的出站消息保留到 broker 确认，断线自动补发。ROS 2/DDS 与 MQTT 之间没有跨协议事务保证，实际 ROS 执行结果应使用下方命令路由协议。直接话题映射不增加 `message_id`、防重放或业务响应 envelope。

`delivery=latest` 适用于周期状态：每个 MQTT topic 只保留最新的未提交样本，最多 128 个 topic，与响应队列分开。每个 topic 最多一个样本等待 MQTT 确认，所有此类样本最多占用一半发送窗口，避免弱网下旧状态占满结果通道。这种策略允许丢弃旧样本，即使配置 QoS 1，也不会保证每个历史样本被发送；重连时丢弃旧的在途遥测，后续发送当前最新样本。

`delivery=latest_nonterminal` 仅适用于 `std_msgs/msg/String` 的 JSON 事件：当 `data` 内 JSON 明确包含 `"terminal": false` 时按最新样本合并；`terminal=true`、缺失字段或无法识别的事件保留默认队列语义。统一框架用它合并任务反馈，终态仍通过可靠队列发送，并同时回写命令响应。终态提交时会移除同 topic 尚未发送的反馈。队列和重放缓存都在内存中；进程退出或缓存淘汰后，不保证恢复。

### 原有命令路由

见 `config.example.json`。`routes` 数组可覆盖内置路由的 `msg_type`/`topic_suffix` 或新增自定义命令：

```json
{ "command": "flight.velocity", "topic_suffix": "flightControl/velocity", "msg_type": "std_msgs/Float64" }
```

MQTT 命令格式（与 C++ 网关一致）：

```json
{
  "version": 1,
  "message_id": "req-12345678",
  "timestamp_ms": 1720000000000,
  "command": { "command": "flight.takeoff" }
}
```

- `msg_type = std_msgs/String`：整包作为 bridge envelope 发布到 `m3td/flightControl/takeoff`；ROS 2 节点处理后向 `m3td/command/result` 发布结果，`source` 为 `mqtt` 时回发 MQTT 响应
- 类型化路由（如 `std_msgs/Float64`）：取 `command.value` 发布标量消息，发布成功立即应答 `{"ok": true}`

统一框架的动作响应采用两阶段：首先返回 `phase=accepted, terminal=false`，完成后在同一 `responses` topic 返回 `phase=result, terminal=true`，保持相同 `message_id` 和 `goal_id`；原 `task_events` 完成事件继续保留。客户端应以终态更新任务，重发原命令 ID 可获取缓存的最新响应，不会重新派发仍在执行的动作。活动关联最多 1024 条，完成响应保留最近 1024 个 ID，均不跨进程重启；超出窗口后不能承诺永久幂等，客户端应使用唯一 ID，禁止更新旧 ID 的时间戳后作为新动作提交。

顶层 `response_timeout_ms`（默认 30000）只限制首次响应。明确收到 `terminal=false` 后改用 `action_timeout_ms`（默认 360000，范围 1000–86400000）等待终态，重复确认不会续期。超时返回 `RESULT_TIMEOUT` 与 `execution_state=unknown`，不能据此认定机器人已停止。统一框架生成器要求至少 360000 ms，比网关默认 300 秒观察时限多 60 秒；独立启动并增大网关 `action_timeout_s` 时，应同步增加桥接层时限。未提供 `terminal` 的旧网关响应保持原先一次响应行为。

## 内置路由

与 `orin_gateway/src/ros2_adapter.cpp` 的 `routes()` 完全一致：`status`、`heartbeat`、`control.enable/disable`、`flight.*`、`gimbal.*`、`mission.*`、`video.*`、`algorithm.enable`，共 23 条。

顶层 `route_profile` 可选择 `legacy_drone`（省略时的默认值，保留上述内置路由）或 `custom`（只启用配置中的 `routes`，没有隐式无人机路由）；未知值会被拒绝。统一机器人框架使用 `custom`，把受支持的命令发送到资产自己的 `mqtt/command`，由 `chaos_mqtt` 调用统一 ROS 服务和 Action。旧配置无需修改。`topic_mappings` 的类型冲突检查同样使用选定的路由配置。

启动前可执行 `mqtt_ros2_adapter --check-config /path/to/config.json`。它加载完整配置并校验路由、TLS 文件路径和映射规则后退出，不初始化 ROS 节点，也不连接 MQTT；返回码 0 表示通过。统一 launch 用它在启动任何进程前检查生成的配置。该检查不连接 broker，因此不验证在线身份认证或网络连通性。

## 测试

```bash
cargo test --locked --all-targets
```

传输测试在本机随机 TCP 端口启动模拟 MQTT broker，无需外部 broker 或证书，覆盖断线补发、多主题订阅、订阅拒绝、可配置消息大小、混合 QoS/retain、双向拥塞与退出期限，并验证 ROS 原生 JSON 类型转换。生产连接仍强制使用 mTLS。

## 与 C++ 网关的差异

| 项 | C++ 网关 | 本适配层 |
|---|---|---|
| 命令执行 | 本地 `CommandDispatcher`（PSDK） | 外部 ROS 2 节点 |
| 结果回传 | 本地派发后直接应答 | 监听 `command/result` 话题 |
| pending 超时 | 无（本地必应答） | 默认 30s 可配 |
| aircraft status 遥测 | 有（PSDK 数据源） | 无 |
| 类型化消息 | 仅 `std_msgs/String` | 额外支持 Bool/Int32/Int64/Float64 |
