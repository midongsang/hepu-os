#!/usr/bin/env python3
"""电池面板取证断言：不做人眼判断，逐项算。

数据源:
  - desk-lines.txt      声明态（注入合成 SSDT）这一次运行的 [DESK]/[BAT]/[SETTINGS] 行
  - absent/desk-lines.txt  默认夹具（无电池）这一次运行
    （原生串口 ser.log 被 .gitignore 的 *.log 挡掉, 所以入库存的是这几行摘录）
  - 01..05*.png         同一轮点击轨迹上的截图

断言:
  A. 声明态: 顶栏"电池槽位"多出一段独立墨迹, 且 wifi 左移一格
  B. 无电池: 电池槽位没有墨迹(那里只有左移后的 wifi), 点击不出面板
  C. 点击电池图标 → 面板矩形内像素变化, 右下角入口是 accent 实心按钮
  D. 点入口 → Settings 第 7 项 Battery 高亮, 页面落在电池页
  E. 老页(About) 仍可点, 第 7 页不抢占旧索引
  F. 导航里直接点 Battery 与面板入口得到完全同样的内容(逐像素相同)
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


def ink(img, box, thresh=450):
    a = img[box[1]:box[3], box[0]:box[2]]
    return int((a.sum(axis=2) < thresh).sum())


def accent_ratio(img, box, tol=14):
    a = img[box[1]:box[3], box[0]:box[2]]
    m = (np.abs(a[:, :, 0] - ACCENT[0]) <= tol) & \
        (np.abs(a[:, :, 1] - ACCENT[1]) <= tol) & \
        (np.abs(a[:, :, 2] - ACCENT[2]) <= tol)
    return float(m.sum()) / m.size


def text(p):
    with open(os.path.join(ROOT, p), "rb") as f:
        return f.read().decode("utf-8", "replace")


def main():
    ser = text("desk-lines.txt")
    ser_absent = text(os.path.join("absent", "desk-lines.txt"))

    # --- A. 声明态顶栏: wifi 在 1668 槽, 电池在 1713 槽, 两段墨迹 ---
    d1 = load("01-desktop.png")
    wifi_slot = (1660, 20, 1708, 58)      # 有电池时 wifi 占第 5 格
    bat_slot = (1709, 20, 1753, 58)       # 电池占第 4 格
    check("A1 声明态 wifi 槽有墨迹", ink(d1, wifi_slot) > 50, "ink=%d" % ink(d1, wifi_slot))
    check("A2 声明态 电池槽有墨迹", ink(d1, bat_slot) > 50, "ink=%d" % ink(d1, bat_slot))
    check("A3 串口 state=declared", "[BAT] state=declared" in ser)

    # --- B. 无电池: 左边一格空白, 点击不出面板 ---
    d0 = load(os.path.join("absent", "01-desktop.png"))
    check("B1 无电池 wifi 左移后原槽位空白", ink(d0, wifi_slot) < 12, "ink=%d" % ink(d0, wifi_slot))
    check("B2 无电池 电池槽仍是 wifi", ink(d0, bat_slot) > 50, "ink=%d" % ink(d0, bat_slot))
    check("B3 无电池串口 state=absent", "[BAT] state=absent" in ser_absent)
    check("B4 无电池点击不产生面板", "[DESK] battery panel open" not in ser_absent)

    # --- C. 点击后: 面板矩形变化 + 右下角入口是 accent ---
    d2 = load("02-panel.png")
    panel = (1374, 78, 1749, 273)
    a1 = d1[panel[1]:panel[3], panel[0]:panel[2]]
    a2 = d2[panel[1]:panel[3], panel[0]:panel[2]]
    changed = int((np.abs(a1 - a2).sum(axis=2) > 8).sum())
    check("C1 面板矩形内像素发生变化", changed > panel_area() * 0.25,
          "changed=%d/%d" % (changed, panel_area()))
    link = (1506, 222, 1731, 258)
    check("C2 右下角入口是 accent 实心按钮", accent_ratio(d2, link) > 0.5,
          "accent=%.2f" % accent_ratio(d2, link))
    check("C3 串口记录 panel open", "[DESK] battery panel open" in ser)

    # --- D. 点入口 → Settings 电池页 ---
    d3 = load("03-settings.png")
    nav_bat = (639, 518, 818, 560)
    nav_app = (639, 227, 818, 269)
    check("D1 导航第 7 项 Battery 高亮", accent_ratio(d3, nav_bat) > 0.5,
          "accent=%.2f" % accent_ratio(d3, nav_bat))
    check("D2 Appearance 未高亮", accent_ratio(d3, nav_app) < 0.05,
          "accent=%.2f" % accent_ratio(d3, nav_app))
    check("D3 串口记录 settings page=battery", "[DESK] battery panel -> settings page=battery" in ser)

    # --- E. 老页仍可点: About(索引 3) ---
    d4 = load("04-about.png")
    nav_about = (639, 375, 818, 417)
    check("E1 About 高亮(旧索引未被第 7 页挤掉)", accent_ratio(d4, nav_about) > 0.5,
          "accent=%.2f" % accent_ratio(d4, nav_about))
    check("E2 About 时 Battery 不高亮", accent_ratio(d4, nav_bat) < 0.05,
          "accent=%.2f" % accent_ratio(d4, nav_bat))
    check("E3 串口记录 nav page=0x3", "[SETTINGS] nav page=0x3" in ser)

    # --- F. 导航直点 Battery === 面板入口进电池页 ---
    d5 = load("05-battery-nav.png")
    content = (840, 220, 1270, 560)
    c3 = d3[content[1]:content[3], content[0]:content[2]]
    c5 = d5[content[1]:content[3], content[0]:content[2]]
    diff = int((np.abs(c3 - c5).sum(axis=2) > 6).sum())
    check("F1 两个入口的电池页内容逐像素相同", diff == 0, "diff=%d" % diff)
    check("F2 串口记录 nav page=0x6", "[SETTINGS] nav page=0x6" in ser)
    check("F3 导航点 Battery 时高亮", accent_ratio(d5, nav_bat) > 0.5,
          "accent=%.2f" % accent_ratio(d5, nav_bat))

    print()
    print("RESULT: %s (%d checks, %d failed)" %
          ("PASS" if not FAILS else "FAIL", len(CHECKS), len(FAILS)))
    if FAILS:
        print("failed:", ", ".join(FAILS))
        return 1
    return 0


def panel_area():
    return (1749 - 1374) * (273 - 78)


if __name__ == "__main__":
    sys.exit(main())
