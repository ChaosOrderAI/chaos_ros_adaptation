# C++17 迁移：资产配置与启动规划器最终设计

范围：`src/chaos_assets`（原生配置引擎、原生 CLI、薄 Python 绑定、原生测试套件）与 `src/chaos_bringup`（原生启动规划器、原生组件派生、原生运行时接口、薄 Python 胶水）。

本文档描述迁移后的**最终设计**和可移植的构建/检查命令。总体迁移边界见 [C++17 迁移总览](cpp-migration.md)，实测结果见 [验证记录](validation.md)。

## 1. 原生引擎负责什么

`chaos_assets` 是 `ament_cmake` 包。原有 Python 实现的全部业务规则现在都在 C++ 库 `chaos_assets_core` 中：

| 关注点 | 原生类型 / 文件 |
|---|---|
| YAML 加载，含 YAML 1.1 隐式类型 | `chaos_assets::load_yaml_string`，`src/yaml.cpp` |
| 规范化 JSON 与配置摘要 | `chaos_assets::canonical_json`、`chaos_assets::config_digest`，`src/json.cpp` |
| 路径与 `package://` URI 解析 | `chaos_assets::resolve_path`，`src/paths.cpp` |
| 清单、型号继承、限值、能力、摘要 | `chaos_assets::Inventory`、`chaos_assets::load_inventory`，`src/inventory.cpp` |
| 传感器解析与逐域传感器图 | `chaos_assets::resolve_sensors`、`chaos_assets::validate_sensor_graph`，`src/sensors.cpp` |
| 录制策略解析 | `chaos_assets::resolve_collection`，`src/collection.cpp` |
| 机器人描述（URDF/Xacro）、结构与摘要 | `chaos_assets::resolve_description`、`chaos_assets::validate_description_graph`，`src/description.cpp` |
| 加固的 XML 解析与序列化（libxml2） | `chaos_assets::XmlDocument`，`src/xml.cpp` |
| 有界子进程执行（posix_spawn） | `chaos_assets::run_bounded`，`src/process.cpp` |

引擎**不依赖** rclcpp、设备 SDK 或厂商插件，只使用 `yaml-cpp`、`nlohmann::ordered_json`、`libxml2`、`urdfdom`、`OpenSSL` 和 `ament_index_cpp`。

### 公开入口

- `asset_config` —— 原生可执行文件，契约与迁移前一致：
  `asset_config INVENTORY [--host HOST] [--mode {mock,real}] [--output FILE]`。
  配置被拒绝时在 stderr 打印 `configuration error: ...` 并以状态码 2 退出。
- `chaos_assets`（Python 包）—— `chaos_assets._chaos_assets_core` 的薄绑定。`chaos_assets.inventory`、`chaos_assets.sensors`、`chaos_assets.description`、`chaos_assets.collection` 和 `chaos_assets.cli` 只在历史导入路径上重新导出原生引擎，**Python 侧不剩任何业务决策**。

### 与原 Python 实现的有意差异

1. **XML 字节格式。** 描述快照由 libxml2 序列化，不再使用 `ElementTree`。原生文档保留 XML 声明和源文件注释，并使用 `<tag/>` 而不是 `<tag />`。两者语义相等但字节不同，因此带机器人描述的资产，其 `description.sha256` 与 `config_digest` 与 Python 基线不同。这是有意的：验收要求语义等价且摘要稳定，而不是复刻 Python 的字节格式。差异被证明只局限于 XML，由 `test_golden_configs.cpp` 覆盖。
2. **错误措辞。** 每一次拒绝的**错误类别**都保留（"unknown fields"、"expected a mapping"、"invalid nonempty string"、"expected a positive finite number" 等），仅在原实现嵌入 Python `repr` 的地方措辞不同。
3. **Xacro 展开。** 展开由第三方 `xacro` 包经一个不含业务决策的薄桥（`tools/xacro_expand.py`）完成。请求以文件方式传递，子进程从不读取标准输入。

## 2. 由代码和测试共同保证的健壮性规则

