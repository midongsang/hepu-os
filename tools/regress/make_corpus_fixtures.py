#!/usr/bin/env python3
"""合成 ACPI 夹具生成器（审计取证用）。

与 tools/regress/make_battery_ssdt.py 同一套极简 AML 编码模式，但刻意覆盖
真实笔记本 DSDT/SSDT 里常见、而当前极简解释器尚未支持的写法。

**这些表全部是合成语料，不是任何真机固件；** 探针结果只能说明“当前解释器
对该写法的处理行为”，不能表述为真机已验证。

用法:
  make_corpus_fixtures.py <输出目录>

生成:
  syn-baseline.aml      基线：现有子集应能建库并求值（对照用）
  syn-ec-region.aml     EC OperationRegion + 16 位 Field
  syn-bankfield.aml     BankField (ext 0x87) 包 EC 字段
  syn-indexfield.aml    IndexField (ext 0x86)
  syn-create-dword.aml   方法内 CreateDWordField (ext 0x8C) 拆包
  syn-osi-branch.aml     If(_OSI(...)) 双分支
  syn-condrefof.aml     CondRefOf (ext 0x12) 守卫可选方法
  syn-extfield.aml      ExtField (ext 0x88)
"""
import os
import struct
import sys


def pkg(payload: bytes) -> bytes:
    total = len(payload) + 1
    if total < 0x40:
        return bytes([total]) + payload
    total = len(payload) + 2
    if total < 0x1000:
        return bytes([0x40 | (total & 0x0F), (total >> 4) & 0xFF]) + payload
    total = len(payload) + 3
    if total < 0x100000:
        return bytes([0x80 | (total & 0x0F), (total >> 4) & 0xFF,
                      (total >> 12) & 0xFF]) + payload
    raise ValueError("payload too large")


def bit_len(bits: int) -> bytes:
    total = bits + 1
    if total < 0x40:
        return bytes([total])
    total = bits + 2
    if total < 0x1000:
        return bytes([0x40 | (total & 0x0F), (total >> 4) & 0xFF])
    raise ValueError("field too wide")


def int_obj(v: int) -> bytes:
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


def string_obj(s: str) -> bytes:
    b = s.encode()
    return b"\x0d" + bytes([len(b)]) + b + b"\x00"


def package(items) -> bytes:
    return b"\x12" + pkg(bytes([len(items)]) + b"".join(items))


def seg(n: str) -> bytes:
    return n.encode().ljust(4, b"_")[:4]


def name(n: str, value: bytes) -> bytes:
    return b"\x08" + seg(n) + value


def method(n: str, body: bytes, args: int = 0) -> bytes:
    return b"\x14" + pkg(seg(n) + bytes([args]) + body)


def device(n: str, body: bytes) -> bytes:
    return b"\x5b\x82" + pkg(seg(n) + body)


def ret(expr: bytes) -> bytes:
    return b"\xa4" + expr


def store(src: bytes, dst: bytes) -> bytes:
    return b"\x70" + src + dst


def operation_region(n: str, space: int, off: int, length: int) -> bytes:
    return b"\x5b\x80" + pkg(seg(n) + bytes([space]) + int_obj(off)
                             + int_obj(length))


def field(region: str, entries) -> bytes:
    payload = seg(region) + b"\x00"
    for e in entries:
        if e[0] is None:
            payload += b"\x00" + bit_len(e[1])
        else:
            payload += seg(e[0]) + bit_len(e[1])
    return b"\x5b\x81" + pkg(payload)


def build_table(body: bytes, oem=b"CORPUS") -> bytes:
    header = bytearray(36)
    header[0:4] = b"SSDT"
    struct.pack_into("<I", header, 4, 36 + len(body))
    header[8] = 2
    header[10:16] = b"HEPUCO"
    header[16:24] = oem[:8].ljust(8, b" ")
    header[24:28] = b"HEPU"
    struct.pack_into("<I", header, 28, 1)
    struct.pack_into("<I", header, 32, 1)
    checksum = (-sum(header) - sum(body)) & 0xFF
    header[9] = checksum
    return bytes(header) + body


BAT_HID = name("_HID", string_obj("PNP0C0A"))


def baseline() -> bytes:
    body = (
        BAT_HID
        + method("_STA", ret(int_obj(0x0F)))
        + name("BST0", package([int_obj(0)] * 4))
        + method("_BST", ret(b"BST0"))
        + method("_BIF", ret(package([int_obj(1), int_obj(10000),
                                      int_obj(10000), int_obj(1),
                                      int_obj(12000)])))
    )
    return build_table(device("BAT0", body), b"SBASE")


def ec_region() -> bytes:
    body = (
        BAT_HID
        + method("_STA", ret(int_obj(0x0F)))
        + operation_region("ECOR", 3, 0, 0x100)
        + field("ECOR", [("B0ST", 16)])
        + name("BST0", package([int_obj(0)] * 4))
        + method("_BST",
                 store(b"B0ST", b"\x88" + b"BST0" + int_obj(2))
                 + ret(b"BST0"))
        + method("_BIF", ret(package([int_obj(1), int_obj(10000),
                                      int_obj(10000), int_obj(1),
                                      int_obj(12000)])))
    )
    return build_table(device("BAT0", body), b"SECRE")


