#!/usr/bin/env python3
"""检查 IME 词库的每个汉字是否都有可渲染字形。

M0 的硬要求：拼音候选不能出现空白框。64 位 TTF 子集只覆盖 559 个词库字，
其余必须由 font_cjk_big 16x16 点阵兜底覆盖。

用法:
  python3 tools/check_ime_glyphs.py
  python3 tools/check_ime_glyphs.py --ttf kernel/arch/x86_64/cjk_font_data.c
"""
import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
IME_FILES = [
    ROOT / "kernel/shared/ime/ime_data.c",
    ROOT / "kernel/shared/ime/ime_data_table.c",
]
CJK_RE = re.compile(r"[\u3400-\u9fff\uf900-\ufaff]")
CODEPOINT_RE = re.compile(r"^\s*\{\s*0x([0-9A-Fa-f]{4,6})\s*,", re.M)


def strings_in_source(path: Path):
    """只取字符串字面量，避免把注释里的汉字算进词库。"""
    text = path.read_text(encoding="utf-8", errors="replace")
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    text = re.sub(r"//[^\n]*", "", text)
    return re.findall(r'"((?:[^"\\]|\\.)*)"', text)


def ime_chars():
    chars = set()
    for p in IME_FILES:
        if not p.exists():
            print(f"missing source: {p}", file=sys.stderr)
            continue
        for s in strings_in_source(p):
            chars.update(CJK_RE.findall(s))
    return chars


def bitmap_codepoints():
    hdr = ROOT / "kernel/include/font_cjk_big.h"
    text = hdr.read_text(encoding="utf-8", errors="replace")
    return {int(m, 16) for m in CODEPOINT_RE.findall(text)}


def ttf_codepoints(path: Path):
    """解析 cjk_font_data.c 里按码点升序的 cmap 数组（仅用于对照报告）。"""
    if not path.exists():
        return set()
    text = path.read_text(encoding="utf-8", errors="replace")
    return {int(m, 16) for m in CODEPOINT_RE.findall(text)}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ttf", default="", help="可选：cjk_font_data.c，打印 TTF 覆盖率")
    args = ap.parse_args()

    chars = ime_chars()
    bmp = bitmap_codepoints()
    missing = sorted((c for c in chars if ord(c) not in bmp), key=ord)
    ttf = ttf_codepoints(ROOT / args.ttf) if args.ttf else set()

    print(f"IME 词库去重汉字: {len(chars)}")
    print(f"font_cjk_big 码点: {len(bmp)}")
    if ttf:
        print(f"TTF 子集覆盖: {sum(1 for c in chars if ord(c) in ttf)}/{len(chars)}"
              f"（其余走点阵兜底）")
    print(f"点阵覆盖: {len(chars) - len(missing)}/{len(chars)}")
    if missing:
        print("缺字: " + "、".join(missing))
        print(f"FAIL: {len(missing)} 个候选会显示空白框")
        return 1
    print("OK: 所有候选都有点阵兜底，不会空白")
    return 0


if __name__ == "__main__":
    sys.exit(main())
