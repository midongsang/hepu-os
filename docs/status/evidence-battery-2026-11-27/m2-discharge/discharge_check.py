#!/usr/bin/env python3
"""放电夹具取证断言：真实 _BST 速率 → 健康度与剩余时间。

数据源:
  - desk-lines.txt  本次 DIS0 放电运行串口摘录
  - 02-panel.png    顶栏/面板仍显示 57%、未标充电
  - 03-settings.png Battery 页含 Health/Time to empty 行
"""
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


def ink(img, box):
    a = img[box[1]:box[3], box[0]:box[2]]
    return int((a.sum(axis=2) < 450).sum())


def main():
    with open(os.path.join(ROOT, "desk-lines.txt"), "rb") as f:
        ser = f.read().decode("utf-8", "replace")
    lines = [l for l in ser.splitlines() if l.startswith("[BAT] aml ")]

    check("D1 选中真实电池槽位 DIS0", "[BAT] aml dev=selected DIS0" in ser)
    ok = [l for l in lines if "pct=57" in l and "chg=0" in l and "rem=5700" in l
          and "full=10000" in l and "rate=1200" in l]
    check("D2 放电读数 57% / 1200mW", len(ok) >= 3, "matched=%d" % len(ok))
    check("D3 _BST 速率换算剩余 285 分钟、满充时间不可用",
          any("health=100" in l and "min=285" in l and "min_full=-1" in l for l in lines))
    check("D4 交流离线 _PSR=0", "[BAT] ac psr=0" in ser)
    check("D5 无降级/假读数路径", "bst failed" not in ser and "untrusted" not in ser)

    d2 = load("02-panel.png")
    check("D6 面板大号数字仍为 accent 色 57%", accent_count(d2, (1390, 108, 1500, 165)) > 200)

    d3 = load("03-settings.png")
    nav_bat = (639, 518, 818, 560)
    check("D7 Battery 导航项高亮", accent_ratio(d3, nav_bat) > 0.5)
    health_row = (1022, 390, 1260, 418)
    time_row = (1022, 424, 1260, 452)
    check("D8 Health/Time to empty 两行均有值",
          ink(d3, health_row) > 20 and ink(d3, time_row) > 20,
          "health_ink=%d time_ink=%d" % (ink(d3, health_row), ink(d3, time_row)))

    print()
    print("RESULT: %s (%d checks, %d failed)" %
          ("PASS" if not FAILS else "FAIL", len(CHECKS), len(FAILS)))
    if FAILS:
        print("failed:", ", ".join(FAILS))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
