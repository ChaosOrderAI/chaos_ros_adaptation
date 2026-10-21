# 首版验证记录

## Go2 机器狗接入验证（2026-10-21）

本轮新增 `unitree_go2` 型号、独立模拟和机载配置、ROS 适配器、独立 SDK 驱动及统一启动管理。在本机 ROS 2 Jazzy / Python 3.12 环境完成以下检查：

- 重建 `chaos_interfaces` 及相关七个 ROS 包成功；`chaos_runtime`、`chaos_bringup` 最终增量构建成功。现有安装空间原先缺少源码已有的 `ControlTrace`，重建后全量测试可运行。本轮未修改或重建 Rust MQTT 实现。
- `python3 -m pytest -q src -rs`：**704 通过，4 跳过**。跳过项为缺少可选 Xacro 依赖的原有模型测试。
- Go2 mock 与机载模板的 `system.launch.py` / `edge.launch.py` dry run 均成功；后者无需 SDK 或设备即可确认真实后端只开放状态、速度和停止。
- `scripts/go2_smoke_test.py`：本机 Domain 186 的机器狗 mock 验证描述、控制租约、前后/横移/转向、超时和显式停止、平面导航、垂直指令拒绝与租约释放。
- `scripts/go2_driver_smoke_test.py`：本机 Domain 185 的实际 Go2Driver、Go2Adapter、runtime 和 SDK API 通过 ROS 互通；厂商 SDK 强制替换为内存实现，并阻止真实 SDK 导入。验证世界系速度转机体系、实际能力拒绝、运动指令映射、暂停 runtime 后驱动独立超时归零、遥测过期拒绝运动及显式停止。
- 原有 `scripts/smoke_test.py` 的无人车/无人机 mock 回归通过。

回归覆盖重复 DDS 状态不延长在线时间、冻结/跳变 ROS 时钟、旧速度消息重放、停止后迟到命令、SDK 输出失败和停止重试，以及跨资产 DDS 连接、速度与里程计话题冲突。模拟与替身测试使用本机 socket，沙箱禁止 socket 后在获准的本机环境执行；所有测试均回收进程和 ROS 资源。

未连接真实 Go2、远端 broker 或旧平台；未启动或修改 `sbdog`，未安装 systemd 服务。厂商 SDK 在目标机的依赖兼容性、实际动作和停机效果仍需机载验收，详见 [Go2 接入说明](unitree-go2.md)。

## 首版记录

日期：2026-10-16（Asia/Shanghai）。环境：当前开发机 ROS 2 Jazzy / Python 3.12。

| 检查 | 结果 |
|---|---|
| `colcon build --base-paths src --symlink-install` | 6 个包构建成功 |
| 加载安装后的默认清单，`system.launch.py dry_run:=true` | 两个资产正确解析；没有启动子节点 |
| `python3 -m pytest -q src` | 91 通过，0 失败 |
| Rust `cargo test --locked --offline --all-targets` | 67 通过，0 失败 |
| `scripts/smoke_test.py` | 实际统一 launch、两个 mock 资产、ROS 类型化接口和 Python SDK 通过 |
| `scripts/mqtt_smoke_test.py` | ROS 侧 MQTT 业务协议转换通过 |
| `scripts/transport_smoke_test.py` | 本机 mTLS MQTT 与完整统一启动链路通过 |
| 非法 MQTT port 的 launch 预检 | 非零退出，任何机器人子进程启动前拒绝配置 |

Python 测试覆盖配置字段和覆盖规则、部署筛选、跨主机 MQTT 身份冲突、重复速度输出、控制租约、速度有效期与范围、无效数据、适配器状态、Action 取消与受理失败、SDK 数值转换和 MQTT 命令协议。

ROS 集成测试验证：多资产状态隔离、能力发现、租约互斥、速度看门狗、过期指令拒绝、导航完成、第二任务拒绝、取消终态、mock 起飞与降落、租约过期中止任务，以及 SDK 的描述、获取/续期/释放控制权和停止。

完整传输测试使用随机回环端口、临时证书、本机 Mosquitto 和 ROS Domain 189，验证真实生成配置的 `--check-config`、统一 launch 的 `enable_mqtt:=true`、状态、请求回执、起降任务受理和匹配 goal ID 的终态。测试未接入外部 broker、真实无人车或无人机。

沙箱默认禁止本机 DDS/测试 socket，相关集成测试经权限审核后在 localhost 重跑通过。测试生成的 broker、launch 和机器人进程均由脚本清理。

