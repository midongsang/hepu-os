#!/usr/bin/env python3
"""赫普OS - CJK 16x16 点阵生成器 (V4.23-E68)

从 Noto Serif CJK 渲染汉字到 16x16 二值点阵, 输出 C 头文件.
用法: python3 tools/gen_cjk_font.py > kernel/include/font_cjk_big.h

渲染策略: 目标字号 18px 渲染 → 居中裁剪/缩放 → 16x16 阈值二值化.
"""
from PIL import Image, ImageFont, ImageDraw
import sys

FONT_PATH = "/usr/share/fonts/opentype/noto/NotoSerifCJK-Bold.ttc"
CELL = 16

TEXTS = [
    "欢迎使用赫普系统中文当前语言已切换未知命令找到运行时间内存储剩余进程总数启动完成核版本帮助磁盘配额规则安全模式诊断闹钟设置",
    "一二是三四五六七八九十百分秒小年月日星期天上午下关机重输入法拼音字母标点符号件目录路径读写删除复制移新建打开闭保退出确认取消返回左右选择引导驱硬备电源池网络无线蓝牙触摸屏幕显示亮度量静",
    "，。！？：；、（）【】《》“”‘’·—…",
]
CHARS = []
for t in TEXTS:
    for c in t:
        if c not in CHARS:
            CHARS.append(c)

# V4.28-E73: 并入 GB2312 一级汉字表(3755 字, 拼音序, 0xB0A1-0xD7F9)
def gb2312_level1():
    out = []
    for row in range(0xB0, 0xD8):
        for col in range(0xA1, 0xFF):
            try:
                c = bytes([row, col]).decode('gb2312')
            except UnicodeDecodeError:
                continue
            if len(c) == 1 and c.isprintable() and ord(c) > 0x80:
                out.append(c)
    return out

for c in gb2312_level1():
    if c not in CHARS:
        CHARS.append(c)
CHARS.sort(key=ord)   # 按 unicode 排序 -> 内核可二分查找

def render_char(ch):
    """返回 16x16 二值点阵 (16 行, 每行 2 字节, 高位在左)"""
    size = 18
    font = ImageFont.truetype(FONT_PATH, size)
    img = Image.new("L", (32, 32), 0)
    d = ImageDraw.Draw(img)
    d.text((16, 16), ch, font=font, fill=255, anchor="mm")
    # 找边界框
    bbox = img.getbbox()
    if bbox is None:
        return None
    crop = img.crop(bbox)
    # 缩放到 16x16 (保持比例, 居中)
    cw, chh = crop.size
    scale = min(CELL / cw, CELL / chh)
    nw, nh = max(1, int(cw * scale)), max(1, int(chh * scale))
    crop = crop.resize((nw, nh), Image.LANCZOS)
    canvas = Image.new("L", (CELL, CELL), 0)
    canvas.paste(crop, ((CELL - nw) // 2, (CELL - nh) // 2))
    px = canvas.load()
    rows = []
    for y in range(CELL):
        hi = 0
        lo = 0
        for x in range(CELL):
            v = 1 if px[x, y] > 96 else 0  # 阈值
            if x < 8:
                hi |= v << (7 - x)
            else:
                lo |= v << (15 - x)
        rows.append(hi)
        rows.append(lo)
    return rows

def main():
    print("/* ============================================================================")
    print(" * 赫普OS - CJK 16x16 点阵库 (自动生成, V4.23-E68)")
    print(" * 源: Noto Serif CJK SC (Bold) -> PIL 渲染 -> 16x16 二值点阵")
    print(" * 生成: tools/gen_cjk_font.py  勿手改!")
    print(" * ========================================================================== */")
    print("#ifndef HEPU_FONT_CJK_BIG_H")
    print("#define HEPU_FONT_CJK_BIG_H")
    print()
    print('#include "font_cjk16.h"')
    print()
    print("/* %d 字: %s */" % (len(CHARS), "".join(CHARS[:40]) + "..."))
    print("static const cjk_glyph_t hepu_cjk_big[] = {")
    for ch in CHARS:
        u = ord(ch)
        g = render_char(ch)
        if g is None:
            sys.stderr.write("skip %s\n" % ch)
            continue
        body = ", ".join("0x%02x" % b for b in g)
        print("    { 0x%04X, { %s } }, /* %s */" % (u, body, ch))
    print("};")
    print()
    print("#define HEPU_CJK_BIG_COUNT (sizeof(hepu_cjk_big) / sizeof(cjk_glyph_t))")
    print()
    print("#endif /* HEPU_FONT_CJK_BIG_H */")

if __name__ == "__main__":
    main()
