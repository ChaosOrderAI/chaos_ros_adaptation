# C++17 迁移：最终设计与可移植构建命令

本文档描述 Python 业务实现迁移到原生 C++17 之后的**最终状态**，以及在任意干净机器上复现构建与检查的命令。它不重复迁移过程的历史记录。

相关文档：[架构与包边界](architecture.md)、[验证记录](validation.md)、[Go2 原生 SDK2](unitree-go2.md)、[机载部署](onboard-deployment.md)。

## 1. 迁移边界

迁移把**业务逻辑**从 Python 移到原生 C++17。Python 侧只保留必要的三类内容：

| 保留内容 | 位置 | 性质 |
|---|---|---|
| launch 胶水 | `src/chaos_bringup/launch/*.launch.py` | 参数解析与进程编排，不含业务规则 |
| 薄绑定模块 | `_chaos_assets_core`、`_chaos_bringup_core`、`_chaos_sdk`、`_chaos_mqtt_core` | pybind11 转发到同一份原生实现 |
| 工具与验收脚本 | `scripts/`、`test/`、`benchmarks/` 及 Xacro 辅助程序 | 启动原生实现、生成夹具、检查结果，不承载生产业务规则 |

迁移前的 Python 业务实现、console 脚本和 `chaos.robot_adapters` entry point 组已从仓库移除，不存在回落到旧实现的路径。`chaos_runtime` 与 `chaos_sensors` 只保留原生 gtest 套件，不再有 Python 对拍套件。

明确不在迁移范围内、本轮未改动的部分：

- `src/chaos_mqtt/transport/` 的 Rust MQTT/ROS 2 传输实现（导入的第三方源码）。
- `drivers/src/OrbbecSDK_ROS2/` 的奥比中光官方相机驱动。
- 厂商 SDK（Unitree SDK2）本身。

## 2. 最终分层

九个生产 ROS 包按职责分层，另有一个独立的性能基准包：

```text
chaos_core            无 ROS：Rejected 错误类型、单调/系统时钟、诊断格式化（头文件库）
chaos_interfaces      IDL：状态、控制租约、速度、停止、导航、飞行契约
chaos_assets          配置引擎：schema 校验、型号继承、限值、sensors、录制策略、description、config_digest
                      （不依赖 rclcpp，也不依赖设备 SDK）
chaos_runtime         每资产运行时：chaos_control / chaos_runtime_core / chaos_go2_core / chaos_runtime_ros / chaos_go2_ros
chaos_sensors         chaos_sensors_health / chaos_sensors_config / chaos_sensors_ros
chaos_sdk             原生异步客户端（chaos_sdk 共享库 + _chaos_sdk 薄绑定）
chaos_mqtt            chaos_mqtt_core / chaos_mqtt_gateway（原生 C++）+ transport/（Rust）+ _chaos_mqtt_core 薄绑定
chaos_data            timing / session / file_io / supervisor / controller / ros_message / audit / export（原生 C++）
chaos_bringup         chaos_bringup_core + chaos_bringup_plan + launch 胶水
chaos_benchmarks      领域微基准，非生产入口
```

控制守卫、Go2 状态策略、健康判断和时间质量策略可脱离 ROS 独立测试。ROS 节点、厂商 SDK 和文件读写由相应适配模块负责；资产加载与数据导出库自身包含受控文件 I/O，不将它们描述成纯计算库。规则只写一份，`robot_runtime --validate-asset` 与 launch 预检调用的是同一个 `chaos::runtime::validateAssetJson`。

### 安装后的可执行入口

| 包 | 入口 |
|---|---|
| `chaos_runtime` | `robot_runtime`、`go2_driver` |
| `chaos_sensors` | `mock_sensor`、`sensor_monitor` |
| `chaos_data` | `collector`、`audit`、`data_audit`、`data_export`、`chaos_data_recorder_worker` |
| `chaos_assets` | `asset_config` |
| `chaos_bringup` | `chaos_bringup_plan` |
| `chaos_mqtt` | `gateway`（原生 C++）、`mqtt_transport`（Rust） |
| `chaos_sdk` | `chaos_sdk_mock_asset`（测试对端） |

全部位于 `lib/<package>/`，用 `ros2 run <package> <name>` 调用。

## 3. 可移植构建

### 3.1 前置依赖

