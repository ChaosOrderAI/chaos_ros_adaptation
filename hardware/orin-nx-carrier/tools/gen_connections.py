#!/usr/bin/env python3
"""生成 Orin NX 载板的连接表与 BOM。

**引脚号的可溯源性**：`pins_full.json` 里的 260 个模块引脚全部来自
ConnectTech《Jetson Orin NX Data Sheet》DS-10712-001 的
"Jetson Orin NX Pin List" 表，并且与各接口表解析结果交叉验证一致
（见 `tools/extract_pinlist.py`）。本脚本对每个模块引脚再做一次**信号名
校验**：网络声明的信号前缀必须能在数据手册的引脚名里找到，否则报错。

载板侧器件用功能引脚名（如 `U4.A1`、`J7.TXP`），出原理图时按各器件
数据手册替换为封装脚号。

用法:
    python3 tools/gen_connections.py --check
    python3 tools/gen_connections.py
"""
from __future__ import annotations

import argparse
import csv
import json
import sys
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PINS = json.loads((ROOT / "pins_full.json").read_text(encoding="utf-8"))
PIN_NAME = {p["pin"]: p["name"] for p in PINS["pins"]}
GND_PINS = set(PINS["gnd_pins"])
M = "J1"  # Orin NX 模块座


@dataclass
class Part:
    ref: str
    value: str
    footprint: str
    mpn: str = ""
    note: str = ""


@dataclass
class Net:
    name: str
    kind: str
    nodes: list[tuple[str, str]] = field(default_factory=list)
    note: str = ""
    sig: str = ""   # 模块引脚名里应出现的信号前缀（用于自动校验）


