#!/usr/bin/env python3
"""兼容夹具断言：3 元素 _BST（无电压）+ 全 0 _BIX → 回退 _BIF，仍读出 57%。

数据源: desk-lines.txt、02-panel.png、03-settings.png。
"""
import os
import numpy as np
from PIL import Image

ACCENT = (201, 169, 110)
ROOT = os.path.dirname(os.path.abspath(__file__))
FAILS, CHECKS = [], []


def check(name, cond, detail=""):
    CHECKS.append(name)
    if not cond:
        FAILS.append(name)
    print("[%s] %s%s" % ("PASS" if cond else "FAIL", name, (" — " + detail) if detail else ""))


def load(p):
    return np.array(Image.open(os.path.join(ROOT, p)).convert("RGB")).astype(int)


def main():
    with open(os.path.join(ROOT, "desk-lines.txt"), "rb") as f:
        ser = f.read().decode("utf-8", "replace")
    lines = [l for l in ser.splitlines() if l.startswith("[BAT] aml ")]

    check("A1 选中 CMP0", "[BAT] aml dev=selected CMP0" in ser)
    check("A2 至少 3 次周期采样", len(lines) >= 3, "samples=%d" % len(lines))
    ok = [l for l in lines if "pct=57" in l and "chg=1" in l and "rem=5700" in l
          and "full=10000" in l and "bif=1" in l and "health=100" in l]
    check("A3 坏 _BIX 回退 _BIF，读出 57%", len(ok) >= 3, "matched=%d" % len(ok))
    check("A4 没有把坏 _BIX 当可信包", "bix=1" not in ser)
    check("A5 无 bst failed / untrusted", "bst failed" not in ser and "untrusted" not in ser)
    check("A6 AC 仍在线", "[BAT] ac psr=1" in ser)

    d2 = load("02-panel.png")
    n = int(((abs(d2[108:165, 1390:1500, 0] - ACCENT[0]) <= 16) &
             (abs(d2[108:165, 1390:1500, 1] - ACCENT[1]) <= 16) &
             (abs(d2[108:165, 1390:1500, 2] - ACCENT[2]) <= 16)).sum())
    check("B1 面板大号数字为 accent 色", n > 200, "accent_px=%d" % n)
    check("B2 串口记录 panel open", "[DESK] battery panel open" in ser)

    d3 = load("03-settings.png")
    nav = d3[518:560, 639:818]
    ratio = float(((abs(nav[:, :, 0] - ACCENT[0]) <= 16) &
                   (abs(nav[:, :, 1] - ACCENT[1]) <= 16) &
                   (abs(nav[:, :, 2] - ACCENT[2]) <= 16)).sum()) / nav[:, :, 0].size
    check("C1 Battery 导航项高亮", ratio > 0.5, "accent=%.2f" % ratio)
    check("C2 串口记录 settings page=battery", "[DESK] battery panel -> settings page=battery" in ser)
    val = d3[315:382, 1022:1260]
    ink = int((val.sum(axis=2) < 450).sum())
    check("C3 Settings 值列有实际内容", ink > 80, "ink=%d" % ink)

    print("\ncompat_check: %d/%d passed" % (len(CHECKS) - len(FAILS), len(CHECKS)))
    return 1 if FAILS else 0


if __name__ == "__main__":
    raise SystemExit(main())
