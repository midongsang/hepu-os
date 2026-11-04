#!/usr/bin/env python3
"""Stage2 页表 vs 内核 BSS 安全检查（4 条硬规则，失败即阻断）
===================================================================
背景：Stage2 早期页表由 Stage2 代码手工构建，CR3 装载后 CPU 依赖它翻译
物理地址。如果 BSS zeroing 覆盖了页表自身内存，会触发 #PF → #DF → Triple
Fault → CPU Reset —— 串口只剩 16 行日志，很难定位。

规则：
1. __bss_end 必须落在 Stage2 低地址页表映射范围内（PD_entry * 2MB）
2. Stage2 PT_BASE（页表存放物理地址）必须 > __bss_end
   —— 即 BSS zeroing 的 rep stosq 清不到页表
3. kheap_memory 结束地址必须落在 Stage2 页表映射范围内
4. 若未来需要扩大 kheap，必须同步提升 PD entry 数量和 PT_BASE

运行：
  python3 tools/check_stage2_bss.py
  python3 tools/check_stage2_bss.py build_x86_64/hepu-kernel64.elf

退出码: 0=全部通过，1=有硬错误
"""
import os, re, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# ====== 硬编码的 Stage2 页表配置（唯一事实源，必须与 stage2_64.S 同步）======
# 任何扩大 kheap / 扩大 BSS 的改动，必须同步修改这里
PT_BASE        = 0x2E00000   # 页表基址 (物理)
PD_ENTRIES     = 24          # PD 低地址 2MB 大页数 = 24 → 48MB 覆盖
PT_COVERAGE    = PD_ENTRIES * 2 * 1024 * 1024   # 0x3000000
KERNEL_ENTRY   = 0x100000    # 内核加载地址

# ====== 检查入口 ======
errors = []
warns  = []

# 定位内核 ELF
if len(sys.argv) >= 2:
    kernel_elf = sys.argv[1]
else:
    kernel_elf = os.path.join(ROOT, 'build_x86_64', 'hepu-kernel64.elf')

if not os.path.exists(kernel_elf):
    print(f'[SKIP] 未找到内核 ELF: {kernel_elf}')
    print('       先编译内核: make -C kernel/arch/x86_64')
    sys.exit(0)

# 用 nm -S 解析符号（GNU nm 2.42+ 输出 4 列: addr size type name）
# 注意: -SN 组合会因为 N(外部调试格式)而失败，必须单独用 -S
try:
    nm_out = subprocess.check_output(['nm', '-S', kernel_elf],
                                    text=True, stderr=subprocess.STDOUT)
except subprocess.CalledProcessError as e:
    print(f'[ERROR] nm -S 失败: {e}')
    sys.exit(1)

# name -> {'addr': int, 'size': int|None, 'type': str}
syms = {}
for line in nm_out.splitlines():
    parts = line.split()
    if len(parts) < 3:
        continue
    try:
        addr = int(parts[0], 16)
    except ValueError:
        continue
    name = parts[-1]
    entry = {'addr': addr, 'size': None, 'type': parts[-2] if len(parts) >= 4 else None}
    # nm -S 下 parts[1] = size(hex, 无 0x)，parts[2] = type，parts[3] = name
    if len(parts) >= 4:
        try:
            entry['size'] = int(parts[1], 16)
        except ValueError:
            pass
    syms[name] = entry

def sym_addr(name):
    return syms.get(name, {}).get('addr')

def sym_size(name):
    return syms.get(name, {}).get('size')

bss_start = sym_addr('__bss_start')
bss_end   = sym_addr('__bss_end')
kheap_mem = sym_addr('kheap_memory')
kheap_size = sym_size('kheap_memory')

# ---- fallback: readelf -sW 补 kheap_memory 大小 ----
if kheap_size is None:
    try:
        re_out = subprocess.check_output(
            ['readelf', '-sW', kernel_elf], text=True, stderr=subprocess.STDOUT)
        for line in re_out.splitlines():
            if 'kheap_memory' in line:
                parts = line.split()
                if len(parts) >= 8:
                    try:
                        kheap_size = int(parts[2], 0)  # auto hex/dec
                    except ValueError:
                        pass
                break
    except subprocess.CalledProcessError:
        pass

def hexm(v):
    """格式化: 0x0123ABCD (dec)MB"""
    if v is None: return 'N/A'
    return f'0x{v:08x} ({v/1024/1024:.1f}MB)'

