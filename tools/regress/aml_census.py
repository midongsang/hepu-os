#!/usr/bin/env python3
"""AML 语料结构普查（离线、只读，不执行解释器）。

对每张 DSDT/SSDT 二进制表做尽力而为的 AML 遍历，统计：
  - 扩展对象（0x5B xx）出现频次与涉及表；
  - 常见单字节操作码频次；
  - Field / IndexField / BankField 的字段位宽分布；
  - 首个“当前解释器不支持”的对象，并区分上下文：
      build —— 命名空间建库期（Scope/Device/Method 外层）
      eval  —— 方法体内部（求值期才会触发）

操作码口径以 ACPICA source/include/amlcode.h 为准；支持/不支持口径来自
HEAD 快照 kernel/arch/x86_64/aml64.c 与 aml64.h 顶部注释。

口径与局限：
  - 遍历是 best-effort：方法体内表达式不做完整 AST 展开；单字节操作码计数
    存在把数据字节误判为操作码的可能（启发式）；0x5B 双字节扩展对象计数
    可信度高于单字节统计。
  - build-skipped 表示建库期整段跳过（IndexField/BankField），节点未注册。

用法:
  aml_census.py --corpus DIR --fixtures DIR --evidence DIR [--manifest TSV]
"""
import argparse
import json
import os
import struct
import sys

# 0x5B 扩展操作码: 名称, 口径
EXT = {
    0x01: ("Mutex", "build-supported"),
    0x02: ("Event", "build-supported"),
    0x10: ("ShiftRightBit(obsolete)", "unsupported"),
    0x11: ("ShiftLeftBit(obsolete)", "unsupported"),
    0x12: ("CondRefOf", "eval-partial"),
    0x13: ("CreateField", "unsupported"),
    0x1F: ("LoadTable", "unsupported"),
    0x20: ("Load", "unsupported"),
    0x21: ("Stall", "eval-unsupported"),
    0x22: ("Sleep", "eval-unsupported"),
    0x23: ("Acquire", "eval-supported"),
    0x24: ("Signal", "unsupported"),
    0x25: ("Wait", "eval-unsupported"),
    0x26: ("Reset", "unsupported"),
    0x27: ("Release", "eval-supported"),
    0x28: ("FromBcd", "unsupported"),
    0x29: ("ToBcd", "unsupported"),
    0x2A: ("Unload", "unsupported"),
    0x30: ("Revision", "eval-supported"),
    0x31: ("Debug", "eval-supported"),
    0x32: ("Fatal", "unsupported"),
    0x33: ("Timer", "unsupported"),
    0x80: ("OperationRegion", "build-supported"),
    0x81: ("Field", "build-supported"),
    0x82: ("Device", "build-supported"),
    0x83: ("Processor", "build-supported"),
    0x84: ("PowerRes", "build-supported"),
    0x85: ("ThermalZone", "build-supported"),
    0x86: ("IndexField", "build-skipped"),
    0x87: ("BankField", "build-skipped"),
    0x88: ("DataRegion", "unsupported"),
}

