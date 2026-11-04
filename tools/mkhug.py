#!/usr/bin/env python3
"""
赫普OS - .hug 原生应用打包工具 (内核加载器格式)
用法: python3 mkhug.py <二进制文件> <输出.hug> [manifest.json]
"""
import struct
import json
import sys
import os

HUG_MAGIC = 0x00475548  # "HUG\0" 小端
HUG_VERSION = 1
HEADER_SIZE = 64


def make_hug(bin_path, output_path, manifest_path=None):
    """将二进制文件打包为内核可加载的.hug格式"""
    with open(bin_path, 'rb') as f:
        code = f.read()

    # manifest
    if manifest_path and os.path.exists(manifest_path):
        with open(manifest_path, 'r', encoding='utf-8') as f:
            manifest = json.dumps(json.load(f), ensure_ascii=False).encode('utf-8')
    else:
        manifest = json.dumps({
            "app_id": "com.hepu.app",
            "app_name": os.path.basename(bin_path),
            "version": "1.0.0",
            "entry": "_start",
            "type": "native"
        }, ensure_ascii=False).encode('utf-8')

    manifest_offset = HEADER_SIZE
    manifest_size = len(manifest)
    code_offset = manifest_offset + manifest_size
    if code_offset % 4:
        code_offset += 4 - (code_offset % 4)
    code_size = len(code)

    # 构建头部 (匹配内核 hug_header_t)
    header = struct.pack('<IIIIIIIIII',
        HUG_MAGIC, HUG_VERSION,
        manifest_offset, manifest_size,
        code_offset, code_size,
        0, 0,  # data_offset, data_size
        0,     # entry_point (代码段起始)
        0      # flags
    )
    header += b'\x00' * (HEADER_SIZE - len(header))

    # 组装文件
    data = header + manifest
    data += b'\x00' * (code_offset - len(data))
    data += code

    with open(output_path, 'wb') as f:
        f.write(data)

    print(f".hug 创建成功: {output_path}")
    print(f"  总大小: {len(data)} bytes")
    print(f"  manifest: {manifest_size} bytes @ {manifest_offset}")
    print(f"  代码: {code_size} bytes @ {code_offset}")
    print(f"  入口: 0x0")


if __name__ == '__main__':
    if len(sys.argv) < 3:
        print("用法: python3 mkhug.py <二进制> <输出.hug> [manifest.json]")
        sys.exit(1)
    bin_path = sys.argv[1]
    output_path = sys.argv[2]
    manifest_path = sys.argv[3] if len(sys.argv) > 3 else None
    make_hug(bin_path, output_path, manifest_path)
