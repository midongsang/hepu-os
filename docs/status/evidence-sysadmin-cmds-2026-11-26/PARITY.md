# 32/64 命令集合对齐清单（系统管理命令，2026-11-26）

基线：main `9c942dc`，分支 `feat/sysadmin-cmds-2026-11-26`。
本清单只覆盖本路新增/对齐的系统管理命令；32/64 既有的 coreutils / 调试命令不对称属历史现状，不在本路范围。

## 1. 本轮新增命令（两边均已注册）

| 命令 | 32 位分派 | 64 位命令表 | 32 help | 64 cmds 补全数组 ×2 |
|---|---|---|---|---|
| `chmod` | `shell_cmd_admin.c` | `shell64.c:3580` | `shell_help.c:90` | 两处均含 |
| `chown` | 同上 | `shell64.c:3581` | `shell_help.c:91` | 两处均含 |
| `chgrp` | 同上 | `shell64.c:3582` | `shell_help.c:92` | 两处均含 |
| `mount` | 同上 | `shell64.c:3583` | `shell_help.c:93` | 两处均含 |
| `umount` | 同上 | `shell64.c:3584` | `shell_help.c:94` | 两处均含 |
| `fsck` | 同上 | `shell64.c:3585` | `shell_help.c:95` | 两处均含 |
| `mkfs` | 同上 | `shell64.c:3586` | `shell_help.c:96` | 两处均含 |
| `useradd` | 同上 | `shell64.c:3587` | `shell_help.c:97` | 两处均含 |
| `userdel` | 同上 | `shell64.c:3588` | `shell_help.c:98` | 两处均含 |

## 2. 接线点（按规范锚点追加，不重排既有分支/条目）

### 32 位（i386）
- 头文件：`kernel/include/shell_admin.h`（声明 `shell_admin_dispatch()` 与测试辅助）。
- 实现：`kernel/sched/shell_cmd_admin.c`（新建；32 位源文件由 Makefile `find kernel -name "*.c"` 自动发现，无需登记 `C_SOURCES`）。
- 分派链：`kernel/sched/shell_execute.c`
  - 顶部 `#include "shell_admin.h"`；
  - 在长 `else if` 链**末尾、最终 unknown 兜底之前**插入集中分派块：
    `} else if (shell_admin_dispatch(cmd, args)) { ... } else { ...not found... }`（`shell_execute.c:500`）。
  - 既有分支顺序未改动。
- 帮助表：`kernel/sched/shell_help.c` 新增 ADMIN 组 9 条（`help_entry_t` 五列：name/group/syntax/desc/ex）。
- 复用辅助：`kernel/fs/vfs.c` 新增只读 `vfs_path_busy()`（扫 fd_table 路径前缀判定忙碌），声明在 `kernel/include/vfs.h`。

### 64 位（x86_64）
- 实现：`kernel/arch/x86_64/shell_cmd_admin64.c`（新建）。
- 构建登记：`kernel/arch/x86_64/Makefile` 的 `C_SOURCES` 在 `shell64.c` 后追加该文件；`vpath` 已覆盖 `ARCH_DIR`，未改 vpath 行。
- 命令表：`kernel/arch/x86_64/shell64.c`
  - 表前 `extern shell_admin64_dispatch` + 静态适配器 `shell_admin64_adapter(int argc, char **argv)`（手动拼回参数字符串；64 位 lib 无 `strcat/strncat`）；
  - `shell_commands[]` 在 history 条目后、`{NULL, NULL}` 前追加 9 条 `(void*)shell_admin64_adapter`。
- 补全数组：两处 `static const char *cmds[]`（Tab 补全表、which 表）均补入上述 9 个命令名。

### KUnit
- 套件：`kernel/kunit/kunit_admin.c`（7 例）；注册于 `kernel/kunit/kunit_registry.c`（extern + `kunit_admin_register()` 调用，排在 `kunit_p3_security_register()` 之后、`kunit_system_manager_register()` 之前）。

## 3. 交接项核对：`journal` / `log` / `service`

本路接手时核对：64 位两处 `cmds[]` 数组中 `journal`、`log`、`service` **均已存在**（各出现 2 次，分别在两处数组），无需补录。32 位分派链原有 `journal"/"log`、`service` 分支保持不动。

## 4. 已验证对齐 / 未对齐现状

- 已对齐：上述 9 个命令在 32/64 命令表、帮助（32）、补全数组（64）两侧同名注册；语义边界一致（数字模式、拒绝路径、错误码就近映射）。
- 未对齐（历史现状，非本路引入，不在本轮闭合）：
  - 32 位存在一批调试/子系统命令（`apps`/`gesture`/`phone`/`touchtest`/`sec`/`ktest`/`loglevel` 等）在 64 位表中无对应条目；64 位存在一批 coreutils（`awk`/`grep`/`sed`/`cp`/`mv`/`rm`/`mkdir` 等）在 32 位无对应条目。属跨路后续工作。
- 语义不对称（如实记录，见 `OPEN-05`）：
  - `fsck --fix`：32 位接既有 `ide_ext2_fsck_fix()`；64 位 `ext2_fsck_run` 仅只读检查，修复路径预留未接。
  - 忙碌判定：32 位接 VFS fd 表；64 位未接 fd 表，仅做非空目录判定。
  - 第二 ext2 实例挂载：两边均不落地（单上下文限制），命中即明确报降级。
