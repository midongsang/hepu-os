#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
赫普OS 指令级模拟器 (Unicorn Engine)
====================================
模拟 MBR → stage2 → 保护模式 → 内核 全启动链路。
通过 hook BIOS 中断(INT13读盘/INT15内存/INT10显示/INT16键盘) + 检测
16→32位模式切换 + 实时读取 VGA 文本显存(0xB8000)，把用户真机上看不到
的启动过程在本地完整复现。

用法:
    python3 tools/sim_run.py                          # 默认debug ISO
    python3 tools/sim_run.py build/hepu-os-v1.02-debug.iso
    python3 tools/sim_run.py --timeout 30              # 跑30秒
    python3 tools/sim_run.py --max-instr 20000000      # 最多执行指令数

原理:
    - MBR 读到 0x7C00 (实模式, INT13模拟从镜像读stage2)
    - stage2 读内核到 0x20000, 收集E820, 构造MBinfo, 进保护模式
    - 检测 CS 从 0 变 0x08 (ljmp进保护模式) → 保存状态 → 重建32位引擎
    - 保护模式搬移内核到 0x100000 → 跳 _start → 内核KLOG
    - VGA内存写hook实时渲染屏幕文本
"""

import sys
import struct
import time

from unicorn import *
from unicorn.x86_const import *

# ============================ 配置 ============================
DEFAULT_ISO = 'build/hepu-os-v1.02-debug.iso'
STAGE2_LBA = 1          # hybrid stage2 物理LBA
STAGE2_BLOCKS = 3       # stage2 块数
KERNEL_LBA = 84         # hybrid 内核物理扇区
SECTOR = 512
TOTAL_MEM = 0x8000000   # 128MB 内存 (覆盖内核BSS __bss_end=0x6A038BC; E820报告)
VGA_BASE = 0xB8000
VGA_SIZE = 4000         # 80x25

# VGA 字符属性 (与引导器一致: 0x1F 蓝底白字 / 0x07 灰底白字 / 0x0F 黑底白字)
ATTR_COLOR = {
    0x07: '\033[0;37m', 0x0F: '\033[0;97m', 0x1F: '\033[0;34;47m',
    0x70: '\033[0;47;30m', 0x1B: '\033[0;36m', 0x1C: '\033[0;33m',
    0x0A: '\033[0;32m', 0x0C: '\033[0;31m', 0x0E: '\033[0;33m',
    0x09: '\033[0;36m', 0x05: '\033[0;35m', 0x0B: '\033[0;34m',
}

class HePuSim:
    def __init__(self, iso_path):
        self.dbg_rk = True
        self.insn_trace = []
        self.iso = open(iso_path, 'rb').read()
        self.iso_path = iso_path
        self.mbr = self.iso[0:512]
        # stage2 在物理扇区1-3
        self.stage2 = self.iso[512:512 + 1536]
        # 内核在物理扇区84起 (hybrid布局), 读取全部可用
        self.kernel_data = self.iso[KERNEL_LBA * SECTOR:]
        self.kernel_size = len(self.kernel_data)
        self.kernel_sectors = (self.kernel_size + SECTOR - 1) // SECTOR
        self.mode16 = True
        self.uc = None
        self.data = {'int': False, 'switch': False, 'halt': False,
                     'vga_rows': {}, 'bios_out': '', 'halt_ip': 0}
        self.screen_snapshot = ''
        self.boot_drive = 0x80
        self.cur_phase = 'MBR'
        self.insn_count = 0

    # ---------------- 内存 ----------------
    def patch_mov_sreg(self, mem):
        """把内存中的 mov Sreg,r/m16 (8E /r, mod=11) 等长替换为nop.
        Unicorn 32位模式对段选择器加载必#GP(读不到我们GDT), 只能patch掉.
        等长替换不改变后续指令地址. 返回 (新内存, patch数)."""
        m = bytearray(mem)
        n = 0
        i = 0
        L = len(m)
        while i < L - 1:
            if m[i] == 0x8e and (m[i+1] & 0xC0) == 0xC0:
                m[i] = 0x90; m[i+1] = 0x90
                n += 1; i += 2
            elif (m[i] == 0x66 and i + 2 < L and m[i+1] == 0x8e
                  and (m[i+2] & 0xC0) == 0xC0):
                m[i] = 0x90; m[i+1] = 0x90; m[i+2] = 0x90
                n += 1; i += 3
            else:
                i += 1
        return bytes(m), n

    def map_memory(self, uc):
        # 低内存 0-1MB
        uc.mem_map(0, 0x100000)
        # 1MB-16MB (内核+扩展)
        uc.mem_map(0x100000, TOTAL_MEM - 0x100000)
        # VGA 显存区 0xB8000 在低内存内, 已映射

    # ---------------- VGA 渲染 ----------------
    def render_vga(self, uc):
        try:
            vga = bytes(uc.mem_read(VGA_BASE, VGA_SIZE))
        except UcError:
            return
        rows = {}
        for r in range(25):
            line = vga[r * 160:(r + 1) * 160]
            text = ''
            for i in range(0, 160, 2):
                ch = line[i]
                if ch == 0:
                    text += ' '
                else:
                    text += chr(ch)
            text = text.rstrip()
            if text.strip():
                rows[r] = text
        self.data['vga_rows'] = rows
        self.render()

    def render(self):
        rows = self.data['vga_rows']
        if not rows:
            return
        # 只在内容变化时打印
        key = tuple(sorted(rows.items()))
        if key == getattr(self, '_last_key', None):
            return
        self._last_key = key
        print('\n' + '=' * 62)
        print(f'  [VGA屏幕] 阶段={self.cur_phase}  指令={self.insn_count}')
        print('=' * 62)
        for r in range(25):
            if r in rows:
                print(f'  行{r:<2} | {rows[r]}')
        print('=' * 62)

    # ---------------- BIOS 中断 ----------------
    def handle_int(self, uc, intno):
        ax = uc.reg_read(UC_X86_REG_AX) & 0xFFFF
        eax = uc.reg_read(UC_X86_REG_EAX)
        ah = (ax >> 8) & 0xFF
        al = ax & 0xFF
        bx = uc.reg_read(UC_X86_REG_BX) & 0xFFFF
        cx = uc.reg_read(UC_X86_REG_CX) & 0xFFFF
        dx = uc.reg_read(UC_X86_REG_DX) & 0xFFFF
        si = uc.reg_read(UC_X86_REG_SI) & 0xFFFF
        di = uc.reg_read(UC_X86_REG_DI) & 0xFFFF
        ds = uc.reg_read(UC_X86_REG_DS) & 0xFFFF
        es = uc.reg_read(UC_X86_REG_ES) & 0xFFFF

        if intno == 0x13:
            self.int13(uc, ax, bx, cx, dx, si, di, ds, es)
        elif intno == 0x15:
            self.int15(uc, eax, bx, cx, dx)
        elif intno == 0x10:
            self.int10(uc, ah, al, bx)
        elif intno == 0x16:
            self.int16(uc, ah)
        else:
            print(f'  [模拟器] 未处理中断 int 0x{intno:x} AH=0x{ah:x}')

    def int13(self, uc, ax, bx, cx, dx, si, di, ds, es):
        """INT13 磁盘读. AH=0x42 EBIOS扩展读 / AH=0x02 CHS"""
        print(f'    [INT13] AH=0x{(ax>>8)&0xff:x} AL=0x{ax&0xff:x} '
              f'CX=0x{cx:04x} DX=0x{dx:04x} DS:SI={ds:04x}:{si:04x} ES:DI={es:04x}:{di:04x}')
        ah = (ax >> 8) & 0xFF
        if ah == 0x42 and getattr(self, 'dbg_rk', False):
            try:
                dapb = bytes(uc.mem_read((ds << 4) + si, 16))
                print(f'    [RK] ECX={uc.reg_read(UC_X86_REG_ECX):#010x} '
                      f'EDX={uc.reg_read(UC_X86_REG_EDX):#010x} EDI={uc.reg_read(UC_X86_REG_EDI):#010x} '
                      f'DAP={dapb.hex()}')
            except UcError as e:
                print(f'    [RK] err {e}')
        if ah == 0x42:
            # DAP at DS:SI
            dap = bytes(uc.mem_read(ds * 16 + si, 16))
            dsize = dap[0]
            blocks = dap[2] | (dap[3] << 8)
            offset = dap[4] | (dap[5] << 8)
            seg = dap[6] | (dap[7] << 8)
            lba = struct.unpack('<Q', dap[8:16])[0]
            target = seg * 16 + offset
            src = lba * SECTOR
            length = blocks * SECTOR
            if src + length <= len(self.iso):
                uc.mem_write(target, self.iso[src:src + length])
                # 返回: CF=0, AL=实际块数
                fl = uc.reg_read(UC_X86_REG_EFLAGS)
                fl &= ~1  # clear CF
                uc.reg_write(UC_X86_REG_EFLAGS, fl)
                uc.reg_write(UC_X86_REG_AX, (uc.reg_read(UC_X86_REG_AX) & 0xFF00) | blocks)
                # V2.09: Unicorn的int指令执行时会把EDX清零(模拟器伪影, 真机不会).
                # INT13 AH=42语义: EDX不被BIOS修改(bootloader后续用EDX做LBA累加).
                # 恢复EDX=LBA, 否则read_loop的addl %eax,%edx变成0+127=127 → LBA卡死.
                try:
                    uc.reg_write(UC_X86_REG_EDX, lba)
                except UcError:
                    pass
                print(f'    [INT13 AH=42] LBA={lba} 块={blocks} → {target:#x} AX={uc.reg_read(UC_X86_REG_AX):#06x} '
                      f'({"内核" if lba >= KERNEL_LBA else "stage2"})')
            else:
                fl = uc.reg_read(UC_X86_REG_EFLAGS) | 1
                uc.reg_write(UC_X86_REG_EFLAGS, fl)
                uc.reg_write(UC_X86_REG_AX, (uc.reg_read(UC_X86_REG_AX) & 0xFF00))
                print(f'    [INT13] 越界: LBA={lba} len={length} iso={len(self.iso)}')
        elif ah == 0x02:
            # CHS读: CH=cyl DH=head CL=sector, AL=blocks, ES:BX=buf
            blocks = ax & 0xFF
            buf = es * 16 + bx
            # 简化: 按LBA等价 (CHS转LBA省略, 用LBA=BLK直接映射不准确)
            # bootloader只用AH=42, 这里兜底
            length = blocks * SECTOR
            if length <= len(self.iso):
                uc.mem_write(buf, self.iso[0:length])
                fl = uc.reg_read(UC_X86_REG_EFLAGS) & ~1
                uc.reg_write(UC_X86_REG_EFLAGS, fl)
        else:
            print(f'    [INT13] 未实现 AH=0x{ah:x}')

    def int15(self, uc, eax, bx, cx, dx):
        """INT15: E820内存映射 / A20 / 扩展内存"""
        ax = eax & 0xFFFF
        if eax == 0xE820:
            # E820: ES:DI 缓冲, ECX=24, EDX='SMAP', EBX=续
            edi = uc.reg_read(UC_X86_REG_DI) & 0xFFFF
            es = uc.reg_read(UC_X86_REG_ES) & 0xFFFF
            ecx = uc.reg_read(UC_X86_REG_ECX) & 0xFFFFFFFF
            ebx = uc.reg_read(UC_X86_REG_EBX) & 0xFFFFFFFF
            edx = uc.reg_read(UC_X86_REG_EDX) & 0xFFFFFFFF
            # 内存区域表
            regions = [
                (0x00000000, 0x0009FC00, 1),   # 0-639KB 可用
                (0x0009FC00, 0x00000400, 2),   # 640KB-1KB 保留
                (0x000F0000, 0x00010000, 2),   # ROM保留
                (0x00100000, TOTAL_MEM - 0x100000, 1),  # 1MB-16MB 可用
            ]
            idx = ebx
            if idx < len(regions):
                base, length, typ = regions[idx]
                buf = es * 16 + edi
                uc.mem_write(buf, struct.pack('<Q', base))
                uc.mem_write(buf + 8, struct.pack('<Q', length))
                uc.mem_write(buf + 16, struct.pack('<I', typ))
                uc.mem_write(buf + 20, struct.pack('<I', 0))
                uc.reg_write(UC_X86_REG_EAX, 0x534D4150)  # 'SMAP'
                uc.reg_write(UC_X86_REG_EBX, idx + 1 if idx + 1 < len(regions) else 0)
                fl = uc.reg_read(UC_X86_REG_EFLAGS) & ~1
                uc.reg_write(UC_X86_REG_EFLAGS, fl)
            else:
                fl = uc.reg_read(UC_X86_REG_EFLAGS) & ~1
                uc.reg_write(UC_X86_REG_EFLAGS, fl)
                uc.reg_write(UC_X86_REG_EBX, 0)
        elif ax == 0x2401:
            # Enable A20
            fl = uc.reg_read(UC_X86_REG_EFLAGS) & ~1
            uc.reg_write(UC_X86_REG_EFLAGS, fl)
        elif eax == 0xE801:
            # 扩展内存: AX=BX=大小(KB), CX=DX=连续大小(KB)
            ext_kb = (TOTAL_MEM - 0x100000) // 1024
            uc.reg_write(UC_X86_REG_AX, ext_kb & 0xFFFF)
            uc.reg_write(UC_X86_REG_BX, ext_kb & 0xFFFF)
            uc.reg_write(UC_X86_REG_CX, ext_kb & 0xFFFF)
            uc.reg_write(UC_X86_REG_DX, ext_kb & 0xFFFF)
            fl = uc.reg_read(UC_X86_REG_EFLAGS) & ~1
            uc.reg_write(UC_X86_REG_EFLAGS, fl)
        elif (ax >> 8) == 0x88:
            # AH=0x88: 扩展内存大小(KB)
            ext_kb = (TOTAL_MEM - 0x100000) // 1024
            uc.reg_write(UC_X86_REG_AX, ext_kb & 0xFFFF)
            fl = uc.reg_read(UC_X86_REG_EFLAGS) & ~1
            uc.reg_write(UC_X86_REG_EFLAGS, fl)
        else:
            print(f'    [INT15] 未实现 AX=0x{ax:x}')

    def int10(self, uc, ah, al, bx):
        if ah == 0x0E:
            # TTY输出 (print_dec32用)
            self.data['bios_out'] += chr(al)
            if al in (10, 13):
                self.data['bios_out'] += '\n'
        elif ah == 0x00:
            pass  # 设置模式
        elif ah == 0x03:
            pass
        else:
            pass

    def int16(self, uc, ah):
        if ah == 0x00:
            # 读按键: 返回空格(0x20), AX=0x1C20 (无shift)
            uc.reg_write(UC_X86_REG_AX, 0x1C20)
        elif ah == 0x01:
            # 检查按键: ZF=0, AX=0x1C20
            uc.reg_write(UC_X86_REG_AX, 0x1C20)
            fl = uc.reg_read(UC_X86_REG_EFLAGS)
            fl &= ~(1 << 6)  # clear ZF
            uc.reg_write(UC_X86_REG_EFLAGS, fl)
        else:
            print(f'    [INT16] 未实现 AH=0x{ah:x}')

    # ---------------- 中断/代码/MEM hook ----------------
    def on_int(self, uc, intno, user_data):
        if getattr(self, 'dbg_pm', False) or True:
            try:
                eip = uc.reg_read(UC_X86_REG_IP if self.mode16 else UC_X86_REG_EIP)
                print(f'  [INTR] int 0x{intno:x} EIP={eip:#x} '
                      f'bytes={bytes(uc.mem_read(eip,4)).hex()} '
                      f'EAX={uc.reg_read(UC_X86_REG_EAX):#x} CS={uc.reg_read(UC_X86_REG_CS):#x}')
            except UcError as ex:
                print(f'  [INTR] int 0x{intno:x} err {ex}')
        self.handle_int(uc, intno)
        # 注意: Unicorn的UC_HOOK_INTR触发时, IP已指向int指令下一条(如jc等).
        # 不能再+2, 否则会跳过int后的分支指令(实测E820的jc/jne被跳过→流程错乱).
        self.data['int'] = True
        uc.emu_stop()

    def on_code(self, uc, address, size, user_data):
        self.insn_count += 1
        # V2.09: 最近指令环形缓冲(跳错定位).
        # 注意: 每条指令mem_read会干扰Unicorn批处理执行(导致寄存器破坏),
        # 只记录地址, 跳错时再从内存读字节.
        if getattr(self, 'insn_trace', None) is not None:
            self.insn_trace.append((address, b''))
            if len(self.insn_trace) > 60:
                self.insn_trace.pop(0)
        # 32位模式: 拦截 movw %r16,%sreg (66 8e /r) —— Unicorn校验段选择器
        # 但读不到我们的GDT(0x8E00) → 误触发#GP. 手动设置段寄存器并跳过指令.
        # 调试: 跟踪read_kernel/关键调用 (16位阶段也打印)
        if not self.mode16:
            # ---- 指令拦截(仅早期, 之后零mem_read, 避免干扰Unicorn批处理) ----
            # far ljmp: 引导器跳内核前启用, 检测到后禁用(引导器只跳一次)
            if not getattr(self, 'far_ljmp_done', False):
                try:
                    b3 = bytes(uc.mem_read(address, 3))
                except UcError:
                    b3 = b''
                if b3 and b3[0] == 0xea:
                    try:
                        b7 = bytes(uc.mem_read(address, 7))
                        if len(b7) == 7 and b7[0] == 0xea:
                            off = b7[1] | (b7[2] << 8) | (b7[3] << 16) | (b7[4] << 24)
                            seg = b7[5] | (b7[6] << 8)
                            if getattr(self, 'dbg_pm', False):
                                print(f'  [LJMP] far jump seg={seg:#x} off={off:#x}')
                            try:
                                uc.reg_write(UC_X86_REG_CS, seg)
                            except UcError:
                                pass
                            uc.reg_write(UC_X86_REG_EIP, off)
                            self.data['skip'] = True
                            self.far_ljmp_done = True
                            print(f'  [LJMPOK] far ljmp 检测到 seg={seg:#x} off={off:#x} @EIP={address:#x}')
                            uc.emu_stop()
                            return
                    except UcError:
                        pass
            # pusha/popa/rep-stosl/mov-to-segment: 仅内核入口区(<0x102000)检测,
            # 这些指令集中在内核初始化早期, 之后内核代码经大量验证无这些指令或无需拦截
            if address < 0x102000:
                try:
                    b3 = bytes(uc.mem_read(address, 3))
                except UcError:
                    b3 = b''
                # pusha(0x60): Unicorn把ESP左移8位而非-32, 手动模拟
                if b3 and b3[0] == 0x60:
                    try:
                        esp = uc.reg_read(UC_X86_REG_ESP)
                        if getattr(self, 'dbg_pusha', False):
                            print(f'  [PUSHA] EIP={address:#x} 前ESP={esp:#x}')
                        regs = {0:UC_X86_REG_EAX,1:UC_X86_REG_ECX,2:UC_X86_REG_EDX,
                                3:UC_X86_REG_EBX,5:UC_X86_REG_EBP,6:UC_X86_REG_ESI,
                                7:UC_X86_REG_EDI}
                        nesp = esp - 32
                        vals = []
                        for i in range(8):
                            if i == 4:
                                vals.append(esp)
                            else:
                                vals.append(uc.reg_read(regs[i]))
                        uc.mem_write(nesp, b''.join(struct.pack('<I', v) for v in vals))
                        uc.reg_write(UC_X86_REG_ESP, nesp)
                        uc.reg_write(UC_X86_REG_EIP, address + 1)
                        self.data['skip'] = True
                        uc.emu_stop()
                        return
                    except UcError:
                        pass
                # popa(0x61)
                if b3 and b3[0] == 0x61:
                    try:
                        esp = uc.reg_read(UC_X86_REG_ESP)
                        if getattr(self, 'dbg_pusha', False):
                            print(f'  [POPA] EIP={address:#x} 前ESP={esp:#x}')
                        vals = struct.unpack('<8I', bytes(uc.mem_read(esp, 32)))
                        regs = {0:UC_X86_REG_EAX,1:UC_X86_REG_ECX,2:UC_X86_REG_EDX,
                                3:UC_X86_REG_EBX,5:UC_X86_REG_EBP,6:UC_X86_REG_ESI,
                                7:UC_X86_REG_EDI}
                        for i in range(8):
                            if i == 4:
                                continue
                            uc.reg_write(regs[i], vals[i])
                        uc.reg_write(UC_X86_REG_ESP, esp + 32)
                        uc.reg_write(UC_X86_REG_EIP, address + 1)
                        self.data['skip'] = True
                        uc.emu_stop()
                        return
                    except UcError:
                        pass
                # rep stosl (F3 AB) BSS清零: ECX巨大时整块清零加速
                if b3 and b3[0] == 0xf3 and len(b3) >= 2 and b3[1] == 0xab:
                    try:
                        ecx = uc.reg_read(UC_X86_REG_ECX)
                        if ecx > 100000:
                            edi = uc.reg_read(UC_X86_REG_EDI)
                            length = ecx * 4
                            chunk = 0x100000
                            pos = edi
                            remaining = length
                            while remaining > 0:
                                c = min(chunk, remaining)
                                uc.mem_write(pos, bytes(bytearray(c)))
                                pos += c
                                remaining -= c
                            uc.reg_write(UC_X86_REG_ECX, 0)
                            uc.reg_write(UC_X86_REG_EIP, address + 2)
                            self.data['skip'] = True
                            uc.emu_stop()
                            return
                    except UcError:
                        pass
                # mov-to-segment (8E /r, 66 8E /r): Unicorn校验段选择器失败→#GP
                is_8e = False
                mlen = 0
                if b3 and b3[0] == 0x8e and (b3[1] & 0xC0) == 0xC0:
                    is_8e, mlen = True, 2
                elif len(b3) >= 3 and b3[0] == 0x66 and b3[1] == 0x8e and (b3[2] & 0xC0) == 0xC0:
                    is_8e, mlen = True, 3
                if is_8e:
                    try:
                        modrm = b3[mlen - 1]
                        segmap = {0:UC_X86_REG_ES,1:UC_X86_REG_CS,2:UC_X86_REG_SS,3:UC_X86_REG_DS,
                                  4:UC_X86_REG_FS,5:UC_X86_REG_GS}
                        sreg = (modrm >> 3) & 0x07
                        srm = modrm & 0x07
                        rmap = {0:UC_X86_REG_EAX,1:UC_X86_REG_ECX,2:UC_X86_REG_EDX,3:UC_X86_REG_EBX,
                                4:UC_X86_REG_ESP,5:UC_X86_REG_EBP,6:UC_X86_REG_ESI,7:UC_X86_REG_EDI}
                        val = uc.reg_read(rmap[srm]) & 0xFFFF
                        if sreg in segmap:
                            try:
                                uc.reg_write(segmap[sreg], val)
                            except UcError:
                                pass
                        uc.reg_write(UC_X86_REG_EIP, address + mlen)
                        self.data['skip'] = True
                        uc.emu_stop()
                        return
                    except (UcError, IndexError):
                        pass
            # 跳过内核early_console_init清屏循环 (0x10de18: 0xB8000→0xB8FA0逐字符)
            # 模拟器render_vga每次VGA写触发解析太慢; 直接一次性清VGA并跳到循环后.
            if address == 0x10de18:
                try:
                    axv = uc.reg_read(UC_X86_REG_AX) & 0xFFFF
                    if axv == 0:
                        axv = 0x0720
                    pair = bytes([axv & 0xFF, (axv >> 8) & 0xFF])
                    uc.mem_write(0xB8000, pair * 2000)
                    uc.reg_write(UC_X86_REG_ECX, 0xB8FA0)
                    uc.reg_write(UC_X86_REG_EIP, 0x10de26)
                    self.data['skip'] = True
                    uc.emu_stop()
                    return
                except UcError:
                    pass
        # ===== 临时调试: 打印32位执行 ====
        if not self.mode16 and getattr(self, 'dbg_pm', False) and self.insn_count < 400:
            try:
                b = bytes(uc.mem_read(address, size))
                print(f'  [PM TRACE] ip={address:#x} size={size} bytes={b.hex()}')
            except UcError:
                pass

        # trace: 前300条指令
        if getattr(self, 'trace_mode', False) and self.insn_count < 300:
            ip = uc.reg_read(UC_X86_REG_IP)
            try:
                b = bytes(uc.mem_read(ip, 4))
            except UcError:
                b = b''
            print(f'      TRACE[{self.insn_count}] ip={ip:#06x} cs={uc.reg_read(UC_X86_REG_CS)&0xffff:#x} '
                  f'ax={uc.reg_read(UC_X86_REG_AX)&0xffff:#06x} '
                  f'di={uc.reg_read(UC_X86_REG_DI)&0xffff:#06x} bytes={b.hex()}')
        # 以下per-instruction检测仅在实模式或内核入口区(<0x102000)执行,
        # 避免每条指令mem_read/reg_read干扰Unicorn批处理(曾导致vga循环寄存器破坏)
        if self.mode16 or address < 0x102000:
            # 检测进入保护模式: CS变0x08
            cs = uc.reg_read(UC_X86_REG_CS) & 0xFFFF
            if cs == 0x08 and self.mode16:
                self.data['switch'] = True
                uc.emu_stop()
            # 检测hlt
            if size == 1 and uc.mem_read(address, 1) == b'\xf4':
                self.data['halt'] = True
                self.data['halt_ip'] = address
                uc.emu_stop()
            # 检测大延时循环:
            #   (a) loop指令(0xE2)   (b) dec ecx; jnz 向后跳(0x75, 负偏移)
            b0 = bytes(uc.mem_read(address, 1)) if size >= 1 else b''
            if size >= 2:
                b2 = bytes(uc.mem_read(address, 2))
                if b2[0] == 0x75:   # jnz rel8
                    rel = struct.unpack('b', bytes([b2[1]]))[0]
                    if rel < 0:     # 向后跳 = 自旋/延时
                        ecx = uc.reg_read(UC_X86_REG_ECX) & 0xFFFFFFFF
                        if ecx > 0x1000:
                            uc.reg_write(UC_X86_REG_ECX, 1)
            if b0 == b'\xe2':   # loop
                ecx = uc.reg_read(UC_X86_REG_ECX) & 0xFFFFFFFF
                if ecx > 0x10000:
                    uc.reg_write(UC_X86_REG_ECX, 1)

    def on_mem_write(self, uc, access, address, size, value, user_data):
        if VGA_BASE <= address < VGA_BASE + VGA_SIZE:
            self.render_vga(uc)

    # ---------------- 运行 ----------------
    def run(self, max_instr=100_000_000, timeout=120):
        # ---- 阶段1: 16位实模式 ----
        print(f'[模拟器] 加载镜像: {self.iso_path} ({len(self.iso)}字节)')
        print(f'[模拟器] 内核数据: {self.kernel_size}字节 ({self.kernel_sectors}块)')
        print(f'[模拟器] 阶段1: MBR@0x7C00 实模式 ...')
        self.cur_phase = 'MBR'
        uc = Uc(UC_ARCH_X86, UC_MODE_16)
        self.map_memory(uc)
        uc.mem_write(0x7C00, self.mbr)
        uc.mem_write(0x7C00 + 510, b'\x55\xaa')
        uc.reg_write(UC_X86_REG_DL, self.boot_drive)
        uc.reg_write(UC_X86_REG_IP, 0x7C00)
        uc.hook_add(UC_HOOK_INTR, self.on_int)
        uc.hook_add(UC_HOOK_CODE, self.on_code)
        uc.hook_add(UC_HOOK_MEM_WRITE, self.on_mem_write)
        self.trace_mode = True   # 打开trace
        self.uc = uc

        self.run_loop(uc, 0x7C00, max_instr, timeout, phase='MBR/stage2')

        if self.data['switch']:
            # ---- 阶段2: 32位保护模式 ----
            print(f'\n[模拟器] 检测到进入保护模式(CS=0x08), 重建32位引擎 ...')
            self.cur_phase = 'PROTECTED'
            # 保存全部寄存器
            regs = {}
            for r in (UC_X86_REG_EAX, UC_X86_REG_EBX, UC_X86_REG_ECX,
                      UC_X86_REG_EDX, UC_X86_REG_ESI, UC_X86_REG_EDI,
                      UC_X86_REG_EBP, UC_X86_REG_ESP, UC_X86_REG_EIP,
                      UC_X86_REG_EFLAGS, UC_X86_REG_CS, UC_X86_REG_DS,
                      UC_X86_REG_ES, UC_X86_REG_SS, UC_X86_REG_FS,
                      UC_X86_REG_GS, UC_X86_REG_CR0):
                regs[r] = uc.reg_read(r)
            # 保存内存
            mem = bytes(uc.mem_read(0, TOTAL_MEM))
            self.mode16 = False

            uc2 = Uc(UC_ARCH_X86, UC_MODE_32)
            self.map_memory(uc2)
            # V2废弃: patch_mov_sreg盲扫所有0x8e X(X&0xC0==0xC0), 误把call/jmp
            # 指令的偏移字节(如0x8e fd)patch成NOP→call跳错. mov-to-segment由
            # on_code指令边界精确的8e检测处理(<0x102000, 见on_code). 直接写内存.
            uc2.mem_write(0, mem)
            for r, v in regs.items():
                try:
                    uc2.reg_write(r, v)
                except UcError:
                    pass
            # V3: 设置GDTR指向引导器GDT(0x8E00) + 段选择子.
            # Unicorn对栈操作(push/call/ret)用SS段基址, 若SS=0(无GDT)则
            # 段基址异常导致ESP偏移+0x10000, get_pc_thunk读返回地址错乱.
            # 设置GDTR+flat段后, SS基址=0, 栈操作与物理地址一致.
            try:
                uc2.reg_write(UC_X86_REG_GDTR, (0, 0x8E00, 0x17, 0))
            except UcError as e:
                print(f'  [WARN] GDTR设置失败: {e}')
            for r, sel in [(UC_X86_REG_CS, 0x08), (UC_X86_REG_SS, 0x10),
                           (UC_X86_REG_DS, 0x10), (UC_X86_REG_ES, 0x10),
                           (UC_X86_REG_FS, 0x10), (UC_X86_REG_GS, 0x10)]:
                try:
                    uc2.reg_write(r, sel)
                except UcError:
                    pass
            try:
                print(f'  [SEGRBK] 设置后读回 CS={uc2.reg_read(UC_X86_REG_CS)&0xffff:#x} '
                      f'DS={uc2.reg_read(UC_X86_REG_DS)&0xffff:#x} '
                      f'SS={uc2.reg_read(UC_X86_REG_SS)&0xffff:#x} '
                      f'GDTR={uc2.reg_read(UC_X86_REG_GDTR)}')
            except UcError as e:
                print(f'  [SEGRBK] 读回失败: {e}')
            print(f'  [模拟器] mov-to-segment由on_code 8e检测处理, 段寄存器预置0x10')
            uc2.hook_add(UC_HOOK_INTR, self.on_int)
            uc2.hook_add(UC_HOOK_CODE, self.on_code)
            uc2.hook_add(UC_HOOK_MEM_WRITE, self.on_mem_write)
            self.uc = uc2
            eip = regs[UC_X86_REG_EIP]
            print(f'  [模拟器] 重建32位引擎, EIP={eip:#x} ESP={regs[UC_X86_REG_ESP]:#x} '
                  f'CS={regs[UC_X86_REG_CS]&0xffff:#x} DS={regs[UC_X86_REG_DS]&0xffff:#x}')
            self.trace_mode = True
            self.dbg_pm = True
            self.dbg_rk = True
            self.run_loop(uc2, eip, max_instr, timeout, phase='KERNEL')

        # ---- 最终VGA快照 ----
        # dump stage2加载区0x7C00 (对比镜像验证加载正确性)
        try:
            s2m = bytes(self.uc.mem_read(0x7C00, 32))
            print(f'[dump] 0x7C00+0x00: {s2m.hex()}')
            print(f'[dump] 0x7C00+0x5A: {bytes(self.uc.mem_read(0x7C5A, 16)).hex()}')
        except UcError:
            pass
        # dump VGA缓冲和最终EIP
        try:
            vga = bytes(self.uc.mem_read(0xB8000, 160))
            print('[dump] VGA行0-4 raw:', vga.hex())
            eipf = self.uc.reg_read(UC_X86_REG_EIP)
            print(f'[dump] 最终EIP={eipf:#x} CS={self.uc.reg_read(UC_X86_REG_CS):#x}')
        except UcError:
            pass
        # dump 关键内存: 内核加载区0x20000 / 0x100000
        try:
            k2 = bytes(self.uc.mem_read(0x20000, 16))
            k1 = bytes(self.uc.mem_read(0x100000, 16))
            print(f'[dump] 0x20000(读盘区): {k2.hex()}')
            print(f'[dump] 0x100000(搬移区): {k1.hex()}')
            import struct
            if len(k1) >= 4:
                m = struct.unpack('<I', k1[:4])[0]
                print(f'[dump] 0x100000魔数: 0x{m:08x} (期望0x1BADB002)')
        except UcError as e:
            print(f'[dump] 内存读取失败: {e}')
        self.render_vga(self.uc)
        print('\n' + '#' * 62)
        print('# 模拟结束')
        print(f'# 阶段: {self.cur_phase}  指令数: {self.insn_count}')
        print(f'# halt: {self.data["halt"]} @ {self.data["halt_ip"]:#x}')
        if self.data['halt']:
            print('# 系统已 hlt (停机)')
        print('#' * 62)

    def run_loop(self, uc, start, max_instr, timeout, phase):
        """带int/切换/超时处理的运行循环"""
        self.cur_phase = phase
        t0 = time.time()
        base_count = self.insn_count
        try:
            while True:
                remaining = max_instr - (self.insn_count - base_count)
                if remaining <= 0:
                    print(f'  [模拟器] 达到指令上限')
                    break
                try:
                    uc.emu_start(start, 0, count=0, timeout=0)
                except UcError as e:
                    print(f'  [模拟器] CPU异常: {e}  far_ljmp_done={getattr(self,"far_ljmp_done",False)} '
                          f'ljmp_try={getattr(self,"ljmp_try",0)}')
                    # 打印EIP和寄存器
                    try:
                        eip = uc.reg_read(UC_X86_REG_EIP)
                        print(f'     EIP={eip:#x} 指令={bytes(uc.mem_read(eip, 8)).hex()}')
                    except UcError:
                        pass
                    # 打印跳错前最近指令
                    tr = getattr(self, 'insn_trace', None)
                    print(f'     insn_trace长度={len(tr) if tr else 0}')
                    if tr:
                        print('     跳错前指令:')
                        for (a, b) in tr[-20:]:
                            try:
                                b = bytes(uc.mem_read(a, 6)).hex()
                            except UcError:
                                b = '??'
                            print(f'       {a:#x}: {b}')
                    # V2.09: 取指异常时打印CS/ESP/栈顶, 定位跳错来源
                    try:
                        print(f'     CS={uc.reg_read(UC_X86_REG_CS):#x} ESP={uc.reg_read(UC_X86_REG_ESP):#x} '
                              f'EAX={uc.reg_read(UC_X86_REG_EAX):#x} EBX={uc.reg_read(UC_X86_REG_EBX):#x} '
                              f'ECX={uc.reg_read(UC_X86_REG_ECX):#x} EDX={uc.reg_read(UC_X86_REG_EDX):#x}')
                        esp = uc.reg_read(UC_X86_REG_ESP)
                        # 栈回溯: 找代码段(0x100000-0x170000)的返回地址
                        try:
                            callchain = []
                            for off in range(0, 0x100, 4):
                                addr = esp + off
                                v = struct.unpack('<I', bytes(uc.mem_read(addr, 4)))[0]
                                if 0x100000 <= v <= 0x170000:
                                    callchain.append(f'{addr:#x}->{v:#x}')
                            print(f'     栈回溯: {callchain[:10]}')
                        except UcError:
                            pass
                        stk = bytes(uc.mem_read(esp & 0xFFFFF000, 32))
                        print(f'     栈顶: {stk.hex()}')
                    except UcError:
                        pass
                    break
                if self.data['int']:
                    self.data['int'] = False
                    start = uc.reg_read(UC_X86_REG_IP)
                    continue
                if self.data.get('skip'):
                    self.data['skip'] = False
                    start = uc.reg_read(UC_X86_REG_EIP)
                    if getattr(self, 'dbg_pm', False):
                        print(f'  [SKIP] 继续 EIP={start:#x} bytes={bytes(uc.mem_read(start,4)).hex()}')
                    continue
                if self.data['switch']:
                    return
                if self.data['halt']:
                    return
                # emu_start正常返回(无hook stop)? 理论上不该到这
                if time.time() - t0 > timeout:
                    print(f'  [模拟器] 超时 {timeout}s, 当前EIP={uc.reg_read(UC_X86_REG_EIP):#x}')
                    try:
                        eip = uc.reg_read(UC_X86_REG_EIP)
                        print(f'     EIP={eip:#x} 指令={bytes(uc.mem_read(eip, 8)).hex()} '
                              f'CS={uc.reg_read(UC_X86_REG_CS):#x}')
                    except UcError:
                        pass
                    break
                print(f'  [模拟器] emu_start返回(无hook触发) IP={uc.reg_read(UC_X86_REG_IP):#x} '
                      f'CS={uc.reg_read(UC_X86_REG_CS)&0xffff:#x}')
                try:
                    ip = uc.reg_read(UC_X86_REG_IP)
                    print(f'     指令={bytes(uc.mem_read(ip, 8)).hex()}')
                except UcError:
                    pass
                break
        except KeyboardInterrupt:
            print('\n  [模拟器] 用户中断')


def main():
    args = sys.argv[1:]
    iso = DEFAULT_ISO
    timeout = 120
    if args and not args[0].startswith('--'):
        iso = args[0]
    if '--timeout' in args:
        i = args.index('--timeout')
        timeout = int(args[i + 1])
    sim = HePuSim(iso)
    sim.run(timeout=timeout)

if __name__ == '__main__':
    main()
