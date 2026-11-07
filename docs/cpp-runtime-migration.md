# C++17 迁移：运行时与传感器最终设计

范围：`chaos_runtime` 与 `chaos_sensors` 两个包（`src/chaos_runtime`、`src/chaos_sensors`）以及它们依赖的 `chaos_core`、`chaos_interfaces`。

本文档描述迁移后的**最终设计**和可移植的构建/检查命令。迁移前的 Python 实现、console 脚本和 entry point 组已从仓库移除；这两个包现在只有原生 gtest 套件，没有 Python 对拍套件。

总体迁移边界见 [C++17 迁移总览](cpp-migration.md)，实测结果见 [验证记录](validation.md)。

## 1. 生产入口

| 入口 | 迁移前 | 现在 |
|---|---|---|
| `robot_runtime` | `chaos_runtime/node.py`（rclpy） | `lib/chaos_runtime/robot_runtime`（rclcpp 可执行文件） |
| `go2_driver` | `chaos_runtime/go2_driver.py`（rclpy） | `lib/chaos_runtime/go2_driver`（rclcpp 可执行文件） |
| `mock_sensor` | `chaos_sensors/mock.py`（rclpy） | `lib/chaos_sensors/mock_sensor`（rclcpp 可执行文件） |
| `sensor_monitor` | `chaos_sensors/monitor.py`（rclpy） | `lib/chaos_sensors/sensor_monitor`（rclcpp 可执行文件） |

`ros2 pkg executables chaos_runtime` 只列出 `robot_runtime` 与 `go2_driver`；`ros2 pkg executables chaos_sensors` 只列出 `mock_sensor` 与 `sensor_monitor`。Python console 脚本和 `chaos.robot_adapters` entry point 组随 Python 业务实现一并移除，不存在静默回落到旧实现的路径。CLI 名称、`asset_json` / `sensor_json` 参数、错误码和资产契约保持不变。

这两个包不再安装 Python 包。launch 工具链调用 `chaos_bringup` 的原生 `validate_asset_with_runtime`，由受限时长和输出大小的子进程执行 `robot_runtime --validate-asset`。预检检查退出码及 JSON 中的 `valid: true`，运行时启动时再次应用同一份资产规则。

## 2. 分层

```text
chaos_core            clock / errors / overflow-safe time / formatting
  └── chaos_control   leases, deadlines, velocity guard, pose validation   (ROS-free)
        ├── chaos_runtime_core     asset contract, asset rules, coordinator,
        │                          mock adapter                       (ROS-free)
        │     ├── chaos_runtime_ros  ros_adapters + runtime_node (rclcpp facade)
        │     └── chaos_go2_ros     go2 driver node + SDK boundary
        └── chaos_go2_core    Go2 transport safety + telemetry conversion (ROS-free)
```

`chaos_runtime_core` 是独立导出的 target，只链接 `chaos_control`，因此领域测试和外部消费者可以复用协调器而不引入 rclcpp 或 DDS。rclcpp 外壳负责回调组、executor 和消息转换；所有控制决策都留在无 ROS 的协调器里。**没有任何 ROS 类型进入** `chaos_control`、`chaos_runtime_core` 或 `chaos_go2_core`。

导出的依赖：`chaos_runtime` 导出 `chaos_core`、`chaos_interfaces`、`rclcpp`、`rclcpp_action`、`geometry_msgs`、`nav_msgs`、`std_msgs`（启用 SDK 后端时另加 `unitree_sdk2`）；`chaos_sensors` 导出 `chaos_core`、`rclcpp`、`sensor_msgs`、`diagnostic_msgs`、`std_msgs`。

## 3. 线程与所有权

