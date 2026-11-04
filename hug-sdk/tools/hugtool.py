#!/usr/bin/env python3
"""
赫普OS .hug 应用包工具 (hugtool)
支持打包、解包、校验、查看信息

用法:
    python3 hugtool.py pack <目录> <输出.hug>
    python3 hugtool.py unpack <文件.hug> <输出目录>
    python3 hugtool.py verify <文件.hug>
    python3 hugtool.py info <文件.hug>
"""

import struct
import zlib
import json
import os
import sys
import hashlib

# .hug 文件格式常量
HUG_MAGIC = b'HUG1'       # 发布格式 (hugtool pack/unpack)
HUG_MAGIC_LEGACY = b'HUG\x00'  # 内核加载器格式 (mkhug.py)
HUG_HEADER_SIZE = 64
HUG_VERSION = 1

# flags
FLAG_SYSTEM_APP = 1 << 0
FLAG_REQUIRE_CREATIVE = 1 << 1
FLAG_HAS_NATIVE = 1 << 2
FLAG_WEB_ONLY = 1 << 3


def make_header(manifest_offset, manifest_size, payload_offset,
                payload_size, sig_offset, sig_size, flags=0):
    """构建64字节头部"""
    header = struct.pack(
        '<4sHHIIIIII',
        HUG_MAGIC,           # magic
        HUG_VERSION,         # version
        flags,               # flags
        manifest_offset,     # manifest_offset
        manifest_size,       # manifest_size
        payload_offset,      # payload_offset
        payload_size,        # payload_size
        sig_offset,          # signature_offset
        sig_size,            # signature_size
    )
    # 填充到64字节
    header += b'\x00' * (HUG_HEADER_SIZE - len(header))
    return header


def parse_header(data):
    """解析头部 (自动检测 HUG1 发布格式 和 HUG\\0 内核格式)"""
    if len(data) < HUG_HEADER_SIZE:
        raise ValueError("文件太小，不是有效的 .hug 文件")
    magic = data[:4]
    if magic == HUG_MAGIC:
        # HUG1 发布格式
        fields = struct.unpack_from('<4sHHIIIIII', data, 0)
        return {
            'format': 'HUG1 (发布格式)',
            'magic': fields[0],
            'version': fields[1],
            'flags': fields[2],
            'manifest_offset': fields[3],
            'manifest_size': fields[4],
            'payload_offset': fields[5],
            'payload_size': fields[6],
            'signature_offset': fields[7],
            'signature_size': fields[8],
        }
    elif magic == HUG_MAGIC_LEGACY:
        # HUG\\0 内核加载器格式 (mkhug.py)
        fields = struct.unpack_from('<IIIIIIIIII', data, 0)
        return {
            'format': 'HUG0 (内核加载器格式)',
            'magic': fields[0],
            'version': fields[1],
            'manifest_offset': fields[2],
            'manifest_size': fields[3],
            'code_offset': fields[4],
            'code_size': fields[5],
            'data_offset': fields[6],
            'data_size': fields[7],
            'entry_point': fields[8],
            'flags': fields[9],
            # 兼容字段
            'payload_offset': fields[4],
            'payload_size': fields[5],
            'signature_offset': 0,
            'signature_size': 0,
        }
    else:
        raise ValueError(f"魔数不匹配: 期望 HUG1 或 HUG\\0, 得到 {magic}")


