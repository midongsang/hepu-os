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

### §1.94 任务管理器底部提示条与服务列表重叠

**现象**：`KERNEL SERVICES (drivers loaded)` 展开的服务行最后一条会与底部的
`Press K then PID + Enter to end a process` 叠在同一行（截图里 "IDE ACTIVE"
直接压在提示文字上）。

**根因**：服务列表的绘制上限写的是未缩放的裸像素
`cy < w->y + w->height - 22`，而提示条是按缩放函数定位的
`cy = w->y + w->height - MC(16)`。窗口放大后 `MC(16)` 远大于 22，两者就撞在一行。

**修法**：上限改为 `w->y + w->height - MC(16) - MC(14)`，按缩放后的行高给提示条
让出一整行。

**验证**：实机截图 `docs/assets/taskmgr-0.58.0.png` 中 `KERNEL SERVICES` 与
`Press K then PID...` 已分成两行；同时该图确认上一轮修的
`thread_exit` 文案已不含 `-> tid 0`，显示为 `thread_exit: tid 1000  (handed off)`。
内核 829,128 B；2/2 PASS、`EXCEPTION=0`。

### §1.95 syscall 返回状态按线程记账，消除全局单值共享

**问题**。`entry_64.S` 在 syscall 入口把 `syscall` 指令压进 rcx/r11 的返回地址与
rflags、以及用户栈指针，分别落到三个全局变量：

```asm
movq %rax, g_syscall_ret_rip      # rcx
movq %rax, g_syscall_ret_rflags   # r11
```

`g_syscall_ustack_slot` 同理。单核单线程时这就是"本条 syscall 的私有状态"，
没有问题。但一条 syscall 执行途中被时钟中断切走、另一条线程再进 syscall 时，
这三个全局会被覆盖 —— 原线程 `sysretq` 返回时拿到的是**别人**的 RIP / RFLAGS / RSP。
实测里这类踩踏表现为随机 #PF 或跳进别的线程的代码段。

**修法**。沿用 `kstack_top` 已有的 per-thread 模式（`g_syscall_kstack_slot` 早就
这么做了）：给 `uthread_t` 加三个字段 `sret_rip / sret_rflags / sustack`，

- `ut_save()`（时钟中断里调用）把当前三个全局值记进线程；
- `ut_restore()`（切到该线程时）把这组值写回全局；
- `uthread_create()` 用 clone 的入口点 `rip` 与 `rsp` 初始化。

汇编接口完全不动 —— 入口照样写全局，只是"全局单值"变成了
"切到哪条线程，就装哪条线程的值"。

**验证**。这属于正确性改造，6 次连续启动 clone_test 全部
`PASS: child ran in shared address space, both counters advanced`，
`FAIL=0`、`EXCEPTION=0`，说明新记账没有引入回归。原有并发冲突的复现用例
（syscall 中途被抢占 + 另一线程重入 syscall）目前还没有构造，
因此这次是**有依据的防御性修复，不是对某个已复现故障的回归验证**。
内核 829,192 B；`check_docs` warns=0 errors=0。

### §1.96 修 ps / clone_test 的 ELF 入口（SEC-22 真实根因）

**纠错**。SEC-22 原先记作"`crt0.S` 的 32 位 `int $0x80` 退出路径"。核查后发现
`user/libhepu/crt0.S` **没有被任何 Makefile 链接** —— 每个程序都自己定义
`_start`，crt0 是孤儿文件。真正的缺陷在别处。

**真实根因**。`ps.c` 与 `clone_test.c` 只写了 `main`、没有 `_start`，而
`user.ld` 用的是 `ENTRY(_start)`。链接器找不到符号时的行为是：

```
ld: warning: cannot find entry symbol _start; defaulting to 0000000040000000
```

入口被退化成段起始地址。对 `ps.elf` 来说 `0x400000` 是辅助函数 `put_int`
（`main` 在 `0x400000e0`）—— 程序一启动就跑进错误函数，参数寄存器里是内核
留下的垃圾值，`ret` 再弹到栈上的随机地址，正是"收尾阶段 rip 落到 0 附近"
那类 #PF 的形态之一。`clone_test` 的 `main` 恰好落在 `0x400000` 所以一直
能跑，但那只是函数布局的巧合，任何一次重排都会翻车。

**修法**。
- `ps.c` / `clone_test.c` 各补一层 `void _start(void) { exit(main()); }`；
- `crt0.S` 改成正确的 64 位版本（`andq $-16,%rsp` → `call main` →
  `movq %rax,%rdi` → `movl $5,%eax` → `syscall`），它虽未被链接，
  但作为"标准入口模板"不该留着 32 位写法。

**验证**。
- 7/7 用户 ELF 的 `e_entry` 与 `_start` 地址逐一对齐，`cannot find entry symbol`
  警告从构建输出消失；
- 临时把 `ps.elf` 放进 autorun 实跑：`entry=0x00000000400000E0`，
  打印 `PID PPID STATE PRI PAGES NAME` 与四行进程数据后正常返回，
  **没有 #PF** —— 原来 `kernel_main_64.c` 里"跨用户栈第二页必崩"的判断
  据此更新为"未再复现，NAME 列乱码待查"；
- 3/3 连续启动：`PASS`、`FAIL=0`、`EXCEPTION=0`、`[REG] autorun done` 齐全。
- 内核 829,192 B；`check_docs` warns=0 errors=0。

**后续**。NAME 列乱码的问题在下一节（§1.97）定位并修好了。

### §1.97 ps 进程表读数错位与输出交错；ps 正式进 autorun

**症状**。`ps.elf` 输出 `0 0 RUN 0 0`（NAME 空）、`1 0 READY 0 0 ��`（NAME 乱码、
PAGES 恒 0），还多出一行。

**根因一（决定性的）**。`sys_ps()` 里自己写了一份 `typedef process_t`：

```c
typedef struct { int pid; int state; int priority; int cpu_id; uint64_t runtime;
                 char name[32]; int env_type; int parent_pid; uint64_t mem_pages; } process_t;
```

这和 `process64_full.c` 里的真实 `process_t` **字段顺序完全不同** —— 真实结构里
`name[32]` 排在 `process_context_t context`、`stack`、`page_table` 之后，偏移远
不止 24。于是 NAME 读到的是别的字段的字节（乱码），`mem_pages` 也读错位置。

修法：在 `process64_full.c` 里提供取值接口

```c
int process64_get_ps_fields(int idx, int *pid, int *ppid, int *state,
                            int *prio, uint64_t *mem_pages, char name[32]);
```

`sys_ps()` 改为调用它，**不再复制结构定义** —— 调用方不该猜内核结构的内存布局。

**根因二**。`buf[n].mem_pages = pt[i].runtime;` —— 把运行时间当内存页数。改回 `mem_pages`。

**根因三**。ps 的 `put_ch` 是 `write(1, &c, 1)`，一行要发几十次 syscall；中途只要
内核往串口插一行日志，字符就互相穿插。改为行缓冲，整行一次 `write`。

**附带**。`process64_create` 复制 name 前先 `memset(0, 32)`，避免槽位复用时残留
上一条进程的名字尾巴。

**关于栈 vs 数据段**。排查中途一度怀疑 `struct hep_ps_entry e[8]`（448 字节）放栈上
会跨到用户栈第二页、导致 `copy_to_user` 失败，于是改成 `static`。**改完输出没变**，
说明那不是根因（真正根因是结构错位）。`static` 保留，但它不是必需条件 —— 这里如实
记录，避免把错误结论写进文档。

**验证**。实机输出（三次启动一致）：

```
PID PPID STATE PRI PAGES NAME
0 0 RUN   0 0 idle
1 0 READY 0 4 kbeat
2 0 READY 2 4 khouse
3 0 READY 3 4 kstat
```

- 3/3 启动：`PASS`、`FAIL=0`、`EXCEPTION=0`、`[REG] autorun done` 齐全；
- `ps.elf` 已正式放进 autorun（`kernel_main_64.c` 的列表），`entry=0x400000F0`；
- 内核 829,576 B；`check_docs` warns=0 errors=0。

### §1.98 clone 偶发 `child never ran` —— APIC timer 校准偶发失败

**现象**。SEC-23 上次修好后一直是 20/20，本轮从 3+10 次回归开始出现
约 1/10 的 `FAIL: child never ran`，且失败那次串口里**完全没有**
`[UT] exit ticks2=...`（说明 `uthread_timer_tick` 从未走到多线程分支）。

**定位过程（用诊断计数器一层层排除）**。加了 6 个计数后，失败样本给出：

| 指标 | 成功样本 | 失败样本 |
|---|---|---|
| `uthread_timer_tick` 进入次数 `entry` | 0x1A 起稳步增长 | **0x0** |
| APIC timer 中断总数 `k64` | ~9,000 | **~45,000** |
| 其中 CPL=3 的次数 `apic_cpl3` | 0xDC | **0x0** |
| 帧 CS 分布 `cs[k,u,o]` | 有 `u` | **u=0x0** |
| 启动日志 | 无告警 | **`[APIC] WARN: timer calibration failed`** |

结论：时钟中断一直在来（4.5 万次），但**全部落在内核态，用户态一次都没有**，
于是用户线程轮转彻底失效，父线程自旋到超时 → `child never ran`。
而 `[CLONE] ret_rflags=0x246`（IF=1）证明**不是用户态关中断**。

**根因**。`apic64_calibrate_timer()` 的测量窗口靠 PIT tick 推进，而 PIT 中断
只有在 IF=1 时才投递。这段代码由 `smp_init` 在启动早期调用，调用方可能仍
处于 `cli` 状态 —— 等待循环里的 `pit64_get_ticks()` 永远不动，必然超时失败，
于是回退 `ICR=6250`。那个值对应 100MHz 总线假设，QEMU 实测约 400MHz，
tick 频率漂移数倍（4.5 万 vs 9 千正是这个比例），用户态调度 tick 随之失效。

**修法**（两处，都只动 `apic64.c`）：

1. `apic64_calibrate_timer` 拆成 wrapper + `_inner`：wrapper 先 `pushfq` 记下
   调用方的 IF，`sti`，跑完窗口后按原状态恢复 `cli`。等待期间中断保持使能，
   PIT tick 能推进。
2. `apic64_init_timer` 里单次校准失败改为**最多重试 3 次**。校准窗口本身是
   时序敏感的（PIT 投递在启动早期并不稳定），单次失败就退回 6250 代价太大。

**验证**。修复前 10 次回归 9/10（失败样本 `calfail=2`、`entry=0`、
`cs[k,u]=[0xF976,0]`）；修复后连续 **15 次启动 15/15 PASS**，
`timer calibration failed` 出现 0 次，`FAIL=0`、`EXCEPTION=0` 全清。
内核体积仍为 829,576 B。

**证据等级说明**。因果链是"校准失败的那次启动 ⇔ clone 失败"（失败样本 100%
带 `calfail`，成功样本 0 次），修复后两者同时消失。但"校准失败为何具体表现为
用户态拿不到 APIC timer 中断"这一环没有用 gdb 逐指令证死 —— 这里如实标注，
避免把推断写成定论。

#### 后续复核（V0.58.4）—— 这条结论要修正

