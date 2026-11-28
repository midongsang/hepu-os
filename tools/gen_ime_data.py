#!/usr/bin/env python3
"""生成 kernel/shared/ime/ime_data_table.c。

数据来源：
- 现行 1,985 字常用表（保留原有候选顺序；含「呃/鞥」等表外生僻字）
- GB2312 一级汉字 3,755 字
- mozillazg/pinyin-data v0.16.0（MIT，见 tools/ime-data/LICENSE-pinyin-data.txt）
- 单字读音只做去声调、ü→v 归一化，不做词组切分/词典扩展

用法：
  python3 tools/gen_ime_data.py [--data PATH] [--out PATH]
"""
import argparse, re, unicodedata
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_DATA = ROOT / 'tools/ime-data/gb2312-level1-pinyin.txt'
DEFAULT_OUT = ROOT / 'kernel/shared/ime/ime_data_table.c'


def gb2312_level1():
    chars=[]
    for hi in range(0xB0, 0xD8):
        for lo in range(0xA1, 0xFF):
            try: c=bytes([hi,lo]).decode('gb2312')
            except UnicodeDecodeError: continue
            if '\u4e00' <= c <= '\u9fff': chars.append(c)
    return chars


def normalize_pinyin(s):
    s=s.strip().lower()
    s=unicodedata.normalize('NFD', s)
    s=''.join(ch for ch in s if unicodedata.category(ch) != 'Mn')
    s=s.replace('ü','v').replace('u:','v')
    return re.sub(r'[^a-z]', '', s)


def load_readings(path, wanted):
    readings={}
    for line in path.read_text(encoding='utf-8').splitlines():
        m=re.match(r'U\+([0-9A-Fa-f]{4,6}):\s*([^#]+)', line)
        if not m: continue
        ch=chr(int(m.group(1),16))
        if ch not in wanted: continue
        values=[]
        for raw in m.group(2).split(','):
            p=normalize_pinyin(raw)
            if p and p not in values: values.append(p)
        if values: readings[ch]=values
    return readings


def parse_existing_table(path):
    groups=[]
    if not path.exists(): return groups
    text=path.read_text(encoding='utf-8')
    for m in re.finditer(r'{"([a-z]+)",\s*"([^"]*)"}', text):
        py, hz=m.group(1), m.group(2)
        chars=[c for c in hz.replace(' ','') if '\u4e00' <= c <= '\u9fff']
        uniq=[]
        for c in chars:
            if c not in uniq: uniq.append(c)
        groups.append([py,uniq])
    return groups


def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--data', type=Path, default=DEFAULT_DATA)
    ap.add_argument('--out', type=Path, default=DEFAULT_OUT)
    args=ap.parse_args()

    gb=gb2312_level1()
    wanted=set(gb)
    readings=load_readings(args.data, wanted)
    missing=[c for c in gb if c not in readings]
    if missing:
        raise SystemExit(f'pinyin data missing for {len(missing)} GB2312 chars: {"".join(missing[:20])}')

    # 先保留现行表（含生僻字），再把 GB2312 的每个读音补进对应组。
    groups=parse_existing_table(args.out)
    idx={py:i for i,(py,_) in enumerate(groups)}
    for ch in gb:
        for py in readings[ch]:
            if py not in idx:
                idx[py]=len(groups)
                groups.append([py,[]])
            g=groups[idx[py]][1]
            if ch not in g: g.append(ch)

    groups.sort(key=lambda x:x[0])
    all_chars=set()
    for _,chars in groups: all_chars.update(chars)

    lines=['/* ============================================================================',
           ' * 赫普OS - 拼音-汉字数据表 (M3) — 主数据表',
           ' * 生成脚本: python3 tools/gen_ime_data.py',
           ' * 来源: GB2312 一级 3,755 字 + mozillazg/pinyin-data v0.16.0 (MIT)',
           ' * 注意: 本文件由脚本生成，不要手改。',
           ' * ========================================================================== */',
           '', '#include <stdint.h>', '',
           'typedef struct {', '    const char *py;', '    const char *hz;', '} ime_entry_t;',
           '', 'const ime_entry_t ime_table[] = {']
    for py,chars in groups:
        lines.append(f'    {{"{py}", "{' '.join(chars)}"}},')
    lines += ['};', '',
              '#define IME_TABLE_SIZE (sizeof(ime_table) / sizeof(ime_table[0]))',
              'const int ime_table_entries = IME_TABLE_SIZE;']
    args.out.write_text('\n'.join(lines)+'\n', encoding='utf-8')
    print(f'generated {args.out}: groups={len(groups)} chars={len(all_chars)} '
          f'occurrences={sum(len(c) for _,c in groups)} bytes={args.out.stat().st_size}')
    print(f'GB2312 level1: {len(gb)}, unique: {len(set(gb))}, max candidates/group: '
          f'{max(len(c) for _,c in groups)}')

if __name__ == '__main__':
    main()
