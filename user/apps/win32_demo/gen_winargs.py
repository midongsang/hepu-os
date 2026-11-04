#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""赫普OS V3.87 阶段B-3: 生成 Win32 PE32 控制台演示程序 winargs.exe
   演示能力 (全走 KERNEL32.dll 导入, 内核 stub 直通 HEPU syscall):
     - GetStdHandle(-11)     → 伪句柄(1)
     - GetModuleFileNameA(0) → 真实模块路径 (stub 纯用户态 rep movsb)
     - GetCommandLineA()     → 真实命令整串 (加载器注入 argv 结构)
     - 直接读 argv 表        → 打印 argc 与 argv[0..n] (stub 页 0x4031E0)
     - ReadFile(stdin)       → 无输入立即返回 0 (不阻塞), 写回 lpNumberOfBytesRead
     - GetTickCount / Sleep  → 毫秒睡眠验证
     - WriteConsoleA / ExitProcess
   导入 8 个 API: WriteConsoleA, GetStdHandle, GetCommandLineA,
                 GetTickCount, Sleep, ExitProcess, GetModuleFileNameA, ReadFile
   布局: .text raw=0x500 (代码+字符串+缓冲), .idata raw=0x300 (8 导入).
   输出: winargs.exe (0xB00 字节) —— 由 HEPU binfmt_pe.c 加载执行.
