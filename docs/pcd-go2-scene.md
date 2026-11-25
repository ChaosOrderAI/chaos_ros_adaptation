# 0.pcd 庭院场景与 Go2 仿真

本机场景来自 `/home/yunze/0.pcd`，共 20,010,726 个带 RGB 的点。
使用官方 Unitree Go2 模型和项目原生 `chaos_sim` 节点，物理包含重力、
地面/障碍物接触和 12 关节 PD 控制。

## 启动

在工作区执行：

```bash
bash scripts/run_pcd_go2.sh
```

脚本同时启动物理进程、Go2 行走控制器与 GPU 点云查看器，默认 ROS Domain **198**，仅本机发现。
默认使用全部 **20,010,726 个原始点**，保留 RGB 颜色，无显示降采样。
本机通过 PRIME 选择 NVIDIA RTX 5060 Laptop GPU；启动日志打印实际 OpenGL 渲染器，
软件渲染会明确报错。

查看器对准南侧步道上的 Go2。左键拖动旋转、右键/中键拖动平移、滚轮缩放；
按 **F** 切换跟随机器狗，按 **O** 查看全景，按 **+ / −** 调整显示面片覆盖大小。
按 **G** 在原始圆片显示与高斯表面融合之间切换；窗口标题显示当前模式。
窗口获得焦点后，按住 **W/S** 前进/后退、**A/D** 左转/右转、**Q/E** 左移/右移；
松开按键停止，**空格** 停止，**R** 复位到出生点。窗口失焦也发送停止指令。
速度指令上限为前进 0.45 m/s、后退 0.30 m/s、横移 0.20 m/s、转向 0.60 rad/s。

## 多机

多机共用同一份碰撞场景，而不是每台机器人各加载一份地图：

```bash
bash scripts/run_pcd_go2.sh --robots 3
```

`--robots` 取 2 到 8。第一台仍叫 `go2`，命名空间 `/sim/go2`；后面是 `go2_2`、
`go2_3`，命名空间挂在入口下 `/sim/go2/go2_2`……沿路径在出生点后方每隔 1 m 排开。
每台有自己的 `cmd_vel` 和行走控制器。默认按键只控制第一台；
按 **1–9** 只控制对应的一台，按 **0** 恢复全部。**R** 只复位当前选择。
物理仍然是一个 MuJoCo 模型，机器人之间会互相碰撞。

```text
                 ┌────────────────────────┐
                 │  chaos_sim simulator   │  1 个进程，1 个 MuJoCo 模型（场景+多台 Go2）
                 │  /sim/go2/...          │  每台机器人一组 joint_states / odom / diagnostics / cmd_vel / reset
                 └────┬───────────┬───────┘
                      │           │
        ┌─────────────┘           └─────────────┐
        ▼                                       ▼
  ┌─────────────┐                       ┌─────────────┐
  │ locomotion  │  …每个机器人一份…      │ locomotion  │   各跑一份 ONNX 策略，
  │ go2         │                       │ go2_2       │   把 cmd_vel 变成 joint_targets
  └──────┬──────┘                       └──────┬──────┘
         │                                     │
         └──────────────┬──────────────────────┘
                        ▼
               ┌──────────────────┐
               │ pcd_gpu_viewer   │  1 个进程，订阅全部命名空间，
               │ (GPU 点云 + 键盘) │  默认只控制 go2，1-9/0 切换选择
               └──────────────────┘
```

可选参数：

- `--layout line|ring|grid`：`line` 沿 -X 单列（默认，适合窄道），`ring` 绕出生点一圈，`grid` 三列横排。
- `--spacing 米`：相邻机器人距离，默认 1.0，范围 0.5–5。
- `--base-name 名字`：第一台名字，其他自动加 `_2`、`_3`（默认 `go2`）。
- `--namespaces nested|flat`：`nested` 是 `/sim/go2` + `/sim/go2/go2_2`（默认），`flat` 是 `/sim/go2` + `/sim/go2_2`。
- `--policy 路径` 或 `--policy 名字=路径`：给全部或指定机器人换 ONNX 策略。

多机验证：

```bash
python3 scripts/check_multi_locomotion.py \
  --manifest generated/shaoxing_las_go2/robots.json \
  --output generated/shaoxing_las_go2/multi_locomotion_validation.json
```

会轮流单独驱动每台机器人、同时驱动全部，并检查任意两台不重叠；结束后复位全部。
标题显示帧率、点数、ROS 状态和键盘速度指令。查看器发布速度指令并订阅实际物理状态，
不自行推进机器人位置。关闭窗口或在启动终端按 Ctrl-C 会结束本次启动的三个进程。
同一生成目录不能重复启动。没有图形会话时可以运行：

