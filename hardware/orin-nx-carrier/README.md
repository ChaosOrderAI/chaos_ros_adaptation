# Orin NX 载板（自研）+ STM32H7 同步板集成

在自研的 Orin NX 载板上，把 [STM32H7 多传感器同步板](../stm32h7-sync-board/)
作为 mezzanine（板对板子板）集成进来，并顺手把之前讨论的 **5G 走 PCIe**、
**RTK 走独立串口**、**PoE 网口** 都落到这块板上。

设计目标一句话：**载板负责供电、接口、时间分发；同步板负责微秒级触发；
两者之间的连接只有一根 UART 和几根 GPIO，复杂时序全在同步板内部解决。**

## 目录

| 路径 | 内容 |
|---|---|
| [connections.md](connections.md) | 完整连接表（155 网络 / 65 器件），由脚本生成 |
| [bom.csv](bom.csv) | BOM |
| [pins_full.json](pins_full.json) | 模块 260 脚的引脚号 + 信号名（来自数据手册 Pin List 表） |
| [pins_orin_nx.json](pins_orin_nx.json) | 从各接口表解析的引脚（含方向/电压域），用于交叉验证 |
| [tools/extract_pinlist.py](tools/extract_pinlist.py) | 从数据手册 PDF 文本解析 Pin List 表 |
| [tools/extract_orin_pins.py](tools/extract_orin_pins.py) | 从数据手册 PDF 文本解析各接口表 |
| [tools/gen_connections.py](tools/gen_connections.py) | 连接表的单一数据源；`--check` 校验自洽性 |
| [tools/check_mezzanine.py](tools/check_mezzanine.py) | 校验载板 J12 与同步板 J10 逐脚对应 |
| [dts/orin-nx-carrier-sync.dtsi](dts/orin-nx-carrier-sync.dtsi) | 设备树叠加：载板 EEPROM、PPS、同步板串口设备 |
| [../stm32h7-sync-board/](../stm32h7-sync-board/) | 同步板（J10 就是它那一侧的 mezzanine 口） |

引脚来源：ConnectTech《Jetson Orin NX Data Sheet》DS-10712-001
（<https://connecttech.com/ftp/pdf/jetson_orin_nx_datasheet.pdf>）。
260 个引脚由 `extract_pinlist.py` 从 "Jetson Orin NX Pin List" 表解析，
再与各接口表解析结果**逐脚比对**，两边一致才允许进入连接表；
`gen_connections.py --check` 还会再验一遍"网络声明的信号前缀 vs 数据手册引脚名"。

```bash
cd hardware/orin-nx-carrier
python3 tools/gen_connections.py --check   # 连接表自洽
python3 tools/gen_connections.py            # 重新生成 connections.md / bom.csv
python3 tools/check_mezzanine.py            # 与同步板 J10 逐脚对上
```

## 模块资源怎么分的

Orin NX 有 4 个 PCIe 控制器、7 条 lane、3 组 USB 3.2、4 路 CSI、1 路千兆
PHY（带 PHY 不带变压器）。本板的分配：

