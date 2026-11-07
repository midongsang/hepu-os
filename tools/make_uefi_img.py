#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""把 BIOS 单盘镜像转换为 BIOS + UEFI 双启动混合镜像

输入: tools/make_single_img64.sh 产出的 /tmp/hepu64_single.img
输出: /tmp/hepu64_uefi.img

布局(扇区):
  LBA 0        MBR 引导代码 + 保护性分区表(type 0xEE)  -> Legacy BIOS 启动
  LBA 1-33     GPT 主头 + 分区项
  LBA 34-65    stage2 (BIOS 引导链, 保持原样)
  LBA 2000     64 位内核 (BIOS 引导链, 保持原样)
  LBA 13000    ext2 根分区 (内核硬编码 g_root_lba=13000)
  LBA ESP_START  FAT32 ESP (EFI/BOOT/BOOTX64.EFI + KERNEL64.BIN + HEPBOOT.CFG)
  末尾 33 扇区  GPT 备份分区项 + 备份头

不依赖 mkfs.vfat / parted / sgdisk，纯 Python 实现。
"""
import os
import shutil
import struct
import sys
import uuid
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from fat32_generator import FAT32Generator

PROJECT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

BASE_IMG = os.environ.get("HEPU_BASE_IMG", "/tmp/hepu64_single.img")
OUT_IMG = os.environ.get("HEPU_UEFI_IMG", "/tmp/hepu64_uefi.img")

EXT2_LBA = 13000
EXT2_END = 471751          # 224MB ext2 分区结束扇区 (make_single_img64.sh)
ESP_LBA = 475136           # 8 扇区对齐, 位于 ext2 之后
ESP_MB = 64
ESP_SECTORS = ESP_MB * 2048
TOTAL_SECTORS = ESP_LBA + ESP_SECTORS + 34   # 末尾预留 34 扇区给 GPT 备份

ESP_TYPE_GUID = uuid.UUID("C12A7328-F81F-11D2-BA4B-00A0C93EC93B").bytes_le
LINUX_TYPE_GUID = uuid.UUID("0FC63DAF-8483-4772-8E79-3D69D8477DE4").bytes_le

SECTOR = 512


def build_esp(files):
    """生成 FAT32 ESP 卷镜像"""
    fat = FAT32Generator(ESP_SECTORS * SECTOR, sectors_per_cluster=1)
    for path, data in files.items():
        fat.add_file(path, data)
    return fat.generate()


def gpt_entry(type_guid, part_guid, first_lba, last_lba, name, attrs=0):
    e = bytearray(128)
    e[0:16] = type_guid
    e[16:32] = part_guid
    struct.pack_into("<Q", e, 32, first_lba)
    struct.pack_into("<Q", e, 40, last_lba)
    struct.pack_into("<Q", e, 48, attrs)
    encoded = name.encode("utf-16-le")
    e[56:56 + min(len(encoded), 72)] = encoded[:72]
    return bytes(e)


def gpt_header(current_lba, backup_lba, entries_lba, disk_guid, entries_crc):
    h = bytearray(SECTOR)
    h[0:8] = b"EFI PART"
    struct.pack_into("<I", h, 8, 0x00010000)      # revision
    struct.pack_into("<I", h, 12, 92)             # header size
    struct.pack_into("<I", h, 16, 0)              # crc32 placeholder
    struct.pack_into("<Q", h, 24, current_lba)
    struct.pack_into("<Q", h, 32, backup_lba)
    struct.pack_into("<Q", h, 40, 34)             # first usable LBA
    struct.pack_into("<Q", h, 48, TOTAL_SECTORS - 34)  # last usable LBA
    h[56:72] = disk_guid
    struct.pack_into("<Q", h, 72, entries_lba)
    struct.pack_into("<I", h, 80, 128)            # number of entries
    struct.pack_into("<I", h, 84, 128)            # entry size
    struct.pack_into("<I", h, 88, entries_crc)
    struct.pack_into("<I", h, 16, zlib.crc32(bytes(h[:92])) & 0xFFFFFFFF)
    return bytes(h)


def write_protective_mbr(img_path):
    """保留 MBR 引导代码, 只替换分区表为保护性 GPT 项"""
    with open(img_path, "r+b") as f:
        f.seek(0)
        mbr = bytearray(f.read(SECTOR))
        if mbr[510:512] != b"\x55\xAA":
            raise SystemExit("错误: 输入镜像 MBR 缺少 0x55AA 签名")
        # 保护性分区: type 0xEE, 覆盖整盘 (从 LBA1 到末尾)
        pte = bytearray(16)
        pte[0] = 0x80                       # 激活, 便于个别 Legacy BIOS
        pte[1:4] = bytes([0x00, 0x02, 0x00])  # CHS start
        pte[4] = 0xEE
        pte[5:8] = bytes([0xFF, 0xFF, 0xFF])  # CHS end
        struct.pack_into("<I", pte, 8, 1)
        struct.pack_into("<I", pte, 12, min(TOTAL_SECTORS - 1, 0xFFFFFFFF))
        mbr[446:462] = pte
        mbr[462:510] = b"\x00" * 48
        f.seek(0)
        f.write(mbr)


def write_gpt(img_path, ext2_first, ext2_last):
    entries = bytearray(128 * 128)
    entries[0:128] = gpt_entry(
        ESP_TYPE_GUID, uuid.uuid4().bytes_le,
        ESP_LBA, ESP_LBA + ESP_SECTORS - 1, "HepuOS ESP", attrs=0)
    entries[128:256] = gpt_entry(
        LINUX_TYPE_GUID, uuid.uuid4().bytes_le,
        ext2_first, ext2_last, "HepuOS Root")
    entries_crc = zlib.crc32(bytes(entries)) & 0xFFFFFFFF
    disk_guid = uuid.uuid4().bytes_le

    primary = gpt_header(1, TOTAL_SECTORS - 1, 2, disk_guid, entries_crc)
    backup = gpt_header(TOTAL_SECTORS - 1, 1, TOTAL_SECTORS - 33,
                        disk_guid, entries_crc)

    with open(img_path, "r+b") as f:
        f.seek(1 * SECTOR)
        f.write(primary)
        f.seek(2 * SECTOR)
        f.write(entries)
        f.seek((TOTAL_SECTORS - 33) * SECTOR)
        f.write(entries)
        f.seek((TOTAL_SECTORS - 1) * SECTOR)
        f.write(backup)


def find_ext2_partition(img_path):
    """从基础镜像的 MBR 分区表读 ext2 (0x83) 分区范围"""
    with open(img_path, "rb") as f:
        f.seek(446)
        for _ in range(4):
            e = f.read(16)
            if e[4] == 0x83:
                start = struct.unpack_from("<I", e, 8)[0]
                size = struct.unpack_from("<I", e, 12)[0]
                return start, start + size - 1
    return EXT2_LBA, EXT2_END


def main():
    if not os.path.exists(BASE_IMG):
        sys.exit("找不到基础镜像 %s, 请先运行 tools/make_single_img64.sh" % BASE_IMG)
    ext2_first, ext2_last = find_ext2_partition(BASE_IMG)
    if ext2_last >= ESP_LBA:
        sys.exit("ext2 分区结束于 LBA%d, 超出 ESP 起始 LBA%d" % (ext2_last, ESP_LBA))
    base_size = os.path.getsize(BASE_IMG)
    if base_size < (ext2_last + 1) * SECTOR:
        sys.exit("基础镜像太小, ext2 分区不完整")

    efi = os.path.join(PROJECT, "tools/hepboot/uefi/hepboot_uefi.efi")
    kernel64 = os.path.join(PROJECT, "build_x86_64/hepu-kernel64.bin")
    for p in (efi, kernel64):
        if not os.path.exists(p):
            sys.exit("缺少文件: %s" % p)

    files = {
        "/EFI/BOOT/BOOTX64.EFI": open(efi, "rb").read(),
        "/KERNEL64.BIN": open(kernel64, "rb").read(),
        "/HEPBOOT.CFG": b"timeout=3\ndefault=0\nkernel_arch=64\ncmdline=\n",
    }
    kernel32 = os.path.join(PROJECT, "build_v250/hepu-kernel.bin")
    if os.path.exists(kernel32):
        files["/KERNEL32.BIN"] = open(kernel32, "rb").read()

    print("[1/5] 复制基础镜像 -> %s" % OUT_IMG)
    shutil.copyfile(BASE_IMG, OUT_IMG)

    print("[2/5] 扩展镜像至 %.0fMB (ESP LBA %d..%d)"
          % (TOTAL_SECTORS * SECTOR / 1048576, ESP_LBA, ESP_LBA + ESP_SECTORS - 1))
    with open(OUT_IMG, "r+b") as f:
        f.truncate(TOTAL_SECTORS * SECTOR)

    print("[3/5] 生成 FAT32 ESP (%dMB)" % ESP_MB)
    esp = build_esp(files)
    assert len(esp) == ESP_SECTORS * SECTOR
    with open(OUT_IMG, "r+b") as f:
        f.seek(ESP_LBA * SECTOR)
        f.write(esp)
    for p, contents in files.items():
        print("        %-22s %8d 字节" % (p, len(contents)))

    print("[4/5] 写入保护性 MBR + GPT 分区表")
    write_protective_mbr(OUT_IMG)
    write_gpt(OUT_IMG, ext2_first, ext2_last)

    print("[5/5] 完成: %s (%.1fMB)" % (OUT_IMG, os.path.getsize(OUT_IMG) / 1048576))


if __name__ == "__main__":
    main()
