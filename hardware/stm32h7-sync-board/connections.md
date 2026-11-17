# 同步板连接表

> 由 `tools/gen_connections.py` 生成，请勿手工编辑。
> MCU 一侧用功能名（`PA0 / TIM5_CH1`）标注；封装引脚号由 CubeMX 决定。

## 网络

| 网络 | 类型 | 连接 | 说明 |
| --- | --- | --- | --- |
| `GND` | power | U1.GND, U2.GND, U3.GND, U4.GND, U5.GND, U6.4, U7.GND, U8.GND, U9.GND, J1.5, J1.6, J2.6, J2.7, J2.8, J3.5, J4.2, J4.4, J5.4, J6.2, J7.6, J8.2, SW1.2, D3.2, C7.2, C8.2, C9.2, C10.2, C12.2, C13.1, L1.2, FB1.2, R6.1, D5.2, D5.5, D5.6, J10.7, J10.8, J10.9, J10.10, J10.15, J10.16, J10.17, J10.20, J10.21, J10.31, J10.32, J10.36, J10.37, J10.40, J10.44 | 主地：电机噪声环境下建议与模拟地在单点连接 |
| `+5V` | power | J2.5, J8.1, U8.1, U6.14, D1.1, D2.1, J10.1, J10.2 | 由外部 5 V 输入经 L1 滤波（载板 mezzanine 从 J10.1/2 供 5 V） |
| `+3V3` | power | U1.VDD, U2.5, U4.5, U5.5, U7.4, J1.4, J5.1 | MCU 数字电源，来自 U8 |
| `+3V3A` | power | U9.1, J6.1 | RTC/模拟电源，与 +3V3 经 FB1 分隔 |
| `VIN` | power | L1.1, R6.2, C12.1 | 外部输入 |
| `PPS_RAW` | signal | J10.38, J1.1, R1.1, D5.1 | RTK 模块的 1PPS 输出 |
| `PPS_FILT` | signal | R1.2, R2.1, D5.3 | RC 低通 1k+100pF |
| `PPS_IN` | signal | R2.2, U2.1 | 施密特整形输入 |
| `PPS_MCU` | signal | U2.6, U1.PA0 / TIM5_CH1 | PPS 捕获，10 ns 计数分辨率 |
| `GPS_RX_MCU` | signal | J10.39, J1.2, U1.PA3 / USART2_RX | RTK 的 NMEA 输入（比 USB 走 NTRIP 更省事） |
| `GPS_TX_MCU` | signal | J1.3, U1.PD5 / USART2_TX | 配置命令输出 |
| `TRIG0_MCU` | signal | U1.PA1 / TIM2_CH1 | 可见光相机触发 |
| `TRIG1_MCU` | signal | U1.PC6 / TIM3_CH1 | 雷达触发 |
| `TRIG2_MCU` | signal | U1.PB6 / TIM4_CH1 | IMU 触发 |
| `TRIG3_MCU` | signal | U1.PA8 / TIM1_CH1 | 红外相机触发 |
| `TRIG0_BUF` | signal | U4.1, U6.2 | 3.3 V 与 5 V 两路缓冲输入 |
| `TRIG1_BUF` | signal | U4.3, U6.3 |  |
| `TRIG2_BUF` | signal | U5.1, U6.5 |  |
| `TRIG3_BUF` | signal | U5.3, U6.6 |  |
| `TRIG0_5V` | signal | U6.9, J9.1 | 可选 5 V 触发输出 |
| `TRIG1_5V` | signal | U6.8, J9.2 |  |
| `TRIG2_5V` | signal | U6.11, J9.3 |  |
| `TRIG3_5V` | signal | U6.13, J9.4 |  |
| `TRIG0_OUT` | signal | J10.27, U4.2, J2.1 |  |
| `TRIG1_OUT` | signal | J10.28, U4.4, J2.2 |  |
| `TRIG2_OUT` | signal | J10.29, U5.2, J2.3 |  |
| `TRIG3_OUT` | signal | J10.30, U5.4, J2.4 |  |
| `EXT_IN_RAW` | signal | J10.34, J4.1, R3.1, D5.4 |  |
| `EXT_IN_FILT` | signal | R3.2, R4.1 |  |
| `EXT_IN_MCU` | signal | R4.2, U2.2, U1.PA7 / TIM3_CH2 | 外部触发输入捕获，用于标定/复现 |
| `STATUS_OUT` | signal | J10.35, U1.PB0, J4.3 | 触发运行状态输出，可同步到主机 GPIO |
| `HOST_TX_MCU` | signal | J10.13, U1.PA9 / USART1_TX, U3.4 |  |
| `HOST_RX_MCU` | signal | J10.12, U1.PA10 / USART1_RX, U3.1 |  |
| `HOST_RXD` | signal | J3.1, U3.3 | 板子收到的方向 |
| `HOST_TXD` | signal | J3.2, U3.6 | 板子发出的方向 |
| `RS485_A` | signal | J3.3, U3.7 |  |
| `RS485_B` | signal | J3.4, U3.8 |  |
| `I2C1_SCL` | signal | J10.18, U1.PB8 / I2C1_SCL, J6.4, R7.1 |  |
| `I2C1_SDA` | signal | J10.19, U1.PB9 / I2C1_SDA, J6.3, R8.1 |  |
| `I2C2_SCL` | signal | U1.PB10 / I2C2_SCL, U7.6, R9.1 |  |
| `I2C2_SDA` | signal | U1.PB11 / I2C2_SDA, U7.5, R10.1 |  |
| `HSE_IN` | signal | Y1.1, C1.1, U1.PH0 / OSC_IN |  |
| `HSE_OUT` | signal | Y1.2, C2.1, U1.PH1 / OSC_OUT |  |
| `LSE_IN` | signal | Y2.1, C3.1, U1.PC14 / OSC32_IN |  |
| `LSE_OUT` | signal | Y2.2, C4.1, U1.PC15 / OSC32_OUT |  |
| `VBAT` | power | BT1.1, D4.1, C15.1, U1.VBAT |  |
| `VBAT_CHG` | power | D4.2, R5.1 | 防反充二极管 |
| `USB_D_P` | signal | U1.PA12 / USB_OTG_FS_D+, J7.3 |  |
| `USB_D_N` | signal | U1.PA11 / USB_OTG_FS_D-, J7.2 |  |
| `USB_CC1` | signal | J7.4, R11.1 |  |
| `USB_CC2` | signal | J7.5, R12.1 |  |
| `USB_VBUS` | power | J7.1, C14.1, U1.USB_VBUS |  |
| `SWDIO` | signal | U1.PA13 / SWDIO, J5.2 |  |
| `SWCLK` | signal | U1.PA14 / SWCLK, J5.3 |  |
| `NRST` | signal | J10.22, U1.NRST, SW1.1, C5.1 |  |
| `BOOT0` | signal | SW2.1, R5.2, U1.BOOT0 |  |
| `LED_LOCK` | signal | U1.PC0, D1.2 | PPS 锁定指示 |
| `LED_TRIG` | signal | U1.PC1, D2.2 |  |
| `MEZZ_TRIG_EN` | signal | J10.14, U1.PB1 | 载板软件使能硬件触发（低有效由固件定义） |
| `MEZZ_PPS_LOCKED` | signal | J10.23, U1.PC2 | 同步板回报 PPS 锁定状态（→ 载板 → 模块 GPIO） |
| `MEZZ_TRIG_STAT` | signal | J10.24, U1.PC3 | 触发运行状态（→ 载板 → 模块 GPIO） |
| `MEZZ_CAM_VSYNC` | signal | J10.25, U1.PC4 | 相机 VSYNC 回读（确认硬件触发真的打到了） |
| `MEZZ_CAM_TIMER` | signal | J10.26, U1.PC5 | 相机 TIMER_SYNC 回读 |
| `MEZZ_AUX4` | signal | J10.33, U1.PB5 | 辅助 IO |
| `MEZZ_AUX5` | signal | J10.41, U1.PB7 | 辅助 IO |
| `MEZZ_AUX6` | signal | J10.42, U1.PA4 | 辅助 IO |
| `MEZZ_AUX7` | signal | J10.43, U1.PA5 | 辅助 IO |

