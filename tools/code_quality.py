#!/usr/bin/env python3
"""赫普OS 代码质量检查工具
检测魔法数字、缓冲区溢出风险、TODO标记等
"""
import os
import re
import sys

KERNEL_DIR = "kernel"

def check_magic_numbers():
    """检测硬编码魔法数字"""
    issues = []
    for root, dirs, files in os.walk(KERNEL_DIR):
        for f in files:
            if not f.endswith(".c"):
                continue
            path = os.path.join(root, f)
            with open(path, "r", encoding="utf-8", errors="ignore") as fp:
                for i, line in enumerate(fp, 1):
                    # 跳过注释和#define行
                    if line.strip().startswith("//") or line.strip().startswith("/*"):
                        continue
                    if "#define" in line:
                        continue
                    # 检测4位以上十六进制
                    matches = re.findall(r'0x[0-9a-fA-F]{4,}', line)
                    for m in matches:
                        if m not in ("0x00000000", "0xFFFFFFFF", "0x00000001"):
                            issues.append(f"{path}:{i}: 魔法数字 {m}")
    return issues

def check_buffer_overflow():
    """检测strcpy/sprintf缓冲区溢出风险"""
    issues = []
    for root, dirs, files in os.walk(KERNEL_DIR):
        for f in files:
            if not f.endswith(".c"):
                continue
            path = os.path.join(root, f)
            with open(path, "r", encoding="utf-8", errors="ignore") as fp:
                for i, line in enumerate(fp, 1):
                    if "strcpy(" in line and "strncpy" not in line:
                        issues.append(f"{path}:{i}: strcpy风险")
                    if "sprintf(" in line and "snprintf" not in line:
                        issues.append(f"{path}:{i}: sprintf风险")
                    if "gets(" in line:
                        issues.append(f"{path}:{i}: gets风险(已废弃)")
    return issues

def check_todo():
    """检测TODO/FIXME标记"""
    issues = []
    for root, dirs, files in os.walk(KERNEL_DIR):
        for f in files:
            if not f.endswith(".c") and not f.endswith(".h"):
                continue
            path = os.path.join(root, f)
            with open(path, "r", encoding="utf-8", errors="ignore") as fp:
                for i, line in enumerate(fp, 1):
                    if "TODO" in line or "FIXME" in line or "XXX" in line:
                        issues.append(f"{path}:{i}: {line.strip()[:60]}")
    return issues

def main():
    print("=" * 60)
    print("赫普OS 代码质量检查")
    print("=" * 60)
    
    magic = check_magic_numbers()
    buffer = check_buffer_overflow()
    todo = check_todo()
    
    print(f"\n魔法数字: {len(magic)} 处")
    print(f"缓冲区溢出风险: {len(buffer)} 处")
    print(f"TODO/FIXME: {len(todo)} 处")
    
    if buffer:
        print("\n=== 缓冲区溢出风险 (高危) ===")
        for i in buffer[:10]:
            print(f"  {i}")
    
    if len(sys.argv) > 1 and sys.argv[1] == "-v":
        print("\n=== 魔法数字详情 ===")
        for i in magic[:20]:
            print(f"  {i}")
    
    print(f"\n总计: {len(magic) + len(buffer) + len(todo)} 个待改进项")
    return 0

if __name__ == "__main__":
    sys.exit(main())
