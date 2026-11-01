#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
赫普OS 16x16 中文字库生成器 (V3.26)
数据源: 文泉驿正黑 (wqy-zenhei.ttc, GPLv2+Apache2.0 双许可), 系统字体渲染
字集: 全项目 .c/.h 字符串 + ime 词库中全部汉字 (UTF-8)
输出: kernel/fonts/font_zh_data.h (zh_utf8 + zh_bmp, 每字 16行 x 2字节, 高字节在前)
用法: python3 tools/gen_zh_font.py
"""
import os, re, glob, sys
from PIL import Image, ImageFont, ImageDraw

PROJECT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONT_PATH = "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc"
OUT = os.path.join(PROJECT, "kernel/fonts/font_zh_data.h")
SIZE = 16

def collect_chars():
    chars = set()
    files = (glob.glob(os.path.join(PROJECT, "kernel/**/*.c"), recursive=True)
             + glob.glob(os.path.join(PROJECT, "kernel/**/*.h"), recursive=True))
    for f in files:
        try:
            with open(f, encoding="utf-8", errors="ignore") as fp:
                s = fp.read()
        except Exception:
            continue
        for ch in s:
            if "\u4e00" <= ch <= "\u9fff":
                chars.add(ch)
    return sorted(chars)

def render_glyph(font, ch):
    """渲染单字 -> 16x16 二值点阵 (list of 16 int rows, bit7 最左)"""
    img = Image.new("L", (20, 20), 0)
    d = ImageDraw.Draw(img)
    d.text((0, 0), ch, font=font, fill=255)
    bbox = img.getbbox()
    if not bbox:
        return None
    crop = img.crop(bbox)
    w, h = crop.size
    scale = min(SIZE / w, SIZE / h)
    nw = max(1, int(w * scale))
    nh = max(1, int(h * scale))
    crop = crop.resize((nw, nh), Image.LANCZOS)
    out = Image.new("L", (SIZE, SIZE), 0)
    out.paste(crop, ((SIZE - nw) // 2, (SIZE - nh) // 2))
    rows = []
    for y in range(SIZE):
        hi = lo = 0
        for x in range(SIZE):
            if out.getpixel((x, y)) > 128:
                if x < 8:
                    hi |= 0x80 >> x
                else:
                    lo |= 0x80 >> (x - 8)
        rows.append((hi, lo))
    return rows

def main():
    if not os.path.exists(FONT_PATH):
        print("FONT 不存在:", FONT_PATH)
        sys.exit(1)
    font = ImageFont.truetype(FONT_PATH, SIZE)
    chars = collect_chars()
    print("待渲染汉字数:", len(chars))
    lines = []
    lines.append("/* ============================================================================")
    lines.append(" * 赫普OS - 16x16 中文点阵字库 (V3.26)")
    lines.append(" * 来源: 文泉驿正黑 (wqy-zenhei.ttc) 渲染, 每字 16x16 = 32 字节 (行内高字节在前)")
    lines.append(f" * 覆盖: 全项目字符串 + ime 词库汉字 {len(chars)} 字, UTF-8 编码")
    lines.append(" * 生成: tools/gen_zh_font.py")
    lines.append(" * ========================================================================== */")
    lines.append("#ifndef FONT_ZH_DATA_H")
    lines.append("#define FONT_ZH_DATA_H")
    lines.append("")
    lines.append("#include <stdint.h>")
    lines.append("")
    lines.append(f"#define ZH_FONT_COUNT {len(chars)}")
    lines.append("")
    lines.append("/* UTF-8 编码表 (每字 3 字节) */")
    lines.append("static const uint8_t zh_utf8[ZH_FONT_COUNT][3] = {")
    utf8_pairs = []
    bmp_pairs = []
    missing = []
    for ch in chars:
        b = ch.encode("utf-8")
        utf8_pairs.append("    { 0x%02X,0x%02X,0x%02X }," % (b[0], b[1], b[2]))
        rows = render_glyph(font, ch)
        if rows is None:
            missing.append(ch)
            rows = [(0, 0)] * SIZE
        bmp_pairs.append("    { " + ",".join("0x%02X" % v for r in rows for v in r) + " },")
    lines.append("\n".join(utf8_pairs))
    lines.append("};")
    lines.append("")
    lines.append("/* 16x16 点阵: 每字 16 行 x 2 字节 (高字节在前) */")
    lines.append(f"static const uint8_t zh_bmp[ZH_FONT_COUNT][32] = {{")
    lines.append("\n".join(bmp_pairs))
    lines.append("};")
    lines.append("")
    lines.append("#endif /* FONT_ZH_DATA_H */")
    with open(OUT, "w", encoding="utf-8") as fp:
        fp.write("\n".join(lines) + "\n")
    print("输出:", OUT, "字节:", os.path.getsize(OUT))
    if missing:
        print("渲染失败字:", "".join(missing))

if __name__ == "__main__":
    main()