需要 ROS 2 Jazzy、colcon、ament_cmake/ament_cmake_python，以及 CMake 包 `nlohmann_json`、`yaml-cpp`、`OpenSSL`、`urdfdom`、`libxml-2.0`（后两者经 `pkg-config`）。Ubuntu 侧的包名与安装命令见 [README](../README.md#开发环境构建与启动)；**本文档只列出依赖，不执行安装**。

`chaos_mqtt/transport/` 需要 Rust/Cargo 工具链与 `build-essential`、`cmake`、`pkg-config`、`libclang-dev`。

### 3.2 全新构建

使用**新的 build / install / log 目录**，加载时只 source 本次安装空间的 `local_setup.bash`，不叠加旧 overlay，避免旧的 `ament_python` 缓存和并列包索引残留：

```bash
source /opt/ros/jazzy/setup.bash

colcon --log-base log build --base-paths src \
  --build-base build \
  --install-base install \
  --cmake-args -DCMAKE_BUILD_TYPE=Release

source install/local_setup.bash
```

`--base-paths src` 是刻意的：工作空间旁边可能存在 ROS 1 工程和厂商 SDK 副本，不应被一并构建。

上述命令采用普通的隔离前缀安装。生产需要合并前缀时增加 `--merge-install`，目标前缀按部署约定指向 `/opt/chaos/ros`，见 [机载部署](onboard-deployment.md)。

### 3.3 Unitree SDK2 可选构建

`chaos_runtime` 的选项 `CHAOS_GO2_WITH_UNITREE_SDK` **默认 OFF**。

```bash
source /opt/ros/jazzy/setup.bash

colcon --log-base log-sdk build --base-paths src \
  --build-base build-sdk \
  --install-base install-sdk \
  --cmake-args -DCHAOS_GO2_WITH_UNITREE_SDK=ON \
               -DCMAKE_PREFIX_PATH="$UNITREE_SDK2_PREFIX"

source install-sdk/local_setup.bash
```

`$UNITREE_SDK2_PREFIX` 指向上游 SDK2 的安装前缀，`find_package(unitree_sdk2 CONFIG REQUIRED)` 必须在该前缀下成立。运行时库搜索顺序见 [Go2 说明](unitree-go2.md)。

### 3.4 离线构建

`chaos_mqtt` 的 Cargo 依赖准备好后，可在构建命令追加 `--cmake-args -DMQTT_ADAPTER_OFFLINE=ON`。Cargo 锁文件固定 Rust 依赖，系统库与 Rust 工具链版本仍由构建环境管理。

## 4. 可移植检查命令

以下命令在已 source 本次安装空间后执行，全部不需要硬件、厂商驱动或远程 broker。

```bash
source /opt/ros/jazzy/setup.bash
source install/local_setup.bash

# 单元测试：gtest（各包 BUILD_TESTING 下）+ pybind 绑定的 pytest
colcon test --packages-select \
  chaos_core chaos_interfaces chaos_assets chaos_bringup chaos_runtime \
  chaos_sensors chaos_sdk chaos_mqtt chaos_data \
  --build-base build --install-base install
colcon test-result --test-result-base build --verbose

# 安装空间的下游消费者：只链接导出的公开 target
benchmarks/verify_consumer.sh install

# 领域微基准（非端到端）
ros2 run chaos_benchmarks micro_benchmark
```

`colcon test` 与 `colcon test-result` 必须指向**本次构建的目录**（`--build-base` / `--test-result-base`），否则读到的是旧安装空间的残留结果。

`benchmarks/verify_consumer.sh` 在临时目录里配置 `benchmarks/consumer`，该工程只 `find_package` 八个公开 C++ 包并链接其导出 target，然后运行产物；临时目录随调用结束删除。

仓库自带的 smoke 脚本位于 `scripts/`，用于端到端的 mock 链路检查。它们使用 Python，其中读取导出的 NPY 需要 `python3-numpy`：

```bash
python3 scripts/smoke_test.py
python3 scripts/go2_smoke_test.py
python3 scripts/mqtt_smoke_test.py
python3 scripts/transport_smoke_test.py
python3 scripts/sensor_smoke_test.py
python3 scripts/description_smoke_test.py
python3 scripts/data_smoke_test.py
python3 scripts/training_data_smoke_test.py
```

各脚本的覆盖范围与限制见 [验证记录](validation.md)。

## 5. 迁移中保留的行为契约

以下兼容性契约由对应回归用例检查：

- 错误码字符串逐字保留（`NO_CONTROL`、`STALE_COMMAND`、`INVALID_FRAME` 等）。
- 常量时间 token 比较，无长度或内容早退；校验顺序与原实现一致。
- 零 Unix 时间戳表示无期限；单调时钟读在前、系统墙钟读在后。
- Go2 零速度命令作为源时间屏障；SDK 写失败闩锁；`step` 不在缓存锁内执行 `move`；遥测采样保留原始 ROS 时间戳。
- 诊断 value 的字符串格式保持一致（浮点格式化与 ties-to-even 取整）。
- `chronyc` 只读调用（`-c -n tracking`）带绝对总超时；退出码、缺失、超时、非 UTF-8 分别映射到不同失败原因。

ROS 时间戳统一经 `chaos::rosStampToNs` 校验（秒字段在 ROS int32 范围内），避免 int64 溢出。

## 6. 验收范围

统一 Release 构建、注册测试、九项 mock/本机 mTLS 验收、数据兼容性检查、安装空间下游消费者及 ROS 往返性能测量已完成，具体计数与测量条件见 [验证记录](validation.md)。SDK/MQTT 的异步生命周期另经 ASan、UBSan 和泄漏检查。

SDK-ON 验证了编译、链接、能力报告与下游导出 target，未连接真实机器狗。真实设备时序、ARM64、长期压力和生产部署仍需目标环境验收；本机 mock 结果不构成硬实时或硬件安全认证。