- `robot_runtime` 使用一个 4 线程 `MultiThreadedExecutor`。
- `services_group`（互斥）：`description`、`control/lease`、`motion/stop`。
- `action_group`（互斥）：只放 goal / cancel / accepted 回调。accepted 回调不阻塞、不为每个 goal 新开线程；它只做 `execute()` 状态迁移（未注册 execute 回调，因此不会阻塞），然后把 goal 交给工作线程。
- `timer_group`：20 Hz 看门狗（`create_wall_timer`，50 ms）。因此 `/clock` 暂停或回拨不会让租约过期、速度超时或停止重试失效。Go2 驱动使用同一机制。
- **一个常驻 action 工作线程**，队列有界（最多 4 个待处理任务）。作业做异常隔离：抛出的作业被记录，不会触发 `std::terminate`。工作线程停止期间拒绝新作业，由调用方向客户端投递终态结果。
- 每个 ROS 端点都是其节点或适配器的成员。`rclcpp::Node` 只持有弱引用，因此 `create_service()` / `create_subscription()` 返回的临时对象若不保存会立刻析构，端点会静默消失。
- **两阶段关停**：`close()` 标记关停、打断正在进行的操作并等待（上限 2 秒）直到没有在途操作；`finalize()` 在 action 工作线程 join 之后才关闭适配器。适配器因此不会被工作线程和关停路径并发操作。

## 4. 资产命名空间是端点的唯一权威

原生运行时和原 `node.py` 一样，所有业务端点都从资产命名空间派生（`prefix = asset['namespace'].rstrip('/')`），Action server 也不例外：

```text
/robots/vehicle_001/state            /robots/vehicle_001/control/lease
/robots/vehicle_001/command_results  /robots/vehicle_001/motion/stop
/robots/vehicle_001/control_trace    /robots/vehicle_001/motion/velocity
/robots/vehicle_001/description      /robots/vehicle_001/navigation/to_pose
```

不使用任何 `__ns` 重映射来隐藏这一规则。传感器监测节点的端点从**资产**命名空间派生（`<asset namespace>/sensors/diagnostics` 与 `<asset namespace>/sensors/description`），而不是逐传感器命名空间，因此传感器顺序和数量不会移动端点。

## 5. 面向 launch 工具链的外部接口

两个接口都在 `rclcpp::init` 之前处理：不创建 ROS 上下文、不打开设备、不启动 broker、不触碰网络。

### `robot_runtime --validate-asset`

从 stdin 读取一份有大小上限的 JSON 资产（最大 1 MiB）。

- 成功：stdout 输出 `{"valid":true}`，退出码 0。
- 失败：原因写入 stderr，退出码非 0。

规则就是运行时自己的规则：`chaos::runtime::validateAssetJson`（`src/chaos_runtime/src/asset_validation.cpp`）。运行时启动路径经 `createAdapter` 调用同一个函数，因此预检与运行时不会不一致。未知的驱动插件被拒绝；mock 插件保留 kind 差异（地面 kind 不能声明 `flight.takeoff` / `flight.land`）。

### `go2_driver --build-capabilities`

输出 `{"unitree_sdk2":true|false}` 并以 0 退出。这个查询只回答一个问题：**这个二进制里是否包含真实的 Unitree SDK2 后端**。它不说明硬件是否可用、网络是否可达或机器狗的状态，也从不初始化厂商 DDS 传输。

SDK-OFF 的二进制在 real 模式下仍然拒绝启动：

```text
[FATAL] [go2_driver]: startup failed: chaos_runtime was built without
Unitree SDK2 support (CHAOS_GO2_WITH_UNITREE_SDK=OFF); a real Go2 driver
cannot start.
```

## 6. Unitree SDK2：编译与链接证据

SDK 是官方仓库，固定在提交 `d1abf001220d3e5f59e00a0c4aaa595e00089a4a`，安装在**非系统前缀**下（没有系统级安装，没有接触机器狗，从未初始化厂商 DDS 传输）。

构建使用与常规构建隔离的独立前缀：

```bash
source /opt/ros/jazzy/setup.bash

colcon --log-base log-sdk build --base-paths src \
  --build-base build-sdk \
  --install-base install-sdk \
  --symlink-install \
  --cmake-args -DCHAOS_GO2_WITH_UNITREE_SDK=ON \
               -DCMAKE_PREFIX_PATH="$UNITREE_SDK2_PREFIX"

source install-sdk/local_setup.bash
```