§1.98 报的"连续 15 次 15/15"**样本量不够，不能作为闭环证据**。之后又跑了两批：

| 批次 | 结果 | 失败样本 |
|---|---|---|
| 10 次（加调度器锁后） | 9/10 | run2 `desk=53s P=0 F=1 calfail=1` |
| 20 次（回退 ICR 改 35000 后） | 19/20 | run4 `desk=57s P=0 F=1 calfail=2` |

两次失败都恰好是带 `[APIC] WARN: timer calibration failed` 的那次启动 ——
**因果方向是对的**（校准失败 ⇔ clone 失败），但**修复没有闭环**。

进一步排除了两个假设：

1. **IF 假设不完整**。wrapper 里已经 `pushfq` 记 IF、`sti` 跑窗口、完成后按原
   状态恢复，失败仍会发生 —— 说明调用方未必处在 `cli`，或者 PIT 中断不来
   另有原因。
2. **频率假设不成立**。把回退 ICR 从 6250（≈5.6kHz）改成 35000（≈1kHz，
   与校准成功的实测值同一量级），失败样本行为**完全不变**。既然"快 5 倍的
   tick"和"正常 tick"两种回退档位都失败，那就不是频率问题 —— 而是**中断根本
   没投递到用户态**（诊断数据 `apic_cpl3=0`、`cs[k,u]=[k,0]` 正是这个含义）。

**当前定位**。失败启动里 PIT 与 APIC timer 两条路径在 CPL=3 的投递都是零，
说明问题在中断控制器层的启动期配置，而不是某个 tick 的频率。已登记
`OPEN_BUGS.md` **OBS-01**。

#### 第三轮复核（V0.58.4）—— 真正的元凶是校准失败后的自旋开销

上面那句"中断根本没投递到用户态"**只对了一半**，得拆开看。用 QEMU monitor
和 gdb 做了可控实验才分清两件本来搅在一起的事。

**实验方法**。在校准调用之前（`apic64_switch_to_apic_mode()` 之后、
`smp_init()` 之前）用 `apic64_disable_irq(acpi_irq_to_gsi(0))` 主动屏蔽
IRQ0，让 PIT 一定不投递 —— 这就等价于 OBS-01 那种"启动起 PIT 就不来"的
启动路径，而且不需要靠 2% 的概率去撞。

**先确认屏蔽真的生效**（否则实验无效）。`info irq` 统计的是中断线活动，
屏蔽后 i8259 那一栏还在涨，不能用它判断。改用 gdb 直接读内核变量，两次采样：

| 变量 | 第一次 | 六秒后 | 结论 |
|---|---|---|---|
| `pit_ticks` | `0x1a15` | `0x1a15` | **完全不动 → PIT 确实没投递进内核** |
| `g_kernel_tick64` | `0x31bb` | `0x8dc9` | 在涨 → APIC timer 正常 |

**实验结果（对照）**：

| 版本 | desk 耗时 | clone |
|---|---|---|
| 修复前（`CAL_SPIN_LIMIT=20000000`，失败重试 3 次） | **43~44 s** | PASS |
| 修复后（`CAL_SPIN_LIMIT=2000000`，不重试） | **16~17 s** | PASS |

**两个结论，都推翻了我之前的说法**：

1. **PIT 缺失本身不导致 clone 失败**。实验里 PIT 完全没投递，clone 照样 PASS ——
   因为 `lib64.c` 里 vector 0x40（APIC timer）也调 `uthread_timer_tick`，
   用户态轮转不依赖 PIT。所以"用户态中断为零"在 OBS-01 里是**结果**，不是原因。
2. **真正拖垮启动的是校准失败后的自旋开销**。`CAL_SPIN_LIMIT` 原值 20000000，
   QEMU TCG 下 `pause` 约 110ns/次，单个循环约 2 秒；校准失败时
   "3 次重试 × BSP/AP 两核 × 2 个等待循环 ≈ 27 秒"，正好对上 43~57 秒的慢启动。
   而且我上一版加的"重试 3 次"把这段开销**放大了 3 倍**，帮了倒忙。

**修法**（都在 `apic64.c`）：`CAL_SPIN_LIMIT` 20000000 → 2000000；去掉重试
（PIT 不投递时重试必然再失败，纯属重复自旋）。

**验证**。25 次回归 **25/25 PASS**，其中 **4 次仍然 `calfail=1`**，但这 4 次
`desk` 全部 15~17 s、clone 全部 PASS —— **校准失败照旧会发生，却不再有可观测
后果**。这才是 OBS-01 该有的收口方式：不去赌"修好 PIT 投递"，而是让降级路径
足够便宜。

**为什么 OBS-01 里能看到 clone FAIL，实验里却看不到**。两者不完全等价：
实验是"校准前屏蔽"，`pit_ticks` 之前已正常跑过一段；OBS-01 是启动全程 PIT
都不投递，`pit_ticks` 从 0 开始，`gfx64_frame_throttle()` 的
`now - last < 2` 会一直成立（差值为 0），桌面渲染被节流卡住，连带 clone 的
等待循环耗尽。所以另外给 `pit64_get_ticks()` 加了 APIC tick 兜底
（`return max(pit_ticks, g_kernel_tick64)`，两者都是 1 ms 粒度），PIT 冻住时
时间照样前进。这一条是防守性的，正常路径下两个源都 >0 且同速，取大无偏差。

### §1.99 SMP 阶段 A：调度器加锁

**为什么要先加锁再开 AP 调度**。`process64_schedule()` 原实现全程无锁，而它
会被两条路径同时调用：`smp_timer_tick()`（时钟中断上下文，AP 核上尤其）和
普通上下文（`process64_yield()` 等）。SMP 下两个核并发执行它会同时改
`ready_queue[]`、`ready_queue_count[]`、`current_pid`、`per_cpu_current_idx[]`，
选出同一个进程、队列计数错乱都是必然。

**两个不能踩的点**：

1. **`spinlock_lock()` 本身不关中断**。持锁时若被本核中断打断、而中断处理里
   又要同一把锁 → 自死锁。所以加锁前必须 `cli`，并把调用方原本的 IF 记下来，
   解锁后**按原值恢复**而不是无脑 `sti`（IRQ 上下文里本来 IF=0，提前开中断
   会在 EOI 之前放嵌套中断进来）。
2. **绝不能持锁跨越 `context_switch_64()`**。切完是另一个线程在跑，锁会永远
   留在上一个线程手里，下一个申请者直接卡死。所以两处切换点都改成
   "取好 pid → 释放锁 → 再切"。

**实现**（`process64_full.c`，新增 56 行）：

```c
static spinlock_t g_sched_lock;
static int g_sched_lock_ready = 0;

static uint64_t sched_lock_acquire(void)   /* pushfq -> 记 IF -> cli -> 懒初始化 -> lock */
static void sched_lock_release(uint64_t f) /* unlock -> 仅当 f 的 bit9 置位才 sti */
```

`process64_schedule()` 开头 `uint64_t sched_iflags = sched_lock_acquire();`，
两处 `context_switch_64` 前 `sched_lock_release(sched_iflags)` 并把返回值先存好，
结尾 `return -1` 前也释放。

**阶段 A 的边界**：**没有动 `smp.c:140` 的 `if (cpu_id != bsp_cpu_id && 0)`**，
AP 核仍然不调度。这一步只验证"加锁本身不改变单核行为"，不与"双核并发"这个
变量混在一起。

**回归**（10 次，`-smp 2`，AP 调度仍关）：9/10 通过，其余 9 次
`desk=15~16s P=1 EXC=0 ps=2`；唯一失败是那次的 §1.98/OBS-01 校准问题
（`calfail=1`），不是加锁引入的 —— `[SPINLOCK] ERROR` 出现 0 次，
`unlock` 的 owner 检查一次都没报错。

**还没做的（阶段 B 的前置风险，先记下来）**：

- `current_pid` 是**全局单值**，不是 per-CPU。`process64_get_current()` 在多核
  下语义有偏，两核同时读会互相串。
- 工作窃取分支允许 `cpu_id == 0 || == cur_cpu`，打开 AP 调度后两核可能选中
  同一个 `cpu_id == 0` 的进程。虽然选中后该进程会从 `ready_queue` 移除、
  `state` 置 `RUNNING`，另一核不会再选到它，但 `old` 重新入队后可能被另一核
  抢走并并发切换 —— 需要给进程加 `running_on` 之类的归属标记。
- `context_switch_64` 是"保存当前到 old、从 new 恢复"，跨核切换同一进程会
  把栈指针写串，这条比前面两条更硬。


### §1.100 SMP 阶段 A2：`current_pid` 改 per-CPU

**为什么要改**。`current_pid` 原来是文件内的全局单值，被三个地方消费：
`process64_get_current()` 的 PID 回退分支、`process64_create()` 记录
`parent_pid`、以及 `process64_get_current_pid()`。SMP 下两个核各跑各的进程，
单值会被另一个核覆盖 —— 本核拿到的"当前进程"其实是对方的。这在阶段 B 打开
AP 调度后会立刻变成错误数据（尤其 `parent_pid` 记错，进程树就是乱的）。

**改动**（全部在 `process64_full.c`，单核行为不变）：

- `static int current_pid` → `static volatile int per_cpu_current_pid[16]`；
- 新增 `sched_cur_pid_internal()`，按 `smp_get_current_cpu()` 取值（越界回落 0）；
- 两处调度切换点改为 `per_cpu_current_pid[cur_cpu] = ...`（仍在校内，持锁）；
- `process64_create` 的 `parent_pid` 改用 `sched_cur_pid_internal()`；
- `process64_get_current()` 的回退分支用本 CPU 的 PID 查找；
- init 时清零全部 16 项，进程释放时只清本 CPU 那一项。

**顺手收拾的两个遗留**：

1. `process64_get_current_pid()` 在 `syscall64_full.c` 里被 `extern` 声明过
   **两次，但从未定义**。补上实现，按 CPU 返回。
2. 文件里有个 `scheduler_lock`，只在 init 里 `spinlock_init` 过一次、
   **从来没被 lock/unlock** —— 死变量，而且容易被误认为"调度路径已经有锁了"。
   删掉；真正生效的是 §1.99 那个 `g_sched_lock`。

**验证**（20 次，`-smp 2`，AP 调度仍关）：**20/20 PASS**，`desk` 稳定
15~16 s，`P=1`、`F=0`、`EXC=0`、`ps` 正常、`calfail=0`。内核
829,832 → **829,896 B**（+64 B）。

**阶段 B 还剩的前置**：进程归属标记（`running_on`），用来防止两核选中同一个
进程后跨核 `context_switch_64` 把栈指针写串。这一条不解决，`smp.c:140` 的
`&& 0` 不能动。

### §1.101 SMP 阶段 A3：进程归属标记 `running_on`

**它防的是什么**。`context_switch_64()` 的语义是"把当前上下文存进 old、从 new
恢复"。如果两个核同时对**同一个进程**做这件事，那个进程的 `rsp` 会被两个核
轮流写，而且**不报异常、不打日志，只是静默把栈写坏** —— 修起来是噩梦。单核
下不存在这个问题，一开 AP 调度就有。

**改动**（`process64_full.c`）：

