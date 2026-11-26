# OPEN-07 阶段 1：seccomp64.h 归位证据（2026-11-26）

## 改动

- `kernel/include/seccomp64.h` → `kernel/arch/x86_64/include/seccomp64.h`
- 两个包含方 `arch/x86_64/seccomp64.c`、`arch/x86_64/syscall64_full.c` 仍写
  `#include "seccomp64.h"`，由既有的 `-I$(ARCH_DIR)/include` 解析，不改源码。
- 32 位源码未动。

## 验证

- `HEPU_EV=/tmp/open07 bash tools/regress/build_all.sh`：成功；kernel64 1021840 B，
  kernel32 3864077 B，镜像 268435456 B。
- 64 位 UEFI 启动：串口 `[Hepu Desktop] booting to lock screen`、
  `[DESKTOP64] Desktop running with mouse support`；锁屏截图 `lock-screen.png`。
- `python3 tools/audit_layout.py`：`errors=0`，`归属错位: 0`；只剩 ACPI/SMP 两条
  同名平行 INFO（OPEN-07 阶段 2 范围）。
- `python3 tools/check_docs.py`：`warns=0 errors=0`。
- `python3 tools/check_stage2_bss.py`：6/6 PASS。

## 文件

- `layout-audit.txt`：布局审计原始输出。
- `check-docs.txt`、`check-stage2-bss.txt`：静态门禁原始输出。
- `serial-64-boot-raw.txt`：64 位 UEFI 启动串口。
- `lock-screen.png`：启动后锁屏截图。
