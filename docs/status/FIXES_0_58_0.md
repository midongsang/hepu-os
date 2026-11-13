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

