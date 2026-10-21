# C++ 工业重构迁移映射

分支 `codex/cpp-industrial-refactor`，基线提交 `172b78c`。本文档随批次更新，记录
Python 实现 → C++ 实现的逐项映射与验证状态。旧 Python 代码在 Git 历史中保留为
对拍 oracle，批次期间生产入口不变。

## 分层

- `chaos_core`（无 ROS）：`Rejected` 错误类型、单调/系统时钟、诊断字符串格式化。
- 各业务包内 C++ 库（无 ROS，单向依赖 chaos_core）：
  - `chaos_runtime::chaos_control`（control.hpp/cpp）
  - `chaos_runtime::chaos_go2_core`（go2_driver_core.hpp/cpp）
  - `chaos_sensors::chaos_sensors_health`（health.hpp/cpp）
  - `chaos_data::chaos_data_timing`（timing.hpp/cpp）
- ROS 节点、协议网关、配置解析、录制监督等尚未迁移，后续批次按任务书执行。

构建：`colcon build --base-paths src --build-base build-cpp --install-base install-cpp --cmake-args -DCMAKE_BUILD_TYPE=Release`
（独立安装前缀，不污染旧 install/）。

## 批次 1（本文档首次提交）

### 源映射

| Python（oracle） | C++ | 状态 |
|---|---|---|
| `chaos_runtime/control.py` | `chaos_runtime::control`（`include/chaos_runtime/control.hpp`, `src/control.cpp`） | 已迁移 |
| `chaos_runtime/go2_driver_core.py`（ROS 无关核心） | `chaos::go2::Go2DriverCore` | 已迁移；`fill_odometry` 依赖 ROS 消息，随 ROS 包装批次迁移 |
| `chaos_sensors/health.py` | `chaos::sensors::HealthTracker` | 已迁移 |
| `chaos_data/timing.py` | `chaos::data`（parseTracking / ChronyProbe / ClockGuard） | 已迁移 |

### 测试映射（pytest → gtest）

| pytest 契约 | gtest | 覆盖说明 |
|---|---|---|
| `chaos_runtime/test/test_control.py`（12 例） | `test/test_control_cpp.cpp`（12 组） | 逐项对应；`token=True` 为 Python 类型卫生用例，C++ 由类型系统保证 |
| `chaos_runtime/test/test_go2_driver.py` 核心部分（23 例） | `test/test_go2_driver_core.cpp`（22 组） | 逐项对应；`failure=None/False` 为 Python 边界类型用例（C++ Move 返回类型为 int，由 1/异常覆盖）；ROS 包装 3 例（`driver_factory` fixture）待 ROS 包装批次迁移后补 gtest 集成 |
| `chaos_sensors/test/test_health.py`（9 例） | `test/test_health_cpp.cpp`（9 组） | 逐项对应；`timeout=True/None` 由 C++ 类型系统保证 |
| `chaos_data/test/test_timing.py`（15 例） | `test/test_timing_cpp.cpp`（15 组） | 逐项对应；`required_samples=True/strict=1` 由 C++ 类型系统保证 |

### 有意保持的 Python 行为细节

- 错误码字符串逐字保留（`NO_CONTROL`、`STALE_COMMAND`、`INVALID_FRAME`…）。
- 常量时间 token 比较：无长度/内容早退；校验顺序与 Python 完全一致。
- `CommandDeadline`：monotonic 读在前、wall 读在后；零 Unix 时间戳 = 无期限。
- Go2：零速度命令 = 源时间屏障；tick 回绕感知排序；SDK 写失败闩锁；
  `step` 不在缓存锁内执行 `move`；遥测采样保留原始 ROS 时间戳。
- health/timing：诊断 value 字符串格式与 Python 输出一致（`pythonFloatStr`、
  `%.6f`、round-half-even 的 `lrint`）。
- chronyc 只读调用（`-c -n tracking`）带绝对总超时；退出码/缺失/超时/非 UTF-8
  分别映射 `chronyc_failed/not_found/timeout/unavailable`。

### 混合构建保真项

`chaos_runtime`、`chaos_sensors`、`chaos_data` 由 ament_python 转为
ament_cmake + ament_cmake_python 混合包：

- Python 包仍由 `ament_python_install_package` 安装，生产入口未切换。
- setuptools entry_points 以 dist-info（`entry_points.txt` + `METADATA`）
  形式显式安装，`chaos.robot_adapters` 插件发现与 console 脚本保持不变。