这些结果证明首版软件契约和模拟链路可运行。真实车辆的 Jazzy 驱动兼容性、导航、停机行为，以及无人机 PSDK 控制、定位转换和飞行任务均未做硬件验收。旧 DJI 后端当前只读状态；真实后端的详细范围见 [架构说明](architecture.md)。

## 机载部署补充验证

同日根据 Ubuntu 24.04 + ROS 2 Jazzy、x86_64 / ARM64 的部署目标增加单设备入口、安装资源引用和服务模板。本轮验证在当前开发机执行，未进行 ARM64 构建或实体设备部署。

| 检查 | 结果 |
|---|---|
| 资产解析与启动计划相关测试 | 35 通过，覆盖单设备约束、身份匹配、真实后端选择和 `package://` 资源解析 |
| 更新后的 `chaos_assets`、`chaos_bringup` 开发安装 | 2 个包构建成功 |
| 干净环境中的普通安装 | 6 个包构建成功；独立 build/log/install，使用 `--merge-install`，未使用源码软链接 |
| 普通安装后的无人车、无人机 `edge.launch.py dry_run:=true` | 配置复制到源码与安装目录之外后仍可解析，各选择一个真实资产；未启动节点 |
| 安装隔离检查 | 6 个 Python 包及 ament share 来自独立安装目录，`sys.path` 不含开发工作空间 |
| `start-edge.sh` 语法及 `--check` | 语法通过；两类设备计划检查通过；错误资产 ID 均在启动前被拒绝 |
| systemd 模板静态校验 | 临时副本仅将 `ExecStart` 替换为当前可执行脚本路径后，`systemd-analyze verify` 通过 |

服务模板未安装或启动；未连接实际底盘、飞控或远端 broker。`dry_run` 验证配置结构与启动计划，真实启动所需的二进制、证书与厂商驱动仍需在目标机准备并验收。部署流程见 [机载部署说明](onboard-deployment.md)。

## 审计问题修复与可靠性回归

日期：2026-10-16（Asia/Shanghai）。修复停止失败不重试、执行期限丢失、畸形输入异常、任务终态重放、Action 受理失败资源回收、资产多次读取和发现环境冲突，并优化 MQTT 状态/反馈队列。

| 检查 | 结果 |
|---|---|
| 全量 ROS 工作空间构建 | 6 个包成功，新服务/动作字段已生成 |
| 全量 Python `python3 -m pytest -q src` | **159 通过** |
| Rust `cargo test --locked --offline --all-targets` | **75 通过** |
| `scripts/smoke_test.py` | 统一 mock 启动、控制、导航/取消、起降及 SDK 通过 |
| `scripts/mqtt_smoke_test.py` | MQTT 门面、异常四元数、任务终态及超时报告通过 |
| `scripts/transport_smoke_test.py` | 实际 mTLS MQTT 链路、终态响应缓存及重复请求恢复通过 |
| 临时 broker 与订阅客户端断开 | mock 导航在断网期间完成；恢复连接后，重发已过原 TTL 的同一请求可取回终态，未重复执行动作 |
| assets/bringup 普通安装 | 两包在独立 `/tmp` 前缀构建成功；实际导入安装副本；两种机载配置在源码外解析并 dry run 成功 |

Python 回归覆盖停止失败退避重试、释放后的 token 撤销、动作最终停止失败返回 `STOP_FAILED`、锁等待耗尽 TTL、执行前过期、墙上时钟回拨、ActionServer 受理响应失败与正常资源回收、深层 JSON、极大数值/时间戳、不可变启动快照及直接 launch 的发现环境。ActionServer 故障路径采用本机 Jazzy 的真实处理方法配合传输替身验证，没有修改系统 rclpy。

Rust 本机 socket 回归包括断线时 10000 个状态样本合并、遥测未确认时控制结果仍可发送、入口满载时暂停新遥测并恢复未确认指令，以及任务受理与终态分别计时。所有测试 broker、客户端及 ROS 子进程由脚本清理；未连接外部服务或硬件。

冒烟脚本已改为明确等待 SDK 的异步状态，并在 broker 中断测试中显式重建订阅客户端，避免把客户端退出或状态尚未到达误判为框架失败。

这些检查不替代目标 ARM64 构建与实机验收。永久阻塞的厂商调用仍需要独立驱动进程和设备 watchdog；结果缓存有界且不跨进程重启。接口升级必须统一重建全部 `chaos_interfaces` 使用者，并更新 Rust 中间件，详见 [部署升级说明](onboard-deployment.md)。

