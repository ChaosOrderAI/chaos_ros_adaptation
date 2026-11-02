# MuJoCo 仿真

`chaos_sim` 是可选 C++17 ROS 2 包。一个 `SingleThreadedExecutor` 进程运行 Go2、T2 两个独立物理世界，包含真实重力、碰撞、关节 PD、标准 ROS 观测。默认剔除纯视觉资源，保留碰撞与惯量；Python 查看器单独加载完整视觉模型。

Go2 使用官方 home 关键帧，T2 使用官方 qpos0。T2 尚未接入平衡策略，PD 驱动后会倒地；浮动根保持真实动力学。首轮控制入口是命名关节目标，尚未接入统一 `chaos_sdk::Robot`、控制租约或 `motion.velocity` 行走接口，也未实现仿真相机与真机 SDK 适配。现有 mock/real 资产启动方式继续独立使用。

## 构建

需要已有 MuJoCo C SDK、ROS 2 与项目的构建依赖。默认 `CHAOS_SIM_ENABLE_MUJOCO=OFF`，普通工作区构建无需 SDK；启用后缺少 SDK 会在 CMake 阶段明确报错。

在工作区根目录运行（`MUJOCO_ROOT` 改为本机 SDK 路径）：

```bash
source /opt/ros/jazzy/setup.bash
colcon --log-base log-cpp-sim build --base-paths src --packages-select chaos_sim \
  --build-base build-cpp-sim --install-base install-cpp-sim \
  --cmake-args -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
               -DCHAOS_SIM_ENABLE_MUJOCO=ON -DMUJOCO_ROOT="$HOME/mujoco"
source install-cpp-sim/local_setup.bash
```

本机验证使用 MuJoCo 3.15.0，SDK 为 `/home/yunze/mujoco`。C++ 核心直接链接已有 SDK，不依赖 GLFW 开发头文件，也不安装 Python 学习框架。

## 显式准备模型

模型文件保存在缓存，源码只跟踪 `src/chaos_sim/config/models.lock.json`。清单固定仓库 commit、文件大小和 Git blob SHA-1（含 Git blob header）。下载工具保留两份上游 `LICENSE`，先校验已存在文件，匹配则复用；校验失败会报出路径，由用户明确移除后重新获取。启动过程不联网。

```bash
ros2 run chaos_sim fetch_models.py --robots go2,t2
# 自定义缓存；--verify-only 完全离线校验
ros2 run chaos_sim fetch_models.py --output /path/to/models --verify-only
```

默认缓存为 `~/.cache/chaos/mujoco`。入口分别为 `go2/unitree_robots/go2/scene.xml` 和 `t2/robots/T2/T2_31dof/T2_31dof.xml`。固定来源：

