# 系统管理命令补齐 — 证据与结论（2026-11-26）

分支：`feat/sysadmin-cmds-2026-11-26`（基线 main `9c942dc`）
范围：32/64 系统管理命令补齐与命令集合对齐；只改本路归属文件。

## 1. 改了什么

### 新增文件
- `kernel/include/shell_admin.h` — 共享命令分派与测试辅助声明。
- `kernel/sched/shell_cmd_admin.c` — 32 位命令实现（558 行）。
- `kernel/arch/x86_64/shell_cmd_admin64.c` — 64 位命令实现（537 行）。
- `kernel/kunit/kunit_admin.c` — KUnit 套件 7 例。

### 修改文件
- `kernel/sched/shell_execute.c` — include + 链尾集中分派块（既有分支未重排）。
- `kernel/sched/shell_help.c` — ADMIN 组 9 条帮助条目。
- `kernel/fs/vfs.c` / `kernel/include/vfs.h` — 新增只读 `vfs_path_busy()`（fd 路径前缀忙碌判定）。
- `kernel/arch/x86_64/Makefile` — `C_SOURCES` 追加 `shell_cmd_admin64.c`（未改 vpath 行）。
- `kernel/arch/x86_64/shell64.c` — 适配器 + 命令表 9 条 + 两处 `cmds[]` 补全数组补名。
- `kernel/kunit/kunit_registry.c` — 注册 `kunit_admin_register()`。
- `docs/status/SYSTEM_TOOLS.md`、`docs/status/OPEN_BUGS.md` — 台账与未闭环边界登记（OPEN-08）。

### 命令清单（32/64 同名注册，对齐明细见 `PARITY.md`）
`chmod`、`chown`、`chgrp`、`mount`、`umount`、`fsck`、`mkfs`、`useradd`、`userdel`。

## 2. 实现口径与边界（如实标注）

| 命令 | 落地口径 | 边界 / 未实现 |
|---|---|---|
| `chmod` | 数字八进制模式（0–7，≤07777）；经 VFS 改权限位并写回 ext2 inode，`ide_ext2_sync()` 落盘 | **不支持** ugo+rwx 符号模式；内存态节点无盘 inode 时只改内存并告警 |
| `chown` | `uid[:gid]` 十进制；inode `i_uid/i_gid` 落盘 | 64 位 shell 暂无角色/属主门禁（与现有 fs 命令一致），未做 EACCES 属主校验 |
| `chgrp` | 仅属组，chown 子集 | 同上 |
| `mount` | 无参列挂载表；校验链：设备不存在 / 重复设备 / 重复挂载点 / 挂载点缺失 / 非目录 / 非空目录 / 忙碌；失败回滚不留半挂 | **第二 ext2 实例挂载本轮不落地**（内核单上下文：超级块/GDT/块缓存无第二份槽位），命中明确报降级；32 忙碌判定接 VFS fd 表，64 忙碌判定延期 |
| `umount` | 卸载运行时挂载点；启动根记录不可卸载 | 同上忙碌判定边界 |
| `fsck` | 复用既有 ext2 一致性检查（超级块/块组计数/位图与空闲计数/目录 inode 引用）；默认只读；结论行 `[FSCK] RESULT: CLEAN\|ISSUES\|ERROR` 可 grep | `--fix`：32 位接既有 `ide_ext2_fsck_fix()`；**64 位 `ext2_fsck_run` 仅只读检查，修复路径预留未接**（参数被解析但忽略） |
| `mkfs` | 显式拒绝并说明 | 内核内无格式化能力；真实格式化需主机 `mke2fs` 造盘再挂 IDE。成本见 OPEN-08 |
| `useradd` / `userdel` | 显式拒绝并说明原因 | 单属主模型（`/account.conf`），无持久化本地多用户库、无组模型；内存态隐私空间注册表不是登录账号且不落盘，未包装伪造多用户能力 |

## 3. 执行命令与关键输出

### 32 位构建
- 命令：`flock /tmp/hepu-build.lock -c 'make -j2'`
- 结果：退出 0，`error:` 计数 0（`build32.log`，含 `CC kernel/kunit/kunit_admin.c`、`CC kernel/sched/shell_cmd_admin.c`）。

### 64 位构建
- 命令：`flock /tmp/hepu-build.lock -c 'make -C kernel/arch/x86_64 -j2'`
- 结果：退出 0，`error:` 计数 0（`build64.log`）；`build_x86_64/hepu-kernel64.bin` 1,014,384 字节。

