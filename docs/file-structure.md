# 文件结构

> 撰写日期 2026-11-25，反映重构分支（21 包）后的结构。本文件回答"仓库里有什么、各文件干什么"；行为契约与验收状态见 [README.md](../README.md)、[architecture.md](architecture.md) 与各专题文档。标注 ★ 的是各包核心文件。

## 仓库根目录

```text
chaos_ros_adaptation/
├── README.md                 # 文档枢纽：构建命令、依赖清单、能力边界、docs 索引
├── .gitignore                # build-*/install-*/log-* 多前缀通配 + generated/ + AI 工具本地状态
├── src/                      # 21 个 ROS 包：chaos_* 生产包 16 + Go2 SLAM 链路 5
├── scripts/                  # 38 个脚本：冒烟测试 / 场景工具链 / 构建 / 车队编排
├── docs/                     # 33 篇文档（含 file-structure.md）
├── benchmarks/               # Python→C++ 迁移性能验收 + 下游 CMake 消费方检查
├── deploy/                   # 机载 systemd 部署契约
├── hardware/                 # 自研电气设计（Orin NX 载板 + STM32H7 同步板）
├── hardware-ref/             # vendored 第三方参考设计（仅阅读，GPL 者不拷码）
├── drivers/                  # 独立 colcon 工作空间：vendored OrbbecSDK_ROS2 v2.10.6
├── generated/                # ~8.6 GB gitignored MuJoCo Go2 场景数据集（4 个，仅存本机）
├── .idea/  .vscode/          # 编辑器配置
├── build/  install/          # 默认变体 colcon 产物（gitignored，可重建）
└── .claude/                  # Claude Code 会话本地状态（gitignored）
```

2026-11 重构：`chaos_camera_trigger` 并入 `chaos_time_sync`，`chaos_thermal_fusion` 并入 `chaos_sensors`，`chaos_bt` 并入 `chaos_sdk`（gated 目标 `chaos_sdk_bt`），`go2_perception` 并入 `go2_slam_backend`。gitignored 过程目录（work/、.claude-flow/、log/、旧构建变体）已清理。

---

## 一、机载智能体层

### src/chaos_agent/ — LLM 规划层

```text
src/chaos_agent/
├── include/chaos_agent/{agent_loop,llama_client}.hpp
├── src/{agent_loop,agent_main,llama_client}.cpp   # ★ agent_loop.cpp：1s 切片、在飞受限再决策、team_status
├── test/agent_loop_tests.cpp
└── CMakeLists.txt  package.xml
```

### src/chaos_skill/ — 技能层（唯一汇聚点）

```text
src/chaos_skill/
├── include/chaos_skill/{vocabulary,capability,executor,intent_ingress,server_ports}.hpp
├── src/{vocabulary,capability,executor,intent_ingress,server_ports,skill_server_main}.cpp
│   └── …                            # ★ executor.cpp：租约/deadline→TTL/hub 记账/7 个 run_* 分派
├── config/demo_vocabulary.json      # 示例词表：4 hub、7 token
├── test/{vocabulary_tests,skill_layer_tests}.cpp
└── CMakeLists.txt  package.xml
```

---

## 二、通信层

### src/chaos_mqtt/ — MQTT 传输 + 命令网关

```text
src/chaos_mqtt/
├── include/chaos_mqtt/{gateway_node,protocol,action_observer,adapter_config,error}.hpp
├── src/{gateway_main,gateway_node,protocol,adapter_config,py_bindings}.cpp
│   └── …                            # ★ gateway_node.cpp：mqtt/command 门面→ROS 服务/速度/skill_intent/OTA
├── test/                            # protocol/gateway/adapter_config/action_observation gtest + 1 pytest
├── transport/                       # 原样导入的 Rust 实现（crate 名 mqtt_ros2_adapter，MIT）
│   ├── src/{main,adapter,mqtt,envelope,forwarding,routes,ros2,replay,config}.rs
│   ├── config.*.example.json  deploy/  scripts/local_mqtt.py
│   └── SOURCE_PROVENANCE.md         # 导入基准 fa36988 + 逐文件 SHA-256
├── chaos_mqtt/{config.py,__init__.py}
├── README.md  CMakeLists.txt  package.xml
└── cmake/cargo_runner.cmake.in
```

### src/chaos_sdk/ — 原生 C++ 客户端 SDK + BT 节点库

