#!/usr/bin/env python3
"""
赫普OS - initrd 镜像创建工具
用法: python3 mkinitrd.py <输出文件> <输入文件1> [输入文件2] ...
"""
import struct
import sys
import os

INITRD_MAGIC = b"HEPURD\x00\x00"  # 8字节，匹配C中的"HEPURD\0"(含自动终止符)
INITRD_VERSION = 1
MAX_FILENAME = 48
HEADER_SIZE = 20  # magic(8) + version(4) + file_count(4) + reserved(4)
ENTRY_SIZE = 64   # name(48) + offset(4) + size(4) + type(4) + reserved(4)


def create_initrd(output_path, input_files):
    """创建initrd镜像"""
    file_count = len(input_files)

    # 计算数据区起始偏移
    data_start = HEADER_SIZE + ENTRY_SIZE * file_count

    entries = []
    data_buffer = bytearray()
    current_offset = data_start

    for filepath in input_files:
        filename = os.path.basename(filepath)
        if len(filename) >= MAX_FILENAME:
            filename = filename[:MAX_FILENAME - 1]

        with open(filepath, 'rb') as f:
            data = f.read()

        entries.append({
            'name': filename.encode('utf-8'),
            'offset': current_offset,
            'size': len(data),
            'type': 0,  # 普通文件
        })
        data_buffer.extend(data)
        current_offset += len(data)

    # 构建镜像
    image = bytearray()

    # 头部
    image.extend(INITRD_MAGIC)
    image.extend(struct.pack('<I', INITRD_VERSION))
    image.extend(struct.pack('<I', file_count))
    image.extend(struct.pack('<I', 0))  # reserved

    # 文件条目
    for entry in entries:
        name = entry['name']
        name_field = name + b'\x00' * (MAX_FILENAME - len(name))
        image.extend(name_field)
        image.extend(struct.pack('<I', entry['offset']))
        image.extend(struct.pack('<I', entry['size']))
        image.extend(struct.pack('<I', entry['type']))
        image.extend(struct.pack('<I', 0))  # reserved

    # 数据区
    image.extend(data_buffer)

    with open(output_path, 'wb') as f:
        f.write(image)

    print(f"initrd 创建成功: {output_path}")
    print(f"  文件数量: {file_count}")
    print(f"  总大小: {len(image)} bytes")
    print(f"  数据区起始: 0x{data_start:x}")
    for entry in entries:
        print(f"    {entry['name'].decode():<32} {entry['size']:>8d} bytes @ 0x{entry['offset']:x}")

    return len(image)


def parse_initrd(path):
    """解析并显示initrd镜像内容"""
    with open(path, 'rb') as f:
        data = f.read()

    magic = data[0:8]
    if magic != INITRD_MAGIC:
        print(f"错误: 不是有效的initrd镜像 (魔数: {magic})")
        return

    version, file_count, reserved = struct.unpack_from('<III', data, 8)
    print(f"initrd 镜像: {path}")
    print(f"  版本: {version}")
    print(f"  文件数量: {file_count}")
    print(f"  总大小: {len(data)} bytes")
    print(f"  文件列表:")

    offset = HEADER_SIZE
    for i in range(file_count):
        name = data[offset:offset + MAX_FILENAME].rstrip(b'\x00').decode('utf-8', errors='replace')
        file_offset, file_size, file_type, _ = struct.unpack_from('<IIII', data, offset + MAX_FILENAME)
        print(f"    {name:<32} {file_size:>8d} bytes @ 0x{file_offset:x}")
        offset += ENTRY_SIZE


if __name__ == '__main__':
    if len(sys.argv) < 3:
        print("用法:")
        print("  创建: python3 mkinitrd.py <输出> <文件1> [文件2] ...")
        print("  解析: python3 mkinitrd.py --parse <镜像文件>")
        sys.exit(1)

    if sys.argv[1] == '--parse':
        parse_initrd(sys.argv[2])
    else:
        output = sys.argv[1]
        inputs = sys.argv[2:]
        create_initrd(output, inputs)
