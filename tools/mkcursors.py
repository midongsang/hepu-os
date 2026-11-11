#!/usr/bin/env python3
"""把 PNG 光标图标集转成 X11 Xcursor (.xcur) 通用格式。
用法: python3 tools/mkcursors.py <png_dir> <out_dir>
生成 24/32/48 三种 nominal size，热点按语义自动推断。
"""
import sys, os, struct
from PIL import Image

# 语义名 -> (源序号, 热点策略)
#   tip  = 左上尖端（箭头类）
#   center = 包围盒中心
CURSORS = [
    ("default",      0,  "tip"),
    ("help",         1,  "tip"),
    ("wait",         2,  "center"),
    ("progress",     3,  "tip"),
    ("move",         4,  "center"),
    ("text",         5,  "center"),
    ("pencil",       6,  "center"),
    ("not-allowed",  7,  "center"),
    ("size-ver",     8,  "center"),
    ("size-hor",     9,  "center"),
    ("size-diag",    10, "center"),
    ("fleur",        11, "center"),
    ("up-arrow",     12, "center"),
    ("link",         13, "tip"),
    ("location",     14, "tip"),
    ("person",       15, "tip"),
    ("crosshair",    16, "center"),
]
SIZES = [24, 32, 48]

def alpha_bbox(im):
    return im.getchannel("A").getbbox()

def hotspot(im, policy):
    bb = alpha_bbox(im)
    if bb is None: return (0, 0)
    if policy == "tip":
        # 尖端：alpha>128 的最上几行里取最左的像素
        px = im.load(); W, H = im.size
        for y in range(bb[1], min(bb[3], bb[1] + H // 3)):
            for x in range(bb[0], bb[2]):
                if px[x, y][3] > 128:
                    return (x, y)
    return ((bb[0] + bb[2]) // 2, (bb[1] + bb[3]) // 2)

def encode(images, out):
    ntoc = len(images)
    hdr = struct.pack("<4sIII", b"Xcur", 16, 0x10000, ntoc)
    toc = b""; body = b""
    pos = 16 + 12 * ntoc
    for (nominal, w, h, xh, yh, pixels) in images:
        toc += struct.pack("<III", 0xfffd0002, nominal, pos + len(body))
        body += struct.pack("<IIIIIIIII", 36, 0xfffd0002, nominal, 1,
                            w, h, xh, yh, 1)
        body += b"".join(struct.pack("<I", p) for p in pixels)
    with open(out, "wb") as f:
        f.write(hdr + toc + body)

def main():
    src, outdir = sys.argv[1], sys.argv[2]
    os.makedirs(outdir, exist_ok=True)
    made = 0
    for name, idx, policy in CURSORS:
        path = os.path.join(src, "attachment_1794365703_%d" % idx)
        if not os.path.exists(path): continue
        base = Image.open(path).convert("RGBA")
        hx, hy = hotspot(base, policy)
        imgs = []
        for s in SIZES:
            im = base.resize((s, s), Image.LANCZOS)
            px = im.load()
            # ARGB8888, 小端
            buf = []
            for y in range(s):
                for x in range(s):
                    r, g, b, a = px[x, y]
                    buf.append((a << 24) | (r << 16) | (g << 8) | b)
            sx = int(round(hx * s / base.size[0]))
            sy = int(round(hy * s / base.size[1]))
            imgs.append((s, s, s, min(sx, s - 1), min(sy, s - 1), buf))
        out = os.path.join(outdir, name + ".xcur")
        encode(imgs, out)
        made += 1
        print(f"  {name:12s} <- #{idx:2d} hotspot=({hx},{hy}) -> {os.path.getsize(out)}B")
    print(f"共生成 {made} 个 xcur")

if __name__ == "__main__":
    main()
