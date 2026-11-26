# MuJoCo 仿真

本机大型点云导入实例：[0.pcd 庭院与 Go2 仿真](pcd-go2-scene.md)，包含场景准备、一键启动和关节动作验证。

`chaos_sim` 是可选 C++17 ROS 2 包。一个 `SingleThreadedExecutor` 进程可运行 1–32 个 Go2、T2 独立物理世界，支持同型号多实例和混合编队，包含真实重力、碰撞、关节 PD、标准 ROS 观测。默认剔除纯视觉资源，保留碰撞与惯量；Python 查看器单独加载完整视觉模型。可选导入 PLY/PCD 点云或 3D Gaussian PLY，作为每个世界的静态几何代理，不是原生 Gaussian Splatting 渲染。不同实例不共享 `mjData`，因此没有机间碰撞或接触。

Go2 使用官方 home 关键帧，T2 使用官方 qpos0。T2 尚未接入平衡策略，PD 驱动后会倒地；浮动根保持真实动力学。首轮控制入口是命名关节目标，尚未接入统一 `chaos_sdk::Robot`、控制租约或 `motion.velocity` 行走接口，也未实现仿真相机与真机 SDK 适配。现有 mock/real 资产启动方式继续独立使用。

## 构建

需要已有 MuJoCo C SDK、ROS 2 与项目的构建依赖。默认 `CHAOS_SIM_ENABLE_MUJOCO=OFF`，普通工作区构建无需 SDK；启用后缺少 SDK 会在 CMake 阶段明确报错。

在工作区根目录运行（`MUJOCO_ROOT` 改为本机 SDK 路径）：

```bash
source /opt/ros/jazzy/setup.bash
colcon --log-base log-sim build --base-paths src --packages-select chaos_sim \
  --build-base build-sim --install-base install-sim \
  --cmake-args -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
               -DCHAOS_SIM_ENABLE_MUJOCO=ON -DMUJOCO_ROOT="$HOME/mujoco"
source install-sim/local_setup.bash
```

变体目录 `build-sim` / `install-sim` / `log-sim` 不入库，清理后用上面的命令原地重建即可。本机验证使用 MuJoCo 3.15.0，SDK 为 `/home/yunze/mujoco`。C++ 核心直接链接已有 SDK，不依赖 GLFW 开发头文件，也不安装 Python 学习框架。

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

模型缓存放在规范位置 `~/.cache/chaos/mujoco`，按 `models.lock.json` 哈希校验；清空后重新下载即可：

```bash
python3 src/chaos_sim/tools/fetch_models.py --verify-only \
  --output ~/.cache/chaos/mujoco
ros2 launch chaos_sim simulator.launch.py robots:=go2,t2 \
  model_root:="$HOME/.cache/chaos/mujoco"
```

## 启动与 ROS 接口

```bash
# 默认两个世界，headless
ros2 launch chaos_sim simulator.launch.py
# 单独运行 T2、自定义缓存
ros2 launch chaos_sim simulator.launch.py robots:=t2 model_root:=/path/to/models
# 两台 Go2 + 一台 T2；重复型号表示多实例
ros2 launch chaos_sim simulator.launch.py robots:=go2,go2,t2
# 四台 Go2
ros2 launch chaos_sim simulator.launch.py robots:=go2,go2,go2,go2
```

型号只出现一次时保留原名称，例如 `/sim/go2`；同型号重复时按列表顺序编号，例如 `robots:=go2,go2,t2` 创建 `/sim/go2_1`、`/sim/go2_2`、`/sim/t2`。最多 32 台。每个实例有独立的关节目标、状态、里程计、诊断和 reset 服务；向一台机器人发送目标或重置不会影响其他实例。所有实例仍由同一进程、同一线程调度，规模增大时应实测 CPU 和内存。

以下 `<ns>` 对应其中一个实例命名空间：

