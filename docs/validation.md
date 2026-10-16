# 首版验证记录

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