| 模块资源 | 引脚 | 去向 |
|---|---|---|
| PCIe Ctrl#4（x4） | 131–157 + 160/162/180/181 | **J4** M.2 Key M → NVMe SSD |
| PCIe Ctrl#7（x2） | 40–66 + 52/54/219/221 | **J5** M.2 Key B → **5G 模组**（数据面走 PCIe） |
| PCIe Ctrl#1（x1） | 167–175 + 182/183 | **J6** M.2 Key E → 预留（WiFi/BT） |
| PCIe Ctrl#9 | 227/229（只有时钟） | 未用 |
| USBSS0 / USB0 | 161–168 / 109/111 | **J7** USB-C #1 → **Orbbec Gemini 335** |
| USBSS1 / USB1 | 39–47 / 115/117 | **J8** USB-C #2 |
| USBSS2 / USB2 | 51–59 / 121/123 | **J9** USB-C #3（仅 SS）；USB2 给 5G 模组做控制通道 |
| CSI0 / CAM0 | 4–18 / 114/116 | **J10** 22-pin CSI 相机座 #1 |
| CSI2 / CAM1 | 22–36 / 120/122 | **J11** 22-pin CSI 相机座 #2 |
| CSI1 / CSI3 | 3–35 | 未用（GMSL 方案需要的话换去解串器） |
| GBE_MDI0–3 | 184–204 | **U3** H110F 变压器 → **J3** RJ45（PoE PD） |
| I2C0 | 185/187 | **U9** 24AA01 载板 EEPROM（@0x50，Orin 靠它认载板） |
| I2C1 / I2C2 | 189/191 / 232/234 | I2C1 → 同步板；I2C2 空闲（同步板的 I2C2 是它自己的 AT24C02） |
| UART0 | 99/101 | **J17** 调试控制台（ttyTHS0） |
| UART1 | 203/205 | **同步板**主链路（经 U4 电平转换，921600 8N1） |
| UART2 | 236/238 | J17 备用串口 |
| SPI1 | 104/106/108 | 未使用（这条链路的时序不依赖它） |
| CAN | 143/145 | J15/J16（模块 CAN 是 **3.3 V 域**，接隔离收发器时注意） |
| GPIO00–GPIO14 | 87/118/124/126/127/128/130/206/208/211/212/216/218/228/230 | 见下表 |

### GPIO 分配

| 引脚 | 信号 | 作用 |
|---|---|---|
| 87 (GPIO00) | `PPS_18` | **RTK 的 PPS**（整形后经 U16 转 1.8 V）→ `pps-gpio` → `chrony` 驯服系统时钟 |
| 118 (GPIO01) | `SYNC_TRIG_EN` | 软件使能同步板的硬件触发（同步板复位用载板上的 SW3 手动做） |
| 124 (GPIO02) | `GPIO_PPS_LOCKED` | 同步板回报 PPS 锁定状态（点亮 D2） |
| 126 (GPIO03) | `GPIO_FAN_PWM` | 风扇 PWM 调速 |
| 127 (GPIO04) | `GPIO_PWR_BTN` | 电源键输入 → U10 时序控制器 |
| 128 (GPIO05) | `GPIO_LED_RUN` | 运行指示灯 |
| 130 (GPIO06) | `GPIO_5G_RESET_N` | 5G 模组复位（经 Q2） |
| 206 (GPIO07) | `GPIO_5G_PWRKEY` | 5G 模组 PWRKEY（经 Q1） |
| 208 (GPIO08) | `GPIO_FAN_TACH` | 风扇转速 |
| 211/216/228/230 | `GPIO_AUX1..4` | 备用 |

## 电源树与上电时序

```
J2 (12–20 V) ─F1 3A─ D4 TVS(33V) ─ D5 防反接 ─┬─ VIN_PROT ─┬─ J1.251..260  VDD_IN（10 脚并联，直通）
                                             │            ├─ U1 TPS54560 buck → +5V/5A
                                             │            ├─ U12 TPS54302 buck → +3V3_5G（5G 专用 3A）
                                             │            └─ U10 TPS3808 UVLO/时序
                                             └─ U2 TPS23753 PoE PD ← J3
+5V ─ U11 TPS7A4700 LDO → +3V3（信号域，噪声低）
+3V3 / +5V → J12（同步板）
```

要点：

- **Orin NX 的 VDD_IN 是 5–20 V**（`MODULE_ID` 在 20 V 机型上模块内已悬空，
  载板不要接）。但 MAXN_SUPER 模式 40 W 时必须 ≥8 V，所以本板定 12–20 V 输入。
