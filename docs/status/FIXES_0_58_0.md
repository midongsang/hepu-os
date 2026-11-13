---
title: FIXES 0.58.0
status: active
sys_version: 0.58.0
updated: 2026-11-13
owner: status
source_of_truth: null
supersedes: null
---

# v0.58.0 修复与增强记录

本版主线是**把 `clone` 从"ENOSYS 空桩"做成真线程**，并在过程中发现并修复了
调度器与中断返回路径上三处结构性缺陷。章节编号接续 `FIXES_0_57_0.md`（§1.84 起）。

## 本版结论速览

| 项 | 状态 |
|---|---|
| 共享父地址空间 | ✅ 实证 |
| 独立用户栈（0x6FF80000） | ✅ 实证 |
| 子线程 `rax=0` | ✅ 实证（根因：IRQ 帧偏移差 2 槽） |
| 独立内核栈 | ✅ 每线程一页，`g_syscall_kstack_slot` 按线程切换 |
| 共享页表引用计数 | ✅ `g_pt_ref` + `[CLONE] aspace kept` |
| 两线程交替运行 + 共享内存可见 | ✅ `PASS: child ran in shared address space, both counters advanced` |
| 线程退出接口 | ✅ `SYS_HEPU_THREAD_EXIT=235`，不终止整进程 |

---

## §1.84 clone 真线程：基础设施就位，卡在"用户线程不是调度单元"

### 做了什么

把 `sys_clone` 从空桩推到完整基础设施，四块都落在代码里：

1. **汇编侧**（`entry_64.S`）：新增 `g_clone_child_rip/rsp/rflags/tls/kstack/gprs[15]`
   六个槽与 `clone_trampoline_64`。子线程进入时先把自己的内核栈装进
   `g_syscall_kstack_slot`（每线程独立 syscall 栈），再按 fork 同款顺序恢复
   12 个 callee-saved GPR，`movq g_clone_child_rsp,%rsp` 换到调用方传入的栈，
   可选 `wrmsr 0xC0000100` 写 FS_BASE，`swapgs` 后 **`xorq %rax,%rax`**（子线程
   clone 返回 0），`sysretq`。**不切 CR3** —— 与父共享页表。
2. **内核侧**（`syscall64_full.c`）：`CLONE_*` 常量、`CLONE_VM`/栈窗口守卫、
   一页 syscall 内核栈分配、15 个 GPR 快照、`process64_create("clone_thr")`、
   `user_process_clone_track()`、按 flags 用 `copy_to_user` 写 `parent_tid`。
3. **进程侧**（`user_process.c`）：`g_pt_ref[]` 页表共享引用计数，
   `release` 里计数 >0 时打印 `[CLONE] aspace kept` 并保留地址空间，不释放。
4. **用户侧**：`ulib.h` 补 `syscall4`（r10）/ `syscall5`（r10+r8），
   新增 `user/apps/clone_test.c`（共享标志 + futex 自阻塞 + PASS/FAIL 判据）。

### QEMU 实测：线程建出来了，但跑不起来

```
[PROC64] Created process: clone_thr (pid=4, prio=2)
[CLONE] thread registered, pt_ref=0x1
[CLONE] created thread pid=0x4 (shared pml4, stack=0x6FF80000)
parent: clone returned tid
[KSVC] beat alive
FAIL: child write not visible to parent
[EXCEPTION] vector=0xE error=0x15 rip=0x0  CR2=0x0  rsp=0x6FFFF008
[CLONE] aspace kept: live clone threads=0x1
```

建线程、槽登记、页表记账、父返回 tid 全部正常；**子线程一次都没被调度**，
父线程忙等 3 亿次后自身 `rip=0 / CR2=0` 崩掉。

### 根因（已定位，不是 clone 自身的问题）

`process64_tick()`（`pit64.c` 的 PIT 中断）只轮转 `process64` 表里的**内核线程**；
`user_process_switch()` 进去的那条用户执行流**根本不在调度表里**，没有
rip/rsp/rflags/GPR/fsbase 的保存槽位。`smp.c::smp_timer_tick` 里 AP 调度还被
`if (cpu_id != bsp_cpu_id && 0)` 硬关掉。后果有两条：