### 32 位无盘冒烟
- 命令：`bash tools/regress/smoke32.sh --no-disk`
- 摘要：`smoke32-nodisk.txt`
- 关键行：
  - `ext2_mounted=0 ext2_nodisk=1`
  - `ktest_result=PASS ktest=1 fail_suites=0 fail_cases=0 skip_cases=4 p3_skip=2`
  - `p3-security: passed=0 failed=0 skipped=2`（两项显式 SKIP，属预期，**未计为 PASS**）
  - admin 套件 2 例在无盘环境显式 SKIP：`mount_dup_device`、`chmod_chown_persist`。

### 32 位带盘冒烟
- 命令：`bash tools/regress/smoke32.sh --disk`
- 摘要：`smoke32-disk.txt`；套件摘录：`ser32-admin-excerpt.txt`
- 关键行：
  - `ext2_mounted=1 ext2_nodisk=0`
  - `ktest_result=PASS ktest=1 fail_suites=0 fail_cases=0 skip_cases=0 p3_skip=0`
  - `p3-security: passed=2 failed=0 skipped=0`
  - `[KTEST] RESULT: ALL PASS`
  - `[KUNIT] suite: system admin commands (0x00000007 tests)` → `result: 0x00000007 passed, 0 failed, 0 skipped`。
  - 落盘例真实执行：`chmod 640` / `chown 2:2` 写回 inode 并读回断言（mode=0640、uid=2、gid=2），通过后清理测试文件。

### 64 位 UEFI 启动取证
- 打包：`HEPU_IMG=... python3 tools/build_uefi_dual.py`（`build-image.log`；ESP 内 `KERNEL64.BIN` 1,014,384 字节，与新构建二进制一致）；引导器 `build-efi.log`（`hepboot_uefi.efi` 106,844 字节）。
- 启动脚本：`boot64-private.sh`（私有实例目录，只按 pidfile 回收 QEMU）；串口：`ser64-boot.log`，运行记录：`boot64-private.log`。
- 关键行：
  - `BdsDxe: starting Boot0002 "UEFI QEMU HARDDISK ..."` → `HepuBoot UEFI v0.08`
  - `[BOOT] step1 done`
  - `[Hepu Desktop] booting to lock screen`
  - `[DESKTOP64] Desktop running with mouse support`
  - 串口无 panic / fault。
- **限制**：64 位内核不带 KUnit 套件（arch x86_64 未挂 kunit 运行器）；本次取证未向 64 shell 注入命令交互，9 条新命令在 64 侧的运行时分派与打印文案**未经串口交互实测**，仅完成：构建 0 error、命令表/补全数组注册核对、二进制字符串存在性核对、UEFI 启动到桌面无故障。

### 文档检查
- 命令：`python3 tools/check_docs.py`
- 结果：`warns=0 errors=0`。

## 4. 未覆盖项 / 未闭环（见 OPEN-08）

1. 第二 ext2 实例运行时挂载：单上下文限制，未落地。
2. `mkfs` 内核内格式化：显式拒绝，延期；成本已记录。
3. 64 位 `fsck --fix`：只读检查，修复路径预留未接。
4. 64 位忙碌（busy）判定：未接 shell/进程 fd 表，仅非空目录判定。
5. 多用户 / 组模型：`useradd/userdel/usermod/groupadd` 显式拒绝，不伪造。
6. 32 位 CLI 无串口交互输入：命令的 shell 分派与文案路径未做交互手测；确定性逻辑由 KUnit 覆盖，落盘经带盘真实 ext2 取证。
7. 64 侧命令运行时分派未交互实测（见上 3.6）。
8. 32/64 既有 coreutils / 调试命令的历史不对称不在本路闭合范围（见 `PARITY.md` 第 4 节）。

## 5. 文件清单（本路改动面）

```
kernel/include/shell_admin.h                      新建
kernel/sched/shell_cmd_admin.c                    新建
kernel/arch/x86_64/shell_cmd_admin64.c            新建
kernel/kunit/kunit_admin.c                         新建
kernel/sched/shell_execute.c                       修改（链尾分派块）
kernel/sched/shell_help.c                          修改（9 条帮助）
kernel/fs/vfs.c                                    修改（vfs_path_busy）
kernel/include/vfs.h                                修改（声明）
kernel/arch/x86_64/Makefile                         修改（C_SOURCES）
kernel/arch/x86_64/shell64.c                        修改（表 + 补全数组）
kernel/kunit/kunit_registry.c                       修改（套件注册）
docs/status/SYSTEM_TOOLS.md                         修改（台账行）
docs/status/OPEN_BUGS.md                            修改（OPEN-08）
```