# 单字节操作码: 名称, 口径
TERM = {
    0x10: ("Scope", "build-supported"),
    0x14: ("Method", "build-supported"),
    0x08: ("Name", "build-supported"),
    0x06: ("Alias", "build-supported"),
    0x15: ("External", "build-supported"),
    0x11: ("Buffer", "supported"),
    0x12: ("Package", "supported"),
    0x13: ("VarPackage", "supported"),
    0x70: ("Store", "supported"),
    0x9D: ("CopyObject", "supported"),
    0xA0: ("If", "supported"),
    0xA1: ("Else", "supported"),
    0xA2: ("While", "supported"),
    0xA4: ("Return", "supported"),
    0xA5: ("Break", "supported"),
    0xA3: ("Noop", "supported"),
    0x83: ("DerefOf", "supported"),
    0x87: ("SizeOf", "supported"),
    0x88: ("Index", "supported"),
    0x72: ("Add", "supported"),
    0x74: ("Subtract", "supported"),
    0x77: ("Multiply", "supported"),
    0x79: ("ShiftLeft", "supported"),
    0x7A: ("ShiftRight", "supported"),
    0x7B: ("And", "supported"),
    0x7C: ("NAnd", "supported"),
    0x7D: ("Or", "supported"),
    0x7E: ("NOr", "supported"),
    0x7F: ("Xor", "supported"),
    0x80: ("Not", "supported"),
    0x75: ("Increment", "supported"),
    0x76: ("Decrement", "supported"),
    0x85: ("Mod", "supported"),
    0x90: ("LAnd", "supported"),
    0x91: ("LOr", "supported"),
    0x92: ("LNot", "supported"),
    0x93: ("LEqual", "supported"),
    0x94: ("LGreater", "supported"),
    0x95: ("LLess", "supported"),
    0x99: ("ToInteger", "supported"),
    # ---- HEAD 未实现 / 仅部分支持 ----
    0x71: ("RefOf", "unsupported"),
    0x73: ("Concatenate", "unsupported"),
    0x78: ("Divide", "unsupported"),
    0x81: ("FindSetLeftBit", "unsupported"),
    0x82: ("FindSetRightBit", "unsupported"),
    0x84: ("ConcatenateTemplate", "unsupported"),
    0x86: ("Notify", "unsupported"),
    0x89: ("Match", "unsupported"),
    0x8A: ("CreateDWordField", "unsupported"),
    0x8B: ("CreateWordField", "unsupported"),
    0x8C: ("CreateByteField", "unsupported"),
    0x8D: ("CreateBitField", "unsupported"),
    0x8E: ("ObjectType", "unsupported"),
    0x8F: ("CreateQWordField", "unsupported"),
    0x96: ("ToBuffer", "unsupported"),
    0x97: ("ToDecimalString", "unsupported"),
    0x98: ("ToHexString", "unsupported"),
    0x9C: ("ToString", "unsupported"),
    0x9E: ("Mid", "unsupported"),
    0x9F: ("Continue", "unsupported"),
    0xCC: ("BreakPoint", "unsupported"),
}

UNSUPPORTED = {"unsupported", "build-skipped", "eval-unsupported", "eval-partial"}