- **上电顺序**按 NVIDIA DG-10931-001：3.3 V IO 域 ≤0.5 V / 1.5 ms 内完成，
  1.8 V 域 ≤0.5 V / 4 ms 内完成，`POWER_EN` → `SYS_RESET*` 间隔约 80 ms。
  U10 负责 UVLO 监控并输出 `POWER_EN`，U13（74LVC1G125 施密特缓冲）驱动
  `POWER_EN`——**这个脚必须是缓冲驱动，不能用普通 GPIO 直连**。
- **SYS_RESET\*** 是开漏 1.8 V：模块上电时序完成后由模块自己拉高表示
  power good，载板只负责在需要时拉低复位。
- **SHUTDOWN_REQ\*** 是开漏 @VDD_IN：模块请求关机时拉低，载板据此切断
  5V/3V3_5G 等下游负载。
- 5G 模组的 3.3 V 用**独立 buck**（U12），不要挂在信号 3.3 V 上：
  5G 发射瞬间电流波动大，会把同步板的 3.3 V 拉出毛刺。
- `PMIC_BBAT`（pin 235）接 **J19** 一次性锂电池（1.75–5.5 V，不可充电），
  断电后模块 RTC 保持时间。

## 同步板集成（mezzanine J12）

J12 是 2×22 排针，同步板以 mezzanine 方式插上去。分工原则：
**载板只做电源、电平转换和连线，时序一律留在同步板的定时器里。**

| J12 脚 | 载板网络 | 同步板 J10 | 方向 | 说明 |
|---|---|---|---|---|
| 1, 2 | `+5V` | `+5V` | 电源 | 同步板主供电（同步板自带 LDO 出 3.3 V） |
| 3, 4 | `+3V3` | — | 备用 | 同步板不接（自带 3.3 V） |
| 5, 6 | `+1V8` | — | 备用 | 同步板 IO 是 3.3 V |
| 7–10 | `GND` | `GND` | 地 | |
| 11 | `CLK_32K` | — | 备用 | 同步板用本地 32.768 kHz 晶振 |
| 12 | `UART_TXD` | `HOST_RX_MCU` | 模块 → 同步板 | 921600 8N1 帧协议 |
| 13 | `UART_RXD` | `HOST_TX_MCU` | 同步板 → 模块 | |
| 14 | `SYNC_TRIG_EN` | `MEZZ_TRIG_EN` | 模块 → 同步板 | 软件使能硬件触发 |
| 15–17 | `GND` | `GND` | 地 | 原计划的 SPI 备用脚，已接地 |
| 18 | `I2C1_SCL` | `I2C1_SCL` | 双向 | |
| 19 | `I2C1_SDA` | `I2C1_SDA` | 双向 | |
| 20, 21 | `GND` | `GND` | 地 | |
| 22 | `SYNC_RESET_N` | `NRST` | 载板 SW3 → 同步板 | 直接接同步板复位脚（无电平问题） |
| 23 | `GPIO_PPS_LOCKED` | `MEZZ_PPS_LOCKED` | 同步板 → 模块 | 1.8 → 3.3 经 U6 |
| 24 | `GPIO_TRIG_STAT` | `MEZZ_TRIG_STAT` | 同步板 → 模块 | 1.8 → 3.3 经 U6 |
| 25 | `CAM_ECHO_VSYNC` | `MEZZ_CAM_VSYNC` | 相机 → 同步板 | 1.8 → 3.3 经 U7 |
| 26 | `CAM_ECHO_TIMER` | `MEZZ_CAM_TIMER` | 相机 → 同步板 | 1.8 → 3.3 经 U7 |
| 27–30 | `SYNC_TRIG0..3` | `TRIG0..3_OUT` | 同步板 → 外部 | 3.3 V，同时引到 J21 线束座 |
| 31, 32 | `GND` | `GND` | 地 | |
| 33 | `GPIO_AUX4` | `MEZZ_AUX4` | 模块 → 同步板 | |
| 34 | `SYNC_EXT_IN` | `EXT_IN_RAW` | 外部 → 同步板 | |
| 35 | `SYNC_EXT_ECHO` | `STATUS_OUT` | 同步板 → 载板 | |
| 36, 37 | `GND` | `GND` | 地 | |
| 38 | `PPS_CLEAN` | `PPS_RAW` | RTK → 同步板 | 整形后的 1PPS |
| 39 | `NMEA_33` | `GPS_RX_MCU` | RTK → 同步板 | NMEA |
| 40 | `GND` | `GND` | 地 | |
| 41–43 | `SYNC_AUX1..3` | `MEZZ_AUX5..7` | 双向 | 备用 IO |
| 44 | `GND` | `GND` | 地 | |