- 用户态父线程不会被打断 → 子线程永远停在 READY，拿不到 CPU；
- 一旦某次 `context_switch` 把 CPU 抽走，父线程**没有恢复路径**，回来时
  上下文已经是坏的（`rip=0`）。

fork 能跑通正是因为它绕开了这个缺口：`sys_fork` 快照整份父上下文到
`g_fork_parent_*`，子进程退出时由 `fork_return_to_parent` **显式**把父恢复回来，
全程不依赖抢占。

### 因此本轮的决定

`sys_clone` 内部保留完整实现，但由 `g_clone_threads_enabled`（默认 0）拦在
最前面返回 `HEPU_ENOSYS`，注释里写清为什么。理由：打开它只会让调用方卡死或
崩，比返回 ENOSYS 更糟。`clone_test.elf` 仍打进 `/bin`（32 个），但**不进 autorun**。

真正打开的前置条件是**让用户线程成为调度单元**：给每条用户执行流一份
rip/rsp/rflags/GPR/fsbase 上下文，由中断里的调度器轮转并保证恢复。这是一个
独立里程碑，工作量与风险都远超"补一个 syscall"。

### 验证（QEMU 实机）

```
[REG] exec /bin/hello.elf
  Hello World from ELF64 user space!
[REG] exec /bin/sec_test.elf
[REG] autorun done
[NOTIFY] Welcome to Hepu OS: Hepu Desktop loaded
```

`EXCEPTION=0`、桌面正常（截图 `hepu_clone_verified.png`，14:40）。
内核 824,136 B。

---

## §1.85 用户线程调度单元（uthread64）：切换成立，子线程未推进

§1.84 定位到 clone 的卡点是"用户执行流不在调度表里"。本轮按这个结论补调度单元。

### 已实现

1. **新模块 `kernel/arch/x86_64/uthread64.c`**（`Makefile` 已接入）
   `uthread_t` 保存每条用户执行流的完整现场（`raw[15]` 对应中断帧
   `r15..rax`，加 `rip/rflags/rsp/fsbase`）。API：
   `uthread_bind_main` / `uthread_create` / `uthread_timer_tick` /
   `uthread_clear_owner`。
2. **接入真实的 tick 路径**：PIT 走的是 **IRQ0（vector 32）**，不是
   APIC 定时器的 0x40。第一版接在 0x40，实测从未触发；改到
   `lib64.c` 的 `irq == 0` 分支后立刻生效。
3. **`ut_restore` 显式写 `IA32_FS_BASE`**：iret/sysret 不恢复它。
4. **时间片 10 tick**（约 10ms），避免每 1ms 切一次把线程推进度压到零。
5. **`process64_tick` 在用户态直接让路**：原来它在用户态 tick 里也会
   `process64_schedule()`，`context_switch` 会把栈换掉，IRQ 返回时
   从别人的栈弹寄存器 —— 实测表现为 `rip=0`、rax 变垃圾。
6. `user_process_create_elf` 登记主线程；`user_process_release` 清理该
   owner 的所有线程。单线程进程 `n<2` 直接 return，行为与之前完全一致。

### QEMU 实测（`/tmp/ser_clone*.log`）

```
[UTHREAD] created tid=0x3E8 owner=0x0
[UT] next rax=0x0 rip=0x40000024 rsp=0x6FF80000 cur_rax=0x400000AA
[UTSAVE] thread=0x1 rip=0x40000024 rax=0x0 rsp=0x6FF80000   (连续 3 次)
parent: clone returned tid
FAIL: child never ran
```

- ✅ 线程注册、帧注入都对：注入的 `rax=0`、`rip=0x40000024`（clone 返回点）、
  `rsp=0x6FF80000`（调用方给的独立栈）与设计完全一致。
