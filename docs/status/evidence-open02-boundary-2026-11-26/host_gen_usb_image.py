#!/usr/bin/env python3
"""
OPEN-02 边界① 宿主侧：生成 usb-storage 测试盘 raw 镜像（确定性图案）。

盘参数（与内核 READ CAPACITY 预期一致）：
  blocks = 0x20000 (131072 扇区)，bsize = 0x200 (512B)，共 64MB。

图案（必须与内核 usb64.c 内 HEPU_MSC_SWEEP_TEST 校验逻辑逐字节一致）：
  扇区 L、字节 i：
    L == 0 且 i < 8  -> 签名 "OPEN02-A" (4F 50 45 4E 30 32 2D 41)
    其余             -> (L & 0xFF) ^ ((L >> 8) & 0xFF) ^ (i & 0xFF)

用法：
  python3 host_gen_usb_image.py <输出镜像路径> [blocks] [bsize]
"""
import os
import sys

SIGNATURE = b"OPEN02-A"
BLOCKS = 0x20000
BSIZE = 0x200


def expected_byte(lba: int, off: int) -> int:
    if lba == 0 and off < len(SIGNATURE):
        return SIGNATURE[off]
    return (lba & 0xFF) ^ ((lba >> 8) & 0xFF) ^ (off & 0xFF)


def main() -> int:
    out = sys.argv[1] if len(sys.argv) > 1 else "open02-usb.img"
    blocks = int(sys.argv[2], 0) if len(sys.argv) > 2 else BLOCKS
    bsize = int(sys.argv[3], 0) if len(sys.argv) > 3 else BSIZE

    with open(out, "wb") as f:
        for lba in range(blocks):
            sector = bytes(expected_byte(lba, i) for i in range(bsize))
            f.write(sector)

    size = os.path.getsize(out)
    print(f"blocks={blocks:#x} bsize={bsize:#x} bytes={size}")
    print(f"LBA0 first8={open(out,'rb').read(8).hex()}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