PARTS: list[Part] = [
    Part("J1", "Orin NX 260-pin", "按 NVIDIA 规范选型", "",
         "Orin NX 模块座"),
    Part("J2", "DC_IN_5.5x2.5", "Connector_BarrelJack:BarrelJack_Horizontal", "", "12–20 V 电源输入"),
    Part("J3", "RJ45_PoE", "Connector_RJ45:Amphenol_74005251-0001", "",
         "千兆网口（PoE PD，雷达/骨干）"),
    Part("J4", "M.2_Key_M_2280", "Connector_M.2:M.2_Key_M_2280_22x-4H", "", "NVMe SSD (PCIe x4)"),
    Part("J5", "M.2_Key_B_2242", "Connector_M.2:M.2_Key_B_2242_22x-4H", "",
         "5G 模组（PCIe x2 数据 + USB 2.0 控制）"),
    Part("J6", "M.2_Key_E_2230", "Connector_M.2:M.2_Key_E_2230_22x-4H", "",
         "WiFi/BT 预留（REFCLK 脚位按所选模组核对）"),
    Part("J7", "USB-C_3.0", "Connector_USB:USB_C_Receptacle_16P", "", "USB 3.0 #1：Gemini 335"),
    Part("J8", "USB-C_3.0", "Connector_USB:USB_C_Receptacle_16P", "", "USB 3.0 #2"),
    Part("J9", "USB-C_3.0", "Connector_USB:USB_C_Receptacle_16P", "", "USB 3.0 #3（仅 SS）"),
    Part("J10", "CSI_22pin", "Connector_PinHeader:PinHeader_2x11_P2.00mm_Vertical", "", "CSI 相机座 #1"),
    Part("J11", "CSI_22pin", "Connector_PinHeader:PinHeader_2x11_P2.00mm_Vertical", "", "CSI 相机座 #2"),
    Part("J12", "SYNC_MEZZ_2x22", "Connector_PinHeader:PinHeader_2x22_P2.00mm_Vertical", "",
         "同步板（STM32H7）板对板接口"),
    Part("J13", "CAM_SYNC_8pin", "Connector_PinHeader:PinHeader_2x04_P2.00mm_Vertical", "",
         "Gemini 335 8-pin 硬件同步座"),
    Part("J14", "RTK_CONN", "Connector_PinHeader:PinHeader_1x06_P2.54mm_Vertical", "",
         "RTK 接收机（差分 PPS + NMEA）"),
    Part("J15", "CAN_3pin", "Connector_JST:JST_XH_B3B-XH-A_1x03_P2.50mm_Vertical", "", "隔离 CAN #1"),
    Part("J16", "CAN_3pin", "Connector_JST:JST_XH_B3B-XH-A_1x03_P2.50mm_Vertical", "", "隔离 CAN #2"),
    Part("J17", "DEBUG_UART", "Connector_PinHeader:PinHeader_1x06_P2.54mm_Vertical", "",
         "调试串口（UART0）+ 备用 GPIO"),
    Part("J18", "FAN_4pin", "Connector_JST:JST_PH_B4B-PH-SM4-TB_1x04_P2.00mm", "", "风扇（PWM 调速）"),
    Part("J19", "RTC_BATT", "Battery:BatteryHolder_Keystone_1060_1x20mm", "1060", "模块 PMIC_BBAT 备份电池"),
    Part("J20", "SIM_6pin", "Connector_PinHeader:PinHeader_1x06_P2.54mm_Vertical", "", "SIM 卡座"),
    Part("J21", "TRIG_OUT_6pin", "Connector_JST:JST_XH_B6B-XH-A_1x06_P2.50mm_Vertical", "",
         "触发线束座：四路触发（3.3 V）+ 两路 1.8 V 副本"),
    Part("U1", "TPS54560-Q1", "Package_TO_SOT_SMD:SOT-23-6", "TPS54560QDGQR", "5 V / 5 A buck（载板电源）"),
    Part("U2", "TPS23753", "Package_TO_SOT_SMD:SOT-23-6", "TPS23753-4ECNR", "PoE PD 控制器"),
    Part("U3", "H110F", "Package_SO:SOIC-16_3.9x9.9mm_P1.27mm", "H110FNL", "千兆网络变压器"),
    Part("U4", "TXS0102", "Package_TO_SOT_SMD:SOT-23-6", "TXS0102DCTR", "UART1 双向 1.8 V ↔ 3.3 V"),
    Part("U5", "74AVC4T245", "Package_SO:SOIC-16_3.9x9.9mm_P1.27mm", "SN74AVC4T245PWR",
         "四路触发输出 3.3 V → 1.8 V"),
    Part("U6", "TXS0104", "Package_SO:SOIC-16_3.9x9.9mm_P1.27mm", "TXS0104PWR",
         "模块→同步板的 4 路状态 GPIO（1.8 V → 3.3 V）"),
    Part("U7", "TXS0102", "Package_TO_SOT_SMD:SOT-23-6", "TXS0102DCTR", "相机同步回读 1.8 V → 3.3 V"),
    Part("U8", "74LVC1G17", "Package_TO_SOT_SMD:SOT-23-5", "SN74LVC1G17DBVR",
         "RTK PPS 施密特整形（3.3 V）"),
    Part("U16", "SN74LVC2T45", "Package_SO:SOIC-8_3.9x4.9mm_P1.27mm", "SN74LVC2T45DR",
         "1.8 V ↔ 3.3 V：PPS 送给模块、模块 UART2 TX 送回 RTK"),
    Part("U17", "74LVC1G17", "Package_TO_SOT_SMD:SOT-23-5", "SN74LVC1G17DBVR",
         "RTK NMEA 施密特整形"),
    Part("U9", "24AA01", "Package_TO_SOT_SMD:SOT-23-5", "24AA01T-I/OT",
         "载板 EEPROM（Orin 靠它识别载板，I2C0 @0x50）"),
    Part("U10", "TPS3808G01", "Package_SOT_SMD:SOT-23-6", "TPS3808G01DBVR",
         "电源时序 / UVLO 监控，输出 POWER_EN"),
    Part("U11", "TPS7A4700", "Package_TO_SOT_SMD:SOT-23-6", "TPS7A4700RGWR",
         "低噪声 3.3 V LDO（信号域供电）"),
    Part("U12", "TPS54302", "Package_TO_SOT_SMD:SOT-23-6", "TPS54302DDCR", "3.3 V / 3 A buck（5G 模组）"),
    Part("U13", "SN74LVC1G125", "Package_TO_SOT_SMD:SOT-23-5", "SN74LVC1G125DBVR",
         "POWER_EN 施密特缓冲（模块要求）"),
    Part("Q1", "Si2302CDS", "Package_TO_SOT_SMD:SOT-23-6", "", "5G PWRKEY 驱动"),
    Part("Q2", "Si2302CDS", "Package_TO_SOT_SMD:SOT-23-6", "", "5G RESET 驱动"),
    Part("SW1", "SW_Push", "Button_Switch_SMD:SW_SPST_SKRPACE010", "", "电源键"),
    Part("SW2", "SW_Push", "Button_Switch_SMD:SW_SPST_SKRPACE010", "", "强制恢复（低有效）"),
    Part("SW3", "SW_Push", "Button_Switch_SMD:SW_SPST_SKRPACE010", "", "同步板复位（低有效）"),
    Part("D1", "LED_Green", "LED_SMD:LED_0603_1608Metric", "", "电源/运行"),
    Part("D2", "LED_Yellow", "LED_SMD:LED_0603_1608Metric", "", "PPS 锁定"),
    Part("D3", "LED_Red", "LED_SMD:LED_0603_1608Metric", "", "故障"),
    Part("F1", "Fuse_3A", "Fuse:Fuse_1812_4532Metric", "", "输入保险丝"),
    Part("D4", "SMBJ33A", "Diode_SMD:D_SMB", "", "输入 TVS"),
    Part("D5", "SS54", "Diode_SMD:D_SMA", "", "防反接"),
    Part("L1", "10uH", "Inductor_SMD:L_1210_3225Metric", "", "5 V 输出滤波"),
    Part("L2", "4u7", "Inductor_SMD:L_1210_3225Metric", "", "3.3 V 输出滤波"),
    Part("C1", "22uF", "Capacitor_SMD:C_1210_3225Metric", "", "输入储能"),
    Part("C2", "100uF", "Capacitor_SMD:C_1210_3225Metric", "", "5 V 轨储能"),
    Part("C3", "10uF", "Capacitor_SMD:C_0805_2012Metric", "", "5 V 轨"),
    Part("C4", "22uF", "Capacitor_SMD:C_0805_2012Metric", "", "3.3 V 轨"),
    Part("C5", "100nF", "Capacitor_SMD:C_0603_1608Metric", "", "去耦"),
    Part("C6", "100uF", "Capacitor_SMD:C_0805_2012Metric", "", "3.3 V 轨（5G 专用）"),
    Part("C7", "22uF", "Capacitor_SMD:C_0805_2012Metric", "", "5G VBUS 储能"),
    Part("R1", "1k", "Resistor_SMD:R_0603_1608Metric", "", "D1 限流"),
    Part("R2", "1k", "Resistor_SMD:R_0603_1608Metric", "", "D2 限流"),
    Part("R3", "1k", "Resistor_SMD:R_0603_1608Metric", "", "D3 限流"),
    Part("R4", "10k", "Resistor_SMD:R_0603_1608Metric", "", "信号下拉"),
    Part("R5", "4k7", "Resistor_SMD:R_0603_1608Metric", "", "I2C 上拉"),
    Part("R6", "100R", "Resistor_SMD:R_0603_1608Metric", "", "串联匹配（预留）"),
    Part("R7", "10k", "Resistor_SMD:R_0603_1608Metric", "", "FORCE_RECOVERY 上拉"),
    Part("R8", "4k7", "Resistor_SMD:R_0603_1608Metric", "", "NMEA 3.3→1.8 分压上臂"),
    Part("R9", "4k7", "Resistor_SMD:R_0603_1608Metric", "", "NMEA 3.3→1.8 分压下臂"),
]