- ✅ `g_ut_cur` 在 0/1 之间交替，说明轮转逻辑在跑。
- ❌ 子线程现场连续三次**数值分毫未变**（`rip` 仍停在返回点，`rax` 仍为 0），
  即 iret 没有让它推进哪怕一条指令。

### 结论与现状

开关 `g_clone_threads_enabled` 仍为 **0**（`sys_clone` 对外 ENOSYS），
`clone_test.elf` 仍在 `/bin`（32 个）但不进 autorun。理由：调度器已能切换，
但被切过去的线程不推进，打开只会让调用方卡死。

下一轮要查的点：IRQ 返回路径对帧的使用（`IRQ_ENTRY` 的 pop 顺序 vs
`stack_frame` 索引是否在恢复段被其它 handler 二次改写），以及 syscall 帧与
IRQ 帧布局差异是否让"从 syscall 返回点启动的线程"落在错误的地方。

### 验证（QEMU 实机）

```
[REG] autorun done
[NOTIFY] Welcome to Hepu OS: Hepu Desktop loaded
```

`EXCEPTION=0`、`WATCHDOG=0`、桌面正常（截图 `hepu_uthread_verified.png`）。
内核 825,160 B。

---

## §1.86 V0.57.6：clone 真线程跑通 —— IRQ 帧偏移根因与实证

### 现象
上一轮（§1.85）`uthread` 切换链路已成立：`g_ut_cur` 在 0/1 交替、帧里打印的
`rax=0 / rip=0x40000024 / rsp=0x6FF80000` 全部"正确"，但子线程现场三次数值分毫未动，
父始终打印 `FAIL: child never ran`。

### 定位方法（改用 QEMU 自带能力，不引外部依赖）
1. `-d int -D /tmp/qemu_int.log`（16.5 MB 硬追踪）：用
   `grep -a 'SP=001b:000000006ff8'` 捞出 **1249 条落在子线程栈上的中断**，
   全部 `IP=0x400000bb`、`R_EAX` 从 `0x1418` 递增到 `0x97a7`。
2. `0x400000bb` 正是 `for (i < 200000) g_parent_count++` 的循环计数初始化 ——
   **子线程在跑，但跑进了父分支**，所以 `g_child_count/g_child_done` 永远是 0。
3. 哨兵实验：把 `raw[14]` 临时写 `0xDEADBEEF`，运行时寄存器仍不是该值；
   但读回帧打印 `frame=0x1E3140 f14=0xDEADBEEF f19=0x40000024 f22=0x6FF80000`
   —— **写帧成功，问题在偏移**。
4. gdbstub 条件断点 `break *0x400000bb if $rsp == 0x6ff80000` 命中：
   `SUB rip=0x400000bb rsp=0x6ff80000 rax=0x1 / cr3=0x25dd000 cs=0x23`
   —— 页表、环级都对，就是 rax 不对。

### 根因
`entry_64.S` 的 `IRQ_ENTRY` 在压完 15 个 GPR 之后才压 `ds`/`es`，所以这两者在
**栈的最低地址**。`stack_frame` 指向最低地址，真实布局是：

```
f[0]=es  f[1]=ds  f[2]=r15 f[3]=r14 ... f[14]=rcx f[15]=rbx f[16]=rax
f[17]=vector f[18]=error f[19]=rip f[20]=cs f[21]=rflags f[22]=rsp f[23]=ss
```

而代码按 `f[0]=r15` 计算，**整体差 2 个槽**：写 `f[14]` 实际写的是 `rbx`，
`rax` 一直保留父线程的返回值 → 子线程 `test %rax,%rax; jne` 直接跳进父分支。

### 修复
- `uthread64.c` 引入 `UT_FRAME_GPR_BASE 2`，`ut_save`/`ut_restore` 统一按
  `f[UT_FRAME_GPR_BASE + i]` 存取 15 个 GPR；`f[19]/f[21]/f[22]` 不变（本就正确）。
- 每个 uthread 分配**独立内核栈**（`kstack_top`），切换时写 `g_syscall_kstack_slot`，
  避免父子线程共用同一个 syscall 内核栈互相踩。