## 器件

| 位号 | 型号 | 封装 | MPN | 说明 |
| --- | --- | --- | --- | --- |
| U1 | STM32H743VGTx | `Package_QFP:LQFP-100_14x14mm_P0.5mm` | STM32H743VGT6 | 主控，480 MHz |
| U2 | 74LVC2G17 | `Package_TO_SOT_SMD:SOT-23-5` | SN74LVC2G17DBVR | 双施密特触发器：PPS 与外部触发的整形 |
| U3 | SN65HVD75 | `Package_SO:SOIC-8_3.9x4.9mm_P1.27mm` | SN65HVD75DR | RS-485 收发器，主机长线用（可 DNP） |
| U4 | 74LVC2G34 | `Package_TO_SOT_SMD:SOT-23-5` | SN74LVC2G34DBVR | 触发输出缓冲 1/2（3.3 V） |
| U5 | 74LVC2G34 | `Package_TO_SOT_SMD:SOT-23-5` | SN74LVC2G34DBVR | 触发输出缓冲 2/2（3.3 V） |
| U6 | 74HCT125 | `Package_SO:SOIC-14_3.9x8.7mm_P1.27mm` | SN74HCT125DR | 触发输出缓冲（5 V），经跳线与 3.3 V 版本二选一 |
| U7 | AT24C02 | `Package_TO_SOT_SMD:SOT-23-6` | AT24C02D-SSHM | 配置存储（I2C2） |
| U8 | MCP1700-3302 | `Package_TO_SOT_SMD:SOT-23-3` | MCP1700T-3302E/TT | MCU 3.3 V LDO |
| U9 | AP2112K-3.3 | `Package_TO_SOT_SMD:SOT-23-5` | AP2112K-3.3TRG1 | RTC/模拟部分 3.3 V LDO（与 MCU 电源隔离） |
| Y1 | 25MHz | `Crystal:Crystal_SMD_3225-4Pin_3.2x2.5mm` | ABM8G-25.000MHZ | HSE 主晶振 |
| Y2 | 32.768kHz | `Crystal:Crystal_SMD_3215-4Pin_3.2x1.5mm` | ABS07-32.768KHZ-T | LSE rtc 晶振 |
| BT1 | CR1220 | `Battery:BatteryHolder_Keystone_3000_1x12mm` | 3000 | RTC 备份电池 |
| J1 | Conn_01x06 | `Connector_Molex:Molex_Micro-Fit_3.0_43045-1006_1x06_P3.00mm_Vertical` | 43045-1006 | GNSS/RTK 模块接口 |
| J2 | Conn_01x08 | `Connector_Molex:Molex_Micro-Fit_3.0_43045-1108_1x08_P3.00mm_Vertical` | 43045-1108 | 四路触发输出 + 电源 |
| J3 | Conn_01x06 | `Connector_JST:JST_XH_B6B-XH-A_1x06_P2.50mm_Vertical` | B6B-XH-A | 主机通信（UART / RS-485） |
| J4 | Conn_01x04 | `Connector_JST:JST_XH_B4B-XH-A_1x04_P2.50mm_Vertical` | B4B-XH-A | 外部触发输入 + 状态输出 |
| J5 | Conn_01x04 | `Connector_PinHeader:PinHeader_1x04_P2.54mm_Vertical` |  | SWD 调试 |
| J6 | Conn_01x04 | `Connector_JST:JST_XH_B4B-XH-A_1x04_P2.50mm_Vertical` | B4B-XH-A | DS3231 RTC 模块 |
| J7 | Conn_USB:USB_C_Receptacle_GCT_USB4105-xx-A_1x06_P2.50mm_Vertical | `USB4105-xx-A` | USB-C 调试口（CDC） |  |
| J8 | Conn_01x02 | `Connector_PinHeader:PinHeader_1x02_P2.54mm_Vertical` |  | 电源输入 |
| J9 | Conn_01x04 | `Connector_PinHeader:PinHeader_1x04_P2.54mm_Vertical` |  | 可选 5 V 触发输出（U6 未贴时为空） |
| J10 | Conn_02x22 | `Connector_PinHeader:PinHeader_2x22_P2.00mm_Vertical` |  | 载板 mezzanine：与 orin-nx-carrier 的 J12 一一对应 |
| SW1 | SW_Push | `Button_Switch_SMD:SW_SPST_SKRPACE010` |  | 复位 |
| SW2 | SW_Push | `Button_Switch_SMD:SW_SPST_SKRPACE010` |  | BOOT0 |
| D1 | LED_Green | `LED_SMD:LED_0603_1608Metric` |  | PPS 锁定 |
| D2 | LED_Yellow | `LED_SMD:LED_0603_1608Metric` |  | 触发运行 |
| D3 | LED_Red | `LED_SMD:LED_0603_1608Metric` |  | 掉 PPS / 故障 |
| FB1 | Ferrite_600R@100MHz | `Inductor_SMD:L_0603_1608Metric` | BLM18PG601SN1D | 模拟/数字分区 |
| L1 | Ferrite_600R@100MHz | `Inductor_SMD:L_0603_1608Metric` | BLM18PG601SN1D | 输入电源滤波 |
| R1 | 1k | `Resistor_SMD:R_0603_1608Metric` |  | PPS 限流 |
| R2 | 100pF | `Capacitor_SMD:C_0603_1608Metric` |  | PPS 低通（fc≈1.6 MHz） |
| R3 | 1k | `Resistor_SMD:R_0603_1608Metric` |  | EXT 输入限流 |
| R4 | 100pF | `Capacitor_SMD:C_0603_1608Metric` |  | EXT 输入低通 |
| R5 | 10k | `Resistor_SMD:R_0603_1608Metric` |  | BOOT0 下拉 |
| R6 | 0R | `Resistor_SMD:R_0603_1608Metric` |  | 电源选择 |
| R7 | 10k | `Resistor_SMD:R_0603_1608Metric` |  | I2C1 上拉（模块自带则 DNP） |
| R8 | 10k | `Resistor_SMD:R_0603_1608Metric` |  | I2C1 上拉 |
| R9 | 2.2k | `Resistor_SMD:R_0603_1608Metric` |  | I2C2 上拉 |
| R10 | 2.2k | `Resistor_SMD:R_0603_1608Metric` |  | I2C2 上拉 |
| R11 | 4.7k | `Resistor_SMD:R_0603_1608Metric` |  | USB CC 下拉 |
| R12 | 4.7k | `Resistor_SMD:R_0603_1608Metric` |  | USB CC 下拉 |
| C1 | 22pF | `Capacitor_SMD:C_0603_1608Metric` |  | Y1 负载电容 |
| C2 | 22pF | `Capacitor_SMD:C_0603_1608Metric` |  | Y1 负载电容 |
| C3 | 6.8pF | `Capacitor_SMD:C_0603_1608Metric` |  | Y2 负载电容 |
| C4 | 6.8pF | `Capacitor_SMD:C_0603_1608Metric` |  | Y2 负载电容 |
| C5 | 100nF | `Capacitor_SMD:C_0603_1608Metric` |  | U1 去耦 |
| C6 | 100nF | `Capacitor_SMD:C_0603_1608Metric` |  | U1 去耦 |
| C7 | 4.7uF | `Capacitor_SMD:C_0805_2012Metric` |  | U8 输入 |
| C8 | 10uF | `Capacitor_SMD:C_0805_2012Metric` |  | U8 输出 |
| C9 | 100nF | `Capacitor_SMD:C_0603_1608Metric` |  | U9 输入 |
| C10 | 10uF | `Capacitor_SMD:C_0805_2012Metric` |  | U9 输出 |
| C11 | 100nF | `Capacitor_SMD:C_0603_1608Metric` |  | U7 去耦 |
| C12 | 4.7uF | `Capacitor_SMD:C_0805_2012Metric` |  | 输入电源储能 |
| C13 | 100nF | `Capacitor_SMD:C_0603_1608Metric` |  | USB 旁路 |
| C14 | 4.7uF | `Capacitor_SMD:C_0805_2012Metric` |  | USB VBUS 去耦 |
| C15 | 1uF | `Capacitor_SMD:C_0603_1608Metric` |  | VBACKUP |
| D4 | 1N4148W | `Diode_SMD:D_SOD-323` |  | 电池防反充 |
| D5 | ESD_Array | `Diode_SMD:D_SOT-23-6` | TPD6E05U06RVZR | 外部接口 ESD |