- `process_t` 尾部加 `volatile int running_on;`（-1 = 未在任何核上跑，
  否则为执行它的 CPU id）。加在尾部不影响 `_Static_assert` 对 `context`
  偏移的对齐检查。
- 初始化：idle 记成**真实的 `smp_get_current_cpu()`**（不硬编码 0 —— 虽然
  QEMU 下 BSP 就是 0，但没理由指望这一点）；其余槽位置 -1。
- `process64_create`：新进程置 -1（还没在任何核上跑过）。
- **选进程时过滤**：候选收集循环里 `if (ro >= 0 && ro != cur_cpu) continue;`。
- **工作窃取分支也过滤**：原来直接取 `ready_queue[prio][0]`，现在先扫一个
  `running_on < 0 || == cur_cpu` 的候选，找不到就 `continue` 到下一个优先级。
  移除时用命中的下标，不再假定首项。
- **切换时交接**：两处切换点都在 `context_switch_64` 之前做
  `old->running_on = -1;` 与 `processes[idx].running_on = cur_cpu;`。

**边界**：**`smp.c:140` 的 `if (cpu_id != bsp_cpu_id && 0)` 依然没动**。这
一轮只是让数据结构和过滤规则就位，单核下这些条件恒不触发。

**验证**（20 次，`-smp 2`，AP 调度仍关）：**20/20 PASS**，`desk` 13~15 s，
`P=1`、`F=0`、`EXC=0`、`ps` 正常、`[SPINLOCK] ERROR` 零次；
其中 2 次仍 `calfail=1` 但同样 PASS。内核 829,896 → **830,024 B**。

#### 阶段 B 剩下的真正阻塞：idle 被所有核共享

`running_on` 不能单独解决问题。初始化的这两行把所有 16 个 CPU 的"当前进程"
都指向了 `processes[0]`（idle）：

```c
per_cpu_current_idx[i] = 0;      /* 全部指向 idle */
per_cpu_current_pid[i] = 0;
```

于是 AP 核一调 `process64_schedule()`，`old = process64_get_current()` 拿到的
就是 idle，它会把 `idle->state = READY`、入队、并**把 idle 的栈存进
`processes[0].context`** —— 而 BSP 此刻也正在跑 idle，两边对同一份
`context.rsp` 互相覆盖。`running_on` 挡不住这个，因为问题不在"选谁"，而在
"AP 核误以为自己已经在跑 idle"。

**要开 `&& 0` 之前必须先做**（顺序不能反）：

1. AP 核的当前进程初始化为 **-1（无当前）**，而不是指向 idle；
2. `process64_schedule()` 支持 `old == NULL` 的首次切入 —— 即用一个 per-CPU
   的 dummy 上下文作为 `old_ctx` 传给 `context_switch_64`，让"本来没在跑任何
   东西"的核也能切进第一个进程；
3. 每核有自己的 idle 循环（或在无进程可跑时进 `hlt` 而不占用 idle 的 context）。

这三条都要在**打开开关之前**完成并单独验证，不能和"双核并发"混在一起试。

### §1.102 SMP 阶段 1 起步：原子原语统一 + 计数原子化 + PMM 加锁

按 `SMP_PROGRESS_0_58_4.md` 的建议顺序，从阶段的第 1 步开始做：先立原子原语，
再把两处最基础的计数器原子化，最后给 PMM 上锁（**这是开 AP 调度的硬门槛**）。

#### SB-01 新增 `atomic64.h`

此前内核里的原子操作有三套来源，屏障口径还不一致：`spinlock.c` 里两个文件
私有的 `static inline`、`kheap64.c` 的 `__atomic_exchange_n`、`klog64.c` 的
`__sync_lock_test_and_set`。多核下这一点很容易变成"看起来锁住了其实没同步"。

新头文件统一提供：

- 屏障：`mb()/rmb()/wmb()`（`mfence/lfence/sfence`）、`barrier_compiler()`；
- 32 位：`atomic_load32/store32/xchg32/cmpxchg32/inc32/dec32/add32/xadd32`；
- 64 位：同上一整套；
- 类型别名 `atomic_u32/atomic_u64`（只是 `volatile` 别名，所以现有
  `volatile uint64_t` 变量不用改类型就能直接传）。

设计约定：所有读-改-写都用带 `lock` 前缀的指令（本身就是完整屏障，不需要
额外 fence）；load/store 用普通 mov，x86 对 8 字节对齐访问本身是原子的，
跨核可见处配合 `mb()`。

`spinlock.c` 的本地 `static inline` 已删除、改调新接口，行为等价。

#### SB-02 / SB-03 两个基础计数器原子化

- `g_kernel_tick64`（`lib64.c:349`）：多核的 `0x40` 中断会并发写，
  `++` 改 `atomic_inc64`；
- `irq_count[]`（`irq64_simple.c:32`）：类型加 `volatile`，三处递增
  （含 spurious 的 `0xFF` 槽）改 `atomic_inc64`。

两者都是纯表达式替换，不改语义。

#### SB-09 PMM 加锁（新增 `g_pmm_lock`）

`pmm64_alloc_page / alloc_pages / free_page / free_pages / reserve_range` 此前
**完全没有互斥**。单核没事，两核同时扫位图就会把同一个空闲页帧判给两个进程，
两边各自写入互相覆盖 —— **不报错，只随机踩内存**，事后从栈回溯几乎定位不到。
计划 1.2 节把它列为最严重的一条。

实现要点：

- 用新加的 **`spinlock_lock_irqsave / spinlock_unlock_irqrestore`**（`spinlock.h`）
  —— PMM 会在中断上下文被调用，必须先 `cli` 再拿锁，`pushfq` 记原 IF，
  解锁时只在原 IF 为 1 时才 `sti`；
- **外层入口加锁、内部用 `*_nolock` 版本**：`alloc_pages` 原本会调
  `alloc_page`、`free_pages` 原本会调 `free_page`。`spinlock_lock` 虽然支持
  递归，但递归解锁只做 `recursion_count--` 而不真正释放锁 —— 此时
  `irqrestore` 若照 flags 开中断，就会出现"锁还拿着但中断已开"。所以拆成
  无锁内核函数，避免嵌套。`irqsave` 这一对也在注释里明确写了**不支持嵌套**。

#### 验证

QEMU `-smp 2` 20 次：**20/20 PASS**，`desk` 13~15 s，`P=1`、`F=0`、`EXC=0`、
`ps` 正常、`[SPINLOCK] ERROR` 零次；5 次仍 `calfail=1`（OBS-01 偶发，行为不变）
但同样 PASS。内核 830,024 → **830,344 B**。

**边界**：`smp.c:140` 的 `&& 0` 依然没动。PMM 锁在单核下恒无竞争，它的价值
要等 AP 真正参与调度时才体现 —— 但必须**在那之前**就就位。

#### 30 轮稳定性观察（SB-09 之后）

按要求在 SB-17 之前先单独观察这一段（阶段 1 原子化 + 阶段 2 的 PMM 锁），
QEMU `-smp 2` 跑 30 轮：

| 指标 | 结果 |
|---|---|
| PASS | **30 / 30** |
| `desk` 到桌面 | 13~15 s，**无一次超 25 s** |
| `PASS: child ran` | 每轮 ≥ 1 |
| `FAIL` / `EXCEPTION` | 0 / 0 |
| `kbeat` | 每轮 2 |
| `[SPINLOCK] ERROR` | 0 |
| `timer calibration failed` | 4 / 30（13.3%） |

`calfail` 的 13.3% 与 OBS-01 记录的"约 16%"吻合，且**每轮照样 `PASS`、`desk`
照样 13~15 s** —— 说明 §1.98 定案的"让降级路径足够便宜"依然成立，PMM 加锁
没有把这条降级路径拖慢（锁只包住位图扫描这一小段，且单核下无竞争）。

结论：这一段可以往下走了。

### §1.103 SB-18：TSS 数组化（每核一份 TSS / RSP0）

计划里 SB-18 的写法是"把 TSS 数组化、每核一份 RSP0"。实施过程中**第一版
方案走了弯路**，这里把弯路一起记下来，因为它顺手证明了一件更重要的事。

#### 弯掉的路径（已全部回退）

第一版改的是 `gdt64.c` 里的 `gdt64[]`：`GDT_ENTRIES` 从 5 改成 `5 + MAX_CPUS*2`，
新增 `tss_table[MAX_CPUS]`、`gdt64_tss_selector_for_cpu()`、
`gdt64_kstack_top_for_cpu()`，`ap_c_entry` 里改成 `gdt64_load_asm(gdt64_get_ptr_addr())`
再 `ltr` 本核选择子。

结果：**64 位内核启动即连续复位 7 次，串口连 `[TSS64]` 都没有。**
用 gdb 直读内核变量 + 串口标记二分，查出两个事实：

1. **`gdt64_init()` 从未被任何代码调用**。`kernel_main_64.c:33` 只有一行
   `extern void gdt64_init(void);` 声明，全仓库没有调用点 —— 整张
   `gdt64[]` / `gdt64_ptr` / `tss_table` 都是**死代码**。串口在 AP 上读到
   `gdt64_ptr` 的原始内容，8 字节全是 `0`，`sgdt` 读回 `base=0 limit=0`，
   一个反证就够了。
2. **BSP/AP 真实在用的 GDT 是 `entry_64.S` 里的 `kernel_gdt64`（7 项）**，
   真实填 TSS 并 `ltr 0x28` 的是 **`tss64_init()`**（`kernel_main_64.c:390`
   调用它）。改死代码自然一点效果都没有。

#### 正确路径（最终形态）

- `entry_64.S`：`kernel_gdt64` 在原 7 项后加 `.fill 30, 8, 0`，共 **37 项**
  （5 个基础段 + 16 核 × 2 槽）；`kernel_gdt64_ptr.limit` 用的是
  `kernel_gdt64_ptr - kernel_gdt64 - 1`，自动跟着变，不用手改。
  `kernel_tss64` 从 `.skip 104`（一份）改成 `.skip 1664`（104 × 16），
  末尾 `.align 16` 并加 `kernel_tss64_end` 标签。
- `tss64_init()` 重写成多核循环：每核一份 104 字节 TSS，**核 0 的 `rsp0`
  沿用已验证的 `kernel_stack_top`（64 KB），核 1..15 用各自的
  `per_cpu_kstack[cpu]` 顶（16 KB × 16 = 256 KB，全在 `.bss`，不进镜像）**；
  写完 TSS 再把描述符写进 `kernel_gdt64[5+cpu*2]` 与 `[6+cpu*2]`
  （`access=0x89`、`limit=103`）。核 i 的选择子 = `0x28 + i*16`。
- `ap_c_entry`：仍按原路径 `gdt64_load_asm(kernel_gdt64_ptr)`，
  然后 `ltr` **本核选择子**，并打一行串口
  `[SB18] cpu=... tss_sel=... rsp0=... rsp_now=...`。

#### 第二个坑：调用顺序

改完仍然连续复位。用 `-d int,cpu_reset` 抓异常，**只有一条内核异常就收尾**：

```
v=0d  e=0038  cpl=0  IP=0008:000000000014552c
v=08  e=0000  cpl=0  IP=0008:000000000014552c
```