```text
src/chaos_sdk/
├── include/chaos_sdk/{robot,action_task,action_slots,sdk_core}.hpp
├── include/chaos_sdk/bt/nodes.hpp   # Navigate/Takeoff/Land BT 节点（namespace chaos_sdk::bt）
├── src/{robot,sdk_core}.cpp         # ★ robot.cpp 1636 行：租约缓存状态机、有界 pending、动作槽
├── src/feedback_dispatcher.{hpp,cpp}    # 进程级有界反馈工作池（内部，不入安装）
├── src/bt/{nodes,register_nodes}.cpp    # BT 节点库实现（CHAOS_SDK_WITH_BT 门控，默认 ON）
├── src/{mock_asset_main,py_bindings}.cpp
├── test/                            # sdk_core/robot_native/feedback_dispatcher/bt_nodes gtest + test_client.py
├── chaos_sdk/client.py  examples/mock_client.py
└── CMakeLists.txt  package.xml
```

### src/chaos_core/ — ROS-free 原语库（全框架地基）

```text
src/chaos_core/include/chaos_core/{errors,clock,time,format,ota_wire,action_result}.hpp
（纯头文件库，无 src、无 ROS 依赖，被所有 C++ 包单向依赖）
```

### src/chaos_interfaces/ — 消息契约（10 msg + 4 srv + 4 action）

```text
src/chaos_interfaces/
├── msg/    RobotState  VelocityCommand  DriverVelocityCommand(内部)  CommandResult
│           ControlTrace  Announcement  BoardTrigger  SkillToken  SkillIntent  SkillEvent
├── srv/    ControlLease  GetDescription  Stop  SkillRequest
├── action/ NavigatePose  Flight  SoftwareUpdate  ExecuteSkill
└── CMakeLists.txt  package.xml
```

---

## 三、运行时与硬件抽象

### src/chaos_runtime/ — 设备最后一跳

```text
src/chaos_runtime/
├── include/chaos_runtime/
│   ├── adapter.hpp  coordinator.hpp  runtime_node.hpp      # ★ 契约与控制核心声明
│   ├── asset_validation.hpp  asset_config.hpp  control.hpp  ota.hpp  trace.hpp
│   ├── observation.hpp  observation_types.hpp
│   ├── driver_deadline.hpp       # ★ CLOCK_MONOTONIC 时钟域身份 + 永不放大截止时间
│   ├── go2_driver_core.hpp  go2/{go2_driver_node,sdk_backend}.hpp
│   ├── humanoid/{backend,booster_backend,driver_core,driver_node,options,process_guard,ros_adapter,telemetry_gate}.hpp
│   ├── ros_adapters.hpp  ros_stamp.hpp  runtime_shutdown.hpp  adapters/mock_adapter.hpp
├── src/                          # coordinator.cpp 1139 行 / observation.cpp 1667 行 + go2/humanoid/adapters
├── test/                         # 16 个 gtest（含父子进程关停 harness）
└── CMakeLists.txt  package.xml   # unitree_sdk2 / Booster SDK 为 opt-in 门控
```

### src/chaos_sensors/ — 传感器流、健康监测与热成像融合

```text
src/chaos_sensors/
├── include/chaos_sensors/{sensor_config,mock_sensor_node,sensor_monitor_node,health,stream_qos,ros_stamp}.hpp
├── src/{sensor_config,mock_sensor_node,mock_sensor_main,sensor_monitor_node,sensor_monitor_main,health}.cpp
├── src/{at600_split_node,thermal_fusion_node,thermal_register_calib}.cpp   # 并入自 chaos_thermal_fusion
├── config/at600.yaml  launch/thermal_fusion.launch.py
├── test/{test_health_cpp,test_sensor_clock_cpp}.cpp
└── CMakeLists.txt  package.xml
```

### src/chaos_time_sync/ — 同步板客户端与触发帧配对

```text
src/chaos_time_sync/
├── src/time_sync_node.cpp          # InfiniteSense 同步板 UDP JSON 协议客户端（类 PTP 校时/触发/IMU/诊断）
├── src/trigger_pair_node.cpp       # 外触发相机帧 ↔ 触发沿配对，按板卡时间重打戳（并入自 chaos_camera_trigger）
├── config/{time_sync,trigger_pair}.yaml
├── launch/{time_sync,trigger_pair}.launch.py
├── README.md                       # 板卡协议、接线、配对参数表
└── CMakeLists.txt  package.xml
```

---

## 四、配置 / 启动 / 数据

