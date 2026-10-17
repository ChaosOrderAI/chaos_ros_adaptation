# chaos_mqtt

机器人端统一 MQTT 通信包，同时包含 Rust 传输层和 Python 命令网关。框架只构建、安装一个 `chaos_mqtt` ROS 包，统一 launch 从该包启动两个进程。

```text
chaos_mqtt/
├── chaos_mqtt/       # 资产配置转换、命令协议、控制权与 ROS API 网关
├── transport/        # 完整 Rust MQTT ↔ ROS 2 实现、Cargo.lock 和传输示例
├── cmake/            # Cargo 构建与测试入口
├── scripts/          # Python 网关入口模板
├── test/             # Python 协议与网关测试
├── CMakeLists.txt
└── package.xml       # 唯一 ROS 包定义
```

在 `chaos_ws` 中构建并加载环境：

```bash
source /opt/ros/jazzy/setup.bash
colcon build --base-paths src
source install/setup.bash
ros2 pkg executables chaos_mqtt
```

安装后包含 `gateway` 和 `mqtt_transport` 两个程序。常规部署由资产 YAML 和 `chaos_bringup` 的统一 launch 管理两者；手动检查已生成的传输配置时可执行：

```bash
ros2 run chaos_mqtt mqtt_transport --check-config /path/to/generated-bridge.json
```

`ament_cmake` 同时安装 Python 模块、网关入口和 Rust release 程序。构建使用锁定的 Cargo 依赖，可通过 `--cmake-args -DMQTT_ADAPTER_OFFLINE=ON` 使用已准备好的离线缓存；`MQTT_ADAPTER_BUILD_JOBS` 和 `MQTT_ADAPTER_IDL_PACKAGES` 保持兼容。构建依赖、传输协议、来源记录和独立调试方法见 [transport/README.md](transport/README.md)。

Python 与 Rust 测试均归入此包：

```bash
colcon test --packages-select chaos_mqtt --event-handlers console_direct+
colcon test-result --verbose
```

`transport/` 不是独立 ROS 包。保留 Rust crate 的 `mqtt_ros2_adapter` 名称以维持原依赖锁文件和源码兼容；ROS 安装入口统一为 `chaos_mqtt/mqtt_transport`。原工作区相邻的 `mqtt_ros2_adapter/` 仅作为来源保留，不参与本框架的构建或运行。
