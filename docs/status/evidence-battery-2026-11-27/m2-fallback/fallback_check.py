#!/usr/bin/env python3
"""第一个 present 槽位坏、第二个可用时的 QEMU 回退取证。"""
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

    check("F1 枚举两个槽位", "[BAT] aml candidates=2" in ser)
    check("F2 init 先选 DUF0", "[BAT] aml dev=selected DUF0" in ser)
    check("F3 DUF0 确实失败（缺 _BST）", "[BAT] aml bst failed code=-1" in ser)
    check("F4 poll 运行中切到 DUF1", "[BAT] aml active=DUF1" in ser)
    ok = [l for l in lines if "pct=57" in l and "chg=1" in l and "slots=1" in l]
    check("F5 回退后只报一个有效槽位", len(ok) >= 3, "matched=%d" % len(ok))

    d2 = None
    p = os.path.join(ROOT, "02-panel.png")
    if os.path.exists(p):
        d2 = np.array(Image.open(p).convert("RGB")).astype(int)
        check("F6 面板显示回退后的 57%", accent_count(d2, (1390, 108, 1500, 165)) > 200)
    else:
        check("F6 面板截图存在", False)

    d3 = np.array(Image.open(os.path.join(ROOT, "03-settings.png")).convert("RGB")).astype(int)
    check("F7 Settings 落在 Battery 页", accent_ratio(d3, (639, 518, 818, 560)) > 0.5)

    print()
    print("RESULT: %s (%d checks, %d failed)" %
          ("PASS" if not FAILS else "FAIL", len(CHECKS), len(FAILS)))
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