`error code = 0x0038` 就是选择子 0x38；`objdump` 定位 `0x14552c` 正好是
`ap_c_entry` 里的 `ltr %ax`。

根因：**`tss64_init()` 原本排在 `smp_init()` 之后**。AP 在这个时间点已经
跑起来了，它执行 `ltr 0x38` 时，GDT 里第 7 项还是全 0 的空描述符 → #GP
→ 双重故障 → 复位。修复是把 `tss64_init()` 移到 `smp_init()` **之前**（先把
每核 TSS 与描述符填好，再放 AP 出去）。

#### 验证

`-smp 2` 跑 20 轮：

| 指标 | 结果 |
|---|---|
| PASS | **20 / 20** |
| 启动次数 | 每轮 **1**（无一次复位） |
| `desk` 到桌面 | 13~16 s |
| `[SB18]` 串口行 | 每轮 1 条 |
| `PASS: child ran` | 每轮 ≥ 1 |
| `FAIL` / `EXCEPTION` | 0 / 0 |
| `[SPINLOCK] ERROR` | 0 |
| `timer calibration failed` | 3 / 20（15%，与基线一致） |

串口实测那一行（每轮完全相同）：

```
[SB18] cpu=0x0000000000000001 tss_sel=0x0000000000000038
       rsp0=0x00000000002021e0 rsp_now=0x000000000247e2a0
```

`tss_sel=0x38` = 核 1 自己的描述符；`rsp0=0x2021e0` 落在
`per_cpu_kstack[1]` 那块 16 KB 的范围里，**与 BSP 的 `kernel_stack_top`
不同** —— 这就是 SB-18 想要的"AP 用户态中断切到自己的栈"。

#### 与计划的两处偏差（如实记录）

1. **IST1/IST2/IST3 仍全核共用**（`irq_stack_df_top` / `irq_stack_nmi_top` /
   `irq_stack_mc_top`）。这三次（双重故障 / NMI / 机器检查）发生时当前栈已
   不可信，继续共用同一组应急栈在多核下同样会撞 —— **本轮不改，单独记为后续项**，
   理由是先让主路径可验证、把变更面控制在最小。
2. 删掉了 `gdt64.c` 里已确认从未被调用的配套：`tss_table[]`、`gdt_set_tss()`、
   `gdt64_get_ptr_addr()`。`gdt64_init()` 本身仍保留（死代码，但暂不动，
   避免一次改太多），只在其中加注释写明"每核 TSS 由 `tss64_init()` 直接写
   `kernel_gdt64`/`kernel_tss64`"。`gdt64_set_kernel_stack()` 与
   `gdt64_get_tss_addr()` 保留并改指向真表（当前无调用点）。

内核 830,344 → **831,112 B**。

#### 顺带纠正一份旧文档

`PLAN_SMP_HARDENING_R5.md` 的 SB-18 条目假设"TSS 在 `gdt64.c` 里"。实测是
反的：`gdt64.c` 那套是死代码，真表在汇编、真初始化在 `tss64_init()`。
改 SB-18 之前必须先确认这一点，否则整轮改动都落在不会执行的分支上。

---

## 1.104 SB-17 完整版：per-CPU syscall 栈槽（0.58.6）

### 目标

把 syscall 入口/出口用的两个全局单值（`g_syscall_kstack_slot` /
`g_syscall_ustack_slot`）换成每核一份，为此把 GS 段当 per-CPU 基址用：
`MSR_KERNEL_GS_BASE` 指向本核的 `percpu_data[cpu]`，入口汇编走 `%gs:0x00` /
`%gs:0x08` 直接取栈。

### 落地的改动

| 文件 | 内容 |
|---|---|
| `entry_64.S` | syscall 入口 `movq %rsp,%gs:0x08` / `movq %gs:0x00,%rsp`；出口 `movq %gs:0x08,%rsp` |
| `syscall64_simple.c` | `percpu_data[16]`；`PERCPU_SIZE 64` 与编译期 `percpu_size_check`；`syscall_current_cpu()` / `syscall64_pcpu()` / `syscall64_kstack_slot_ptr()` / `syscall64_ustack_slot_ptr()`；`syscall64_init_for_cpu(cpu,kstack_top)` 写本核 MSR |
| `uthread64.c` | `ut_save` / `ut_restore` / `uthread_apply_redirect` 三处改走 `syscall64_*_slot_ptr()` |
| `syscall64_full.c` | fork 的两处读改走 `syscall64_ustack_slot_ptr()` |
| `smp.c` | `ap_c_entry` 在 SB-18 块后调 `syscall64_init_for_cpu(cpu_id, gdt64_kstack_top_for_cpu(cpu_id))`，打 `[SB17]` 串口行 |

实测核 0 `msr_gsbase=0x23CA00`、核 1 `gs_percpu=0x23CA60`，布局正确。

### 踩到的坑：切换用户态时 GS 段协议不一致 → #DF

改完第一轮验证，`exec /bin/sec_test.elf` 切到用户态后立刻：

```
[EXCEPTION] vector=0x8 error=0x0 rip=0x1001C1
```

`0x1001C1` 正是 `syscall_entry_64` 里 `swapgs` → 存用户栈 → 取内核栈之后的
第一条 `push %rax`。

**取证过程**（全部用开源工具，没靠猜）：

1. `objdump -d` 定位到 `push %rax`（`0x1001C1`）。
2. QEMU `-d int` 抓第一次异常：`v=0e e=0002 ... IP=0010:...? SP=0010:0000000000000000 CR2=fffffffffffffff8`
   —— **`SP=0`**，`CR2=-8`，即 `push` 踩在 `rsp-8`。也就是说 `%gs:0x00` 读出来是 **0**。
3. QEMU monitor `xp /1gx 0x23ca00` 直接读物理内存：崩溃后该槽是**正常值**
   `0x1E56D0`，说明问题不在初始化写的值，而在**那一刻 GS 基址不对**。
4. 在 `syscall_handler_64` 入口临时读 `MSR_GS_BASE(0xC0000101)` /
   `MSR_KERNEL_GS_BASE(0xC0000102)` / `%gs:0` 打到串口：

```
[DIAG] #1 gsbase=0          kgsbase=0x23CA20 gs0=0
[DIAG] #2 gsbase=0x23CA20   kgsbase=0         gs0=0x1E56D0
[DIAG] #3 gsbase=0x23CA20   kgsbase=0         gs0=0x1E56D0
```

**第一次 syscall 时 GS=0，第二次起就正常了。** 两次都执行了同一个 `swapgs`，
差别只能是**进入 syscall 之前 GS 的初始状态不同**。

根因：`user_process_switch()`（`user_process.c`）里的 `user_enter()` 只做
`iretq`。它经常是在 syscall handler 上下文里被调用的（exec / 切进程），
那时 `swapgs` 已经把 GS 换成了 percpu —— **直接 iretq 就把 percpu 带进了用户态**。
于是用户第一次 syscall 的 `swapgs` 又把它换回 0，`%gs:0x00` 读出 0，
第一条 `push` 踩到 `rsp-8 = -8` → #PF → 抬不起 #PF 帧 → #DF。

关键点：**`%gs:0x08`（地址 8）和 `%gs:0x00`（地址 0）都落在低 1MB 已映射区**，
所以两条 GS 访问本身都不报 #PF，只有随后的 `push` 才暴露 —— 这也是为什么
异常 RIP 落在 `push` 而不是 GS 访问指令上，容易看错方向。

### 修法

在 `user_process_switch()` 调 `user_enter()` 之前，按当前 GS 状态补一次：

```c
uint64_t gsb = read_msr(0xC0000101);       /* MSR_GS_BASE */
if (gsb != 0) __asm__ volatile("swapgs");   /* 还在 syscall 上下文, 还原成 0 */
```

GS 协议因此固定为：**内核普通态 GS_BASE=0；syscall 入口 swapgs 后 GS_BASE=percpu；
返回用户态前必须让 GS_BASE 回到 0**。`user_enter` 是唯一"从 syscall 上下文
直出用户态"的路径，所以只需要在这里补。

### 验证

5/5 轮全 PASS：

```
run1 desk=1 EXC=0 PASS=1 FAIL=0 timercal=0
run2 desk=1 EXC=0 PASS=1 FAIL=0 timercal=0
run3 desk=1 EXC=0 PASS=1 FAIL=0 timercal=0
run4 desk=1 EXC=0 PASS=1 FAIL=0 timercal=0
run5 desk=1 EXC=0 PASS=1 FAIL=0 timercal=0
```

桌面截图入库 `docs/assets/smp-sb17-0.58.6.png`（Info 面板 Uptime 0s、
Kernel 64-bit SMP、CPU 2 cores、Memory 976 MB）。

### 附带修掉的镜像构建问题

同一轮里发现"重建镜像后 ext2 根分区挂载失败、落到首启 OOBE"，根因是
`tools/build_uefi_dual.py` 收尾的 `_write_protective_mbr()` **整块重写 512 字节**，
把 `create_ext2_data` 刚写的 hybrid `0x83` 项冲掉了。本机内核只解析 MBR 分区项
（不解析 GPT），根分区就是靠这条 hybrid 项认出来的；冲掉之后内核把 GPT 头
（LBA 2）当超级块，于是 `invalid superblock parameters` → 挂载失败 → OOBE。

修法：`_write_protective_mbr(*, root_lba, root_sectors)` 改为在所有句柄关闭之后
用独立描述符**只写 446..512 这 66 字节**（槽 0 = `0xEE` 保护项，槽 1 = `0x83`
指向 ext2 根，末尾 `0x55AA`），`write_hybrid_mbr` 也在同一收尾步骤里统一写。
重建后：

```
[PARTS] P0x1 type=0xEE lba=0x1 sz=0x7FFFF ext2=0x0
[PARTS] P0x2 type=0x83 lba=0x40800 sz=0x3C000 ext2=0x1
[AUTH] Account loaded: dev (role 2)
```

（读 MBR 时注意：槽 1 的 type 在文件偏移 **466**，462 是 boot flag，
按 462 判断会误以为没写进去。）

---

## 1.105 SB-19：LAPIC 状态 per-CPU 化（0.58.7）

### 动机

SB-17 把 syscall 栈槽做成 per-CPU 之后，暴露出一个更底层的问题：
`smp_get_current_cpu()` 的实现就是 `return apic64_get_id()`，而
`apic64_get_id()` 在 PIC 模式下返回的是**全局单值 `apic_id`**。也就是说

```c
uint32_t apic64_get_id(void)
{
    if (apic_mode) return 直读LAPIC_ID;
    return apic_id;      /* PIC 模式：所有核都返回 BSP 的 0 */
}
```

**PIC 模式下每个核都自称 CPU0**，所有 per-CPU 下标全部塌到 0 号槽。
SB-17 的双核 dump 看着正常（0x23CA00 / 0x23CA60），是因为 AP 那条路径
显式传了 `cpu_id`；但任何走 `smp_get_current_cpu()` 的代码，在 AP 上都算错。

### 落地

`apic64.c`：

- `apic_id` / `apic_version` / `apic_initialized` 三个全局单值删除，
  改为 `apic_pcpu[16]`（`id` / `version` / `initialized` / `tpr` / `svr` / `lvt_timer`）。
