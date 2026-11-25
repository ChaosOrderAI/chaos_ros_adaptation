# 人形机器人后端（原生 humanoid / Booster T2）

本页描述 `chaos_runtime` 的**原生人形机器人**支持：厂商中立的 `HumanoidBackend` 契约、通用 `humanoid_driver` 可执行程序，以及 Booster T2 真实后端的构建与配置方式。整体沿用现有 ROS / SDK / MQTT 主接口，覆盖整机平面运动这一层。

## 后端契约与公共结构

- `src/chaos_runtime/include/chaos_runtime/humanoid/backend.hpp`：厂商中立的 `HumanoidBackend`，提供 move/stop 接口、由后端自行持有并回调的遥测通道，以及后端工厂注册表。
- `driver_core.hpp`：各后端共用的截止时间与看门狗安全逻辑（命令超时、状态陈旧、输出失败锁存）。
- 可执行程序 `humanoid_driver` 接受 `asset_json`，由统一 launch 管理，生命周期与现有其他原生驱动一致。

## 能力边界

| 能力 | 真实后端 | mock 后端 |
|---|---|---|
| `state` | 支持 | 支持 |
| `motion.velocity` | 支持 | 支持 |
| `motion.stop` | 支持 | 支持 |
| `navigation.pose` | 不支持 | 支持 |

- 平面运动：线速度 x/y 与角速度 yaw，无 z 轴速度、无横滚/俯仰分量。
- 模式切换由机器人侧负责：驱动不下发 `ChangeMode`，也不执行 `GetUp`，机器人须已处于可接受速度命令的行走模式。
- 真实后端要求 `rt/robot_states` 出现 mode2 行走心跳，同时 `rt/odom` 保持新鲜（由机器人侧 ROS bridge 提供）。任一条件不满足即判为状态无效并拒绝下发运动。

## Booster T2 构建

`chaos_runtime` 的 CMake 选项 `CHAOS_HUMANOID_WITH_BOOSTER_SDK` **默认 OFF**：

- **OFF（默认）**：不链接厂商 SDK。mock 模式与全部本地校验可用；real 模式的资产在启动前的预检阶段被拒绝并报告原因。
- **ON**：必须提供 `-DBOOSTER_SDK_ROOT=/absolute/sdk`。构建会包含该目录下的 `include/`，并链接 `lib/${arch}/libbooster_robotics_sdk.a`。

