#!/usr/bin/env python3
"""ACPI 语料准备脚本（审计取证用，只读第三方仓库，不把二进制写进仓库）。

功能：
  1. 从已浅克隆的公开 GitHub 仓库拷贝精选的真实二进制 AML（.dat/.aml）；
  2. 把 linux-surface/acpidumps 这类 acpidump 十六进制文本（.out）按 ACPI 头
     长度还原成二进制表（还原件，非固件原始二进制，清单里单独标注）；
  3. 在本地 OVMF / SeaBIOS 固件镜像里扫描 DSDT/SSDT 头并只读提取；
  4. 计算 SHA256，输出 corpus-manifest.tsv / .json 供探针与报告引用。

用法：
  prepare_aml_corpus.py [--clones DIR] [--out DIR] [--evidence DIR]

环境变量：
  CLONES_DIR  默认 /tmp/hepu-aml-corpus/work/cl
  OUT_DIR      默认 /tmp/hepu-aml-corpus/dl
  EVIDENCE_DIR 默认仓库内 docs/status/evidence-aml-corpus-2026-11-27

注意：本脚本不编译 ASL（环境无 iasl）；.dsl/.asl 文本一律不纳入二进制语料。
"""
import hashlib
import json
import os
import struct
import sys
from urllib.parse import quote

DEFAULT_CLONES = os.environ.get(
    "HEPU_CORPUS_CLONES",
    "/home/user/.doubao/agent_mode/workspace/.sessions/38452875115179010/"
    "agents/s_000K1zmR5nS/scratch/cl")
DEFAULT_OUT = os.environ.get(
    "HEPU_CORPUS_OUT",
    "/home/user/.doubao/agent_mode/workspace/.sessions/38452875115179010/"
    "agents/s_000K1zmR5nS/scratch/dl")
DEFAULT_EVIDENCE = os.path.join(
    os.path.dirname(os.path.abspath(__file__)),
    "..", "..", "docs", "status", "evidence-aml-corpus-2026-11-27")

# ---------------------------------------------------------------------------
# 精选清单：repo, branch, path, 输出名, 机型/备注, 语料子类
# kind: raw-binary（仓库里直接是二进制）/ reassembled（从十六进制文本还原）
# ---------------------------------------------------------------------------
RAW_PICKS = [
    # --- marco-giunta/lenovo-acpi-dump（main，真实笔记本 acpidump -b 输出）---
    ("marco-giunta/lenovo-acpi-dump", "main",
     "16AFR10H/acpi_dump/dsdt.dat", "lenovo-16afr10h-dsdt.dat",
     "Lenovo 16AFR10H 笔记本 DSDT", "raw-binary"),
    ("marco-giunta/lenovo-acpi-dump", "main",
     "16AFR10H/acpi_dump/ssdt1.dat", "lenovo-16afr10h-ssdt1.dat",
     "Lenovo 16AFR10H SSDT1", "raw-binary"),
    ("marco-giunta/lenovo-acpi-dump", "main",
     "16AFR10H/acpi_dump/ssdt2.dat", "lenovo-16afr10h-ssdt2.dat",
     "Lenovo 16AFR10H SSDT2", "raw-binary"),
    ("marco-giunta/lenovo-acpi-dump", "main",
     "14ASP10/QFCN29WW/acpi_dump/dsdt.dat", "lenovo-14asp10-dsdt.dat",
     "Lenovo 14ASP10 (QFCN29WW) DSDT", "raw-binary"),
    ("marco-giunta/lenovo-acpi-dump", "main",
     "14ASP10/QFCN29WW/acpi_dump/ssdt1.dat", "lenovo-14asp10-ssdt1.dat",
     "Lenovo 14ASP10 SSDT1", "raw-binary"),
    # --- Phandoxx/acpidump-a16-11m（main，骁龙 SDM8380 ARM64 笔记本）---
    ("Phandoxx/acpidump-a16-11m", "main",
     "acpiorig/dsdt.dat", "a16-11m-dsdt.dat",
     "A16-11m (SDM8380) DSDT", "raw-binary"),
    ("Phandoxx/acpidump-a16-11m", "main",
     "acpiorig/ssdt.dat", "a16-11m-ssdt.dat",
     "A16-11m (SDM8380) SSDT", "raw-binary"),
    # --- holoto/ec-471g（master，Clover origin 原始转储，未打补丁）---
    ("holoto/ec-471g", "master",
     "EFI/EFI/CLOVER/ACPI/origin/DSDT.aml", "acer-ec471g-dsdt.aml",
     "Acer EC-471G origin DSDT", "raw-binary"),
    ("holoto/ec-471g", "master",
     "EFI/EFI/CLOVER/ACPI/origin/SSDT-1.aml", "acer-ec471g-ssdt1.aml",
     "Acer EC-471G origin SSDT-1", "raw-binary"),
    ("holoto/ec-471g", "master",
     "EFI/EFI/CLOVER/ACPI/origin/SSDT-2.aml", "acer-ec471g-ssdt2.aml",
     "Acer EC-471G origin SSDT-2", "raw-binary"),
    # --- javanesse/Asus-ROG-GL552JX-Hackintosh（master，origin 转储）---
    ("javanesse/Asus-ROG-GL552JX-Hackintosh", "master",
     "Archieve/V2/1. origin/DSDT.aml", "asus-gl552jx-dsdt.aml",
     "Asus ROG GL552JX origin DSDT", "raw-binary"),
    ("javanesse/Asus-ROG-GL552JX-Hackintosh", "master",
     "Archieve/V2/1. origin/SSDT-2-CpuPm.aml", "asus-gl552jx-ssdt-cpupm.aml",
     "Asus ROG GL552JX origin SSDT-CpuPm", "raw-binary"),
    # --- 补丁型 SSDT（hackintosh 作者改写过，不是原厂表，清单单独标注）---
    ("icedman/XPS13-9350-hackintosh", "master",
     "ssdts/SSDT.aml", "dell-xps9350-ssdt-cpupm.aml",
     "Dell XPS13 9350 补丁 SSDT (CPU PM, 非原厂)", "raw-binary"),
    ("CLAY-BIOS/Lenovo-ThinkPad-T450s-Hackintosh-OpenCore", "master",
     "EFI/OC/ACPI/SSDT-BATX.aml", "t450s-ssdt-batx.aml",
     "ThinkPad T450s 电池补丁 SSDT-BATX", "raw-binary"),
    ("CLAY-BIOS/Lenovo-ThinkPad-T450s-Hackintosh-OpenCore", "master",
     "EFI/OC/ACPI/old patch/SSDT-OCBATT.aml", "t450s-ssdt-ocbatt.aml",
     "ThinkPad T450s 旧版电池补丁 SSDT-OCBATT", "raw-binary"),
    ("zhtengw/EFI-for-X1C6-hackintosh", "master",
     "OpenCore/EFI/OC/ACPI/SSDT-Battery.aml", "x1c6-ssdt-battery.aml",
     "ThinkPad X1C6 电池补丁 SSDT-Battery", "raw-binary"),
]

