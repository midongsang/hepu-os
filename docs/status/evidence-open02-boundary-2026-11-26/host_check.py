#!/usr/bin/env python3
"""
OPEN-02 边界① 宿主侧独立核对（不采信内核自报，直接读 raw 镜像 + 解析串口原文）。

做三件事：
  1. 按与内核相同的采样序列（步长 256 + 头 1..3 + 尾 8 扇区）独立重读镜像，
     逐字节比对确定性图案，复算 checksum_sum / checksum_xor；
  2. 对写读回环扇区（blocks-1）核对内核写入的回环图案；
  3. 额外用确定性 LCG 抽 32 个不在内核采样序列内的 LBA 独立 diff；
  4. 解析串口 [MSC-SWEEP] 行，与宿主复算结果逐项比对，输出 PASS/FAIL。

用法：
  python3 host_check.py <usb镜像> <串口日志> [报告输出]
"""
import os
import re
import sys

BLOCKS = 0x20000
BSIZE = 0x200
STRIDE = 256
SIGNATURE = b"OPEN02-A"


def expected_byte(lba: int, off: int) -> int:
    if lba == 0 and off < len(SIGNATURE):
        return SIGNATURE[off]
    return (lba & 0xFF) ^ ((lba >> 8) & 0xFF) ^ (off & 0xFF)


def writeback_byte(lba: int, off: int) -> int:
    return (lba * 31 + off * 13 + 0x5A) & 0xFF


def sampled_lbas(blocks: int, stride: int = STRIDE):
    seen, out = set(), []
    for l in range(0, blocks, stride):
        if l not in seen:
            seen.add(l); out.append(l)
    for l in (1, 2, 3):
        if l < blocks and l not in seen:
            seen.add(l); out.append(l)
    tail = blocks - 8 if blocks > 8 else 0
    for l in range(tail, blocks):
        if l not in seen:
            seen.add(l); out.append(l)
    return out


def main() -> int:
    img_path = sys.argv[1]
    ser_path = sys.argv[2]
    report = sys.argv[3] if len(sys.argv) > 3 else None

    lines = []
    def log(msg=""):
        print(msg)
        lines.append(msg)

    with open(img_path, "rb") as f:
        img = f.read()
    if len(img) != BLOCKS * BSIZE:
        log(f"[FAIL] 镜像大小 {len(img)} != {BLOCKS*BSIZE}")
        return 2

    def sector(lba: int) -> bytes:
        return img[lba * BSIZE:(lba + 1) * BSIZE]

    # ---- 1. 内核采样序列独立比对 ----
    # 注意：末扇区 wlba 在写读回环阶段被覆盖，原始图案校验排除它；
    # 校验和按“原始预期图案”复算（内核读遍历发生在写回环之前，读到的是原始扇区）。
    wlba = BLOCKS - 1
    lbas = sampled_lbas(BLOCKS)
    sum_cs = 0
    xor_cs = 0
    mism = []
    for lba in lbas:
        sec = sector(lba)
        for i in range(BSIZE):
            want = expected_byte(lba, i)
            sum_cs = (sum_cs + want) & 0xFFFFFFFF
            xor_cs ^= want
            if lba != wlba and sec[i] != want:
                mism.append((lba, i, want, sec[i]))
                if len(mism) >= 8:
                    break
        if len(mism) >= 8:
            break

    log(f"[HOST] sampled_lbas={len(lbas)} first={lbas[0]} last={lbas[-1]}")
    log(f"[HOST] pattern mismatch count (truncated at 8)={len(mism)}")
    for m in mism:
        log(f"  mismatch lba=0x{m[0]:X} off=0x{m[1]:X} want=0x{m[2]:02X} got=0x{m[3]:02X}")
    log(f"[HOST] checksum_sum=0x{sum_cs:08X} checksum_xor=0x{xor_cs:08X}")

    # ---- 2. 写读回环扇区（blocks-1）----
    wlba = BLOCKS - 1
    wsec = sector(wlba)
    wmism = [i for i in range(BSIZE) if wsec[i] != writeback_byte(wlba, i)]
    log(f"[HOST] writeback lba=0x{wlba:X} wmism={len(wmism)}")

    # ---- 3. 额外独立抽样（LCG，不与内核序列重合）----
    extra, state = [], 123456789
    while len(extra) < 32:
        state = (state * 1103515245 + 12345) & 0x7FFFFFFF
        l = state % BLOCKS
        if l == wlba or l in set(lbas):
            continue
        extra.append(l)
    extra_mism = []
    for lba in extra:
        sec = sector(lba)
        for i in range(BSIZE):
            if sec[i] != expected_byte(lba, i):
                extra_mism.append((lba, i))
                break
    log(f"[HOST] extra random samples={len(extra)} mismatch={len(extra_mism)}")

    # ---- 4. 解析串口 [MSC-SWEEP] 行 ----
    with open(ser_path, "r", errors="replace") as f:
        ser = f.read()
    m = re.search(r"\[MSC-SWEEP\][^\n]*", ser)
    if not m:
        log("[FAIL] 串口未找到 [MSC-SWEEP] 判定行")
        if report:
            open(report, "w").write("\n".join(lines) + "\n")
        return 3
    line = m.group(0)
    log(f"[SER ] {line}")

    def field(name, base=10):
        mm = re.search(name + r"=([0-9A-Za-z]+)", line)
        return int(mm.group(1), base) if mm else None

    blocks = field("blocks")
    bsize = field("bsize")
    stride = field("stride")
    sampled = field("sampled")
    read_err = field("read_err")
    mismatch = field("mismatch")
    wlba_s = field("wlba", 16)
    wrc = field("wrc", 16)
    rdrc = field("rdrc", 16)
    wmism_s = field("wmism")
    cs_sum = field("checksum_sum", 16)
    cs_xor = field("checksum_xor", 16)
    elapsed = field("elapsed_ms")

    checks = [
        ("blocks", blocks, BLOCKS),
        ("bsize", bsize, BSIZE),
        ("stride", stride, STRIDE),
        ("sampled", sampled, len(lbas)),
        ("read_err", read_err, 0),
        ("mismatch", mismatch, 0),
        ("wlba", wlba_s, wlba),
        ("wrc", wrc, 0),
        ("rdrc", rdrc, 0),
        ("wmism", wmism_s, 0),
        ("checksum_sum", cs_sum, sum_cs),
        ("checksum_xor", cs_xor, xor_cs),
    ]
    all_ok = not mism and not wmism and not extra_mism
    log("")
    for name, got, want in checks:
        ok = got == want
        all_ok &= ok
        log(f"[{'OK ' if ok else 'FAIL'}] {name}: serial={got} host={want}")

    log(f"[HOST] extra-sample mismatch={len(extra_mism)} (all_ok 计入)")
    log(f"[HOST] elapsed_ms(serial)={elapsed}")
    log("")
    log("RESULT: " + ("PASS" if all_ok else "FAIL"))

    if report:
        open(report, "w").write("\n".join(lines) + "\n")
    return 0 if all_ok else 1


if __name__ == "__main__":
    sys.exit(main())
