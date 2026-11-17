# STM32H7 多传感器同步板

四路传感器（可见光相机 / 雷达 / IMU / 红外相机）硬件触发同步，时基来自
GNSS/RTK 的 PPS，目标是**微秒级**同步。

方案参照 [OctoSense SyncBoard](../octosense-syncboard/README.md)（MIT），
但有两处关键改动，都是为了把抖动从"软件中断"降到"硬件比较"：

| | OctoSense | 本设计 |
|---|---|---|
| 时基 | DS3231 RTC 的 SQW（±2 ppm，自由振荡） | **RTK 模块的 PPS**（GNSS 驯服） |
| 触发产生 | ISR 里 `digitalWriteFast`（抖动 ~1–5 µs） | **定时器输出比较**（抖动 ~几十 ns） |

## 架构

```
                    ┌──────────────────────── 同步板 (STM32H743VGTx) ───────────────────────┐
  RTK 模块 ──PPS──► │ 施密特整形 → TIM5_CH1 捕获 (10 ns tick)                              │
  (NMEA)  ──UART──► │ USART2 → NMEA 解析 (UTC + 定位状态)                                  │
                    │   TIM5 计数器 ──TRGO(1 Hz)──► ITR0 ──┬─► TIM2  触发0 可见光相机       │
  DS3231  ──I2C1──► │                                     ├─► TIM3  触发1 雷达           │
  AT24C02 ──I2C2──► │                                     ├─► TIM4  触发2 IMU             │
                    │                                     └─► TIM1  触发3 红外相机       │
  主机 ────UART1/RS485──► │ 帧协议：状态 / PPS / 触发配置 / 应答                          │
                    └───────────────────────────────────────────────────────────────────┘
```

关键点是 **TIM5 → ITR0 的主从级联**：TIM5 每个 PPS 边沿清零计数，1 秒后发一次
TRGO；四个通道定时器都以 ITR0 为 Reset 源，因此每秒都被重新锚定到 PPS 边界，
而通道之间的相位关系由同一个 240 MHz 时基保持，**不随时间漂移**。

## 目录

| 路径 | 内容 |
|---|---|
| [connections.md](connections.md) | 完整连接表（58 器件 / 67 网络），由脚本生成 |
| [bom.csv](bom.csv) | BOM |
| [tools/gen_connections.py](tools/gen_connections.py) | 连接表的单一数据源，`--check` 可校验自洽 |
| [firmware/core/](firmware/core/) | 可移植核心逻辑：帧协议、触发调度、指纹、时间换算（无 HAL 依赖） |
| [firmware/stm32h7/](firmware/stm32h7/) | STM32 集成层：PPS 捕获、触发输出、时间源、主机协议 |
| [firmware/stm32h7/CUBE.md](firmware/stm32h7/CUBE.md) | CubeMX 配置清单（时钟/定时器/NVIC/串口） |
| [host_test/](host_test/) | 核心逻辑的主机端单元测试 |

## 已验证 / 未验证

**已验证**：核心逻辑（帧编解码 + CRC、触发相位计算、定时器参数规划、脉冲指纹、
GPS/UTC 换算）用 gcc 编译并在主机上跑通，53 项断言全过：

```bash
cd hardware/stm32h7-sync-board/host_test && make
```

**未验证**：`firmware/stm32h7/` 下的 HAL 代码**没有编译过**——本机没有
arm-none-eabi-gcc 也没有 STM32Cube HAL/CMSIS 包。它是按 CubeMX 工程的
`main.h` / `htimX` / `huartX` 句柄约定写的，需要在真实工程里编译。
连接表的自洽性由 `tools/gen_connections.py --check` 保证（每个引脚只出现在一个网络里）。

## 抖动预算

| 环节 | 量级 | 说明 |
|---|---|---|
| RTK PPS 本身 | 20–50 ns RMS | 取决于接收机与天线；由 `status.jitter_ns` 实测上报 |
| RC 低通 (1k+100pF) | ~几百 ns 上升沿 | 只滤高频噪声，不引入固定延迟 |
| 施密特整形 (74LVC2G17) | ~5–20 ns | 固定延迟，可标定 |
| TIM5 捕获量化 | ±10 ns | 计数 tick = 10 ns |
| TIM5→TIM2 主从复位 | ~20–50 ns | 内部触发通路 |
| 输出比较 (OC) 量化 | ±4.2 ns | 240 MHz 定时器 |
| 缓冲器 (74LVC2G34) | ~5–15 ns | |
| **合计（不含传感器内部延迟）** | **< 150 ns** | 远好于 µs 级要求 |