这 44 脚是**逐脚对应**的，由 `tools/check_mezzanine.py` 校验：载板改了一处而
同步板没改，脚本立刻报错。

**电平转换**：模块 IO 域是 1.8 V，同步板 IO 是 3.3 V，中间夹三个转换器：

| 位号 | 器件 | 方向 | 说明 |
|---|---|---|---|
| U4 | TXS0102 | 双向 | UART1（1.8 ↔ 3.3），通道 1/2 |
| U6 | TXS0104 | 1.8 → 3.3 | 四路状态 GPIO：PPS 锁定 / 触发状态 / 触发使能 / AUX |
| U5 | 74AVC4T245 | 单向 A→B | 四路触发输出 3.3 → 1.8（DIR 脚固定接 A→B） |
| U7 | TXS0102 | 1.8 → 3.3 | 相机同步座回读（VSYNC_OUT / TIMER_SYNC_OUT） |
| U16 | SN74LVC2T45 | 双向 | PPS 3.3 → 1.8 给模块；模块 UART2 TX 1.8 → 3.3 给 RTK |
| U8 / U17 | 74LVC1G17 | — | RTK 的 PPS / NMEA 施密特整形（3.3 V） |

注意 TXS 系列的限制：需要两侧都有上拉，速度适合 UART（921600 没问题），
**不适合高速 SPI**（>5 MHz 会不稳）。所以这条载板↔同步板链路只走 UART +
几个低速状态 GPIO，高速数据不经过它。

## RTK 的接法

RTK 接收机通过 **J14**（1×6）接进来，是整条时间链的源头：

| J14 脚 | 网络 | 说明 |
|---|---|---|
| 1 | `RTK_5V` | 接收机供电 |
| 2 | GND | |
| 3 | `RTK_PPS` | 1PPS（3.3 V TTL）→ U8 施密特整形 |
| 4 | `RTK_NMEA` | NMEA 输出（3.3 V TTL）→ U17 整形 |
| 5 | `NMEA_TXD_33` | 模块配置 RTK（经 U16 从 1.8 V 转 3.3 V） |
| 6 | — | 备用 |

整形后的 PPS 一路给同步板（J12.38 → `PPS_RAW`），一路经 U16 转成 1.8 V 给
模块的 GPIO00（pin 87），再由 `pps-gpio` → `/dev/pps0` → `chrony`。

NMEA 一路给同步板（J12.39），另一路经 R8/R9 分压成 1.65 V 给模块的
UART2（pin 238），这样 `chrony` 同时拿到 PPS（相位）和 NMEA（UTC 偏移），
开机几十秒内就能把系统时钟拉到亚微秒。

**注意**：模块的 IO 域是 1.8 V、RTK 的电平是 3.3 V，中间必须有转换；
直接连会让 PPS 幅度不够（PPS_RAW 是 3.3 V 施密特输出，1.8 V 输入的
VIH 只有 1.26 V，3.3 V 虽够高但会超出 1.8 V 域的绝对最大值）。

## 相机硬件触发（Gemini 335）

Gemini 335 是 **USB 3.0** 相机，不是 CSI/GMSL；它有一个 8-pin 同步座，
本板用 **J13** 引出：

