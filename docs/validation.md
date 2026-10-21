# C++ 重构验证记录

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
colcon --log-base log-cpp build --base-paths src \
  --build-base build-cpp --install-base install-cpp \
  --cmake-args -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
               -DCHAOS_GO2_WITH_UNITREE_SDK=OFF
source install-cpp/local_setup.bash
colcon --log-base log-cpp-test test \
  --build-base build-cpp --install-base install-cpp
colcon test-result --test-result-base build-cpp --verbose
benchmarks/verify_consumer.sh install-cpp

# 原生 fake SDK 测试二进制位置；不会访问实际机器狗。
export CHAOS_RUNTIME_BUILD_BASE="$PWD/build-cpp"
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
