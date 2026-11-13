from PIL import Image
import sys

NAMES = ["arrow","wait","help","hourglass","cross","text","pen","no",
         "sb_v","sb_h","diag1","diag2","move","sb_up","hand","arrow2","arrow3"]
# 热点(相对包围盒, 0..1 比例); None 表示左上角(尖端)
HOT = {0:(0,0), 1:(0,0), 2:(0,0), 15:(0,0), 16:(0,0),
       13:(0.5,1.0), 14:(0.45,0.0)}

TARGET = 48
def process(idx):
    im = Image.open(f"c{idx}.png").convert("RGBA")
    a = im.split()[3]
    bbox = a.point(lambda v: 255 if v > 10 else 0).getbbox()
    if not bbox: return None
    im = im.crop(bbox)
    w, h = im.size
    scale = TARGET / max(w, h)
    nw, nh = max(1, round(w*scale)), max(1, round(h*scale))
    im = im.resize((nw, nh), Image.LANCZOS)
    px = list(im.getdata())
    # 亮度阈值二分色
    op = [p for p in px if p[3] > 40]
    if not op: return None
    lum = [(p[0]+p[1]+p[2])/3 for p in op]
    thr = sorted(lum)[len(lum)//2]
    fills, strokes = [], []
    for p, l in zip(op, lum):
        (fills if l >= thr else strokes).append(p)
    def avg(cs):
        return (sum(c[0] for c in cs)//len(cs), sum(c[1] for c in cs)//len(cs), sum(c[2] for c in cs)//len(cs))
    fill = avg(fills) if fills else avg(strokes)
    stroke = avg(strokes) if strokes else avg(fills)
    alpha = bytes(p[3] for p in px)
    bits = bytearray((nw*nh+7)//8)
    for i, p in enumerate(px):
        if p[3] == 0: continue
        l = (p[0]+p[1]+p[2])/3
        if l >= thr: bits[i>>3] |= 1 << (i & 7)
    if idx in HOT:
        hx, hy = HOT[idx]
        hx, hy = int(hx*nw), int(hy*nh)
    else:
        hx, hy = nw//2, nh//2
    return dict(w=nw, h=nh, hx=hx, hy=hy, alpha=alpha, bits=bytes(bits),
                fill=fill, stroke=stroke)

out = []
out.append("/* cursor_modern_data.c — 内置第二套光标 (Modern) 位图数据\n")
out.append(" * 由 128x128 线稿素材裁包围盒后缩放到 48px, 每像素只存 alpha + 1bit 前景/描边选择,\n")
out.append(" * 颜色每图两组 —— 素材本身就是双色线稿, 这样 17 个形态只占 40KB 左右。 */\n")
out.append("#include <stdint.h>\n#include \"cursor_modern.h\"\n\n")

def emit_bytes(name, data, per=16):
    out.append("static const uint8_t %s[] = {\n" % name)
    for i in range(0, len(data), per):
        out.append("    " + ",".join("0x%02X" % b for b in data[i:i+per]) + ",\n")
    out.append("};\n\n")

recs = []
for i in range(17):
    r = process(i)
    if not r: continue
    emit_bytes("cm_alpha_%d" % i, r["alpha"])
    emit_bytes("cm_bits_%d" % i, r["bits"])
    recs.append((i, r))

out.append("const cur_modern_t g_cur_modern[] = {\n")
for i, r in recs:
    out.append("    { %d, %d, %d, %d, %d, cm_alpha_%d, cm_bits_%d, 0x%02X%02X%02X, 0x%02X%02X%02X },\n"
               % (i, r["w"], r["h"], r["hx"], r["hy"], i, i,
                  r["fill"][0], r["fill"][1], r["fill"][2],
                  r["stroke"][0], r["stroke"][1], r["stroke"][2]))
out.append("};\n")
out.append("const int g_cur_modern_count = %d;\n" % len(recs))

open("/home/user/Doubao/chats/38452875115179010/hepu-os/kernel/arch/x86_64/cursor_modern_data.c","w").write("".join(out))
print("records", len(recs))
for i, r in recs: print(i, NAMES[i], r["w"], r["h"], "hot", r["hx"], r["hy"], "fill", r["fill"], "stroke", r["stroke"])