| J13 脚 | 名称 | 接法 |
|---|---|---|
| 1 | VCC | +5 V（Orbbec 手册里可配 1.8/3.3/5 V，**以所用型号手册为准**） |
| 2 | GPIO_OUT | **本板未接**（U7 只有 2 通道；需要时加一个 TXS0101） |
| 3 | VSYNC_OUT | 相机 → U7 → J12.25 |
| 4 | TIMER_SYNC_OUT | 相机 → U7 → J12.26 |
| 5 | RESET_IN | 模块 GPIO12（pin 218，1.8 V 直连） |
| 6 | VSYNC_IN | U5(B1) ← `SYNC_TRIG0`（1.8 V） |
| 7 | TIMER_SYNC_IN | U5(B2) ← `SYNC_TRIG1`（1.8 V） |
| 8 | GND | 地 |

**触发链**是：同步板 TIM 输出比较 → 3.3 V → U5 转 1.8 V → 相机 VSYNC_IN。
同步板输出的脉冲宽度要 ≥100 µs（Orbbec 的要求），由同步板固件的
`sync_channel_oc_compare()` 配置。

**相机回读**（VSYNC_OUT / TIMER_SYNC_OUT）回到同步板，是为了在软件里
确认每一帧确实被硬件触发打到了——这是排查"触发没生效"最直接的手段。

## 5G 走 PCIe

- 数据面：Ctrl#7 x2 → M.2 Key B（**PCIe x2**，即 USB 3.2/PCIe 双模模组的
  PCIe 模式），内核用 `mbhi` 驱动（Quectel RM520N-GL 等）或 `qmi_wwan`。
- 控制面：USB2_D_N/DP（pin 121/123）→ 模组的 USB 2.0 口，做 AT 拨号、
  固件升级、NMEA/诊断输出。**两条路都接上**，调试时用 USB，量产用 PCIe。
- 电源：`+3V3_5G`（U12，3 A）+ C6/C7 储能；5G 发射脉冲（~2 A 峰值、~100 µs 宽）
  是这块板最大的电源噪声源，U12 的输入电容要靠近模组。
- SIM：J20 SIM 卡座 → J5（模组侧脚位按所选模组的 M.2 定义核对）。

**为什么不走 USB 3.0 做数据面**：5G 的实测峰值在 NSA 下可达 600–900 Mbps，
USB 3.0 (5 Gbps) 理论够，但共享总线时延抖动大、驱动栈更复杂；PCIe x2
(2 Gbps 有效) 足够承载 5G 数据，且在 Orin 上是原生路径。

## 布局与 PI 约束

1. **模块座必须严格按 NVIDIA 参考设计**（J1 的连接器型号、走线、长度匹配、
   去耦、参考层分割全部照抄 DG-10931-001）。这是唯一不能自己发挥的部分。
2. **GND 66 个脚全部接主地**，见 connections.md 的 `GND` 网络。
3. **高速差分对**（PCIe/USB3/CSI）走 100 Ω 差分阻抗、等长、避免过孔换层，
   参考地平面完整、不跨分割。
4. **模拟/敏感信号分区**：PPS、RTK 的差分线、DS3231 的 32 kHz 晶振回路
   放在板的另一侧，远离 5G 模组的电源开关节点与天线馈线。
5. **5G 天线**：M.2 天线出口到 SMA 的走线 50 Ω，**馈线两侧做地过孔围栏**，
   天线下方禁铺铜，天线净空区按模组手册。RF 与数字之间留地隔离带。
6. **散热**：Orin NX 模块是主要热源，靠导热垫压到散热器；5G 模组和
   NVMe 也要单独考虑。风扇由 GPIO03 PWM 调速，8 pin 风扇座 J18。
7. **PoE**：U2（TPS23753）+ 隔离变压器，网络侧与机器人侧地要隔离。
   雷达（Livox）走以太网，如果雷达用 PoE 供电就省一根电源线。