```bash
bash scripts/run_pcd_go2.sh --headless
```

日志为 `generated/pcd0_go2/simulator.log`、`locomotion.log`、`viewer.log`；ROS 日志在其 `ros-log/` 目录。
`VIEWER_PYTHON` 可覆盖本机默认 `/home/yunze/mujoco_playground/.venv/bin/python`；
`PCD_GO2_SCENE_DIR` 可选择另一份准备好的场景。
`bash scripts/run_pcd_go2.sh --mesh` 可打开早期的低精度网格代理查看器。
旧网格查看器没有键盘速度控制，可通过 ROS 发布速度。
`--no-locomotion` 只运行原有 PD 站立模式，适用于直接关节动作测试。

没有原始照片或视频时，可以从现有点云开启高斯表面融合：

```bash
bash scripts/run_pcd_go2.sh --gaussian
```

这是基于测量点及其法线的 **Gaussian 加权表面渲染**，没有进行照片训练，
不称为训练后的 3DGS 重建。它复用全部 20,010,726 点和 RGB 数据，
先找到最近点云表面，再在线性深度相近的点之间按高斯权重融合颜色。
支持范围为现有自适应半径的 1.25 倍，深度带限制为 8–35 mm；
原始颜色按近似 sRGB 转线性空间后融合。最终按不透明表面合成，避免把天空混入植被造成亮圈。
点云与机器人仍严格按深度遮挡，机器人运动和碰撞模型不变。

这一模式主要减少平面上的硬色块和小缝隙；缺失扫描区域仍然缺失，
稀疏植被近看仍能看出面片，也不补造真实纹理。
它需要两次点绘制及一次全屏合成，帧率低于原版；按 G 可随时比较或切回。
默认启动仍使用原版圆片模式。1600×1000 下新增深度与颜色缓冲约 19 MB，
全部点的位置、颜色、法线及半径缓冲继续复用。

高斯模式在场景目录已有 `gaussians.npy` 时改用三维高斯细节层次，而不是把每个点都画两遍。
`prepare_3d_gaussians.py` 把点云分成 8 m 的块，每块里每个 0.16 m 体素拟合一个
各向异性高斯（均值、颜色和协方差都来自该体素里的实测点，没有照片训练）。
近处的块仍绘制原始点，远处的块绘制这些高斯；屏幕上的椭圆是协方差的 EWA 投影。
启动脚本会在需要时生成该缓存。按 G 仍可切回圆片。