GND_NODES = [
    ("J2", "2"), ("F1", "2"), ("D5", "2"), ("C1", "2"), ("C2", "2"), ("C3", "2"),
    ("C4", "2"), ("C5", "2"), ("C6", "2"), ("C7", "2"), ("L2", "2"),
    ("J7", "GND"), ("J8", "GND"), ("J9", "GND"),
    ("J12", "7"), ("J12", "8"), ("J12", "9"), ("J12", "10"),
    ("J12", "15"), ("J12", "16"), ("J12", "17"), ("J12", "20"), ("J12", "21"),
    ("J12", "31"), ("J12", "32"), ("J12", "36"), ("J12", "37"), ("J12", "40"),
    ("J12", "44"),
    ("J10", "GND"), ("J11", "GND"), ("J13", "GND"), ("J15", "1"), ("J16", "1"),
    ("J17", "GND"), ("J18", "GND"), ("J20", "GND"),
    ("U3", "GND"), ("U4", "GND"), ("U5", "GND"), ("U6", "GND"), ("U7", "GND"),
    ("U8", "GND"), ("U9", "GND"), ("U10", "GND"), ("U11", "GND"), ("U12", "GND"),
    ("U13", "GND"), ("Q1", "S"), ("Q2", "S"),
    ("SW1", "2"), ("SW2", "2"), ("SW3", "2"),
    ("D1", "2"), ("D3", "2"), ("R4", "2"), ("R5", "2"), ("R6", "2"), ("R7", "2"), ("R9", "2"),
]
# 模块的全部 GND 脚（数据手册 Pin List 表里标绿的 66 个）
GND_NODES += [(M, str(p)) for p in sorted(GND_PINS)]