## 传感器层与 Gemini 335 集成

日期：2026-10-16（Asia/Shanghai）。新增 `chaos_sensors`、传感器资产声明、统一启动与进程监督，以及 Gemini 335 官方驱动的独立源码工作空间。

| 检查 | 结果 |
|---|---|
| 默认开发工作空间构建 | **7 个包成功**，厂商源码不参与默认构建 |
| 全量 Python `python3 -m pytest -q src` | **319 通过** |
| `scripts/sensor_smoke_test.py` | localhost / Domain 190：Gemini 335 模板强制 mock，RGB/深度 Image、CameraInfo、内置 IMU、模拟雷达和独立 IMU 通过 |
| 传感器发现和健康检查 | 晚加入订阅者取得 transient-local 目录；不泄露驱动参数；monitor:false 流没有额外诊断订阅；监测流健康正常 |
| 静态 TF 与故障退出 | 显式模拟外参发布正确；只终止本测试自己的一个 mock 传感器后，整个统一 launch 以失败状态退出 |
| 原有 `scripts/smoke_test.py` | 两机器人状态隔离、控制租约、导航/取消、起降和 SDK 回归通过 |
| 普通安装及隔离检查 | 7 个包在 `/tmp/chaos-sensors-install-zCxRKe` 独立构建、普通安装；干净环境中模块全部来自新前缀，未引用开发源码 |
| 安装后 Gemini 335 计划 | 资产模板复制到安装前缀外后仍能解析 package URI，edge dry run 显示真实相机 node 与 5 条标准数据流；没有启动硬件 |
| 服务入口与驱动构建脚本 | bash 语法通过；可选驱动环境的加载顺序、缺失文件拒绝和兼容旧环境均有回归测试 |
| 官方驱动完整性 | v2.10.6 / `296156521b60bdf813cb94c46cbb55562bbdc453`，原厂 git 工作树未修改，x64/ARM64 SDK 与许可证保留 |
| 原厂驱动源码构建 | `orbbec_camera_msgs` 成功；`orbbec_camera` 因本机缺少 backward_ros 等依赖未完成，详见 [依赖清单](orbbec-driver.md) |

新增测试覆盖模型与实例合并、驱动模式替换、mock 强制隔离、类型化 ROS 参数、特殊 remap 拒绝、参数文件结构/选择器/快照、话题及 frame 冲突、保留 base/odom/map、显式 TF 环、时间和数量边界、接收超时与源时间检查。独立审查发现的参数文件预检不足和定位坐标系冲突已修复。

首次真实 DDS 测试暴露 `DiagnosticStatus.level` 在 Jazzy 中是单字节字段；已将内部整数级别转换为 bytes，并增加完整 DiagnosticArray 的 CDR 序列化回归，修复后集成测试通过。直接 dry run 的首次检查受默认日志目录只读限制，改用可写的临时 ROS_LOG_DIR 后通过。

本轮没有修改 Rust 中间件或重新执行其测试，前一节 75 项结果属于前轮记录。没有安装系统依赖、修改设备权限、连接 Gemini 335/雷达或开展 ARM64 编译。模拟 Image 包括深度通道均为合成格式，不代表真实深度编码或测量精度；真实采集、USB 重连、标定与性能仍需实机验收。

## URDF / Xacro 导入与模型发布

日期：2026-10-16（Asia/Shanghai）。新增可选 description 配置、模型校验与 XML 快照、每资产 robot_state_publisher，以及合成 URDF/Xacro 示例。用户目前没有整机 URDF，示例几何与安装点不作为真实设备模型。

| 检查 | 结果 |
|---|---|
| 开发工作空间构建 | **7 个包成功** |
| 全量 Python `python3 -m pytest -q src` | **414 通过**，包含真实 Xacro 展开测试 |
| `scripts/description_smoke_test.py` | localhost / Domain 191：两个模型的独立描述、静态 TF、URDF 中的 IMU 安装点、晚加入订阅、不同关节位置对应的动态 TF 全部通过 |
| Xacro 示例 dry run | typed arguments 转换、相对 macro include、展开模型原生校验通过，不启动 ROS 节点 |
| 普通安装 | 7 个包在 `/tmp/chaos-urdf-install-0remFl` 独立构建并普通安装，未使用源码软链接 |
| 安装隔离与资源 | 干净环境的 7 包模块来自新安装前缀；清单复制到前缀外后，模型 package URI、相对 URDF、Xacro include 和参数仍正确解析 |
| 安装后启动计划 | 双模型 URDF dry run、模型依赖可执行文件预检通过；没有运行硬件 |

