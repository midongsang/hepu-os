#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
NONBLACK 像素计数与判定（回归取证用）。

用法:
  python3 measure.py <image.png|ppm> [role] [--json out.json]
  python3 measure.py --batch <dir> <prefix> [role] --json out.json

role:
  desktop  真桌面: 1920x1080 接近全量非黑, NONBLACK >= 2,073,597（基线口径）
  lock     锁屏: 米白底, 全屏非黑属正常; 只计数 + 浅底存在性判定, 不套桌面阈值
  menu/loading/protect/text  记录计数, 判定留空（目视 + 串口日志为准）

NONBLACK 定义: PIL 逐像素统计 != (0,0,0) 的像素数；总像素 W*H。
"""
import json
import os
import sys
import glob

DESKTOP_MIN = 2073597
TOTAL = 2073600


def measure(path, role):
    from PIL import Image
    im = Image.open(path).convert("RGB")
    w, h = im.size
    total = w * h
    non = sum(1 for p in im.getdata() if p != (0, 0, 0))
    black = total - non
    ratio = non / total if total else 0.0

    verdict = None
    note = ""
    if role == "desktop":
        verdict = "PASS" if non >= DESKTOP_MIN else "FAIL"
        note = "桌面阈值 NONBLACK >= %d / %d" % (DESKTOP_MIN, total)
    elif role == "lock":
        # 米白底: 非黑占比过半即认为浅色背景在场；不要求全量饱和（与桌面区分开）
        verdict = "PASS" if ratio >= 0.5 else "FAIL"
        note = "锁屏米白底, 非黑占比 >= 50%% 即记录为在场；NONBLACK 不单独区分锁屏/桌面"
    else:
        note = "记录项, 判定以画面目视与串口日志为准"

    return {
        "path": os.path.abspath(path),
        "role": role,
        "width": w,
        "height": h,
        "total": total,
        "nonblack": non,
        "black": black,
        "nonblack_ratio": round(ratio, 6),
        "verdict": verdict,
        "note": note,
    }


def main():
    args = sys.argv[1:]
    batch = False
    if args and args[0] == "--batch":
        batch = True
        args = args[1:]

    json_out = None
    if "--json" in args:
        i = args.index("--json")
        json_out = args[i + 1]
        del args[i:i + 2]

    results = []
    if batch:
        d, prefix = args[0], args[1]
        role = args[2] if len(args) > 2 else "menu"
        for f in sorted(glob.glob(os.path.join(d, prefix + "*.png"))):
            results.append(measure(f, role))
    else:
        path = args[0]
        role = args[1] if len(args) > 1 else "menu"
        results.append(measure(path, role))

    if json_out:
        os.makedirs(os.path.dirname(os.path.abspath(json_out)), exist_ok=True)
        with open(json_out, "w") as f:
            json.dump(results, f, ensure_ascii=False, indent=2)

    for r in results:
        print("%-55s %4dx%-4d NONBLACK=%8d/%8d ratio=%.4f verdict=%s" % (
            os.path.basename(r["path"]), r["width"], r["height"],
            r["nonblack"], r["total"], r["nonblack_ratio"], r["verdict"]))

    if not batch and json_out is None:
        print(json.dumps(results[0], ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
