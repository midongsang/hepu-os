#!/usr/bin/env python3
"""生成 docs/README.md —— 扫 frontmatter 自动索引（100% 可重建）"""
import datetime
import os, re


def current_version():
    """从 status/VERSIONING.md 的 frontmatter 读取当前版本，避免索引硬编码版本号。"""
    p = os.path.join('docs', 'status', 'VERSIONING.md')
    try:
        s = open(p, errors='replace').read()
    except OSError:
        return 'unknown'
    m = re.search(r'^---\n(.*?)\n---', s, re.S)
    if not m:
        return 'unknown'
    vm = re.search(r'^sys_version:\s*(\S+)', m.group(1), re.M)
    return vm.group(1) if vm else 'unknown'

def read_fm(p):
    s = open(p, errors='replace').read()
    m = re.search(r'^---\n(.*?)\n---', s, re.S)
    title, status = None, 'active'
    if m:
        fm = m.group(1)
        tm = re.search(r'^title:\s*(.+)$', fm, re.M)
        sm = re.search(r'^status:\s*(.+)$', fm, re.M)
        if tm: title = tm.group(1).strip()
        if sm: status = sm.group(1).strip()
    if not title:
        title = os.path.basename(p)[:-3]
    return title, status

def main():
    root = 'docs'
    groups = {}
    for dirpath, _, files in os.walk(root):
        if '/history/archive' in dirpath: continue   # 归档不占索引主体
        for fn in sorted(files):
            if not fn.endswith('.md'): continue
            p = os.path.join(dirpath, fn)
            if p.endswith('README.md'): continue
            title, status = read_fm(p)
            rel = p[len(root)+1:]
            groups.setdefault(os.path.dirname(rel) or '.', []).append((rel, title, status))

    order = ['.', 'architecture', 'specs', 'guides', 'planning', 'boot', 'security', 'status', 'history', 'history/devlog', 'history/security']
    ver = current_version()
    today = datetime.date.today().isoformat()
    lines = [f'''---
title: 赫普OS 文档索引
status: active
sys_version: {ver}
updated: {today}
generator: tools/gen_docs_index.py
source_of_truth: frontmatter of docs/*
---

# 赫普OS 文档索引

> 本文件由 `tools/gen_docs_index.py` 自动生成，禁止手写。文档分区：`现在是什么`(architecture) / `契约是什么`(specs) / `怎么做`(guides) / `下一步做什么`(planning) / `启动与固件`(boot) / `安全策略与审计`(security) / `现在到哪了`(status) / `过去发生了什么`(history) / `截图`(screenshots) / `架构图`(diagrams)。
> 唯一事实源：VERSIONING.md(版本)、ROADMAP.md(优先级)、SYSCALL_ABI.md(系统调用，源码生成)。

''']
    for g in order:
        if g not in groups: continue
        name = {'': '根目录'}.get(g, g)
        lines.append(f'## {name}\n')
        for rel, title, status in groups[g]:
            badge = '' if status == 'active' else f' `({status})`'
            lines.append(f'- [{title}]({rel}){badge}')
        lines.append('')
    # 归档
    arch = []
    for _, _, files in os.walk(os.path.join(root, 'history/archive')):
        for fn in sorted(files):
            if fn.endswith('.md'):
                arch.append(fn[:-3])
    lines.append('## 历史归档（history/archive）\n')
    lines.append(', '.join(f'`{a}`' for a in arch))
    lines.append('\n> 归档为只读历史，见 history/ 目录。')
    open(os.path.join(root, 'README.md'), 'w').write('\n'.join(lines))

if __name__ == '__main__':
    main()
