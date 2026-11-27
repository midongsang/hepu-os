#!/usr/bin/env python3
"""固定态截图比较。

用法:
  compare_visual.py <baseline_dir> <current_dir>

比较 01-lock / 02-desktop / 03-drawer / 04-control-center / 05-settings /
06-devices / 07-audio / 08-dock / 09-weather。
时钟与天气卡取的是实时文本，比较前按区域屏蔽。
阈值按“布局回归”而不是“像素完全一致”设定：
  - 平均绝对差 <= 2.0（0..255）
  - 变化超过 16 的像素占比 <= 0.8%
这样能抓住卡片移位、文字裁切、面板变宽、图标消失，又不会因 TTF 抗锯齿
的 1px 抖动误报。
"""
import os
import sys
from PIL import Image, ImageChops

NAMES = [
    "01-lock.png",
    "02-desktop.png",
    "03-drawer.png",
    "04-control-center.png",
    "05-settings.png",
    "06-devices.png",
    "07-audio.png",
    "08-dock.png",
    "09-weather.png",
]
MEAN_LIMIT = 2.0
CHANGED_RATIO_LIMIT = 0.008
CHANGED_DIFF = 16
# 动态文本区域：顶栏时钟胶囊、右下天气卡。其余布局必须逐像素稳定。
MASKS = [
    (850, 0, 1075, 70),
    (1690, 870, 1900, 1080),
]


def masked_histogram(diff):
    diff = diff.convert("L")
    for box in MASKS:
        diff.paste(0, box)
    return diff.histogram()


def main():
    base_dir, cur_dir = sys.argv[1], sys.argv[2]
    failures = []
    for name in NAMES:
        bp = os.path.join(base_dir, name)
        cp = os.path.join(cur_dir, name)
        if not os.path.exists(bp):
            print(f"MISSING baseline {bp}")
            failures.append(name)
            continue
        if not os.path.exists(cp):
            print(f"MISSING current {cp}")
            failures.append(name)
            continue
        a = Image.open(bp).convert("RGB")
        b = Image.open(cp).convert("RGB")
        if a.size != b.size:
            print(f"FAIL {name}: size {a.size} -> {b.size}")
            failures.append(name)
            continue
        diff = ImageChops.difference(a, b)
        hist = masked_histogram(diff)
        total = a.size[0] * a.size[1]
        changed = sum(hist[CHANGED_DIFF + 1:])
        mean = sum(i * n for i, n in enumerate(hist)) / max(total, 1)
        ratio = changed / max(total, 1)
        status = "OK" if mean <= MEAN_LIMIT and ratio <= CHANGED_RATIO_LIMIT else "FAIL"
        print(f"{status} {name}: mean={mean:.3f} changed>{CHANGED_DIFF}={ratio:.4%} ({changed}/{total})")
        if status == "FAIL":
            failures.append(name)

    if failures:
        print("visual regression: " + ", ".join(failures))
        return 1
    print("visual baseline: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
