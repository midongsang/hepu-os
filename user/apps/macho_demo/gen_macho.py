#!/usr/bin/env python3
"""
赫普OS - 阶段C: 最小 32 位 x86 Mach-O 演示程序生成器
生成 MH_EXECUTE / CPU_TYPE_X86 的 Mach-O 二进制 (macho_demo):
  - Mach-O 头 (magic=0xFEEDFACE, 32 位小端)
  - LC_SEGMENT __TEXT (1 个 __text section, vmaddr=0x1000)
  - LC_UNIXTHREAD (x86_THREAD_STATE, eip=0x1000)
代码: 经 stub 页桥接打印 "macho: hello from HEPU!" + "cmdline: <...>" +
      argv0 + argv1 (V3.87 C-2: 真实 argv 槽区注入) + 换行, 然后 exit(0)
stub 页地址/数据地址由加载器 binfmt_macho.c 按固定 patch 偏移回填 (call rel32 + imm32).

patch 偏移表 (代码区偏移, 加载器硬编码使用; imm 为 push 指令 opcode+1):
  +0x06: msg 地址 (imm32 = 0x1000 + MSG_OFF)
  +0x0B: call stub_write #1 rel32 (= stub_va - N_CALL1)  N_CALL1=0x10
  +0x19: label2 地址 (imm32 = 0x1000 + LBL2_OFF)
  +0x1D: call stub_write #2 rel32                       N_CALL2=0x22
  +0x2B: cmdline 地址 (imm32 = stub_va + 0x100)
  +0x2F: call stub_write #3 rel32                       N_CALL3=0x34
  +0x3D: nl 地址 (imm32 = 0x1000 + NL_OFF)
  +0x41: call stub_write #4 rel32                       N_CALL4=0x46
  +0x4E: argv0 地址 (imm32 = stub_va + 0x160)
  +0x53: call stub_write #5 rel32                       N_CALL5=0x57
  +0x60: nl 地址 (imm32 = 0x1000 + NL_OFF)
  +0x65: call stub_write #6 rel32                       N_CALL6=0x69
  +0x72: argv1 地址 (imm32 = stub_va + 0x180)
  +0x77: call stub_write #7 rel32                       N_CALL7=0x7B
  +0x84: nl 地址 (imm32 = 0x1000 + NL_OFF)
  +0x89: call stub_write #8 rel32                       N_CALL8=0x8D
  +0x93: call stub_exit rel32                           N_EXIT=0x97
"""
import struct
import sys

MH_MAGIC      = 0xFEEDFACE
CPU_TYPE_X86  = 7
CPU_SUBTYPE_I386 = 3
MH_EXECUTE    = 2
LC_SEGMENT    = 0x1
LC_UNIXTHREAD = 0x5
X86_THREAD_STATE = 1

VM_BASE = 0x1000
MSG_OFF = 0x99
LBL2_OFF = 0xB1
NL_OFF  = 0xBA

def build_code():
    """生成 demo 代码 (固定布局, patch 点写 0, 由加载器回填)"""
    c = bytearray()
    def print_blk(imm):
        nonlocal c
        # push imm32 (5B) + push addr(占位) (5B) + call(占位) (5B) + add esp,8 (3B)
        c += struct.pack('<B', 0x68) + struct.pack('<I', imm)
        c += struct.pack('<B', 0x68) + struct.pack('<I', 0)
        c += struct.pack('<B', 0xE8) + struct.pack('<i', 0)
        c += b'\x83\xC4\x08'
    # 打印 "macho: hello from HEPU!\n" (24B)      msg  [P+0x06]
    print_blk(24)
    # 打印 "cmdline: " (9B)                       label2 [P+0x19]
    print_blk(9)
    # 打印 cmdline 串 (最多 32B)                  cmdline [P+0x2B]
    print_blk(32)
    # 打印 "\n"                                   nl [P+0x3D]
    print_blk(1)
    # 打印 argv0 串 (最多 32B, stub 页 0x160)     argv0 [P+0x4E]
    print_blk(32)
    # 打印 "\n"                                   nl [P+0x60]
    print_blk(1)
    # 打印 argv1 串 (最多 32B, stub 页 0x180)     argv1 [P+0x72]
    print_blk(32)
    # 打印 "\n"                                   nl [P+0x84]
    print_blk(1)
    # exit(0)
    c += b'\x6A\x00'                                      # push 0
    c += struct.pack('<B', 0xE8) + struct.pack('<i', 0)   # call stub_exit [P+0x93]
    # fallback
    c += b'\xEB\xD4'                                      # jmp 0x1000
    # 数据
    assert len(c) == MSG_OFF, 'code head len=%d want %d' % (len(c), MSG_OFF)
    c += b'macho: hello from HEPU!\n'
    assert len(c) == LBL2_OFF, 'msg len=%d want %d' % (len(c), LBL2_OFF)
    c += b'cmdline: '
    assert len(c) == NL_OFF, 'lbl2 len=%d want %d' % (len(c), NL_OFF)
    c += b'\n'
    return bytes(c)

def main(out):
    code = build_code()
    fileoff = 0xE8  # hdr(28) + seg(56+68=124) + thr(80) = 232 = 0xE8
    # LC_SEGMENT __TEXT (56B cmd + 68B section)
    seg = struct.pack('<2I', LC_SEGMENT, 56 + 68)          # cmd, cmdsize
    seg += b'__TEXT' + b'\x00' * 10                        # segname[16]
    seg += struct.pack('<6I',
                       VM_BASE, len(code),                # vmaddr, vmsize
                       fileoff, len(code),                # fileoff, filesize
                       7, 5)                              # maxprot(RWX), initprot(RE)
    seg += struct.pack('<2I', 1, 0)                       # nsects, flags
    # section __text
    seg += b'__text' + b'\x00' * 10                       # sectname[16]
    seg += b'__TEXT' + b'\x00' * 10                       # segname[16] (section 内)
    seg += struct.pack('<9I',
                       VM_BASE, len(code),                # addr, size
                       fileoff, 0,                        # offset, align
                       0, 0,                              # reloff, nreloc
                       0x80000400, 0, 0)                  # flags(S_REGULAR|S_ATTR_PURE_INSTRUCTIONS), res1, res2
    # LC_UNIXTHREAD (80)
    thr = struct.pack('<IIII', LC_UNIXTHREAD, 80, X86_THREAD_STATE, 16)
    regs = [0] * 16
    regs[7]  = 0x40010000    # esp
    regs[9]  = 0x200         # eflags
    regs[10] = VM_BASE       # eip
    thr += struct.pack('<16I', *regs)

    hdr = struct.pack('<7I', MH_MAGIC, CPU_TYPE_X86, CPU_SUBTYPE_I386,
                      MH_EXECUTE, 2, 56 + 68 + 80, 0)
    blob = hdr + seg + thr
    pad = fileoff - len(blob)
    assert pad >= 0, 'header too big: %d > 0xE8' % len(blob)
    blob += b'\x00' * pad
    blob += code
    with open(out, 'wb') as f:
        f.write(blob)
    print('macho_demo: %d bytes (code=%d, fileoff=0x%x)' % (len(blob), len(code), fileoff))

if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else 'build_v250/macho_demo')