- `sys_exit` 开头先 `uthread_clear_owner()`，退出过程不再被 tick 切到同进程其它线程。

### 实证
`/tmp/ser_fix1.log`、`/tmp/ser_f3_1.log`：

```
[CLONE] thread registered, pt_ref=0x1
[CLONE] thread tid=0x3E8 (shared pml4, entry=0x40000024, stack=0x6FF80000)
parent: clone returned tid
PASS: child ran in shared address space, both counters advanced
```

共享地址空间、独立用户栈、子 rax=0、两线程交替推进计数器，四项全部成立。

### 遗留
- 进程收尾阶段仍有 `vector=0xE error=0x15 rip=0x0` 的 #PF（见 OPEN_BUGS SEC-21），
  异常被用户态 fault 处理器捕获、进程终止后系统继续，桌面正常加载。
- 两次运行出现一次 FAIL 的波动，与收尾 #PF 属同一族问题，尚未完全收敛。

---

## §1.87 V0.57.7：清掉 clone 收尾 #PF，三次运行全绿

§1.86 拿到 `PASS` 后，同一次运行尾部仍有
`[EXCEPTION] vector=0xE error=0x15 rip=0x0`（`rsp=0x6FFFF008`），进程被异常终止。
本轮定位并清掉，两次独立根因：

### 根因一：`uthread_clear_owner` 释放了正在使用的内核栈
`sys_exit` 开头会调 `uthread_clear_owner()`，而该函数把每条线程的
`kstack_top - 4096` 还给 `pmm64_free_page`。此刻 CPU **正踩在这块页上**执行
syscall 返回路径，页被回收复写后，栈上的 `rcx`（`sysretq` 的 RIP）被踩成 0，
于是返回即跳地址 0 取指 —— 与 `error=0x15`（保护违例/用户态/取指）完全吻合。

修法：`clear_owner` 只清 `used`，**保留 `kstack_top`**，由下次 `bind_main`
/`uthread_create` 复用同一块页（无释放、无 UAF、也无泄漏）。

### 根因二：crt0 用的是 32 位 `int $0x80` 退出路径
`user/libhepu/crt0.S` 结尾是 `pushl $0 / call main / movl $eax,%ebx /
movl $5,%eax / int $0x80`。在被 clone 切换过的上下文里，这条 32 位返回路径
不可靠，`main` 返回后会在回到用户态的一瞬间落到地址 0。

修法：`clone_test` 结尾改用 ulib 的 `exit(0)`（64 位 `syscall`），不再依赖 crt0。
crt0 本身是历史包袱，影响所有程序，本轮不动，另行记录。

### 实证（3 次独立启动，`/tmp/ser_f5_{1,2,3}.log`）
```
PASS=1  FAIL=0  EXCEPTION=0  "Process exited"=3
```
clone 测试、hello、sec_test 三个 autorun 程序全部正常退出，桌面正常加载。

### 稳定性小结
- 帧偏移（`UT_FRAME_GPR_BASE 2`）：子线程 rax 真正为 0
- 每线程独立 syscall 内核栈：父子不再互踩同一块栈
- 内核栈不随退出释放：消除 use-after-free
- 显式 64 位 exit：绕开 crt0 的 int 0x80 返回路径

---

## §1.88 V0.57.8：线程退出接口 —— clone 子线程不再留孤儿

### 缺口
`clone` 子线程此前只能靠 `for (;;)` 收尾。父线程退出后，这条执行流仍留在
用户态空转，没有一个"这条线程结束了"的表达方式 —— 既浪费 CPU，也让
`g_ut` 表里长期挂着一条没有意义的记录。