### src/chaos_assets/ — 配置唯一真源（不依赖 rclcpp）

```text
src/chaos_assets/
├── include/chaos_assets/{inventory,sensors,collection,description,paths,process,errors,yaml,xml,json,text,pyfmt,cli}.hpp
├── src/{inventory,sensors,collection,description,paths,process,yaml,xml,json}.cpp  # ★ inventory.cpp
├── asset_config_main.cpp  py_bindings.cpp
├── config/                         # demo/go2/humanoid 清单 + 机载实例 + 传感器五家示例 + mqtt/ota/采集/描述
├── models/{booster_t2,demo_ota,demo_ugv,dji_m3td,lingao_vehicle,unitree_go2}/v1/model.yaml
├── chaos_assets/                   # Python 再导出层
├── tools/xacro_expand.py  test/（gtest + pytest + baseline 黄金基线）
└── CMakeLists.txt  package.xml
```

### src/chaos_bringup/ — 启动编排

```text
src/chaos_bringup/
├── include/chaos_bringup/{planner,components,driver_registry,runtime}.hpp
├── src/{planner,components,runtime,plan_main,py_bindings}.cpp   # ★ planner.cpp：冻结 LaunchPlan + preflight
├── chaos_bringup/{launching,planning,nodes,drivers,sensors,description,collection,skills,time_sync}.py
├── launch/{system,edge}.launch.py
├── test/                           # gtest + launch py 测试
└── CMakeLists.txt  package.xml
```

### src/chaos_data/ — 机载采集与训练导出

```text
src/chaos_data/
├── include/chaos_data/{controller,recorder_supervisor,recorder_control,control_channel,session,timing,audit,export,file_io,npy,ros_message}.hpp
├── src/                            # ★ controller.cpp 时钟门控 + recorder 双进程(FD3) + export 管线 + audit
├── config/{drone,vehicle}_velocity.example.yaml
├── test/  chaos_data/rosbag_compat.py
└── CMakeLists.txt  package.xml     # 可执行：collector / chaos_data_recorder_worker / data_audit / data_export
```

---

## 五、外围与可选包

```text
src/chaos_sim/                     # 可选 MuJoCo 关节 PD 仿真（CHAOS_SIM_ENABLE_MUJOCO=ON）
├── include/chaos_sim/{world,scene}.hpp  src/{world,scene,scene_import,simulator_node}.cpp
├── config/{models.lock.json,policies/go2/policy.onnx,scenes/demo.ply}
├── launch/simulator.launch.py  tools/{fetch_models,viewer}.py  test/

src/chaos_benchmarks/              # 纯领域微基准（非生产入口）
└── micro_benchmark.cpp
```

---

## 六、Go2 SLAM 导航链路（vendored 上游 + 本地包）

```text
src/fast_livo2/                    # GPL-2.0 vendored（hku-mars 0d2c034）；UPSTREAM.txt 纪律
├── src/{LIVMapper,main,camera,frame,IMU_Processing,preprocess,vio,vision,visual_point,voxel_map}.cpp
├── include/utils/（自写 se3/camera/vision，GPL 边界）  config/  launch/  test/  UPSTREAM.txt

src/scan_planner/                  # Apache-2.0 vendored（SCAN-Planner-Ros2 103bce4，六子包）
├── UPSTREAM.txt
├── plan_env/  path_searching/  bspline_opt/  traj_utils/  scan_planner_msgs/
└── plan_manage/（FSM + 控制器 + 仿真辅助节点 + scripts）

src/go2_slam_backend/              # 本地：关键帧位姿图 + ICP 回环 → map→odom；含近场障碍云
├── include/go2_slam_backend/se3_graph.hpp
├── src/{se3_graph,slam_backend_node,obstacle_cloud_node}.cpp   # obstacle 并入自 go2_perception
├── config/{slam_backend,obstacle}.yaml  test/test_se3_graph.cpp

src/go2_terrain_planner/           # 本地：2.5D 高程图楼梯路由
└── src/{elevation_planner,terrain_planner_node}.cpp  config/  test/

src/go2_bringup/                   # Go2 整栈 launch + URDF + 实机参数
└── launch/stack.launch.py  urdf/go2_sensors.urdf  config/scan_planner.yaml  rviz/

src/go2_v1_bridge/                 # 纯 Rust：平台 v1 MQTT 设备协议 ↔ ROS 原语
└── src/{main,envelope,commands}.rs

src/livox_ros_driver2/             # MIT vendored（21445540）+ 系统 Livox-SDK2
└── src/  config/  launch_ROS2/  msg/   # 禁跑其自带 build.sh，统一 scripts/build_slam.sh
```

