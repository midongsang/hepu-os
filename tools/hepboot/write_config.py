#!/usr/bin/env python3
"""
HepuBoot配置文件写入工具
将配置写入磁盘镜像的LBA 11（固定位置，无需文件系统）

配置结构（512字节）:
  offset 0:   magic = "HEPC" (0x48455043)
  offset 4:   version = 1
  offset 8:   default_mode (0=正常, 1=安全, 2=恢复)
  offset 12:  timeout (菜单超时秒数)
  offset 16:  extra_cmdline[256] (额外内核参数)
  其余保留为0

用法:
  python3 write_config.py <img_path> [--mode 0|1|2] [--timeout N] [--cmdline "xxx"]
  python3 write_config.py build/hepu-os-v1.02.img --mode 1
  python3 write_config.py build/hepu-os-v1.02.img --mode 0 --timeout 5
"""

import struct
import sys
import os

CONFIG_LBA = 11
SECTOR_SIZE = 512
MAGIC = 0x48455043  # "HEPC"
VERSION = 1

def build_config(default_mode=0, timeout=3, extra_cmdline=""):
    """构建512字节配置扇区"""
    buf = bytearray(SECTOR_SIZE)
    struct.pack_into('<I', buf, 0, MAGIC)
    struct.pack_into('<I', buf, 4, VERSION)
    struct.pack_into('<I', buf, 8, default_mode)
    struct.pack_into('<I', buf, 12, timeout)
    cmdline_bytes = extra_cmdline.encode('utf-8')[:255]
    buf[16:16+len(cmdline_bytes)] = cmdline_bytes
    buf[16+len(cmdline_bytes)] = 0
    return bytes(buf)

def read_config(img_path):
    """读取当前配置"""
    with open(img_path, 'r+b') as f:
        f.seek(CONFIG_LBA * SECTOR_SIZE)
        data = f.read(SECTOR_SIZE)
    magic = struct.unpack_from('<I', data, 0)[0]
    if magic != MAGIC:
        return None
    version = struct.unpack_from('<I', data, 4)[0]
    default_mode = struct.unpack_from('<I', data, 8)[0]
    timeout = struct.unpack_from('<I', data, 12)[0]
    cmdline = data[16:16+256].split(b'\x00')[0].decode('utf-8', errors='replace')
    return {
        'magic': hex(magic),
        'version': version,
        'default_mode': default_mode,
        'timeout': timeout,
        'extra_cmdline': cmdline
    }

def write_config(img_path, default_mode=0, timeout=3, extra_cmdline=""):
    """写入配置到LBA 11"""
    config = build_config(default_mode, timeout, extra_cmdline)
    with open(img_path, 'r+b') as f:
        f.seek(CONFIG_LBA * SECTOR_SIZE)
        f.write(config)
    print(f"配置已写入 {img_path} LBA {CONFIG_LBA}")
    print(f"  default_mode = {default_mode} ({'正常' if default_mode==0 else '安全' if default_mode==1 else '恢复'})")
    print(f"  timeout = {timeout}秒")
    print(f"  extra_cmdline = '{extra_cmdline}'")

def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)

    img_path = sys.argv[1]
    if not os.path.exists(img_path):
        print(f"错误: 文件不存在 {img_path}")
        sys.exit(1)

    # 解析参数
    default_mode = 0
    timeout = 3
    extra_cmdline = ""
    show = False

    i = 2
    while i < len(sys.argv):
        if sys.argv[i] == '--mode' and i+1 < len(sys.argv):
            default_mode = int(sys.argv[i+1])
            i += 2
        elif sys.argv[i] == '--timeout' and i+1 < len(sys.argv):
            timeout = int(sys.argv[i+1])
            i += 2
        elif sys.argv[i] == '--cmdline' and i+1 < len(sys.argv):
            extra_cmdline = sys.argv[i+1]
            i += 2
        elif sys.argv[i] == '--show':
            show = True
            i += 1
        else:
            print(f"未知参数: {sys.argv[i]}")
            sys.exit(1)

    if show:
        cfg = read_config(img_path)
        if cfg:
            print("当前配置:")
            for k, v in cfg.items():
                print(f"  {k}: {v}")
        else:
            print("未找到有效配置（magic不匹配）")
    else:
        write_config(img_path, default_mode, timeout, extra_cmdline)

if __name__ == '__main__':
    main()