class Walk:
    def __init__(self, blob, table):
        self.b = blob
        self.end = len(blob)
        self.table = table
        self.ext = {}
        self.terms = {}
        self.eval_ext = {}
        self.eval_term = {}
        self.fields = []
        self.first = {"build": None, "eval": None}
        self.ok = True

    def _tag(self, status, off, name, context):
        if status in UNSUPPORTED and self.first[context] is None:
            self.first[context] = (off, name)

    def add_ext(self, code, off, context):
        name, status = EXT.get(code, (f"UnknownExt{code:02X}", "unsupported"))
        d = self.ext.setdefault(code, {"count": 0, "status": status,
                                      "name": name, "tables": set()})
        d["count"] += 1
        d["tables"].add(self.table)
        self._tag(status, off, name, context)

    def add_term(self, op, context):
        name, status = TERM.get(op, (f"Unknown{op:02X}", "unsupported"))
        d = self.terms.setdefault(op, {"count": 0, "status": status,
                                      "name": name, "tables": set()})
        d["count"] += 1
        d["tables"].add(self.table)
        self._tag(status, 0, name, context)

    def pkg(self, p):
        if p >= self.end:
            return None
        b0 = self.b[p]
        lead = b0 >> 6
        if lead == 0:
            length = b0 & 0x3F
            hsize = 1
        else:
            if p + lead >= self.end:
                return None
            length = b0 & 0x0F
            for i in range(1, lead + 1):
                length |= self.b[p + i] << (4 + 8 * (i - 1))
            hsize = 1 + lead
        body_end = p + length
        if body_end > self.end:
            return None
        return length, body_end, p + hsize

    def namestr(self, p):
        while p < self.end and self.b[p] == 0x5E:
            p += 1
        if p < self.end and self.b[p] == 0x5C:
            p += 1
        if p >= self.end:
            return None
        if self.b[p] == 0x2E:
            n = 2
            p += 1
        elif self.b[p] == 0x2F:
            if p + 1 >= self.end:
                return None
            n = self.b[p + 1]
            p += 2
        else:
            n = 1
        p += 4 * n
        return p if p <= self.end else None

    def const(self, p):
        op = self.b[p]
        if op in (0x00, 0x01, 0xFF):
            return p + 1
        if op == 0x0A:
            return p + 2
        if op == 0x0B:
            return p + 3
        if op == 0x0C:
            return p + 5
        if op == 0x0E:
            return p + 9
        return None

    def data(self, p):
        op = self.b[p]
        if op in (0x00, 0x01, 0xFF, 0x0A, 0x0B, 0x0C, 0x0E):
            return self.const(p)
        if op == 0x0D:
            if p + 2 >= self.end:
                return None
            return p + 3 + self.b[p + 1]
        if op == 0x11:
            r = self.pkg(p + 1)
            return r[1] if r else None
        if op in (0x12, 0x13):
            r = self.pkg(p + 1)
            return r[1] if r else None
        if 0x41 <= op <= 0x5B or op in (0x5C, 0x5E, 0x2E, 0x2F):
            return self.namestr(p)
        return None

    def fieldlist(self, p, body_end, op_name):
        while p < body_end:
            b0 = self.b[p]
            if b0 == 0x00:
                r = self.pkg(p + 1)
                if not r:
                    return p
                p = r[1]
            elif b0 in (0x01, 0x03):
                p += 3 if b0 == 0x01 else 4
            else:
                if p + 4 > body_end:
                    return p
                p += 4
                r = self.pkg(p)
                if not r:
                    return p
                bits = r[0] - (r[2] - p)
                if bits > 0:
                    self.fields.append((op_name, bits))
                p = r[1]
        return p

    def raw_scan(self):
        """方法体/全表原始字节启发式扫描：无法区分代码与数据，仅作线索。"""
        body = self.b[36:]
        i = 0
        while i < len(body):
            if body[i] == 0x5B and i + 1 < len(body):
                code = body[i + 1]
                name, status = EXT.get(code, (f"UnknownExt{code:02X}", "unsupported"))
                d = self.eval_ext.setdefault(code, {"count": 0, "status": status,
                                                   "name": name})
                d["count"] += 1
                if status in UNSUPPORTED and self.first["eval"] is None:
                    self.first["eval"] = (i + 36, name)
                i += 2
            else:
                op = body[i]
                if op in TERM and TERM[op][1] in UNSUPPORTED:
                    name, status = TERM[op]
                    d = self.eval_term.setdefault(op, {"count": 0, "status": status,
                                                       "name": name})
                    d["count"] += 1
                    if self.first["eval"] is None:
                        self.first["eval"] = (i + 36, name)
                i += 1

    def walk_terms(self, p, body_end, depth=0):
        # 结构化遍历只覆盖建库期外层结构；方法体整体跳过，避免表达式解析失同步。
        if depth > 16:
            return
        ctx = "build"
        while p < body_end:
            op = self.b[p]
            if op == 0x5B:
                if p + 1 >= body_end:
                    return
                code = self.b[p + 1]
                off = p
                p += 2
                self.add_ext(code, off, ctx)
                if code in (0x82, 0x83, 0x84, 0x85):
                    r = self.pkg(p)
                    if not r:
                        return
                    _, be, p = r
                    p = self.namestr(p)
                    if p is None:
                        return
                    if code == 0x83:
                        p += 6
                    elif code == 0x84:
                        p += 3
                    if p > be:
                        return
                    self.walk_terms(p, be, depth + 1)
                    p = be
                elif code == 0x80:
                    r = self.pkg(p)
                    if not r:
                        return
                    _, be, p = r
                    p = self.namestr(p)
                    if p is None:
                        return
                    if p < be:
                        p += 1
                        c = self.const(p)
                        p = c if c else p
                        if p and p < be:
                            c = self.const(p)
                            p = c if c else be
                    p = be
                elif code == 0x81:
                    r = self.pkg(p)
                    if not r:
                        return
                    _, be, p = r
                    p = self.namestr(p)
                    if p is None:
                        return
                    if p < be:
                        p += 1
                        p = self.fieldlist(p, be, "Field")
                    p = be
                elif code in (0x86, 0x87):
                    r = self.pkg(p)
                    if not r:
                        return
                    _, be, p = r
                    p = self.namestr(p)
                    if p is None:
                        return
                    p = self.namestr(p)
                    if p is None:
                        return
                    if p < be:
                        p += 1
                        p = self.fieldlist(p, be, EXT[code][0])
                    p = be
                elif code == 0x88:
                    r = self.pkg(p)
                    if not r:
                        return
                    _, be, p = r
                    p = self.namestr(p)
                    if p is None:
                        return
                    if p < be:
                        p += 1
                        p = self.fieldlist(p, be, "DataRegion")
                    p = be
                elif code == 0x01:
                    p = self.namestr(p)
                    if p is not None:
                        p += 1
                elif code == 0x02:
                    p = self.namestr(p)
                elif code == 0x12:
                    p = self.namestr(p)
                elif code == 0x23:
                    p = self.namestr(p)
                    c = self.const(p)
                    p = c if c else p
                elif code == 0x27:
                    p = self.namestr(p)
                else:
                    return
            elif op == 0x10:
                r = self.pkg(p + 1)
                if not r:
                    return
                _, be, q = r
                q = self.namestr(q)
                if q is None:
                    return
                self.walk_terms(q, be, depth + 1)
                p = be
            elif op == 0x14:
                # 方法体整体跳过：方法内表达式由原始字节启发式扫描统计。
                r = self.pkg(p + 1)
                if not r:
                    return
                _, be, q = r
                p = be
            elif op == 0x08:
                q = self.namestr(p + 1)
                if q is None:
                    return
                d = self.data(q)
                p = d if d else q
            elif op == 0x06:
                q = self.namestr(p + 1)
                if q is None:
                    return
                p = self.namestr(q)
                if p is None:
                    return
            elif op == 0x15:
                q = self.namestr(p + 1)
                if q is None:
                    return
                p = q + 2
            elif op in (0x11, 0x12, 0x13):
                r = self.pkg(p + 1)
                if not r:
                    return
                p = r[1]
            elif op in (0xA0, 0xA2):
                r = self.pkg(p + 1)
                if not r:
                    return
                _, be, _ = r
                p = be
            elif op in TERM:
                self.add_term(op, ctx)
                p += 1
            else:
                p += 1


