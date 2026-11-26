#!/usr/bin/env python3
"""Stage2 页表 vs 内核 BSS 安全检查（6 条硬规则，失败即阻断）
===================================================================
背景：Stage2 早期页表由 Stage2 代码手工构建，CR3 装载后 CPU 依赖它翻译
物理地址。如果 BSS zeroing 覆盖了页表自身内存，会触发 #PF → #DF → Triple
Fault → CPU Reset —— 串口只剩 16 行日志，很难定位。

OPEN-09 补充：UEFI 入口曾长期使用引导器留下的低 4KB 栈（0x8F000-0x90000），
而 jump_kernel_64 的临时 GDT 在 0x8F050，正好落在栈内；栈帧一深就覆盖
GDT[1]，首个中断按 0x08 取门描述符时 #GP → #DF → triple fault。因此除
Stage2 页表外，还必须静态断言早期入口的栈边界。

规则：
1. __bss_end 必须落在 Stage2 低地址页表映射范围内（PD_entry * 2MB）
2. Stage2 PT_BASE（页表存放物理地址）必须 > __bss_end
   —— 即 BSS zeroing 的 rep stosq 清不到页表
3. kheap_memory 结束地址必须落在 Stage2 页表映射范围内
4. 若未来需要扩大 kheap，必须同步提升 PD entry 数量和 PT_BASE
5. OPEN-09: hepu_kstack 不得与引导器低 4KB 栈/GDT（0x8F000-0x90000）重叠，
   且必须落在 BSS 内
6. OPEN-09: entry_64.S 首条指令必须是引导器魔数 movl $0x2BADB002,%edi；
   BSS 清零之后才切 hepu_kstack_top，切栈前不得有 C 调用

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

# ====== OPEN-09: 早期入口栈边界（引导器低 4KB 栈 vs 内核 64KB 栈）======
# jump_kernel_64.S 的临时 GDT 固定在 0x8F050，位于引导器 0x8F000-0x90000 栈内。
# 这两条规则把"入口必须离开该栈"从人工判断变成构建期硬断言。
BOOT_STACK_BOTTOM = 0x8F000
BOOT_STACK_TOP    = 0x90000
TEMP_GDT_ADDR     = 0x8F050
KSTACK_BOTTOM = sym_addr('hepu_kstack_bottom')
KSTACK_TOP    = sym_addr('hepu_kstack_top')

if KSTACK_BOTTOM is None or KSTACK_TOP is None:
    errors.append('规则 5: 未找到 hepu_kstack_bottom / hepu_kstack_top 符号')
elif KSTACK_TOP <= KSTACK_BOTTOM:
    errors.append(f'规则 5 FAIL: 内核栈边界非法 bottom=0x{KSTACK_BOTTOM:x} top=0x{KSTACK_TOP:x}')
elif KSTACK_BOTTOM < BOOT_STACK_TOP and KSTACK_TOP > BOOT_STACK_BOTTOM:
    errors.append(f'规则 5 FAIL: 内核栈 0x{KSTACK_BOTTOM:x}-0x{KSTACK_TOP:x} 与引导器栈 0x{BOOT_STACK_BOTTOM:x}-0x{BOOT_STACK_TOP:x} 重叠（临时 GDT=0x{TEMP_GDT_ADDR:x}）')
elif bss_start is not None and bss_end is not None and not (KSTACK_BOTTOM >= bss_start and KSTACK_TOP <= bss_end):
    errors.append(f'规则 5 FAIL: 内核栈 0x{KSTACK_BOTTOM:x}-0x{KSTACK_TOP:x} 不在 BSS 0x{bss_start:x}-0x{bss_end:x} 内')
else:
    print(f'[{chr(0x2714)}] 规则 5 PASS: 内核栈 0x{KSTACK_BOTTOM:x}-0x{KSTACK_TOP:x} 与引导器栈 0x{BOOT_STACK_BOTTOM:x}-0x{BOOT_STACK_TOP:x} 不重叠')

# 规则 6: 入口汇编顺序约束（魔数首指令 + BSS 清零后再切栈 + 切栈前无 call）
ENTRY_SRC = os.path.join(ROOT, 'kernel/arch/x86_64/entry_64.S')
try:
    entry_text = open(ENTRY_SRC, encoding='utf-8', errors='replace').read()
except OSError as e:
    errors.append(f'规则 6: 无法读取 {ENTRY_SRC}: {e}')
    entry_text = ''

def _entry_region(text):
    """返回 x86_64_entry_64 到 kernel_gdt64_flush 结束（下一个全局符号前）的指令行。"""
    start = text.find('x86_64_entry_64:')
    if start < 0:
        return None
    flush = text.find('kernel_gdt64_flush:', start)
    if flush < 0:
        return None
    # 到下一个 .global 之前结束（当前函数之后是 koutc）
    end = text.find('.global', flush)
    body = text[flush:end if end > 0 else len(text)]
    # 去掉块注释和行注释，只留指令
    body = re.sub(r'/\*.*?\*/', '', body, flags=re.S)
    body = re.sub(r'#.*', '', body)
    lines = [ln.strip() for ln in body.splitlines()]
    return [ln for ln in lines if ln and not ln.startswith(';')]

entry_start = entry_text.find('x86_64_entry_64:')
first_instr = ''
if entry_start >= 0:
    after = entry_text[entry_start:].splitlines()[1:]
    for ln in after:
        s = ln.strip()
        if s and not s.startswith('/*') and not s.startswith('*') and not s.startswith('#'):
            first_instr = s
            break

body_lines = _entry_region(entry_text)
if body_lines is None:
    errors.append('规则 6: 未找到 x86_64_entry_64 / kernel_gdt64_flush 标签')
else:
    magic_ok = first_instr.startswith('movl $0x2BADB002, %edi')
    rep_idx = next((i for i, ln in enumerate(body_lines) if ln.startswith('rep stosq')), None)
    rsp_idx = next((i for i, ln in enumerate(body_lines) if 'hepu_kstack_top' in ln and '%rsp' in ln), None)
    call_idx = next((i for i, ln in enumerate(body_lines) if ln.startswith('call')), None)
    if not magic_ok:
        errors.append(f'规则 6 FAIL: x86_64_entry_64 首条指令不是引导器魔数 movl $0x2BADB002,%edi（实际: {first_instr!r}）')
    elif rep_idx is None or rsp_idx is None:
        errors.append('规则 6 FAIL: 未找到 rep stosq 或 hepu_kstack_top 切栈指令')
    elif rsp_idx <= rep_idx:
        errors.append('规则 6 FAIL: hepu_kstack_top 切栈发生在 rep stosq 之前（BSS 清零会清掉正在使用的内核栈）')
    elif call_idx is not None and call_idx <= rsp_idx:
        errors.append('规则 6 FAIL: 切到 hepu_kstack_top 之前出现 C call')
    else:
        print(f'[{chr(0x2714)}] 规则 6 PASS: 入口首指令为 0x2BADB002 魔数，BSS 清零后切 hepu_kstack_top，切栈前无 call')

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
    print(f'  - 若早期栈越界:             改 entry_64.S 的 hepu_kstack_top 切换点，禁止回到 0x8F000-0x90000')
    print(f'  - 改完 Stage2 后, 同步更新本脚本顶部的 PT_BASE / PD_ENTRIES')
    sys.exit(1)
else:
    print(f'[PASS] 全部安全检查通过 ({6 - (1 if kheap_end is None else 0)} 条规则)')
    sys.exit(0)
