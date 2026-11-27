#!/usr/bin/env python3
"""
OPEN-02 边界④ 宿主侧独立核对：usb-storage 全盘逐扇区读。

不采信内核自报：直接逐字节读 64MB USB 镜像，按
host_gen_usb_image.py 的确定性图案复算 131072 个扇区，
并与串口 [MSC-FULLREAD] 判定行逐项比对。

用法：
  python3 host_check_fullread.py <usb镜像> <串口日志> [报告输出]
"""
import re
import sys

BLOCKS = 0x20000          # 131072
BSIZE = 0x200
SIGNATURE = b"OPEN02-A"


def expected_byte(lba: int, off: int) -> int:
    if lba == 0 and off < len(SIGNATURE):
        return SIGNATURE[off]
    return (lba & 0xFF) ^ ((lba >> 8) & 0xFF) ^ (off & 0xFF)


def main() -> int:
    img_path, ser_path = sys.argv[1], sys.argv[2]
    report = sys.argv[3] if len(sys.argv) > 3 else None

    lines = []

    def log(msg=""):
        print(msg)
        lines.append(msg)

    with open(img_path, "rb") as f:
        img = f.read()
    if len(img) != BLOCKS * BSIZE:
        log(f"[FAIL] 镜像大小 {len(img)} != {BLOCKS * BSIZE}")
        return 2

    mism = 0
    first_bad = None
    sum_cs = 0
    xor_cs = 0
    for lba in range(BLOCKS):
        base = lba * BSIZE
        for i in range(BSIZE):
            want = expected_byte(lba, i)
            got = img[base + i]
            sum_cs = (sum_cs + got) & 0xFFFFFFFF
            xor_cs ^= got
            if got != want:
                mism += 1
                if first_bad is None:
                    first_bad = (lba, i, want, got)

    log(f"[HOST] image_bytes={len(img)} blocks={BLOCKS} bsize={BSIZE}")
    log(f"[HOST] full-byte mismatch={mism}")
    if first_bad:
        log(
            "[HOST] first_bad lba=0x%X off=0x%X want=0x%02X got=0x%02X"
            % first_bad
        )
    log(f"[HOST] checksum_sum=0x{sum_cs:08X} checksum_xor=0x{xor_cs:02X}")

    ser = open(ser_path, "r", errors="replace").read()
    m = re.search(r"\[MSC-FULLREAD\] blocks=[^\n]*", ser)
    if not m:
        log("[FAIL] 串口未找到 [MSC-FULLREAD] 判定行")
        if report:
            open(report, "w").write("\n".join(lines) + "\n")
        return 3
    line = m.group(0)
    log(f"[SER ] {line}")

    def field(name, base=10):
        mm = re.search(name + r"=([0-9A-Za-z]+)", line)
        return int(mm.group(1), base) if mm else None

    checks = [
        ("blocks", field("blocks"), BLOCKS),
        ("chunk", field("chunk"), 16),
        ("rounds", field("rounds"), BLOCKS // 16),
        ("sampled", field("sampled"), BLOCKS),
        ("read_err", field("read_err"), 0),
        ("mismatch", field("mismatch"), 0),
        ("checksum_sum", field("checksum_sum", 16), sum_cs),
        ("checksum_xor", field("checksum_xor", 16), xor_cs),
    ]

    log("")
    all_ok = mism == 0
    for name, got, want in checks:
        ok = got == want
        all_ok &= ok
        log(f"[{'OK ' if ok else 'FAIL'}] {name}: serial={got} host={want}")

    log(f"[HOST] elapsed_ms(serial)={field('elapsed_ms')}")
    log("")
    log("RESULT: " + ("PASS" if all_ok else "FAIL"))

    if report:
        open(report, "w").write("\n".join(lines) + "\n")
    return 0 if all_ok else 1


if __name__ == "__main__":
    sys.exit(main())
