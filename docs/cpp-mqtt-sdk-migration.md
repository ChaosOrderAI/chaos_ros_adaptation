# MQTT 网关与 SDK 的 C++ 迁移说明

本文记录 `chaos_mqtt` 与 `chaos_sdk` 从 Python 迁到 C++17 的边界、协议契约、
构建方式与验收证据。所有自研业务逻辑现在都在 C++ 侧，Python 只剩薄绑定。

## 1. 归属与边界

| 模块 | 归属 | 说明 |
| --- | --- | --- |
| `src/chaos_mqtt/transport/` | 导入的 Rust 基础设施 | 保持原样：TLS/mTLS、重连、回执合并、队列语义均由 Rust 适配器负责，C++ 侧不重写、不改源码。 |
| `src/chaos_mqtt/src/protocol.cpp` | 自研协议（C++） | 纯逻辑，不依赖 rclcpp：信封校验、默认值、期限、错误码、响应封装、请求关联缓存。 |
| `src/chaos_mqtt/src/adapter_config.cpp` | 自研配置（C++） | 供 `chaos_assets`/`chaos_bringup` 复用的适配器配置解析与校验，可独立链接。 |
| `src/chaos_mqtt/src/gateway_node.cpp` | 自研网关（C++） | rclcpp/rclcpp_action 节点：topic/服务/Action 路由、关联重放、事件相位、终态。 |
| `src/chaos_sdk/` | 自研 SDK（C++） | 原生异步客户端：能力发现、租约、速度、停止、动作受理/结果/取消。 |

未复制配置逻辑到其他模块，Python 侧没有保留第二套业务实现。

## 2. 协议契约（保持不变）

* 线格式、字段名、默认字段、错误码、终态事件语义与原 Python 版一致。
* `response.data` 保持嵌套：`scripts/mqtt_smoke_test.py` 断言
  `reply['response']['data']['token']`，服务响应载荷不会被摊平。
* 期限使用 Unix UTC 毫秒，原样传播到 runtime；队列等待不会刷新期限。
* `deadline_time()` 先按 int64 校验再窄化：秒数必须落在 `(0, 2^31)`，
  跨 2038 边界与当前时间都通过；亚秒向下取整不会延长有效期。
* 数值拒绝 bool、JSON 深度/极值拒绝、未知字段拒绝、显式 `null` 与“省略字段”
  语义区分（省略按协议默认值，显式 null 拒绝）。

协议回归由原生协议测试和执行实际网关的 smoke 脚本覆盖；迁移时另外进行了独立差分校验。

## 3. 网关行为

* pending 关联缓存与 completed 重放缓存均有上限（`Correlations`），请求超时用单调时钟。
* 服务请求超时或被取消时，调用 `rclcpp::Client::remove_pending_request()` 清理
  rclcpp 内部 pending，避免静默服务无限堆积。
* 动作超时只取消**本地观测**：不发送远端取消，也不声称远端已停止；
  终态里 `execution_state` 仍是 `unknown`。
* 动作结果为 `UNKNOWN` 或缺少 result 时 `execution_state="unknown"`，不会谎称 known。
* 迟到响应（本地已超时）被丢弃，不会二次结算；销毁中的 pending 也会被解决。
* 每个在途 goal 拥有专用 action client；超时只销毁对应观测，正常完成的 client 在有界空闲池中复用。释放延迟到回调结束后的安全位置。详见 [MQTT 设计](cpp-mqtt-migration.md)。

## 4. SDK（C++ 原生 API）

`chaos_sdk::Robot` 是唯一的实现，Python 只是绑定：