# linux-surface/acpidumps 的 .out 文本还原：选 DSDT 与紧随其后的第一张 SSDT。
OUT_PICKS = [
    ("linux-surface/acpidumps", "master", "surface_book_2/acpidump.out",
     "surface-book2", "Surface Book 2"),
    ("linux-surface/acpidumps", "master", "surface_laptop_5/acpidump.out",
     "surface-laptop5", "Surface Laptop 5"),
    ("linux-surface/acpidumps", "master", "surface_pro_7/acpidump.out",
     "surface-pro7", "Surface Pro 7"),
]

# 完整 owner/name -> 本地浅克隆目录名
DIR_ALIAS = {
    "linux-surface/acpidumps": "acpidumps",
    "Phandoxx/acpidump-a16-11m": "acpidump-a16-11m",
    "holoto/ec-471g": "ec-471g",
    "javanesse/Asus-ROG-GL552JX-Hackintosh": "Asus-ROG-GL552JX-Hackintosh",
    "icedman/XPS13-9350-hackintosh": "XPS13-9350-hackintosh",
    "CLAY-BIOS/Lenovo-ThinkPad-T450s-Hackintosh-OpenCore":
        "Lenovo-ThinkPad-T450s-Hackintosh-OpenCore",
    "zhtengw/EFI-for-X1C6-hackintosh": "EFI-for-X1C6-hackintosh",
    "marco-giunta/lenovo-acpi-dump": "lenovo-acpi-dump",
}

FIRMWARE_SCAN = [
    ("/home/user/Doubao/chats/38452875115179010/.vm-assets/OVMF_CODE_4M.fd",
     "OVMF_CODE_4M.fd"),
    ("/home/user/Doubao/chats/38452875115179010/.vm-assets/OVMF_VARS_4M.fd",
     "OVMF_VARS_4M.fd"),
    ("/home/user/Doubao/chats/38452875115179010/.toolchain/pkg/datadir/bios.bin",
     "SeaBIOS-bios.bin"),
]

EISA = {
    "PNP0C0A": bytes([0x0A, 0x0C, 0xD0, 0x41]),  # 0x41D00C0A LE
    "ACPI0003": bytes([0x03, 0x00, 0x70, 0x04]),  # 0x04700003 LE
    "PNP0C09": bytes([0x09, 0x0C, 0xD0, 0x41]),  # 0x41D00C09 LE
}


