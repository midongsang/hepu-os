---
title: SMP 硬化包进度对齐与阶段 5（SB-25）前置设计
status: active
sys_version: 0.58.0
updated: 2026-11-13
owner: kernel
source_of_truth: null
supersedes: null
---

# SMP 硬化包进度对齐与 SB-25 前置设计

> 定位：本文不是新计划。[PLAN_SMP_HARDENING_R5.md](./PLAN_SMP_HARDENING_R5.md)
> 的 SB-01..SB-25 仍是唯一清单，本文只做三件事：① 对账本轮实际做了什么，
> 如实说明**顺序偏离**；② 记录两条计划里没有、实测才发现的 SB-25 阻塞；
> ③ 给出下一步的建议顺序与可测退出准则。

## 一、对账：本轮到底落了哪几条

| 条目 | 计划归属 | 本轮状态 | 说明 |
|---|---|---|---|
| **SB-23** 就绪队列并发 | 阶段 4 | **已完成（部分）** | 新增调度器自旋锁 `g_sched_lock`（irqsave 语义：`pushfq` 记 IF → `cli` → lock，解锁按原 IF 恢复）；`process64_schedule()` 的候选收集/加权选择/出队/计数更新全在锁内；**两处 `context_switch_64` 前释放锁**，不持锁跨切换 |
| **SB-17** per-CPU 存储（一部分） | 阶段 3 | **已完成（仅当前进程 PID 一项）** | `current_pid` 全局单值 → `per_cpu_current_pid[16]`；`process64_get_current()` 回退分支、`process64_create` 的 `parent_pid`、`process64_get_current_pid()` 全部按核取。SB-17 的其余部分（per-CPU 内核栈槽、`MSR_KERNEL_GS_BASE`、`g_syscall_*_slot` 淘汰）**未动** |
| **新增：`running_on` 归属标记** | 计划外 | **已完成** | 计划里没有这一项。`process_t` 加 `volatile int running_on`，选进程时跳过被别核占着的、切换点做 `old→-1 / new→cur` 交接 |
| **SB-01/02/03/09** | 阶段 1 + 阶段 2 分配器 | **已完成**（`b82f416`） | 新增 `atomic64.h`；`g_kernel_tick64`、`irq_count[]` 原子化；PMM 加 `g_pmm_lock` + 三个 `*_nolock` 内部版 + `spinlock_lock_irqsave`。30 轮观察 30/30 PASS |
| **SB-18** 每核 TSS/RSP0 | 阶段 4 | **已完成**（§1.103） | `kernel_gdt64` 扩到 37 项、`kernel_tss64` 扩到 16 份；`tss64_init()` 多核化；`tss64_init()` 调用位次提到 `smp_init()` 之前（原来排在后面导致 AP `ltr` 撞空描述符 #GP）。20 轮 20/20 PASS |
| SB-04..SB-08、SB-10..SB-16、SB-19..SB-22、SB-24、SB-25 | — | **未开始** | SB-22/23 之外的就绪队列部分未动；SB-25 的两个阻塞已在第三节记录 |

## 二、顺序偏离：先做了阶段 4 的 SB-23

计划规定的顺序是 阶段 1（原子）→ 阶段 2（分配器加锁）→ 阶段 3（per-CPU）
→ 阶段 4（IPI + 调度器）。本轮直接从阶段 4 的 SB-23 切入。

**为什么现在这样做不危险**。`smp.c:140` 的 `if (cpu_id != bsp_cpu_id && 0)`
仍然是关的 —— **AP 核不参与调度，任何时刻只有 BSP 在跑进程**。所以：

- 就绪队列只有单核在读写，`g_sched_lock` 恒无竞争，只是把设施先摆好；
- PMM 虽然无锁（SB-09 未做），但单核分配不会出现"同一页帧双发"；
- 本轮所有改动在单核下条件恒不触发，20 次回归 20/20 PASS 也印证了这点。

**但不能顺着往下做 SB-25。** 计划的退出准则写得很清楚：SB-25（放开 AP
调度）的前提是 **SB-17 / SB-21 / SB-23 全部落地**。现在只做了 SB-23，SB-17
只做了七分之一，SB-21（IPI）一条没做。更要紧的是 SB-09（PMM）没做 ——
一旦 AP 参与调度、两核同时 `pmm64_alloc_page()`，就会复现计划 1.2 节描述的
"同一物理页帧挂进两个进程页表"，而且**不报错、只是随机踩内存**。

## 三、实测补充：计划里没写的两条 SB-25 阻塞

这两条是这两轮翻代码才发现的，计划文档的 SB-25 只写了"每核运行队列、唤醒
IPI、迁移策略"，没覆盖下面两条底层前提。