print(f'=== Stage2 PT vs Kernel BSS ===')
print(f'内核 ELF:    {kernel_elf}')
print(f'')
print(f'--- Stage2 页表配置 ---')
print(f'PT_BASE:     {hexm(PT_BASE)}')
print(f'PD entries:  {PD_ENTRIES} × 2MB = 覆盖 0x0 - 0x{PT_COVERAGE:06x} ({PT_COVERAGE/1024/1024:.0f}MB)')
print(f'')
print(f'--- 内核 BSS ---')
print(f'__bss_start: {hexm(bss_start)}')
print(f'__bss_end:   {hexm(bss_end)}')
if bss_start and bss_end:
    print(f'BSS size:    {(bss_end-bss_start)/1024/1024:.1f}MB')
print(f'')
print(f'--- kheap ---')
print(f'kheap_memory base: {hexm(kheap_mem)}')
if kheap_size is not None:
    print(f'kheap size:        {kheap_size/1024/1024:.1f}MB')
    kheap_end = (kheap_mem or 0) + kheap_size
    print(f'kheap end:         0x{kheap_end:08x} ({kheap_end/1024/1024:.1f}MB)')
else:
    kheap_end = None
    if kheap_mem is not None:
        print('kheap size:    (readelf 未能解析，跳过规则 3)')
print('')

# ====== 规则检查 ======
# 规则 1: __bss_end < PT_COVERAGE
if bss_end is None:
    errors.append('规则 1: 未找到 __bss_end 符号 (内核可能未编译)')
elif bss_end >= PT_COVERAGE:
    errors.append(f'规则 1 FAIL: __bss_end {hexm(bss_end)} >= PT 覆盖上限 0x{PT_COVERAGE:06x} ({PT_COVERAGE/1024/1024:.0f}MB)')
else:
    print(f'[{chr(0x2714)}] 规则 1 PASS: __bss_end {hexm(bss_end)} < PT 覆盖 0x{PT_COVERAGE:06x}')

# 规则 2: PT_BASE >= __bss_end
if bss_end is None:
    errors.append('规则 2: 未找到 __bss_end 符号 (内核可能未编译)')
elif PT_BASE < bss_end:
    errors.append(f'规则 2 FAIL: PT_BASE 0x{PT_BASE:06x} 在 BSS 范围内 (BSS end = 0x{bss_end:06x}) —— BSS zeroing 会覆盖页表！')
else:
    print(f'[{chr(0x2714)}] 规则 2 PASS: PT_BASE 0x{PT_BASE:06x} >= BSS end {hexm(bss_end)}')

# 规则 3: kheap_end < PT_COVERAGE
if kheap_end is not None:
    if kheap_end >= PT_COVERAGE:
        errors.append(f'规则 3 FAIL: kheap_end 0x{kheap_end:08x} >= PT 覆盖上限 0x{PT_COVERAGE:06x}')
    else:
        print(f'[{chr(0x2714)}] 规则 3 PASS: kheap_end 0x{kheap_end:08x} < PT 覆盖 0x{PT_COVERAGE:06x}')

# 规则 4: PT_BASE 必须在自身映射范围内 (自引用检查)
if PT_BASE + 0x8000 > PT_COVERAGE:  # PT 约占 32KB = 8 个页
    errors.append(f'规则 4 FAIL: PT_BASE 0x{PT_BASE:06x} + 32KB = 0x{PT_BASE+0x8000:06x} 超出自身映射范围 0x{PT_COVERAGE:06x}')
else:
    print(f'[{chr(0x2714)}] 规则 4 PASS: PT 自身 (0x{PT_BASE:06x} - 0x{PT_BASE+0x8000:06x}) 落在映射范围内')

# ====== 汇总 ======
print('')
if errors:
    print(f'[FAIL] 共 {len(errors)} 条硬错误:')
    for e in errors:
        print(f'  [x] {e}')
    print('')
    print('修复参考:')
    print(f'  - 若 BSS/heap 超出 PT 覆盖: 改 stage2_64.S 中 movl $24, %ecx 为更大值')
    print(f'  - 若 PT_BASE 落入 BSS:      改 .equ PT_BASE, 0x2E00000 为更高地址')
    print(f'  - 改完 Stage2 后, 同步更新本脚本顶部的 PT_BASE / PD_ENTRIES')
    sys.exit(1)
else:
    print(f'[PASS] 全部安全检查通过 ({4 - (1 if kheap_end is None else 0)} 条规则)')
    sys.exit(0)