def sha256(b: bytes) -> str:
    return hashlib.sha256(b).hexdigest()


def detect_ids(blob: bytes):
    out = {}
    for name, sig in (("PNP0C0A", b"PNP0C0A"), ("ACPI0003", b"ACPI0003"),
                      ("PNP0C09", b"PNP0C09")):
        out[name] = (sig in blob) or (EISA[name] in blob)
    return out


def acpi_header(blob: bytes):
    """返回 (sig, length)；头不合法返回 None。"""
    if len(blob) < 36:
        return None
    sig = blob[0:4].decode("ascii", "replace")
    length = struct.unpack("<I", blob[4:8])[0]
    if sig not in ("DSDT", "SSDT"):
        return None
    if length < 36 or length > len(blob):
        return None
    return sig, length


def parse_acpidump_text(text: str):
    """把 acpidump 文本拆成 {表签名: [bytes,...]}。"""
    sections = []
    cur_sig = None
    cur_bytes = []
    for line in text.splitlines():
        head = line.strip()
        if head.endswith("@ 0x0000000000000000") or " @ 0x" in head:
            if cur_sig is not None:
                sections.append((cur_sig, bytes(cur_bytes)))
            cur_sig = head.split(" @ ")[0].strip()
            cur_bytes = []
            continue
        if cur_sig is None:
            continue
        # 十六进制行： "    0000: 53 53 ..."
        if ":" not in head:
            continue
        hexpart = head.split(":", 1)[1]
        tokens = hexpart.split()
        for tok in tokens:
            if len(tok) == 2 and all(c in "0123456789abcdefABCDEF" for c in tok):
                cur_bytes.append(int(tok, 16))
    if cur_sig is not None:
        sections.append((cur_sig, bytes(cur_bytes)))
    return sections


