#!/usr/bin/env python3
"""AC 离线夹具断言：ACPI0003._PSR=0 时设置页显示 Offline，且电池读数仍为 57%。"""
import os
import sys

import numpy as np
from PIL import Image

ACCENT = (201, 169, 110)
ROOT = os.path.dirname(os.path.abspath(__file__))
EVID = os.path.dirname(ROOT)
FAILS, CHECKS = [], []


def check(name, cond, detail=""):
    CHECKS.append(name)
    if not cond:
        FAILS.append(name)
    print("[%s] %s%s" % ("PASS" if cond else "FAIL", name, (" — " + detail) if detail else ""))


def load(path):
    return np.array(Image.open(path).convert("RGB")).astype(int)


def main():
    with open(os.path.join(ROOT, "desk-lines.txt"), "rb") as f:
        ser = f.read().decode("utf-8", "replace")
    check("A1 找到 ACPI0003 设备 AC00", "[BAT] ac dev=AC00" in ser)
    check("A2 _PSR=0 交流离线", "[BAT] ac psr=0" in ser)
    check("A3 电池仍读 57% 且未充电", "pct=57" in ser and "chg=0" in ser)

    shot = os.path.join(ROOT, "03-settings.png")
    check("B1 设置页截图存在", os.path.getsize(shot) > 100000)
    img = load(shot)
    nav = img[525:557, 641:817]
    m = (np.abs(nav[:, :, 0] - ACCENT[0]) <= 16) & \
        (np.abs(nav[:, :, 1] - ACCENT[1]) <= 16) & \
        (np.abs(nav[:, :, 2] - ACCENT[2]) <= 16)
    check("B2 Battery 导航项高亮", float(m.mean()) > 0.5, "accent=%.2f" % m.mean())

    ref = load(os.path.join(EVID, "m2-valid", "03-settings.png"))
    ac_row = (835, 378, 1150, 405)
    diff = int((np.abs(img[ac_row[1]:ac_row[3], ac_row[0]:ac_row[2]] -
                       ref[ac_row[1]:ac_row[3], ac_row[0]:ac_row[2]]).sum(axis=2) > 30).sum())
    check("C1 AC 行与在线夹具不同", diff > 50, "diff_px=%d" % diff)

    print()
    print("RESULT: %s (%d checks, %d failed)" %
          ("PASS" if not FAILS else "FAIL", len(CHECKS), len(FAILS)))
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