## 时间怎么在这块板上跑起来

```
RTK 接收机 ──1PPS/NMEA──► U8/U17 (施密特整形)
                            ├──► J12.38 ──► 同步板 TIM5 捕获 (10 ns tick)
                            └──► U16 ──► 模块 GPIO00 ──► pps-gpio ──► /dev/pps0 ──► chrony
同步板 TIM5 ──TRGO/ITR0──► 4 个通道定时器 ──► 触发输出 ──► U5 ──► 相机同步座 (<150 ns)
```

两条时间线：

- **系统时钟**：`chrony` 用 `/dev/pps0` 驯服 `CLOCK_REALTIME`，
  这是"墙钟可信"，精度 µs 级。仓库里 `src/chaos_data` 的 `ChronyProbe`
  门禁（`max_uncertainty_ms: 10.0`）就是查这个。
- **传感器对齐**：硬件触发之间 <150 ns，这是"同一时刻"。

**驱动必须用硬件时刻打 `header.stamp`**，否则 USB/以太网传输抖动
（几百 µs 到几 ms）会把硬件同步的收益全部吃掉。做法见
[同步板 README](../stm32h7-sync-board/README.md#与本仓库的衔接)。

设备树见 [dts/orin-nx-carrier-sync.dtsi](dts/orin-nx-carrier-sync.dtsi)：
`pps-gpio` + `pps0` 别名 + 载板 EEPROM + 同步板串口设备节点。

## 已验证 / 未验证

**已验证**（用脚本在本地跑通）：

- 260 个模块引脚的编号与信号名，来源可溯源，且与数据手册各接口表
  逐脚一致；
- 连接表自洽性（`python3 tools/gen_connections.py --check` 一次查完）：
  - 每个引脚只出现在一个网络里；
  - 模块 66 个 GND 脚全部落网，且网络里没有混入非 GND 脚；
  - 每个模块引脚的实际信号名与网络声明的信号前缀相符；
  - 电平转换器的 A/B 通道脚必须在**同一个**网络里（一条线穿过器件，
    拆成两个网就是短路/悬空）；
- 载板 J12 与同步板 J10 的 44 脚**逐脚对应**（`tools/check_mezzanine.py`）：
  任何一边改了而另一边没改，脚本立刻报错。

**未验证**：

- **PCB 本身没有画**。本目录只有连接表、BOM 和设计说明；真正出图之前，
  载板侧器件的**封装脚号要按各器件数据手册替换**（现在用的是功能引脚名），
  M.2 座、USB-C 座、SIM 座的脚号也要按选定型号核对。
- **上电时序波形没实测**。U10/U13 的时序要按 NVIDIA 的
  "Power sequencing" 图逐段量（3.3 V IO / 1.8 V IO / POWER_EN / SYS_RESET）。
- **5G 模组的 M.2 脚位（尤其 REFCLK 与 SIM）要按最终选型核对**。
- **相机同步座的 VCC 电压与触发脉宽要按 Orbbec 具体型号手册确认**。
- 设备树叠加里的 UART/I2C 节点名与 GPIO 宏要在真实 JetPack 上核对。

## 下一步

- [ ] 定 5G 模组型号（RM520N-GL / FM160 等）并把它的 M.2 脚位填进连接表
- [ ] 定 5G 模组的 REFCLK 方案（是否用模块自身晶振还是载板提供 100 MHz）
- [ ] 定 Orin NX 具体 SKU（16GB/32GB、MAXN 功率档）以确定电源裕量
- [ ] 载板侧器件脚号补全 → 导出 KiCad 网表 → 出原理图
- [ ] 布局：先模块座与 PCIe/USB3 高速对，再电源，最后模拟区
- [ ] 设备树与 chrony PPS 实测：验证 `/dev/pps0` 出现、锁定时间 <60 s
