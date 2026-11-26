# C++ 重构验证记录

## 2026-11-04 点云与 3D 高斯场景导入

在 ROS 2 Jazzy、MuJoCo 3.15.0 环境使用 `build-sim-scene` / `install-sim-scene` Release 构建。PLY/PCD 点云导入为球体，含 `scale_0/1/2` 的 3D Gaussian PLY 导入为椭球。二者都是 MuJoCo 静态几何代理，不是原生 Gaussian Splatting 渲染。

- `colcon test-result --test-result-base build-sim-scene` 为 **8 tests，0 errors、0 failures、0 skipped**。汇总包含 CTest 包装记录。`chaos_sim_scene` 通过，5 项 launch pytest 通过。
- 官方 Go2 `scene.xml` 与 `src/chaos_sim/config/scenes/demo.ply`（体素 0.1 m、半径 0.12 m）导出 `{"input_points":29,"geoms":29,"gaussian":false}`。XML 的 meshdir/texturedir 为绝对路径，Python 重新加载得到 `nq=19`、`ngeom=85`。
- 同一机器人与 `src/chaos_sim/test/gaussian.ply`（体素 0）导出 `{"input_points":2,"geoms":2,"gaussian":true}`，重新加载得到 `nq=19`、`ngeom=58`、椭球 2 个。
- `robots:=go2,go2` 加载演示场景后，两个实例的启动日志和诊断都是 `scene_points=29`、`scene_geoms=29`。
- `go2_1` 查看器在 `DISPLAY=:1` 打开标题为 `MuJoCo : go2 scene` 的窗口，并持续运行超过 2 分钟。查看器使用 `/home/yunze/mujoco_playground/.venv/bin/python`，需要把 `/usr/lib/python3/dist-packages` 加入 `PYTHONPATH`，并与仿真节点同样设置 `ROS_AUTOMATIC_DISCOVERY_RANGE=LOCALHOST`。

随后的代码审查确认了 15 个导入缺陷，均已修复并补进 `chaos_sim_scene`。包括默认体素并格、浮点颜色/透明度、只显示场景被编译删掉、空 `texturedir` 指错目录、PCD 宽度/大小写/`DATA` 模式、子 body 障碍、根 default 接触参数、打包颜色，以及 `--max-geoms -1`。修复后同一测试目录仍是 **8 tests，0 errors、0 failures、0 skipped**。官方 Go2 演示场景重新加载仍为 `nq=19`、`ngeom=85`。

每个实例仍有独立物理世界和自己的场景副本，没有机间碰撞。用法见 [MuJoCo 说明](mujoco.md)。

## 2026-11-04 MuJoCo 多机仿真

在 ROS 2 Jazzy、MuJoCo 3.15.0 环境使用全新 `build-sim-multi` / `install-sim-multi` Release 构建。启动列表允许重复 Go2、T2，重复型号按顺序生成独立名称和 ROS 命名空间；单型号旧名称保持不变，单进程上限为 32 个独立物理世界。

- `chaos_sim` 构建成功；`colcon test-result` 为 **6 tests，0 errors、0 failures、0 skipped**，其中包含 CTest 包装记录。4 项 launch 配置用例覆盖旧名称兼容、重复型号编号、非法/超量列表和模型缺失。
- 真实 smoke 启动 **两台 Go2 + 一台 T2**，验证三实例状态、同型号控制与 reset 隔离、目标校验、超时、暂停/恢复、实际关节位移及 SIGINT 正常退出。10 秒 20 次采样线程始终为 **14**，RSS 为 **105,820 KiB**，中位数增长 **0 KiB**；这只是本次三实例运行结果，不代表其他规模的资源上界。
- 实际 `ros2 launch ... robots:=go2,go2` 与 `--name go2_2` 查看器联合检查通过，查看器真实开窗、frame 匹配并正常退出。

多机仍采用每实例独立 `mjModel/mjData`，不模拟机间碰撞；查看器每个进程显示一台机器人。统一运动控制、平衡策略及共享物理场景不在本轮范围内。

## 2026-10-31 精简、资源优化与 MuJoCo

本轮复用 `build-humanoid` / `install-humanoid` 的 Release 安装；ROS 2 Jazzy、MuJoCo 3.15.0，厂商 SDK 后端关闭。保留工作区原有人形机器人改动，精简 runtime、SDK、传感器、MQTT 和授时转换；新增可选 `chaos_sim`。

