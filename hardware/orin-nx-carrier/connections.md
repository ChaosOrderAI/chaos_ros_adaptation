# Orin NX 载板连接表

> 由 `tools/gen_connections.py` 生成，请勿手工编辑。
> 模块引脚号与信号名来自 `pins_full.json`（Orin NX 数据手册 DS-10712-001 的 Pin List 表，已与各接口表交叉验证）。
> 载板侧器件用功能引脚名，出原理图时按各器件数据手册替换为封装脚号。

## 网络

| 网络 | 类型 | 连接 | 说明 |
| --- | --- | --- | --- |
| `GND` | power | J2.2, F1.2, D5.2, C1.2, C2.2, C3.2, C4.2, C5.2, C6.2, C7.2, L2.2, J7.GND, J8.GND, J9.GND, J12.7, J12.8, J12.9, J12.10, J12.15, J12.16, J12.17, J12.20, J12.21, J12.31, J12.32, J12.36, J12.37, J12.40, J12.44, J10.GND, J11.GND, J13.GND, J15.1, J16.1, J17.GND, J18.GND, J20.GND, U3.GND, U4.GND, U5.GND, U6.GND, U7.GND, U8.GND, U9.GND, U10.GND, U11.GND, U12.GND, U13.GND, Q1.S, Q2.S, SW1.2, SW2.2, SW3.2, D1.2, D3.2, R4.2, R5.2, R6.2, R7.2, R9.2, J1.1 (GND), J1.2 (GND), J1.7 (GND), J1.8 (GND), J1.13 (GND), J1.14 (GND), J1.19 (GND), J1.20 (GND), J1.25 (GND), J1.26 (GND), J1.31 (GND), J1.32 (GND), J1.37 (GND), J1.38 (GND), J1.43 (GND), J1.44 (GND), J1.49 (GND), J1.50 (GND), J1.55 (GND), J1.56 (GND), J1.61 (GND), J1.62 (GND), J1.67 (GND), J1.68 (GND), J1.73 (GND), J1.74 (GND), J1.79 (GND), J1.80 (GND), J1.85 (GND), J1.86 (GND), J1.102 (GND), J1.107 (GND), J1.113 (GND), J1.119 (GND), J1.125 (GND), J1.129 (GND), J1.132 (GND), J1.135 (GND), J1.138 (GND), J1.141 (GND), J1.144 (GND), J1.146 (GND), J1.147 (GND), J1.152 (GND), J1.153 (GND), J1.158 (GND), J1.159 (GND), J1.164 (GND), J1.165 (GND), J1.170 (GND), J1.171 (GND), J1.176 (GND), J1.177 (GND), J1.200 (GND), J1.201 (GND), J1.231 (GND), J1.241 (GND), J1.242 (GND), J1.243 (GND), J1.244 (GND), J1.245 (GND), J1.246 (GND), J1.247 (GND), J1.248 (GND), J1.249 (GND), J1.250 (GND) | 模块 GND 脚共 66 个（见 pins_full.json 的 gnd_pins），必须全部接本网络 |
| `VIN_PROT` | power | J2.1, F1.1, D4.1, D5.1, L1.1, U1.VIN, U10.VIN, U12.VIN, J1.251 (VDD_IN), J1.252 (VDD_IN), J1.253 (VDD_IN), J1.254 (VDD_IN), J1.255 (VDD_IN), J1.256 (VDD_IN), J1.257 (VDD_IN), J1.258 (VDD_IN), J1.259 (VDD_IN), J1.260 (VDD_IN) | 模块 VDD_IN 直通（Orin NX 支持 5–20 V；MAXN_SUPER 40 W 需 ≥8 V） |
| `+5V` | power | U1.VOUT, L1.2, C2.1, C3.1, J7.VBUS, J8.VBUS, J9.VBUS, J12.1, J12.2, J13.1, U11.VIN, U10.VDD, J18.PWR, J19.1 | 载板 5 V 轨（5 A） |
| `+3V3` | power | U11.VOUT, C4.1, U9.VDD, U4.VCCA, U5.VCCA, U6.VCCA, U7.VCCA, U8.VCC, J12.3, J12.4 | 信号域 3.3 V（由 LDO 从 5 V 降压，噪声低） |
| `+3V3_5G` | power | U12.VOUT, L2.1, C6.1, C7.1, J5.3V3 | 5G 模组 3.3 V / 3 A 专用 buck（独立时序，不受信号 3.3 V 影响） |
| `+1V8` | power | U4.VCCB, U5.VCCB, U6.VCCB, U7.VCCB, J12.5, J12.6 | 模块 1.8 V IO 域（电平转换器 B 侧） |
| `POWER_EN` | signal | J1.237 (POWER_EN), U13.A, U10.PG | 模块上电使能（模块侧为 5 V 施密特输入，必须用缓冲器驱动） |
| `SYS_RESET_N` | signal | J1.239 (SYS_RESET), U10.MR, R7.1 | 模块复位（开漏 1.8 V；上电时序完成后由模块拉高表示 power good） |
| `SHUTDOWN_REQ_N` | signal | J1.233 (SHUTDOWN_REQ), U10.SHDN | 模块请求关机（开漏 @VDD_IN，模块内约 5 k 上拉） |
| `SLEEP_WAKE` | signal | J1.240 (SLEEP/WAKE), J17.3 | 休眠/唤醒 |
| `FORCE_RECOVERY_N` | signal | J1.214 (FORCE_RECOVERY), SW2.1 | 强制恢复（按下接地） |
| `MODULE_ID` | signal | J1.217 (MODULE_ID) | 模块内已悬空（20 V 能力机型），载板不要接任何东西 |
| `PMIC_BBAT` | power | J1.235 (PMIC_BBAT), J19.2 | 模块 RTC 备份电池（1.75–5.5 V，不可充电） |
| `CLK_32K` | signal | J1.210 (CLK_32K_OUT), J12.11 | 模块 32 kHz 输出（同步板侧暂不接） |
| `MOD_SLEEP_N` | signal | J1.178 (MOD_SLEEP) | 模块进入 SC7（低有效） |
| `PCIE_WAKE_N` | signal | J1.179 (PCIE_WAKE) | PCIe 唤醒（开漏） |
| `PCIE0_RX0N` | signal | J1.131 (PCIE0_RX0_N), J4.RXP | Ctrl#4 lane0 RX- |
| `PCIE0_RX0P` | signal | J1.133 (PCIE0_RX0_P), J4.RXN | Ctrl#4 lane0 RX+ |
| `PCIE0_TX0N` | signal | J1.134 (PCIE0_TX0_N), J4.TXN | Ctrl#4 lane0 TX- |
| `PCIE0_TX0P` | signal | J1.136 (PCIE0_TX0_P), J4.TXP | Ctrl#4 lane0 TX+ |
| `PCIE0_RX1N` | signal | J1.137 (PCIE0_RX1_N), J4.RXP1 | Ctrl#4 lane1 RX- |
| `PCIE0_RX1P` | signal | J1.139 (PCIE0_RX1_P), J4.RXN1 | Ctrl#4 lane1 RX+ |
| `PCIE0_TX1N` | signal | J1.140 (PCIE0_TX1_N), J4.TXN1 | Ctrl#4 lane1 TX- |
| `PCIE0_TX1P` | signal | J1.142 (PCIE0_TX1_P), J4.TXP1 | Ctrl#4 lane1 TX+ |
| `PCIE0_RX2N` | signal | J1.149 (PCIE0_RX2_N), J4.RXP2 | Ctrl#4 lane2 RX- |
| `PCIE0_RX2P` | signal | J1.151 (PCIE0_RX2_P), J4.RXN2 | Ctrl#4 lane2 RX+ |
| `PCIE0_TX2N` | signal | J1.148 (PCIE0_TX2_N), J4.TXN2 | Ctrl#4 lane2 TX- |
| `PCIE0_TX2P` | signal | J1.150 (PCIE0_TX2_P), J4.TXP2 | Ctrl#4 lane2 TX+ |
| `PCIE0_RX3N` | signal | J1.155 (PCIE0_RX3_N), J4.RXP3 | Ctrl#4 lane3 RX- |
| `PCIE0_RX3P` | signal | J1.157 (PCIE0_RX3_P), J4.RXN3 | Ctrl#4 lane3 RX+ |
| `PCIE0_TX3N` | signal | J1.154 (PCIE0_TX3_N), J4.TXN3 | Ctrl#4 lane3 TX- |
| `PCIE0_TX3P` | signal | J1.156 (PCIE0_TX3_P), J4.TXP3 | Ctrl#4 lane3 TX+ |
| `PCIE0_CLKN` | signal | J1.160 (PCIE0_CLK_N), J4.REFCLK_N | Ctrl#4 100 MHz 参考时钟 |
| `PCIE0_CLKP` | signal | J1.162 (PCIE0_CLK_P), J4.REFCLK_P | Ctrl#4 100 MHz 参考时钟 |
| `PCIE0_RST_N` | signal | J1.181 (PCIE0_RST), J4.PERST_N | Ctrl#4 复位 |
| `PCIE0_CLKREQ_N` | signal | J1.180 (PCIE0_CLKREQ), J4.CLKREQ_N | Ctrl#4 时钟请求 |
| `PCIE2_RX0N` | signal | J1.40 (PCIE2_RX0_N), J5.RXP | Ctrl#7 lane0 → 5G |
| `PCIE2_RX0P` | signal | J1.42 (PCIE2_RX0_P), J5.RXN | Ctrl#7 lane0 → 5G |
| `PCIE2_TX0N` | signal | J1.46 (PCIE2_TX0_N), J5.TXN | Ctrl#7 lane0 → 5G |
| `PCIE2_TX0P` | signal | J1.48 (PCIE2_TX0_P), J5.TXP | Ctrl#7 lane0 → 5G |
| `PCIE2_RX1N` | signal | J1.58 (PCIE2_RX1_N), J5.RXP1 | Ctrl#7 lane1 → 5G |
| `PCIE2_RX1P` | signal | J1.60 (PCIE2_RX1_P), J5.RXN1 | Ctrl#7 lane1 → 5G |
| `PCIE2_TX1N` | signal | J1.64 (PCIE2_TX1_N), J5.TXN1 | Ctrl#7 lane1 → 5G |
| `PCIE2_TX1P` | signal | J1.66 (PCIE2_TX1_P), J5.TXP1 | Ctrl#7 lane1 → 5G |
| `PCIE2_CLKN` | signal | J1.52 (PCIE2_CLK_N), J5.REFCLK_N | 5G REFCLK |
| `PCIE2_CLKP` | signal | J1.54 (PCIE2_CLK_P), J5.REFCLK_P | 5G REFCLK |
| `PCIE2_RST_N` | signal | J1.219 (PCIE2_RST), J5.PERST_N | 5G 复位 |
| `PCIE2_CLKREQ_N` | signal | J1.221 (PCIE2_CLKREQ), J5.CLKREQ_N | 5G 时钟请求 |
| `PCIE1_RX0N` | signal | J1.167 (PCIE1_RX0_N), J6.RXP | Ctrl#1 lane0，预留 |
| `PCIE1_RX0P` | signal | J1.169 (PCIE1_RX0_P), J6.RXN | Ctrl#1 lane0，预留 |
| `PCIE1_TX0N` | signal | J1.172 (PCIE1_TX0_N), J6.TXN | Ctrl#1 lane0，预留 |
| `PCIE1_TX0P` | signal | J1.174 (PCIE1_TX0_P), J6.TXP | Ctrl#1 lane0，预留 |
| `PCIE1_CLKN` | signal | J1.173 (PCIE1_CLK_N), J6.REFCLK_N | Ctrl#1 参考时钟 |
| `PCIE1_CLKP` | signal | J1.175 (PCIE1_CLK_P), J6.REFCLK_P | Ctrl#1 参考时钟 |
| `PCIE1_RST_N` | signal | J1.183 (PCIE1_RST), J6.PERST_N | Ctrl#1 复位 |
| `PCIE1_CLKREQ_N` | signal | J1.182 (PCIE1_CLKREQ), J6.CLKREQ_N | Ctrl#1 时钟请求 |
| `USBSS0_RXN` | signal | J1.161 (USBSS0_RX_N), J7.RXP, J7.RXP_B | USB3 #1 SS |
| `USBSS0_RXP` | signal | J1.163 (USBSS0_RX_P), J7.RXN, J7.RXN_B | USB3 #1 SS |
| `USBSS0_TXN` | signal | J1.166 (USBSS0_TX_N), J7.TXP, J7.TXP_B | USB3 #1 SS |
| `USBSS0_TXP` | signal | J1.168 (USBSS0_TX_P), J7.TXN, J7.TXN_B | USB3 #1 SS |
| `USB0_DN` | signal | J1.109 (USB0_D_N), J7.DN, J7.DN_B | USB3 #1 的 USB2.0 通道 |
| `USB0_DP` | signal | J1.111 (USB0_D_P), J7.DP, J7.DP_B | USB3 #1 的 USB2.0 通道 |
| `USBSS1_RXN` | signal | J1.39 (USBSS1_RX_N), J8.RXP, J8.RXP_B | USB3 #2 SS |
| `USBSS1_RXP` | signal | J1.41 (USBSS1_RX_P), J8.RXN, J8.RXN_B | USB3 #2 SS |
| `USBSS1_TXN` | signal | J1.45 (USBSS1_TX_N), J8.TXP, J8.TXP_B | USB3 #2 SS |
| `USBSS1_TXP` | signal | J1.47 (USBSS1_TX_P), J8.TXN, J8.TXN_B | USB3 #2 SS |
| `USB1_DN` | signal | J1.115 (USB1_D_N), J8.DN, J8.DN_B | USB3 #2 的 USB2.0 通道 |
| `USB1_DP` | signal | J1.117 (USB1_D_P), J8.DP, J8.DP_B | USB3 #2 的 USB2.0 通道 |
| `USBSS2_RXN` | signal | J1.51 (USBSS2_RX_N), J9.RXP, J9.RXP_B | USB3 #3 SS |
| `USBSS2_RXP` | signal | J1.53 (USBSS2_RX_P), J9.RXN, J9.RXN_B | USB3 #3 SS |
| `USBSS2_TXN` | signal | J1.57 (USBSS2_TX_N), J9.TXP, J9.TXP_B | USB3 #3 SS |
| `USBSS2_TXP` | signal | J1.59 (USBSS2_TX_P), J9.TXN, J9.TXN_B | USB3 #3 SS |
| `USB2_DN` | signal | J1.121 (USB2_D_N), J5.USB_DN | 5G 模组 USB 2.0 控制通道 |
| `USB2_DP` | signal | J1.123 (USB2_D_P), J5.USB_DP | 5G 模组 USB 2.0 控制通道 |
| `GBE_MDI0N` | signal | J1.184 (GBE_MDI0_N), U3.TX0N | 千兆变压器 |
| `GBE_MDI0P` | signal | J1.186 (GBE_MDI0_P), U3.TX0P | 千兆变压器 |
| `GBE_MDI1N` | signal | J1.190 (GBE_MDI1_N), U3.TX1N | 千兆变压器 |
| `GBE_MDI1P` | signal | J1.192 (GBE_MDI1_P), U3.TX1P | 千兆变压器 |
| `GBE_MDI2N` | signal | J1.196 (GBE_MDI2_N), U3.TX2N | 千兆变压器 |
| `GBE_MDI2P` | signal | J1.198 (GBE_MDI2_P), U3.TX2P | 千兆变压器 |
| `GBE_MDI3N` | signal | J1.202 (GBE_MDI3_N), U3.TX3N | 千兆变压器 |
| `GBE_MDI3P` | signal | J1.204 (GBE_MDI3_P), U3.TX3P | 千兆变压器 |
| `GBE_LED_LINK` | signal | J1.188 (GBE_LED_LINK), J3.LED_LINK | 网口 LED |
| `GBE_LED_ACT` | signal | J1.194 (GBE_LED_ACT), J3.LED_ACT | 网口 LED |
| `CSI0_D0N` | signal | J1.4 (CSI0_D0_N), J10.D0N | CSI 相机座 #1 |
| `CSI0_D0P` | signal | J1.6 (CSI0_D0_P), J10.D0P | CSI 相机座 #1 |
| `CSI0_D1N` | signal | J1.16 (CSI0_D1_N), J10.D1N | CSI 相机座 #1 |
| `CSI0_D1P` | signal | J1.18 (CSI0_D1_P), J10.D1P | CSI 相机座 #1 |
| `CSI0_CLKN` | signal | J1.10 (CSI0_CLK_N), J10.CLKN | CSI 相机座 #1 |
| `CSI0_CLKP` | signal | J1.12 (CSI0_CLK_P), J10.CLKP | CSI 相机座 #1 |
| `CAM0_PWDN` | signal | J1.114 (CAM0_PWDN), J10.PWDN | 相机下电 |
| `CAM0_MCLK` | signal | J1.116 (CAM0_MCLK), J10.MCLK | 相机参考时钟 |
| `CSI2_D0N` | signal | J1.22 (CSI2_D0_N), J11.D0N | CSI 相机座 #2 |
| `CSI2_D0P` | signal | J1.24 (CSI2_D0_P), J11.D0P | CSI 相机座 #2 |
| `CSI2_D1N` | signal | J1.34 (CSI2_D1_N), J11.D1N | CSI 相机座 #2 |
| `CSI2_D1P` | signal | J1.36 (CSI2_D1_P), J11.D1P | CSI 相机座 #2 |
| `CSI2_CLKN` | signal | J1.28 (CSI2_CLK_N), J11.CLKN | CSI 相机座 #2 |
| `CSI2_CLKP` | signal | J1.30 (CSI2_CLK_P), J11.CLKP | CSI 相机座 #2 |
| `CAM1_PWDN` | signal | J1.120 (CAM1_PWDN), J11.PWDN | 相机下电 |
| `CAM1_MCLK` | signal | J1.122 (CAM1_MCLK), J11.MCLK | 相机参考时钟 |
| `I2C0_SCL` | signal | J1.185 (I2C0_SCL), U9.SCL, R5.1 | 载板 EEPROM：Orin 靠它识别载板（I2C0 @0x50） |
| `I2C0_SDA` | signal | J1.187 (I2C0_SDA), U9.SDA | 载板 EEPROM |
| `I2C1_SCL` | signal | J1.189 (I2C1_SCL), J12.18 | 同步板 / 风扇 / 温度传感器 |
| `I2C1_SDA` | signal | J1.191 (I2C1_SDA), J12.19 | 同步板 / 风扇 / 温度传感器 |
| `I2C2_SCL` | signal | J1.232 (I2C2_SCL) | 模块 I2C2（同步板的 I2C2 是它自己的 AT24C02，不接这里） |
| `I2C2_SDA` | signal | J1.234 (I2C2_SDA) | 模块 I2C2（同步板的 I2C2 是它自己的 AT24C02，不接这里） |
| `UART_TXD` | signal | J1.203 (UART1_TXD), U4.A1, U4.B1, J12.12 | 同步板链路：Orin UART1 TXD → U4 → 同步板 RX（921600 8N1） |
| `UART_RXD` | signal | J1.205 (UART1_RXD), U4.A2, U4.B2, J12.13 | 同步板链路：同步板 TX → U4 → Orin UART1 RXD |
| `CAN_RX` | signal | J1.143 (CAN_RX), J15.CAN_RX, J16.CAN_RX | 模块 CAN 是 3.3 V 域，需与隔离收发器对接 |
| `CAN_TX` | signal | J1.145 (CAN_TX), J15.CAN_TX, J16.CAN_TX | 模块 CAN 是 3.3 V 域 |
| `UART0_TXD_18` | signal | J1.99 (UART0_TXD), J17.1 | 调试控制台（UART0，1.8 V，需电平转换到 USB-UART） |
| `UART0_RXD_18` | signal | J1.101 (UART0_RXD), J17.2 | 调试控制台（UART0） |
| `UART2_TXD_18` | signal | J1.236 (UART2_TXD), U16.A2 | 模块配置 RTK 用（1.8 V → U16 → 3.3 V） |
| `UART2_RXD_18` | signal | J1.238 (UART2_RXD), R8.1 | RTK 的 NMEA（3.3 V 分压到 1.8 V） |
| `PPS_18` | signal | J1.87 (GPIO00), U16.A1 | RTK 的 PPS（整形后 1.8 V）→ 模块 GPIO00，chrony 用它驯服系统时钟 |
| `SYNC_TRIG_EN` | signal | J1.118 (GPIO01), U6.A3, U6.B3, J12.14 | 模块软件使能同步板的硬件触发 |
| `GPIO_PPS_LOCKED` | signal | J1.124 (GPIO02), U6.A1, U6.B1, J12.23, D2.A, R2.1 | 同步板回报 PPS 锁定状态 |
| `GPIO_FAN_PWM` | signal | J1.126 (GPIO03), J18.PWM | 风扇 PWM 调速 |
| `GPIO_PWR_BTN` | signal | J1.127 (GPIO04), U10.PB | 电源键输入 |
| `GPIO_LED_RUN` | signal | J1.128 (GPIO05), D1.A, R1.1 | 运行指示 |
| `GPIO_5G_RESET_N` | signal | J1.130 (GPIO06), Q2.G, J5.RST_N | 5G 模组复位 |
| `GPIO_5G_PWRKEY` | signal | J1.206 (GPIO07), Q1.G, J5.PWRKEY | 5G 模组 PWRKEY |
| `GPIO_FAN_TACH` | signal | J1.208 (GPIO08), J18.TACH | 风扇转速 |
| `GPIO_TRIG_STAT` | signal | J1.212 (GPIO10), U6.A2, U6.B2, J12.24 | 同步板触发运行状态 |
| `GPIO_CAM_RESET_N` | signal | J1.218 (GPIO12), J13.RESET_IN | 相机同步座复位输入 |
| `GPIO_AUX1` | signal | J1.211 (GPIO09), J17.4 | 备用 GPIO |
| `GPIO_AUX2` | signal | J1.216 (GPIO11), J17.5 | 备用 GPIO |
| `GPIO_AUX3` | signal | J1.228 (GPIO13), J17.6 | 备用 GPIO |
| `GPIO_AUX4` | signal | J1.230 (GPIO14), U6.A4, U6.B4, J12.33 | 备用 GPIO（→ 同步板） |
| `SYNC_RESET_N` | signal | SW3.1, J12.22 | 同步板复位（低有效，直接接同步板 NRST） |
| `SYNC_TRIG0` | signal | J12.27, U5.A1, J21.1 | 触发0 → 可见光相机 / 外部线束 |
| `SYNC_TRIG1` | signal | J12.28, U5.A2, J21.2 | 触发1 → 雷达 / 外部线束 |
| `SYNC_TRIG2` | signal | J12.29, U5.A3, J21.3 | 触发2 → IMU / 外部线束 |
| `SYNC_TRIG3` | signal | J12.30, U5.A4, J21.4 | 触发3 → 红外相机 / 外部线束 |
| `TRIG0_18` | signal | U5.B1, J13.VSYNC_IN | 触发0 → 相机 1.8 V |
| `TRIG1_18` | signal | U5.B2, J13.TIMER_SYNC_IN | 触发1 → 相机 1.8 V |
| `TRIG2_18` | signal | U5.B3, J21.5 | 触发2 → 1.8 V 副本（线束） |
| `TRIG3_18` | signal | U5.B4, J21.6 | 触发3 → 1.8 V 副本（线束） |
| `SYNC_EXT_IN` | signal | J12.34 | 外部触发输入 |
| `SYNC_EXT_ECHO` | signal | J12.35 | 外部触发回读 |
| `PPS_CLEAN` | signal | U8.Y, U16.B1, J12.38 | RTK PPS 整形后（3.3 V）：一份给同步板，一份经 U16 转 1.8 V 给模块 |
| `NMEA_33` | signal | U17.Y, J12.39, R8.2, R9.1 | RTK NMEA（3.3 V）：给同步板，同时分压到 1.8 V 给模块 |
| `NMEA_TXD_33` | signal | U16.B2, J14.5 | 模块配置 RTK（3.3 V） |
| `SYNC_AUX1` | signal | J12.41 | 同步板辅助 IO |
| `SYNC_AUX2` | signal | J12.42 | 同步板辅助 IO |
| `SYNC_AUX3` | signal | J12.43 | 同步板辅助 IO |
| `CAM_ECHO_VSYNC` | signal | J13.VSYNC_OUT, U7.A1, U7.B1, J12.25 | 相机 VSYNC 回读 → 同步板 |
| `CAM_ECHO_TIMER` | signal | J13.TIMER_SYNC_OUT, U7.A2, U7.B2, J12.26 | 相机 TIMER_SYNC 回读 → 同步板 |
| `RTK_5V` | power | J14.1 | RTK 接收机供电 |
| `RTK_PPS` | signal | J14.3, U8.A | RTK 的 1PPS（3.3 V TTL） |
| `RTK_NMEA` | signal | J14.4, U17.A | RTK 的 NMEA 输出（3.3 V TTL） |
| `SIM_CLK` | signal | J20.CLK, J5.SIM_CLK | 5G SIM（脚位按所选模组核对） |
| `SIM_RST` | signal | J20.RST, J5.SIM_RST | 5G SIM 复位 |
| `SIM_IO` | signal | J20.IO, J5.SIM_IO | 5G SIM 数据 |
| `SIM_PWR` | power | J20.VCC, J5.SIM_VCC | SIM 供电（按模组要求从 +3V3 经 50 Ω 串阻引出） |