### 接口
- 新号：`SYS_HEPU_THREAD_EXIT = 235`，`SYS_MAX` 229 → 236（数组要容纳 235 这个下标）
- **号位踩坑**：一开始选 209，看起来是 `#define SYS_*` 的空洞，但
  `syscall64_full.c` 里 `[SYSCALL_UMASK] = wrap_umask` 用的正是 209 ——
  这是**同一个号空间的另一套前缀**，`grep SYS_` 看不见它。重复下标在 GCC 下
  只是覆盖不报错，等于静默把 UMASK 顶掉了。教训：选号必须同时查
  `SYS_xxx` 与 `SYSCALL_xxx` 两套前缀，或直接跑生成器看冲突（ABI 文档开头也写了这一条）。
- 顺带补齐一个长期缺口：`[SYS_HEPU_PS]`（227）同样因为用了 `SYS_` 前缀，
  从来没进过 `SYSCALL_ABI.md`；本轮一并改成 `SYSCALL_HEPU_PS` 别名，现已在表内。
- 用户侧：`ulib.h` 的 `thread_exit()`
- 语义：结束**当前这条用户线程**，不终止整进程

### 实现要点
syscall 返回走的是 `sysretq`，RIP/RFLAGS/RSP 分别取自
`rcx` / `r11` / `g_syscall_ustack_slot`。所以只要在**本线程的内核栈**上
重建目标线程的整套返回帧，`sysretq` 就会直接落到目标线程：

1. `wrap_thread_exit` → `uthread_exit_current()`：
   - 找同 owner、`UT_READY` 的兄弟线程；**找不到就什么都不做直接返回**
     （绝不因一次 `thread_exit` 把整个进程带走）
   - 找到则把当前线程 `used=0`（`kstack_top` 保留复用）、`g_ut_cur=目标`、
     置 `g_thr_redirect_flag`