def pack_directory(source_dir, output_path, flags=0):
    """将目录打包为 .hug 文件"""
    # 读取 manifest
    manifest_path = os.path.join(source_dir, 'manifest.json')
    if not os.path.exists(manifest_path):
        print(f"错误: 未找到 manifest.json ({manifest_path})")
        return False

    with open(manifest_path, 'r', encoding='utf-8') as f:
        manifest = json.load(f)

    manifest_json = json.dumps(manifest, ensure_ascii=False, indent=2).encode('utf-8')
    manifest_compressed = zlib.compress(manifest_json)

    # 收集 payload 文件（排除 manifest.json）
    payload_files = []
    for root, dirs, files in os.walk(source_dir):
        for fname in files:
            full_path = os.path.join(root, fname)
            rel_path = os.path.relpath(full_path, source_dir)
            if rel_path == 'manifest.json':
                continue
            with open(full_path, 'rb') as f:
                content = f.read()
            payload_files.append((rel_path, content))

    # 构建 payload: 简单的 tar-like 格式
    # 每个文件: name_len(2) + name + content_len(4) + content
    payload_raw = b''
    for rel_path, content in payload_files:
        name_bytes = rel_path.encode('utf-8')
        payload_raw += struct.pack('<H', len(name_bytes))
        payload_raw += name_bytes
        payload_raw += struct.pack('<I', len(content))
        payload_raw += content
    payload_compressed = zlib.compress(payload_raw)

    # 计算偏移
    manifest_offset = HUG_HEADER_SIZE
    manifest_size = len(manifest_compressed)
    payload_offset = manifest_offset + manifest_size
    payload_size = len(payload_compressed)
    sig_offset = payload_offset + payload_size
    sig_size = 0  # 原型阶段不做实际签名

    header = make_header(manifest_offset, manifest_size,
                         payload_offset, payload_size,
                         sig_offset, sig_size, flags)

    # 写入文件
    with open(output_path, 'wb') as f:
        f.write(header)
        f.write(manifest_compressed)
        f.write(payload_compressed)

    print(f"打包完成: {output_path}")
    print(f"  应用ID: {manifest.get('app_id', 'unknown')}")
    print(f"  应用名: {manifest.get('app_name', 'unknown')}")
    print(f"  版本: {manifest.get('version', 'unknown')}")
    print(f"  Manifest: {manifest_size} bytes (压缩前 {len(manifest_json)} bytes)")
    print(f"  Payload: {payload_size} bytes ({len(payload_files)} 个文件)")
    print(f"  总大小: {os.path.getsize(output_path)} bytes")
    return True


def unpack_file(hug_path, output_dir):
    """解包 .hug 文件到目录"""
    with open(hug_path, 'rb') as f:
        data = f.read()

    header = parse_header(data)

    # 解压 manifest
    manifest_data = data[header['manifest_offset']:
                         header['manifest_offset'] + header['manifest_size']]
    manifest_json = zlib.decompress(manifest_data)
    manifest = json.loads(manifest_json)

    # 解压 payload
    payload_data = data[header['payload_offset']:
                        header['payload_offset'] + header['payload_size']]
    payload_raw = zlib.decompress(payload_data)

    os.makedirs(output_dir, exist_ok=True)

    # 写入 manifest
    with open(os.path.join(output_dir, 'manifest.json'), 'w', encoding='utf-8') as f:
        json.dump(manifest, f, ensure_ascii=False, indent=2)

    # 解析 payload
    offset = 0
    file_count = 0
    while offset < len(payload_raw):
        name_len = struct.unpack_from('<H', payload_raw, offset)[0]
        offset += 2
        name = payload_raw[offset:offset + name_len].decode('utf-8')
        offset += name_len
        content_len = struct.unpack_from('<I', payload_raw, offset)[0]
        offset += 4
        content = payload_raw[offset:offset + content_len]
        offset += content_len

        out_path = os.path.join(output_dir, name)
        os.makedirs(os.path.dirname(out_path), exist_ok=True)
        with open(out_path, 'wb') as f:
            f.write(content)
        file_count += 1

    print(f"解包完成: {output_dir}")
    print(f"  应用: {manifest.get('app_name', 'unknown')} v{manifest.get('version', '?')}")
    print(f"  解压文件数: {file_count + 1}")
    return True


def verify_file(hug_path):
    """校验 .hug 文件完整性"""
    print(f"校验: {hug_path}")
    try:
        with open(hug_path, 'rb') as f:
            data = f.read()

        header = parse_header(data)
        print(f"  格式版本: {header['version']}")
        print(f"  标志位: 0x{header['flags']:04x}")

        # 校验 manifest
        manifest_data = data[header['manifest_offset']:
                             header['manifest_offset'] + header['manifest_size']]
        manifest_json = zlib.decompress(manifest_data)
        manifest = json.loads(manifest_json)
        print(f"  应用ID: {manifest.get('app_id', 'N/A')}")
        print(f"  应用名: {manifest.get('app_name', 'N/A')}")

        # 校验必需字段
        required = ['app_id', 'app_name', 'version', 'entry', 'permissions']
        missing = [f for f in required if f not in manifest]
        if missing:
            print(f"  [警告] 缺少必需字段: {missing}")
        else:
            print(f"  [OK] Manifest 字段完整")

        # 校验权限声明
        perms = manifest.get('permissions', [])
        for p in perms:
            if 'reason' not in p:
                print(f"  [警告] 权限 {p.get('name')} 缺少 reason 声明")
        print(f"  [OK] 权限声明: {len(perms)} 项")

        # 校验 payload 可解压
        payload_data = data[header['payload_offset']:
                            header['payload_offset'] + header['payload_size']]
        zlib.decompress(payload_data)
        print(f"  [OK] Payload 完整性校验通过")

        # 校验文件大小
        expected_end = header['signature_offset'] + header['signature_size']
        if expected_end <= len(data):
            print(f"  [OK] 文件大小一致")
        else:
            print(f"  [错误] 文件被截断 (期望 {expected_end}, 实际 {len(data)})")
            return False

        print("\n校验通过 ✓")
        return True

    except Exception as e:
        print(f"  [错误] 校验失败: {e}")
        return False


