#!/usr/bin/env python3
"""从 assets/icons-status64/p64_battery64.png 生成顶栏电池轮廓掩码。

为什么需要这个脚本
------------------
用户 2026-11-11 给的图标批次里，电池只有两种形态：
  * assets/icons-v2/128_battery_std.png —— 实心竖版，24px 下是一块黑砖，不可用；
  * assets/icons-status64/p64_battery64.png —— 同一批线性风格的横版电池，可用。
但后者把"约 69% 电量"直接烧进了像素（机身左 2/3 是实心），一旦贴上顶栏，
图标本身就在谎报电量，和"M0 移除假 100%"的原则冲突。

所以这里把素材拆成两层：
  * 轮廓层（本脚本输出）—— 机身圆角框 + 正极，固定不变；
  * 电量层（运行时绘制）—— 由 ACPI 状态决定，见 hpdesk.c 的 hs_battery_icon_state()。

几何从 PNG 实测（不是拍脑袋）：机身 40x20@64、描边 4@64、正极 3x8@64。
与原素材的两处差异：
  1. 去掉烧死的电量填充；
  2. 正极由"留 2px 间隙"改为贴合机身 —— 24px 显示时间隙只剩 0.75px，
     正极会脱开成孤立的小点，读起来像坏图标。

用法: python3 tools/gen_battery_glyph.py [--check]
"""
import os
import sys
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "assets/icons-status64/p64_battery64.png")
OUT = os.path.join(ROOT, "kernel/arch/x86_64/hs_battery_glyph.h")

SS = 8          # 超采样倍数, 让 1bit 掩码的圆角也有台阶感的合理过渡
GRID = 64       # 输出网格


def load_ink(path):
    im = Image.open(path).convert("L")
    w, h = im.size
    px = im.load()
    return [[1 if px[x, y] < 128 else 0 for x in range(w)] for y in range(h)], w, h


def x_runs(ink, w, h):
    """按列方向切成互不相连的块, 用来区分机身与正极。"""
    cols = [x for x in range(w) if any(ink[y][x] for y in range(h))]
    runs, s = [], None
    for x in range(w):
        on = x in cols
        if on and s is None:
            s = x
        if not on and s is not None:
            runs.append((s, x - 1))
            s = None
    if s is not None:
        runs.append((s, w - 1))
    return runs


def measure(path):
    ink, w, h = load_ink(path)
    runs = x_runs(ink, w, h)
    # 最大的一块是机身, 其余是正极
    body = max(runs, key=lambda r: r[1] - r[0])
    nub = [r for r in runs if r != body]
    ys = [y for y in range(h) for x in range(body[0], body[1] + 1) if ink[y][x]]
    body_box = (body[0], min(ys), body[1] - body[0] + 1, max(ys) - min(ys) + 1)
    # 描边厚度: 机身竖直中线那一列, 从顶往下连续有墨的行数
    # 取机身右四分之一处: 那里没有左锚定的电量填充, 从上往下连续有墨的段就是描边
    cx = body[0] + (body[1] - body[0]) * 3 // 4
    t = 0
    for y in range(h):
        if ink[y][cx]:
            t += 1
        elif t:
            break
    nub_box = None
    if nub:
        r = nub[0]
        ny = [y for y in range(h) for x in range(r[0], r[1] + 1) if ink[y][x]]
        nub_box = (r[0], min(ny), r[1] - r[0] + 1, max(ny) - min(ny) + 1)
    return body_box, nub_box, t


def in_rrect(X, Y, x, y, w, h, r):
    if X < x or X > x + w or Y < y or Y > y + h:
        return False
    cx = x + r if X < x + r else (x + w - r if X > x + w - r else X)
    cy = y + r if Y < y + r else (y + h - r if Y > y + h - r else Y)
    if (X < x + r or X > x + w - r) and (Y < y + r or Y > y + h - r):
        return (X - cx) ** 2 + (Y - cy) ** 2 <= r * r
    return True


