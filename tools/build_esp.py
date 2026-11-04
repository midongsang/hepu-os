#!/usr/bin/env python3
"""生成 FAT32 ESP 分区镜像(含 UEFI 引导器+内核), 写入混合镜像指定偏移
用法: build_esp.py <hybrid_img> <esp_start_mb> <esp_size_mb> <efi_file> <kernel_file>
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from fat32_generator import create_esp_partition

def main():
    img, start_mb, size_mb, efi, kernel = sys.argv[1:6]
    size = int(size_mb) * 1024 * 1024
    files = {
        '/EFI/BOOT/BOOTX64.EFI': open(efi, 'rb').read(),
        '/hepu-kernel64.bin': open(kernel, 'rb').read(),
    }
    esp = create_esp_partition(size, files)
    with open(img, 'r+b') as f:
        f.seek(int(start_mb) * 1024 * 1024)
        f.write(esp)
    print(f"ESP written: {len(esp)} bytes @ {start_mb}MiB (BOOTX64.EFI + kernel)")

if __name__ == '__main__':
    main()