### 3.1 `context_switch_64` 不支持 `old == NULL`

`entry_64.S:494` 起的实现里，保存段是：

```asm
    movq %rsp, 56(%rdi)     /* 旧进程上下文 */
    movq %rbp, 48(%rdi)
    movq %rbx,  8(%rdi)
    ...
    testq %rdi, %rdi
    jz 9f
    fxsave 208(%rdi)
9:
```

`testq` 只保护了 `fxsave`，**前 6 条 `movq` 直接写 `%rdi` 的偏移**。传 NULL
进去会往地址 0x38/0x30/0x08 写 —— 直接踩坏低地址内存。而阶段 5 需要的正是
"某个核本来没在跑任何进程，要切进第一个进程"这种 `old == NULL` 的场景。

→ 必须先改：把 `testq %rdi,%rdi` 挪到保存段最前面，整段跳过（`jz 9f`）。

### 3.2 AP 核把 idle 当成自己的当前进程，且没有调度循环

`ap_c_entry()`（`smp.c:69`）里：

```c
per_cpu_current_proc[cpu_id] = 0;   /* idle进程 */
```

而且初始化时 `per_cpu_current_idx[i] = 0` 对**全部 16 个 CPU** 生效。于是 AP
一旦调用 `process64_schedule()`，`old = process64_get_current()` 拿到的就是
idle，它会把 `idle->state = READY`、入队、并把 **idle 的栈存进
`processes[0].context`** —— 而此时 BSP 也正在跑 idle，两边对同一份
`context.rsp` 互相覆盖。`running_on` 挡不住：问题不在"选谁"，而在"AP 核
误以为自己已经在跑 idle"。

另外 `ap_c_entry()` 结尾是：

```c
__asm__ volatile("sti");
while (1) { __asm__ volatile("hlt"); }
```

**AP 没有调度循环**，只是 hlt 等中断。就算在 `smp_timer_tick()` 里放开调度，
`process64_schedule()` 返回后在 `hlt` 里继续睡，切进来的进程也跑不起来。

→ 阶段 5 需要：AP 当前进程初始化为 -1（无当前）；每核自己的 idle 循环（或
无活可干时直接 `hlt` 而不占用 idle 的 context）。

## 四、建议的下一步顺序

按计划的依赖关系，加上上面两条实测阻塞，建议：

1. **SB-01 原子原语**（`atomic64.h` + 屏障）—— 后续所有原子化都复用它；
2. **SB-02 / SB-03**（`g_kernel_tick64`、`irq_count[]` 原子化）—— 两行级改动，
   但把"计数器在多核下会丢"这个最基础的问题先消掉；
3. **SB-09 PMM 加锁** —— **这条是开 AP 调度的硬门槛**，不做就是随机踩内存；
4. **SB-17 完整版**（per-CPU 内核栈槽 + `MSR_KERNEL_GS_BASE`）—— 多核系统
   调用才能不串栈；
5. **SB-21 IPI 闭环** —— 唤醒/刷新/停核的通道；
6. **才谈 SB-25**：先补 3.1 的 `old == NULL` 与 3.2 的 AP idle 循环，再把
   `smp.c:140` 的 `&& 0` 拿掉。

### 每步的可测退出准则

| 步骤 | 通过标准 |
|---|---|
| SB-01 | 主机并发计数桩：结束值 = 线程数 × 次数，CAS 成功/失败路径都覆盖 |
| SB-02/03 | QEMU `-smp 4` 冷启动 10 次，`g_kernel_tick64` 单调不减；`irq_count` 总和与派发次数对账一致 |
| SB-09 | QEMU `-smp 4` 并发 fork/exec/退出压测 10 分钟无 panic；不变量 `used+free+reserved ≈ total` 全程成立 |
| SB-17 | 多核并发 write/brk/mmap/fork，各核内核栈指针互不相同 |
| SB-21 | 发 RESCHEDULE IPI 后目标核进入处理并有应答计数；无丢失、无死锁 |
| 3.1 | 主机桩：`context_switch_64(NULL, &new)` 不写任何内存、能正常切到 new |
| 3.2 | AP 不再持有 `processes[0]` 的 context；两核并发 `schedule()` 后 idle 的 rsp 只被 BSP 写 |
| SB-25 | 双核各跑用户进程，`ps` 里能看到两个不同 CPU 上的进程；压测 30 分钟无 panic/XOR |

## 五、可回退边界

本轮已落地的三项都能独立 revert：

- `g_sched_lock`：删掉 acquire/release 与两处释放点即可回到无锁；
- `per_cpu_current_pid`：改回单值，三处消费点同步改回；
- `running_on`：删字段与三处判断/交接逻辑，结构体布局回到原样。

三者互不依赖，可分别回退。