def raster(body, nub):
    x, y, w, h = body[0], body[1], body[2], body[3]
    st, r = body[4], body[5]
    cov = [[0.0] * GRID for _ in range(GRID)]
    for py in range(GRID):
        for pxx in range(GRID):
            hit = 0
            for sy in range(SS):
                for sx in range(SS):
                    X = pxx + (sx + 0.5) / SS
                    Y = py + (sy + 0.5) / SS
                    outer = in_rrect(X, Y, x, y, w, h, r)
                    inner = in_rrect(X, Y, x + st, y + st, w - 2 * st, h - 2 * st, max(r - st, 0))
                    if outer and not inner:
                        hit += 1
            cov[py][pxx] = hit / (SS * SS)
    if nub:
        nx, ny, nw, nh = nub
        for py in range(GRID):
            for pxx in range(GRID):
                hit = 0
                for sy in range(SS):
                    for sx in range(SS):
                        X = pxx + (sx + 0.5) / SS
                        Y = py + (sy + 0.5) / SS
                        if nx <= X <= nx + nw and ny <= Y <= ny + nh:
                            hit += 1
                if hit:
                    cov[py][pxx] = max(cov[py][pxx], hit / (SS * SS))
    return cov


def emit(rows, inner, meta):
    lines = []
    lines.append("/* 赫普OS 顶栏电池字形（轮廓层）—— 自动生成, 勿手改。")
    lines.append(" * 生成: python3 tools/gen_battery_glyph.py")
    lines.append(" * 来源: assets/icons-status64/p64_battery64.png (用户 2026-11-11 图标批次的线性横版电池)")
    lines.append(" * 只保留机身圆角框 + 正极; 原图烧死的电量填充已剥离, 电量由 ACPI 状态驱动。")
    lines.append(" * 每行一个 uint64, bit63 = 最左像素, 1 = 有墨; 用 hs_icon_bmp64() 绘制。 */")
    lines.append("#ifndef HS_BATTERY_GLYPH_H")
    lines.append("#define HS_BATTERY_GLYPH_H")
    lines.append("#include <stdint.h>")
    lines.append("")
    for k, v in meta.items():
        lines.append("/* %s */" % v)
    lines.append("#define HS_BAT64_INNER_X %d" % inner[0])
    lines.append("#define HS_BAT64_INNER_Y %d" % inner[1])
    lines.append("#define HS_BAT64_INNER_W %d" % inner[2])
    lines.append("#define HS_BAT64_INNER_H %d" % inner[3])
    lines.append("")
    lines.append("static const uint64_t hs_bat64_outline[64] = {")
    for y in range(64):
        lines.append("    0x%016Xu%s" % (rows[y], "," if y < 63 else ""))
    lines.append("};")
    lines.append("")
    lines.append("#endif /* HS_BATTERY_GLYPH_H */")
    return "\n".join(lines) + "\n"


def main():
    body64, nub64, stroke64 = measure(SRC)
    # PNG 是 128 网格, 输出是 64 网格
    s = 64.0 / 128.0
    body = (body64[0] * s, body64[1] * s, body64[2] * s, body64[3] * s, stroke64 * s, 4.0)
    nub = None
    if nub64:
        # 正极贴合机身(见文件头说明 2)
        nub = (body[0] + body[2], nub64[1] * s, nub64[2] * s, nub64[3] * s)
    cov = raster(body, nub)
    rows = []
    for y in range(64):
        v = 0
        for x in range(64):
            if cov[y][x] >= 0.5:
                v |= 1 << (63 - x)
        rows.append(v)

    # 可填充内区 = 机身减去描边
    inner = (int(round(body[0] + body[4])), int(round(body[1] + body[4])),
             int(round(body[2] - 2 * body[4])), int(round(body[3] - 2 * body[4])))

    meta = {
        "geometry": "机身 x%.0f y%.0f %.0fx%.0f, 描边 %.0f, 圆角 4; 正极 %dx%d 贴合机身"
                    % (body[0], body[1], body[2], body[3], body[4], nub[2], nub[3]),
        "source_bbox": "PNG 128 网格实测 机身 (%d,%d,%d,%d) 描边 %d"
                       % (body64[0], body64[1], body64[2], body64[3], stroke64),
    }
    text = emit(rows, inner, meta)

    if "--check" in sys.argv:
        old = open(OUT).read() if os.path.exists(OUT) else ""
        if old != text:
            print("FAIL: %s 与生成结果不一致, 请重跑 python3 tools/gen_battery_glyph.py" % OUT)
            return 1
        print("OK: 生成结果与仓库一致")
        return 0

    with open(OUT, "w") as f:
        f.write(text)
    print("wrote", OUT)
    print("inner", inner)
    return 0


if __name__ == "__main__":
    sys.exit(main())