"""

import struct
import sys

IMAGEBASE      = 0x00400000
ENTRY_RVA      = 0x1000
SIZE_OF_IMAGE  = 0x3000          # 0x403000 起为 stub 页 (加载器约定)
STUB_BASE      = IMAGEBASE + SIZE_OF_IMAGE   # 0x403000

# ---- IAT / thunk / hint-name 布局 (.idata VA 0x2000) ----
IDATA_RVA   = 0x2000
IID_OFF     = 0x00               # IMAGE_IMPORT_DESCRIPTOR (20B)
IID_END     = 0x14               # 终止描述符
THUNK_OFF   = 0x28               # OriginalFirstThunk 数组 (8 槽)
IAT_OFF     = 0x48               # FirstThunk / IAT (8 槽)
DLL_OFF     = 0x68               # "KERNEL32.dll\0"
NAME_OFF    = 0x78               # hint/name 表 (依次 8 项)

APIS = [b"WriteConsoleA", b"GetStdHandle", b"GetCommandLineA",
        b"GetTickCount", b"Sleep", b"ExitProcess",
        b"GetModuleFileNameA", b"ReadFile"]
N = len(APIS)

# hint/name 表布局: 每一项 = WORD hint + 名\0, 顺序连续
name_off = []
pos = NAME_OFF
for api in APIS:
    name_off.append(pos)
    pos += 2 + len(api) + 1
IDATA_TOTAL = pos - IDATA_RVA      # 相对 .idata 起点
assert IDATA_TOTAL <= 0x300, hex(IDATA_TOTAL)

# IAT 槽虚拟地址 (内核改写为 stub)
iat_va = [IMAGEBASE + IDATA_RVA + IAT_OFF + i * 4 for i in range(N)]
idx = {api: i for i, api in enumerate(APIS)}

# ---- .text: 代码区 0x200 + 字符串区 0x200 起 ----
CODE_SIZE = 0x200
STR_BASE = IMAGEBASE + 0x1000 + CODE_SIZE
STR = {
    "sh_ok":   b"stdhandle ok\n",
    "sh_fail": b"stdhandle fail\n",
    "mod":     b"module: ",
    "cmd":     b"cmdline: ",
    "argv":    b"\nargv[",
    "argv_hi": b"]=",
    "spc":     b" ",
    "nl":      b"\n",
    "read":    b"read: ",
    "bytes":   b" bytes\n",
    "tk1":     b"tick nonzero\n",
    "tk0":     b"tick zero\n",
    "sleep":   b"sleep done\n",
    "hex":     b"0123456789ABCDEF",
}
str_va = {}
p = STR_BASE
for k, v in STR.items():
    str_va[k] = p
    p += len(v) + 1
# 数据缓冲 (静态区, 字符串后对齐)
p = (p + 0xF) & ~0xF
HEX_BUF = p;   p += 16          # whex 输出 8 字符 + 0
MOD_BUF = p;   p += 64          # GetModuleFileNameA 缓冲
READ_BUF = p;  p += 64          # ReadFile 缓冲
NREAD = p;     p += 4           # lpNumberOfBytesRead
ARGV_TBL = STUB_BASE + 0x1E0    # 加载器注入的 argv 表 (内核约定, 见 binfmt_pe.c)
ARGV_IDX = STUB_BASE + 0x240    # argv 循环索引槽 (stub 页数据区空闲区, 0x240-0x27F)
assert p - (IMAGEBASE + 0x1000) <= 0x500, hex(p)

# ---- 指令组装: 两遍回填跳转 ----
insns = []

def raw(b):
    insns.append(("raw", b))
def jz(label):
    insns.append(("jz", label))
def jnz(label):
    insns.append(("jnz", label))
def jmp(label):
    insns.append(("jmp", label))
def call_rel(label):
    insns.append(("call_rel", label))
def mov_esi(label):
    insns.append(("mov_esi", label))
def mov_imm(v):
    insns.append(("mov_imm", v))
def loop_rel(label):
    insns.append(("loop_rel", label))
def jl_rel(label):
    insns.append(("jl_rel", label))
def label(name):
    insns.append(("label", name))

def call_iat(api):
    raw(b"\xFF\x15" + struct.pack("<I", iat_va[idx[api]]))

# --- 入口 ---
label("entry")
# GetStdHandle(-11) → 1
raw(b"\x6A\xF5")                                   # push -11
call_iat(b"GetStdHandle")
raw(b"\x3D\x01\x00\x00\x00")                       # cmp eax,1
jz("sh_ok")
mov_esi("sh_fail")
call_rel("write_str")
jmp("after_sh")
label("sh_ok")
mov_esi("sh_ok")
call_rel("write_str")
label("after_sh")
# --- GetModuleFileNameA(0, MOD_BUF, 64) ---
raw(b"\x6A\x40")                                   # push 64 (nSize)
raw(b"\x68" + struct.pack("<I", MOD_BUF))          # push MOD_BUF (lpFilename)
raw(b"\x6A\x00")                                   # push 0 (hModule)
call_iat(b"GetModuleFileNameA")
mov_esi("mod")
call_rel("write_str")
mov_imm(MOD_BUF)                                   # esi = 模块路径缓冲
call_rel("write_str")
mov_esi("nl")
call_rel("write_str")
# --- GetCommandLineA → 命令整串 ---
call_iat(b"GetCommandLineA")
raw(b"\x50")                                       # push eax (保存命令行指针, write_str 会覆盖 eax)
mov_esi("cmd")
call_rel("write_str")
raw(b"\x5E")                                       # pop esi (命令行指针)
call_rel("write_str")
mov_esi("nl")
call_rel("write_str")
# --- 读 argv 表: edi=表, ecx=argc, ebx=idx 1..argc ---
raw(b"\xBF" + struct.pack("<I", ARGV_TBL))         # mov edi,ARGV_TBL
raw(b"\x8B\x0F")                                   # mov ecx,[edi] (argc)
label("argv_hdr")
mov_esi("argv")                                    # "\nargv["
call_rel("write_str")
raw(b"\x89\xC8")                                   # mov eax,ecx (argc → whex 入参 eax)
call_rel("whex")                                   # 打印 argc hex
# whex 破坏 edi/ecx → 重新加载 argv 表指针与 argc
raw(b"\xBF" + struct.pack("<I", ARGV_TBL))         # mov edi,ARGV_TBL
raw(b"\x8B\x0F")                                   # mov ecx,[edi] (argc)
mov_esi("argv_hi")                                 # "]= "
call_rel("write_str")
# 循环打印 argv[1..argc]
# 索引存固定内存槽 ARGV_IDX: write_str 经 WCA stub 破坏 ebx/ecx/edx/eax(返回), 
# stub 返回 ebx=0 → 任何 cmp ebx,* 都会恒 <argc 死循环 → 改用 test esi,esi 判 argv 表 null 终止退出
raw(b"\xC7\x05" + struct.pack("<I", ARGV_IDX) + b"\x00\x00\x00\x00")  # mov dword [ARGV_IDX],0
label("argv_lp")
raw(b"\xA1" + struct.pack("<I", ARGV_IDX))         # mov eax,[ARGV_IDX]
raw(b"\x40")                                       # inc eax
raw(b"\xA3" + struct.pack("<I", ARGV_IDX))         # mov [ARGV_IDX],eax
raw(b"\x89\xC3")                                   # mov ebx,eax (idx)
raw(b"\xBF" + struct.pack("<I", ARGV_TBL))         # mov edi,ARGV_TBL
raw(b"\x8B\x74\x9F\x00")                           # mov esi,[edi+ebx*4] (argv[ebx])
raw(b"\x85\xF6")                                   # test esi,esi
jz("argv_done")                                    # argv 表 null 终止 → 退出
call_rel("write_str")
mov_esi("spc")
call_rel("write_str")
jmp("argv_lp")                                     # 无条件回跳 (不依赖 flags/寄存器)
label("argv_done")
mov_esi("nl")
call_rel("write_str")
# --- ReadFile(stdin=1, READ_BUF, 64, &NREAD, NULL) ---
raw(b"\x6A\x00")                                   # push 0 (lpOverlapped)
raw(b"\x68" + struct.pack("<I", NREAD))            # push &NREAD
raw(b"\x6A\x40")                                   # push 64
raw(b"\x68" + struct.pack("<I", READ_BUF))         # push READ_BUF
raw(b"\x6A\x01")                                   # push 1 (stdin 伪句柄)
call_iat(b"ReadFile")
mov_esi("read")
call_rel("write_str")
raw(b"\xA1" + struct.pack("<I", NREAD))            # mov eax,[NREAD]
call_rel("whex")
mov_esi("bytes")
call_rel("write_str")
# --- GetTickCount ---
call_iat(b"GetTickCount")
raw(b"\x85\xC0")                                   # test eax,eax
jnz("tk_ok")
mov_esi("tk0")
call_rel("write_str")
jmp("after_tk")
label("tk_ok")
mov_esi("tk1")
call_rel("write_str")
label("after_tk")
# --- Sleep(300) ---
raw(b"\x68\x2C\x01\x00\x00")                       # push 300
call_iat(b"Sleep")
mov_esi("sleep")
call_rel("write_str")
# --- ExitProcess(0) ---
raw(b"\x6A\x00")
call_iat(b"ExitProcess")
raw(b"\xCC")                                       # int3 fallback

# --- write_hex: eax → 8 位 hex 到 HEX_BUF, esi=HEX_BUF ---
label("whex")
raw(b"\x89\xC6")                                   # mov esi,eax (原始值)
raw(b"\x89\xF0")                                   # mov eax,esi
raw(b"\xB9\x08\x00\x00\x00")                       # mov ecx,8
raw(b"\xBF" + struct.pack("<I", HEX_BUF))          # mov edi,HEX_BUF
label("whex_l")
raw(b"\xC1\xC0\x04")                               # rol eax,4
raw(b"\x50")                                       # push eax
raw(b"\x25\x0F\x00\x00\x00")                       # and eax,0xF
raw(b"\x89\xC6")                                   # mov esi,eax
raw(b"\x8A\x86" + struct.pack("<I", str_va["hex"]))# mov al,[esi+HEX_TBL]
raw(b"\x88\x07")                                   # mov [edi],al
raw(b"\x47")                                       # inc edi
raw(b"\x58")                                       # pop eax
loop_rel("whex_l")                                 # loop whex_l (两遍回填 rel8)
raw(b"\xC6\x07\x00")                               # mov byte [edi],0
raw(b"\xBE" + struct.pack("<I", HEX_BUF))          # mov esi,HEX_BUF
raw(b"\xE9" + b"\x00\x00\x00\x00")                 # jmp write_str (回填)
label("write_str")
# --- write_str: esi=字符串, 输出到 \0 (WriteConsoleA stdcall 5 参) ---
raw(b"\x89\xF7")                                   # mov edi,esi
raw(b"\x31\xC9")                                   # xor ecx,ecx
label("cnt")
raw(b"\x80\x3F\x00")                               # cmp byte [edi],0
jz("cnt_done")
raw(b"\x47")                                       # inc edi
raw(b"\x41")                                       # inc ecx
jmp("cnt")
label("cnt_done")
raw(b"\x6A\x00")                                   # push 0 (reserved/written 槽)
raw(b"\x89\xE0")                                   # mov eax,esp (written ptr)
raw(b"\x6A\x00")                                   # push 0 (reserved)
raw(b"\x51")                                       # push ecx (n)
raw(b"\x56")                                       # push esi (buf)
raw(b"\x6A\xF5")                                   # push -11 (hOut)
call_iat(b"WriteConsoleA")
# 注: WriteConsoleA 是 stdcall 5 参, stub 侧 ret 20 已弹全部 5 参,
# 调用者无需再清栈——多弹会导致 ret 错位
raw(b"\xC3")                                       # ret

# --- 两遍: 计算 label 偏移 + 回填 (含 whex 尾部 jmp write_str) ---
offs = {}
o = 0
for kind, p in insns:
    if kind == "label":
        offs[p] = o
        continue
    if kind in ("jz", "jnz", "jmp", "loop_rel", "jl_rel"):
        o += 2
    elif kind in ("call_rel", "mov_esi", "mov_imm"):
        o += 5
    else:
        o += len(p)

code = bytearray()
# 先按 raw 展开, whex 尾部 jmp 用占位 4B, 再定位回填
for kind, p in insns:
    if kind == "label":
        continue
    if kind == "raw":
        code += p
    elif kind == "jz":
        rel = offs[p] - (len(code) + 2)
        assert -128 <= rel <= 127, (p, rel)
        code += b"\x74" + bytes([rel & 0xFF])
    elif kind == "jnz":
        rel = offs[p] - (len(code) + 2)
        assert -128 <= rel <= 127, (p, rel)
        code += b"\x75" + bytes([rel & 0xFF])
    elif kind == "jmp":
        rel = offs[p] - (len(code) + 2)
        assert -128 <= rel <= 127, (p, rel)
        code += b"\xEB" + bytes([rel & 0xFF])
    elif kind == "loop_rel":
        rel = offs[p] - (len(code) + 2)
        assert -128 <= rel <= 127, (p, rel)
        code += b"\xE2" + bytes([rel & 0xFF])
    elif kind == "jl_rel":
        rel = offs[p] - (len(code) + 2)
        assert -128 <= rel <= 127, (p, rel)
        code += b"\x7C" + bytes([rel & 0xFF])
    elif kind == "call_rel":
        rel = offs[p] - (len(code) + 5)
        code += b"\xE8" + struct.pack("<i", rel)
    elif kind == "mov_esi":
        code += b"\xBE" + struct.pack("<I", str_va[p])
    elif kind == "mov_imm":
        code += b"\xBE" + struct.pack("<I", p)
    else:
        raise ValueError(kind)
assert len(code) <= CODE_SIZE, hex(len(code))
# whex 尾部 4B 占位跳转 (E9 rel32) → 回填到 write_str
whex_jmp_off = None
o = 0
for kind, p in insns:
    if kind == "label":
        if p == "whex":
            pass
        continue
    if kind == "raw" and p == b"\xE9\x00\x00\x00\x00":
        whex_jmp_off = o
        break
    if kind in ("jz", "jnz", "jmp", "loop_rel", "jl_rel"):
        o += 2
    elif kind in ("call_rel", "mov_esi", "mov_imm"):
        o += 5
    else:
        o += len(p)
assert whex_jmp_off is not None
rel = offs["write_str"] - (whex_jmp_off + 5)
code[whex_jmp_off:whex_jmp_off + 5] = b"\xE9" + struct.pack("<i", rel)

code += b"\x00" * (CODE_SIZE - len(code))
for k, v in STR.items():
    code += v + b"\x00"
# 数据缓冲区
code += b"\x00" * (HEX_BUF - (IMAGEBASE + 0x1000) - len(code))
code += b"\x00" * 16          # HEX_BUF
code += b"\x00" * 64          # MOD_BUF
code += b"\x00" * 64          # READ_BUF
code += b"\x00" * 4           # NREAD
code += b"\x00" * (0x500 - len(code))
assert len(code) == 0x500

# ---- .idata ----
idata = bytearray(0x300)
struct.pack_into("<IIIII", idata, IID_OFF,
                 IDATA_RVA + THUNK_OFF,     # OriginalFirstThunk
                 0, 0,
                 IDATA_RVA + DLL_OFF,       # Name
                 IDATA_RVA + IAT_OFF)       # FirstThunk
for i in range(N):
    struct.pack_into("<I", idata, THUNK_OFF + i * 4,
                     IDATA_RVA + name_off[i])
idata[DLL_OFF:DLL_OFF + 13] = b"KERNEL32.dll\x00"
for i, api in enumerate(APIS):
    off = name_off[i]
    idata[off:off + 2] = struct.pack("<H", 0)
    idata[off + 2:off + 2 + len(api)] = api

# ---- DOS + PE 头 ----
dos = bytearray(0x80)
dos[0:2] = b"MZ"
struct.pack_into("<I", dos, 0x3C, 0x80)

opt = struct.pack("<HH", 0x10B, 0)
opt += struct.pack("<I", 0x500)                # SizeOfCode
opt += struct.pack("<I", 0x300)                # SizeOfInitializedData
opt += struct.pack("<I", 0)                    # SizeOfUninitializedData
opt += struct.pack("<I", ENTRY_RVA)            # AddressOfEntryPoint
opt += struct.pack("<I", 0x1000)               # BaseOfCode
opt += struct.pack("<I", 0x2000)               # BaseOfData
opt += struct.pack("<I", IMAGEBASE)
opt += struct.pack("<I", 0x1000)               # SectionAlignment
opt += struct.pack("<I", 0x200)                # FileAlignment
opt += struct.pack("<HHHHHH", 4, 0, 0, 0, 4, 0)
opt += struct.pack("<I", 0)
opt += struct.pack("<I", SIZE_OF_IMAGE)
opt += struct.pack("<I", 0x300)                # SizeOfHeaders
opt += struct.pack("<I", 0)
opt += struct.pack("<HH", 3, 0)                # Subsystem=CONSOLE
opt += struct.pack("<IIII", 0x100000, 0x1000, 0x100000, 0x1000)
opt += struct.pack("<II", 0, 16)
dirs = [(0, 0)] * 16
dirs[1] = (IDATA_RVA, 0x100)
for va, sz in dirs:
    opt += struct.pack("<II", va, sz)
assert len(opt) == 0xE0

coff = struct.pack("<HHI", 0x14C, 2, 0)
coff += struct.pack("<II", 0, 0)
coff += struct.pack("<HH", 0xE0, 0x0102)

def sect(name, vsz, va, rawsz, rawoff, chars):
    s = name.ljust(8, b"\x00")
    s += struct.pack("<IIIIII", vsz, va, rawsz, rawoff, 0, 0)
    s += struct.pack("<HHI", 0, 0, chars)
    return s

sec_tbl  = sect(b".text",  0x500, 0x1000, 0x500, 0x300, 0x60000020)
sec_tbl += sect(b".idata", 0x300, 0x2000, 0x300, 0x800, 0xC0000040)

pe = b"PE\x00\x00" + coff + opt + sec_tbl
assert len(dos) + len(pe) <= 0x300
out = bytes(dos) + pe
out += b"\x00" * (0x300 - len(out))
out += bytes(code)          # raw @ 0x300 (.text)
out += bytes(idata)         # raw @ 0x800 (.idata)
assert len(out) == 0xB00, hex(len(out))

path = sys.argv[1] if len(sys.argv) > 1 else "winargs.exe"
with open(path, "wb") as f:
    f.write(out)
print("gen winargs.exe:", len(out), "bytes, apis =", N)
