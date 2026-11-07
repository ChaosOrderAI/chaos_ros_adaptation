# 可复现性能验收

这些脚本只启动 mock runtime，测量速度命令从发布到收到 `CommandResult` 的本机往返时间。它包含 DDS、ROS executor 和测量客户端调度开销，不代表实机执行延迟或硬实时保证。CPU 仅统计直接启动的 runtime 进程，内存来自 Linux `/proc`，不包含测量客户端。

先按仓库 README 在全新的前缀完成 Release 构建，再在干净终端 source ROS 和该前缀的 `local_setup.bash`。下列命令从工作空间根目录执行；`INSTALL` 应替换为实际安装前缀。

```bash
INSTALL="$PWD/install"
RESULTS="$(mktemp -d /tmp/chaos-benchmark-XXXXXX)"
python3 benchmarks/extract_python_baseline.py \
  --ref 172b78c --output "$RESULTS/python-baseline"
python3 benchmarks/compare_runtime.py \
  --baseline "$RESULTS/python-baseline/robot_runtime" \
  --native "$INSTALL/chaos_runtime/lib/chaos_runtime/robot_runtime" \
  --output "$RESULTS/comparison"
```

基线提取器只读取指定 Git 提交中的 Python 源码和入口定义，不执行旧 `setup.py`、不安装旧包。它记录提交和源文件 SHA-256。若使用 merged install，native 路径是 `$INSTALL/lib/chaos_runtime/robot_runtime`。

默认分别测量 100 Hz × 500 条、20 Hz × 200 条；每次预热 50 条，结束后统一等待 3 秒收集回复。每组运行两轮，按 Python/C++、C++/Python 交替顺序执行。使用相同 mock 资产、租约、QoS、ROS domain 和客户端；运行期间应停止其他构建及负载任务。默认 domain 为 194，可通过 `--domain` 指定空闲域，发现范围固定为 `LOCALHOST`。

`result.json` 保留每次运行的原始延迟、p50/p95/p99、预期/收到/缺失回复数、拒绝码、CPU、RSS、可执行文件哈希及系统负载。**先检查缺失和拒绝，再比较延迟**：百分位只针对收到的回复；漏掉慢回复会使延迟看起来更低。CPU 窗口包括预热和相同的收尾等待，VmHWM 包括启动。单次低负载测量不能替代长期压力和真实硬件验收。

单独运行一组可使用 `latency_roundtrip.py RATE SAMPLES --executable ... --native`；Python 基线省略 `--native`。`--asset` 接受已经解析的 mock 资产 JSON；脚本拒绝真实设备插件。默认固定配置在 `fixtures/mock_asset.json`。

`verify_consumer.sh` 是安装产物的下游 C++ 编译/链接检查，不是性能基准。它验证公开 CMake targets 能从独立工程消费。`results/runtime_comparison_2026-10-22.json` 保存本次匹配条件下的完整采样；旧的纯函数对比记录已归档到仓库外。
