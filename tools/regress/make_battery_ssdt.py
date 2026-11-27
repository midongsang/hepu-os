#!/usr/bin/env python3
"""生成用于 QEMU 取证 / 主机单测的合成 SSDT（不是产品固件的一部分）。

用法:
  make_battery_ssdt.py <输出文件> [declared|valid|valid-bix|absent|nobst|ec] [百分比]
  make_battery_ssdt.py --header <输出头文件>

模式与预期:
  declared   仅声明 PNP0C0A + Name(_STA,0x0F)  -> DECLARED
  valid      _STA/_BIF/_BST 全实现（_BIF 路径） -> VALID
  valid-bix  同上但用 _BIX                       -> VALID
  absent     Method(_STA) 返回 0                 -> ABSENT
  nobst      只有声明，没有 _BST                  -> DECLARED（不可信）
  ec         _BST 读 EmbeddedControl 字段         -> DECLARED（明确不支持 EC）
"""
import struct
import sys

# ---------------------------------------------------------------- AML 编码


def pkg(payload):
    """PkgLength 编码：长度包含长度字节自身。"""
    total = len(payload) + 1
    if total < 0x40:
        return bytes([total]) + payload
    total = len(payload) + 2
    if total < 0x1000:
        return bytes([0x40 | (total & 0x0F), (total >> 4) & 0xFF]) + payload
    total = len(payload) + 3
    if total < 0x100000:
        return bytes([0x80 | (total & 0x0F), (total >> 4) & 0xFF, (total >> 12) & 0xFF]) + payload
    raise ValueError("payload too large")


def bit_len(bits):
    """FieldList 里的位宽：PkgLength 值 = 位宽 + 长度字节数。"""
    total = bits + 1
    if total < 0x40:
        return bytes([total])
    total = bits + 2
    if total < 0x1000:
        return bytes([0x40 | (total & 0x0F), (total >> 4) & 0xFF])
    raise ValueError("field too wide")


def int_obj(v):
    if v == 0:
        return b"\x00"
    if v == 1:
        return b"\x01"
    if v == 0xFF:
        return b"\xff"
    if 0 <= v <= 0xFF:
        return b"\x0a" + bytes([v])
    if 0 <= v <= 0xFFFF:
        return b"\x0b" + struct.pack("<H", v)
    if 0 <= v <= 0xFFFFFFFF:
        return b"\x0c" + struct.pack("<I", v)
    return b"\x0e" + struct.pack("<Q", v)


def string_obj(s):
    b = s.encode()
    return b"\x0d" + bytes([len(b)]) + b + b"\x00"


def package(items):
    return b"\x12" + pkg(bytes([len(items)]) + b"".join(items))


def seg(n):
    b = n.encode()
    if len(b) > 4:
        raise ValueError("name seg too long: %s" % n)
    return b.ljust(4, b"_")


def name(n, value):
    return b"\x08" + seg(n) + value


def method(n, body, args=0, serialized=False):
    flags = args | (0x08 if serialized else 0)
    return b"\x14" + pkg(seg(n) + bytes([flags]) + body)


def device(n, body):
    return b"\x5b\x82" + pkg(seg(n) + body)


def ret(expr):
    return b"\xa4" + expr


def store(src, dst):
    return b"\x70" + src + dst


def index(base_name, i):
    return b"\x88" + base_name.encode() + int_obj(i)


def l_equal(a, b):
    return b"\x93" + a + b


def if_then(predicate, body):
    return b"\xa0" + pkg(predicate + body)


def else_body(body):
    return b"\xa1" + pkg(body)


def operation_region(n, space, off, length):
    return b"\x5b\x80" + pkg(n.encode() + bytes([space]) + int_obj(off) + int_obj(length))


def field(region, entries):
    payload = region.encode() + b"\x00"
    for e in entries:
        if e[0] is None:
            payload += b"\x00" + bit_len(e[1])
        else:
            payload += e[0].encode() + bit_len(e[1])
    return b"\x5b\x81" + pkg(payload)


def build_table(body, oem_table_id=b"BATDECL"):
    header = bytearray(36)
    header[0:4] = b"SSDT"
    struct.pack_into("<I", header, 4, 36 + len(body))
    header[8] = 2
    header[10:16] = b"HEPUTS"
    header[16:24] = oem_table_id[:8].ljust(8, b" ")
    header[24:28] = b"HEPU"
    struct.pack_into("<I", header, 28, 1)
    struct.pack_into("<I", header, 32, 1)
    checksum = (-sum(header) - sum(body)) & 0xFF
    header[9] = checksum
    return bytes(header) + body


# ---------------------------------------------------------------- 各模式


def make_declared(dev="BAT0"):
    return build_table(
        device(dev, name("_HID", string_obj("PNP0C0A")) + name("_STA", int_obj(0x0F))),
        oem_table_id=b"BATDECL",
    )


def make_valid(percent=57, use_bix=False, dev="BAT0"):
    design = 10000
    full = 10000
    rem = design * percent // 100
    bst0 = name("BST0", package([int_obj(0), int_obj(0), int_obj(0), int_obj(0)]))
    bst_body = (
        store(int_obj(2), index("BST0", 0))
        + store(int_obj(0), index("BST0", 1))
        + store(int_obj(rem), index("BST0", 2))
        + store(int_obj(12000), index("BST0", 3))
        + ret(b"BST0")
    )
    if use_bix:
        caps = method("_BIX", ret(package([int_obj(0), int_obj(0), int_obj(design),
                                           int_obj(full), int_obj(12000)])))
    else:
        caps = method("_BIF", ret(package([int_obj(0), int_obj(design), int_obj(full),
                                           int_obj(1), int_obj(12000)])))
    extra = (
        method("_TIF", if_then(l_equal(int_obj(0x0F), int_obj(0x0F)), ret(int_obj(1)))
               + else_body(ret(int_obj(0))) + ret(int_obj(3)))
        + method("_TAR", ret(b"\x72" + int_obj(0x10) + int_obj(0x20)))
    )
    body = name("_HID", string_obj("PNP0C0A")) + method("_STA", ret(int_obj(0x0F))) \
        + bst0 + method("_BST", bst_body) + caps + extra
    return build_table(device(dev, body), oem_table_id=b"BATVALID")