NETS: list[Net] = [
    # ---------------- 电源 ----------------
    Net("GND", "power", list(GND_NODES),
        "模块 GND 脚共 66 个（见 pins_full.json 的 gnd_pins），必须全部接本网络"),
    Net("VIN_PROT", "power", [
        ("J2", "1"), ("F1", "1"), ("D4", "1"), ("D5", "1"), ("L1", "1"),
        ("U1", "VIN"), ("U10", "VIN"), ("U12", "VIN"),
        ("J1", "251"), ("J1", "252"), ("J1", "253"), ("J1", "254"), ("J1", "255"),
        ("J1", "256"), ("J1", "257"), ("J1", "258"), ("J1", "259"), ("J1", "260"),
    ], "模块 VDD_IN 直通（Orin NX 支持 5–20 V；MAXN_SUPER 40 W 需 ≥8 V）", sig="VDD_IN"),
    Net("+5V", "power", [
        ("U1", "VOUT"), ("L1", "2"), ("C2", "1"), ("C3", "1"),
        ("J7", "VBUS"), ("J8", "VBUS"), ("J9", "VBUS"),
        ("J12", "1"), ("J12", "2"), ("J13", "1"),
        ("U11", "VIN"), ("U10", "VDD"), ("J18", "PWR"), ("J19", "1"),
    ], "载板 5 V 轨（5 A）"),
    Net("+3V3", "power", [
        ("U11", "VOUT"), ("C4", "1"), ("U9", "VDD"),
        ("U4", "VCCA"), ("U5", "VCCA"), ("U6", "VCCA"), ("U7", "VCCA"), ("U8", "VCC"),
        ("J12", "3"), ("J12", "4"),
    ], "信号域 3.3 V（由 LDO 从 5 V 降压，噪声低）"),
    Net("+3V3_5G", "power", [
        ("U12", "VOUT"), ("L2", "1"), ("C6", "1"), ("C7", "1"), ("J5", "3V3"),
    ], "5G 模组 3.3 V / 3 A 专用 buck（独立时序，不受信号 3.3 V 影响）"),
    Net("+1V8", "power", [
        ("U4", "VCCB"), ("U5", "VCCB"), ("U6", "VCCB"), ("U7", "VCCB"),
        ("J12", "5"), ("J12", "6"),
    ], "模块 1.8 V IO 域（电平转换器 B 侧）"),

    # ---------------- 上电时序与控制 ----------------
    Net("POWER_EN", "signal", [("J1", "237"), ("U13", "A"), ("U10", "PG")],
        "模块上电使能（模块侧为 5 V 施密特输入，必须用缓冲器驱动）", sig="POWER_EN"),
    Net("SYS_RESET_N", "signal", [("J1", "239"), ("U10", "MR"), ("R7", "1")],
        "模块复位（开漏 1.8 V；上电时序完成后由模块拉高表示 power good）", sig="SYS_RESET"),
    Net("SHUTDOWN_REQ_N", "signal", [("J1", "233"), ("U10", "SHDN")],
        "模块请求关机（开漏 @VDD_IN，模块内约 5 k 上拉）", sig="SHUTDOWN"),
    Net("SLEEP_WAKE", "signal", [("J1", "240"), ("J17", "3")],
        "休眠/唤醒", sig="SLEEP"),
    Net("FORCE_RECOVERY_N", "signal", [("J1", "214"), ("SW2", "1")],
        "强制恢复（按下接地）", sig="FORCE_RECOVERY"),
    Net("MODULE_ID", "signal", [("J1", "217")],
        "模块内已悬空（20 V 能力机型），载板不要接任何东西", sig="MODULE_ID"),
    Net("PMIC_BBAT", "power", [("J1", "235"), ("J19", "2")],
        "模块 RTC 备份电池（1.75–5.5 V，不可充电）", sig="PMIC_BBAT"),
    Net("CLK_32K", "signal", [("J1", "210"), ("J12", "11")],
        "模块 32 kHz 输出（同步板侧暂不接）", sig="CLK_32K"),
    Net("MOD_SLEEP_N", "signal", [("J1", "178")], "模块进入 SC7（低有效）", sig="MOD_SLEEP"),
    Net("PCIE_WAKE_N", "signal", [("J1", "179")], "PCIe 唤醒（开漏）", sig="PCIE_WAKE"),

    # ---------------- PCIe x4：NVMe ----------------
    Net("PCIE0_RX0N", "signal", [("J1", "131"), ("J4", "RXP")], "Ctrl#4 lane0 RX-", sig="PCIE0_RX0"),
    Net("PCIE0_RX0P", "signal", [("J1", "133"), ("J4", "RXN")], "Ctrl#4 lane0 RX+", sig="PCIE0_RX0"),
    Net("PCIE0_TX0N", "signal", [("J1", "134"), ("J4", "TXN")], "Ctrl#4 lane0 TX-", sig="PCIE0_TX0"),
    Net("PCIE0_TX0P", "signal", [("J1", "136"), ("J4", "TXP")], "Ctrl#4 lane0 TX+", sig="PCIE0_TX0"),
    Net("PCIE0_RX1N", "signal", [("J1", "137"), ("J4", "RXP1")], "Ctrl#4 lane1 RX-", sig="PCIE0_RX1"),
    Net("PCIE0_RX1P", "signal", [("J1", "139"), ("J4", "RXN1")], "Ctrl#4 lane1 RX+", sig="PCIE0_RX1"),
    Net("PCIE0_TX1N", "signal", [("J1", "140"), ("J4", "TXN1")], "Ctrl#4 lane1 TX-", sig="PCIE0_TX1"),
    Net("PCIE0_TX1P", "signal", [("J1", "142"), ("J4", "TXP1")], "Ctrl#4 lane1 TX+", sig="PCIE0_TX1"),
    Net("PCIE0_RX2N", "signal", [("J1", "149"), ("J4", "RXP2")], "Ctrl#4 lane2 RX-", sig="PCIE0_RX2"),
    Net("PCIE0_RX2P", "signal", [("J1", "151"), ("J4", "RXN2")], "Ctrl#4 lane2 RX+", sig="PCIE0_RX2"),
    Net("PCIE0_TX2N", "signal", [("J1", "148"), ("J4", "TXN2")], "Ctrl#4 lane2 TX-", sig="PCIE0_TX2"),
    Net("PCIE0_TX2P", "signal", [("J1", "150"), ("J4", "TXP2")], "Ctrl#4 lane2 TX+", sig="PCIE0_TX2"),
    Net("PCIE0_RX3N", "signal", [("J1", "155"), ("J4", "RXP3")], "Ctrl#4 lane3 RX-", sig="PCIE0_RX3"),
    Net("PCIE0_RX3P", "signal", [("J1", "157"), ("J4", "RXN3")], "Ctrl#4 lane3 RX+", sig="PCIE0_RX3"),
    Net("PCIE0_TX3N", "signal", [("J1", "154"), ("J4", "TXN3")], "Ctrl#4 lane3 TX-", sig="PCIE0_TX3"),
    Net("PCIE0_TX3P", "signal", [("J1", "156"), ("J4", "TXP3")], "Ctrl#4 lane3 TX+", sig="PCIE0_TX3"),
    Net("PCIE0_CLKN", "signal", [("J1", "160"), ("J4", "REFCLK_N")], "Ctrl#4 100 MHz 参考时钟", sig="PCIE0_CLK"),
    Net("PCIE0_CLKP", "signal", [("J1", "162"), ("J4", "REFCLK_P")], "Ctrl#4 100 MHz 参考时钟", sig="PCIE0_CLK"),
    Net("PCIE0_RST_N", "signal", [("J1", "181"), ("J4", "PERST_N")], "Ctrl#4 复位", sig="PCIE0_RST"),
    Net("PCIE0_CLKREQ_N", "signal", [("J1", "180"), ("J4", "CLKREQ_N")], "Ctrl#4 时钟请求", sig="PCIE0_CLKREQ"),

    # ---------------- PCIe x2：5G ----------------
    Net("PCIE2_RX0N", "signal", [("J1", "40"), ("J5", "RXP")], "Ctrl#7 lane0 → 5G", sig="PCIE2_RX0"),
    Net("PCIE2_RX0P", "signal", [("J1", "42"), ("J5", "RXN")], "Ctrl#7 lane0 → 5G", sig="PCIE2_RX0"),
    Net("PCIE2_TX0N", "signal", [("J1", "46"), ("J5", "TXN")], "Ctrl#7 lane0 → 5G", sig="PCIE2_TX0"),
    Net("PCIE2_TX0P", "signal", [("J1", "48"), ("J5", "TXP")], "Ctrl#7 lane0 → 5G", sig="PCIE2_TX0"),
    Net("PCIE2_RX1N", "signal", [("J1", "58"), ("J5", "RXP1")], "Ctrl#7 lane1 → 5G", sig="PCIE2_RX1"),
    Net("PCIE2_RX1P", "signal", [("J1", "60"), ("J5", "RXN1")], "Ctrl#7 lane1 → 5G", sig="PCIE2_RX1"),
    Net("PCIE2_TX1N", "signal", [("J1", "64"), ("J5", "TXN1")], "Ctrl#7 lane1 → 5G", sig="PCIE2_TX1"),
    Net("PCIE2_TX1P", "signal", [("J1", "66"), ("J5", "TXP1")], "Ctrl#7 lane1 → 5G", sig="PCIE2_TX1"),
    Net("PCIE2_CLKN", "signal", [("J1", "52"), ("J5", "REFCLK_N")], "5G REFCLK", sig="PCIE2_CLK"),
    Net("PCIE2_CLKP", "signal", [("J1", "54"), ("J5", "REFCLK_P")], "5G REFCLK", sig="PCIE2_CLK"),
    Net("PCIE2_RST_N", "signal", [("J1", "219"), ("J5", "PERST_N")], "5G 复位", sig="PCIE2_RST"),
    Net("PCIE2_CLKREQ_N", "signal", [("J1", "221"), ("J5", "CLKREQ_N")], "5G 时钟请求", sig="PCIE2_CLKREQ"),

    # ---------------- PCIe x1：预留 ----------------
    Net("PCIE1_RX0N", "signal", [("J1", "167"), ("J6", "RXP")], "Ctrl#1 lane0，预留", sig="PCIE1_RX0"),
    Net("PCIE1_RX0P", "signal", [("J1", "169"), ("J6", "RXN")], "Ctrl#1 lane0，预留", sig="PCIE1_RX0"),
    Net("PCIE1_TX0N", "signal", [("J1", "172"), ("J6", "TXN")], "Ctrl#1 lane0，预留", sig="PCIE1_TX0"),
    Net("PCIE1_TX0P", "signal", [("J1", "174"), ("J6", "TXP")], "Ctrl#1 lane0，预留", sig="PCIE1_TX0"),
    Net("PCIE1_CLKN", "signal", [("J1", "173"), ("J6", "REFCLK_N")], "Ctrl#1 参考时钟", sig="PCIE1_CLK"),
    Net("PCIE1_CLKP", "signal", [("J1", "175"), ("J6", "REFCLK_P")], "Ctrl#1 参考时钟", sig="PCIE1_CLK"),
    Net("PCIE1_RST_N", "signal", [("J1", "183"), ("J6", "PERST_N")], "Ctrl#1 复位", sig="PCIE1_RST"),
    Net("PCIE1_CLKREQ_N", "signal", [("J1", "182"), ("J6", "CLKREQ_N")], "Ctrl#1 时钟请求", sig="PCIE1_CLKREQ"),

    # ---------------- USB ----------------
    Net("USBSS0_RXN", "signal", [("J1", "161"), ("J7", "RXP"), ("J7", "RXP_B")], "USB3 #1 SS", sig="USBSS0_RX"),
    Net("USBSS0_RXP", "signal", [("J1", "163"), ("J7", "RXN"), ("J7", "RXN_B")], "USB3 #1 SS", sig="USBSS0_RX"),
    Net("USBSS0_TXN", "signal", [("J1", "166"), ("J7", "TXP"), ("J7", "TXP_B")], "USB3 #1 SS", sig="USBSS0_TX"),
    Net("USBSS0_TXP", "signal", [("J1", "168"), ("J7", "TXN"), ("J7", "TXN_B")], "USB3 #1 SS", sig="USBSS0_TX"),
    Net("USB0_DN", "signal", [("J1", "109"), ("J7", "DN"), ("J7", "DN_B")], "USB3 #1 的 USB2.0 通道", sig="USB0_D"),
    Net("USB0_DP", "signal", [("J1", "111"), ("J7", "DP"), ("J7", "DP_B")], "USB3 #1 的 USB2.0 通道", sig="USB0_D"),
    Net("USBSS1_RXN", "signal", [("J1", "39"), ("J8", "RXP"), ("J8", "RXP_B")], "USB3 #2 SS", sig="USBSS1_RX"),
    Net("USBSS1_RXP", "signal", [("J1", "41"), ("J8", "RXN"), ("J8", "RXN_B")], "USB3 #2 SS", sig="USBSS1_RX"),
    Net("USBSS1_TXN", "signal", [("J1", "45"), ("J8", "TXP"), ("J8", "TXP_B")], "USB3 #2 SS", sig="USBSS1_TX"),
    Net("USBSS1_TXP", "signal", [("J1", "47"), ("J8", "TXN"), ("J8", "TXN_B")], "USB3 #2 SS", sig="USBSS1_TX"),
    Net("USB1_DN", "signal", [("J1", "115"), ("J8", "DN"), ("J8", "DN_B")], "USB3 #2 的 USB2.0 通道", sig="USB1_D"),
    Net("USB1_DP", "signal", [("J1", "117"), ("J8", "DP"), ("J8", "DP_B")], "USB3 #2 的 USB2.0 通道", sig="USB1_D"),
    Net("USBSS2_RXN", "signal", [("J1", "51"), ("J9", "RXP"), ("J9", "RXP_B")], "USB3 #3 SS", sig="USBSS2_RX"),
    Net("USBSS2_RXP", "signal", [("J1", "53"), ("J9", "RXN"), ("J9", "RXN_B")], "USB3 #3 SS", sig="USBSS2_RX"),
    Net("USBSS2_TXN", "signal", [("J1", "57"), ("J9", "TXP"), ("J9", "TXP_B")], "USB3 #3 SS", sig="USBSS2_TX"),
    Net("USBSS2_TXP", "signal", [("J1", "59"), ("J9", "TXN"), ("J9", "TXN_B")], "USB3 #3 SS", sig="USBSS2_TX"),
    Net("USB2_DN", "signal", [("J1", "121"), ("J5", "USB_DN")], "5G 模组 USB 2.0 控制通道", sig="USB2_D"),
    Net("USB2_DP", "signal", [("J1", "123"), ("J5", "USB_DP")], "5G 模组 USB 2.0 控制通道", sig="USB2_D"),

    # ---------------- 千兆网 ----------------
    Net("GBE_MDI0N", "signal", [("J1", "184"), ("U3", "TX0N")], "千兆变压器", sig="GBE_MDI0"),
    Net("GBE_MDI0P", "signal", [("J1", "186"), ("U3", "TX0P")], "千兆变压器", sig="GBE_MDI0"),
    Net("GBE_MDI1N", "signal", [("J1", "190"), ("U3", "TX1N")], "千兆变压器", sig="GBE_MDI1"),
    Net("GBE_MDI1P", "signal", [("J1", "192"), ("U3", "TX1P")], "千兆变压器", sig="GBE_MDI1"),
    Net("GBE_MDI2N", "signal", [("J1", "196"), ("U3", "TX2N")], "千兆变压器", sig="GBE_MDI2"),
    Net("GBE_MDI2P", "signal", [("J1", "198"), ("U3", "TX2P")], "千兆变压器", sig="GBE_MDI2"),
    Net("GBE_MDI3N", "signal", [("J1", "202"), ("U3", "TX3N")], "千兆变压器", sig="GBE_MDI3"),
    Net("GBE_MDI3P", "signal", [("J1", "204"), ("U3", "TX3P")], "千兆变压器", sig="GBE_MDI3"),
    Net("GBE_LED_LINK", "signal", [("J1", "188"), ("J3", "LED_LINK")], "网口 LED", sig="GBE_LED_LINK"),
    Net("GBE_LED_ACT", "signal", [("J1", "194"), ("J3", "LED_ACT")], "网口 LED", sig="GBE_LED_ACT"),

    # ---------------- CSI 相机 ----------------
    Net("CSI0_D0N", "signal", [("J1", "4"), ("J10", "D0N")], "CSI 相机座 #1", sig="CSI0_D0"),
    Net("CSI0_D0P", "signal", [("J1", "6"), ("J10", "D0P")], "CSI 相机座 #1", sig="CSI0_D0"),
    Net("CSI0_D1N", "signal", [("J1", "16"), ("J10", "D1N")], "CSI 相机座 #1", sig="CSI0_D1"),
    Net("CSI0_D1P", "signal", [("J1", "18"), ("J10", "D1P")], "CSI 相机座 #1", sig="CSI0_D1"),
    Net("CSI0_CLKN", "signal", [("J1", "10"), ("J10", "CLKN")], "CSI 相机座 #1", sig="CSI0_CLK"),
    Net("CSI0_CLKP", "signal", [("J1", "12"), ("J10", "CLKP")], "CSI 相机座 #1", sig="CSI0_CLK"),
    Net("CAM0_PWDN", "signal", [("J1", "114"), ("J10", "PWDN")], "相机下电", sig="CAM0_PWDN"),
    Net("CAM0_MCLK", "signal", [("J1", "116"), ("J10", "MCLK")], "相机参考时钟", sig="CAM0_MCLK"),
    Net("CSI2_D0N", "signal", [("J1", "22"), ("J11", "D0N")], "CSI 相机座 #2", sig="CSI2_D0"),
    Net("CSI2_D0P", "signal", [("J1", "24"), ("J11", "D0P")], "CSI 相机座 #2", sig="CSI2_D0"),
    Net("CSI2_D1N", "signal", [("J1", "34"), ("J11", "D1N")], "CSI 相机座 #2", sig="CSI2_D1"),
    Net("CSI2_D1P", "signal", [("J1", "36"), ("J11", "D1P")], "CSI 相机座 #2", sig="CSI2_D1"),
    Net("CSI2_CLKN", "signal", [("J1", "28"), ("J11", "CLKN")], "CSI 相机座 #2", sig="CSI2_CLK"),
    Net("CSI2_CLKP", "signal", [("J1", "30"), ("J11", "CLKP")], "CSI 相机座 #2", sig="CSI2_CLK"),
    Net("CAM1_PWDN", "signal", [("J1", "120"), ("J11", "PWDN")], "相机下电", sig="CAM1_PWDN"),
    Net("CAM1_MCLK", "signal", [("J1", "122"), ("J11", "MCLK")], "相机参考时钟", sig="CAM1_MCLK"),

    # ---------------- I2C / UART / SPI / CAN ----------------
    Net("I2C0_SCL", "signal", [("J1", "185"), ("U9", "SCL"), ("R5", "1")],
        "载板 EEPROM：Orin 靠它识别载板（I2C0 @0x50）", sig="I2C0_SCL"),
    Net("I2C0_SDA", "signal", [("J1", "187"), ("U9", "SDA")], "载板 EEPROM", sig="I2C0_SDA"),
    Net("I2C1_SCL", "signal", [("J1", "189"), ("J12", "18")], "同步板 / 风扇 / 温度传感器", sig="I2C1_SCL"),
    Net("I2C1_SDA", "signal", [("J1", "191"), ("J12", "19")], "同步板 / 风扇 / 温度传感器", sig="I2C1_SDA"),
    Net("I2C2_SCL", "signal", [("J1", "232")],
        "模块 I2C2（同步板的 I2C2 是它自己的 AT24C02，不接这里）", sig="I2C2_SCL"),
    Net("I2C2_SDA", "signal", [("J1", "234")],
        "模块 I2C2（同步板的 I2C2 是它自己的 AT24C02，不接这里）", sig="I2C2_SDA"),
    # 电平转换器是"同一条线穿过器件"：A 脚和 B 脚必须在同一个网络里
    Net("UART_TXD", "signal", [("J1", "203"), ("U4", "A1"), ("U4", "B1"), ("J12", "12")],
        "同步板链路：Orin UART1 TXD → U4 → 同步板 RX（921600 8N1）", sig="UART1_TXD"),
    Net("UART_RXD", "signal", [("J1", "205"), ("U4", "A2"), ("U4", "B2"), ("J12", "13")],
        "同步板链路：同步板 TX → U4 → Orin UART1 RXD", sig="UART1_RXD"),
    Net("CAN_RX", "signal", [("J1", "143"), ("J15", "CAN_RX"), ("J16", "CAN_RX")],
        "模块 CAN 是 3.3 V 域，需与隔离收发器对接", sig="CAN_RX"),
    Net("CAN_TX", "signal", [("J1", "145"), ("J15", "CAN_TX"), ("J16", "CAN_TX")],
        "模块 CAN 是 3.3 V 域", sig="CAN_TX"),
    Net("UART0_TXD_18", "signal", [("J1", "99"), ("J17", "1")],
        "调试控制台（UART0，1.8 V，需电平转换到 USB-UART）", sig="UART0_TXD"),
    Net("UART0_RXD_18", "signal", [("J1", "101"), ("J17", "2")],
        "调试控制台（UART0）", sig="UART0_RXD"),
    Net("UART2_TXD_18", "signal", [("J1", "236"), ("U16", "A2")],
        "模块配置 RTK 用（1.8 V → U16 → 3.3 V）", sig="UART2_TXD"),
    Net("UART2_RXD_18", "signal", [("J1", "238"), ("R8", "1")],
        "RTK 的 NMEA（3.3 V 分压到 1.8 V）", sig="UART2_RXD"),

    # ---------------- GPIO ----------------
    Net("PPS_18", "signal", [("J1", "87"), ("U16", "A1")],
        "RTK 的 PPS（整形后 1.8 V）→ 模块 GPIO00，chrony 用它驯服系统时钟", sig="GPIO00"),
    Net("SYNC_TRIG_EN", "signal", [("J1", "118"), ("U6", "A3"), ("U6", "B3"), ("J12", "14")],
        "模块软件使能同步板的硬件触发", sig="GPIO01"),
    Net("GPIO_PPS_LOCKED", "signal", [("J1", "124"), ("U6", "A1"), ("U6", "B1"), ("J12", "23"),
                                      ("D2", "A"), ("R2", "1")],
        "同步板回报 PPS 锁定状态", sig="GPIO02"),
    Net("GPIO_FAN_PWM", "signal", [("J1", "126"), ("J18", "PWM")], "风扇 PWM 调速", sig="GPIO03"),
    Net("GPIO_PWR_BTN", "signal", [("J1", "127"), ("U10", "PB")], "电源键输入", sig="GPIO04"),
    Net("GPIO_LED_RUN", "signal", [("J1", "128"), ("D1", "A"), ("R1", "1")], "运行指示", sig="GPIO05"),
    Net("GPIO_5G_RESET_N", "signal", [("J1", "130"), ("Q2", "G"), ("J5", "RST_N")], "5G 模组复位", sig="GPIO06"),
    Net("GPIO_5G_PWRKEY", "signal", [("J1", "206"), ("Q1", "G"), ("J5", "PWRKEY")], "5G 模组 PWRKEY", sig="GPIO07"),
    Net("GPIO_FAN_TACH", "signal", [("J1", "208"), ("J18", "TACH")], "风扇转速", sig="GPIO08"),
    Net("GPIO_TRIG_STAT", "signal", [("J1", "212"), ("U6", "A2"), ("U6", "B2"), ("J12", "24")],
        "同步板触发运行状态", sig="GPIO10"),
    Net("GPIO_CAM_RESET_N", "signal", [("J1", "218"), ("J13", "RESET_IN")], "相机同步座复位输入", sig="GPIO12"),
    Net("GPIO_AUX1", "signal", [("J1", "211"), ("J17", "4")], "备用 GPIO", sig="GPIO09"),
    Net("GPIO_AUX2", "signal", [("J1", "216"), ("J17", "5")], "备用 GPIO", sig="GPIO11"),
    Net("GPIO_AUX3", "signal", [("J1", "228"), ("J17", "6")], "备用 GPIO", sig="GPIO13"),
    Net("GPIO_AUX4", "signal", [("J1", "230"), ("U6", "A4"), ("U6", "B4"), ("J12", "33")],
        "备用 GPIO（→ 同步板）", sig="GPIO14"),

    # ---------------- 同步板 mezzanine（J12） ----------------
    # 四路触发：同步板输出 3.3 V（J12），经 U5 转 1.8 V 后给相机同步座
    Net("SYNC_RESET_N", "signal", [("SW3", "1"), ("J12", "22")],
        "同步板复位（低有效，直接接同步板 NRST）"),
    Net("SYNC_TRIG0", "signal", [("J12", "27"), ("U5", "A1"), ("J21", "1")],
        "触发0 → 可见光相机 / 外部线束"),
    Net("SYNC_TRIG1", "signal", [("J12", "28"), ("U5", "A2"), ("J21", "2")],
        "触发1 → 雷达 / 外部线束"),
    Net("SYNC_TRIG2", "signal", [("J12", "29"), ("U5", "A3"), ("J21", "3")],
        "触发2 → IMU / 外部线束"),
    Net("SYNC_TRIG3", "signal", [("J12", "30"), ("U5", "A4"), ("J21", "4")],
        "触发3 → 红外相机 / 外部线束"),
    Net("TRIG0_18", "signal", [("U5", "B1"), ("J13", "VSYNC_IN")], "触发0 → 相机 1.8 V"),
    Net("TRIG1_18", "signal", [("U5", "B2"), ("J13", "TIMER_SYNC_IN")], "触发1 → 相机 1.8 V"),
    Net("TRIG2_18", "signal", [("U5", "B3"), ("J21", "5")], "触发2 → 1.8 V 副本（线束）"),
    Net("TRIG3_18", "signal", [("U5", "B4"), ("J21", "6")], "触发3 → 1.8 V 副本（线束）"),
    Net("SYNC_EXT_IN", "signal", [("J12", "34")], "外部触发输入"),
    Net("SYNC_EXT_ECHO", "signal", [("J12", "35")], "外部触发回读"),
    Net("PPS_CLEAN", "signal", [("U8", "Y"), ("U16", "B1"), ("J12", "38")],
        "RTK PPS 整形后（3.3 V）：一份给同步板，一份经 U16 转 1.8 V 给模块"),
    Net("NMEA_33", "signal", [("U17", "Y"), ("J12", "39"), ("R8", "2"), ("R9", "1")],
        "RTK NMEA（3.3 V）：给同步板，同时分压到 1.8 V 给模块"),
    Net("NMEA_TXD_33", "signal", [("U16", "B2"), ("J14", "5")], "模块配置 RTK（3.3 V）"),
    Net("SYNC_AUX1", "signal", [("J12", "41")], "同步板辅助 IO"),
    Net("SYNC_AUX2", "signal", [("J12", "42")], "同步板辅助 IO"),
    Net("SYNC_AUX3", "signal", [("J12", "43")], "同步板辅助 IO"),
    Net("CAM_ECHO_VSYNC", "signal", [("J13", "VSYNC_OUT"), ("U7", "A1"), ("U7", "B1"),
                                      ("J12", "25")], "相机 VSYNC 回读 → 同步板"),
    Net("CAM_ECHO_TIMER", "signal", [("J13", "TIMER_SYNC_OUT"), ("U7", "A2"), ("U7", "B2"),
                                      ("J12", "26")], "相机 TIMER_SYNC 回读 → 同步板"),

    # ---------------- RTK ----------------
    Net("RTK_5V", "power", [("J14", "1")], "RTK 接收机供电"),
    Net("RTK_PPS", "signal", [("J14", "3"), ("U8", "A")], "RTK 的 1PPS（3.3 V TTL）"),
    Net("RTK_NMEA", "signal", [("J14", "4"), ("U17", "A")], "RTK 的 NMEA 输出（3.3 V TTL）"),

    # ---------------- SIM ----------------
    Net("SIM_CLK", "signal", [("J20", "CLK"), ("J5", "SIM_CLK")], "5G SIM（脚位按所选模组核对）"),
    Net("SIM_RST", "signal", [("J20", "RST"), ("J5", "SIM_RST")], "5G SIM 复位"),
    Net("SIM_IO", "signal", [("J20", "IO"), ("J5", "SIM_IO")], "5G SIM 数据"),
    Net("SIM_PWR", "power", [("J20", "VCC"), ("J5", "SIM_VCC")],
        "SIM 供电（按模组要求从 +3V3 经 50 Ω 串阻引出）"),
]


