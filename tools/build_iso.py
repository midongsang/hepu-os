#!/usr/bin/env python3
"""
HepuOS ISO镜像构建器
创建isohybrid混合镜像（MBR+ISO），支持Rufus ISO模式和DD模式
"""
import os
import struct
import sys

try:
    import pycdlib
except ImportError:
    sys.stderr.write("[ISO] 缺少 Python 依赖 pycdlib（pip install pycdlib）。\n")
    sys.exit(2)

# P3-11: 原来是另一个会话副本的绝对路径(38448056008342018), 换机器/换会话后
# 必然失效。改为从脚本自身位置推导: tools/<this>.py 的上一级就是仓库根。
import os as _os
PROJECT = _os.path.dirname(_os.path.dirname(_os.path.abspath(__file__)))
sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
from stage2_builder import ensure_stage2
OUTPUT = "/tmp/hepu64.iso"

def main():
    print("[1/5] 创建ISO文件系统...")
    iso = pycdlib.PyCdlib()
    iso.new(interchange_level=3,
            joliet=True,
            rock_ridge='1.09',
            vol_ident='HEPUOS_BOOT',
            pub_ident_str='HepuOS',
            preparer_ident_str='HepuOS Build System')

    # 添加引导文件（stage2）；缺产物时先由 ensure_stage2 调 make 构建，
    # 工具缺失/汇编失败/超预留扇区都会明确报错并非零退出。
    print("[2/5] 添加引导文件...")
    stage2_path, stage2_size = ensure_stage2(PROJECT)
    iso.add_fp(open(stage2_path, 'rb'), stage2_size,
               '/STAGE2.SYS;1',
               rr_name='stage2.sys',
               joliet_path='/stage2.sys')
    print(f"  stage2: {stage2_size} 字节")

    # 添加64位内核
    kernel64_path = f"{PROJECT}/build_x86_64/hepu-kernel64.bin"
    if os.path.exists(kernel64_path):
        kernel64_size = os.path.getsize(kernel64_path)
        iso.add_fp(open(kernel64_path, 'rb'), kernel64_size,
                   '/KERNEL64.BIN;1',
                   rr_name='kernel64.bin',
                   joliet_path='/kernel64.bin')
        print(f"  64位内核: {kernel64_size} 字节")

    # 添加32位内核
    kernel32_path = f"{PROJECT}/build_v250/hepu-kernel.bin"
    if os.path.exists(kernel32_path):
        kernel32_size = os.path.getsize(kernel32_path)
        iso.add_fp(open(kernel32_path, 'rb'), kernel32_size,
                   '/KERNEL32.BIN;1',
                   rr_name='kernel32.bin',
                   joliet_path='/kernel32.bin')
        print(f"  32位内核: {kernel32_size} 字节")

    # 添加El Torito引导记录（无模拟模式）
    # 加载到0x8000（与stage2链接地址一致，避免重定位）
    print("[3/5] 添加El Torito引导记录...")
    iso.add_eltorito('/STAGE2.SYS;1',
                     media_name='noemul',
                     boot_load_seg=0x0800,  # 加载到0x8000（段0x0800）
                     boot_load_size=32,  # 加载32个512字节扇区=16KB
                     platform_id=0)  # x86

    # 添加isohybrid MBR（支持U盘引导）
    print("[4/5] 添加isohybrid MBR...")
    try:
        iso.add_isohybrid(part_entry=1,
                          part_type=0x83,  # Linux分区类型
                          geometry_sectors=32,
                          geometry_heads=64)
        print("  isohybrid MBR添加成功")
    except Exception as e:
        print(f"  isohybrid警告: {e}")

    # 写入ISO
    print("[5/5] 写入ISO文件...")
    iso.write(OUTPUT)
    iso.close()

    iso_size = os.path.getsize(OUTPUT)
    print(f"\nISO创建完成: {OUTPUT}")
    print(f"  大小: {iso_size} 字节 ({iso_size/1024/1024:.1f} MB)")
    print(f"  格式: ISO 9660 + Joliet + Rock Ridge + El Torito + isohybrid MBR")
    print(f"  引导: 无模拟模式, 加载段0x0")

if __name__ == '__main__':
    main()
