#!/usr/bin/env python3
"""从 Orin NX 模块数据手册的 PDF 文本里提取 260-pin 引脚表。

用途：载板接口分配要有真实的模块引脚号，不能靠记忆。

数据来源（自行下载后用 pdftotext -layout 转成文本再喂给本脚本）：
  ConnectTech《Jetson Orin NX Data Sheet》DS-10712-001
  https://connecttech.com/ftp/pdf/jetson_orin_nx_datasheet.pdf
  （NVIDIA 原版为 Jetson Orin NX Series Data Sheet，章节 1.9.x 给出各接口引脚表）

用法:
    pdftotext -layout jetson_orin_nx_datasheet.pdf ds.txt
    python3 tools/extract_orin_pins.py ds.txt > pins_orin_nx.json
"""
from __future__ import annotations

import json
import re
import sys
from collections import Counter

DIRECTIONS = {
    "Input", "Output", "Bidir", "Passive", "NC",
    "Input/Output", "Power", "Analog",
}


def parse(text: str) -> list[dict]:
    pins: list[dict] = []
    for raw in text.split("\n"):
        line = raw.strip()
        if not line or "JETSON | ORIN NX" in line or "Ampere GPU" in line:
            continue
        if line.startswith("Table "):
            continue
        fields = [f.strip() for f in re.split(r"\s{2,}", line) if f.strip()]
        if len(fields) < 3:
            continue
        if not re.fullmatch(r"\d{1,3}", fields[0]):
            continue
        if not re.fullmatch(r"[A-Z][A-Z0-9_#\-\.\[\]/]*", fields[1]):
            continue
        direction = None
        for i, f in enumerate(fields):
            if f in DIRECTIONS:
                direction = (i, f)
                break
        if direction is None:
            continue
        desc = " ".join(fields[2:direction[0]])
        pin_type = fields[-1] if direction[0] != len(fields) - 1 else ""
        if pin_type in DIRECTIONS:
            pin_type = ""
        pins.append({
            "pin": int(fields[0]),
            "name": fields[1],
            "desc": desc,
            "dir": direction[1],
            "type": pin_type,
        })
    pins.sort(key=lambda p: p["pin"])
    return pins


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__, file=sys.stderr)
        return 2
    with open(sys.argv[1], encoding="utf-8", errors="replace") as fh:
        pins = parse(fh.read())
    counts = Counter(p["name"].split("_")[0] for p in pins)
    json.dump(
        {
            "source": "ConnectTech Jetson Orin NX Data Sheet DS-10712-001 "
                      "(NVIDIA Jetson Orin NX Series Data Sheet, tables 1.9.x)",
            "url": "https://connecttech.com/ftp/pdf/jetson_orin_nx_datasheet.pdf",
            "pins": pins,
            "groups": dict(counts),
        },
        sys.stdout,
        ensure_ascii=False,
        indent=1,
    )
    print()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