- `apic_hw_id()` 一律直读本核 LAPIC ID 寄存器，**PIC 模式下也读**；
  `apic64_get_id()` 直接返回它。
- `apic64_init()` 只写 0 号槽；`apic64_enable_for_ap()` 只写本核槽，
  记下本核 TPR/SVR/LVT，**绝不回写 `apic_mode` 这类系统级全局**。
- `apic64_is_initialized()` 改为按本核判断。
- 新增 `apic64_get_pcpu_state()` / `apic64_pcpu_id()` / `apic64_pcpu_version()`
  / `apic64_dump_pcpu()` 诊断接口，各核状态可按核区分读取。

`smp.c`：AP 在 `apic64_enable_for_ap()` 之后打
`[SB19] cpu=N lapic_id=N ver=N`。

### 踩到的坑：早期 #PF

第一版把 `apic64_get_id()` 直接改成读硬件，结果启动早期就：

```
[EXCEPTION] vector=0xE error=0x0 rip=0x1449BB
  CR2=0xF0000020
  PML4e=0x26A3027 PDPTe=0x0
```

`CR2=0xF0000020` 就是 LAPIC ID 寄存器地址，`PDPTe=0` 说明那一页还没映射。
`apic64_init()` 之前，启动流程里已经有代码经 `smp_get_current_cpu()`
调到 `apic64_get_id()`（旧版这时候返回全局 0，不碰硬件，所以一直没出事）。

修法：加 `apic_mapped` 标志，`apic64_init()` 映射完 LAPIC/IOAPIC 页才置 1；
`apic_hw_id()` 在未映射时回落到已缓存的 0 号槽值，不碰 MMIO。

### 验证

5/5 轮 `desk=1 EXC=0 PASS=1 FAIL=0`，每轮 AP 都打：

```
[SB19] cpu=0x1 lapic_id=0x1 ver=0x14
[APIC64] Local APIC ID=0 Ver=0x14
```

**AP 拿到自己的 ID=1** —— 这是改造前做不到的（旧代码这里恒为 0）。
`timer calibration failed` 出现 2 次，属已知 OBS-01（约 16% 概率），非本轮引入。

截图入库 `docs/assets/smp-sb19-0.58.7.png`。

---

## 1.106 SB-04 / SB-05 / SB-06 / SB-07 / SB-08：阶段 1 收口（0.58.8）

阶段 1 原本剩 5 条，本轮一次做完。这 5 条全是"改动小、但多核下一定会暴露"的
计数与输出问题。

### SB-04 异常计数原子化

`lib64.c` 里 `g_user_exception_count++` 改为 `atomic_inc64(&g_user_exception_count)`。
这个计数被启动自检电池用来判定"是否发生过用户态异常"，多核同时异常时旧版
`++` 是读-改-写三步，两个核会丢掉一次递增，导致自检计数偏低、结论偏向"干净"。
类型和对外符号不变。

### SB-05 打印锁

新增 `static spinlock_t print_lock`，`vga_puts` 拆成两层：

```c
void vga_puts_nolock(const char *str);   /* 原实现 */
void vga_puts(const char *str)           /* 对外入口 */
{
    uint64_t f = spinlock_lock_irqsave(&print_lock);
    vga_puts_nolock(str);
    spinlock_unlock_irqrestore(&print_lock, f);
}
```

拆两层而不是在函数体内逐 `return` 收口，是因为 `vga_puts` 有"输出重定向到钩子"
的提前返回分支，逐点收口容易漏一个就是一个死锁。

三条约束：

1. 锁内只做字节输出。`serial_puts_local()` 直接写端口、不回调 `vga_puts`，安全。
2. **panic 路径走 `vga_puts_nolock()`**——崩溃时很可能正持有这把锁，再去拿就是
   自己等自己。`hepu_panic()` 保留原来的 `vga_puts` 调用是安全的：它一般在
   非持锁上下文被调用；真在持锁时崩溃的极端场景，靠串口直出不丢信息。
3. 锁用 `irqsave` 版本：时钟中断里的日志输出会和主循环抢同一把锁，不先 `cli`
   就是本核自死锁。

### SB-06 堆统计读取加锁

核对下来 `kmalloc`/`kfree` 的统计与 `trace_add`/`trace_del` **本来就在**
`heap_lock` 临界区里（`alloc_locked` / 释放路径的调用方持锁），写侧没有缺口。
真正缺的是读侧：`kheap64_get_stats()` 不带锁逐字段读，跨核可能读到
`heap_used` 已加、`free` 还没算的中间态，出现 `used + free != total`。
改为一次性在临界区内取完快照再赋值；`kheap64_get_usage_percent()` 同样处理。

### SB-07 IP ID 原子递增

`net64_ip.c` 的 `ip_id_counter` 从 `uint16_t` 改为 `volatile uint32_t`，
取 ID 改 `atomic_xadd32(&ip_id_counter, 1)` 取返回值低 16 位。
旧版 `ip_id_counter++` 在多核同时发包时会取到相同 ID，而分片按
`(src, dst, id)` 归并——重复 ID 会让两片**不同数据**被拼成同一个包。

### SB-08 FPU 保存区扩到 16 槽

原来 `fpu_save_area[8][512]`，而 `MAX_CPUS` 是 16，`irq_handler_64` 里算核 ID 时
写的是 `(... >> 24) & 7`。也就是说 **核 ID ≥ 8 会回绕到低编号核的槽**，
两个核共用一块 FXSAVE 区，中断嵌套时互相覆盖浮点状态。

改法：

- `#define FPU_SAVE_MAX_CPUS 16`，数组改成 `[FPU_SAVE_MAX_CPUS][512]`；
- 核 ID 读取去掉 `& 7`，改成 `& 0xFF`（LAPIC ID 是 8 位）；
- 越界时明确报 `[FPU] ERROR: cpu id beyond fpu_save_area` 并停机，
  而不是悄悄复用别人的槽。

**这条不改的话 `-smp 12` 是隐性踩内存**，而且症状是浮点结果错乱，
排查起来会怀疑到编译器或 SSE 支持上。

### 验证

- 5/5 轮 `desk=1 EXC=0 PASS=1 FAIL=0 SPINERR=0`，`timer calibration failed` 0 次；
- 追加 `-smp 12`：`desk=1 exc=0 fpuerr=0`，11 个 AP 全部打出 `[SB19]`，无 FPU 越界；
- `-smp 4`：3 个 AP，同样干净。

截图入库 `docs/assets/smp-sb04-08-0.58.8.png`（Net RX 772 KB/s 持续刷新，
日志无半行交错，说明打印锁生效）。

### 环境侧：工具链迁出 /tmp

本轮 `/tmp/pkg` 又被周期性清理（这是第三次），连带 QEMU、sgdisk、mkfs.vfat、
mtools 全丢。这次把工具链直接装在项目目录下 `.toolchain/pkg/root2`，
按 jammy 索引解析依赖闭包共 101 个包。另发现一个问题：**家目录所在文件系统
不支持 fcntl 文件锁**，QEMU 打开镜像时报 `Failed to get "write" lock: Is another
process using the image`，而 `pgrep` 查下来一个残留进程都没有。解法是给
`-drive` 加 `,file.locking=off`。

`tools/build_uefi_dual.py` 的输出路径改为读环境变量 `HEPU_IMG`（默认仍是
`/tmp/hepu_uefi_dual.img`），回归脚本可以指到项目目录下。

---

## 1.107 SB-05 补口：串口直写路径纳入 print_lock（0.58.9）

### 发现过程

跑完 SB-04..08 的回归后翻串口原文，发现这样一行：

```
[SB19] cpu=[SMP] 0x0000000000000001 lapic_id=AP 0x0000000000000001 ver=0x...
```

BSP 打的 `[SMP]` 和 AP 打的 `[SB19]` 插进了同一行。但 VGA 侧画面是干净的，
`SPINERR=0` 也是 0 —— 说明 SB-05 的 `print_lock` 生效了，只是没覆盖全。

### 根因

`smp.c` 里 `[SB17]` / `[SB18]` / `[SB19]` 这些诊断输出走的是
`sb18_serial_puts()` 一个 **static 函数，`sb18_outb(0x3F8, ...)` 直接写 COM1
端口**。它完全绕过 `vga_puts`，自然也绕过了 `print_lock`。于是：

- BSP 的 `[SMP] ...` 走 `vga_puts` → 持锁 → 写串口；
- AP 的 `[SB19] ...` 走 `sb18_serial_puts` → 不持锁 → 同时写串口；
- 两个核在 `0x3F8` 上抢 LSR 的 THR 位，字节就交错。

### 修法

`lib64.c` 把同一把 `print_lock` 开一对导出接口：

```c
uint64_t print_lock_acquire(void);   /* spinlock_lock_irqsave(&print_lock) */
void     print_lock_release(uint64_t flags);
```

`sb18_serial_puts()` 整个循环包进这对接口。**不做成两个独立的锁**——两把锁
保护同一个资源等于没保护。

### 验证

3/3 轮回归后，串口里是干净的整行：

```
[SB19] cpu=0x0000000000000001 lapic_id=0x0000000000000001 ver=0x0000000000000014
```

再 `-smp 12` 时 11 条 AP 行也全部完整。这条补上之后，**串口日志才能当证据用**。

### 顺带记录

本轮 3 轮里 2 轮出现 `[APIC] WARN: timer calibration failed, fallback ICR=35000`，
且每轮是两条（BSP 首次校准 + 之后一次 recalibrate 各一条），比历史记录的
约 16% 明显偏高。已知根因在 OBS-01，且 `apic64_timer_recalibrate()` 失败后
**未恢复 LVT 周期模式**（校准函数开头会置屏蔽位）。三轮 `desk=1 PASS=1` 说明
后果仍被兜住，但这件事要单独处理，本轮不混入。

---

## 1.108 Info 面板去冗余（0.58.9）

用户最早提的那条：侧栏 Info 面板的 System Info 区块里，CPU/MEM/DSK 三根
进度条下面又跟一段文字（`CPU: 2 cores` / `Memory: 979 MB`），一根条和一个
数字说同一件事。

核对下来，那三根进度条在 V0.55 侧栏改版时已经被去掉了，当前是纯文本版。
但新的问题出现了：**Memory 只剩总量、没有占用率**——老版进度条上的
"4%"这个信息被一起砍掉了；Disk 则完全不在面板里。

所以这轮不是"再删一遍"，而是把信息重排成不重复、也不丢：

| 旧版（三进度条 + 文本） | 现在 |
|---|---|
| CPU 条 0% + `CPU: 2 cores` | `Kernel  64-bit SMP  2 cores`（合并成一行） |
| MEM 条 4% + `Memory: 979 MB` | `Memory  979 MB  4%`（合并成一行） |
| DSK 条 9% | `Disk  9%` |

- `Kernel: 64-bit SMP` 和 `CPU: N cores` 说的都是平台规格，合并；
- `Memory` 用 `pmm64_get_stats()` 的"非空闲物理内存"口径算占用率
  （V0.56.1 已把 used 口径修正为 `total - free`，含内核镜像/页表/固件保留区）；
- 新增 `Disk`，数据来自 `ext2_get_usage_percent()`（读超级块的
  `s_blocks_count` / `s_free_blocks_count`）。这两个值只在挂载时读取，
  运行期新写入不会实时反映，面板只给量级，不做强一致承诺；
  读不到时显示 `--` 而不是 `0%`。