- **进程运行器**（`src/process.cpp`）：只用 `posix_spawn`，不 fork，fork 与 exec 之间不做分配。每个 `posix_spawn_file_actions_*` 与 `posix_spawnattr_*` 的返回值都被检查，准备不完整的子进程绝不启动。管道用 `pipe2(O_CLOEXEC)` 创建，所有描述符（含读端）都被搬迁到标准流之上，因此子进程的 `dup2` 映射不可能关闭错误的描述符。标准输入是 `/dev/null` 或预填充的匿名文件，父进程不向 stdin 管道写入。子进程在自己的进程组中运行；退出检测使用 `waitid(..., WNOWAIT)`，使进程标识在组被终止期间保持保留，不会误杀无关进程。输出有上限，截止时间是单一的 `steady_clock` 截止点，守卫器在每一条路径上（含异常）都终止并回收子进程。
- **XML**（`src/xml.cpp`）：libxml2 开启 `XML_PARSE_NONET`，不开启 `DTDLOAD`、`NOENT` 或 `HUGE`。`DOCTYPE` 与 `ENTITY` 声明在解析器运行前被拒绝，之后再检查内部子集。文档上限 4 MiB、嵌套上限 256 层。文档由实现对象持有，因此移动赋值会释放上一份文档。
- **路径**（`src/paths.cpp`）：`package://` URI 规范化为包内相对路径；绝对相对路径、`..` 段、重复斜杠和空路径一律被拒绝，URI 无法逃出包的 share 目录。
- **C++ 生命周期**：`text()` 与 `record()` 按值返回，临时文档不会留下悬垂引用。插入 `ordered_json` 对象后仍要使用的每个值都会复制，因为插入键可能使该对象的引用和迭代器失效。

### 子进程的标准输入

`chaos_assets::run_bounded` 接受给子进程标准输入的载荷。载荷由**匿名普通文件**承载（`memfd_create`，回退到 unlink 的临时文件），绝不使用管道：

- 管道有容量。父进程必须先写完全部载荷才能读取子进程的输出；若子进程在读入之前写出的量超过管道容量，双方都无法推进，形成死锁。预填充文件消除了对孩子读取进度的依赖；文件准备仍可能遭遇资源或存储错误，且不计入 spawn 后的执行超时。
- 子进程关闭标准输入时，管道可能在父进程中触发 `SIGPIPE`。运行器不改动进程级信号处置，因此这样的子进程会杀掉启动器。普通文件不会。
- 文件在子进程启动前准备、回卷到开头并设置 close-on-exec。子进程拿到它作为标准输入的一份副本，父进程关闭自己的一份，文件随最后一个引用消失。
- 单次载荷上限 1 MiB（`kMaxProcessInput`），限制此接口额外保留的输入大小。

## 3. 启动规划器（chaos_bringup）

启动规划同样是原生的。C++ 核心拥有全部决策，Python 层只组装 ROS launch 动作。

| 关注点 | 归属 |
|---|---|
| 资产选择、冲突规则（速度输出归属、Go2 DDS 连接、Go2 里程）、MQTT 身份 | `chaos_bringup_core`（`planner.cpp`） |
| 组件派生、摘要、预检、参数文件快照、进程规格 | `chaos_bringup_core`（`components.cpp`） |
| 运行时的无硬件接口（资产校验、能力报告） | `chaos_bringup_core`（`runtime.cpp`） |
| MQTT 适配器配置 | `chaos_mqtt`（注入，从不复制） |
| `launch` 动作组装、退出策略、已解析资产快照 | `chaos_bringup/launching.py` |

### 接口契约