当前系统安装了 RSP 和 urdfdom，但没有 Xacro。本轮仅将官方 Xacro 2.1.1 放在 `/tmp/chaos-urdf-reference-xacro`，通过测试进程的 PYTHONPATH 使用，未安装系统软件。全量 414 项结果在此环境中取得；未提供 Xacro 时相关真实展开用例跳过，并有明确缺依赖错误的回归。生产使用 Xacro 需安装相应目标机依赖。

新增回归覆盖：型号/实例覆盖与换源文件、原生 URDF 语义、根 frame 与前缀、定位坐标系保护、跨资产 frame 归属、URDF 和传感器变换重复/成环、关节及 mimic 限制、mesh/texture 路径、源文件替换后的不可变快照、启动前依赖检查，以及避免向业务节点复制完整 XML。

独立审查发现并修复了 Xacro optional include 的异常兼容、FIFO 模型入口阻塞，以及误标为 URDF 的残留 Xacro 被忽略后只发布部分模型的问题。校验器对子进程 stdout/stderr 总量和执行时间设限，并拒绝 XML 实体与非普通源文件；这些限制不构成不可信 Xacro 代码沙箱。

测试只验证软件导入与模型发布链路，没有启动 RViz 图形界面、加载实际整机模型、验证真实安装外参或进行 ARM64 构建。正常 launch 不产生模拟 JointState，动态关节仍需真实驱动或明确的仿真来源。

## MQTT 中间件内置与统一部署（历史并列包布局）

日期：2026-10-17（Asia/Shanghai）。将现有 Rust 工作树完整纳入 `src/mqtt_ros2_adapter`，通过 ament_cmake 接入 colcon；资产通信配置默认使用安装包索引，保留显式可执行文件覆盖。

本节记录首次内置时的过渡布局，之后已将传输与网关合并为一个 `chaos_mqtt` 包；当前结构及验证以后一节为准。

| 检查 | 结果 |
|---|---|
| 全量普通安装 | **8 个包成功**，独立 build/log/install 位于 `/tmp/chaos-mqtt-integrated-dMHAYL`，使用 merge install，无源码软链接 |
| 全量 Python 回归 | **424 通过**；沿用上节临时 Xacro 2.1.1 测试环境 |
| 内置 Rust CTest / Cargo release 回归 | **75 通过**，包含所有 targets；colcon test-result 无错误或失败 |
| 干净环境安装检查 | `/tmp` 中仅加载系统 Jazzy 和新安装前缀，Python 模块、资产和辅助脚本均来自安装空间，运行 PATH 不含 Cargo 或旧二进制 |
| ROS 包入口和动态库 | `ros2 pkg executables` 正确发现程序；二进制为本机 x86_64 普通文件；ldd 无缺失、源码或 build 目录依赖，不再链接 turtlesim、pendulum、ros_gz 消息库 |
| 安装版传输 smoke | 从 `/tmp` 运行脚本副本；`ros2 run --check-config`、默认包索引启动、mTLS MQTT→Rust→gateway→mock 状态/描述/控制及动作终态全部通过 |
| 断线恢复 | 临时 broker 中断后终态保留；恢复连接及原请求重试取得已完成结果，不重复执行 |
| 机载配置 | 两种模板复制到安装空间外后可解析；车辆 edge dry run 启用 MQTT 并保留真实后端，不启动设备 |
| 导入完整性 | 除按计划更新的 README 外，24 份导入文件匹配来源 SHA-256；未导入原仓库元数据、机器环境、构建缓存、设备配置或证书 |

新增 Python 回归覆盖：省略 executable 的默认值、显式非法值拒绝、相对路径兼容、包索引优先于 PATH、缺包/缺二进制/无执行权限的预检、外部程序覆盖，以及未构建时的 dry run。独立审查补齐 `rcl_action` 依赖并清除继承的 `CMAKE_IDL_PACKAGES`，避免消息绑定受外部环境污染。首次构建发现独立 CMake 脚本未声明 policy，已补版本声明，使 OFFLINE 布尔选项正确生效；同时清理未继承文件描述符的 Make jobserver 环境。修复后重新构建安装并完成上述 Rust 与传输验证。

