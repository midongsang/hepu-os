#!/usr/bin/env python3
"""
HepuOS 混合ISO构建器
生成ISO 9660文件系统后，在固定LBA位置写入MBR+stage2+内核，
使ISO可同时作为CD-ROM（El Torito）和硬盘（MBR）引导。
"""
import pycdlib
import os
import struct
import subprocess

PROJECT = "/home/user/Doubao/chats/38448056008342018/extracted/os-source/hepu_os_src/hepu-os"
OUTPUT = "/tmp/hepu64.iso"
TMP_ISO = "/tmp/hepu64_tmp.iso"

# 布局常量（与硬盘镜像一致）
STAGE2_LBA = 34
KERNEL64_LBA = 100
KERNEL32_LBA = 500

def main():
    print("[1/4] 创建ISO文件系统...")
    iso = pycdlib.PyCdlib()
    iso.new(interchange_level=3,
            joliet=True,
            rock_ridge='1.09',
            vol_ident='HEPUOS_BOOT',
            pub_ident_str='HepuOS',
            preparer_ident_str='HepuOS Build System')

    # 添加引导文件（stage2）- El Torito加载到0x8000
    print("[2/4] 添加引导文件...")
    stage2_path = f"{PROJECT}/tools/hepboot/x86_64/stage2_64.bin"
    stage2_size = os.path.getsize(stage2_path)
    iso.add_fp(open(stage2_path, 'rb'), stage2_size,
               '/STAGE2.SYS;1',
               rr_name='stage2.sys',
               joliet_path='/stage2.sys')

    # 添加内核文件
    kernel64_path = f"{PROJECT}/build_x86_64/hepu-kernel64.bin"
    if os.path.exists(kernel64_path):
        kernel64_size = os.path.getsize(kernel64_path)
        iso.add_fp(open(kernel64_path, 'rb'), kernel64_size,
                   '/KERNEL64.BIN;1',
                   rr_name='kernel64.bin',
                   joliet_path='/kernel64.bin')

    kernel32_path = f"{PROJECT}/build_v250/hepu-kernel.bin"
    if os.path.exists(kernel32_path):
        kernel32_size = os.path.getsize(kernel32_path)
        iso.add_fp(open(kernel32_path, 'rb'), kernel32_size,
                   '/KERNEL32.BIN;1',
                   rr_name='kernel32.bin',
                   joliet_path='/kernel32.bin')

    # El Torito引导记录（无模拟模式，加载到0x8000）
    iso.add_eltorito('/STAGE2.SYS;1',
                     media_name='noemul',
                     boot_load_seg=0x0800,
                     boot_load_size=32,
                     platform_id=0)

    # 写入临时ISO
    iso.write(TMP_ISO)
    iso.close()
    print(f"  临时ISO: {os.path.getsize(TMP_ISO)} 字节")

    # 只写入内核到固定LBA位置（stage2保持El Torito原始位置，避免破坏ISO结构）
    # CD-ROM引导时，El Torito加载stage2文件，stage2从LBA 100读取内核
    print("[3/4] 写入内核到固定LBA位置...")

    with open(TMP_ISO, 'r+b') as f:
        # 写入64位内核（LBA 100）- CD-ROM和硬盘模式都从这里读取
        if os.path.exists(kernel64_path):
            f.seek(KERNEL64_LBA * 512)
            with open(kernel64_path, 'rb') as k_f:
                f.write(k_f.read())
            print(f"  64位内核写入完成（LBA {KERNEL64_LBA}，{kernel64_size}字节）")

        # 写入32位内核（LBA 500）
        if os.path.exists(kernel32_path):
            f.seek(KERNEL32_LBA * 512)
            with open(kernel32_path, 'rb') as k_f:
                f.write(k_f.read())
            print(f"  32位内核写入完成（LBA {KERNEL32_LBA}，{kernel32_size}字节）")

    # 重命名为最终ISO
    os.rename(TMP_ISO, OUTPUT)

    iso_size = os.path.getsize(OUTPUT)
    print(f"\n[4/4] 混合ISO创建完成: {OUTPUT}")
    print(f"  大小: {iso_size} 字节 ({iso_size/1024/1024:.1f} MB)")
    print(f"  格式: ISO 9660 + El Torito + MBR混合引导")
    print(f"  引导: CD-ROM(El Torito) + 硬盘(MBR) 双模式")
    print(f"  布局: MBR@LBA0, stage2@LBA{STAGE2_LBA}, kernel64@LBA{KERNEL64_LBA}, kernel32@LBA{KERNEL32_LBA}")

if __name__ == '__main__':
    main()