- console 脚本（`robot_runtime`、`go2_driver`、`mock_sensor`、`sensor_monitor`、
  `collector`、`audit`、`data_audit`、`data_export`）由 CMake 生成等价 wrapper。
- `chaos_data` 的 `config/*.yaml` 与 resource 标记改由 CMake 安装，目标路径不变。

### 微基准（Release）

`src/chaos_benchmarks/micro_benchmark`：与 `baseline_micro.py` 同输入、同批次结构
（31 批 × 20000，预热 20000，43 字符 token）。分位数为批均值分位数（索引采用与
Python `round` 一致的 ties-to-even），不是单请求延迟；纯领域基准，不代表 ROS 端到端
收益。checksum 经 volatile sink 防优化消除，且与 Python 基线逐项一致。

环境：x86_64，Intel Core Ultra 7 251HX，18 逻辑核，Ubuntu 24.04 / Jazzy，
`-O3 -DNDEBUG`（colcon Release）。

| 工作负载 | Python 基线批均值 median ns | C++ 批均值 median ns | 比值 | 两边 checksum |
|---|---|---|---|---|
| control_velocity_valid | 846.9 | 38.7–41.7 | ~20–22× | 62247999.99997777（一致） |
| sensor_health_observe | 211.0 | 6.9–7.4 | ~29–31× | 204600310000.0（一致） |

原始数据：`benchmarks/results/baseline_python_2026-10-21.json`、
`benchmarks/results/cpp_native_2026-10-21.json`（及 run2）。批均值 p95/p99 见 JSON；
上述数字仅为同机同输入的领域微基准对比，不得当作 ROS 端到端或整机性能结论。

### 审查修复（Codex 初审后落实）

- 时间戳 int64 溢出：`chaos::rosStampToNs` 统一 ROS int32 秒界内校验
  （sec ≤ 2147483647），control deadline / go2 stampNs / health observe 全部改经该校验；
  新增 INT64_MAX 等越界用例，ASan/UBSan 下通过。
- Go2 并发封装：删除全部未加锁 getter 与 mutable fault()；`status()` 返回加锁复制的
  `Status` 快照，`sample()` 返回加锁复制的 optional 样本；step/write 改为模板转发引用
  （消除 std::function 拷贝测试替身导致的观测丢失，也符合 Python 鸭子类型语义）。
- ControlGuard 封装：owner/token/expires/velocity_expires 私有化，只读 getter +
  `clearVelocity()`；runtime 后续只能经显式方法变更状态。
- CSPRNG token：`getrandom(2)` 取 32 字节，base64url 无填充编码（43 字符，与
  `secrets.token_urlsafe(32)` 等价）；失败抛异常，绝不安空 token 租约。
- owner Unicode 语义：按码点计数（≤128）、Unicode 空白剥离（与 Python `str.strip()`
  对齐）、畸形 UTF-8 拒绝。
- chronyc 子进程：绝对 deadline 覆盖读 EOF 与 EOF 后退出等待（WNOHANG 轮询 + 超时
  SIGKILL 回收）；输出上限 4096 字节；所有路径关闭 FD 并回收子进程；复现探针
  （stdout 后 sleep 0.4s）确认 50ms 预算实际耗时 ~50ms（修复前 ~400ms）。
  `ChronyProbe::sample()` 改为查询完成后采时间。
- 基准分位数索引与 Python `round`（ties-to-even）对齐。
- `ament_cmake_gtest` 仅在 `BUILD_TESTING` 下查找；生产构建无测试依赖。
- 安装后下游消费者验证：`benchmarks/verify_consumer.sh`（`benchmarks/consumer`）
  仅对 `install-cpp` 配置链接全部四个导出 target，通过。

### 验证记录（2026-10-21 实测）

- gtest（Release）：test_control_cpp 15、test_go2_driver_core_cpp 25、
  test_health_cpp 10、test_timing_cpp 15，共 65 项全部通过。
- gtest（ASan+UBSan，`-fno-sanitize-recover=all`，含泄漏检测）：同样 65 项全部通过，
  无 sanitizer 报告。
- Python 全量回归（契约基线命令）：`704 passed, 4 skipped in 3.32s`
  （与基线 704/4 完全一致）。
- entry points：`chaos.robot_adapters` 四个插件（mock/lingao_ros2/unitree_go2/
  dji_psdk_legacy）从 install-cpp 元数据解析并可加载；8 个 console 脚本安装就位。
- 安装后下游消费者编译链接运行通过（见上）。
- chronyc deadline 复现探针：DEADLINE-OK（50.2ms / 49.5ms 两次）。