只运行本机回环测试端点、临时 broker 和 mock 机器人；未安装系统依赖或 systemd 服务，未连接远端 broker、真实设备或 zkdx。ARM64 原生构建及设备验收仍需在对应环境完成。源码和 Cargo 锁文件已内置，完整第三方 Rust 缓存未打包；离线构建需要预先准备依赖缓存。原相邻 `mqtt_ros2_adapter` 工作树保持原样，框架后续使用内置副本维护。

## chaos_mqtt 单包合并

日期：2026-10-17（Asia/Shanghai）。按单一目录、单一 ROS 包要求，将完整 Rust 实现移入 `src/chaos_mqtt/transport`；父包使用 ament_cmake 与 ament_cmake_python 同时安装业务网关和传输。框架中已移除独立 `mqtt_ros2_adapter` ROS 包，以及旧 chaos_mqtt 的 setuptools 构建入口。

| 检查 | 结果 |
|---|---|
| 源码包发现 | **7 个包**；chaos_mqtt 只有一个 package.xml，transport 无嵌套 ROS 包 |
| 全新普通安装 | `/tmp/chaos-mqtt-merged-yW4WFK` 中独立构建、merge install，**7 个包成功** |
| 全量 Python 回归 | **426 通过**，沿用临时 Xacro 2.1.1 测试环境 |
| chaos_mqtt 的统一 colcon test | Python **32 通过**（属于上述 426 项），Rust **75 通过**；两组 CTest 均成功 |
| 安装隔离 | 空白环境仅 source 系统 Jazzy 与新安装空间，索引恰有 7 个框架包，无旧 mqtt_ros2_adapter 包；Python 导入不含源码路径 |
| 包入口与资源 | `ros2 pkg executables chaos_mqtt` 恰好列出 gateway、mqtt_transport；两者为普通安装文件，传输 helper 来自 share/chaos_mqtt/transport |
| 动态库与兼容 | 无缺失库或构建目录依赖；旧裸 executable 名称解析到同包新入口，不查询旧包或回退 PATH |
| 完整 mTLS 控制链路 | 从 `/tmp` 运行脚本副本，验证同包传输/网关的统一 launch、状态、能力、控制权、起降和终态；断线与重试恢复通过 |
| 资源与源码 | 两类机载模板在安装空间之外解析成功；原 Rust 代码、锁文件、辅助脚本和示例均保持来源哈希 |

本次确认的是软件目录、构建、安装与模拟链路的合并。内部保留 Rust 传输和 Python 网关两个受管进程，公共 ROS/MQTT 业务协议保持一致。没有部署到实机、验证 ARM64 或启动生产服务。迁移应使用全新的 build/install 前缀，避免旧 ament_python 缓存和并列包索引残留，具体步骤见 [机载部署说明](onboard-deployment.md)。

## 数据采集与时间质量

日期：2026-10-17（Asia/Shanghai）。新增独立 `chaos_data` 包，工作空间现为 **8 个包**；与历史并列 MQTT 包布局无关。本机使用 ROS 2 Jazzy、rosbag2 0.26.11，实际存储插件为 MCAP 和 SQLite。

| 检查 | 结果 |
|---|---|
| 全新普通安装 | `/tmp/chaos-data-build` 独立 build/log/install，merge install，**8 个包成功**；最终 Python 修复已重新安装 |
| 全量 Python 回归 | **558 通过**，包含采集配置、启动、会话、时间与审计测试；沿用临时 Xacro 测试依赖 |
| 原生数据包与时间质量测试 | **89 通过**，包含真实临时 MCAP/SQLite 读写、原数据哈希不变、clock step、失同步、低空间、子进程启动/关闭失败与有界回收 |
| 安装隔离与 dry run | 从 `/tmp` 导入的 assets/bringup/data/mqtt 均来自新安装副本；安装版采集模板、模型与 `data_audit` 入口可用；dry run 展示精确话题与时间策略 |
| 完整采集 smoke | Domain 192、localhost、模拟相机/雷达/IMU：未同步不创建 bag，稳定后录制，失稳关段并写污染窗口，恢复后另起独立段 |
| 文件与源时间保真 | 每段实际按 2 秒分卷 MCAP，可读回状态、图像、CameraInfo、点云、IMU、静态 TF、模型及目录；审计生成索引并保留原时间戳 |
| 关闭与重复 SIGINT | 统一 launch 退出后 manifest completed、分段 closed、metadata 完整；独立真实 DDS 回归连续 3 次在关闭中再发 8 次 SIGINT，均正常完成 |
| MQTT 采集状态 | 原生配置检查、临时本机 mTLS broker 和内置传输实际转发采集/时间状态；原有控制、动作终态、断线重试链路通过 |
| 服务模板 | 停止宽限调整为 50 秒；将临时验证副本的 ExecStart 指向当前源码脚本后，systemd-analyze verify 通过，未安装服务 |

