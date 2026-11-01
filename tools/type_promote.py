#!/usr/bin/env python3
"""
赫普OS - 类型统一推广工具
统计并推广使用types.h中定义的统一类型(u8/u16/u32/u64等)
"""
import os
import re
import sys

TYPE_MAP = {
    'uint8_t': 'u8',
    'uint16_t': 'u16',
    'uint32_t': 'u32',
    'uint64_t': 'u64',
    'int8_t': 's8',
    'int16_t': 's16',
    'int32_t': 's32',
    'int64_t': 's64',
}

def scan_file(filepath):
    """扫描文件中的类型使用"""
    with open(filepath, 'r') as f:
        content = f.read()
    
    counts = {}
    for old, new in TYPE_MAP.items():
        # 匹配完整的类型名（前后不是字母数字下划线）
        pattern = r'(?<![a-zA-Z0-9_])' + re.escape(old) + r'(?![a-zA-Z0-9_])'
        count = len(re.findall(pattern, content))
        if count > 0:
            counts[old] = count
    
    return counts

def promote_file(filepath, dry_run=True):
    """推广文件中的类型"""
    with open(filepath, 'r') as f:
        content = f.read()
    
    original = content
    changes = 0
    
    for old, new in TYPE_MAP.items():
        pattern = r'(?<![a-zA-Z0-9_])' + re.escape(old) + r'(?![a-zA-Z0-9_])'
        new_content, count = re.subn(pattern, new, content)
        if count > 0:
            changes += count
            content = new_content
    
    if not dry_run and changes > 0:
        with open(filepath, 'w') as f:
            f.write(content)
    
    return changes

def main():
    kernel_dir = 'kernel'
    all_files = []
    
    for root, dirs, files in os.walk(kernel_dir):
        for f in files:
            if f.endswith('.c') or f.endswith('.h'):
                all_files.append(os.path.join(root, f))
    
    print(f"扫描 {len(all_files)} 个文件...")
    print()
    
    total_counts = {k: 0 for k in TYPE_MAP}
    files_with_types = 0
    files_promoted = 0
    
    for filepath in sorted(all_files):
        counts = scan_file(filepath)
        if counts:
            files_with_types += 1
            total = sum(counts.values())
            for k, v in counts.items():
                total_counts[k] += v
            
            # 只推广新模块（kunit/kbench/quic/http3/doq/mp_quic等）
            is_new_module = any(x in filepath for x in [
                'kunit', 'kbench', 'quic_', 'http3', 'doq', 'mp_quic',
                'qpack', 'init_sequence', 'sys_', 'kunit_log'
            ])
            
            if is_new_module and '--apply' in sys.argv:
                changes = promote_file(filepath, dry_run=False)
                if changes > 0:
                    files_promoted += 1
                    print(f"  推广: {filepath} ({changes} 处)")
    
    print(f"\n=== 类型使用统计 ===")
    for old, new in TYPE_MAP.items():
        print(f"  {old:10s} -> {new:4s}: {total_counts[old]:5d} 处")
    
    print(f"\n文件统计:")
    print(f"  总文件数: {len(all_files)}")
    print(f"  使用原始类型: {files_with_types}")
    print(f"  已推广: {files_promoted}")
    
    if '--apply' not in sys.argv:
        print(f"\n使用 --apply 参数执行推广（仅新模块）")

if __name__ == '__main__':
    main()
