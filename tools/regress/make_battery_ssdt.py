#!/usr/bin/env python3
"""生成一个只用于 QEMU 取证的最小 SSDT：声明 PNP0C0A（不实现 _BST）。

它不是产品固件的一部分，只用于验证 M1 的“声明存在 -> DECLARED”分支。
"""
import struct
import sys

# Device(BAT0) { Name(_HID, "PNP0C0A"); Name(_STA, 0x0F) }
body = bytes([
    0x5B, 0x82, 0x1A, 0x42, 0x41, 0x54, 0x30,          # Device(BAT0) pkg 26
    0x08, 0x5F, 0x48, 0x49, 0x44,                       # Name(_HID
    0x0D, 0x50, 0x4E, 0x50, 0x30, 0x43, 0x30, 0x41, 0x00,  # "PNP0C0A"
    0x08, 0x5F, 0x53, 0x54, 0x41, 0x0A, 0x0F,           # Name(_STA, 0x0F)
])

header = bytearray(36)
header[0:4] = b"SSDT"
struct.pack_into("<I", header, 4, 36 + len(body))
header[8] = 2
header[10:16] = b"HEPUTS"
header[16:24] = b"BATDECL "
header[24:28] = b"HEPU"
struct.pack_into("<I", header, 28, 1)
struct.pack_into("<I", header, 32, 1)
checksum = (-sum(header) - sum(body)) & 0xFF
header[9] = checksum
table = bytes(header) + body

if len(sys.argv) != 2:
    print("用法: make_battery_ssdt.py <输出文件>", file=sys.stderr)
    sys.exit(2)
open(sys.argv[1], "wb").write(table)
print("wrote %s (%d bytes, checksum=0x%02x)" % (sys.argv[1], len(table), checksum))