---

## 七、根级支撑目录

### scripts/（38 个，七类）

```text
冒烟/验收（各独占 ROS Domain）：smoke_test  go2_smoke  go2_driver_smoke  humanoid_smoke  mqtt_smoke
  transport_smoke（mTLS 全链路）  sensor_smoke  description_smoke  data_smoke  training_data_smoke  mujoco_smoke
点云→MuJoCo 场景工具链：las_to_pcd  mesh_to_pcd  pcd_reader  prepare_pcd_go2  prepare_pcd_gpu
  prepare_pcd_normals  prepare_3d_gaussians  gaussian_surface_renderer  gaussian_lod_renderer
  check_gaussian_surface  compose_multi_go2  pcd_gpu_viewer
Go2 运动链路：go2_policy  go2_locomotion  check_go2_locomotion  check_multi_locomotion  check_pcd_go2
合成数据：synth_livox（无硬件 bring-up fast_livo2）
构建/验收：build_slam.sh（★ 27 包合并工作空间）  validate_humble.sh（Humble 验收，BT 门关闭）
车队 MQTT：fleet_test_certs.sh  run_fleet_uplink.sh  run_fleet_downlink.sh
场景运行（Domain 198–201）：run_pcd_go2.sh  run_shaoxing_go2.sh  run_shaoxing_las_go2.sh  run_merged_recon_go2.sh
```

### docs/（33 篇）

```text
架构与数据流：architecture.md  data-flow.md  sensors.md（含 AT600 段）  robot-description.md  file-structure.md
C++17 迁移：cpp-migration.md  cpp-assets-migration.md  cpp-runtime-migration.md
             cpp-data-migration.md(EN)  cpp-mqtt-migration.md(EN)  cpp-mqtt-sdk-migration.md
硬件接入：unitree-go2.md  humanoid.md  orbbec-driver.md  humble.md  onboard-deployment.md
仿真场景：mujoco.md  pcd-go2-scene.md  shaoxing-go2-scene.md  shaoxing-las-go2-scene.md  merged-recon-go2-scene.md
数据与训练：data-collection.md  training-data.md
车队通信：fleet-telemetry-uplink.md  collaborative-perception-preresearch.md
机载 AI 层：agent-skill-layer.md
上游与纪律：upstream-reference.md  go2-slam-agents.md
验证与行为树：validation.md  behavior-tree.md（chaos_sdk_bt）
未提交分析（主树 3 篇）：harness-architecture-analysis.md  zkdx-pr120-architecture-analysis.md  harness-comparison-zkdx.md
```

### benchmarks/ / deploy/ / hardware/ / hardware-ref/ / drivers/

```text
benchmarks/   extract_python_baseline.py  latency_roundtrip.py  compare_runtime.py  consumer/  verify_consumer.sh
              # consumer 现链接 chaos_sdk::chaos_sdk_bt（chaos_bt 并入后的导出检查）
deploy/       chaos-edge.service  start-edge.sh  edge.env.example  robots/  time-sync/
hardware/     orin-nx-carrier/（自研载板，connections.md 单源可 --check）  stm32h7-sync-board/（核心 53 项主机断言已过；HAL 层需 arm-none-eabi-gcc）
hardware-ref/ OctoSense SyncBoard（MIT，拷入）；gps-timestamper（GPL-3.0，仅阅读）
drivers/      orbbec.repos  build-orbbec.sh（独立工作空间只编 orbbec_camera）
```

---

## 依赖方向速查

```text
chaos_core (无依赖)
  ↑
chaos_interfaces ←  所有包共享
  ↑
chaos_assets (纯配置, 不依赖 rclcpp) → chaos_bringup → 启动 chaos_mqtt / runtime / sensors / skill / data / time_sync
chaos_sdk (含可选 chaos_sdk_bt) ← chaos_skill / 外部调用方；chaos_mqtt(gateway) ← 平台命令下行
chaos_runtime ← 技能层与 SDK 的控制面终点；适配器直发(Twist)或经独立驱动进程(SDK)
Go2 SLAM 链路内部单向：livox → fast_livo2 → slam_backend(含 obstacle_cloud) → terrain_planner → scan_planner → /cmd_vel → chaos_runtime go2_driver
```