2. `entry_64.S` 在 `syscall_handler_64` 返回后检查该 flag，
   调 `uthread_apply_redirect(f)`：
   - 15 个 GPR 槽整体换成目标的 `raw[]`
   - **`f[12]=rcx** 换 `t->rip`、**`f[4]=r11`** 换 `t->rflags`
     （这两个槽在 `sysretq` 语义下不是普通寄存器）
   - `g_syscall_ustack_slot = t->rsp`、`g_syscall_kstack_slot = t->kstack_top`

注意 syscall 帧与 IRQ 帧布局不同：syscall 帧没有 `ds`/`es` 那两个槽，
所以 GPR 直接从 `f[0]` 起算，不受 §1.86 的 +2 偏移影响。

### 实证（2 次独立启动，`/tmp/ser_f6_{1,2}.log`）
```
[UTHREAD] thread exit tid=0x3E8 -> switch to tid=0x64
PASS: child ran in shared address space, both counters advanced
[SYSCALL64] Process exited with code 0
```
顺序说明链路是完整的：子线程跑完计数 → 退出并切回父 → 父看到 `g_child_done`
打 PASS → 正常 `exit(0)`。`PASS=1 FAIL=0 EXCEPTION=0` 连续两次，桌面正常。

## §1.89 任务管理器显示用户线程表

**动机**：任务管理器此前只画 `process64` 内核线程与用户进程，用户态 uthread
完全不可见 —— "clone 到底跑没跑"只能看串口。

**做法**：
- `uthread64.c` 新增只读统计接口 `uthread_get_stats()` / `uthread_snapshot()` /
  `uthread_get_last_exit()`，并在 `uthread_create` 里记账累计数与最近一次
  clone 的创建者/被创建者 tid；`uthread_exit_current()` 里记录 thread_exit 的授受关系。
- 任务管理器摘要区新增两行：`THREADS  total: N   active: M`，
  以及 `clone: tid P -> C  RUNNING` / `thread_exit: tid A -> B (handed off)`。

**验证**：内核 828,168 B（+1.3KB），编译无警告；启动串口可读到
`[UTHREAD] created tid=0x3E8`、`thread exit tid=0x3E8 -> tid=0x64`、`PASS`。

**已知限制**：`clone_test` 在 autorun 里同步执行完就退出，任务管理器被打开时
线程表通常已空（只剩累计数与授受关系）—— 要看"运行中"的线程需要让演示进程
常驻，这需要异步 exec（当前内核只支持同步 `user_process_exec`），未在本版实施。

## §1.90 修复 clone 线程偶发不被调度（SEC-23）

**现象**：`clone_test` 偶发 `FAIL: child never ran`，但 `EXCEPTION=0`，
且 `[CLONE] aspace kept: live clone threads=0x1` —— 线程建好了，就是没跑。

**根因**：`uthread_create` 把 `g_ut_slice` 顶到阈值想"下一次 tick 立刻切"，
却没同步 `g_ut_slice_owner`；tick 里 owner 不匹配就把计数复位为 0，
意图失效，退化成等满 10 个 tick，父线程早已进入等待。

**修法**：create 时一并写 `g_ut_slice_owner = owner`（一行），
并新增 `g_ut_n2_ticks` / `g_ut_switches` 两个只读计数，在 `uthread_exit_current`
退出时打印 `[UT] exit ticks2=.. switches=..`，用于事后确认调度是否真的发生。

**验证**：修复前 6 次启动 2 次失败；修复后连续 20 次启动 20/20 PASS，
`switches` 稳定 33~37 次。内核 828,360 B。

## §1.91 把 sleep 的等待挪到用户态（让睡眠期也能轮转）

**问题**：`sys_sleep` 是内核里的 `nop` 忙等（`ms * 100000` 次）。睡眠期间 CPU 被本线程
独占，而用户线程轮转只在"时钟中断 + CPL=3"时触发 —— 内核态等待期间一次都不触发，
于是同进程的其它线程整段睡眠都轮不到。这解释了两件事：

1. 给 `clone_test` 加的 `sleep` 兜底其实帮不上忙（父线程睡在内核里，子线程照样饿死）；
2. 任何"父子线程靠 sleep 交替"的用法在本系统上都是假并发。

**修法**：`user/ulib.h` 的 `sleep()` 不再陷入 `SYS_SLEEP`，改为用户态忙等
（同样的迭代量，毫秒语义不变）。CPU 仍被本线程占着，但时钟中断能正常打断并轮转，
其它线程照常获得时间片。内核 `sys_sleep` 保留，用于内核自身需要的短延时。

**验证（对照实验）**：把 `clone_test` 父线程的 spin 上限压到 `1`，让它几乎不让 CPU、
完全依赖 sleep 兜底：

- 改动前该场景必然 `FAIL: child never ran`（sleep 睡在内核，子线程饿死）；
- 改动后 4/4 PASS，恢复原逻辑再跑 6 次亦 6/6 PASS。

**限制**：这是"用户态忙等"，不是真正的睡眠 —— 不省电、不让出 CPU 配额，
但能让调度器照常工作。真正的阻塞/唤醒式 sleep 需要给 `uthread_t` 加
`wake_tick` 并让 tick 跳过未到期线程，属独立改造。

### §1.92 侧栏 Info 与任务管理器职责去重；任务管理器接真实 CPU 采样

**问题一：同一组占用数据画两遍。**

侧栏 Info 页有 CPU/MEM/DSK 三条占用条（`hpdesk.c` 侧栏渲染），而任务管理器摘要行
同样给出 `CPU: x%  MEM: y%`，两处呈现的是同一组数据。侧栏这三条既没有曲线，
也没有进程级明细，信息密度明显低于任务管理器；同时侧栏内容只占面板上部约三分之一，
下面大片空区。占用指标属于任务管理器职责，侧栏不应重复承担。

修法：删掉侧栏三条占用条，改为两组纯文本行（左标签右对齐值）：
- `STATUS`：Uptime、Processes、Threads(active/total)、Net RX、Net TX
- `SYSTEM`：Version、Kernel、CPU(core 数)、Memory(总量)

数据源分别为 `pit64_get_ms()`、`process64_count()`、`uthread_get_stats()`、
`e1000_get_net_stats()`、`smp_get_cpu_count()`、`pmm64_get_stats()`。右对齐用
`font64_get_string_width()` 实测文本宽度，避免靠空格凑对齐。

**问题二：任务管理器里的 CPU 没接上真采样。**

`hs_apps.c` 的摘要行与 CPU 曲线都调 `hs_process_get_cpu_usage()`
（`hsservices.c:272`），该函数直接返回 `g_proc.total_cpu` —— 这个字段全仓没有
任何地方更新，因此**恒为 0**：数字永远显示 `CPU: 0%`，曲线永远是一条平线。

修法：两处改用 `process64_cpu_usage()`（`process64_full.c:657`），它由 PIT 调度
tick 累计 `g_total_ticks` 与 idle 进程 tick 得出真实占用率，且已有 `g_total_ticks < 50`
的启动期保护。

**验证**：内核 829,000 B（+768 B）；连续 2 次启动 2/2 PASS、`EXCEPTION=0`；
侧栏实机截图见 `docs/assets/desktop-0.58.0.png`（STATUS/SYSTEM 两组，占用条已移除）。
任务管理器窗口侧的截图受 SEC-24（桌面主循环十余轮后停止推进）影响未能取到，
CPU 采样改动已编译进镜像并通过启动回归，待 SEC-24 修好后可一并截图取证。

### §1.93 修复桌面主循环被同步 HTTP 阻塞（SEC-24 真因）；TTF 字形缓存扩到整屏

**现象**：桌面起来后，时钟与侧栏 Uptime 停止前进，画面冻结；在主循环里新建的窗口
（任务管理器）永远等不到一次重绘。此前记为"主循环在 `hlt` 后停止推进"，并把
`sti; hlt` 当作加固写入代码 —— **那个判断是错的**。

**定位**：用 `-s` 起 QEMU + gdbstub 连续四次采样 RIP，四次都停在同一处 →
`http64_get+421`。`hlt` 与 EOI 都是无辜的，主循环是被**同步 HTTP 请求**堵住了。

**成因一（决定性的）**：`net64_http.c` 的连接建立等待写成了

```c
for (int i = 0; i < 5000; i++) {
    net64_poll();
    if (tcp64_get_state(sock) == TCP_ESTABLISHED) break;
    for (volatile int dw = 0; dw < 1000000; dw++);
}
```

在 QEMU 的软件 CPU 上，单轮 100 万次空转就要数十毫秒；外网不可达时 5000 轮跑满，
主循环被堵住数分钟。日志里能看到的证据是反复出现
`[HTTP64] Connecting to 51.81.167.44:80...` 后长时间没有 `Sending request`。

修法：改为 RTC 秒级 3 秒墙钟上限 + 短空转，连不上最多损失 3 秒。

**成因二（放大器）**：`gfx64.c` 里 `TTF_CACHE_ITEMS` 只有 24 个槽，而一屏存在
上百个（字符, 字号）组合 —— 缓存几乎全程未命中，每次重绘都在重新光栅化。
`gfx64_put_string` 又在整串绘制期间 `cli`，于是每串文本都把时钟中断挡在外面。
修法：槽位扩到 256，单槽 128×128 收到 64×64，总占用 1 MB（与原 24×128×128 ≈ 384 KB
同量级）。超宽字形仍由 `ttf_canary` 兜底不缓存。

**附带**：天气重试 6 次×60 帧 → 3 次×300 帧，启动期累计阻塞控制在 10 秒内。

**验证**：
- 到桌面耗时 46–55 s → **13–14 s**（渲染提速的直接证据）；
- 主循环不再冻死：Uptime 15 s → 2 m 39 s 连续推进，时钟逐分前进；
- 通知、网络状态、Quick Controls 均正常渲染；
- 内核 829,064 B；连续启动回归 PASS、`EXCEPTION=0`。

**遗留**：`hpdesk_fetch_weather` 仍是同步调用，只是被超时兜住了。彻底做法是把
HTTP 改成后台状态机 / 独立线程，让主循环永不阻塞 —— 记入 OPEN_BUGS 后续项。
QEMU 用户态网络访问外网 HTTP 常被丢弃，因此天气卡在模拟环境里多为 `N/A`，
这不是解析逻辑的问题。

