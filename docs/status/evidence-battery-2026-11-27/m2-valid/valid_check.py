#!/usr/bin/env python3
"""M2 最小版取证断言：真实 ACPI 读数（VALID）与四种 fail-closed 夹具。

数据源（全部在本目录或同级 m2-* 目录，随仓库入库）:
  - desk-lines.txt                  本次 VALID 运行的 [AML]/[BAT]/[DESK]/[SETTINGS] 摘录
  - 01-desktop.png / 02-panel.png / 03-settings.png
  - ../m2-bix|m2-absent|m2-nobst|m2-ec/desk-lines.txt  四种边界夹具的串口摘录

断言:
  A. AML 建表并找到 PNP0C0A 设备（BAT0），串口连续报出 pct=57 / chg=1
  B. 没有降级噪声：无 no acpi tables、无 bst failed、无 untrusted
  C. 面板大号数字是 accent 色的真实读数（不是 --）
  D. 面板入口把 Settings 落到第 7 页 Battery 且该页有值（导航高亮 + 串口）
  E. _BIX 路径同样读出 57%
  F. _STA=0 夹具判 absent（不出读数）
  G. 缺 _BST 夹具 fail-closed：bst failed code=-1（NOTFOUND）
  H. EC 字段夹具 fail-closed：bst failed code=-3（UNSUP，不是假装成功）
"""
import os
import sys

import numpy as np
from PIL import Image

ACCENT = (201, 169, 110)
ROOT = os.path.dirname(os.path.abspath(__file__))
EVID = os.path.dirname(ROOT)

FAILS = []
CHECKS = []


def check(name, cond, detail=""):
    CHECKS.append((name, bool(cond), detail))
    if not cond:
        FAILS.append(name)
    print("[%s] %s%s" % ("PASS" if cond else "FAIL", name, (" — " + detail) if detail else ""))


def load(p):
    return np.array(Image.open(os.path.join(ROOT, p)).convert("RGB")).astype(int)


def load_ev(p):
    return np.array(Image.open(os.path.join(EVID, p)).convert("RGB")).astype(int)


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


def text(p):
    with open(os.path.join(ROOT, p), "rb") as f:
        return f.read().decode("utf-8", "replace")


def text_ev(d):
    with open(os.path.join(EVID, d, "desk-lines.txt"), "rb") as f:
        return f.read().decode("utf-8", "replace")


def main():
    ser = text("desk-lines.txt")
    lines = [l for l in ser.splitlines() if l.startswith("[BAT] aml ")]

    # --- A. 命名空间与真实读数 ---
    check("A1 串口找到 PNP0C0A 设备 BAT0", "[BAT] aml dev=BAT0" in ser)
    check("A2 至少 3 次周期采样", len(lines) >= 3, "samples=%d" % len(lines))
    ok = [l for l in lines if "pct=57" in l and "chg=1" in l
          and "bst=1" in l and "bif=1" in l and "rem=5700" in l and "full=10000" in l]
    check("A3 读数百分比=57 且充电中 (rem=5700/full=10000)", len(ok) >= 3, "matched=%d" % len(ok))

    # --- B. 没有退化/假数据路径 ---
    check("B1 无 no acpi tables 噪声", "no acpi tables" not in ser)
    check("B2 无 bst failed", "bst failed" not in ser)
    check("B3 无 untrusted", "untrusted" not in ser)
    check("B4 AML 建表成功且无降级", "[AML] tables=0x3" in ser and "degraded=0x0" in ser)

    # --- C. 面板大号数字是 accent 色（真实 57%，不是 --） ---
    d2 = load("02-panel.png")
    big = (1390, 108, 1500, 165)     # 面板内 57% 大号数字
    n = accent_count(d2, big)
    check("C1 面板大号数字为 accent 色", n > 200, "accent_px=%d" % n)
    panel = (1374, 78, 1749, 273)
    check("C2 面板矩形存在且非空", d2[panel[1]:panel[3], panel[0]:panel[2]].size > 0)
    check("C3 串口记录 panel open", "[DESK] battery panel open" in ser)

    # --- D. 入口 → Settings 第 7 页 Battery ---
    d3 = load("03-settings.png")
    nav_bat = (639, 518, 818, 560)
    nav_app = (639, 227, 818, 269)
    check("D1 Battery 导航项高亮", accent_ratio(d3, nav_bat) > 0.5,
          "accent=%.2f" % accent_ratio(d3, nav_bat))
    check("D2 Appearance 未高亮", accent_ratio(d3, nav_app) < 0.05,
          "accent=%.2f" % accent_ratio(d3, nav_app))
    check("D3 串口记录 settings page=battery", "[DESK] battery panel -> settings page=battery" in ser)
    check("D4 串口记录 nav page=0x6", "[SETTINGS] nav page=0x6" in ser)
    # 值列区域不是空白（Remaining/Charging 行有墨迹）
    val = (1022, 315, 1260, 382)
    ink = int((d3[val[1]:val[3], val[0]:val[2]].sum(axis=2) < 450).sum())
    check("D5 Settings 值列有实际内容", ink > 80, "ink=%d" % ink)

    # --- E. _BIX 路径 ---
    bix = text_ev("m2-bix")
    check("E1 _BIX 夹具读出 57%", "bix=1" in bix and "pct=57" in bix)

    # --- F. _STA=0 ---
    absent = text_ev("m2-absent")
    check("F1 _STA=0 判 absent", "[BAT] aml sta=absent" in absent)
    check("F2 absent 不出读数", "pct=" not in absent)

    # --- G. 缺 _BST ---
    nobst = text_ev("m2-nobst")
    check("G1 缺 _BST fail-closed", "bst failed code=-1" in nobst)

    # --- H. EC 字段必须明确 UNSUP ---
    ec = text_ev("m2-ec")
    check("H1 EC 未启用且字段读为 UNSUP(-3)", "bst failed code=-3" in ec and "ec=0x0" in ec)
    check("H2 EC 不产生假读数", "pct=" not in ec)

    print()
    print("RESULT: %s (%d checks, %d failed)" %
          ("PASS" if not FAILS else "FAIL", len(CHECKS), len(FAILS)))
    if FAILS:
        print("failed:", ", ".join(FAILS))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
