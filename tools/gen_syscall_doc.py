#!/usr/bin/env python3
"""生成 docs/specs/SYSCALL_ABI.md —— 从 syscall64_full.c 提取宏+注册表"""
import re, sys

def main():
    src = open('kernel/arch/x86_64/syscall64_full.c').read()

    # 1) 宏: #define SYSCALL_NAME N
    macros = {}
    for m in re.finditer(r'#define\s+(SYSCALL_\w+)\s+(\d+)', src):
        macros[m.group(1)] = int(m.group(2))

    # 2) 注册表: [SYSCALL_X] = wrap_y
    regs = re.findall(r'\[\s*(SYSCALL_\w+)\s*\]\s*=\s*(\w+)', src)
    registered = {name: wrap for name, wrap in regs}

    # 3) 注册表即实现来源

    lines = []
    lines.append('''---
title: 赫普OS 系统调用 ABI（源码自动生成）
status: active
sys_version: 0.54.6
updated: 2026-11-01
generator: tools/gen_syscall_doc.py
source_of_truth: kernel/arch/x86_64/syscall64_full.c
---

# 系统调用 ABI（SYSCALL_ABI）

> 本文件由 `tools/gen_syscall_doc.py` 自动生成，禁止手写。
> 生成命令：`python3 tools/gen_syscall_doc.py > docs/specs/SYSCALL_ABI.md`
> SYSCALL_MAX=256。调用约定：`syscall64_dispatch(number, arg1..arg5)`。

## 已注册系统调用

| 号 | 名称 | 处理器 | 状态 |
|----|------|--------|------|
''')
    for name in sorted(macros, key=lambda n: macros[n]):
        num = macros[name]
        wrap = registered.get(name, '—')
        st = '已实现' if wrap != '—' else '未注册'
        lines.append(f'| {num} | {name.replace("SYSCALL_","")} | {wrap} | {st} |')

    print('\n'.join(lines))

if __name__ == '__main__':
    main()