- **不可变性。** `LaunchPlan` 保存每一项的编码形式并交出分离的文档，调用方无法修改已检查的计划。Python 侧的 `LaunchPlan` 只是同一快照的按读复制包装。
- **注入而非探测。** 规划器从不探测厂商 SDK。已注册的存储写入器、原生驱动后端、可用的 ROS 可执行文件和运行时校验都作为可调用对象（`PreflightOptions`）注入。因此 mock 构建无法进入 real 模式：没有报告后端的真实 Go2 资产会预检失败。
- **运行时接口。** `robot_runtime --validate-asset` 通过标准输入接收资产文档并回答 `{"valid": true}`；`go2_driver --build-capabilities` 回答 `{"unitree_sdk2": true|false}`。两者都由原生有界进程运行器执行，强制截止时间、有界输出和有界标准输入，且从不在管道上阻塞。预检在创建任何一个进程动作之前校验**全部**被选中的资产。
- **校验的是运行时协议，不是退出码。** 退出码 0 不算回答。引擎解析回复，要求它是一个带 `valid` 字段且值为 `true` 的 JSON 对象。畸形文档、缺字段和值为 `false` 都会中止启动，因此一个只打印错误文本的运行时无法通过预检。
- **共享错误类型。** 规划器与资产引擎抛出同一个 `chaos_assets.ConfigurationError`，同时调用两个包的调用方只需捕获一种异常。

### Python 边界

`planning.py`、`launching.py`、`sensors.py`、`description.py`、`collection.py`、`drivers.py` 和 `runtime.py` 都是胶水：转发请求、转换数值、组装 `launch` 动作。Python 中唯一的行为是动作列表的组装、共享退出策略，以及把已解析资产复制到私有 launch 目录。

## 4. 构建与检查命令

```bash
source /opt/ros/jazzy/setup.bash

# 全新构建目录；只加载本次安装空间，不叠加旧 overlay
colcon --log-base log build --base-paths src \
  --build-base build --install-base install \
  --cmake-args -DCMAKE_BUILD_TYPE=Release
source install/local_setup.bash

colcon test --packages-select chaos_assets chaos_bringup \
  --build-base build --install-base install
colcon test-result --test-result-base build --verbose
```

Golden 配置测试先将包内绝对路径规范化，再独立复算摘要，因此普通安装和不同路径的 checkout 均可比较；不要求 `--symlink-install`。

### Sanitizer 构建

```bash
colcon --log-base log-asan build --packages-select chaos_assets \
  --build-base build-asan --install-base install-asan \
  --symlink-install \
  --cmake-args -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -g" \
    -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined" \
    -DCMAKE_SHARED_LINKER_FLAGS="-fsanitize=address,undefined"

cd build-asan/chaos_assets
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 \
  ctest -E chaos_assets_python
```

库本身被插桩，不只是测试被插桩。

**已知限制。** Python 绑定测试无法在 sanitizer 下运行：把 `LD_PRELOAD=libasan.so` 注入未插桩的 CPython 会触发 `__interception::real___cxa_throw`。因此 Python 绑定针对 release 构建验证：

```bash
python3 -m pytest src/chaos_assets/test/test_python_bindings.py -q
```

等价行为由原生 gtest 覆盖，它们在 ASan/UBSan/LSan 下干净通过。

### 外部消费者

`benchmarks/consumer` 只 `find_package` 公开的 C++ 包并链接其导出 target，然后运行产物：

```bash
benchmarks/verify_consumer.sh install
```

导出的 target 不再引用内部 imported target：基于 pkg-config 的依赖以普通链接标志链接，`ament_export_dependencies` 发布 `nlohmann_json`、`yaml-cpp`、`ament_index_cpp` 和 `OpenSSL`。

## 5. 测试套件

原生 gtest 二进制（`ament_add_gtest`），全部位于 `src/chaos_assets/test`：