- 9 个生产包及仿真包回归完成；`colcon test-result` 汇总 **961 条记录，0 errors、0 failures、5 skipped**。汇总包含 CTest 包装记录，不等于独立用例数。
- 双 mock、传感器、双人形 mock 三项 ROS smoke 均通过。MuJoCo smoke 使用两个官方模型，验证真实关节位移和力矩、无效目标拒绝、超时保持、不经过 reset 的暂停/恢复、独立重置和 SIGINT 退出。
- 单 mock runtime 空闲线程 **22 → 21**；首次 Action 后升到 22，随后三次 Action 保持 22，四次均成功，进程正常退出。约 8 MiB 的虚拟地址空间减少来自省去的空闲线程栈映射；RSS 约 33 MiB，未测出显著下降。
- 双模型 headless 仿真在同一主机上，禁用编译资产缓存前 RSS **973,264 KiB**，最终采样 **108,600 KiB**（约 106 MiB）；20 次采样覆盖 10 秒，线程始终 14，RSS 中位数增长 0 KiB。采样不包含独立查看器，也不包含启动编译峰值。
- 两个查看器已在实际 X11 会话开窗运行，并分别通过 SIGINT 正常退出；等待首条状态期间退出也通过。查看器禁用编译缓存后，Go2 RSS **495,996 KiB**、T2 **1,197,660 KiB**，低于禁用前的 558,724 / 1,482,520 KiB；这部分包含完整视觉模型和图形资源，不计入 headless 内存数据。
- 剔除纯视觉资源后，Go2 `mjModel` 缓冲由 44,892,116 降至 52,500 字节，T2 由 183,372,344 降至 339,192 字节。这是模型缓冲而非整个进程 RSS；碰撞、惯量、浮动根动力学保留。核心用例验证连续 200 次目标/步进/读取复用状态缓冲且没有新增 C++ `new` 分配，不据此声称 ROS/DDS 全链路零分配。

构建、模型来源与复现命令见 [MuJoCo 说明](mujoco.md)，数据与线程所有权见 [数据流转](data-flow.md)。T2 平衡与统一速度行走尚未接入，本轮未做实机验收。当时的执行日志、资源采样和 colcon 结果只留在本地 `work/multi-model-team/`（该目录已在 2026-11-25 的过程文件清理中删除），不进入版本库；`install-humanoid*` 是可重新生成的安装目录，不是源码的一部分。

## 前期 C++ 迁移验收

本次验收针对 9 个生产 ROS 包和 1 个基准包的统一 Release 普通安装产物。自研业务实现为 C++17；Python 保留 launch、薄绑定、第三方 Xacro 桥接和工具/测试脚本。导入的 Rust MQTT 传输与厂商驱动不属于这次语言迁移。

## 构建与功能验收

环境：Ubuntu 主机、x86_64、ROS 2 Jazzy、GCC 13、Python 3.12。构建使用全新的 build/install 前缀，`CHAOS_GO2_WITH_UNITREE_SDK=OFF`、`BUILD_TESTING=ON`、Release、普通安装；运行只加载 ROS 与这次安装空间。未使用旧 Python 安装产物。

- **10/10 包构建成功**。八个公开 C++ 包均可从独立工程 `find_package`、链接导出 target 并运行。
- 原生 gtest **358 项**：357 通过、1 项条件跳过；该项检查“没有 Xacro 时的错误”，因完整套件加载了真实 Xacro 而跳过，随后移除 Xacro 环境单独运行，**通过**。
- Python 绑定/launch/真实 bag 测试 **343 项全部通过**，直接检查原生实现；已删除旧 Python 业务副本及只测试旧实现的套件。
- Rust 传输 **75 项全部通过**。`colcon test-result` 显示 753 条记录，其中包含 CTest 包装结果，不能当成独立用例总数。
- 九项端到端脚本全部通过：双 mock 运行时、Go2 SDK、Go2 驱动 fake SDK、传感器、机器人描述、数据采集、训练数据导出、MQTT 网关、本机 mTLS 传输。
- MQTT 传输验收实际启动随机回环端口和临时证书，覆盖控制往返、动作终态、临时 broker 中断与重连后的终态重放；未连接远程 broker。
- 传感器 ROS 时钟冻结/恢复检查通过：冻结时不产生新 mock 图像，诊断仍按稳态时钟继续；负值与带符号时间戳归一化检查通过。
- 数据导出独立对拍 **80/80 + 14/14 通过**，基线为 Git `172b78c`，覆盖 MCAP/SQLite、图像与向量、动作标签、来源完整性和极小/高精度 FPS。最终导出 ELF SHA-256：`6c571fa307ad3b4793454ef00ba2cf5392a5aa1a66e9b3ac935f99f82337ee0d`。复审夹具及日志在协调工作目录留存，常规仓库回归另包含 16 个实际 bag CLI 用例和 3 个 journal 游标用例。
- SDK 的 **25 项原生用例**、MQTT 的 **40 项原生用例（5 套件）**及进程运行器的两项 stdin 边界检查通过 ASan、UBSan 和泄漏检查。插桩包括被测库，未将 Python 绑定测试误计为 sanitizer 验收。