- [Unitree unitree_mujoco](https://github.com/unitreerobotics/unitree_mujoco/tree/1eb6642e3f3fdfb7fb13a9794fd6a2dd93ea0e7d)，commit `1eb6642e3f3fdfb7fb13a9794fd6a2dd93ea0e7d`，BSD 3-Clause。
- [Booster booster_assets](https://github.com/BoosterRobotics/booster_assets/tree/12a97516f57469078707afb587a5f84c1cb110d1)，commit `12a97516f57469078707afb587a5f84c1cb110d1`，BSD 3-Clause。

本机已有经过校验的缓存可直接复用，避免重复下载：

```bash
python3 src/chaos_sim/tools/fetch_models.py --verify-only \
  --output work/multi-model-team/simplify-20261031/model-cache
ros2 launch chaos_sim simulator.launch.py robots:=go2,t2 \
  model_root:="$PWD/work/multi-model-team/simplify-20261031/model-cache"
```

## 启动与 ROS 接口

```bash
# 默认两个世界，headless
ros2 launch chaos_sim simulator.launch.py
# 单独运行 T2、自定义缓存
ros2 launch chaos_sim simulator.launch.py robots:=t2 model_root:=/path/to/models
```

默认命名空间为 `/sim/go2`、`/sim/t2`，与真机话题区分。以下 `<ns>` 对应其中一个：

- `<ns>/joint_targets`：`sensor_msgs/msg/JointState`，QoS depth 1，只使用 `name` 和 `position`，支持部分关节更新。
- `<ns>/joint_states`：实际关节位置、速度、执行器力矩，默认 50 Hz。
- `<ns>/odom`：`nav_msgs/msg/Odometry`，默认 50 Hz。父 frame 为 `<robot>/odom`，子 frame 为 `<robot>/base_link`；twist 采用机器人本体坐标系。
- `<ns>/diagnostics`：`DiagnosticArray`，包含 `sim_time`、`paused`、接受/拒绝/超时计数、模型/arena 字节数与丢弃的调度 tick 数。
- `<ns>/reset`：`std_srvs/srv/Trigger`，恢复官方初态，清除速度、外力及旧目标。
- `/chaos_sim/pause`：`std_srvs/srv/SetBool`，暂停或恢复全部世界。

消息时间戳使用 ROS 节点时间，物理时间在诊断 `sim_time` 中给出；不发布全局 `/clock`。关节名称可从 `joint_states.name` 读取。非有限值、未知/重复关节、长度不符、越界目标，以及非空 velocity/effort 均整体拒绝；拒绝不会覆盖原目标。

```bash
ros2 topic echo /sim/go2/joint_states --once
ros2 service call /chaos_sim/pause std_srvs/srv/SetBool '{data: true}'
ros2 service call /sim/go2/reset std_srvs/srv/Trigger '{}'
ros2 service call /chaos_sim/pause std_srvs/srv/SetBool '{data: false}'
```

每条有效目标在默认 0.5 秒无后续命令时执行一次当前位置保持。暂停立即清除旧目标，暂停期间拒绝新目标，恢复保持当前位置；超时和调度使用 steady clock。每个模型只分配一次 `mjModel/mjData`，消息和状态缓冲启动预分配。物理 tick、订阅、服务与发布在同一线程运行，按模型 1/2 ms 步长推进；单次回调最多补 8 个基础 tick，落后则记入诊断并丢弃过期调度，避免无界补步。

独立 `simulator` 在加载模型前禁用 MuJoCo 的[编译资产缓存](https://mujoco.readthedocs.io/en/stable/APIreference/APIfunctions.html#asset-cache)：模型只在启动时编译一次，无需为重复编译保留网格。`World` 库不修改宿主的全局缓存配置，也不调整系统分配器。显示模型在单独的查看器进程中加载。

直接运行 `ros2 run chaos_sim simulator` 时，通过 ROS 参数文件传入字符串 `worlds_json`（JSON 数组 1–2 项，每项 `name/namespace/model_path/kp/kd`）；launch 已生成该参数。节点启动参数另含 `discard_visual=true`、`publish_rate_hz=50`、`command_timeout_s=0.5`。发布频率范围为 `[0.1,1000]` Hz，越界或非有限值在模型加载前拒绝。本轮参数在启动时生效。

## 可选查看器

查看器订阅上述状态并更新模型 qpos，调用 `mj_forward` 渲染；物理始终由 headless 节点推进。查看器也在模型编译前禁用资产缓存，退出时等待本次创建的渲染线程结束，再释放 ROS 节点。查看器进程退出不影响仿真，关闭仿真使用 Ctrl-C，查看器单独关闭窗口或 Ctrl-C。

先 source ROS 与工作区，再用装有 `mujoco`、`numpy` 且能导入 `rclpy` 的 Python 启动。Python 路径可显式选择：

```bash
source /opt/ros/jazzy/setup.bash
source install-cpp-sim/local_setup.bash
VIEWER_PYTHON=/path/to/venv/bin/python
"$VIEWER_PYTHON" src/chaos_sim/tools/viewer.py --robot go2 \
  --model-root "$HOME/.cache/chaos/mujoco"
```

本机现有查看器环境为 `/home/yunze/mujoco_playground/.venv/bin/python`，它需要补充系统 Python 包路径以导入 ROS 的 `yaml` 依赖：

```bash
PYTHONPATH="/usr/lib/python3/dist-packages:$PYTHONPATH" \
  /home/yunze/mujoco_playground/.venv/bin/python src/chaos_sim/tools/viewer.py \
  --robot go2 --model-root "$PWD/work/multi-model-team/simplify-20261031/model-cache"
```

查看器需要可用的图形会话；`--robot t2` 可切换模型，`--namespace` 可覆盖订阅前缀。两进程需使用相同 ROS domain。关节或 frame 不匹配、15 秒收不到完整状态时会报出明确错误。

## 验证

```bash
source /opt/ros/jazzy/setup.bash
source install-cpp-sim/local_setup.bash
python3 scripts/mujoco_smoke_test.py --model-root /path/to/models
colcon --log-base log-cpp-sim-test test --build-base build-cpp-sim \
  --install-base install-cpp-sim --packages-select chaos_sim --return-code-on-test-failure
colcon test-result --test-result-base build-cpp-sim --verbose
```

smoke 固定 localhost 发现范围，默认独立 ROS domain 197，只启动仿真进程。它加载两个官方模型，检查物理时间增长、ROS joint/odom、实际关节位移与力矩、目标拒绝、steady 超时、不经过 reset 的暂停/恢复、独立重置及 SIGINT 正常退出；预热后连续采样线程数与 RSS，并输出实际采样区间。资源检查采用有界增长阈值，不据此声明实时性。

本机已验证两个查看器真实开窗及正常退出。双模型 headless 采样约 106 MiB、14 个进程线程；完整视觉查看器单独占用 Go2 约 484 MiB、T2 约 1170 MiB（包含图形资源）。模型编译峰值、图形后端和分配器会影响内存；具体测量与回归范围见 [验证记录](validation.md)。
