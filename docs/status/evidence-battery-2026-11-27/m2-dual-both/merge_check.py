#!/usr/bin/env python3
"""两块都 present 的 QEMU 取证：30% + 70% 合并成 50%。"""
import os
import sys

import numpy as np
from PIL import Image

ACCENT = (201, 169, 110)
ROOT = os.path.dirname(os.path.abspath(__file__))
FAILS = []
CHECKS = []


def check(name, cond, detail=""):
    CHECKS.append((name, bool(cond), detail))
    if not cond:
        FAILS.append(name)
    print("[%s] %s%s" % ("PASS" if cond else "FAIL", name, (" — " + detail) if detail else ""))


def load(p):
    return np.array(Image.open(os.path.join(ROOT, p)).convert("RGB")).astype(int)


def accent_count(img, box, tol=16):
    a = img[box[1]:box[3], box[0]:box[2]]
    m = (np.abs(a[:, :, 0] - ACCENT[0]) <= tol) & \
        (np.abs(a[:, :, 1] - ACCENT[1]) <= tol) & \
        (np.abs(a[:, :, 2] - ACCENT[2]) <= tol)
    return int(m.sum())


def accent_ratio(img, box, tol=16):
    a = img[box[1]:box[3], box[0]:box[2]]
    m = (np.abs(a[:, :, 0] - ACCENT[0]) <= tol) & \
        (np.abs(a[:, :, 1] - ACCENT[1]) <= tol) & \
        (np.abs(a[:, :, 2] - ACCENT[2]) <= tol)
    return float(m.sum()) / m.size


def main():
    with open(os.path.join(ROOT, "desk-lines.txt"), "rb") as f:
        ser = f.read().decode("utf-8", "replace")
    lines = [l for l in ser.splitlines() if l.startswith("[BAT] aml ")]

    check("M1 枚举两个槽位", "[BAT] aml candidates=2" in ser)
    check("M2 首选 DUB0", "[BAT] aml dev=selected DUB0" in ser)
    ok = [l for l in lines if "pct=50" in l and "chg=1" in l and "rem=10000" in l
          and "full=20000" in l and "slots=2" in l]
    check("M3 容量合并 3000+7000 / 10000+10000", len(ok) >= 3, "matched=%d" % len(ok))
    check("M4 合并后 50%", any("pct=50" in l for l in lines))
    check("M5 健康度仍由合并容量算", any("health=100" in l for l in lines))

    d2 = load("02-panel.png")
    check("M6 面板大号数字为 50%", accent_count(d2, (1390, 108, 1500, 165)) > 150)
    d3 = load("03-settings.png")
    check("M7 Battery 导航项高亮", accent_ratio(d3, (639, 518, 818, 560)) > 0.5)

    print()
    print("RESULT: %s (%d checks, %d failed)" %
          ("PASS" if not FAILS else "FAIL", len(CHECKS), len(FAILS)))
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