def bankfield() -> bytes:
    # BankFieldOp := 0x5B 0x87 PkgLength RegionName BankName FieldFlags FieldList
    bank = (
        b"\x5b\x87" + pkg(seg("ECOR") + seg("BK00") + b"\x00"
                           + seg("B0ST") + bit_len(16))
    )
    body = (
        BAT_HID
        + method("_STA", ret(int_obj(0x0F)))
        + operation_region("ECOR", 3, 0, 0x100)
        + bank
        + name("BST0", package([int_obj(0)] * 4))
        + method("_BST",
                 store(b"B0ST", b"\x88" + b"BST0" + int_obj(2))
                 + ret(b"BST0"))
    )
    return build_table(device("BAT0", body), b"SBANK")


def indexfield() -> bytes:
    # IndexFieldOp := 0x5B 0x86 PkgLength IndexRegion DataRegion FieldFlags FieldList
    idx = (
        b"\x5b\x86" + pkg(seg("IDXR") + seg("DATR") + b"\x00"
                           + seg("B0ST") + bit_len(16))
    )
    body = (
        BAT_HID
        + method("_STA", ret(int_obj(0x0F)))
        + operation_region("IDXR", 1, 0x66, 2)
        + operation_region("DATR", 1, 0x62, 2)
        + idx
        + name("BST0", package([int_obj(0)] * 4))
        + method("_BST",
                 store(b"B0ST", b"\x88" + b"BST0" + int_obj(2))
                 + ret(b"BST0"))
    )
    return build_table(device("BAT0", body), b"SIDXF")


def create_dword() -> bytes:
    # _BST: Buffer(4) -> CreateDWordField(0x8A 单字节操作码, Buf, 0, DW00) 拆包
    # 依据 ACPICA amlcode.h：CreateDWordField = 0x8A（单字节，非 0x5B 前缀）
    buf = b"\x11" + pkg(int_obj(4))
    create = (b"\x8a" + buf + int_obj(0) + seg("DW00"))
    body = (
        BAT_HID
        + method("_STA", ret(int_obj(0x0F)))
        + method("_BST",
                 create
                 + ret(package([int_obj(2), int_obj(0), b"DW00",
                                 int_obj(12000)])))
        + method("_BIF", ret(package([int_obj(1), int_obj(10000),
                                      int_obj(10000), int_obj(1),
                                      int_obj(12000)])))
    )
    return build_table(device("BAT0", body), b"SCRDW")


def osi_branch() -> bytes:
    # If(_OSI("Windows 2009")) { Return(0) } Else { Return(0x0F) }
    cond = b"_OSI" + string_obj("Windows 2009")
    if_then = b"\xa0" + pkg(cond + ret(int_obj(0)))
    else_body = b"\xa1" + pkg(ret(int_obj(0x0F)))
    body = (
        BAT_HID
        + method("_STA", if_then + else_body)
        + method("_BIF", ret(package([int_obj(1), int_obj(10000),
                                      int_obj(10000), int_obj(1),
                                      int_obj(12000)])))
    )
    return build_table(device("BAT0", body), b"SOSIB")


def condrefof() -> bytes:
    # CondRefOf(\_SB_.BATX, Local0)；存在性守卫后再取值。
    cond = b"\x5b\x12" + b"\\_SB_" + seg("BATX") + b"\x60"
    if_then = b"\xa0" + pkg(b"\x93" + b"\x60" + int_obj(1) + ret(int_obj(0x0F)))
    else_body = b"\xa1" + pkg(ret(int_obj(0x0F)))
    body = (
        BAT_HID
        + method("_STA", cond + if_then + else_body)
        + method("_BIF", ret(package([int_obj(1), int_obj(10000),
                                      int_obj(10000), int_obj(1),
                                      int_obj(12000)])))
    )
    return build_table(device("BAT0", body), b"SCREF")


def extfield() -> bytes:
    # ExtFieldOp := 0x5B 0x88 PkgLength RegionName FieldFlags FieldList
    ext = (
        b"\x5b\x88" + pkg(seg("ECOR") + b"\x00"
                           + seg("B0ST") + bit_len(16))
    )
    body = (
        BAT_HID
        + method("_STA", ret(int_obj(0x0F)))
        + operation_region("ECOR", 3, 0, 0x100)
        + ext
        + name("BST0", package([int_obj(0)] * 4))
        + method("_BST", ret(b"BST0"))
    )
    return build_table(device("BAT0", body), b"SEXTF")


FIXTURES = {
    "syn-baseline.aml": baseline,
    "syn-ec-region.aml": ec_region,
    "syn-bankfield.aml": bankfield,
    "syn-indexfield.aml": indexfield,
    "syn-create-dword.aml": create_dword,
    "syn-osi-branch.aml": osi_branch,
    "syn-condrefof.aml": condrefof,
    "syn-extfield.aml": extfield,
}


def main(argv):
    outdir = argv[1] if len(argv) > 1 else "."
    os.makedirs(outdir, exist_ok=True)
    for name, fn in FIXTURES.items():
        blob = fn()
        with open(os.path.join(outdir, name), "wb") as f:
            f.write(blob)
        print(f"wrote {name} ({len(blob)} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
