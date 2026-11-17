# OctoSense SyncBoard 参考原理图

第三方开源参考设计，来自 [OctoSense](https://github.com/anthonytec2/OctoSense)（MIT License，Copyright (c) 2026 Anthony Bisulco）。
本目录只作为自研同步板的**设计参考**，不是本仓库的正式产品设计文件。

## 文件

| 文件 | 说明 |
|---|---|
| `SyncBoard.kicad_sch` | KiCad 9 原理图源文件（可编辑） |
| `SyncBoard.kicad_pcb` | KiCad 9 PCB 布线文件 |
| `SyncBoard.kicad_pro` | KiCad 工程配置 |
| `SyncBoard.pdf` | 仓库自带渲染出的原理图 PDF（单页 A4） |
| `SyncBoard.schematic.png` | 由上面的 PDF 渲染出的位图，便于快速查看 |
| `pps_clock.ino` | Teensy 4.1 固件（PPS 时钟 + 多路触发输出） |
| `LICENSE.octosense` | 上游 MIT 许可证原文 |

## 板子是什么

一块 **Teensy 4.1 载板**，把 Teensy 的引出重排成三类接口：

```
GNSS/RTC 时基            触发输出                        辅助
DS3231 SQW 1Hz ──► GPIO16  ──►  10 路触发（J1 Molex 2x05）  ──►  电源 + SPI（J4）
32kHz      ──► GPIO21              每路都有测试点（J3）        I2C（J2 pin3/4）
```

上电后由 DS3231 的 1 Hz 方波（SQW，落在 GPIO16）驱动，Teensy 用 100 Hz 定时器
（5 ms/拍）相位对齐到 PPS 边沿，再由固件在多路 GPIO 上产生触发脉冲。

## 网络表（以已布线的 PCB 为准）

### J1 — Trigger Pin Connector（Molex Micro-Fit 3.0，2x05）
固件常量见 `pps_clock.ino` 顶部。

| J1 脚 | Teensy 4.1 GPIO | 固件用途 | J3 测试点 |
|---|---|---|---|
| 1 | GPIO2（pad 4, FLEXPWM OUT2） | `PIN_IMU`：1 Hz 脉冲，10 ms 宽 | J3.2 |
| 2 | GPIO5（pad 7） | `PIN_RGB_L`：连续 100 Hz 方波（相机触发） | J3.4 |
| 3 | GPIO8（pad 10） | `PIN_EVENT`：事件相机，宽脉冲 | J3.6 |
| 4 | GPIO9（pad 11, OUT1C） | 备用 PWM | J3.8 |
| 5 | GPIO10（pad 12, CS_MQSR） | 备用 PWM | J3.10 |
| 6 | GND | 地 | — |
| 7 | GPIO26（pad 18） | 备用 | J3.3 |
| 8 | GPIO25（pad 17） | 备用 | J3.5 |
| 9 | GPIO24（pad 16） | `PIN_RGB_1HZ`：1 Hz 脉冲 | J3.7 |
| 10 | GPIO12（pad 14, MISO_MQSL） | `PIN_LIDAR`：雷达 PPS，宽脉冲 | J3.9 |

### J2 — Connector for RTC（1x06，DS3231 模块接口）

| J2 脚 | 信号 | Teensy |
|---|---|---|
| 1 | 32 kHz | GPIO21（pad 43） |
| 2 | SOW 1 Hz（PPS 输入） | GPIO16（pad 38） |
| 3 | SCL | GPIO19（pad 41） |
| 4 | SDA | GPIO18（pad 40） |
| 5 | VCC 3V3 | 3V3 |
| 6 | GND | GND |

### J4 — Aux Pins（1x06）

| J4 脚 | 信号 | Teensy |
|---|---|---|
| 1 | GND | GND |
| 2 | 3V3 | 3V3 |
| 3 | VIN | VIN（pad 48） |
| 4 | SPI MISO | GPIO39（pad 31） |
| 5 | SPI CS | GPIO38（pad 30） |
| 6 | SPI CS | GPIO37（pad 29） |

J3（2x05 排针）是每路触发的并联测试点，方便示波器量触发沿。

## 固件的时序技巧

- **相位锁定**：`IntervalTimer` 100 Hz，每拍 5 ms；RTC 的 1 Hz 上升沿中断里重启定时器，
  使 100 Hz 方波始终与 PPS 同相。
- **并发触发**：PPS 到达时 `digitalWriteFast` 同时拉高 IMU / EVENT / RGB_1HZ / LIDAR 四路，
  4.5 µs 的 `digitalWriteFast` 指令让四路起始沿相差在百 ns 级。
- **宽脉冲编码**：前 6 个 PPS 的 EVENT/LIDAR 脉冲宽度按 30→80 ms 递增，IMU/RGB 再补一个
  间隔递增的第二脉冲。这串唯一脉冲序列是**事后离线对齐各路数据流的指纹**
  （见上游 `data_processing/timesync/`），即使中途丢了触发也能靠指纹把流重新对齐。
- **统一对齐**：所有传感器收到的是"同一根 PPS 边沿"的派生信号，上游用 `timeSync.py`
  做仿射拟合（Kalman 平滑）把各流重采样到公共时钟。

## 移植到本项目的四点注意

1. **时基不是 GNSS**：这块板用 DS3231 RTC 自由振荡（±2 ppm 量级），没有 GNSS 驯服。
   本项目要求 RTK 厘米级定位 + 微秒级同步，应把 **RTK 模块的 PPS 引脚接到 GPIO16**，
   取代 DS3231 的 SQW；DS3231 只保留 I2C 读时间戳。需要给 DS3231 换更大负载电容或
   恒温晶振，否则它会拖累时间基准。
2. **触发沿是软件产生的**：`digitalWriteFast` 在 ISR 里执行，抖动在几 µs 量级，
   达不到"硬件捕获"的亚 µs。若要更硬的指标，应把触发改成定时器的**输出比较**（硬件），
   用一路输入捕获把 PPS 边沿锁存进同一个定时器的时间基准，再由比较单元产生触发。
3. **曝光延迟仍需标定**：触发到相机实际曝光中心之间有固定延迟（可见光与红外不同），
   雷达另走 PPS+NMEA 自守时。这三段固定延迟必须逐路实测后写进时间戳修正。
4. **J1 的脚序以 PCB 为准**：仓库自带的 `SyncBoard.pdf` 是较早版本，标注文本与当前
   PCB 布线存在出入（例如 GND 所在脚位）。以 `SyncBoard.kicad_pcb` 提取出的网络表为准。

## 参考设计二：gps-timestamper（**仅供阅读，GPL-3.0，未拷入本仓库**）

[DAlexis/gps-timestamper](https://github.com/DAlexis/gps-timestamper) —— STM32F407VGTx，
用 GNSS PPS 给**外部模拟/TTL 触发信号打时间戳**，标称精度约 1 µs。与本项目"外部 MCU
做时钟同步"的想法最接近，硬件是 KiCad 5 传统格式（`schematic/v1/gps-timestamper.sch`）。

从其 `gps-timestamper.net` 网络表读出的结构：

| 器件 | 作用 |
|---|---|
| U8 STM32F407VGTx | 主控；`PPS_IN` 接 J1.8（GPS 模块 1PPS），`TTL_IN` 接 U8.23（外部触发的捕获脚） |
| U9 74HC04 | 反相/缓冲：把 PPS 与触发信号整形成适合捕获的电平，J3 为 PPS_OUT 输出 |
| U1/U2/U4/U5/U6/U7/U10 MCP633 | 7 片 8 通道模拟开关，做**多路触发的模拟多路复用**，被测信号经 `Ch0_to_amp`/`Ch1_to_amp` 送到 MCU 的 ADC 通道 |
| U3 MCP1826S + L1 | 3V3 LDO + 铁氧体滤波（模拟/数字分区供电，GNDA/GNDS/GND 三套地） |
| J1 GPS / J2 Input / J3 PPS_OUT / J4 USB OTG / J5 SWD | 接口 |

设计要点：**PPS 与被测触发都走同一条模拟链**，MCU 用 ADC 采样被测信号相对 PPS
锁存时刻的偏移，避免软件中断抖动，这是它能做到 ~1 µs 的原因。
拿它对照本项目：四路传感器触发应尽量走"同一根 PPS 派生 + 硬捕获"的路子。

⚠️ 该项目为 **GPL-3.0**，本仓库只放 OctoSense（MIT）的文件。若要把它的固件并入本项目，
需按 GPL 处理；仅阅读硬件设计思路没有这个问题。

## 参考设计三：EverySync / VersaVIS（Altium，非本仓库收录）

[NEU-REAL/EverySync-Hardware-Suite](https://github.com/NEU-REAL/EverySync-Hardware-Suite)（IROS 2024，
<1 ms）用的是 Altium 工程（`EverySync_AD/*.SchDoc.schdoc` + `EverySyncBoard.PcbDoc.pcbdoc`），
另有 Altium 导出的 JSON 便于程序化解析。主控为 SAMD21，与 VersaVIS 同源。
优点是硬件/固件/软件全开放；缺点是工具链不是 KiCad，且精度标称只有亚毫秒，
离本项目要求还差一个量级。

## 许可证

上游 OctoSense 为 MIT（见 `LICENSE.octosense`）。若把 `pps_clock.ino` 改写后并入本项目，
需保留原版权声明。注意这与 InfiniteSenseSDK（GPL-3.0）不同，后者只能当参考不能直接拷代码。