- `<ns>/joint_targets`：`sensor_msgs/msg/JointState`，QoS depth 1，只使用 `name` 和 `position`，支持部分关节更新。
- `<ns>/joint_states`：实际关节位置、速度、执行器力矩，默认 50 Hz。
- `<ns>/odom`：`nav_msgs/msg/Odometry`，默认 50 Hz。父 frame 为 `<robot>/odom`，子 frame 为 `<robot>/base_link`；twist 采用机器人本体坐标系。
- `<ns>/diagnostics`：`DiagnosticArray`，包含 `sim_time`、`paused`、接受/拒绝/超时计数、模型/arena 字节数、丢弃的调度 tick 数，以及导入场景的 `scene_points`、`scene_geoms`。
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

直接运行 `ros2 run chaos_sim simulator` 时，通过 ROS 参数文件传入字符串 `worlds_json`（JSON 数组 1–32 项，每项 `name/namespace/model_path/kp/kd`，名称和绝对命名空间必须唯一）；launch 已生成该参数。导入场景时，launch 还会为每个实例写入 `scene_path`、`scene_voxel_size`、`scene_point_radius`、`scene_max_geoms` 和 `scene_collision`。节点启动参数另含 `discard_visual=true`、`publish_rate_hz=50`、`command_timeout_s=0.5`。发布频率范围为 `[0.1,1000]` Hz，越界或非有限值在模型加载前拒绝。本轮参数在启动时生效。

## 点云与 3D 高斯场景

导入结果是 MuJoCo 静态几何代理，不是原生 Gaussian Splatting 渲染，也不产生 splatting 图像。普通点云变成球体；PLY 顶点含 `scale_0/1/2` 时识别为 3D Gaussian，并变成椭球。颜色来自整数或浮点 RGB、打包的 `rgb`/`rgba`，或球谐 DC 项 `f_dc_0..2`。整数通道按存储位宽归一化；浮点通道在 `[0, 1]` 内直接使用，大于 1 时按 0–255 解释。ASCII 打包颜色按十进制整数解包，二进制 `F32` 仍按 PCL 位模式；`rgba` 的高字节是透明度。可选 `rot_0..3` 按文件顺序归一化后作为 MuJoCo `wxyz` 四元数，`opacity` 经 sigmoid 写入透明度。高斯尺度先取指数，再限制在 `[1e-4, 10]`；任一轴非有限则跳过该点。

支持 ASCII 与 binary little-endian PLY，以及 ASCII 与 binary PCD。`TYPE` 和 `DATA` 不区分大小写。`SIZE` 必须与类型匹配，二进制按 `SIZE` 字节读取。`binary_compressed` PCD 会拒绝，需先转成 ascii 或 binary。非有限坐标或高斯尺度被跳过；没有有限点、超过 `scene_max_geoms`、体素尺寸非法或文件打不开，都会在加载前失败。体素尺寸大于 0 时，每个体素只保留第一个点；刚好落在二进制除法误差上的边界点不会并进前一格。

导入前会去掉机器人模型 world 级的静态 geom、light、camera，以及没有自由关节的直接子 body，避免与官方地面或障碍重复。浮动基座必须是 world 的直接子 body。场景 geom 使用独立的 `chaos_scene` 接触默认值，不继承模型根 `<default>`。它们加到每个实例自己的世界里，因此每台机器人都有一份副本，仍然没有机间碰撞。headless 仿真会丢弃纯视觉 geom；`scene_collision:=false` 时不替换官方环境，诊断 `scene_geoms` 为 0，代理只出现在查看器里。

```bash
ros2 launch chaos_sim simulator.launch.py robots:=go2,go2 \
  model_root:="$HOME/.cache/chaos/mujoco" \
  scene:="$PWD/src/chaos_sim/config/scenes/demo.ply" \
  scene_voxel_size:=0.1 scene_point_radius:=0.12 \
  scene_max_geoms:=20000 scene_collision:=true
```

`scene` 为空则保留官方模型环境。默认体素 0.1 m、点半径 0.03 m、最多 20000 个 geom、参与碰撞。`src/chaos_sim/config/scenes/demo.ply` 是 29 个彩色点的最小示例；要连成可见地面，需要加大 `scene_point_radius`。

查看器调用已安装的 `scene_import` 写出临时 XML 再加载。该工具也可单独使用，成功时打印 JSON 统计。导出的 `meshdir` 和 `texturedir` 会写成绝对路径，临时目录中的 XML 仍能找到官方网格。