实机数值 4% / 9% 与用户原始截图完全一致，说明数据源本来就对，只是展示方式重复。
截图入库 `docs/assets/info-panel-0.58.9.png`。

---

## 1.109 侧栏去掉 Info 标签页，系统信息并入任务管理器（0.59.0）

### 判断

侧栏原本是 Info / Apps / Weather 三页。Info 页放的是 Uptime、进程数、线程数、
Net RX/TX、Version、Kernel、CPU 核数、Memory、Disk。

这些数据任务管理器里**本来就有位置**：CPU/MEM/UP 摘要、网络曲线（RX/TX 带
标签）、进程表（PID/NAME/CPU%/MEM/STATE）、内核服务清单。侧栏那一页既没有曲线
也没有明细，信息量明显更低，却要和任务管理器各维护一份 —— 同一组数据两处
呈现，改一处就得记得改另一处。

代码里其实早就写过同样的判断（V0.58.3 的注释：*"CPU/MEM/DSK 三条占用条移除此处
—— 占用是任务管理器的职责, 两处同时呈现等于同一组数据画两遍"*），但那次只砍了
三条进度条，没砍整个页面。这轮把整页并掉。

### 落地

**侧栏**（`hpdesk.c`）：

- 标签栏 3 项 → 2 项：`{"Apps", "Weather"}`，宽度 `(sw - hs_sx(24)) / 2`；
- 删除整个 `g_sidebar_tab == 0` 的 Info 内容块，以及三条进度条遗留的几何变量
  （`s_bar_x` / `s_bar_w` / `s_pct_x`，早已无人使用）；
- 索引语义整体前移：Apps `1 → 0`，Weather 保持 else 分支；
  同步改了四处——标签绘制、点击命中、hit-test（`hpdesk_sidebar_tab_at()`）、
  `hs_sidebar_max_scroll()` 的 tab 判断；
- Tab 键循环 `% 3 → % 2`。

**任务管理器**（`hs_apps.c`）：新增一行系统摘要，放在 `MEM/PROC/DRV` 行上面：

```
Hepu OS v0.58.0  |  64-bit SMP  |  2 cores  |  DSK 9%
```

版本、平台规格、磁盘占用这三项原本只有侧栏 Info 页有，现在补进任务管理器。
网络速率不重复加——曲线旁已有 RX/TX 标签，再加一遍就是新造冗余。

### 验证

- 侧栏：只剩 `Apps` / `Weather` 两个标签，宽度按二等分，Apps 图标网格布局
  未受影响；
- 任务管理器：首行显示 `Hepu OS v0.58.0 | 64-bit SMP | 2 cores | DSK 9%`，
  下面 MEM/PROC/DRV、CPU/MEM/UP、THREADS 各行位置不变；
- 3/3 轮回归 `desk=1 EXC=0 PASS=1 FAIL=0 SPINERR=0`，`TEMP-` 标记残留检查为 0。

截图入库：`docs/assets/sidebar-2tabs-0.59.0.png`（侧栏两标签）、
`docs/assets/taskmgr-sysinfo-0.59.0.png`（任务管理器新摘要行）。

---

## 1.110 任务管理器重排：数值与曲线并为一个单元（0.59.1）

### 问题

上一轮把侧栏 Info 并进来之后，任务管理器变成"上面几行文字摘要 + 下面三条
独立曲线、曲线旁边只挂一个 `CPU%` / `RX` / `TX` 标签"的结构。两个毛病：

1. **数字和图形分属两个视觉单元，互相解释不了**。`CPU: 0%` 在上面一行，
   曲线在下面一行；看曲线不知道现在是多少，看数字不知道走势。内存更糟——
   `MEM: 4%` 是一行字，`44860 / 1002864 KB` 是另一行字，量纲还是 KB，
   和侧栏原来的 `979 MB` 差着三个数量级。
2. **进程表 CPU 列把 `idle` 写成 `100%`**。那是"空闲线程吃掉了没被别的进程
   用掉的 CPU 时间"，但在进程表里读起来就像"CPU 被这个进程吃满了"，
   和上面 `CPU: 0%` 直接矛盾——用户看到的就是"数字不对"。

### 改法

**每个指标变成一行：左边"名称 + 当前值"固定宽度右对齐，右边紧跟它自己的
图形。** 图形起点固定在 `cx + MC(112)`，三条对齐，所以竖着扫一眼就能把
数值和形状对上：

```
Hepu OS v0.58.0 | 2 cores | DSK 9%                    UP 43s
────────────────────────────────────────────────────────────
CPU   0%   [曲线 · 带淡底色块]
MEM   4%   [占用条 ███░░░░]              43 / 979 MB
NET  RX 0K  TX 0K  [曲线 · 带淡底色块]
────────────────────────────────────────────────────────────
PROC 4   DRV 9
PID  NAME      CPU  MEM  STATE
0    idle       --    0   sys      ← idle 的 CPU 列改为 "--"
```

具体：

- 版本 / 平台规格 / 磁盘压成顶部一行，uptime 右对齐在同一行末尾，
  不再各占一整行；
- CPU 与 NET 用时间曲线，**MEM 没有时间序列，改用占用条**——给一个涨落很小
  的量画曲线本来就没意义；条右侧直接标 `43 / 979 MB`，单位与侧栏统一为 MB；
- 曲线区加一层 6% 白的淡底色块。之前曲线是浮在面板底色上的裸柱，
  0% 时只剩 1px 基线，整块看着像"空表格"；
- NET 的 RX/TX 两个速率从上下两行小字改成一行 `RX 0K  TX 0K`，
  之前两行挤在 22px 高里，下端已经压到曲线区；
- `idle`(PID 0) 的 CPU 列显示 `--`，不再显示 100%。

### 验证

- 3/3 轮 `desk=1 EXC=0 PASS=1 FAIL=0 SPINERR=0`；
- 截图确认：CPU `0%` 与曲线同一行、MEM `4%` 与条及 `43 / 979 MB` 同一行、
  NET 两个速率在一行且与曲线同一行、idle 行 CPU 列为 `--`；
- `TEMP-` 标记残留检查为 0。

截图入库 `docs/assets/taskmgr-redesign-0.59.1.png`。

### 遗留

`UP` 与 `PROC`/`DRV` 的排版仍偏紧凑，窗口高度 380 基准下，进程数一多
（超过约 6 行）KERNEL SERVICES 会被挤掉。要做成可滚动的分区需要引入
窗口内滚动容器，这是另一件事，不在本轮。

---

## 1.111 §1.110 回退：任务管理器恢复原布局（0.59.2）

> **本文档不删除 §1.110 原文。** §1.110 记录的是当时做过的改动与当时的判断，
> 结论已被用户否决，改动已回退。保留原文是为了留下"当时为什么这么改"的依据。

### 回退原因

用户明确要求改回原来的视图："视图怎么给我改成那样了，样式都不统一，给我改回
我圈起来的视图"。§1.110 的重排虽然解决了"数字与曲线分离"，但引入了新的不一致：

- 数值字号用 `MC(12)`、标签用 `MC(10)`、右上 uptime 用 `MC(10)`，整套
  Task Manager 内部出现了三档字号，而系统其他窗口（Settings / Files）是统一的
  `MC(11)` 正文 + 单一标题字号；
- 指标行左列的"名称 + 数值 + 图形"三列结构只在任务管理器里有，其他窗口没有
  第二处，看起来像从别的系统里搬来的一块；
- 内存改成占用条之后，与上面 CPU、下面 NET 的曲线形态不一致，三条指标行
  三种视觉形态。

也就是说：把"数字和曲线分离"修掉的同时，把"整个面板与其他窗口不统一"引了进来。
后者对用户更刺眼。

### 回退动作

```
git checkout d680ff3 -- kernel/arch/x86_64/hs_apps.c
```

`hs_apps.c` 与 `d680ff3` 完全一致（`git diff d680ff3 -- ...` 为 0 行），
即恢复为：

```
Hepu OS v0.58.0 | 64-bit SMP | 2 cores | DSK 9%
MEM: 44860/1002864 KB   PROC: 4   DRV: 9
CPU: 0%   MEM: 4%   UP: 10s
THREADS  total: 1  active: 0
thread_exit: tid 1000 (handed off)
[CPU 曲线]  CPU%
[RX 曲线]   RX
[TX 曲线]   TX
PID  NAME  CPU  MEM  STATE
```

验证：3/3 轮 `desk=1 EXC=0 PASS=1 FAIL=0 SPINERR=0`。

### 已知保留问题（未修，等确认）

1. `idle`(PID 0) 的 CPU 列仍显示 `100%`，与上方 `CPU: 0%` 并存 —— 语义上
   没错（空闲线程占用剩余 CPU 时间），但读起来矛盾。回退后此现象恢复。
2. `MEM: 44860/1002864 KB` 与侧栏的 `979 MB` 量纲不一致。

### 附带调查：滚动条的全局现状

用户提问"窗口没有滚动条，其他的是不是也没有？"。逐处核对结果：

| 位置 | 滚动方式 | 可见滚动条 |
|---|---|---|
| 侧栏 Apps 页 | 滚轮，`g_sidebar_scroll` + `hs_sidebar_max_scroll()` | **有**，3px 宽 accent 细滑块（`Clavis StyledScrollBar`） |
| 侧栏 Weather 页 | 同上 | 无 |
| 终端 | 键盘 PgUp/PgDn，`t_scroll` | 无 |
| 文件管理器 | `f_scroll` / `f_tab_scroll` | 无 |
| 任务管理器 | **完全没有滚动**，内容超出直接裁掉 | 无 |
| Settings / 其他 app | 固定内容 | 不适用 |

结论：全系统**没有通用滚动条组件**，只有侧栏 Apps 页手绘了一处滑块。
任务管理器的 `KERNEL SERVICES` 被挤出窗口，本质是没有滚动容器，不是漏画了条。
要做成通用的需要窗口级滚动容器（内容区裁剪 + 偏移 + 滑块 + 滚轮/键盘绑定），
属于独立改造，待确认是否纳入本轮。

---

## 1.112 窗口内容滚动容器 + 通用细滑块（0.59.2）

### 背景

用户提问"窗口没有滚动条，其他的是不是也没有？"。逐处核对：

| 位置 | 滚动方式 | 可见滚动条 |
|---|---|---|
| 侧栏 Apps 页 | 滚轮 | 有（手绘 3px 滑块） |
| 侧栏 Weather 页 | 滚轮 | 无 |
| 终端 | PgUp/PgDn | 无 |
| 文件管理器 | 滚轮 | 无 |
| 任务管理器 | **完全没有** | 无 |

任务管理器的 `KERNEL SERVICES` 被挤出窗口，不是漏画了一根条，而是**没有滚动
容器**：没有偏移量、没有裁剪、没有滚轮分支。图形层也没有 clip rect —— 内容
一旦超出边框就会画到窗口外面、盖住别的窗口。所以这件事不是"补一根条"，是
要先有三件东西：偏移、裁剪、滑块。

### 一、抽出通用滑块 `hs_scrollbar_draw()`

