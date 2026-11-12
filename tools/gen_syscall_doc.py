#!/usr/bin/env python3
"""生成 docs/specs/SYSCALL_ABI.md —— 从源码解析真实号表

事实源（按优先级）：
  1. kernel/include/syscall.h        —— SYS_xxx 数字定义（原生号空间）
  2. kernel/arch/x86_64/syscall64_full.c
       - #define SYSCALL_xxx N         —— 直接数字（多为 Linux 号直取）
       - #define SYSCALL_xxx SYS_yyy   —— 引用原生号
       - 表格 [SYSCALL_xxx] = wrap_yyy —— 已注册即已实现

V0.54.6 起源码把 SYSCALL_xxx 改成「引用 SYS_xxx」，旧版脚本只认数字，
于是产出空表、文档长期停留在过期快照。本版按 C 预处理器的覆盖语义求值：
同一宏名后出现的定义覆盖先出现的。
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SYS_H = ROOT / 'kernel/include/syscall.h'
FULL_C = ROOT / 'kernel/arch/x86_64/syscall64_full.c'


def parse_sys_h(path):
    """原生号空间：SYS_xxx = 数字"""
    text = path.read_text(encoding='utf-8', errors='ignore')
    out = {}
    for m in re.finditer(r'^\s*#define\s+(SYS_[A-Z0-9_]+)\s+(\d+)\b', text, re.M):
        out[m.group(1)] = int(m.group(2))
    return out


def parse_full_c(path):
    """SYSCALL_xxx 定义（按出现顺序覆盖）+ 注册表"""
    text = path.read_text(encoding='utf-8', errors='ignore')
    defs = {}
    for m in re.finditer(r'^\s*#define\s+(SYSCALL_[A-Z0-9_]+)\s+([A-Za-z0-9_]+)\b(.*)$', text, re.M):
        name, tok, tail = m.group(1), m.group(2), m.group(3)
        line = text[:m.start()].count('\n') + 1
        kind = 'num' if tok.isdigit() else 'ref'
        if 'Linux' in tail and tok.isdigit():
            kind = 'linux_direct'
        defs[name] = (tok, kind, line)

    regs = {}
    for m in re.finditer(r'\[\s*(SYSCALL_[A-Z0-9_]+)\s*\]\s*=\s*(\w+)', text):
        regs[m.group(1)] = m.group(2)
    return defs, regs


def resolve(defs, sysnums):
    rows, unresolved = [], []
    for name, (tok, kind, line) in defs.items():
        if tok.isdigit():
            val = int(tok)
            origin = 'Linux 号直取' if kind == 'linux_direct' else '源码数字'
        else:
            if tok not in sysnums:
                unresolved.append((name, tok, line))
                continue
            val, origin = sysnums[tok], f'原生 {tok}'
        rows.append({'name': name.replace('SYSCALL_', ''), 'macro': name,
                     'num': val, 'origin': origin, 'line': line})
    return rows, unresolved


def main():
    sysnums = parse_sys_h(SYS_H)
    defs, regs = parse_full_c(FULL_C)
    rows, unresolved = resolve(defs, sysnums)

    by_num = {}
    for r in rows:
        if r['macro'] in regs:
            by_num.setdefault(r['num'], []).append(r)
    collisions = {n: v for n, v in by_num.items() if len(v) > 1}

    # SYSCALL_MAX 是表容量边界常量，不是系统调用
    rows = [r for r in rows if r['name'] != 'MAX']

    impl = [r for r in rows if r['macro'] in regs]
    unim = [r for r in rows if r['macro'] not in regs]
    impl.sort(key=lambda r: (r['num'], r['name']))

    out = []
    out.append('''---
title: 赫普OS 系统调用 ABI（源码自动生成）
status: active
sys_version: 0.57.0
updated: 2026-11-12
generator: tools/gen_syscall_doc.py
source_of_truth: kernel/include/syscall.h + kernel/arch/x86_64/syscall64_full.c
---

# 系统调用 ABI（SYSCALL_ABI）

> 本文件由 `tools/gen_syscall_doc.py` 自动生成，**禁止手写**。
> 重新生成：`python3 tools/gen_syscall_doc.py > docs/specs/SYSCALL_ABI.md`
> 双重事实源：`SYS_xxx` 号在 `kernel/include/syscall.h`，`SYSCALL_xxx` 映射与注册表在 `syscall64_full.c`。
> 调用约定：`syscall64_dispatch(number, arg1..arg5)`；64 位入口 `syscall` 指令带 6 个参数寄存器（rdi/rsi/rdx/r10/r8/r9），32 位 `int 0x80` 入口只带 4 个。
> 「号」= 最终数值；「号来源」标明该值是原生号还是直接采用 Linux 号（后者有撞号风险，见文末自检）。

## 已注册（已实现）系统调用

| 号 | 名称 | 处理器 | 号来源 | 宏 |
|----|------|--------|--------|-----|
''')
    for r in impl:
        out.append(f"| {r['num']} | {r['name']} | {regs[r['macro']]} | {r['origin']} | `{r['macro']}` |\n")
    out.append(f"\n共 {len(impl)} 项已注册。\n")

    if unim:
        out.append('\n## 已定义但未注册（未实现/未接线）\n\n| 名称 | 号 | 号来源 | 宏 |\n|------|-----|--------|-----|\n')
        for r in sorted(unim, key=lambda r: (r['num'], r['name'])):
            out.append(f"| {r['name']} | {r['num']} | {r['origin']} | `{r['macro']}` |\n")

    out.append('\n## 生成器自检\n\n')
    if collisions:
        out.append('⚠️ **号冲突：同一号被多个已注册宏占用**（注册表按源码顺序覆盖，必然有一个不可达）：\n\n')
        out.append('| 号 | 冲突宏 |\n|-----|--------|\n')
        for n, items in sorted(collisions.items()):
            names = '、'.join(f"`{i['macro']}`" for i in items)
            out.append(f'| {n} | {names} |\n')
    else:
        out.append('✅ 未检测到已注册宏之间的号冲突。\n')

    if unresolved:
        out.append('\n⚠️ **未解析引用**（宏引用了 `syscall.h` 中不存在的 `SYS_xxx`）：\n\n')
        for name, tok, line in unresolved:
            out.append(f'- `{name}` → `{tok}`（第 {line} 行）\n')

    out.append('''
## 口径说明

1. **号空间是混用的**：`SYS_xxx` 原生号（0–99）、`SYS_LNX64_xxx` 自定义扩展号（100 段）、
   以及少数直接采用 Linux x86_64 号（如 `SYSCALL_GETSOCKNAME=140`、`SYSCALL_SHUTDOWN=145`）。
   后者一旦与未来原生号扩张撞车，注册表会静默覆盖——新增号时必须先跑本生成器查冲突。
2. **本表只描述"已注册"**（即 `syscall_table[]` 里有项）。注册了但实现是桩返回 0 的项
   （当前：`epoll_create/epoll_ctl/epoll_wait/eventfd2`）**仍然算"已注册"**，
   其语义完整度见 `planning/ROADMAP.md` P1-1 与 `architecture/KERNEL.md` 4.3 备注。
3. 32 位路径（`int 0x80`）与本表共用同一号空间，但入口只压 4 个参数寄存器，
   所以 `mmap(fd/offset)`、`clone(tls)`、`futex` 在 32 位路径上参数不全。
''')

    sys.stdout.write(''.join(out))


if __name__ == '__main__':
    main()
