#!/usr/bin/env python3
"""从数据手册的 "Jetson Orin NX Pin List" 表解析完整的 260 脚。

数据手册那张表是左右双栏排版：

    Jetson SODIMM | Jetson Orin NX | Pin # | Pin # | Jetson SODIMM | Jetson Orin NX
     Signal Name  |    Function    | Top   |Bottom | Signal Name  |   Function
                   |                | Odd   | Even   |

左半的 "Jetson Orin NX Function" 对应**奇数脚**，右半的对应**偶数脚**；
另外两列是 Orin Nano (SODIMM) 在同一脚号上的叫法，仅作对照。

用法:
    pdftotext -layout jetson_orin_nx_datasheet.pdf ds.txt
    python3 tools/extract_pinlist.py ds.txt > pins_full.json
"""
from __future__ import annotations

import json
import re
import sys

SECTION = "Jetson Orin NX Pin List"


def parse(text: str) -> list[dict]:
    lines = text.split("\n")
    # 定位引脚表：表头行之后直到出现 "Table 26"
    start = None
    for i, ln in enumerate(lines):
        if ln.strip() == SECTION and i > 100:
            start = i
            break
    if start is None:
        raise SystemExit("找不到 Jetson Orin NX Pin List 表")
    end = start
    for i in range(start, min(start + 400, len(lines))):
        if "Table 26" in lines[i]:
            end = i
            break

    pins: dict[int, str] = {}
    notes: dict[int, str] = {}
    row = re.compile(
        r"^\s*(?P<a>\S+)\s+(?P<b>\S+)\s+(?P<odd>\d{1,3})\s+(?P<even>\d{1,3})\s+"
        r"(?P<e>\S+)\s+(?P<f>\S+(?:\s+\([^)]*\))?)\s*$"
    )
    for ln in lines[start:end]:
        m = row.match(ln)
        if not m:
            continue
        odd, even = int(m["odd"]), int(m["even"])
        if odd % 2 != 1:
            continue
        # 奇数脚用左半的 Orin NX Function，偶数脚用右半的
        for num, raw in ((odd, m["b"]), (even, m["f"])):
            note = ""
            if "(" in raw:
                raw, note = raw.split("(", 1)
                raw = raw.strip()
                note = note.rstrip(")")
            pins[num] = raw.rstrip("*")
            if note:
                notes[num] = note
    return [{"pin": p, "name": pins[p], **({"note": notes[p]} if p in notes else {})}
            for p in sorted(pins)]


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__, file=sys.stderr)
        return 2
    text = open(sys.argv[1], encoding="utf-8", errors="replace").read()
    pins = parse(text)

    # 与从接口表解析的结果交叉验证（接口表 pin <-> 引脚表 pin 必须一致）
    conflicts = []
    try:
        old = json.load(open("pins_orin_nx.json", encoding="utf-8"))
    except FileNotFoundError:
        old = None
    if old:
        new_map = {p["pin"]: p["name"] for p in pins}
        for p in old["pins"]:
            n = new_map.get(p["pin"])
            if n is None:
                conflicts.append(f"引脚表缺 pin {p['pin']} ({p['name']})")
            elif n != p["name"]:
                conflicts.append(f"pin {p['pin']}: 接口表 {p['name']} vs 引脚表 {n}")

    out = {
        "source": "ConnectTech Jetson Orin NX Data Sheet DS-10712-001, "
                  "Jetson Orin NX Pin List 表",
        "url": "https://connecttech.com/ftp/pdf/jetson_orin_nx_datasheet.pdf",
        "pins": pins,
        "gnd_pins": [p["pin"] for p in pins if p["name"] == "GND"],
        "conflicts": conflicts,
    }
    json.dump(out, sys.stdout, ensure_ascii=False, indent=1)
    print()
    if conflicts:
        print(f"冲突 {len(conflicts)} 处:", file=sys.stderr)
        for c in conflicts:
            print("  " + c, file=sys.stderr)
        return 1
    print(f"解析 {len(pins)} 脚，与接口表完全一致", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
