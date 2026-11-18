# diag 权限审计段 —— 32 位 QEMU 实跑取证记录

- 分支：`test/diag-audit`，基点 `391e1db`（main）
- 架构：32 位 x86（i386，multiboot 直启 `kernel.elf`）
- 取证日期：2026-11-18
- 证据目录：`evidence-diag-audit/`（本文件同目录相对路径）

## 1. 本次验证了什么

`95eed32` 已把能力授权审计环接上系统调用与诊断应用：

- 内核侧：`SYS_CAP_AUDIT = 237`（`kernel/include/syscall.h`），处理函数
  `sys_cap_audit`（`kernel/arch/x86/syscall.c`），从审计环 `cap_audit_read`
  拷出记录后 `copy_to_user`；审计环实现见 `kernel/security/capability.c`
  （32 位那套；`kernel/arch/x86_64/capability64.c` 是 64 位另一套，未作为本次结论依据）。
- 用户态：`user/apps/diag/diag.c` 末尾「权限审计：最近被拒绝的操作」段，
  一次取 32 条，按二进制结构解码后打印域 / 句柄 / 所需权利 / 原因。

本次在 32 位 QEMU 实跑了两种状态：**空环态**（无拒绝记录）与**真实拒绝态**
（环内落 2 条记录，diag 正确解码四个字段）。

## 2. 环境与复现命令

### 2.1 工具链环境

```bash
export WT=/home/user/Doubao/chats/38452875115179010/wt-diag-audit
export CHAT=/home/user/Doubao/chats/38452875115179010
export T="$CHAT/.toolchain/pkg/root2"
export PATH="$T/sbin:$T/usr/bin:$PATH"
export LD_LIBRARY_PATH="$T/usr/lib/x86_64-linux-gnu:$T/lib/x86_64-linux-gnu"
export QEMU_MODULE_DIR="$T/usr/lib/x86_64-linux-gnu/qemu"
# QEMU BIOS/pc-bios 目录（注意不是 root2/pkg/datadir）
export BIOS="$CHAT/.toolchain/pkg/datadir"
```

QEMU：`qemu-system-i386` 6.2.0。

### 2.2 编译（串行化，-j2）

```bash
cd "$WT"
flock /tmp/hepu-build.lock -c 'make -j2'
# 产物：build_v250/kernel.elf、build_v250/initrd.img、build_v250/user/diag.hug
```

### 2.3 启动 QEMU（multiboot 直启 ELF，纯 .bin 会静默不动）

```bash
qemu-system-i386 -L "$BIOS" \
  -kernel "$WT/build_v250/kernel.elf" -m 256 -smp 1 -display none \
  -monitor unix:<私有实例目录>/mon.sock,server,nowait \
  -serial file:<私有日志路径>.serial.log -no-reboot
```

本次两次取证均给 150s 启动窗口后按 pidfile 精确结束实例；串口日志落在
worktree 私有目录 `.qemu-logs/`，归档副本在 `evidence-diag-audit/`。

### 2.4 输出如何落到串口

diag 的 `printf` → 用户态 `SYS_PRINT`（`sys_print`，
`kernel/arch/x86/sys_hepu.c`）→ 同时 `vga_puts` 与 `serial_puts(0x3F8, ...)`。
因此审计段输出**本就进 COM1 串口**，不需要临时 tee；串口日志即应用 stdout。

## 3. 链路核对（取证前静态确认）

| 环节 | 位置 | 结论 |
|---|---|---|
| 系统调用号 | `kernel/include/syscall.h:172` `SYS_CAP_AUDIT 237`，`SYS_MAX 238` | 237 在分发表范围内 |
| 内核处理 | `kernel/arch/x86/syscall.c` `sys_cap_audit` | 参数 `a1=用户缓冲, a2=最多条数`；`a1==0 \|\| a2==0 \|\| a2>64` 返回 `-1`；`n=cap_audit_read(...)`，`n<=0` 原样返回 `n`；否则 `copy_to_user` 拷 `n*sizeof(rec)`，拷贝失败返回 `-1` |
| 审计环 | `kernel/security/capability.c` | 256 条定长环；仅拒绝时 `cap_audit_record` 留痕；`cap_audit_read` 旧→新拷出 |
| 32 位校验入口 | 同上 `cap_check(domain, handle, rights)` | 拒绝分支：域/句柄越界→reason 4；句柄无效→reason 1；权利不足→reason 2；超 base→reason 3 |
| 用户态消费 | `user/apps/diag/diag.c` 审计段 | `hepu_syscall2(237, recs, 32)`；`(int)n<=0` 打印 `(暂无记录)`，否则逐行解码 |

