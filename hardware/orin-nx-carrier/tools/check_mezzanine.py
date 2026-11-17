#!/usr/bin/env python3
"""校验载板 J12 与同步板 J10 的 mezzanine 引脚一一对应。

两个连接表是各自独立的（不同板、不同网络名），但插在一起就是一条线。
这个脚本按**明确的对应表**检查两边：任何一边改了引脚而另一边没改，
或者某个脚在一边悬空在另一边有信号，都会报出来。

用法:
    python3 tools/check_mezzanine.py
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SYNC = ROOT.parent / "stm32h7-sync-board" / "connections.md"

# J12 脚号 -> (载板网络, 同步板网络)；None 表示该脚在同步板侧悬空
EXPECT: dict[int, tuple[str | None, str | None]] = {
    1: ("+5V", "+5V"),
    2: ("+5V", "+5V"),
    3: ("+3V3", None),          # 同步板自带 3.3 V，备用
    4: ("+3V3", None),
    5: ("+1V8", None),          # 同步板 IO 是 3.3 V，备用
    6: ("+1V8", None),
    7: ("GND", "GND"),
    8: ("GND", "GND"),
    9: ("GND", "GND"),
    10: ("GND", "GND"),
    11: ("CLK_32K", None),      # 同步板用本地 32.768 kHz 晶振
    12: ("UART_TXD", "HOST_RX_MCU"),   # 模块 → 同步板
    13: ("UART_RXD", "HOST_TX_MCU"),   # 同步板 → 模块
    14: ("SYNC_TRIG_EN", "MEZZ_TRIG_EN"),
    15: ("GND", "GND"),
    16: ("GND", "GND"),
    17: ("GND", "GND"),
    18: ("I2C1_SCL", "I2C1_SCL"),
    19: ("I2C1_SDA", "I2C1_SDA"),
    20: ("GND", "GND"),
    21: ("GND", "GND"),
    22: ("SYNC_RESET_N", "NRST"),
    23: ("GPIO_PPS_LOCKED", "MEZZ_PPS_LOCKED"),
    24: ("GPIO_TRIG_STAT", "MEZZ_TRIG_STAT"),
    25: ("CAM_ECHO_VSYNC", "MEZZ_CAM_VSYNC"),
    26: ("CAM_ECHO_TIMER", "MEZZ_CAM_TIMER"),
    27: ("SYNC_TRIG0", "TRIG0_OUT"),
    28: ("SYNC_TRIG1", "TRIG1_OUT"),
    29: ("SYNC_TRIG2", "TRIG2_OUT"),
    30: ("SYNC_TRIG3", "TRIG3_OUT"),
    31: ("GND", "GND"),
    32: ("GND", "GND"),
    33: ("GPIO_AUX4", "MEZZ_AUX4"),
    34: ("SYNC_EXT_IN", "EXT_IN_RAW"),
    35: ("SYNC_EXT_ECHO", "STATUS_OUT"),
    36: ("GND", "GND"),
    37: ("GND", "GND"),
    38: ("PPS_CLEAN", "PPS_RAW"),
    39: ("NMEA_33", "GPS_RX_MCU"),
    40: ("GND", "GND"),
    41: ("SYNC_AUX1", "MEZZ_AUX5"),
    42: ("SYNC_AUX2", "MEZZ_AUX6"),
    43: ("SYNC_AUX3", "MEZZ_AUX7"),
    44: ("GND", "GND"),
}

ROW = re.compile(r"^\|\s*`([^`]+)`\s*\|[^|]*\|([^|]*)\|")


def parse(path: Path) -> dict[str, set[str]]:
    """从 connections.md 读出 {网络: {器件.引脚}}。"""
    nets: dict[str, set[str]] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        m = ROW.match(line)
        if not m:
            continue
        net, nodes = m.group(1), m.group(2)
        for node in nodes.split(","):
            node = node.strip()
            if node:
                nets.setdefault(net, set()).add(node)
    return nets


def net_of(nets: dict[str, set[str]], ref: str, pin: str) -> str | None:
    for net, nodes in nets.items():
        if f"{ref}.{pin}" in nodes:
            return net
    return None


def main() -> int:
    problems: list[str] = []
    carrier = parse(ROOT / "connections.md")
    if not SYNC.exists():
        print(f"找不到同步板连接表 {SYNC}", file=sys.stderr)
        return 2
    sync = parse(SYNC)

    for pin, (want_carrier, want_sync) in sorted(EXPECT.items()):
        got_carrier = net_of(carrier, "J12", str(pin))
        got_sync = net_of(sync, "J10", str(pin))
        if got_carrier != want_carrier:
            problems.append(
                f"J12.{pin}: 载板上是 {got_carrier}，期望 {want_carrier}")
        if got_sync != want_sync:
            problems.append(
                f"J10.{pin}: 同步板上是 {got_sync}，期望 {want_sync}")

    # 两边都必须恰好用满 44 脚
    for ref, nets, other in (("J12", carrier, "同步板"), ("J10", sync, "载板")):
        used = {p for net in nets for n in nets[net]
                if n.startswith(f"{ref}.") for p in n.split(".")[1:]}
        stray = used - {str(p) for p in EXPECT}
        if stray:
            problems.append(f"{ref} 上有表外的脚: {sorted(stray)}")
        print(f"{ref}: 用了 {len(used)}/44 脚")

    for p in problems:
        print(f"PROBLEM: {p}", file=sys.stderr)
    if problems:
        return 1
    print(f"mezzanine 一致：J12 ↔ J10 共 {len(EXPECT)} 脚逐脚对上")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
