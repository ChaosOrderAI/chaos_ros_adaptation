# 行为树节点

`chaos_bt` 把 `chaos_sdk::Robot` 的导航、起飞和降落包装成 BehaviorTree.CPP 节点。行为树跑在调用方进程里，由调用方 tick；runtime 仍然只执行单个动作，租约、能力检查和停止都由 runtime 决定。

节点不申请租约，也不续租。调用方先 `acquire`，再把同一个 `Robot` 交给 `chaos_bt::register_nodes`。`Robot` 的 executor 必须在别的线程上转，不能放在 tick 行为树的线程上。

```cpp
auto robot = chaos_sdk::Robot::create_standalone("vehicle_001");
BT::BehaviorTreeFactory factory;
chaos_bt::register_nodes(factory, robot);
auto tree = factory.createTreeFromText(xml);
```

## 节点

| 节点 | 输入 | 输出 |
|---|---|---|
| `Navigate` | `frame_id`、`x`、`y`、`z`、`yaw`，可选 `ttl_s` | `goal_id`、`task_state`、`progress`、`code`、`message`、`current_pose` |
| `Takeoff` | `altitude_m`，可选 `ttl_s` | 同上，另有 `current_altitude_m` |
| `Land` | 可选 `ttl_s` | 同 `Takeoff` |

`ttl_s` 只限制动作何时可以开始，不限制动作多久必须结束。`yaw` 转成绕竖直轴的四元数。缺端口、空 `frame_id` 和非有限数值在发送目标之前抛出，属于树的配置错误。

一次 tick 只查询 future，不等待。服务端接受之前是 `RUNNING`；拒绝是 `FAILURE`，`task_state` 为 `rejected`。终态用 SDK 的分类：只有服务端报告成功且业务结果也成功才是 `SUCCESS`。`failed`、`canceled`、`timed_out` 和 `unknown` 都是 `FAILURE`。

## 取消

`halt` 不代表动作已经停下来。

- 动作已被接受时，节点发出一次取消请求，输出 `code=CANCEL_REQUESTED`。这只说明请求已发出。
- 动作还没被接受时，节点输出 `code=CANCEL_DEFERRED`，并在受理之后补发一次取消。补发发生在行为树之外，不再改写黑板。
- 只有后续 tick 观察到服务端的 `CANCELED` 终态，`task_state` 才是 `canceled`。
- 本地观察超时输出 `timed_out`，客户端关闭或取消发不出去输出 `unknown`。这两种情况都不表示远端已经停止，节点也不会自动再发一次目标。

旧动作还没到终态时，runtime 可能以 `BUSY` 拒绝同一个资产上的新动作。租约由调用方持有，`halt` 不释放租约。

## 反馈

反馈回调只把最新样本记下来，下一次 tick 再写到输出端口。样本可能被合并或丢弃，输出是最近一次看到的值，不是完整的反馈序列。

XML 里写了的输出端口按 BehaviorTree.CPP 的重映射写入；没有写的端口仍写到黑板上同名的条目，不会被丢掉。

## 构建

`chaos_bt` 依赖 `ros-jazzy-behaviortree-cpp`。`chaos_sdk` 和 `chaos_runtime` 不依赖它。

```bash
source /opt/ros/jazzy/setup.bash
colcon build --base-paths src --packages-select chaos_bt
colcon test --base-paths src --packages-select chaos_bt
```