原来全系统只有侧栏 Apps 页手绘了一处滑块（`Clavis StyledScrollBar`）。抽成
公共函数放进 `hpdesk.c`，供各窗口共用，粗细/圆角/颜色一致：

```c
void hs_scrollbar_draw(int x, int track_y, int track_h, int offset, int max_scroll);
```

thumb 高度按 `视口/(视口+可滚量)` 折算，并设 16px 最小高度 —— 否则内容一长
滑块会细到看不见。

### 二、任务管理器的裁剪 + 偏移层

图形层没有 clip，所以用一层薄包装代替：

```c
static void m_text_c(..., int y, ...)  { y -= m_scroll_off; if (不在[m_vtop,m_vbot]) return; ... }
static void m_bar_c(int x, int y, ...) { 同上 }
```

`m_render` 与 `m_draw_curve` 里的绘制全部改走这两个包装，于是"内容偏移"和
"越界不画"同时成立。`m_vtop/m_vbot` 只在 `m_render` 期间被设成窗口内容区，
其余时间是无限区间，同文件里其他 app 不受影响。

可滚量用**上一帧**量出的内容末尾算（`m_content_bottom`）。内容高度每帧变化
很小，差一帧无感，但省掉了"先算一遍高度再画一遍"的两趟遍历。

裁剪区上下边界踩过两个坑，都修了：

- 下边界原来按 `height - MC(20)`，结果内容压到底部提示条上，`KERNEL SERVICES`
  和 `Press K then PID + Enter` 叠在一起 → 改 `MC(26)`（提示条占 16）；
- 上边界原来 `MC(22)`，滚动后内容会钻到 `Task Manager` 标题上 → 改 `MC(34)`。

### 三、滚轮分支

`hs_apps_handle_wheel()` 里原本没有任务管理器的分支，滚轮到任务管理器会被最后
一段终端逻辑吃掉 —— 表现是"在任务管理器上滚轮，终端在滚"。现在按焦点窗口
认领，步长 `MC(18)`。

### 四、文件管理器接入

文件管理器滚轮一直能用，但屏幕上没有"下面还有"的提示。列表模式接入同一个
`hs_scrollbar_draw()`；网格模式暂不接入（它的滚动语义是整行图标，另说）。

### 验证

- 3/3 轮 `desk=1 EXC=0 PASS=1 FAIL=0 SPINERR=0`，`TEMP-` 残留 0；
- 截图确认：任务管理器右侧出现滑块、底部提示条不再被内容压住、
  `KERNEL SERVICES` 退到视口下方由滚动查看；
- 另做过一次"强制滚到底"的临时验证（滚轮模拟不便），确认内容整体上移、
  顶部规格行滚出视口并被正确裁剪（不越界绘制）。

截图入库 `docs/assets/window-scrollbar-0.59.2.png`。

### 遗留

- 网格模式的文件管理器、侧栏 Weather 页仍无滑块；
- 滑块只支持滚轮，不支持拖动（拖动要接窗口内的鼠标按下/移动/释放，属于
  另一层输入分发）；
- `idle` 的 CPU 列仍显示 `100%`、`MEM` 仍用 KB 量纲 —— 回退时原样保留，
  未在本轮改动。

---

## 1.113 任务管理器：内存补上视图，占用率数字贴着图（0.59.3）

### 用户要什么

"你就不能把内存占用也做成视图吗？那些的数字的占用率，放到视图旁啊？"
并且明确选"拆"——顶部那三行文字整体拆掉。

具体三层：

1. 内存只有 `MEM: 4%` 和 `44860/1002880 KB` 两串文字，而下面 CPU / RX / TX 各有一条
   曲线 —— 内存被单拎出来当纯文字；
2. `CPU: 0%`、`MEM: 4%` 在顶部文字行，图形在下面另起一块，中间还夹着 THREADS，
   数和形状分属两个区域；
3. 顶部应该只留规格和计数。

### 改法

**顶部压成一行**：`Hepu OS v0.58.0 | 64-bit SMP | 2 cores | 979 MB | DSK 9%`，
`UP 39s` 右对齐同行。

内存**总量**归到这一行（它属于规格，不是占用率），**占用率**跟着 MEM 视图走，
不再在上面重复。

**四条视图，同宽同高、左端对齐**：

```
CPU   0%   [曲线]
MEM   4%   [曲线]      ← 新增，紫色，原来是纯文字
RX    0K   [曲线]
TX    0K   [曲线]
```

每条左侧是"名称 + 当前值"，固定列宽（名称在 `cx`，数值在 `cx + MC(46)`），
图形起点统一在 `cx + MC(96)`。字号全程 MC(11) 一档 —— 上一版被判"样式不统一"
就是因为我引入了 12/10/9 三种字号，这次不再出现。

内存用**时间曲线**，和另外三条同形态。上一版给它单独做占用条（"内存涨落小，
画时间序列没意义"），形态和另外三条对不上，本身也属于"不统一"的一部分。

**THREADS 压成一行**：`THREADS 1  ACTIVE 0  last exit tid 1000`，放在四条视图
下方。它是计数不是占用率，两行拆成一行是为了不再把主视图往下推。

**PROC/DRV 计数**：原来在顶部的 `MEM: … PROC: 4 DRV: 9` 行里，那行拆了，
移到 `KERNEL SERVICES` 标题行右侧，右对齐。

### 踩到的坑

规格行要显示物理内存总量，而原代码里 `pmm64_get_stats()` 的调用在规格行**之后**，
于是先拼字符串时 `mt` 还是 0，屏幕上出现 `0 MB`。把取统计提到规格行之前，
并删掉后面重复的那次调用。

另有两处右对齐压到滑块上：`UP` 和 `PROC/DRV` 都改为距右边 `MC(12)` 收笔。

### 验证

- 3/3 轮 `desk=1 EXC=0 PASS=1 FAIL=0 SPINERR=0`，`TEMP-SHOT` 残留 0；
- 截图确认：规格行含 `979 MB`（不再是 0 MB）、四条视图左端对齐、MEM 有曲线、
  每条数值紧贴其视图左侧、THREADS 单行、`PROC 4  DRV 9` 在服务区右对齐。

截图入库 `docs/assets/taskmgr-curves-0.59.3.png`。

### 遗留

- 四条曲线在数值接近 0 时只剩 1px 基线，整块看起来偏空；要不要给视图区加淡底色
  是审美问题，等确认；
- `idle` 的 CPU 列仍显示 `100%`（语义没错但读起来与 `CPU 0%` 矛盾），本轮未动。

---

## 1.114 任务管理器分标签页 + 表格划分 + 视图收窄（0.59.4）

### 用户提的问题

1. 排版还是有重叠；
2. 四条视图有点宽；
3. 进程表"没有表格划分"，底下的设备列表也是；
4. 建议：**给任务管理器加一个标签页，把进程下面的设备状态放进去**。

### 为什么第 4 条本来就应该做

`KERNEL SERVICES` 自从 0.57.0 加进来就一直硬接在进程表下面。它是"驱动框架里注册了
什么"，进程表是"调度器在跑什么"——两件不相干的事共用一页、共用一段高度。进程一多，
驱动清单就被挤出视口（0.59.2 加滚动容器只是把症状盖住）。分页就是根因上的修法。

### 改法

**标签栏**：任务管理器内容顶部加 `Processes | Devices` 两页，active 项 accent 底白字，
与侧栏标签同一套画法（`m_draw_tabs`）。标签栏固定在窗口顶部、不参与内容滚动 —— 所以它
直接调 `gfx64_fill_rounded_rect` / `font64_draw_string`，不走 `m_text_c` / `m_bar_c`
那层包装（那层会套滚动偏移、还会被内容裁剪区切掉）。内容裁剪区 `m_vtop` 从 `MC(34)`
下移到 `MC(50)`，让内容在标签栏之下开始。

**Processes 页**：规格行 → 四条视图 → THREADS → 进程表。设备列表整段搬走。

**Devices 页**：`DRIVER | STATE` 两列表格 + 行分隔线，`PROC n DRV n` 计数右对齐在表头
右侧。

**表格划分**：进程表与设备表都加了**极淡的行分隔线**
（`hs_apps_mix(panel_bg, 0xFFFFFF, 9)`）。原来只有一列列文字，没有横线，看着就是一坨。

**视图收窄**：`gw` 从 `cw - MC(96)` 收到 `cw - MC(96) - MC(88)`，右侧留白，不再顶到滑块。

**重叠**：TX 曲线与 THREADS 行之间原来的 `MC(14)` 撑到 `MC(20)`；THREADS 与表头之间
补一条分隔线，免得计数行和表格粘在一起；进程行高 `MC(15) → MC(17)`（让出行线的位置）。

### 两个坑

**1) 窗口内容区没有点击派发。** `hpdesk_mouse_down` 只处理标题栏（三按钮/双击/拖拽）
和 Files 的标签（`hs_files_tab_click`），窗口内容区的点击直接落到下层窗口 —— 标签页
点上去毫无反应。补了一条特例派发：命中 Task Manager 窗口内容区时调
`hs_taskmgr_click(x, y)`，由它自己判断是不是落在标签栏。

**2) Tab 键切不了页。** 第一版用 Tab，实机没反应。查 `hpdesk` 的按键处理顺序，
`if (key == 0x0F || key == '\t') { g_sidebar_tab = (g_sidebar_tab + 1) % 2; return; }`
排在 `hs_apps_handle_key()` **之前**，而且不判断当前聚焦哪个窗口 —— Tab 被侧栏全局
截走了。改侧栏行为会破坏用户已经接受的侧栏交互，所以任务管理器改用**左右方向键**切页
（`KEY_EXT_LEFT` / `KEY_EXT_RIGHT`），提示条同步改成
`K + PID ends a process   ·   arrows switch page`。

### 验证

- 3/3 轮 `desk=1 EXC=0 PASS=1 FAIL=0 SPINERR=0`，`TEMP-SHOT` 残留 0；
- 截图两张：Processes 页（标签栏 active 在左、视图收窄、进程表带行线）、
  Devices 页（`DRIVER/STATE` 两列 + 9 个驱动 + `PROC 4  DRV 9`）；
- Devices 页是通过**真实调用 `hs_taskmgr_click`** 切过去的，同时验证了命中几何
  （第一次算错：把点击点放在了两个标签的交界缝里，正好不命中，改到右半标签中心才进）。

截图入库 `docs/assets/taskmgr-tabs-processes-0.59.4.png`、`taskmgr-tabs-devices-0.59.4.png`。

### 遗留

- 设备页表格只有两列、右侧大片留白，列宽按内容定的，没有按窗口比例摊开；
- 进程表仍然没有竖线（只加了行线），要不要列分隔线看观感；
- 设备页的 `P`/`D` 之类字母快捷键没做（K 已被结束进程占用，暂用方向键）。

---

## 1.115 进程表列竖线 + PRIO/CORE/TIME 三列（0.59.5）

### 用户要求

"加竖线，丰富信息和状态。"

### 两件事

**一、表格化补完**：0.59.4 只加了行分隔线，列与列之间还是空的，读起来仍是"几段文字
并排"。这次在 `PID | NAME | CPU | MEM | PRIO | CORE | TIME | STATE` 每两列之间补一条
竖线（`hs_apps_mix(panel_bg, 0xFFFFFF, 18)`，12% 太淡看不出来）。竖线从表头画到最后
一行，用 `m_table_top` / 当前 `cy` 定上下界；设备页 `DRIVER | STATE` 同样补一条。