后端只通过 `find_package(unitree_sdk2 CONFIG REQUIRED)` 及其导出的 target 消费厂商库，不手写 include 或库路径。SDK-ON 的 `go2_driver` 含 `unitree::robot` 符号；SDK-OFF 的二进制不含，并报告 `{"unitree_sdk2":false}`。

SDK **只做了编译与链接验证**。真实后端从未执行：没有初始化 DDS 传输，没有连接机器狗，也不作任何硬件时序结论。`SetTimeout` 只配置 SDK 发送超时，不能证明 `Move` 是非阻塞的或硬实时的。

### 运行时库搜索顺序

SDK 前缀自带 `libddsc.so.0` 与 `libddscxx.so.0`，而 ROS 2 Jazzy 自己也带一份 `libddsc.so.0`（`/opt/ros/jazzy/lib/x86_64-linux-gnu/`）。必须让厂商目录排在 ROS 库目录之前，否则会解析到与厂商 `libddscxx` 不配套的那一份 `libddsc`，形成版本错配：

```bash
export LD_LIBRARY_PATH="$UNITREE_SDK2_PREFIX/lib:/opt/ros/jazzy/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
```

部署时通过服务环境文件导出该变量，见 [机载部署](onboard-deployment.md)。

## 7. 构建与检查命令

```bash
source /opt/ros/jazzy/setup.bash

# 全新构建目录；只加载本次安装空间，不叠加旧 overlay
colcon --log-base log build --base-paths src \
  --build-base build --install-base install \
  --cmake-args -DCMAKE_BUILD_TYPE=Release
source install/local_setup.bash

colcon test --packages-select chaos_runtime chaos_sensors \
  --build-base build --install-base install
colcon test-result --test-result-base build --verbose
```

覆盖范围：

| 套件 | 覆盖内容 |
|---|---|
| `test_control_cpp` | 无 ROS 的控制守卫：租约、期限、速度守卫、位姿校验 |
| `test_coordinator_cpp` | 无 ROS 的协调器（只链接 `chaos_runtime_core`） |
| `test_asset_validation_cpp` | 共享的无 ROS 资产规则 |
| `test_go2_driver_core_cpp` | **无 ROS** 的 Go2 核心：不涉及 DDS，不涉及 ROS 消息 |
| `test_ros_adapters_cpp` | ROS 层状态边界，真实订阅 |
| `test_runtime_ros_cpp` | ROS 层运行时：端点、租约、停止、goal 成功、goal 取消、冻结的 ROS 时钟 |
| `test_go2_driver_ros_cpp` | ROS 层驱动与假 SDK 后端：遥测、命令写入、里程源值、遥测过期、畸形遥测、SDK 写故障、关停握手 |
| `test_health_cpp` | 传感器健康领域逻辑 |
| `test_sensor_clock_cpp` | 传感器时钟冻结/恢复与诊断行为 |

`chaos_runtime` 与 `chaos_sensors` 两个包**没有注册 pytest 套件**：迁移前的 Python 对拍实现已从仓库移除，原生实现是唯一的实现来源。

各套件当前的通过数量与整体结论见 [验证记录](validation.md)。

## 8. 验证边界

本机验收覆盖 x86-64、mock 后端、ROS 接口和假 SDK 的故障路径。真实机器狗、其他设备、ARM 平台和长期运行仍需要在对应环境验收。SDK-ON 的运行环境必须加载匹配的厂商动态库，见第 6 节。

原生 gtest 与必要的端到端测试脚本共同约束行为；迁移前的实现可从 Git 基线读取用于独立对照，不再作为另一套业务实现保留在工作树中。公开接口和关键内部边界的注释说明了单位、时钟、所有权、线程和错误处理；性能结论及其适用范围见验证记录。