本次编译验证使用官方 SDK commit `87a9a26d06a94ddd50968e7576f1170f6fa5a80f`（[官方仓库](https://github.com/BoosterRobotics/booster_robotics_sdk)）。生产链路只使用原生 C++ SDK。

```bash
source /opt/ros/jazzy/setup.bash
# 默认构建（不含厂商 SDK）：使用全新 build / install / log 目录
colcon --log-base log-humanoid build --base-paths src \
  --build-base build-humanoid --install-base install-humanoid \
  --cmake-args -DCMAKE_BUILD_TYPE=Release
source install-humanoid/local_setup.bash
```

开启 SDK 时另开终端，只加载系统 ROS 环境，并使用另一套构建目录：

```bash
source /opt/ros/jazzy/setup.bash
colcon --log-base log-humanoid-sdk build --base-paths src \
  --build-base build-humanoid-sdk --install-base install-humanoid-sdk \
  --cmake-args -DCMAKE_BUILD_TYPE=Release -DCHAOS_HUMANOID_WITH_BOOSTER_SDK=ON \
  -DBOOSTER_SDK_ROOT=/absolute/sdk
source install-humanoid-sdk/local_setup.bash
# 构建能力自证（不需要连接机器人）
ros2 run chaos_runtime humanoid_driver --build-capabilities
# 默认构建输出 {"booster_sdk":false}；SDK-ON 输出 {"booster_sdk":true}
```

## 配置

- `src/chaos_assets/config/humanoid.demo.yaml`：两个 mock 实例 `t2_001` / `t2_002`（同一开发主机）；`src/chaos_assets/config/t2.onboard.example.yaml`：单台真实 T2 的机载模板，拷贝到 `/etc/chaos/` 后按设备实际情况修改。
- 型号定义：`src/chaos_assets/models/booster_t2/v1/model.yaml`。

```bash
# 规划检查（不启动节点、驱动或 MQTT 连接）；去掉 dry_run:=true 才真正启动 mock 实例
ros2 launch chaos_bringup system.launch.py \
  inventory:="$PWD/src/chaos_assets/config/humanoid.demo.yaml" dry_run:=true
```

### 驱动选项

| 选项 | 含义 |
|---|---|
| `backend` | 当前为 `booster_t2` |
| `source_odom_frame` / `source_base_frame` | SDK 原始里程计/机体 frame 名，必须显式配置 |
| `network_interface` | 厂商 DDS 使用的网卡名或本地 IPv4 地址 |
| `dds_domain_id` | 取值 0..232，必须与机器人上厂商服务的 domain 一致 |
| `state_topic` | 当前后端要求 `rt/odom` |
| `robot_state_topic` | 当前后端要求 `rt/robot_states` |
| `velocity_frame` | 当前要求 `body`，x/y/yaw 均为机体坐标系 |
| `cmd_vel_topic` | 速度命令话题（绝对路径），按设备实际命名填写 |
| `odom_topic` | 里程计话题（绝对路径），按设备实际命名填写 |

示例文件里的默认值需与真实设备核对后再使用。厂商话题本身不带后缀，因此每台真实机器人必须使用**唯一的 Booster DDS domain**；话题或里程计命名冲突在启动规划阶段被拒绝。

框架的 `source_odom_frame` / `source_base_frame` 选项与资产的 per-asset `frames` 是两套**独立**检查项：框架只重映射 frame 名称，不做任何坐标变换。

## 安全语义

- 状态、命令或模式任一陈旧即清空当前速度命令。
- 截止时间基于 Linux 单调时钟并固定时钟域身份，系统时间调整不会影响看门狗判定。
- 收到新鲜里程计与行走模式，且成功下发零速度后，才允许新运动指令。
- 初始零速度提交失败时保持未就绪并重试；握手后的输出失败锁存到驱动重启。状态恢复后需新指令，不重放旧命令。
- 关闭驱动时先等正在投递的 SDK 回调完成，并屏蔽关闭后仍排队的回调，避免访问已销毁的状态；构造失败也走同一清理路径。
- 零速度调用返回本地提交结果，不代表实机已停止。

## 适用范围与验证状态

- 覆盖整机平面运动。T2 整机为 31 DoF，本次未提供关节级接口与臂部动作；真实导航不在本次范围内，mock 后端以 `navigation.pose` 提供该能力。
- 未在真实机器人上做实机测试，端到端延迟尚未测量。
- 运行时单测使用 fake 后端；`scripts/humanoid_smoke_test.py` 通过统一 launch 和 SDK 验证双 mock 的平面运动、停止、指令过期与设备隔离。

## 本地验证

本次 x86_64 环境已完成 SDK-OFF 与 SDK-ON 编译链接、701 项测试检查（0 错误、0 失败、5 跳过），以及双人形机器人 mock 冒烟测试。

```bash
source /opt/ros/jazzy/setup.bash
source install-humanoid/local_setup.bash   # 目录清理后先按上文 build-humanoid 命令重建
colcon test --base-paths src --packages-select chaos_assets chaos_runtime chaos_bringup \
  --build-base build-humanoid --install-base install-humanoid
colcon test-result --test-result-base build-humanoid --verbose
python3 scripts/humanoid_smoke_test.py
```

## 扩展新后端

后端工厂注册表让同一套驱动骨架复用不同厂商实现。新增厂商后端除注册工厂外还需要：

1. 实现 `HumanoidBackend`（move/stop 与自行持有的遥测回调）。
2. 在后端工厂注册表注册该后端。
3. 增加后端专属的配置校验与构建选项。
4. 扩展构建能力报告，并更新 `chaos_bringup` 的 `driver_registry.hpp` 后端能力映射（当前 humanoid 映射到 `booster_sdk`，新增后端必须同步更新）。
5. 补充型号定义与测试。

## 官方参考

- T2 入门：<https://docs.booster.tech/zh-CN/docs/product-manual/t2/getting-started/overview/>
- C++ SDK：<https://docs.booster.tech/zh-CN/docs/developer-guide/cpp/sdk-overview/>
- Booster OS Python SDK：<https://docs.booster.tech/zh-CN/docs/developer-guide/booster-os-python-sdk/sdk-overview/>
- Agent 开发：<https://docs.booster.tech/zh-CN/docs/developer-guide/agent-development/intro/>