```bash
ros2 run chaos_sim scene_import ROBOT_XML SCENE.ply OUTPUT.xml \
  --voxel-size 0.1 --point-radius 0.03 --max-geoms 20000
# 只显示、不参与碰撞时追加 --no-collision
```

## 可选查看器

查看器订阅上述状态并更新模型 qpos，调用 `mj_forward` 渲染；物理始终由 headless 节点推进。查看器也在模型编译前禁用资产缓存，退出时等待本次创建的渲染线程结束，再释放 ROS 节点。查看器进程退出不影响仿真，关闭仿真使用 Ctrl-C，查看器单独关闭窗口或 Ctrl-C。

先 source ROS 与工作区，再用装有 `mujoco`、`numpy` 且能导入 `rclpy` 的 Python 启动。Python 路径可显式选择：

```bash
source /opt/ros/jazzy/setup.bash
source install-sim/local_setup.bash
VIEWER_PYTHON=/path/to/venv/bin/python
"$VIEWER_PYTHON" src/chaos_sim/tools/viewer.py --robot go2 \
  --model-root "$HOME/.cache/chaos/mujoco"
# 查看多机中的 go2_2；每个查看器进程显示一个实例
"$VIEWER_PYTHON" src/chaos_sim/tools/viewer.py --robot go2 --name go2_2 \
  --model-root "$HOME/.cache/chaos/mujoco"
```

本机现有查看器环境为 `/home/yunze/mujoco_playground/.venv/bin/python`，它需要补充系统 Python 包路径以导入 ROS 的 `yaml` 依赖：

```bash
PYTHONPATH="/usr/lib/python3/dist-packages:$PYTHONPATH" \
  /home/yunze/mujoco_playground/.venv/bin/python src/chaos_sim/tools/viewer.py \
  --robot go2 --name go2_1 \
  --model-root "$HOME/.cache/chaos/mujoco" \
  --scene "$PWD/src/chaos_sim/config/scenes/demo.ply" \
  --scene-voxel-size 0.1 --scene-point-radius 0.12
```

查看器需要可用的图形会话；`--robot t2` 可切换模型，`--name` 选择多机实例并匹配其 frame，`--namespace` 可进一步覆盖订阅前缀。`--scene` 指定 PLY/PCD。未显式给出的 `--scene-voxel-size`、`--scene-point-radius`、`--scene-max-geoms` 和 `--scene-collision` 会读取正在运行的 `chaos_simulator` 里同名实例的参数；仿真未运行时才用 0.1 m、0.03 m、20000 和参与碰撞。每个查看器只显示一个实例，不为独立物理世界提供共同的机间碰撞视图。两进程需使用相同 ROS domain；本机仿真节点固定 `ROS_AUTOMATIC_DISCOVERY_RANGE=LOCALHOST`，查看器进程也要设置该项。关节或 frame 不匹配、15 秒收不到完整状态时会报出明确错误。

## 验证

```bash
source /opt/ros/jazzy/setup.bash
source install-sim/local_setup.bash
python3 scripts/mujoco_smoke_test.py --model-root /path/to/models
colcon --log-base log-sim-test test --build-base build-sim \
  --install-base install-sim --packages-select chaos_sim --return-code-on-test-failure
colcon test-result --test-result-base build-sim --verbose
```

smoke 固定 localhost 发现范围，默认独立 ROS domain 197，只启动仿真进程。它加载两台 Go2 和一台 T2，检查多实例物理时间增长、ROS joint/odom、同型号话题与控制隔离、独立重置、实际关节位移与力矩、目标拒绝、steady 超时、不经过 reset 的暂停/恢复及 SIGINT 正常退出；预热后连续采样线程数与 RSS，并输出实际采样区间。资源检查采用有界增长阈值，不据此声明实时性。

本机已验证两个查看器真实开窗及正常退出。双模型 headless 采样约 106 MiB、14 个进程线程；完整视觉查看器单独占用 Go2 约 484 MiB、T2 约 1170 MiB（包含图形资源）。模型编译峰值、图形后端和分配器会影响内存；具体测量与回归范围见 [验证记录](validation.md)。
