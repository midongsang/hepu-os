#!/usr/bin/env python3
"""
赫普OS - 标准 Android boot.img 生成工具
生成符合 fastboot 协议的 boot 镜像，可通过 fastboot flash boot 刷入
"""

import struct
import sys
import os
import hashlib

BOOT_MAGIC = b'ANDROID!'
BOOT_MAGIC_SIZE = 8
BOOT_NAME_SIZE = 16
BOOT_ARGS_SIZE = 512
BOOT_EXTRA_ARGS_SIZE = 1024

# 默认加载地址（标准 Android）
KERNEL_ADDR  = 0x00008000
RAMDISK_ADDR = 0x01000000
SECOND_ADDR  = 0x00f00000
TAGS_ADDR    = 0x00000100
PAGE_SIZE    = 2048


def pad_to_page(data, page_size=PAGE_SIZE):
    """按页大小对齐"""
    remainder = len(data) % page_size
    if remainder:
        data += b'\x00' * (page_size - remainder)
    return data


def make_boot_image(kernel_data, ramdisk_data=b'', second_data=b'',
                    cmdline='', product='hepu-os', output_path='boot.img'):
    """生成标准 boot.img"""

    # 计算各部分大小
    kernel_size = len(kernel_data)
    ramdisk_size = len(ramdisk_data)
    second_size = len(second_data)

    # 构造头部
    # 格式: magic(8) + kernel_size(4) + kernel_addr(4) + ramdisk_size(4) +
    #       ramdisk_addr(4) + second_size(4) + second_addr(4) + tags_addr(4) +
    #       page_size(4) + unused(4) + product(16) + cmdline(512) + id(32) +
    #       extra_cmdline(1024)
    header = struct.pack(
        '<8sIIIIIIII',
        BOOT_MAGIC,
        kernel_size,
        KERNEL_ADDR,
        ramdisk_size,
        RAMDISK_ADDR,
        second_size,
        SECOND_ADDR,
        TAGS_ADDR,
        PAGE_SIZE
    )
    header += b'\x00' * 4  # unused

    # product name
    product_bytes = product.encode('utf-8')[:BOOT_NAME_SIZE]
    product_bytes += b'\x00' * (BOOT_NAME_SIZE - len(product_bytes))
    header += product_bytes

    # cmdline
    cmdline_bytes = cmdline.encode('utf-8')[:BOOT_ARGS_SIZE]
    cmdline_bytes += b'\x00' * (BOOT_ARGS_SIZE - len(cmdline_bytes))
    header += cmdline_bytes

    # id (sha1 of kernel+ramdisk+second)
    sha = hashlib.sha1()
    sha.update(kernel_data)
    sha.update(ramdisk_data)
    sha.update(second_data)
    id_bytes = sha.digest()[:32]
    id_bytes += b'\x00' * (32 - len(id_bytes))
    header += id_bytes

    # extra cmdline
    extra = b'\x00' * BOOT_EXTRA_ARGS_SIZE
    header += extra

    # 标准 boot.img 头部按 page_size 对齐到 2048 字节
    if len(header) < 2048:
        header += b'\x00' * (2048 - len(header))
    assert len(header) == 2048, f"Header size {len(header)} != 2048"

    # 组合镜像: header + kernel + ramdisk + second
    image = header
    image += pad_to_page(kernel_data)
    image += pad_to_page(ramdisk_data)
    if second_data:
        image += pad_to_page(second_data)

    with open(output_path, 'wb') as f:
        f.write(image)

    print(f"boot.img 生成完成: {output_path}")
    print(f"  内核大小: {kernel_size} bytes ({kernel_size/1024:.1f} KB)")
    print(f"  Ramdisk: {ramdisk_size} bytes")
    print(f"  总大小: {len(image)} bytes ({len(image)/1024:.1f} KB)")
    print(f"  产品名: {product}")
    print(f"  命令行: {cmdline}")

    return output_path


def parse_boot_image(image_path):
    """解析 boot.img 信息"""
    with open(image_path, 'rb') as f:
        data = f.read()

    if len(data) < 2048:
        print("错误: 文件太小")
        return

    magic = data[:8]
    if magic != BOOT_MAGIC:
        print(f"错误: 魔数不匹配 {magic}")
        return

    fields = struct.unpack_from('<8sIIIIIIII', data, 0)
    kernel_size = fields[1]
    kernel_addr = fields[2]
    ramdisk_size = fields[3]
    ramdisk_addr = fields[4]
    second_size = fields[5]
    page_size = fields[8]

    product = data[44:60].rstrip(b'\x00').decode('utf-8', errors='replace')
    cmdline = data[60:572].rstrip(b'\x00').decode('utf-8', errors='replace')

    print(f"boot.img 信息: {image_path}")
    print(f"  魔数: {magic}")
    print(f"  内核大小: {kernel_size} bytes @ 0x{kernel_addr:08x}")
    print(f"  Ramdisk: {ramdisk_size} bytes @ 0x{ramdisk_addr:08x}")
    print(f"  Second: {second_size} bytes")
    print(f"  页大小: {page_size}")
    print(f"  产品: {product}")
    print(f"  命令行: {cmdline}")


if __name__ == '__main__':
    if len(sys.argv) < 2:
        print("用法:")
        print(f"  {sys.argv[0]} make <kernel.bin> [output.img]")
        print(f"  {sys.argv[0]} info <boot.img>")
        sys.exit(1)

    cmd = sys.argv[1]

    if cmd == 'make' and len(sys.argv) >= 3:
        kernel_path = sys.argv[2]
        output = sys.argv[3] if len(sys.argv) > 3 else 'boot.img'
        with open(kernel_path, 'rb') as f:
            kernel = f.read()
        make_boot_image(
            kernel,
            cmdline='console=tty0 hepu.os=1 quiet',
            product='hepu-os',
            output_path=output
        )
    elif cmd == 'info' and len(sys.argv) >= 3:
        parse_boot_image(sys.argv[2])
    else:
        print("未知命令")
        sys.exit(1)
