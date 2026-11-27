#!/usr/bin/env python3
"""双电池槽位取证：DUA0 明确 absent，DUA1 有可信 _BST；UI 必须读 DUA1 的 57%。"""
import os
import numpy as np
from PIL import Image

ROOT = os.path.dirname(os.path.abspath(__file__))
ACCENT = (201, 169, 110)
FAILS = []


def check(name, cond, detail=""):
    print("[%s] %s%s" % ("PASS" if cond else "FAIL", name, (" — " + detail) if detail else ""))
    if not cond:
        FAILS.append(name)


def text():
    with open(os.path.join(ROOT, "desk-lines.txt"), "rb") as f:
        return f.read().decode("utf-8", "replace")


def accent_count(p, box, tol=16):
    img = np.array(Image.open(os.path.join(ROOT, p)).convert("RGB")).astype(int)
    a = img[box[1]:box[3], box[0]:box[2]]
    m = (np.abs(a[:, :, 0] - ACCENT[0]) <= tol) & \
        (np.abs(a[:, :, 1] - ACCENT[1]) <= tol) & \
        (np.abs(a[:, :, 2] - ACCENT[2]) <= tol)
    return int(m.sum())


def main():
    ser = text()
    check("D1 枚举到两个 PNP0C0A 槽位", "[BAT] aml candidates=2" in ser)
    check("D2 两个槽位都出现在日志", "[BAT] aml dev=DUA0" in ser and "[BAT] aml dev=DUA1" in ser)
    check("D3 跳过 absent DUA0，选中 DUA1", "[BAT] aml dev=selected DUA1" in ser)
    ok = [l for l in ser.splitlines() if l.startswith("[BAT] aml sta=")
          and "pct=57" in l and "chg=1" in l and "rem=5700" in l and "full=10000" in l]
    check("D4 至少 3 次可信采样", len(ok) >= 3, "matched=%d" % len(ok))
    check("D5 没有 bst failed / untrusted", "bst failed" not in ser and "untrusted" not in ser)
    check("D6 面板大号数字为 accent 色", accent_count("02-panel.png", (1390, 108, 1500, 165)) > 200,
          "accent_px=%d" % accent_count("02-panel.png", (1390, 108, 1500, 165)))
    check("D7 面板打开并进入 Battery 页", "[DESK] battery panel open" in ser
          and "[DESK] battery panel -> settings page=battery" in ser)
    print("\nRESULT: %s (%d checks, %d failed)" % ("PASS" if not FAILS else "FAIL",
                                                  7, len(FAILS)))
    return 0 if not FAILS else 1


if __name__ == "__main__":
    raise SystemExit(main())
