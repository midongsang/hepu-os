#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""赫普OS V3.87 阶段B: 生成最小 Win32 PE32 控制台程序 winhello.exe
   手工按公开 PE/COFF 规范(IMAGE_FILE_MACHINE_I386 / PE32 / 控制台子系统)
   构造: DOS头 + PE头 + .text(代码/消息) + .idata(导入表 KERNEL32.dll)。
   调用约定 stdcall: WriteConsoleA(-11, msg, 21, 0, 0) + ExitProcess(0)。
   输出: winhello.exe (约 1.5KB) —— 由 HEPU 内核 binfmt_pe.c 加载执行,
   IAT 槽由加载器改写为内核提供的用户态 trampoline stub 地址。
"""

import struct
import sys

IMAGEBASE      = 0x00400000
SEC_ALIGN      = 0x1000
FILE_ALIGN     = 0x200
ENTRY_RVA      = 0x1000          # .text 首字节
SIZE_OF_IMAGE  = 0x3000          # .text/.idata 之后 0x3000 是 stub 页

MSG = b"Hello from Win32 PE on HEPU!\n"
MSG_LEN = len(MSG)               # 29

# ---- .text 代码 (字节级, 用 call [IAT] 间接调用) ----
msg_va   = IMAGEBASE + 0x1000 + 0x20
iat_wca  = IMAGEBASE + 0x2000 + 0x34   # WriteConsoleA IAT 槽
iat_ep   = IMAGEBASE + 0x2000 + 0x38   # ExitProcess    IAT 槽

text_code = bytearray()
text_code += b"\x6A\x00"                       # push 0           ; reserved
text_code += b"\x6A\x00"                       # push 0           ; written
# V6.48 BUG-20调试: 读取stub页0x00403014字节(stub_write中ret 20的操作码0xC2)
# 如果stub页映射正确, edx=0xC2; 如果映射错误, edx为其他值.
text_code += b"\xA1" + struct.pack("<I", 0x00403014)  # mov eax, [0x403014]
text_code += b"\x25\xFF\x00\x00\x00"           # and eax, 0xFF
text_code += b"\x50"                             # push eax          ; n (调试用)
text_code += b"\x68" + struct.pack("<I", msg_va)  # push msg
text_code += b"\x68\xF5\xFF\xFF\xFF"           # push -11         ; STD_OUTPUT_HANDLE
text_code += b"\xFF\x15" + struct.pack("<I", iat_wca)  # call [WriteConsoleA]
text_code += b"\x6A\x00"                       # push 0           ; exit code
text_code += b"\xFF\x15" + struct.pack("<I", iat_ep)   # call [ExitProcess]
text_code += b"\xCC"                           # int3 (fallback)
text_code += b"\x00" * (0x20 - len(text_code))
text_code += MSG + b"\x00"
assert len(text_code) <= 0x200
text_code += b"\x00" * (0x200 - len(text_code))

# ---- .idata: 导入描述符 + thunk + IAT + DLL名 + hint/name 表 ----
# 布局: 0x00 iid(20) | 0x14 终止(20) | 0x28 thunk(12) | 0x34 IAT(12)
#       0x40 DLL名(13) | 0x50 WCA hint/name(17) | 0x61 EP hint/name(14) | 总 0x6F
idata = bytearray(0x200)
# IMAGE_IMPORT_DESCRIPTOR[0] (KERNEL32.dll)
struct.pack_into("<IIIII", idata, 0x00,
                 0x2000 + 0x28,   # OriginalFirstThunk -> thunk 数组
                 0,               # TimeDateStamp
                 0,               # ForwarderChain
                 0x2000 + 0x40,   # Name -> "KERNEL32.dll"
                 0x2000 + 0x34)   # FirstThunk -> IAT 数组
# 终止描述符 (全零)
# thunk 数组 (hint/name RVA 表)
struct.pack_into("<III", idata, 0x28,
                 0x2000 + 0x50,   # -> WriteConsoleA hint/name
                 0x2000 + 0x61,   # -> ExitProcess    hint/name
                 0)               # 终止
# IAT 槽 (内核加载时改写为 stub 地址; 先填 0)
struct.pack_into("<III", idata, 0x34, 0, 0, 0)
# DLL 名
idata[0x40:0x40+13] = b"KERNEL32.dll\x00"
# hint/name 表: WORD hint + ASCII 名 (\0 结尾)
idata[0x50:0x50+16] = struct.pack("<H", 0) + b"WriteConsoleA\x00"   # 2 + 14 = 16
idata[0x61:0x61+14] = struct.pack("<H", 0) + b"ExitProcess\x00"     # 2 + 12 = 14

# ---- DOS 头 + PE 头 ----
dos = bytearray(0x80)
dos[0:2]   = b"MZ"
struct.pack_into("<I", dos, 0x3C, 0x80)        # e_lfanew

opt = struct.pack("<HH", 0x10B, 0)             # Magic=PE32, linker ver
opt += struct.pack("<I", 0x200)                # SizeOfCode
opt += struct.pack("<I", 0x200)                # SizeOfInitializedData
opt += struct.pack("<I", 0)                    # SizeOfUninitializedData
opt += struct.pack("<I", ENTRY_RVA)            # AddressOfEntryPoint
opt += struct.pack("<I", 0x1000)               # BaseOfCode
opt += struct.pack("<I", 0x2000)               # BaseOfData
opt += struct.pack("<I", IMAGEBASE)            # ImageBase
opt += struct.pack("<I", SEC_ALIGN)            # SectionAlignment
opt += struct.pack("<I", FILE_ALIGN)           # FileAlignment
opt += struct.pack("<HHHHHH", 4, 0, 0, 0, 4, 0)  # OS/Image/Subsystem ver
opt += struct.pack("<I", 0)                    # Win32VersionValue
opt += struct.pack("<I", SIZE_OF_IMAGE)        # SizeOfImage
opt += struct.pack("<I", 0x300)                # SizeOfHeaders (头总长 0x248, 对齐 0x300)
opt += struct.pack("<I", 0)                    # CheckSum
opt += struct.pack("<HH", 3, 0)                # Subsystem=CONSOLE, DllChar
opt += struct.pack("<IIII", 0x100000, 0x1000, 0x100000, 0x1000)  # stack/heap
opt += struct.pack("<II", 0, 16)               # LoaderFlags, NumberOfRvaAndSizes
dirs = [(0,0)] * 16
dirs[1] = (0x2000, 0x100)                      # Import Table
for va, sz in dirs:
    opt += struct.pack("<II", va, sz)
assert len(opt) == 0xE0

coff = struct.pack("<HHI", 0x14C, 2, 0)        # Machine=i386, 2 sections
coff += struct.pack("<II", 0, 0)               # symtab ptr/count
coff += struct.pack("<HH", 0xE0, 0x0102)       # OptHdrSize, Characteristics

def sect(name, vsz, va, rawsz, rawoff, chars):
    s = name.ljust(8, b"\x00")
    s += struct.pack("<IIIIII", vsz, va, rawsz, rawoff, 0, 0)
    s += struct.pack("<HHI", 0, 0, chars)
    return s

sec_tbl  = sect(b".text",  0x200, 0x1000, 0x200, 0x300, 0x60000020)
sec_tbl += sect(b".idata", 0x100, 0x2000, 0x200, 0x500, 0xC0000040)

pe = b"PE\x00\x00" + coff + opt + sec_tbl
assert len(dos) + len(pe) <= 0x300
out = bytes(dos) + pe
out += b"\x00" * (0x300 - len(out))          # 头区填充到 SizeOfHeaders(0x300)
out += bytes(text_code)                      # raw @ 0x300
out += bytes(idata)                          # raw @ 0x500
assert len(out) == 0x700, hex(len(out))      # 0x300 头 + 0x200 + 0x200

path = sys.argv[1] if len(sys.argv) > 1 else "winhello.exe"
with open(path, "wb") as f:
    f.write(out)
print("gen winhello.exe:", len(out), "bytes, msg_len =", MSG_LEN)