def read_blob(path):
    b = open(path, "rb").read()
    if len(b) < 36:
        return None
    sig = b[0:4]
    if sig not in (b"DSDT", b"SSDT"):
        return None
    ln = struct.unpack("<I", b[4:8])[0]
    if 36 <= ln <= len(b):
        b = b[:ln]
    return b


def load_manifest(path):
    rows = {}
    if not path or not os.path.isfile(path):
        return rows
    with open(path, encoding="utf-8") as f:
        header = f.readline().rstrip("\n").split("\t")
        for line in f:
            cols = line.rstrip("\n").split("\t")
            row = dict(zip(header, cols))
            rows[row["name"]] = row
    return rows


def bucket(bits):
    if bits <= 8:
        return "<=8"
    if bits <= 16:
        return "9-16"
    if bits <= 32:
        return "17-32"
    if bits <= 64:
        return "33-64"
    return ">64"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--corpus", required=True)
    ap.add_argument("--fixtures", required=True)
    ap.add_argument("--evidence", required=True)
    ap.add_argument("--manifest", default="")
    args = ap.parse_args()

    manifest = load_manifest(args.manifest)
    files = []
    for d in (args.corpus, os.path.join(args.corpus, "firmware"), args.fixtures):
        if d and os.path.isdir(d):
            for name in sorted(os.listdir(d)):
                if name.endswith((".aml", ".dat")):
                    files.append(os.path.join(d, name))

    ext_agg = {}
    term_agg = {}
    eval_ext_agg = {}
    eval_term_agg = {}
    field_agg = {}
    per_table = []

    for path in files:
        blob = read_blob(path)
        name = os.path.basename(path)
        if blob is None:
            continue
        w = Walk(blob, name)
        w.walk_terms(0, len(blob[36:]))
        w.raw_scan()
        per_table.append({
            "table": name,
            "kind": manifest.get(name, {}).get("kind", "?"),
            "size": len(blob),
            "first_build": w.first["build"],
            "first_eval": w.first["eval"],
        })
        for code, info in w.ext.items():
            a = ext_agg.setdefault(code, {"count": 0, "tables": set(),
                                          "name": info["name"],
                                          "status": info["status"]})
            a["count"] += info["count"]
            a["tables"].add(name)
        for op, info in w.terms.items():
            t = term_agg.setdefault(op, {"count": 0, "tables": set(),
                                         "name": info["name"],
                                         "status": info["status"]})
            t["count"] += info["count"]
            t["tables"].add(name)
        for code, info in w.eval_ext.items():
            a = eval_ext_agg.setdefault(code, {"count": 0, "tables": set(),
                                               "name": info["name"],
                                               "status": info["status"]})
            a["count"] += info["count"]
            a["tables"].add(name)
        for op, info in w.eval_term.items():
            t = eval_term_agg.setdefault(op, {"count": 0, "tables": set(),
                                              "name": info["name"],
                                              "status": info["status"]})
            t["count"] += info["count"]
            t["tables"].add(name)
        for op, bits in w.fields:
            f = field_agg.setdefault(op, {"buckets": {}, "tables": set()})
            f["buckets"][bucket(bits)] = f["buckets"].get(bucket(bits), 0) + 1
            f["tables"].add(name)

    os.makedirs(args.evidence, exist_ok=True)

    op_path = os.path.join(args.evidence, "census-opcodes.tsv")
    with open(op_path, "w", encoding="utf-8") as f:
        f.write("category\topcode\topname\tsupport\tcount\ttables\ttable_list\n")
        for code, a in sorted(ext_agg.items(), key=lambda kv: -kv[1]["count"]):
            f.write("build-ext\t0x%02X\t%s\t%s\t%d\t%d\t%s\n" % (
                code, a["name"], a["status"], a["count"], len(a["tables"]),
                ",".join(sorted(a["tables"]))))
        for op, a in sorted(term_agg.items(), key=lambda kv: -kv[1]["count"]):
            f.write("build-term\t0x%02X\t%s\t%s\t%d\t%d\t%s\n" % (
                op, a["name"], a["status"], a["count"], len(a["tables"]),
                ",".join(sorted(a["tables"]))))
        for code, a in sorted(eval_ext_agg.items(), key=lambda kv: -kv[1]["count"]):
            f.write("eval-ext*\t0x%02X\t%s\t%s\t%d\t%d\t%s\n" % (
                code, a["name"], a["status"], a["count"], len(a["tables"]),
                ",".join(sorted(a["tables"]))))
        for op, a in sorted(eval_term_agg.items(), key=lambda kv: -kv[1]["count"]):
            f.write("eval-term*\t0x%02X\t%s\t%s\t%d\t%d\t%s\n" % (
                op, a["name"], a["status"], a["count"], len(a["tables"]),
                ",".join(sorted(a["tables"]))))

    fld_path = os.path.join(args.evidence, "census-fields.tsv")
    with open(fld_path, "w", encoding="utf-8") as f:
        f.write("field_op\tcount\t<=8\t9-16\t17-32\t33-64\t>64\ttables\n")
        for op, a in sorted(field_agg.items()):
            b = a["buckets"]
            f.write("%s\t%d\t%d\t%d\t%d\t%d\t%d\t%s\n" % (
                op, sum(b.values()), b.get("<=8", 0), b.get("9-16", 0),
                b.get("17-32", 0), b.get("33-64", 0), b.get(">64", 0),
                ",".join(sorted(a["tables"]))))

    unsup_ext = [(c, a) for c, a in ext_agg.items() if a["status"] in UNSUPPORTED]
    unsup_ext.sort(key=lambda kv: -kv[1]["count"])
    unsup_eval_ext = [(c, a) for c, a in eval_ext_agg.items()
                      if a["status"] in UNSUPPORTED]
    unsup_eval_ext.sort(key=lambda kv: -kv[1]["count"])
    unsup_eval_term = [(c, a) for c, a in eval_term_agg.items()
                       if a["status"] in UNSUPPORTED]
    unsup_eval_term.sort(key=lambda kv: -kv[1]["count"])

    md = os.path.join(args.evidence, "census-summary.md")
    with open(md, "w", encoding="utf-8") as f:
        f.write("# 结构普查汇总（%d 张表）\n\n" % len(per_table))
        f.write("> 口径：`build-*` 来自结构化外层遍历（可信度较高）；"
                "`eval-*` 来自全表原始字节启发式扫描，无法区分代码与数据，"
                "频次只作线索，不能单独作为缺口结论。\n\n")
        f.write("## A. 建库期外层结构中不支持/跳过的扩展对象\n\n")
        f.write("| 操作码 | 名称 | 口径 | 出现次数 | 涉及表数 |\n|---|---|---|---|---|\n")
        for code, a in unsup_ext:
            f.write("| 0x%02X | %s | %s | %d | %d |\n" % (
                code, a["name"], a["status"], a["count"], len(a["tables"])))
        f.write("\n## B. 求值期扩展对象（启发式扫描，线索）\n\n")
        f.write("| 操作码 | 名称 | 口径 | 出现次数 | 涉及表数 |\n|---|---|---|---|---|\n")
        for code, a in unsup_eval_ext:
            f.write("| 0x%02X | %s | %s | %d | %d |\n" % (
                code, a["name"], a["status"], a["count"], len(a["tables"])))
        f.write("\n## C. 求值期单字节操作码（启发式扫描，线索）\n\n")
        f.write("| 操作码 | 名称 | 口径 | 出现次数 | 涉及表数 |\n|---|---|---|---|---|\n")
        for code, a in unsup_eval_term:
            f.write("| 0x%02X | %s | %s | %d | %d |\n" % (
                code, a["name"], a["status"], a["count"], len(a["tables"])))
        f.write("\n## 逐表首个未支持对象（build / eval 上下文）\n\n")
        f.write("| 表 | 子类 | 大小 | build 首个 | eval 首个(启发式) |\n|---|---|---|---|---|\n")
        for t in per_table:
            fb = t["first_build"]
            fe = t["first_eval"]
            f.write("| %s | %s | %d | %s | %s |\n" % (
                t["table"], t["kind"], t["size"],
                f"{fb[1]}@0x{fb[0]:x}" if fb else "-",
                f"{fe[1]}@0x{fe[0]:x}" if fe else "-"))

    with open(os.path.join(args.evidence, "census-per-table.json"), "w",
              encoding="utf-8") as f:
        json.dump(per_table, f, ensure_ascii=False, indent=1)

    print("tables=%d build_ext=%d eval_ext=%d eval_term=%d" % (
        len(per_table), len(ext_agg), len(eval_ext_agg), len(eval_term_agg)))


if __name__ == "__main__":
    sys.exit(main())