传感器内部的固定延迟（触发→曝光中心、雷达自守时偏差）必须逐路实测标定，
这部分通常是 µs 级，是端到端误差的主要来源。

## 一个必须知道的约束：频率与相位

四个通道定时器每秒被 PPS 重新锚定一次。**通道周期必须整除 1 秒**
（例如 10 / 20 / 25 / 50 / 100 Hz），否则每次锚定都会在通道上造成最多
一个周期的相位跳变。

需要 30 Hz 这类频率时有两个选择：

1. **DMA 比较表**（推荐，抖动仍 < 100 ns）：把一个周期内的所有沿预计算成
   CCR 表，用 DMA 循环模式在每次比较事件后自动更新下一个 CCR，
   整个过程不经过 CPU。
2. 软件重装 CCR：抖动退化为中断延迟量级（~1–2 µs）。

相机频率若可协商，优先选 25 Hz / 50 Hz 这类整除频率，可以直接走纯硬件方案。

## 脉冲指纹

每秒的前 6 个 PPS 用递增的脉冲宽度编码一个固定图案（默认 `1,0,1,1,0,0`，
基准 10 ms、步进 20 ms），主机侧据此把各路数据流对齐到同一条时间轴；
中途丢触发也能靠指纹重新对齐。实现见 `sync_core.c` 的
`sync_fingerprint_widths_us()` / `sync_fingerprint_matches()`。

## 作为 Orin NX 载板的 mezzanine

同步板加了 **J10（2×22）**，与载板的 J12 一一对应，可以直接插在载板上
（配 5 mm 铜柱）。分工：

- **载板**：供电、电平转换（1.8 V ↔ 3.3 V）、把触发线引到相机同步座。
- **同步板**：所有时序。PPS 捕获、四路触发的相位、帧协议，都在同步板里。

插上之后，模块（Orin）侧看到的是：

```
RTK ──PPS/NMEA──► 同步板 (TIM5 捕获 + 四路 OC 触发)
                    │  UART 帧协议
                    ▼
                  载板 ──► 模块 GPIO (PPS 锁定 / 触发状态)
```

J10 与 J12 的 44 脚逐脚对应关系由
`hardware/orin-nx-carrier/tools/check_mezzanine.py` 校验：改了一边忘了另一边，
脚本会直接报错。引脚分配见
[firmware/stm32h7/CUBE.md](firmware/stm32h7/CUBE.md) 的 6.1 节。

**装成 mezzanine 时 U3（RS-485）不贴**，USART1 只能接一边。

## 与本仓库的衔接

`src/chaos_data` 目前只有软件级时钟门禁（`ChronyProbe` 探测
`CLOCK_REALTIME`，容差 `max_uncertainty_ms: 10.0`），它保证的是"墙钟可信"，
和硬件同步是两件事。硬件同步落地后建议补两件事：

1. **驱动用硬件时间戳打 `header.stamp`**。如果驱动用"收到消息时的系统时间"
   打戳，那么 USB/网络传输的抖动就有几百 µs 到几 ms，硬件同步白做了。
   ROS 2 里可以用 `sensor_msgs/msg/TimeReference` 把硬件时刻注入驱动。
2. **在 `time_sync.jsonl` 里记录硬件同步状态**（PPS 锁定与否、最近抖动、
   触发序号），让导出时能区分"软件同步过的数据"和"硬件同步过的数据"。

具体改法待定，可以先做硬件。

## 下一步

- [ ] 在真实 CubeMX 工程里编译 `firmware/stm32h7/`，修正 HAL 调用细节
- [ ] 用示波器量四路触发的相对边沿，确认 < 100 ns
- [ ] 标定各传感器的触发→采样固定延迟
- [ ] 把连接表导出成 KiCad 网表（需要先确定封装引脚号）
- [ ] 主机侧 ROS 2 节点：订阅状态、给驱动注入硬件时间戳