| 套件 | 内容 |
|---|---|
| `test_yaml.cpp` | YAML 1.1 隐式类型：`0b`/`0x`/`0` 前缀、六十进制数字、显式标签、带引号标量、重复键、非字符串键、递归别名、嵌套深度、整数范围 |
| `test_paths.cpp` | 相对与绝对路径、`package://` 解析、`..`、空路径、重复斜杠、query 与 fragment |
| `test_inventory.cpp` | demo 清单、real/mock 后端、Python 套件的拒绝表、主机过滤、禁用的资产、选项覆盖、脱离式数据、传输可执行文件处理、摘要稳定性 |
| `test_sensors.cpp` | 话题与命名空间解析、强制 mock 驱动、被拒绝的声明、安装外参、同一域内的重复 frame 与话题 |
| `test_collection.cpp` | 默认值、输出目录、被拒绝的标志、storage id、拆分上限、空闲空间预留、轮询间隔、模拟时间、显式话题 |
| `test_description.cpp` | 连接与关节及摘要、禁用的描述、`DOCTYPE`/`ENTITY`、未展开的 Xacro、畸形 XML、非法控制字符、重复属性、非法树、根 frame、mesh 资源、远程资源、纯 URDF 上的参数、xacro 失败报告 |
| `test_process.cpp` | 输出与退出码、真正运行的子进程、输出上限、截止时间、关闭的流、保持管道打开的孙进程、大量标准错误、缺失的可执行文件、进程组终止、信号终止、有界匿名文件形式的标准输入、关闭自己标准输入的子进程、先写 128 KiB 再读 256 KiB 的子进程、从不读输入的子进程、标准输入上限、打开的描述符数量 |
| `test_golden_configs.cpp` | 与基线快照的等价性对比 |

`chaos_bringup` 的原生套件：

| 套件 | 内容 |
|---|---|
| `test_planner` | 选择、不可变性、MQTT 身份、四条归属规则、传输解析 |
| `test_components` | 组件派生、摘要、预检、参数文件校验、输出目录检查 |
| `test_runtime` | 运行时协议：有效回答、否定回答、畸形回答、缺字段的回答、失败的接口、缺失的接口 |

原有 Python 测试套件被保留并注册；它导入的模块现在是原生引擎的薄绑定，因此同样的断言作用在迁移后的实现上。

### 启动胶水：能力被注入

原生规划器的预检向构建询问其能力，而不是探测它，因此 launch 层测试通过 `test/conftest.py` 的 `capabilities` fixture 声明所模拟的部署环境：

| 原机制 | 新机制 |
|---|---|
| `monkeypatch.setattr(collection, 'require_executable', ...)` | `capabilities['executable_available']` |
| `monkeypatch.setattr(collection, 'registered_writers', ...)` | `capabilities['registered_writers']` |
| `monkeypatch.setattr(drivers.importlib.util, 'find_spec', ...)` | `capabilities['backend_available']` |
| 运行时的资产校验 | `capabilities['validate_asset']` |
| `monkeypatch.setattr(planning, 'load_inventory', ...)` | 冻结的计划本身；测试在读取后修改文件，并断言计划保留快照 |

断言得以保留：被禁用组件的依赖不会被询问、缺失的厂商驱动在构造节点之前被拒绝、mock 传感器不需要厂商驱动、没有后端的 Go2 资产被拒绝、录制器的退出策略不会停止机器人、参数文件快照是私有的。

## 6. 已知未完成项

1. **描述字节格式与 Python 基线不同**（见第 1 节）。如果下游消费者把描述摘要与存储值比较，迁移后需要重新计算一次。带机器人描述的两个资产的摘要差异只源于此，XML 语义相等，源文件摘要一致。
2. **Xacro 展开依赖可选包。** `xacro` 是可选依赖；套件中的真实 Xacro 用例在缺少它时被跳过，在 `PYTHONPATH` 包含官方源码时运行。桥接保持原包装器的异常约定：缺失 include 会恢复文件栈并报告为 `XacroException`，这也是 `xacro` 包在可选 include 时唯一捕获的异常。
3. **包前缀顺序决定使用哪个运行时。** 规划器通过包索引解析运行时的无硬件接口，因此 `AMENT_PREFIX_PATH` 中**第一个** `chaos_runtime` 前缀获胜。旧 Python 运行时前缀会遮蔽原生运行时，导致 launch 以无关错误失败。必须先 source 新运行时前缀。
4. **没有接触任何硬件。** 本文档中的结果全部来自 mock 配置、独立前缀和本地进程。Go2 驱动的 real 模式、真实传感器和真实录制器均未测试。