实现见 `scripts/gaussian_surface_renderer.py`，方法参考
[Surface Splatting](https://cgl.ethz.ch/Downloads/Publications/Papers/2001/p_Zwi01a.pdf)
的表面可见性筛选及加权颜色归一化；不是完整的 EWA 或学习型 3DGS 实现。

行走控制需要 Python 3.12 的 ONNX Runtime。本机已安装到生成目录，首次在其他环境运行时执行：

```bash
python3 -m pip install --target generated/pcd0_go2/python --no-deps onnxruntime==1.23.2
```

## 行走控制

Go2 使用仓库内的 [GS-Playground 预训练策略](../src/chaos_sim/config/policies/go2/README.md)，
通过机体速度、姿态和 12 关节反馈生成关节目标，再由 MuJoCo 的电机力矩、重力和接触产生运动。
策略在 CPU 单线程运行，GPU 继续负责全部点云显示。物理频率 500 Hz，反馈 100 Hz，
策略 50 Hz；行走模式使用 PD `kp=50, kd=0.5`。
控制器还跟踪期望航向并对角速度做滤波反馈，改善原地转向响应和直行偏航；
航向修正有明确限幅，静止时归零。
横移指令经过死区补偿与变化率限制；实际移动速度受策略和接触地形影响。

`/sim/go2/cmd_vel` 接收 `geometry_msgs/Twist`，使用机体坐标系的
`linear.x`、`linear.y` 和 `angular.z`。指令超过 0.3 秒未更新后减速到零，
零速度时策略继续保持平衡。超过 0.1 秒未收到完整反馈则停止发送关节目标。
摔倒后禁用运动，按 R 复位恢复。`/sim/go2/locomotion/status` 提供控制器 JSON 状态。

键盘只在操作时持续发布速度，空闲时不争用其他 ROS 控制端。一次只使用一个速度控制端。
当前没有自动导航或避障；出生点沿步道方向，前后移动的空间更大，横移时注意步道边缘。

启动脚本生成 `simulator_locomotion.yaml`，保留原来的站立参数和场景碰撞文件。
以下检查在同一 ROS 环境中运行，会前进、停车、转向、倒退，最后复位；检查时不要操作键盘：

```bash
python3 scripts/check_go2_locomotion.py --output generated/pcd0_go2/locomotion_validation.json
```

## 场景处理

原文件保持不变。GPU 显示直接使用全部有限点的位置与原始颜色，保持原文件顺序。
每个点绘制为贴合局部表面方向的圆形面片，使用 4× MSAA 抗锯齿；
面片大小根据实际邻点距离调整，减少地面和墙面的小间隙。
与机器狗共用相机和深度缓冲，保证前后遮挡一致。
点云本身缺失的数据仍然缺失，这不是新增的纹理网格或 3D Gaussian 训练结果。

全部点的位置/颜色缓冲约 320 MB，法向约 240 MB，自适应半径约 80 MB，
共约 640 MB；此外还有机器人、帧缓冲和驱动的显存开销。
每次启动检查缓存的原文件、坐标原点及参数；更改场景后会重新生成对应缓存。

出生点选在南侧步道：原 PCD 坐标 `(18, 33, 15.991900444)` m。
局部坐标为 `PCD 坐标 - 出生点坐标`。PCD 中的 `VIEWPOINT` 单独记录，
不叠加其地理坐标偏移。机器人沿局部 +X 方向。

碰撞代理使用 0.6 m 网格：出生地面高度 ±0.18 m 内的点生成中位数高度的地面砖；
其他高度使用占据体素方块。缺失扫描区域保持空洞。
这种代理适合场景交互与初步接触验证，台阶、细杆和植被接触精度受体素尺寸限制。

生成物位于 Git 忽略的 `generated/pcd0_go2/`：

- `go2_physics.xml`：用于原生物理节点的机器人与碰撞场景。
- `cloud_gpu.npy`、`cloud_gpu.json`：全部点的 GPU 数据与来源、点数、采样参数。
- `cloud_gpu_normals.npy`、`cloud_gpu_radii.npy`：逐点表面法向和显示半径。
- `cloud_gpu_normals.json`：表面估计参数及点缓冲版本关联。
- `go2_scene.xml`、`scene_meshes/`：早期彩色网格代理，供 `--mesh` 选择。
- `scene_local.ply`：用于初始碰撞代理准备的降采样点云。
- `scene_metadata.json`：来源、参数、点数、坐标变换和场景边界。
- `simulator.yaml`：物理节点参数。

重新生成需要系统 Python 的 NumPy 和已缓存的官方 Go2 模型：

```bash
python3 scripts/prepare_pcd_go2.py /home/yunze/0.pcd \
  --model-root ~/.cache/chaos/mujoco \
  --output generated/pcd0_go2 --spawn-xy 18 33
python3 scripts/prepare_pcd_gpu.py --scene-dir generated/pcd0_go2 --voxel 0
python3 scripts/prepare_pcd_normals.py --scene-dir generated/pcd0_go2
```

GPU 准备工具需要系统 NumPy/SciPy；查看器使用 MuJoCo、GLFW、PyOpenGL、NumPy 和 ROS。
`--voxel 0` 使用全部点；`--voxel .01` 可生成 1 cm 体素版本（本场景为 14,301,707 点）。
启动脚本默认准备并使用全部点，不会按帧率自动降低点数。

生成 XML 包含本机模型和网格的绝对路径。移动工作区后应重新生成。
物理模块构建方式见 [MuJoCo 说明](mujoco.md)，可用 `build-sim` / `install-sim` 变体；目录清理后按 [MuJoCo 说明](mujoco.md) 重建。

## 运行验证和关节动作

使用 `bash scripts/run_pcd_go2.sh --no-locomotion` 启动后，在另一个终端执行：

```bash
source /opt/ros/jazzy/setup.bash
source install-sim/local_setup.bash
export ROS_DOMAIN_ID=198 ROS_AUTOMATIC_DISCOVERY_RANGE=LOCALHOST
unset ROS_LOCALHOST_ONLY
export ROS_LOG_DIR="$PWD/generated/pcd0_go2/ros-log"
python3 scripts/check_pcd_go2.py --output generated/pcd0_go2/validation.json
```

检查会复位 Go2，测量站立姿态与约 50 Hz 的关节反馈，然后执行一次轻微屈腿动作，
验证实际关节位移、控制受理和物理时间推进，最后复位到站立。
结果写入 `validation.json`。

该屈腿检查直接发布关节目标，不能与行走策略同时使用。
相关关节话题和复位接口见 [MuJoCo 说明](mujoco.md)。