def info_file(hug_path):
    """显示 .hug 文件信息"""
    with open(hug_path, 'rb') as f:
        data = f.read()

    header = parse_header(data)
    manifest_data = data[header['manifest_offset']:
                         header['manifest_offset'] + header['manifest_size']]
    # 内核格式(HUG0)的manifest是未压缩的纯JSON，发布格式(HUG1)是zlib压缩的
    try:
        manifest = json.loads(zlib.decompress(manifest_data))
    except:
        manifest = json.loads(manifest_data.decode('utf-8'))

    print("=" * 50)
    print(f"  文件格式: {header.get('format', 'unknown')}")
    print(f"  应用名称: {manifest.get('app_name', 'N/A')}")
    print(f"  应用ID:   {manifest.get('app_id', 'N/A')}")
    print(f"  版本:     {manifest.get('version', 'N/A')}")
    print(f"  开发者:   {manifest.get('author', 'N/A')}")
    print(f"  入口:     {manifest.get('entry', 'N/A')}")
    print(f"  最低系统: {manifest.get('min_os_version', 'N/A')}")
    print(f"  目标架构: {', '.join(manifest.get('target_arch', []))}")
    print("-" * 50)
    print("  权限声明:")
    for p in manifest.get('permissions', []):
        req = "必需" if p.get('required') else "可选"
        print(f"    - {p['name']} ({req}): {p.get('reason', '无说明')}")
    print("-" * 50)
    sandbox = manifest.get('sandbox', {})
    print(f"  沙盒配置: 网络={sandbox.get('network', '?')}, "
          f"文件={sandbox.get('filesystem', '?')}, "
          f"IPC={sandbox.get('ipc', '?')}")
    print("-" * 50)
    print(f"  格式版本: {header['version']}")
    print(f"  标志位:   0x{header['flags']:04x}")
    print(f"  Manifest: {header['manifest_size']} bytes")
    print(f"  Payload:  {header['payload_size']} bytes")
    print(f"  签名:     {header['signature_size']} bytes")
    print(f"  文件总大小: {len(data)} bytes")
    print("=" * 50)


def print_usage():
    print("赫普OS .hug 应用包工具 v1.0")
    print()
    print("用法:")
    print(f"  {sys.argv[0]} pack   <目录> <输出.hug>  打包目录为.hug文件")
    print(f"  {sys.argv[0]} unpack <文件.hug> <目录>   解包.hug文件")
    print(f"  {sys.argv[0]} verify <文件.hug>          校验.hug文件完整性")
    print(f"  {sys.argv[0]} info   <文件.hug>          显示.hug文件信息")


def main():
    if len(sys.argv) < 2:
        print_usage()
        sys.exit(1)

    cmd = sys.argv[1]

    if cmd == 'pack' and len(sys.argv) >= 4:
        success = pack_directory(sys.argv[2], sys.argv[3])
        sys.exit(0 if success else 1)
    elif cmd == 'unpack' and len(sys.argv) >= 4:
        success = unpack_file(sys.argv[2], sys.argv[3])
        sys.exit(0 if success else 1)
    elif cmd == 'verify' and len(sys.argv) >= 3:
        success = verify_file(sys.argv[2])
        sys.exit(0 if success else 1)
    elif cmd == 'info' and len(sys.argv) >= 3:
        info_file(sys.argv[2])
    else:
        print_usage()
        sys.exit(1)


if __name__ == '__main__':
    main()
