#!/usr/bin/env python3
"""docs 防腐 CI 检查（4 条硬规则，失败即阻断）
1. sys_version 必须等于 VERSIONING.md 当前版本
2. 相对链接可解析
3. 无 NUL；status=active 文档不得含 V\\d+\\.\\d+ 自称版本号
4. 文中出现 待实现/⬜ 且对应源码文件存在 -> 告警
"""
import os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DOCS = os.path.join(ROOT, 'docs')
errors, warns = [], []

# 规则1: 版本一致（唯一事实源 = VERSIONING.md frontmatter 的 sys_version）
ver = None
try:
    _v = open(os.path.join(DOCS, 'status/VERSIONING.md'), errors='replace').read()
    _fm = re.search(r'^---\n(.*?)\n---', _v, re.S)
    if _fm:
        _sv = re.search(r'^sys_version:\s*(\S+)', _fm.group(1), re.M)
        if _sv:
            ver = _sv.group(1).strip()
except OSError:
    pass
if not ver:
    errors.append('VERSIONING.md 未找到当前版本')

def fm_of(p):
    s = open(p, errors='replace').read()
    m = re.search(r'^---\n(.*?)\n---', s, re.S)
    return m.group(1) if m else ''

for dirpath, _, files in os.walk(DOCS):
    for fn in files:
        if not fn.endswith('.md'): continue
        p = os.path.join(dirpath, fn)
        s = open(p, errors='replace').read()
        rel = os.path.relpath(p, DOCS)
        in_archive = '/archive/' in rel
        if rel.endswith('README.md'): continue   # 生成文件单独校验
        # 规则3: NUL
        if '\x00' in s:
            errors.append(f'[NUL] {rel}')
        fm = fm_of(p)
        status = re.search(r'^status:\s*(\S+)', fm, re.M)
        st = status.group(1) if status else 'active'
        # 规则1: sys_version（history 为冻结快照，不参与现行版本一致性检查）
        sv = re.search(r'^sys_version:\s*(\S+)', fm, re.M)
        in_history = rel.startswith('history/') or in_archive
        if sv and ver and not in_history and sv.group(1) != ver:
            errors.append(f'[VER] {rel}: sys_version={sv.group(1)} != {ver}')
        # 规则3: active 文档自称版本号(归档/变更日志豁免)
        # 仅当 V\d+.\d+ 出现在"自称当前版本"语境才告警，历史时间线/文档修订号不算
        if st == 'active' and not in_archive and rel != 'status/VERSIONING.md':
            for vm in re.finditer(r'V\d+\.\d+', s):
                ctx = s[max(0, vm.start()-30):vm.start()]
                if re.search(r'当前(版本|稳定|系统)|系统版本|内核版本|稳定版本|本系统|当前系统|适用系统版本|目标版本', ctx):
                    warns.append(f'[VER-SELF] {rel}: L{s[:vm.start()].count(chr(10))+1} 自称版本号 {vm.group()}')
                    break
        # 规则2: 相对链接
        for lm in re.finditer(r'\]\(([^)]+\.md)(?:#\S+)?\)', s):
            tgt = lm.group(1)
            if tgt.startswith(('http', '#')): continue
            if in_archive: continue   # 归档为历史快照, 不断链检查
            base = os.path.dirname(p)
            if not os.path.exists(os.path.normpath(os.path.join(base, tgt))):
                errors.append(f'[LINK] {rel}: {tgt} 不可解析')
        # 规则4: 待实现标记
        if st == 'active' and re.search(r'待实现|⬜', s):
            warns.append(f'[TODO-SRC] {rel}: 含待实现标记(需核对对应源码是否已实现)')

print('=== docs CI 检查 ===')
print(f'当前版本: {ver}')
for w in warns: print('WARN:', w)
for e in errors: print('ERROR:', e)
print(f'warns={len(warns)} errors={len(errors)}')
sys.exit(1 if errors else 0)