```cpp
#include "chaos_sdk/robot.hpp"

auto robot = chaos_sdk::Robot::create_standalone("vehicle_001");   // 或传入外部 node
robot->set_response_timeout(5.0);        // 本地响应超时（不是 TTL）
auto lease = robot->acquire("operator", 5.0).get();
if (!lease.success) { throw std::runtime_error(lease.message); }
// target 为调用方显式准备的 geometry_msgs::msg::PoseStamped
robot->velocity(0.2, 0.0, 0.0, 0.0);            // 返回 correlation id
auto goal = robot->navigate(target);            // std::shared_future<shared_ptr<NavigationTask>>
auto task = goal.get();                         // 拒绝时是 nullptr
if (!task) { throw std::runtime_error("goal rejected"); }
// 如需取消，在等待结果之前调用 task->cancel()，并检查最终状态。
auto result = task->result().get();              // 终态结果（WrappedResult）
```

要点：

* **typed 任务对象**：`ActionTask<ActionT>` 是动作的用户可见句柄。C++ 里
  `async_get_result()` / `async_cancel_goal()` 属于 Client 而非 handle，任务对象把
  两者封装好，调用方不需要接触私有 client。
* **受理历史独立于 ROS 资源**：goal handle 在终态/超时/close 时被释放，但
  `accepted()` 与 `goal_id()` 在受理时记录一次并一直保留，因此
  “这个 goal 被受理过、id 是多少”在资源清理之后依然可读。
* **本地响应超时**：每个异步请求都有本地超时（默认 5s，action 受理后切换到动作
  超时 300s）。超时抛 `chaos_sdk::TimeoutError`；`close()` 后未决请求抛
  `chaos_sdk::ClientClosedError`。TTL 只约束“何时可以开始执行”，不是响应超时。
  超时只终止**本地观测**：远端是否停止仍然未知，状态语义是 `timed_out`，不是
  “已停止”。
* **服务发现**：中间件会丢弃发给尚未发现之服务的请求，所以 SDK 在服务被发现之后
  才把请求交给中间件（看门狗在节点 executor 上重试）。因此调用不会因为发现延迟
  而丢失请求；发现等待计入本地响应超时预算。
* **有界 pending**：默认上限 64（`set_max_pending`），超出抛
  `chaos_sdk::PendingOverflowError`，不会无界增长。
* **单一完成权**：每个在途请求是一个 pending operation，claim（从 pending 表中移除
  该条目）是唯一的完成令牌，claim 与状态迁移在同一把锁的临界区内完成。晚到的响应、
  晚到的受理、晚到的取消应答都只能看到“没有条目”，因此不会覆盖已经确定的终态。
  例如：goal 已经 SUCCEEDED、而取消请求迟迟不应答时，取消超时会去 claim 这个
  goal，claim 失败就不会把 SUCCEEDED 改写成 `timed_out`。
* **动作资源**：每种 action 一个有界 slot 池（默认 8 个）。slot 带有 generation，
  迟到的回调只能释放自己那一代租约，不会误伤复用后的新 goal。slot 被 retire 时
  会注销它在节点上的 waitable 注册并释放 client，池因此不会随超时请求增长。
* **生命周期**：所有状态放在共享 `Impl` 里，回调只捕获 `weak_ptr`，销毁后不会
  访问已释放内存；`close()` 幂等，解决全部 promise 并清理 pending；
  `create_standalone()` 自持独立 `rclcpp::Context`、node 与 executor 线程，
  只 shutdown 自己拥有的 context，因此关闭一个客户端不影响其它客户端。
* **状态只读**：`state()` 返回 `RobotState::ConstSharedPtr`，外部无法篡改缓存。
* **并发**：`close()` 与发送并发安全；关闭后调用抛稳定异常而不是崩溃。

### Python 兼容层

`chaos_sdk` 的 Python 包只剩薄绑定：

* `chaos_sdk.Robot(asset_id, namespace=None, base_frame=None, command_timeout_s=0.5,
  command_ttl_s=5.0, response_timeout_s=5.0)`：原生客户端自持 context、node 与
  executor 线程，**不再接收 rclpy Node**（原 rclpy 业务实现已删除）。旧签名
  `Robot(node, asset_id, ...)` 仍然可用，但传入的 node 会被忽略。