**32 位当前没有可从用户态直接触发 `cap_check` 的入口**：全树（排除
`arch/x86_64/`）grep 不到新三参 `cap_check` 的调用方；`kernel/kunit/kunit_security.c`
里同名的 `cap_check(proc, cap)` 是另一套旧 POSIX 能力模型，与本环无关。
因此拒绝记录是用内核侧临时探针直接调 `cap_check` 制造的（见第 6 节，已全部还原）。

## 4. 证据 A：空环态（正常无记录）

日志：`evidence-diag-audit/run-A-empty/serial.log`，段摘录
`evidence-diag-audit/run-A-empty/diag-section.txt`。

审计段原文：

```text
[权限审计：最近被拒绝的操作]
  [debug-audit] raw syscall n=0
  (暂无记录)
```

- **syscall 返回值**：`raw syscall n=0`。这是 `cap_audit_read` 返回 0、
  `sys_cap_audit` 原样返回 0 的正常空环路径；**不是**返回 `-1` 被 diag
  打印成「暂无记录」。区分口径：`sys_cap_audit` 仅在参数非法或
  `copy_to_user` 失败时返回 `-1`；空环返回 0。
- diag 随后走完剩余段落并正常退出：

```text
========================================
  诊断完毕。系统归人，数据归己。
========================================
[AUTO] diag hug_run rc=0x00000000
[AUTO] diag done
```

## 5. 证据 B：真实拒绝态（2 条记录，四字段解码核对）

日志：`evidence-diag-audit/run-B-denied/serial.log`，段摘录
`evidence-diag-audit/run-B-denied/diag-section.txt`。

临时探针在 diag 启动前直接调 `cap_check` 制造两条拒绝（探针已还原，见第 6 节）：

| # | 入参 `cap_check(domain, handle, rights)` | 期望 reason | 内核返回 |
|---|---|---|---|
| 1 | `(0, 0, 0x1)`（`CAP_RIGHT_READ`） | 1 句柄无效/已撤销 | 0（拒绝） |
| 2 | `(64, 0, 0x3)`（`READ\|WRITE`，域号越界） | 4 域号越界 | 0（拒绝） |

探针串口原文：

```text
[AUDIT-PROBE] #1 ret=0x00000000 expect domain=0 handle=0 rights=0x1 reason=1
[AUDIT-PROBE] #2 ret=0x00000000 expect domain=64 handle=0 rights=0x3 reason=4
```

diag 审计段原文：

```text
[权限审计：最近被拒绝的操作]
  [debug-audit] raw syscall n=2
  域    句柄 所需权利 原因
  0      0      0x1        句柄无效/已撤销
  64     0      0x3        域号越界
  共 2 条拒绝记录
```

字段核对（探针入参 ↔ diag 解码行）：

| 记录 | domain_id | handle_idx | required_rights | reason | diag 解码 |
|---|---|---|---|---|---|
| #1 | 0 | 0 | 0x1 | 1 | `0  0  0x1  句柄无效/已撤销` |
| #2 | 64 | 0 | 0x3 | 4 | `64  0  0x3  域号越界` |

四字段全部对得上；`raw syscall n=2` 与「共 2 条拒绝记录」一致。

diag 退出码：

```text
[SYSCALL] proc exit(code=0x00000000)
[AUTO] diag hug_run rc=0x00000000
```

diag `main` 返回 `fail_count > 0 ? 1 : 0`，本次 `失败: 0项`，退出码 0；
`crt0` 把 `eax=0` 经 `SYS_EXIT` 交给内核。

## 6. 二进制结构消费说明（不是文本 grep）

diag 按定长结构从内核缓冲逐字段解码，不做任何文本匹配。两侧结构对照
（主机 gcc 实测 `sizeof` 与偏移）：

| 字段 | 内核 `cap_audit_rec_t`（`kernel/shared/capability/hepu_cap.h`） | diag 本地 typedef（`user/apps/diag/diag.c`） | 偏移 |
|---|---|---|---|
| `ts` | `uint64_t` | `unsigned long long` | 0 |
| `domain_id` | `int32_t` | `int` | 8 |
| `handle_idx` | `int32_t` | `int` | 12 |
| `required_rights` | `uint32_t` | `unsigned int` | 16 |
| `reason` | `uint8_t` | `unsigned char` | 20 |

两侧 `sizeof = 24`，偏移完全一致；`sys_cap_audit` 按
`n * sizeof(cap_audit_rec_t)` 拷给用户态。diag 打印的域 / 句柄 / 权利 /
原因均取自结构字段，reason 码在 diag 侧枚举映射为文案
（1 句柄无效/已撤销，2 权利不足，3 超出能力上限，4 域号越界）。