def main():
    clones = os.environ.get("CLONES_DIR", DEFAULT_CLONES)
    out = os.environ.get("OUT_DIR", DEFAULT_OUT)
    evidence = os.environ.get("EVIDENCE_DIR", DEFAULT_EVIDENCE)
    os.makedirs(out, exist_ok=True)
    os.makedirs(os.path.join(out, "firmware"), exist_ok=True)
    os.makedirs(evidence, exist_ok=True)

    manifest = []

    def add(row):
        manifest.append(row)

    # 1) 真实二进制拷贝
    for repo, branch, rel, name, note, kind in RAW_PICKS:
        src = os.path.join(clones, DIR_ALIAS.get(repo, repo), rel)
        url = (f"https://raw.githubusercontent.com/{repo}/{branch}/"
               f"{quote(rel, safe='/')}")
        if not os.path.isfile(src):
            add({"name": name, "source_url": url, "kind": kind, "note": note,
                 "status": "MISSING", "size": 0, "sha256": "", "sig": "",
                 "battery": False, "ac": False, "ec": False})
            print(f"[miss] {name} <- {src}", file=sys.stderr)
            continue
        blob = open(src, "rb").read()
        hdr = acpi_header(blob)
        if hdr:
            blob = blob[:hdr[1]]
        dst = os.path.join(out, name)
        with open(dst, "wb") as f:
            f.write(blob)
        ids = detect_ids(blob)
        add({"name": name, "source_url": url, "kind": kind, "note": note,
             "status": "ok", "size": len(blob), "sha256": sha256(blob),
             "sig": hdr[0] if hdr else "?",
             "battery": ids["PNP0C0A"], "ac": ids["ACPI0003"],
             "ec": ids["PNP0C09"]})
        print(f"[raw] {name} {len(blob)}B {hdr[0] if hdr else '??'}")

    # 2) .out 文本还原
    for repo, branch, rel, slug, machine in OUT_PICKS:
        src = os.path.join(clones, DIR_ALIAS.get(repo, repo), rel)
        url = (f"https://github.com/{repo}/blob/{branch}/"
               f"{quote(rel, safe='/')}")
        if not os.path.isfile(src):
            print(f"[miss] {src}", file=sys.stderr)
            continue
        text = open(src, encoding="utf-8", errors="replace").read()
        sections = parse_acpidump_text(text)
        dsdt = [b for s, b in sections if s == "DSDT"]
        SSDTs = [b for s, b in sections if s == "SSDT"]
        picked = ([("DSDT", dsdt[0])] if dsdt else [])
        if SSDTs:
            picked.append(("SSDT", SSDTs[0]))
        for i, (sig, blob) in enumerate(picked):
            hdr = acpi_header(blob)
            if not hdr:
                continue
            blob = blob[:hdr[1]]
            name = f"{slug}-{'dsdt' if sig == 'DSDT' else 'ssdt'}{i if sig=='SSDT' else ''}.aml"
            dst = os.path.join(out, name)
            with open(dst, "wb") as f:
                f.write(blob)
            ids = detect_ids(blob)
            add({"name": name, "source_url": url, "kind": "reassembled",
                 "note": f"{machine} {sig}（由 acpidump 十六进制文本还原，非原始二进制）",
                 "status": "ok", "size": len(blob), "sha256": sha256(blob),
                 "sig": sig, "battery": ids["PNP0C0A"], "ac": ids["ACPI0003"],
                 "ec": ids["PNP0C09"]})
            print(f"[out] {name} {len(blob)}B {sig}")

    # 3) 固件镜像扫描（只读）
    for path, label in FIRMWARE_SCAN:
        if not os.path.isfile(path):
            add({"name": label, "source_url": f"local:{path}",
                 "kind": "firmware", "note": "固件镜像缺失", "status": "MISSING",
                 "size": 0, "sha256": "", "sig": "", "battery": False,
                 "ac": False, "ec": False})
            continue
        blob = open(path, "rb").read()
        fw_hash = sha256(blob)
        found = []
        off = 0
        while True:
            i = blob.find(b"DSDT", off)
            j = blob.find(b"SSDT", off)
            nxt = min(x for x in (i, j) if x >= 0) if (i >= 0 or j >= 0) else -1
            if nxt < 0:
                break
            off = nxt + 1
            if nxt + 36 > len(blob):
                continue
            length = struct.unpack("<I", blob[nxt + 4:nxt + 8])[0]
            sig = blob[nxt:nxt + 4].decode()
            if 36 <= length <= len(blob) - nxt:
                table = blob[nxt:nxt + length]
                cksum_ok = (sum(table) & 0xFF) == 0
                # 长度超过镜像剩余过多或校验和不通过的，按误报剔除，不进探针语料。
                if not cksum_ok or length > len(blob) - nxt - 16:
                    continue
                tname = f"{label}.{sig.lower()}@{nxt:#x}.aml"
                with open(os.path.join(out, "firmware", tname), "wb") as f:
                    f.write(table)
                ids = detect_ids(table)
                add({"name": tname, "source_url": f"local:{path}#0x{nxt:x}",
                     "kind": "firmware",
                     "note": f"{label} 镜像内 {sig} 头（QEMU 固件模板，不是笔记本电池固件；checksum=ok）",
                     "status": "ok", "size": len(table), "sha256": sha256(table),
                     "sig": sig, "battery": ids["PNP0C0A"], "ac": ids["ACPI0003"],
                     "ec": ids["PNP0C09"], "fw_sha256": fw_hash})
                found.append(tname)
        if not found:
            add({"name": label, "source_url": f"local:{path}", "kind": "firmware",
                 "note": f"{label} 内未定位到合法 DSDT/SSDT 头", "status": "none",
                 "size": len(blob), "sha256": fw_hash, "sig": "",
                 "battery": False, "ac": False, "ec": False})
            print(f"[fw] {label}: 未发现 DSDT/SSDT 表")

    # 3b) 合成夹具（自产小文件，明确标注 synthetic，不是真机固件）
    fixture_dir = os.environ.get(
        "FIXTURE_DIR",
        os.path.join(evidence, "fixtures"))
    if os.path.isdir(fixture_dir):
        for name in sorted(os.listdir(fixture_dir)):
            if not name.endswith((".aml", ".dat")):
                continue
            blob = open(os.path.join(fixture_dir, name), "rb").read()
            ids = detect_ids(blob)
            add({"name": name,
                 "source_url": "self-generated:tools/regress/make_corpus_fixtures.py",
                 "kind": "synthetic",
                 "note": "合成夹具（贴近真实写法，非真机固件）",
                 "status": "ok", "size": len(blob), "sha256": sha256(blob),
                 "sig": "SSDT", "battery": ids["PNP0C0A"],
                 "ac": ids["ACPI0003"], "ec": ids["PNP0C09"]})

    # 4) 清单落盘
    tsv = os.path.join(evidence, "corpus-manifest.tsv")
    cols = ["name", "kind", "status", "sig", "size", "sha256", "battery",
            "ac", "ec", "note", "source_url"]
    with open(tsv, "w", encoding="utf-8") as f:
        f.write("\t".join(cols) + "\n")
        for r in manifest:
            f.write("\t".join(str(r.get(c, "")) for c in cols) + "\n")
    with open(os.path.join(evidence, "corpus-manifest.json"), "w",
              encoding="utf-8") as f:
        json.dump(manifest, f, ensure_ascii=False, indent=1)
    print(f"\nmanifest: {tsv}  ({len(manifest)} 条)")


if __name__ == "__main__":
    main()