## 器件

| 位号 | 型号 | 封装 | MPN | 说明 |
| --- | --- | --- | --- | --- |
| J1 | Orin NX 260-pin | `按 NVIDIA 规范选型` |  | Orin NX 模块座 |
| J2 | DC_IN_5.5x2.5 | `Connector_BarrelJack:BarrelJack_Horizontal` |  | 12–20 V 电源输入 |
| J3 | RJ45_PoE | `Connector_RJ45:Amphenol_74005251-0001` |  | 千兆网口（PoE PD，雷达/骨干） |
| J4 | M.2_Key_M_2280 | `Connector_M.2:M.2_Key_M_2280_22x-4H` |  | NVMe SSD (PCIe x4) |
| J5 | M.2_Key_B_2242 | `Connector_M.2:M.2_Key_B_2242_22x-4H` |  | 5G 模组（PCIe x2 数据 + USB 2.0 控制） |
| J6 | M.2_Key_E_2230 | `Connector_M.2:M.2_Key_E_2230_22x-4H` |  | WiFi/BT 预留（REFCLK 脚位按所选模组核对） |
| J7 | USB-C_3.0 | `Connector_USB:USB_C_Receptacle_16P` |  | USB 3.0 #1：Gemini 335 |
| J8 | USB-C_3.0 | `Connector_USB:USB_C_Receptacle_16P` |  | USB 3.0 #2 |
| J9 | USB-C_3.0 | `Connector_USB:USB_C_Receptacle_16P` |  | USB 3.0 #3（仅 SS） |
| J10 | CSI_22pin | `Connector_PinHeader:PinHeader_2x11_P2.00mm_Vertical` |  | CSI 相机座 #1 |
| J11 | CSI_22pin | `Connector_PinHeader:PinHeader_2x11_P2.00mm_Vertical` |  | CSI 相机座 #2 |
| J12 | SYNC_MEZZ_2x22 | `Connector_PinHeader:PinHeader_2x22_P2.00mm_Vertical` |  | 同步板（STM32H7）板对板接口 |
| J13 | CAM_SYNC_8pin | `Connector_PinHeader:PinHeader_2x04_P2.00mm_Vertical` |  | Gemini 335 8-pin 硬件同步座 |
| J14 | RTK_CONN | `Connector_PinHeader:PinHeader_1x06_P2.54mm_Vertical` |  | RTK 接收机（差分 PPS + NMEA） |
| J15 | CAN_3pin | `Connector_JST:JST_XH_B3B-XH-A_1x03_P2.50mm_Vertical` |  | 隔离 CAN #1 |
| J16 | CAN_3pin | `Connector_JST:JST_XH_B3B-XH-A_1x03_P2.50mm_Vertical` |  | 隔离 CAN #2 |
| J17 | DEBUG_UART | `Connector_PinHeader:PinHeader_1x06_P2.54mm_Vertical` |  | 调试串口（UART0）+ 备用 GPIO |
| J18 | FAN_4pin | `Connector_JST:JST_PH_B4B-PH-SM4-TB_1x04_P2.00mm` |  | 风扇（PWM 调速） |
| J19 | RTC_BATT | `Battery:BatteryHolder_Keystone_1060_1x20mm` | 1060 | 模块 PMIC_BBAT 备份电池 |
| J20 | SIM_6pin | `Connector_PinHeader:PinHeader_1x06_P2.54mm_Vertical` |  | SIM 卡座 |
| J21 | TRIG_OUT_6pin | `Connector_JST:JST_XH_B6B-XH-A_1x06_P2.50mm_Vertical` |  | 触发线束座：四路触发（3.3 V）+ 两路 1.8 V 副本 |
| U1 | TPS54560-Q1 | `Package_TO_SOT_SMD:SOT-23-6` | TPS54560QDGQR | 5 V / 5 A buck（载板电源） |
| U2 | TPS23753 | `Package_TO_SOT_SMD:SOT-23-6` | TPS23753-4ECNR | PoE PD 控制器 |
| U3 | H110F | `Package_SO:SOIC-16_3.9x9.9mm_P1.27mm` | H110FNL | 千兆网络变压器 |
| U4 | TXS0102 | `Package_TO_SOT_SMD:SOT-23-6` | TXS0102DCTR | UART1 双向 1.8 V ↔ 3.3 V |
| U5 | 74AVC4T245 | `Package_SO:SOIC-16_3.9x9.9mm_P1.27mm` | SN74AVC4T245PWR | 四路触发输出 3.3 V → 1.8 V |
| U6 | TXS0104 | `Package_SO:SOIC-16_3.9x9.9mm_P1.27mm` | TXS0104PWR | 模块→同步板的 4 路状态 GPIO（1.8 V → 3.3 V） |
| U7 | TXS0102 | `Package_TO_SOT_SMD:SOT-23-6` | TXS0102DCTR | 相机同步回读 1.8 V → 3.3 V |
| U8 | 74LVC1G17 | `Package_TO_SOT_SMD:SOT-23-5` | SN74LVC1G17DBVR | RTK PPS 施密特整形（3.3 V） |
| U16 | SN74LVC2T45 | `Package_SO:SOIC-8_3.9x4.9mm_P1.27mm` | SN74LVC2T45DR | 1.8 V ↔ 3.3 V：PPS 送给模块、模块 UART2 TX 送回 RTK |
| U17 | 74LVC1G17 | `Package_TO_SOT_SMD:SOT-23-5` | SN74LVC1G17DBVR | RTK NMEA 施密特整形 |
| U9 | 24AA01 | `Package_TO_SOT_SMD:SOT-23-5` | 24AA01T-I/OT | 载板 EEPROM（Orin 靠它识别载板，I2C0 @0x50） |
| U10 | TPS3808G01 | `Package_SOT_SMD:SOT-23-6` | TPS3808G01DBVR | 电源时序 / UVLO 监控，输出 POWER_EN |
| U11 | TPS7A4700 | `Package_TO_SOT_SMD:SOT-23-6` | TPS7A4700RGWR | 低噪声 3.3 V LDO（信号域供电） |
| U12 | TPS54302 | `Package_TO_SOT_SMD:SOT-23-6` | TPS54302DDCR | 3.3 V / 3 A buck（5G 模组） |
| U13 | SN74LVC1G125 | `Package_TO_SOT_SMD:SOT-23-5` | SN74LVC1G125DBVR | POWER_EN 施密特缓冲（模块要求） |
| Q1 | Si2302CDS | `Package_TO_SOT_SMD:SOT-23-6` |  | 5G PWRKEY 驱动 |
| Q2 | Si2302CDS | `Package_TO_SOT_SMD:SOT-23-6` |  | 5G RESET 驱动 |
| SW1 | SW_Push | `Button_Switch_SMD:SW_SPST_SKRPACE010` |  | 电源键 |
| SW2 | SW_Push | `Button_Switch_SMD:SW_SPST_SKRPACE010` |  | 强制恢复（低有效） |
| SW3 | SW_Push | `Button_Switch_SMD:SW_SPST_SKRPACE010` |  | 同步板复位（低有效） |
| D1 | LED_Green | `LED_SMD:LED_0603_1608Metric` |  | 电源/运行 |
| D2 | LED_Yellow | `LED_SMD:LED_0603_1608Metric` |  | PPS 锁定 |
| D3 | LED_Red | `LED_SMD:LED_0603_1608Metric` |  | 故障 |
| F1 | Fuse_3A | `Fuse:Fuse_1812_4532Metric` |  | 输入保险丝 |
| D4 | SMBJ33A | `Diode_SMD:D_SMB` |  | 输入 TVS |
| D5 | SS54 | `Diode_SMD:D_SMA` |  | 防反接 |
| L1 | 10uH | `Inductor_SMD:L_1210_3225Metric` |  | 5 V 输出滤波 |
| L2 | 4u7 | `Inductor_SMD:L_1210_3225Metric` |  | 3.3 V 输出滤波 |
| C1 | 22uF | `Capacitor_SMD:C_1210_3225Metric` |  | 输入储能 |
| C2 | 100uF | `Capacitor_SMD:C_1210_3225Metric` |  | 5 V 轨储能 |
| C3 | 10uF | `Capacitor_SMD:C_0805_2012Metric` |  | 5 V 轨 |
| C4 | 22uF | `Capacitor_SMD:C_0805_2012Metric` |  | 3.3 V 轨 |
| C5 | 100nF | `Capacitor_SMD:C_0603_1608Metric` |  | 去耦 |
| C6 | 100uF | `Capacitor_SMD:C_0805_2012Metric` |  | 3.3 V 轨（5G 专用） |
| C7 | 22uF | `Capacitor_SMD:C_0805_2012Metric` |  | 5G VBUS 储能 |
| R1 | 1k | `Resistor_SMD:R_0603_1608Metric` |  | D1 限流 |
| R2 | 1k | `Resistor_SMD:R_0603_1608Metric` |  | D2 限流 |
| R3 | 1k | `Resistor_SMD:R_0603_1608Metric` |  | D3 限流 |
| R4 | 10k | `Resistor_SMD:R_0603_1608Metric` |  | 信号下拉 |
| R5 | 4k7 | `Resistor_SMD:R_0603_1608Metric` |  | I2C 上拉 |
| R6 | 100R | `Resistor_SMD:R_0603_1608Metric` |  | 串联匹配（预留） |
| R7 | 10k | `Resistor_SMD:R_0603_1608Metric` |  | FORCE_RECOVERY 上拉 |
| R8 | 4k7 | `Resistor_SMD:R_0603_1608Metric` |  | NMEA 3.3→1.8 分压上臂 |
| R9 | 4k7 | `Resistor_SMD:R_0603_1608Metric` |  | NMEA 3.3→1.8 分压下臂 |
