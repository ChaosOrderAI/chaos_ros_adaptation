# 内置源码来源

此目录在 2026-10-17 从工作区原有 `mqtt_ros2_adapter/` 的**当前工作树**导入，包含当时已存在的未提交改动，不是仅从 Git 提交导出。原目录保持不变；框架后续以本目录为维护和部署入口，两份源码不会自动同步。

- 原仓库当时的基准提交：`fa36988e2f7c72a3672fcb2a7044db2a19f3038e`。
- `SOURCE_MANIFEST.json` 记录所选原文件在导入时的 SHA-256，便于比对来源；README 的框架构建说明和新增 ROS 包装文件不属于原快照。清单不表示文件永远不能修改。
- 原 Rust 业务源码、`Cargo.toml`、`Cargo.lock`、示例、测试辅助脚本和独立部署模板完整保留。
- 未导入 `.git`、`.idea`、`.cargo` 机器环境配置、`target`、Python 缓存、本机 `config.json`、`.local` 或证书/私钥。
- 原 `Cargo.toml` 声明 `MIT`；此次来源目录没有独立 `LICENSE` 文件。本包保留该声明，没有补造著作权人或替原作者追加许可文本。Cargo 依赖各自的许可证保持其原状。

首次导入时新增独立的 `mqtt_ros2_adapter` ROS 包装；随后按统一框架要求将完整源码移入 `chaos_mqtt/transport/`，由父目录唯一的 `package.xml`、`CMakeLists.txt` 和 `cmake/cargo_runner.cmake.in` 与 Python 网关一同构建、安装。传输目录不再包含嵌套 ROS 包。Rust crate 名称、原业务源码、锁文件和来源清单保持不变，安装程序命名为 `lib/chaos_mqtt/mqtt_transport`。

框架默认仅绑定 `std_msgs`、`geometry_msgs`、`sensor_msgs`、`nav_msgs`、`diagnostic_msgs`，以及 r2r 必须的基础消息包，避免隐式依赖开发机的仿真和桌面消息库。扩展消息映射时须通过 `MQTT_ADAPTER_IDL_PACKAGES` 纳入对应包及其消息依赖，并重新编译。

构建使用原 `Cargo.lock`；首次构建仍需取得锁定的 Cargo/Git 依赖，当前没有打包整个 Rust 依赖缓存。离线构建需要事先准备缓存或在匹配环境构建安装产物，`MQTT_ADAPTER_OFFLINE=ON` 会明确拒绝联网下载。