def make_absent(dev="ABS0"):
    return build_table(
        device(dev, name("_HID", string_obj("PNP0C0A")) + method("_STA", ret(int_obj(0)))),
        oem_table_id=b"BATABS",
    )


def make_nobst(dev="NOB0"):
    return build_table(
        device(dev, name("_HID", string_obj("PNP0C0A"))
               + method("_STA", ret(int_obj(0x0F)))),
        oem_table_id=b"BATNOBST",
    )


def make_ec(percent=57, dev="ECB0"):
    bst0 = name("BST0", package([int_obj(0), int_obj(0), int_obj(0), int_obj(0)]))
    bst_body = (
        store(int_obj(2), index("BST0", 0))
        + store(b"B0ST", index("BST0", 2))     # 读 EC 字段 -> 解释器不支持
        + ret(b"BST0")
    )
    body = (
        name("_HID", string_obj("PNP0C0A"))
        + method("_STA", ret(int_obj(0x0F)))
        + bst0
        + operation_region("ECOR", 3, 0, 0x100)
        + field("ECOR", [("B0ST", 8)])
        + method("_BST", bst_body)
    )
    return build_table(device(dev, body), oem_table_id=b"BATEC")


def make_ec_read(percent=57, dev="ECR0"):
    """EC 型电池：Field 16 位来自 EmbeddedControl，值由主机模拟 EC 提供。"""
    design = 10000
    rem = design * percent // 100
    bst0 = name("BST0", package([int_obj(0), int_obj(0), int_obj(0), int_obj(0)]))
    bst_body = (
        store(int_obj(2), index("BST0", 0))
        + store(b"B0ST", index("BST0", 2))     # 16 位 EC 字段，应拼成 0x1644=5700
        + store(int_obj(12000), index("BST0", 3))
        + ret(b"BST0")
    )
    caps = method("_BIF", ret(package([int_obj(0), int_obj(design), int_obj(design),
                                       int_obj(1), int_obj(12000)])))
    body = (
        name("_HID", string_obj("PNP0C0A"))
        + method("_STA", ret(int_obj(0x0F)))
        + bst0
        + method("_BST", bst_body)
        + caps
        + operation_region("ECOR", 3, 0, 0x100)
        + field("ECOR", [("B0ST", 16)])
    )
    ecdev = device("EC00", name("_HID", string_obj("PNP0C09"))
                   + method("_STA", ret(int_obj(0x0F))))
    return build_table(ecdev + device(dev, body), oem_table_id=b"BATECR")


MODES = {
    "declared": lambda pct=57: make_declared(),
    "valid": make_valid,
    "valid-bix": lambda pct=57: make_valid(pct, use_bix=True),
    "absent": lambda pct=57: make_absent(),
    "nobst": lambda pct=57: make_nobst(),
    "ec": make_ec,
    "ec-read": make_ec_read,
}


def write_array(f, name, table):
    f.write("static const unsigned char %s[] = {" % name)
    for i, b in enumerate(table):
        if i % 12 == 0:
            f.write("\n    ")
        f.write("0x%02x," % b)
    f.write("\n};\n")


def write_header(path):
    valid = make_valid(57, dev="BAT0")
    bix = make_valid(57, use_bix=True, dev="BIX0")
    ec = make_ec(57, dev="ECB0")
    ec_read = make_ec_read(57, dev="ECR0")
    absent = make_absent("ABS0")
    with open(path, "w", encoding="utf-8") as f:
        f.write("/* 由 tools/regress/make_battery_ssdt.py --header 生成，勿手改。 */\n")
        f.write("#ifndef BATTERY_SSDT_BLOBS_H\n#define BATTERY_SSDT_BLOBS_H\n\n")
        write_array(f, "g_battery_valid_ssdt", valid)
        write_array(f, "g_battery_bix_ssdt", bix)
        write_array(f, "g_battery_ec_ssdt", ec)
        write_array(f, "g_battery_ec_read_ssdt", ec_read)
        write_array(f, "g_battery_absent_ssdt", absent)
        f.write("\n#endif\n")
    print("wrote %s (valid=%d bix=%d ec=%d ec_read=%d absent=%d)" %
          (path, len(valid), len(bix), len(ec), len(ec_read), len(absent)))


def main(argv):
    if not argv:
        print(__doc__, file=sys.stderr)
        return 2
    if argv[0] == "--header":
        if len(argv) < 2:
            print(__doc__, file=sys.stderr)
            return 2
        write_header(argv[1])
        return 0

    path = argv[0]
    mode = argv[1] if len(argv) > 1 else "declared"
    if mode not in MODES:
        print("未知模式: %s" % mode, file=sys.stderr)
        return 2
    pct = int(argv[2]) if len(argv) > 2 else 57
    table = MODES[mode](pct)
    with open(path, "wb") as f:
        f.write(table)
    print("wrote %s (%d bytes, mode=%s)" % (path, len(table), mode))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