SDK 与 MQTT 回归检查了本地超时、迟到回调、关闭与完成竞争、取消终态、并发目标隔离、有界 pending 和实际客户端弱引用失效。超时只结束本地观察，不表示远端已经停止。

## ROS 往返性能实测

测量源码与复现命令见 [benchmarks/README.md](../benchmarks/README.md)，完整原始采样和环境记录见 [runtime_comparison_2026-10-22.json](../benchmarks/results/runtime_comparison_2026-10-22.json)。基线从 `172b78c2f0aa98e829ead5cf498c8a1553907095` 提取，C++ 使用最终统一安装的 Release 二进制。

相同 mock 配置、QoS、租约、客户端与本机 DDS；每轮预热 50 条，收尾等待 3 秒。每个频率运行两轮，顺序为 Python/C++、C++/Python。测量期间没有并行的本任务构建或测试工作。主机有 18 个逻辑 CPU，负载记录约 2.0–2.4，不是隔离实时系统。

- **100 Hz，每轮 500 条**：C++ 两轮均收到 500/500，p95 为 **0.527 / 0.598 ms**；Python 收到 151/500、249/500，已收到回复的 p95 为 9.062 / 7.595 ms。
- **20 Hz，每轮 200 条**：C++ 两轮均收到 200/200，p95 为 **0.784 / 0.722 ms**；Python 收到 189/200、180/200，已收到回复的 p95 为 17.080 / 25.448 ms。
- **进程 CPU**：100 Hz 时 C++ 约 **1.18% 单核**，Python 43.31–55.20%；20 Hz 时 C++ 0.58–0.65%，Python 9.64–21.35%。
- **进程驻留内存**：C++ 31.8 MiB，Python 72.0–72.2 MiB，约减少 **56%**。所有收到的回复均未报告业务拒绝。

这些数值证明此主机、此 mock 工作负载下的变化。延迟包含 DDS、executor 和测量客户端调度；百分位只统计收到的回复，Python 的缺失回复不能忽略，也不能据此计算统一的“提速倍数”。CPU 窗口含预热和收尾等待，内存统计仅含 runtime 进程，不含客户端；未测硬件指令执行时间、生产吞吐极限或硬实时上界。旧的不可完整复现的纯函数对比结果已移出仓库，`chaos_benchmarks` 保留为独立微基准工具。

## 复现主要检查

从工作空间根目录，在干净终端执行；依赖见 [README](../README.md)。测试需要现有 mosquitto 服务端/客户端、OpenSSL、NumPy、pytest 及对应 ROS 测试依赖。mosquitto 通常位于 `/usr/sbin`，测试环境 PATH 应包含该目录。脚本不会安装依赖。

```bash
source /opt/ros/jazzy/setup.bash
colcon --log-base log build --base-paths src \
  --build-base build --install-base install \
  --cmake-args -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
               -DCHAOS_GO2_WITH_UNITREE_SDK=OFF
source install/local_setup.bash
colcon --log-base log-test test \
  --build-base build --install-base install
colcon test-result --test-result-base build --verbose
benchmarks/verify_consumer.sh install

# 原生 fake SDK 测试二进制位置；不会访问实际机器狗。
export CHAOS_RUNTIME_BUILD_BASE="$PWD/build"
for name in smoke_test go2_smoke_test go2_driver_smoke_test sensor_smoke_test \
            description_smoke_test data_smoke_test training_data_smoke_test \
            mqtt_smoke_test transport_smoke_test; do
  python3 "scripts/$name.py" || break
done
```

Cargo 缓存齐备时可额外传 `-DMQTT_ADAPTER_OFFLINE=ON`。始终指定本次 build/install 目录，避免读取旧结果。真实 Xacro 展开与缺包分支分开检查；本次使用上游 Xacro v2.1.1，提交 `da4b3849f8320903d625250089f67f0632be86f2`。

## 厂商与部署边界

- Unitree SDK2 固定在 `d1abf001220d3e5f59e00a0c4aaa595e00089a4a`。SDK-ON 编译、链接、`--build-capabilities` 和导出 target 的下游消费者均通过；SDK-OFF 明确报告不可用并拒绝 real 模式。
- **没有进行实机或 ARM64 验收**。未初始化实际机器狗 DDS、未连接真实底盘、飞控、相机、雷达或 IMU，未测厂商调用最坏耗时、设备停机效果、USB 重连或长时间磁盘压力。
- 未安装/启动生产 systemd 服务、未修改系统时钟或授时配置。采集准入和失步用例使用合成时间质量证据，不证明真实 NTP 或传感器同步精度。
- 训练导出为本项目 JSONL/NPY 中间格式；未实现 LeRobot 原生转换、模型训练或推理。mock 图像与运动不代表真实相机精度或机器人动力学。
- 接口升级需统一重建 `chaos_interfaces` 使用者。部署用新前缀且不叠加旧 overlay；详见 [机载部署说明](onboard-deployment.md)。
