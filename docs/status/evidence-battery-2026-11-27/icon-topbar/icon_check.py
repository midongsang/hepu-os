#!/usr/bin/env python3
"""顶栏电池图标取证核对：只认截图与串口里的事实。

检查项：
  1. 默认 QEMU 判 ABSENT，合成 SSDT 判 DECLARED（串口 [BAT] 行）；
  2. 两张截图都是 1920x1080；
  3. 顶栏图标区"宽 >= 20px 的墨段"数量：默认 2 段（wifi/齿轮），
     声明态 3 段（多出一个 25px 宽的电池）——证明多出来的是独立一个图标，
     不是把某个邻居改胖了（电源/喇叭笔画会被抗锯齿切成 <20px 的小段，不计入）；
  4. 电池段的几何：宽 20..30、高 <= 16，符合横版电池比例；
  5. x 1750..1885 在两次运行里逐像素相同——插入电池没有推移右侧三个图标，
     也没动它们的笔画。
"""
import os
import re
import sys
from PIL import Image, ImageChops

ROOT = os.path.dirname(os.path.abspath(__file__))
BAR_Y = (12, 44)       # 胶囊内部, 避开上方壁纸与描边
STRIP_X = (1600, 1880)   # 右侧留出胶囊圆角, 免得把边缘阴影当成图标
INK = 170              # 浅米色胶囊底约 200+, 图标前景约 90


def state(run):
    txt = open(os.path.join(ROOT, run, "bat-lines.txt"), encoding="utf-8", errors="replace").read()
    m = re.search(r"\[BAT\] state=(\w+) .*?reason=(\S+)", txt)
    if not m:
        raise SystemExit("missing [BAT] line in %s" % run)
    return m.group(1), m.group(2)


def lum(path):
    im = Image.open(path).convert("L")
    return im, im.load()


def runs_of(px, w):
    cols = [x for x in range(STRIP_X[0], STRIP_X[1])
            if any(px[x, y] < INK for y in range(*BAR_Y))]
    out, s = [], None
    for x in range(STRIP_X[0], STRIP_X[1]):
        on = x in cols
        if on and s is None:
            s = x
        if not on and s is not None:
            out.append((s, x - 1))
            s = None
    if s is not None:
        out.append((s, STRIP_X[1] - 1))
    return [r for r in out if r[1] - r[0] + 1 >= 20]


def main():
    d_state, d_reason = state("default")
    c_state, c_reason = state("declared")
    d_im, d_px = lum(os.path.join(ROOT, "default/desktop.png"))
    c_im, c_px = lum(os.path.join(ROOT, "declared/desktop.png"))

    d_runs = runs_of(d_px, d_im.size[0])
    c_runs = runs_of(c_px, c_im.size[0])
    # 声明态里, 落在 wifi 与喇叭之间的那一段就是电池
    battery = [r for r in c_runs if 1700 <= r[0] <= 1760]
    bat_h = 0
    if battery:
        b = battery[0]
        ys = [y for y in range(*BAR_Y) for x in range(b[0], b[1] + 1) if c_px[x, y] < INK]
        bat_h = max(ys) - min(ys) + 1

    same_right = ImageChops.difference(
        d_im.crop((1750, 0, 1885, 52)).convert("RGB"),
        c_im.crop((1750, 0, 1885, 52)).convert("RGB"),
    ).getbbox() is None

    checks = [
        ("default_absent", d_state == "absent", "state=%s reason=%s" % (d_state, d_reason)),
        ("declared_declared", c_state == "declared", "state=%s reason=%s" % (c_state, c_reason)),
        ("size_1920x1080", d_im.size == (1920, 1080) and c_im.size == (1920, 1080),
         "%s / %s" % (d_im.size, c_im.size)),
        ("wide_runs_default_2", len(d_runs) == 2, "%r" % (d_runs,)),
        ("wide_runs_declared_3", len(c_runs) == 3, "%r" % (c_runs,)),
        ("battery_width_20_30", bool(battery) and 20 <= battery[0][1] - battery[0][0] + 1 <= 30,
         "x=%r" % (battery,)),
        ("battery_height_le_16", 0 < bat_h <= 16, "h=%d" % bat_h),
        ("right_icons_unmoved", same_right, "x 1750..1885 逐像素相同" if same_right else "有位移"),
    ]

    lines = []
    ok = True
    for name, passed, detail in checks:
        lines.append("%-26s %s  %s" % (name, "PASS" if passed else "FAIL", detail))
        ok = ok and passed
    lines.append("")
    lines.append("RESULT: %s" % ("PASS" if ok else "FAIL"))
    text = "\n".join(lines)
    print(text)
    with open(os.path.join(ROOT, "check.txt"), "w", encoding="utf-8") as f:
        f.write(text + "\n")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