* 响应以 `Message` 返回：它是绑定层的只读视图，同时支持属性访问
  （`description.base_frame`）与下标访问（`description['base_frame']`），嵌套字段（如
  `state.pose.pose.position.x`）同样是 `Message`，因此不需要在 Python 侧做 ROS
  消息编解码。
* Python 受理 Future 返回 `Task`；服务端明确拒绝 goal 时 `accepted=False`（C++ 对应空任务指针），本地超时等错误抛异常。`Task` 带 `accepted`/`goal_id`/`state` 属性，`result()` 与 `cancel()`
  等待原生结果；`get_result_async()` / `cancel_goal_async()` 返回可通过 `done()` 轮询的 Future。
* `Future.result()` 与 `Task.result()` 在等待期间释放 GIL，因此同一客户端的
  其他 Python 线程与原生 executor 在等待期间仍然可以推进。
* 能力、租约、TTL、超时、ROS 客户端全部在 C++ 侧，Python 只做边界转换。

## 5. 构建与安装

```bash
source /opt/ros/jazzy/setup.bash
colcon --log-base log build --base-paths src \
  --build-base build --install-base install \
  --packages-up-to chaos_sdk chaos_mqtt \
  --cmake-args -DCMAKE_BUILD_TYPE=Release
source install/local_setup.bash
```


产物：

* `chaos_mqtt`：`lib/chaos_mqtt/gateway`（C++ ELF 入口）、`lib/libchaos_mqtt_core.a`、
  `lib/libchaos_mqtt_gateway.a`、`include/chaos_mqtt/*.hpp`、
  `lib/chaos_mqtt/mqtt_transport`（Rust 适配器）、Python 薄绑定 `_chaos_mqtt_core`。
* `chaos_sdk`：`lib/libchaos_sdk.so`、`include/chaos_sdk/*.hpp`、
  Python 薄绑定 `_chaos_sdk`。
* 导出目标：`export_chaos_mqtt` / `export_chaos_sdk`（`ament_export_targets`），
  外部包可以直接 `find_package(chaos_mqtt)` / `find_package(chaos_sdk)`。

CMake 胶水修复：`chaos_mqtt_core`/`chaos_mqtt_gateway` 打开
`POSITION_INDEPENDENT_CODE`（pybind 模块需要链接静态库）；sanitizer 选项
（`CHAOS_MQTT_SANITIZE`、`CHAOS_SDK_SANITIZE`）作用在**被测库本身**并作为
PUBLIC 用法要求传递给测试；`pybind11_add_module` 前补 `find_package(Python3 ...)`；
`chaos_mqtt/package.xml` 去掉与 `<depend>` 重复的 `exec_depend`。

## 6. 测试

* `chaos_mqtt`：`test_protocol`、`test_adapter_config`、`test_gateway_unit`、
  `test_gateway_integration`（mock ROS 服务/Action）、`test_action_observation`（释放与延迟派发）、`chaos_mqtt_python`
  （薄绑定）、`mqtt_adapter_cargo_test`（Rust 传输原有测试）。
* `chaos_sdk`：`test_robot_native` —— 真实 mock ROS 服务与真实 action server，
  覆盖 describe/租约获取续租释放/失败不装 token/速度前置条件、
  静默服务本地超时、动作受理→结果、动作拒绝、动作取消、
  `close()` 解决未决请求并拒绝后续调用、pending 上限、受理与 goal id 在资源清理后
  仍可读、目标位姿确实到达服务端、取消超时不覆盖已完成 goal、
  retire 的 slot 真的释放了 ROS client（weak 引用过期）、超过池容量的连续 goal
  之后健康 goal 仍然可用。
* `chaos_sdk/test/test_client.py` —— 原生绑定的端到端测试：mock asset 作为独立
  进程运行，Python 客户端跨进程发现它，覆盖 describe/acquire/navigate/cancel/close、
  扁平 dict 与真实 `geometry_msgs/PoseStamped` 两种目标位姿、关闭一个客户端不影响
  另一个。绑定是强制依赖，模块缺失时直接失败而不是整体 skip。
