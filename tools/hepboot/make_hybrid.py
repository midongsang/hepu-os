#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
赫普OS - isohybrid 混合镜像构造器
把标准ISO转换为isohybrid：在ISO系统区(LBA0-15, 32768字节, 不参与ISO数据)
写入MBR引导扇区 + hybrid stage2，使同一镜像可：
  - 刻光盘   → El Torito (LBA20引导映像)
  - dd到U盘 → MBR → hybrid stage2 → 内核 (物理扇区84起)
  - Etcher/Rufus-DD → 同dd
ISO9660结构完全不受影响（系统区不参与ISO数据）。
"""
import struct
import sys

SECTOR = 2048

def make_hybrid(iso_path, mbr_bytes, stage2_bytes, out_path):
    iso = bytearray(open(iso_path, 'rb').read())

    # 系统区前512字节 = MBR
    assert len(mbr_bytes) == 512, f"MBR必须512字节, 实际{len(mbr_bytes)}"
    iso[0:512] = mbr_bytes

    # stage2 放在物理扇区1起（字节512），最多8块(4096字节) V6.50扩大支持VBE
    assert len(stage2_bytes) <= 4096, f"stage2超4096字节: {len(stage2_bytes)}"
    iso[512:512+len(stage2_bytes)] = stage2_bytes

    # MBR分区表：第1分区 = 整盘, type 0x17, LBA0起
    total_sectors = len(iso) // 512
    if total_sectors * 512 < len(iso):
        total_sectors += 1
    # 限制分区表大小字段
    if total_sectors > 0xFFFFFFFF:
        total_sectors = 0xFFFFFFFF
    pt = bytearray(16)
    pt[0] = 0x80                 # bootable
    pt[1:4] = b'\x00\x02\x00'    # CHS start (LBA0)
    pt[4] = 0x17                 # partition type: Hidden HPFS/NTFS (isohybrid标准)
    # CHS end: 计算大概
    # LBA -> CHS (假设 63 扇区/磁道, 255 磁头)
    end_lba = total_sectors - 1
    if end_lba > 0xFFFFFF:  # 8GB限制, CHS范围
        c = 1023; h = 254; s = 63
    else:
        s = (end_lba % 63) + 1
        h = (end_lba // 63) % 255
        c = end_lba // (63 * 255)
        if c > 1023: c = 1023
    pt[5] = h
    pt[6] = ((c >> 8) & 0xC0) | s
    pt[7] = c & 0xFF
    struct.pack_into('<I', pt, 8, 0)          # LBA start = 0
    struct.pack_into('<I', pt, 12, total_sectors)  # 扇区数
    iso[446:462] = pt

    open(out_path, 'wb').write(bytes(iso))
    return out_path, len(iso)

if __name__ == '__main__':
    if len(sys.argv) < 5:
        print("用法: make_hybrid.py <iso> <mbr> <stage2> <out>")
        sys.exit(1)
    mbr = open(sys.argv[2], 'rb').read()
    s2 = open(sys.argv[3], 'rb').read()
    path, size = make_hybrid(sys.argv[1], mbr, s2, sys.argv[4])
    print(f"hybrid ISO: {path} ({size}字节, {size//512}物理扇区)")
