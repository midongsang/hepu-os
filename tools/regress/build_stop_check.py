#!/usr/bin/env python3
"""对标 HEAD aml64.c parse_termlist 外层分发，定位每张表首个被拒对象。

只用于审计取证；接受集合严格照 HEAD：
  0x10 Scope / 0x14 Method / 0x08 Name / 0x06 Alias / 0x15 External
  0x5B ext: 0x01 Mutex, 0x02 Event, 0x80 OpRegion, 0x81 Field,
            0x82 Device, 0x83 Processor, 0x84 PowerRes, 0x85 ThermalZone,
            0x86 IndexField(skip), 0x87 BankField(skip)
其余外层对象 -> 建库扫描终止（对应 parse_termlist return -1）。
"""
import os, struct, sys, glob, json

ACCEPT_EXT = {0x01, 0x02, 0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87}
EXT_NAME = {
    0x01: "Mutex", 0x02: "Event", 0x80: "OperationRegion", 0x81: "Field",
    0x82: "Device", 0x83: "Processor", 0x84: "PowerRes", 0x85: "ThermalZone",
    0x86: "IndexField", 0x87: "BankField", 0x88: "DataRegion",
    0x13: "CreateField", 0x20: "Load", 0x21: "Stall", 0x22: "Sleep",
    0x23: "Acquire", 0x24: "Signal", 0x25: "Wait", 0x26: "Reset",
    0x27: "Release", 0x28: "FromBcd", 0x29: "ToBcd", 0x2A: "Unload",
    0x30: "Revision", 0x31: "Debug", 0x32: "Fatal", 0x33: "Timer",
}
TERM_NAME = {
    0x10: "Scope", 0x14: "Method", 0x08: "Name", 0x06: "Alias",
    0x15: "External", 0x11: "Buffer", 0x12: "Package", 0x13: "VarPackage",
    0x70: "Store", 0xA0: "If", 0xA1: "Else", 0xA2: "While", 0xA4: "Return",
    0x86: "Notify", 0x8A: "CreateDWordField", 0x8B: "CreateWordField",
    0x8C: "CreateByteField", 0x8D: "CreateBitField", 0x8E: "ObjectType",
    0x8F: "CreateQWordField",
}


def pkg(b, p, end):
    if p >= end: return None
    b0 = b[p]; lead = b0 >> 6
    if lead == 0:
        length = b0 & 0x3F; h = 1
    else:
        if p + lead >= end: return None
        length = b0 & 0x0F
        for i in range(1, lead + 1):
            length |= b[p + i] << (4 + 8 * (i - 1))
        h = 1 + lead
    be = p + length
    return (be, p + h) if be <= end else None


def namestr(b, p, end):
    while p < end and b[p] == 0x5E: p += 1
    if p < end and b[p] == 0x5C: p += 1
    if p >= end: return None
    if b[p] == 0x2E: n, p = 2, p + 1
    elif b[p] == 0x2F:
        if p + 1 >= end: return None
        n, p = b[p + 1], p + 2
    else: n = 1
    p += 4 * n
    return p if p <= end else None


def const(b, p):
    op = b[p]
    if op in (0, 1, 0xFF): return p + 1
    if op == 0x0A: return p + 2
    if op == 0x0B: return p + 3
    if op == 0x0C: return p + 5
    if op == 0x0E: return p + 9
    return None


def data(b, p, end):
    op = b[p]
    if op in (0, 1, 0xFF, 0x0A, 0x0B, 0x0C, 0x0E): return const(b, p)
    if op == 0x0D:
        if p + 2 >= end: return None
        return p + 3 + b[p + 1]
    if op in (0x11, 0x12, 0x13):
        r = pkg(b, p + 1, end)
        return r[0] if r else None
    if 0x41 <= op <= 0x5B or op in (0x5C, 0x5E, 0x2E, 0x2F):
        return namestr(b, p, end)
    return None


def scan(b, end, depth=0, stop=None):
    """返回 (ok, first_rej_offset, first_rej_desc)。"""
    if depth > 16:
        return True, -1, "depth-limit"
    p = 0
    first = None
    while p < end:
        op = b[p]
        if op == 0x10:
            r = pkg(b, p + 1, end)
            if not r: return False, p, "PkgLength 损坏"
            be, q = r
            q = namestr(b, q, be)
            if q is None: return False, p, "NameString 损坏"
            ok, off, desc = scan(b, be, depth + 1)
            if not ok and first is None: first = (off, desc)
            p = be
        elif op == 0x14:
            r = pkg(b, p + 1, end)
            if not r: return False, p, "PkgLength 损坏"
            be, q = r
            p = be
        elif op == 0x08:
            q = namestr(b, p + 1, end)
            if q is None: return False, p, "NameString 损坏"
            d = data(b, q, end)
            p = d if d else q
        elif op == 0x06:
            q = namestr(b, p + 1, end)
            if q is None: return False, p, "Alias NameString 损坏"
            p = namestr(b, q, end)
            if p is None: return False, q, "Alias NameString 损坏"
        elif op == 0x15:
            q = namestr(b, p + 1, end)
            if q is None: return False, p, "External NameString 损坏"
            p = q + 2
        elif op == 0x5B:
            if p + 1 >= end: return False, p, "扩展前缀截断"
            code = b[p + 1]
            if code in ACCEPT_EXT:
                r = pkg(b, p + 2, end)
                if not r: return False, p, "扩展对象 PkgLength 损坏"
                be, q = r
                if code in (0x82, 0x83, 0x84, 0x85):
                    q = namestr(b, q, be)
                    if q is None: return False, p, "Device NameString 损坏"
                    if code == 0x83: q += 6
                    elif code == 0x84: q += 3
                    ok, off, desc = scan(b, be, depth + 1)
                    if not ok and first is None: first = (off, desc)
                p = be
            else:
                nm = EXT_NAME.get(code, f"UnknownExt{code:02X}")
                return False, p, f"未支持扩展对象 0x5B {code:02X} ({nm})"
        else:
            nm = TERM_NAME.get(op, f"UnknownOp{op:02X}")
            return False, p, f"外层未识别对象 0x{op:02X} ({nm})"
    return True, first[0] if first else -1, first[1] if first else "ok"


def main():
    corpus = sys.argv[1]
    fixtures = sys.argv[2]
    out = sys.argv[3]
    rows = []
    files = sorted(glob.glob(os.path.join(corpus, "*.aml")) +
                   glob.glob(os.path.join(corpus, "*.dat")) +
                   glob.glob(os.path.join(corpus, "firmware", "*.aml")) +
                   glob.glob(os.path.join(fixtures, "*.aml")))
    for path in files:
        b = open(path, "rb").read()
        if len(b) < 36 or b[0:4] not in (b"DSDT", b"SSDT"):
            continue
        ln = struct.unpack("<I", b[4:8])[0]
        if 36 <= ln <= len(b): b = b[:ln]
        body = b[36:]
        ok, off, desc = scan(body, len(body))
        rows.append({"file": os.path.basename(path), "size": len(b),
                     "build_ok": ok, "stop_offset": off, "reason": desc})
    with open(out, "w", encoding="utf-8") as f:
        f.write("file\tsize\tbuild_ok\tstop_offset\treason\n")
        for r in rows:
            f.write(f"{r['file']}\t{r['size']}\t{r['build_ok']}\t{r['stop_offset']}\t{r['reason']}\n")
    for r in rows:
        print(f"{r['file']:40s} ok={r['build_ok']} off={r['stop_offset']} {r['reason']}")


if __name__ == "__main__":
    main()
