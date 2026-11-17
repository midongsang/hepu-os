#!/usr/bin/env python3
"""P3-11: stage2_64.bin 构建助手。

干净 checkout 下 ``tools/hepboot/x86_64/stage2_64.bin`` 是被 ``*.bin`` 规则
忽略的构建产物，仓库里只有 ``stage2_64.S`` 和该目录的 Makefile。两个 ISO 脚本
原先直接 ``os.path.getsize(stage2_path)``，缺文件时抛 ``FileNotFoundError``
原始栈跟踪，看不出缺什么、该怎么补。这里把「缺产物 -> 调 make -> 校验产物与
大小」收口成一个函数，失败路径全部打印明确原因并以非零退出：

  - 缺 as/ld         -> 提示安装 binutils，退出码 2
  - make 失败        -> 原样打印汇编/链接输出，退出码 3
  - make 后仍缺产物  -> 明确报错，退出码 4
  - 产物超预留扇区   -> 明确报错，退出码 5

本模块不修改 stage2 源码，也不改变其二进制布局（LBA34 起、预留 64 扇区 = 32 KB，
与 ``stage2_64.S`` 末尾 ``.org 32768``、``make_hybrid_img.sh`` 的 ``count=64``
一致）。
"""
import os
import shutil
import subprocess
import sys

# stage2 在硬盘/混合镜像上预留 64 扇区（32 KB）；.text 在 stage2_64.S 末尾
# `.org 32768` 显式填到这个尺寸。超出即挤占后续镜像布局，必须阻断。
STAGE2_MAX_BYTES = 64 * 512
STAGE2_DIR_REL = os.path.join("tools", "hepboot", "x86_64")
STAGE2_NAME = "stage2_64.bin"


def ensure_stage2(project):
    """确保 stage2_64.bin 存在并通过大小校验。

    参数:
        project: 仓库根目录绝对路径。
    返回:
        (stage2_path, stage2_size)。
    失败时向 stderr 打印明确原因并 sys.exit 非零，不吞异常、不静默跳过。
    """
    stage2_dir = os.path.join(project, STAGE2_DIR_REL)
    stage2_path = os.path.join(stage2_dir, STAGE2_NAME)

    if not os.path.exists(stage2_path):
        missing = [t for t in ("as", "ld") if shutil.which(t) is None]
        if missing:
            sys.stderr.write(
                "[STAGE2] 缺少汇编/链接工具: %s\n" % ", ".join(missing) +
                "         stage2_64.bin 需要 `as --32` 与 `ld -m elf_i386` 构建，\n"
                "         请先安装 binutils (Debian/Ubuntu: apt install binutils)。\n")
            sys.exit(2)

        print("[STAGE2] 未找到 %s，调用 make -C %s 构建..." % (STAGE2_NAME, stage2_dir))
        proc = subprocess.run(["make", "-C", stage2_dir],
                              capture_output=True, text=True)
        if proc.stdout:
            print(proc.stdout.rstrip())
        if proc.returncode != 0:
            if proc.stderr:
                sys.stderr.write(proc.stderr)
            sys.stderr.write(
                "[STAGE2] make 构建 %s 失败 (exit=%d)，见上方汇编/链接输出。\n"
                % (STAGE2_NAME, proc.returncode))
            sys.exit(3)

        if not os.path.exists(stage2_path):
            sys.stderr.write(
                "[STAGE2] make 已结束但仍未产出 %s（目录: %s）。\n"
                % (STAGE2_NAME, stage2_dir))
            sys.exit(4)

    size = os.path.getsize(stage2_path)
    if size > STAGE2_MAX_BYTES:
        sectors = (size + 511) // 512
        sys.stderr.write(
            "[STAGE2] %s 超出预留空间: %d 字节 = %d 扇区 > %d 扇区 (%d 字节)。\n"
            "         stage2 布局在 LBA34 起预留 64 扇区，请缩小 .text 后再构建。\n"
            % (STAGE2_NAME, size, sectors,
               STAGE2_MAX_BYTES // 512, STAGE2_MAX_BYTES))
        sys.exit(5)

    return stage2_path, size
