#!/usr/bin/env python3
"""把探针原始日志汇总成 probe-results.tsv（逐表建库/枚举/求值结果）。"""
import argparse
import csv
import os
import re


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--log", required=True)
    ap.add_argument("--manifest", required=True)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    manifest = {}
    with open(args.manifest, encoding="utf-8") as f:
        for row in csv.DictReader(f, delimiter="\t"):
            manifest[row["name"]] = row

    text = open(args.log, encoding="utf-8").read()
    blocks = text.split("=== TABLE ")[1:]
    rows = []
    for b in blocks:
        fname = os.path.basename(b.splitlines()[0].strip())
        m = re.search(r"sig=(\S+) size=(\d+) build_rc=(-?\d+)\((\w+)\) ready=(\d+) nodes=(\d+)", b)
        d = re.search(r"devices: PNP0C0A=(\d+) ACPI0003=(\d+) PNP0C09=(\d+)", b)
        ev = {}
        for meth in ("_STA", "_BIF", "_BIX", "_BST"):
            mm = re.search(rf"[^\n]*{meth} rc=(-?\d+)\((\w+)\)", b)
            ev[meth] = f"{mm.group(1)}/{mm.group(2)}" if mm else "-"
        kind = manifest.get(fname, {}).get("kind", "?")
        size = m.group(2) if m else "?"
        rows.append({
            "file": fname, "kind": kind,
            "sig": m.group(1) if m else "?", "size": size,
            "build_rc": f"{m.group(3)}/{m.group(4)}" if m else "?",
            "ready": m.group(5) if m else "?", "nodes": m.group(6) if m else "?",
            "bat": d.group(1) if d else "?", "ac": d.group(2) if d else "?",
            "ec": d.group(3) if d else "?",
            "_STA": ev["_STA"], "_BIF": ev["_BIF"], "_BIX": ev["_BIX"],
            "_BST": ev["_BST"],
        })

    cols = ["file", "kind", "sig", "size", "build_rc", "ready", "nodes",
            "bat", "ac", "ec", "_STA", "_BIF", "_BIX", "_BST"]
    with open(args.out, "w", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=cols, delimiter="\t")
        w.writeheader()
        w.writerows(rows)
    print(f"rows={len(rows)} -> {args.out}")


if __name__ == "__main__":
    main()
