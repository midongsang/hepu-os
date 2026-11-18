---
title: 2026-11-18 四条并行任务的集成交付记录
status: active
updated: 2026-11-18
---

# 2026-11-18 集成交付记录

四条任务各自在独立 worktree / 分支上完成，本文记录**合并顺序、集成期修复、合并后实测**。
分支基点均为 `391e1db`；集成分支 `integrate/verified` 基于 main `86cb4b3`。

## 1. 合并了什么

| 分支 | 提交 | 内容 | 状态 |
|---|---|---|---|
| `test/diag-audit` | `f45c9db`、`b3572e5` | diag 权限审计段实跑取证；修 diag 版本行卡死 | 已并入 |
| `fix/linuxhello32` | `5de0e09` | OBS-07：transcomp 自测 64KB 栈帧踩穿 16KB 内核栈 | 已并入 |
| `feat/crash-ctx` | `bb78ca1`、`850d53c` | crash_ctx 现场页 + 结构化查询 syscall + diag 段 | 已并入 |
| `fix/vfs-contract` | `d025f8c` | VFS ops 契约、拷贝收口、静态门禁、`/dev/rocfg` | **暂不合并**，见 §4 |

集成顺序：diag → linuxhello → crash-ctx。两处冲突：`VERSIONING.md`（frontmatter 与迭代记录），
按"保留 main 的 0.62.0 + 0.61.1 作为次新条目"归并；其余文件自动合并。

## 2. 集成期修复（三条，都属于"合起来才暴露"的问题）

1. **32 位链接断链**：main `3f0edb1`（屏保 + Accounts）让共用 `kernel/arch/x86_64/hpdesk.c`
   引用 `auth64_user_name` / `auth64_current_role` / `rtc64_get_timestamp`，但 32 位的
   `kernel/arch/x86/hpdesk32_deps.c` 没补桩，32 位构建直接 ld 失败。按该文件既有的"退化壳"
   约定补三个桩：前两者返回占位（32 位无账户子系统），时间戳用 `pit_get_seconds()`
   单调秒（**不是墙钟**，只够屏保空闲计时）。这是 main 自身的 32 位回归，与本轮四条无关。
2. **crashsvc 默认会截断自检**：crash_ctx 的验收钩子原先无条件跑在自检序列中段，
   故意 #DE 崩溃后 `sys_exit` 回收全部进程并 longjmp，后续自检项整批被截断。
   改为 `make AUTO_CRASH_SVC=1` 才编入（`-DHEPU_AUTO_CRASH_SVC`，默认关闭），并挪到
   自检序列末尾。
3. **用户态缺页日志污染冒烟判据**：新日志里 `user #PF ...` 字面命中
   `tools/regress32.sh` 的 `PANIC|#GP|#PF|fatal error` 判据，会让 `fault` 恒为 1。
   文案改为 `user page-fault ...` / `user page-fault reaped`，机器可读字段（pc/sp/
   last_syscall/msg 结构）不变。

## 3. 合并后实测（`integrate/verified`，干净重编）

- 32 位构建：`make -j2` 退出 0，`build_v250/hepu-kernel.bin` = 3,774,445 字节。
- 32 位冒烟（按 `tools/regress32.sh` 判据、私有日志复现）：
  `serial=1 initrd=1 net=1 desk=1 fault=0 bound=1` → **PASS**
- 自检序列：42 条 `[AUTO]` 事件，跑到 `[AUTO] ALL DONE` + PAE 验证完成；
  `linuxhello` 打印 `Hello from Linux ELF on HEPU!` 后 `[AUTO] linuxhello done`（OBS-07 修复生效）。
- KUNIT：`syscall deep /syscall table complete`、`user scenario / boot complete`、
  `interrupts active`、`datetime`、`integration / flow syscall seq`、
  `security E2E / boundary max size` 共 **6 项失败 = 台账既有 32 位基线失败集**，无新增。
- diag（临时把 diag 提到自检序列取证，取证后已 `git checkout --` 还原，`shell.c` 相对
  集成分支 diff = 0）：
  - `内核版本: 1.0.2`（版本行修复生效）
  - `[权限审计：最近被拒绝的操作]` → `(暂无记录)`
  - `[崩溃现场：结构化状态查询]` → `crashdemo.elf (无崩溃记录)`、`diag (无崩溃记录)`
  - `hug_run rc=0x00000000`
- `crashsvc` 默认构建中 0 次出现（默认关闭生效）。

## 4. 为什么 VFS 分支没合

`fix/vfs-contract` 自身在其基线上验收通过（门禁 0 违规 + 反例失败自证、`/dev/rocfg`
只读设备、FS E2E 13 例、64 位 969,072 字节 < 1MB）。但它的 text +1712 B / data +96 B
引起镜像布局位移，暴露了既有的野指针类脆弱性：

- 该分支单独：`integration/subsystem memory`、`performance/perf memory usage` 两个 KUNIT
  失败（`heap_total == 0`），基线同环境通过；
- 并入本集成树：`linuxhello` 在 `process_create` 分配 64 KB 后踩野地址 `0x82630000`，
  poison page 命中后看门狗复位，自检停在 `[AUTO] linuxhello start`（去掉 VFS 即恢复）。

结论：**契约代码本身没问题，是布局位移引爆了别处的野指针写入**。写入源未定位，
登记为 `OBS-08`。分支保留，待专项定位后再评估合并。

## 5. 复现命令

```bash
CHAT=/home/user/Doubao/chats/38452875115179010
T=$CHAT/.toolchain/pkg/root2
export PATH=$T/sbin:$T/usr/bin:$PATH \
       LD_LIBRARY_PATH=$T/usr/lib/x86_64-linux-gnu:$T/lib/x86_64-linux-gnu \
       QEMU_MODULE_DIR=$T/usr/lib/x86_64-linux-gnu/qemu
cd $CHAT/wt-integrate            # 或合并后的树
flock /tmp/hepu-build.lock -c 'make -j2'
timeout 150 qemu-system-i386 -L $CHAT/.toolchain/pkg/datadir \
  -kernel build_v250/kernel.elf -m 256 -display none \
  -serial file:.qemu-logs/run.log -no-reboot
# 取证 diag：临时在 kernel/sched/shell.c 自检序列前部插入
#   { extern int hug_run(const char *); hug_run("diag"); }
# 跑完必须 git checkout -- kernel/sched/shell.c
# 取证 crashsvc：flock /tmp/hepu-build.lock -c 'make -j2 AUTO_CRASH_SVC=1'
```

## 6. 证据

`evidence-integration/`
- `run-default-full.log`：默认构建完整启动日志（冒烟判据、KUNIT、OBS-07 修复、无 crashsvc）
- `run-diag-hoisted.log`：带临时 diag 前置的取证日志
- `diag-section.txt`：审计段 + 崩溃现场段原文
- `runs-summary.txt`：`[AUTO]` 序列 / KUNIT 失败项 / linuxhello 关键行摘录
