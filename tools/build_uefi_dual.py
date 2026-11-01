#!/usr/bin/env python3
"""
HepuOS 双内核 UEFI+GPT 镜像构建脚本
使用 pyfatfs 创建 FAT32 ESP 分区
"""
import struct
import os
import sys
import subprocess
import zlib
import uuid
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from fat32_generator import create_esp_partition

PROJECT = "/home/user/Doubao/chats/38442532206397698/hepu_os_src/hepu-os"
IMG_PATH = "/tmp/hepu_uefi_dual.img"
IMG_SIZE = 256 * 1024 * 1024  # 256MB

# 分区布局
ESP_START = 2048          # 1MiB
ESP_SIZE = 128 * 1024 * 1024  # 128MB
ESP_SECTORS = ESP_SIZE // 512
DATA_START = ESP_START + ESP_SECTORS
DATA_SIZE = 120 * 1024 * 1024  # 120MB
DATA_SECTORS = DATA_SIZE // 512

def create_gpt_partition_table(img):
    """创建GPT分区表"""
    # Protective MBR (LBA 0)
    mbr = bytearray(512)
    mbr[446] = 0x00  # boot flag
    mbr[447] = 0x00  # start CHS
    mbr[448] = 0x02
    mbr[449] = 0x00
    mbr[450] = 0xEE  # GPT protective
    mbr[451] = 0xFF  # end CHS
    mbr[452] = 0xFF
    mbr[453] = 0xFF
    struct.pack_into('<I', mbr, 454, 1)  # start LBA
    struct.pack_into('<I', mbr, 458, (IMG_SIZE // 512) - 1)  # size
    mbr[510] = 0x55
    mbr[511] = 0xAA
    img.seek(0)
    img.write(mbr)

    # GPT Header (LBA 1)
    gpt_header = bytearray(512)
    gpt_header[0:8] = b'EFI PART'  # signature
    struct.pack_into('<I', gpt_header, 8, 0x00010000)  # revision
    struct.pack_into('<I', gpt_header, 12, 92)  # header size
    # CRC32 placeholder (calculate later)
    struct.pack_into('<Q', gpt_header, 24, 1)  # current LBA
    struct.pack_into('<Q', gpt_header, 32, (IMG_SIZE // 512) - 1)  # backup LBA
    struct.pack_into('<Q', gpt_header, 40, 34)  # first usable LBA
    struct.pack_into('<Q', gpt_header, 48, (IMG_SIZE // 512) - 34)  # last usable LBA
    # GUID (random)
    import uuid
    disk_guid = uuid.uuid4().bytes_le
    gpt_header[56:72] = disk_guid
    struct.pack_into('<Q', gpt_header, 72, 2)  # partition entry LBA
    struct.pack_into('<I', gpt_header, 80, 128)  # number of partition entries
    struct.pack_into('<I', gpt_header, 84, 128)  # size of partition entry

    # Calculate CRC32
    import zlib
    crc = zlib.crc32(bytes(gpt_header[:92])) & 0xFFFFFFFF
    struct.pack_into('<I', gpt_header, 16, crc)

    img.seek(512)
    img.write(gpt_header)

    # Partition entries (LBA 2-33)
    part_entries = bytearray(128 * 128)  # 128 entries * 128 bytes

    # Partition 1: ESP (FAT32)
    esp_guid = b'\x28\x73\x2A\xC1\x1F\xF8\xD2\x11\xBA\x4B\x00\xA0\xC9\x3E\xC9\x3B'
    part_entries[0:16] = esp_guid
    part_entries[16:32] = uuid.uuid4().bytes_le  # unique GUID
    struct.pack_into('<Q', part_entries, 32, ESP_START)
    struct.pack_into('<Q', part_entries, 40, ESP_START + ESP_SECTORS - 1)
    struct.pack_into('<Q', part_entries, 48, 0x7)  # attributes: ESP
    name = 'EFI System Partition'
    part_entries[56:56+len(name)*2] = name.encode('utf-16-le')

    # Partition 2: DATA (ext2)
    data_guid = b'\xA2\xA0\xD0\xEB\xE5\xB9\x33\x44\x87\xC0\x68\xB6\xB7\x26\x99\xC7'
    part_entries[128:144] = data_guid
    part_entries[144:160] = uuid.uuid4().bytes_le
    struct.pack_into('<Q', part_entries, 160, DATA_START)
    struct.pack_into('<Q', part_entries, 168, DATA_START + DATA_SECTORS - 1)
    name2 = 'HepuOS Root'
    part_entries[184:184+len(name2)*2] = name2.encode('utf-16-le')

    img.seek(1024)
    img.write(part_entries)

    print(f"[GPT] 分区表创建完成")
    print(f"  ESP: LBA {ESP_START}-{ESP_START+ESP_SECTORS-1} ({ESP_SIZE//1024//1024}MB)")
    print(f"  DATA: LBA {DATA_START}-{DATA_START+DATA_SECTORS-1} ({DATA_SIZE//1024//1024}MB)")

def create_fat32_esp(img):
    """创建FAT32 ESP分区并写入文件"""
    print("[FAT32] 创建ESP分区...")

    # 准备文件
    files = {}

    # BOOTX64.EFI（UEFI规范要求在/EFI/BOOT/目录）
    with open(f"{PROJECT}/tools/hepboot/uefi/hepboot_uefi.efi", 'rb') as f:
        files['/EFI/BOOT/BOOTX64.EFI'] = f.read()
    print(f"  /EFI/BOOT/BOOTX64.EFI ({len(files['/EFI/BOOT/BOOTX64.EFI'])} bytes)")

    # 64位内核
    with open(f"{PROJECT}/build_x86_64/hepu-kernel64.bin", 'rb') as f:
        files['/EFI/BOOT/KERNEL64.BIN'] = f.read()
    print(f"  KERNEL64.BIN ({len(files['/EFI/BOOT/KERNEL64.BIN'])} bytes)")

    # 32位内核
    with open(f"{PROJECT}/build_v250/hepu-kernel.bin", 'rb') as f:
        files['/EFI/BOOT/KERNEL32.BIN'] = f.read()
    print(f"  KERNEL32.BIN ({len(files['/EFI/BOOT/KERNEL32.BIN'])} bytes)")

    # 配置文件
    files['/EFI/BOOT/HEPBOOT.CFG'] = b"# HepuBoot UEFI Configuration v0.10.71\ntimeout=5\ndefault=0\nkernel_arch=64\ncmdline=\n"

    # 生成FAT32分区
    esp_data = create_esp_partition(ESP_SIZE, files)

    # 写入主镜像
    img.seek(ESP_START * 512)
    img.write(esp_data)

    print("[FAT32] ESP分区创建完成")

def create_ext2_data(img):
    """创建ext2根分区"""
    print("[EXT2] 创建根分区...")

    ext2_tmp = "/tmp/ext2_partition.img"
    subprocess.run(['mkfs.ext2', '-F', '-b', '4096', ext2_tmp,
                    str(DATA_SIZE // 4096)],
                   capture_output=True)

    # 写入字体文件
    font_path = f"{PROJECT}/kernel/fonts/DejaVuSans.ttf"
    if os.path.exists(font_path):
        subprocess.run(['debugfs', '-w', ext2_tmp,
                        '-R', 'mkdir /fonts'], capture_output=True)
        subprocess.run(['debugfs', '-w', ext2_tmp,
                        '-R', f'write {font_path} /fonts/DejaVuSans.ttf'],
                       capture_output=True)
        print("  写入 /fonts/DejaVuSans.ttf")

    # 将ext2分区写入主镜像
    with open(ext2_tmp, 'rb') as f:
        ext2_data = f.read()
    img.seek(DATA_START * 512)
    img.write(ext2_data)

    os.remove(ext2_tmp)
    print("[EXT2] 根分区创建完成")

def main():
    print("=== HepuOS 双内核 UEFI 镜像构建 ===")

    # 创建镜像文件
    with open(IMG_PATH, 'wb') as img:
        img.truncate(IMG_SIZE)

        create_gpt_partition_table(img)
        create_fat32_esp(img)
        create_ext2_data(img)

    size = os.path.getsize(IMG_PATH)
    print(f"\n=== 构建完成 ===")
    print(f"  镜像: {IMG_PATH}")
    print(f"  大小: {size} bytes ({size//1024//1024}MB)")
    print(f"\n镜像布局:")
    print(f"  GPT分区表")
    print(f"  ESP分区 (FAT32, 128MB): /EFI/BOOT/BOOTX64.EFI + kernel64.bin + kernel32.bin")
    print(f"  DATA分区 (ext2, 120MB): 根文件系统 + /fonts/")

if __name__ == '__main__':
    main()
