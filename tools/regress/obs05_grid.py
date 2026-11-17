#!/usr/bin/env python3
# ============================================================================
# OBS-05 测量口径：九宫格（3x3）非黑像素统计 + 网格叠加对照图。
#
# 口径（与整帧 NONBLACK 保持一致，不另设背景色排除）：
#   - 分辨率：以截图实际尺寸为准，整帧分母 = W*H（桌面目标 1920x1080 = 2,073,600）。
#   - 非黑判定（主口径，strict）：RGB 三通道均不等于 (0,0,0)，即 pixel != #000000。
#   - 近黑口径（secondary，仅供参考）：任一通道 >=16 即判非黑（<16 视为近黑）。
#   - 九宫格：列/行均分三格，每格分母 = 格内像素数；占比 = 格内非黑 / 格内像素。
#   - 不排除背景色；不做裁剪；网格线与标注只画在对照副本上，不回写原图。
#
# 用法:
#   python3 obs05_grid.py <image.png> [image2.png ...] --out-json <obs05.json> \
#       [--prefix <label>] [--overlay-dir <dir>]
# ============================================================================
import argparse
import json
import os
from PIL import Image, ImageDraw, ImageFont

CELLS = [
    ("TL", 0, 0), ("TC", 1, 0), ("TR", 2, 0),
    ("ML", 0, 1), ("MC", 1, 1), ("MR", 2, 1),
    ("BL", 0, 2), ("BC", 1, 2), ("BR", 2, 2),
]


def strict_nonblack(px):
    return px != (0, 0, 0)


def near_nonblack(px):
    return any(c >= 16 for c in px)


def measure_image(path, overlay_dir, prefix=""):
    im = Image.open(path).convert("RGB")
    W, H = im.size
    total = W * H
    px = im.load()
    cells = {}
    strict_cell = {k: 0 for k, _, _ in CELLS}
    near_cell = {k: 0 for k, _, _ in CELLS}
    strict_all = near_all = 0
    cw, ch = W // 3, H // 3
    for y in range(H):
        r = min(2, y // ch)
        for x in range(W):
            c = min(2, x // cw)
            p = px[x, y]
            key = CELLS[r * 3 + c][0]
            if strict_nonblack(p):
                strict_all += 1
                strict_cell[key] += 1
            if near_nonblack(p):
                near_all += 1
                near_cell[key] += 1
    cell_px = cw * ch
    for k, col, row in CELLS:
        cells[k] = {
            "col": col, "row": row,
            "strict_nonblack": strict_cell[k],
            "strict_ratio": round(strict_cell[k] / cell_px, 4),
            "near_nonblack": near_cell[k],
            "near_ratio": round(near_cell[k] / cell_px, 4),
        }
    result = {
        "path": os.path.abspath(path),
        "size": [W, H],
        "total_pixels": total,
        "strict_nonblack": strict_all,
        "strict_ratio": round(strict_all / total, 4),
        "near_nonblack": near_all,
        "near_ratio": round(near_all / total, 4),
        "cells": cells,
    }

    overlay = im.copy()
    d = ImageDraw.Draw(overlay)
    for i in (1, 2):
        d.line([(cw * i, 0), (cw * i, H)], fill=(255, 60, 60), width=2)
        d.line([(0, ch * i), (W, ch * i)], fill=(255, 60, 60), width=2)
    try:
        font = ImageFont.truetype("DejaVuSans-Bold.ttf", 28)
    except Exception:
        font = ImageFont.load_default()
    for k, col, row in CELLS:
        ratio = cells[k]["strict_ratio"]
        tx, ty = col * cw + 10, row * ch + 10
        d.rectangle([tx, ty, tx + 150, ty + 44], fill=(0, 0, 0))
        d.text((tx + 6, ty + 6), "%s %.2f" % (k, ratio), fill=(255, 255, 0), font=font)
    if overlay_dir:
        os.makedirs(overlay_dir, exist_ok=True)
        base = os.path.splitext(os.path.basename(path))[0]
        out = os.path.join(overlay_dir, "%s_%s_grid.png" % (prefix, base))
        overlay.save(out)
        result["overlay"] = os.path.abspath(out)
    return result


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("images", nargs="+")
    ap.add_argument("--out-json", required=True)
    ap.add_argument("--prefix", default="")
    ap.add_argument("--overlay-dir", default="")
    args = ap.parse_args()

    results = []
    for img in args.images:
        if os.path.exists(img):
            results.append(measure_image(img, args.overlay_dir, args.prefix))
    agg = {
        "label": args.prefix,
        "threshold_strict": "pixel != (0,0,0)；三通道全为 0 才判黑",
        "threshold_near": "secondary: 任一通道 >=16 判非黑",
        "denominator": "W*H（1920x1080 目标下 = 2,073,600），不排除背景色、不裁剪",
        "images": results,
    }
    if results:
        agg["whole_strict_min"] = min(r["strict_nonblack"] for r in results)
        agg["whole_strict_avg"] = round(
            sum(r["strict_nonblack"] for r in results) / len(results), 1)
    os.makedirs(os.path.dirname(os.path.abspath(args.out_json)), exist_ok=True)
    with open(args.out_json, "w") as f:
        json.dump(agg, f, ensure_ascii=False, indent=2)
    print(json.dumps(agg, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