def check() -> list[str]:
    problems: list[str] = []
    refs = {p.ref for p in PARTS}
    used: dict[tuple[str, str], str] = {}
    for net in NETS:
        seen: set[tuple[str, str]] = set()
        for ref, pin in net.nodes:
            if ref not in refs:
                problems.append(f"网络 {net.name}: 未登记的器件 {ref}")
                continue
            if (ref, pin) in seen:
                problems.append(f"网络 {net.name}: {ref}.{pin} 重复")
            seen.add((ref, pin))
            key = (ref, pin)
            if key in used:
                problems.append(f"{ref}.{pin} 同时在 {used[key]} 与 {net.name}")
            used[key] = net.name
    # 模块引脚必须存在，且信号名要匹配网络声明的前缀
    for net in NETS:
        for ref, pin in net.nodes:
            if ref != M or not pin.isdigit():
                continue
            num = int(pin)
            if num not in PIN_NAME:
                problems.append(f"网络 {net.name}: 模块 pin {num} 不在数据手册引脚表里")
                continue
            if net.sig and net.sig not in PIN_NAME[num]:
                problems.append(
                    f"网络 {net.name}: 模块 pin {num} 实际是 {PIN_NAME[num]}，"
                    f"与声明的 {net.sig} 不符")
    # 电平转换器：同一条线穿过器件，A/B 脚必须在同一个网络里
    # （U5 是单向 A→B 的 74AVC4T245，两侧本来就分开，不在此列）
    for ref in ("U4", "U6", "U7"):
        chan: dict[str, str] = {}
        for net in NETS:
            for r, pin in net.nodes:
                if r != ref:
                    continue
                if pin in ("GND", "VCCA", "VCCB", "DIR"):
                    continue
                if len(pin) < 2 or pin[0] not in "AB" or not pin[1:].isdigit():
                    problems.append(f"{ref}.{pin} 不是 A/B 通道脚")
                    continue
                key = pin[1:]
                if key in chan and chan[key] != net.name:
                    problems.append(
                        f"{ref} 通道 {key} 的 A/B 脚被拆到了 {chan[key]} 与 {net.name}"
                        f" 两个网络——同一条线穿过转换器，必须是同一个网络")
                chan[key] = net.name
    # 模块所有 GND 脚都要接到 GND 网络
    gnd_net = next(n for n in NETS if n.name == "GND")
    gnd_mod = {int(p) for r, p in gnd_net.nodes if r == M}
    missing = GND_PINS - gnd_mod
    extra = gnd_mod - GND_PINS
    if missing:
        problems.append(f"模块 GND 脚未接 GND 网络: {sorted(missing)}")
    if extra:
        problems.append(f"GND 网络里混进了非 GND 模块脚: {sorted(extra)}")
    return problems