## 7. 临时改动清单与还原证明

取证脚手架分两轮，下列临时项**最终提交均不保留**；取证后已
`git checkout --` 还原：

| 文件 | 临时内容 | 还原方式 |
|---|---|---|
| `kernel/sched/shell.c` | 把 `diag`（`.hug` 原生应用，走 `hug_run("diag")`）临时提到自检序列最前；B 轮两条 `cap_check` 拒绝探针；串口标记 | `git checkout -- kernel/sched/shell.c` |
| `user/apps/diag/diag.c` | A/B 轮：临时打印审计 syscall 原始返回值 `n`、调试分段标记、临时绕过版本行；C 轮：临时打印 `raw syscall n=`（最终源码已去掉该行，仅 §8.1 的版本解码 hunk 保留为正式改动） | 临时埋点行已删除；版本解码 hunk 见 §8.1 |
| `kernel/arch/x86/sys_process.c` | 临时把 `sys_exit` 退出码镜像到串口 | `git checkout -- kernel/arch/x86/sys_process.c` |

还原后核对（C 轮收尾）：

```text
$ git diff 391e1db -- kernel/sched/shell.c | wc -l
0
$ git diff 391e1db -- user/apps/diag/diag.c | grep '^+'
（仅 §8.1 版本解码 hunk，无临时埋点行）
```

## 8. 版本行卡死问题与遗留观察

### 8.1 `hepu_info()` 返回编码 u32 被当字符串指针 —— 已修复

**根因**：`sys_hepu_info`（`kernel/arch/x86/sys_hepu.c:18-36`）自 V7.66
起出于地址泄露防护，不再返回内核字符串指针，改为返回编码版本号：

```text
返回 u32 = (major << 24) | (minor << 16) | (patch << 8)
"1.0.2" -> 0x01000200
```

用户态包装 `hepu_info()`（`user/libhepu/include/hepu/syscall.h:68`）的返回
类型标注 `const char *` 是历史遗留，实际拿到的是这个 u32。基线 `diag.c:92`
把返回值当字符串指针传给 `%s`，在 32 位 QEMU 解引用 `0x01000200`，
diag 卡在版本行之前、到不了审计段（A/B 两轮取证只能临时绕过该行）。

**修复方式**：只改 `user/apps/diag/diag.c` 版本行这一个局部 hunk，把返回值
按 `unsigned int` 解码回三段版本号打印；**未动内核 syscall ABI、未改 syscall
号、未动审计段与诊断表**：

```c
/* sys_hepu_info 返回编码版本号 (major<<24 | minor<<16 | patch<<8)，不是字符串指针 */
{
    unsigned int ver = (unsigned int)hepu_info();
    printf("  内核版本: %u.%u.%u\n",
           (ver >> 24) & 0xFFu, (ver >> 16) & 0xFFu, (ver >> 8) & 0xFFu);
}
```

**证据（`evidence-diag-audit/run-C-clean-diag/`）**：

- `serial.log` + `diag-section.txt`：带临时 `raw syscall n=` 打印的复跑，
  版本行解码为 `1.0.2`，审计段 `raw syscall n=0` → `(暂无记录)`，随后到达
  `[建议]` 与诊断完毕横幅，`hug_run rc=0x00000000`。
- `diag-section-final-source.txt`：去掉临时调试行后的**最终提交源码**复核 boot，
  同样从横幅跑到 `内核版本: 1.0.2`、`(暂无记录)`、`[建议]`，`rc=0x00000000`。

**未修的同类 `%s` 误用调用点（本轮只列出，不修）**：

| 文件:行 | 现状写法 | 现象 |
|---|---|---|
| `user/apps/hello_c/hello.c:18` | `printf("内核版本: %s\n", hepu_info());` | 编码 u32 被当字符串指针，32 位下卡在版本行 |
| `user/apps/sysinfo/sysinfo.c:19` | `printf("  内核版本: %s\n", hepu_info());` | 同上 |
| `user/apps/top/top.c:43` | `printf("  内核: %s\n", hepu_info());` | 同上 |

> 备注：`user/apps/perf/perf.c:132` 未调用 `hepu_info()`（硬编码版本串）；
> `user/apps/notepad/notepad.c:81` 仅字符串字面量提及该 API，均非误用。

### 8.2 32 位尚无用户态可触发的能力校验入口

新三参 `cap_check` 目前只被 B 轮临时探针直接调用；后续把某条 syscall / IPC
路径接到 `cap_check` 后，可去掉探针，用真实业务拒绝复测本审计段。

### 8.3 审计记录时间戳

审计记录 `ts` 在 32 位侧恒为 0（暂无统一单调时钟），diag 段也不展示时间戳。