回归中发现重复 SIGINT 会在 rclpy 恢复默认信号处理器后打断原生刷盘，造成会话遗留 running；已改为采集器自行处理首次退出信号、关闭期间忽略后续 SIGINT，SIGTERM 仍允许强制终止。监督器在原生录制器就绪后立即复查时间质量并保存启动间隙；静态数据只在同步准入后订阅，失稳后重建订阅，避免暂停录制时丢掉 retained TF/模型。

采集 smoke 的 chrony 结果来自临时查询程序，用于验证逻辑，没有修改系统时钟。当前机器未安装 chronyc，真实严格采集将等待时间质量证据。服务器/客户端配置只是占位模板；没有部署统一 NTP、测量机器人间实际偏差、验证 Gemini 335 硬件同步、进行长时带宽/磁盘压测或 ARM64 构建。默认 10 ms 是主机质量准入门限，不是本次验收得到的传感器同步精度。数据只落本机，训练功能提供原始 bag 和时间索引，没有自动配对或生成训练张量。

## 训练数据规范与离线导出

日期：2026-10-17（Asia/Shanghai）。参考固定版本 Kuavo Data Challenge 的分层组织，扩展 `chaos_data` 离线导出，并新增运行时 `ControlTrace`。工作空间仍为 **8 个包**。本轮范围为训练数据准备、训练与推理契约预留，具体格式见 [训练数据说明](training-data.md)。

| 检查 | 结果 |
|---|---|
| 全新普通安装 | `/tmp/chaos-training-build` 独立 build/log/install，分批完成 **8 个包**，merge install，无源码软链接；最终导出修改已重新安装 |
| Python 回归 | **621 通过，无跳过**：排除新导出文件的全量回归 569 项，新增导出回归 52 项 |
| 控制证据 | 包含 11 项新增运行时回归：发令时刻及剩余有效期、停止/租约失效、失败重试、发布失败不改变控制结果、脱敏和动作切换 |
| 原生存储导出 | 新增测试实际读写 MCAP、SQLite；数值、图像 NPY、来源哈希和原始文件不变通过 |
| 时间与标签边界 | 因果观测、晚到停止、拒绝命令、TTL、未闭合尾部、序号丢失/重复/倒退、无效状态、时钟证据归属、跨段文件拒绝均通过 |
| 图像与输出完整性 | RGB/BGR/mono、16 位深度、浮点像素、大小端和行填充校验通过；原子发布、防覆盖、失败清理及资源上限通过 |
| 安装隔离 | 从 `/tmp` 检查 data/runtime/assets 模块均来自新安装空间；ControlTrace、两类导出模板和 `data_export --help` 可用 |
| 完整训练数据 smoke | localhost / Domain 193，实际统一 launch、SDK 速度控制、停止、原生录包和安装版 CLI 导出通过；严格模式拒绝未验证时间，显式 mock 豁免输出带质量标记 |
| 控制链路回归 | localhost / Domain 187，原有多资产隔离、控制租约、速度 watchdog、导航取消、mock 起降和 SDK 流程通过 |

首次完整 smoke 中，测试配置误将 mock 位姿的 map 坐标系写成 odom，导出按契约拒绝全部无效样本；修正测试配置后通过。新增无效原因统计，便于从 `invalid_reasons` 定位坐标系、数据龄期或动作有效性问题。独立复审补齐动作尾部闭合、源时间倒退拒绝、RobotState 观测时间归属，以及时间日志与配置哈希的一致性检查。

此次真实存储测试使用合成消息和时间质量证据，完整 smoke 使用明确标注的 mock 数据；没有配置真实 chrony 服务器或验证传感器同步精度。输出为本项目的 JSONL/NPY 中间格式，未实现 LeRobot 原生转换、模型训练、模型加载或机载推理。没有运行实际 Gemini 335、无人车/无人机、GPU 训练或 ARM64 构建；Rust 传输未修改，本轮未重复执行此前的 Cargo 测试。