def label(ref: str, pin: str) -> str:
    if ref == M and pin.isdigit():
        return f"J1.{pin} ({PIN_NAME.get(int(pin), '?')})"
    return f"{ref}.{pin}"


def gen_markdown() -> str:
    lines = [
        "# Orin NX 载板连接表",
        "",
        "> 由 `tools/gen_connections.py` 生成，请勿手工编辑。",
        "> 模块引脚号与信号名来自 `pins_full.json`（Orin NX 数据手册 "
        "DS-10712-001 的 Pin List 表，已与各接口表交叉验证）。",
        "> 载板侧器件用功能引脚名，出原理图时按各器件数据手册替换为封装脚号。",
        "",
        "## 网络",
        "",
        "| 网络 | 类型 | 连接 | 说明 |",
        "| --- | --- | --- | --- |",
    ]
    for net in NETS:
        nodes = ", ".join(label(r, p) for r, p in net.nodes)
        lines.append(f"| `{net.name}` | {net.kind} | {nodes} | {net.note} |")
    lines += ["", "## 器件", "",
              "| 位号 | 型号 | 封装 | MPN | 说明 |",
              "| --- | --- | --- | --- | --- |"]
    for p in PARTS:
        lines.append(f"| {p.ref} | {p.value} | `{p.footprint}` | {p.mpn} | {p.note} |")
    lines.append("")
    return "\n".join(lines)


def gen_bom() -> list[list[str]]:
    rows = [["位号", "型号", "封装", "MPN", "说明"]]
    for p in PARTS:
        rows.append([p.ref, p.value, p.footprint, p.mpn, p.note])
    return rows


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args()

    problems = check()
    for p in problems:
        print(f"PROBLEM: {p}", file=sys.stderr)
    if problems:
        return 1
    if args.check:
        print(f"连接表自洽：{len(PARTS)} 器件，{len(NETS)} 网络，"
              f"模块 {len(PIN_NAME)} 脚全部可溯源，GND {len(GND_PINS)} 脚已全部落网")
        return 0
    (ROOT / "connections.md").write_text(gen_markdown(), encoding="utf-8")
    with (ROOT / "bom.csv").open("w", newline="", encoding="utf-8") as fh:
        csv.writer(fh).writerows(gen_bom())
    print(f"已写出 connections.md 与 bom.csv（{len(PARTS)} 器件 / {len(NETS)} 网络）")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