**二、丰富信息**：原表只有 PID/NAME/CPU/MEM/STATE 五列。`process64_stat_t` 里其实
一直有 `priority`、`cpu_id`、`runtime` 三个字段，但 `hs_process_info_t` 这层把它们
丢掉了 —— 界面自然显示不出来。这次：

- `hsservices.h` 的 `hs_process_info_t` 补 `priority` / `core` / `runtime` / `env_type`；
- `hs_process_monitor_update()` 从 `process64_snapshot()` 一并填过去；
- 表里新增 `PRIO` / `CORE` / `TIME` 三列。`TIME` 是累计运行 tick 按 100Hz 折算，
  一分钟内显示秒、以上显示分（截图里 idle 是 `5m`，其余内核线程 `0s`）；
- `MEM` 列补单位 `K`（原来光一个数字，单位还是 KB/MB 混用的遗留问题的一部分）；
- `STATE` 列按状态分色：`sys` 灰、`RUN` 绿、`kthr` 蓝、`idle` 灰。

### 踩到的坑：窗口创建了但屏幕上没有

改完第一次截图，任务管理器**完全不见**，只有 Settings。加打印才知道：
`hs_window_create` 成功（780x570）、`visible=1`、`z_order=[0,1]` 里它在最上层、
`rect=450,40` 正确 —— 什么都对，就是不显示。

原因是**没人标脏**：`Z_ORDER` 和几何都对，但这一块区域没被标记为需要重绘，合成器
一直贴的是旧像素（壁纸）。在 TEMP 注入块里补一句 `hpdesk_mark_dirty()` 才跳出来。

这解释了之前偶发的"注入窗口要等别的窗口重绘才出现"。真实用户从 App Store 点开任务
管理器不会有这个问题 —— 点击本身就会标脏；只有"启动即注入"这种没有交互的路径才暴露。
属于既有隐患，记在这里，`hs_window_create` 之后是否应该统一标脏另议。

### 验证

- 3/3 轮 `desk=1 EXC=0 PASS=1 FAIL=0 SPINERR=0`，`TEMP-SHOT` / `TMDBG` 残留 0；
- 截图两张：Processes 页（八列 + 七条列竖线 + 行线，`PRIO 2 / CORE 1`、`TIME 5m` 有数据）、
  Devices 页（`DRIVER | STATE` 两列 + 竖线 + `PROC 4 DRV 9`）。

截图入库 `docs/assets/taskmgr-grid-processes-0.59.5.png`、`taskmgr-grid-devices-0.59.5.png`。

### 遗留

- `idle`(PID 0) 的 CPU 仍显示 `100%`：它按 `runtime/总tick` 算，idle 确实占满时间，
  但和顶部 `CPU 0%` 并排读起来刺眼，要不要改成 `--` 未定；
- `TIME` 的 100Hz 折算取自 PIT 频率，若将来 tick 频率变更这里要同步；
- 设备页列宽仍是按内容定的，右侧留白偏大。

---

## 1.116 曲线终于画满整宽（0.59.6）

### 用户的问题

"任务管理器的视图没有曲线吗？"

### 查下来是三层问题叠在一起

**第一层：那不是折线，是柱状采样。** 0.56.2 时发现 `draw_line` 在窗口内容层上
画不出来，于是改成"每个采样 2px 宽、3px 步进"的柱子。柱与柱之间有空隙，值又常年
贴底，屏幕上就是一排短竖线，远看根本不成形。改成**连续面积图**：2px 步进无缝拼接，
半透明色从基线填到当前值，顶上再压 1px 亮线当轮廓。

**第二层：量程跟着 UI 缩放了。** 调用处写的是 `m_draw_curve(..., MC(100))` /
`MC(256)`，而 `MC()` 是按窗口缩放系数换算的 —— 1.5 倍下满量程变成 150，
`v=100` 只能画到图高的 2/3。**满量程不是像素，不该缩放**。改成裸 `100` / `256`。

**第三层（关键）：`M_HIST` 只有 72。** 每个采样 2px，72 个采样最多铺 144px，
而图形区有 470px 左右 —— 不管系统跑多久，曲线**永远只占右侧一小段**，左边全是空的。
按最宽图形区反推，改成 288（`(cw-MC(96)-MC(88)) / 2` 的基准值）。

另外补了一条：**首次打开时把整条历史预填成当前值**。`hist` 从全 0 开始、且只在
渲染帧 push，开窗头几秒只有右端一点点颜色，看着依然像"没接上数据"。预填后一开窗
就是满宽水平线，之后右端逐步被真实采样替换。

### 顺带记一个注入坑

TEMP 注入的窗口这轮又出现"创建成功但屏幕上没有"。上一轮（0.59.5）的结论是**标脏**
不够，这轮发现光 `hpdesk_mark_dirty()` 也不够 —— 还要**手动调一次 `tw->render(tw)`**：
窗口刚创建时 wm 认为内容干净，不会主动调 render，于是这块区域从来没有被画过。
标脏只让它重绘，重绘时读到的还是旧像素。两条都补上才稳定出现。
（真实用户点图标打开不受影响：点击 → 标脏 → wm 调 render，链路是完整的。）

### 验证

- 3/3 轮 `desk=1 EXC=0 PASS=1 FAIL=0 SPINERR=0`，`TEMP-SHOT` 残留 0；
- 截图：MEM 紫色面积带、CPU 绿色、RX/TX 均**满宽**，不再是右端一截。

截图入库 `docs/assets/taskmgr-curve-fix-0.59.6.png`。

### 说明

空闲系统 CPU 0%、MEM 4%，曲线自然贴在图底 —— 这是数据真实，不是画不出来：
数值一上去就有明显起伏。要不要改自适应量程（把小值放大好看）属于取舍，没擅自改，
因为那会让"4%"的高度看起来像 20%，和旁边的数字对不上。

---

## 1.117 窗口阴影重做（0.59.7）

### 用户的话

"窗口的阴影怎么那么假。"

### 假在哪

原实现（0.57.0）：

```c
for (int k = 3; k >= 1; k--) {
    int d = k * hs_s(4);
    uint32_t c = hs_mix_color(g_shell.colors.window_bg, 0x000000, 10 + (3 - k) * 9);
    gfx64_fill_rounded_rect(w->x - d, w->y - d + hs_sy(6), ...);
}
```

三个问题叠在一起：

1. **只有 3 层、每层 4px、相邻层差 9 个百分点**（10 / 19 / 28%）。三层之间
   不是渐变而是**跳变**，叠出来就是给窗口描了三圈实心边；
2. **基色用 `window_bg`**（窗口自己的背景色）混黑。阴影该贴合的是它下面那层走廊
   （壁纸/别的窗口），用窗口色当基色，在壁纸上就会露出一圈跟谁都不挨着的色环；
3. 只向下偏 6px，四边均摊，光源方向不明显。

### 改法

```c
int step = hs_s(2);  int layers = 9;                 /* 9 层 2px, 相邻层只差 3% */
uint32_t base = gfx64_get_pixel(w->x + w->width/2, w->y + w->height + hs_sy(12));
for (int k = layers; k >= 1; k--) {
    int d = k * step;
    int pct = 5 + (layers - k) * 3;                  /* 外 5% -> 内 29% */
    gfx64_fill_rounded_rect(w->x - d, w->y - d + hs_sy(8), ..., hs_mix_color(base, 0, pct));
}
```

- **采样真实像素当基色**：取窗口下缘正中往下 12px 那一点（影子真正会落到的地方），
  阴影就贴在当前背景上，换壁纸/换主题都不会露出色环；
- **层数 3 → 9、层距 4 → 2、色差 9% → 3%**：台阶细到看不出来，视觉上是连续衰减；
- 向下偏移 6 → 8，影子偏下，符合"光从上方来"。

### 为什么不用 `gfx64_blur_region`

`gfx64_blur_region()` 是现成的、能做出真正的软阴影，但它是**逐像素盒式模糊**：

```c
for py: for px: for dy(-r..r): for dx(-r..r): gfx64_get_pixel(...)
```

800×640 的区域、blur=10 就是 4.8×10^5 × 441 ≈ **2 亿次像素读**，一次窗口重绘就要
这么久，桌面会直接卡死。所以走"多层渐变"这条路，代价是 9 次圆角填充（每次约
50 万像素写），实测启动到桌面仍是 14 秒左右，没有变慢。

### 验证

- 1 轮 `desk=1 EXC=0 PASS=1 FAIL=0 SPINERR=0`；
- 截图：Settings 与侧栏周围是连续衰减的暗影，三条色带消失。

截图入库 `docs/assets/window-shadow-0.59.7.png`。

### 遗留

- 仍是"无 alpha 混合"的近似：每层是不透明色块，靠细密层数骗过眼睛。真要精确
  阴影得等合成器支持逐窗口 alpha 或离屏合成；
- 采样点若恰好落在另一个窗口上，基色会取到那个窗口的颜色（阴影偏色）。概率低但存在。

---

## 1.118 窗口阴影改用真正的 alpha 混合（0.59.8）

0.59.7 把"3 层色带"做到"9 层细渐变"，只是把台阶变细，**问题本身没解决**：色块
依然是不透明的，层间一定有台阶；基色依然得靠采样去猜，一旦采样点落在别的窗口上
阴影就偏色。用户追问"怎么解决"，指的就是这两条。

### 解法

直接逐像素混黑，不再叠色块：

```c
for (y = y0-dmax; y <= y1+dmax; y++)
  for (x = x0-dmax; x <= x1+dmax; x++) {
      if (在窗口矩形内) continue;            /* 内部被窗口盖住, 白画 */
      dist = 到窗口边界的距离(四角用 math64_isqrt 取欧氏距离);
      a = (dmax - dist)² * amax / dmax²;      /* 二次衰减, 连续 */
      gfx64_blend_pixel(x, y, 0x000000, a);
  }
```

三件事一次解决：

1. **没有台阶**：alpha 是距离的连续函数，不是几层离散值；
2. **不会偏色**：真混合，底下是什么颜色就自然变暗，不需要采样基色；
3. **跟着圆角走**：四角的距离用欧氏距离算，阴影在圆角处自然收弧。

### 开销

关键在于**只遍历窗口外侧那一圈**，内部整块跳过：

| | 0.59.7（9 层填充） | 0.59.8（环 + blend） |
|---|---|---|
| 覆盖像素 | 9 × 55 万 ≈ 490 万次写 | ≈ 9 万次 读+混+写 |
| 台阶 | 有 | 无 |
| 偏色 | 有 | 无 |

实测启动到桌面 15s（之前 14s），量级一致，没有出现掉帧。

### 遗留

- 阴影仍是"1px 半径的硬衰减"，不是高斯；要真正柔和得按 distance 做多次采样，
  又是 10 倍开销，暂不做；
- 每个窗口重绘都要重算一遍阴影，没有缓存。如果以后桌面重绘变频繁，可以考虑
  把阴影烘到一张离屏图里复用（窗口改尺寸时才重算）。
