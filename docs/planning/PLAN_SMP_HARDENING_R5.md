---
title: 组合B：SMP 硬化包实施计划（PLAN_SMP_HARDENING_R5）
status: active
sys_version: 0.58.0
updated: 2026-11-13
owner: kernel
source_of_truth: null
supersedes: null
---

# 组合 B：SMP 硬化包实施计划

> **进度（2026-11-13）**：SB-23 与 SB-17 的「当前进程 PID」部分已落地，
> 另有计划外的 `running_on` 归属标记。**顺序偏离了本计划的阶段划分**（先做了
> 阶段 4 的 SB-23），偏离的安全前提是 `smp.c:140` 的 AP 调度开关仍关闭。
> **2026-11-13 阶段 2 完成**：SB-10（用户进程表）、SB-11（fd 表）、
> SB-12（TCP）、SB-13（UDP）、SB-14（ext2）五条表锁已落地，均为「表级粗锁 +
> irqsave / per-CPU 深度计数」，提交 6332e6d、7e0fe09、bd2c010、07bcec9。
> **SB-24（遗留栈归属标注）已完成**：`kernel/sched/smp.c` 与
> `kernel/include/smp.h` 文件头标注「未进入 64 位镜像、接口以 arch 版为准」，
> 并点明两套 `MAX_CPUS`（8 vs 16）的同名不同义风险；不改代码、不改行为，
> 镜像体积保持 878160 B。
>
> **SB-23（调度队列锁）已完成**：`g_sched_lock` 改静态初始化（原懒初始化
> 存在「B 核 init 把 A 核正持有的锁清零」的启动期竞态）；`add/remove_from_
> ready_queue` 拆 `_locked` + 公开包装，调度器内部改调 `_locked` 版（不依赖
> 同核递归，避开递归 unlock 提前 sti）；`process64_block/wakeup` 的「查状态→
> 改状态→出队」整段持锁；队列满不再静默丢弃，改打 `[SCHED] ready queue full`。
> AP 调度开关（`smp_timer_tick` 的 `&& 0`）按计划保持关闭。
>
> **SB-22（跨核 TLB shootdown）已完成**：新增 `vmm64_flush_tlb_others()`，
> 在 vmm64 六处页表修改点（大页映射、常规映射、unmap、mprotect、COW 拆页）
> 的本地 invlpg 之后接入，经 SB-21 的 `TLB_FLUSH` IPI 广播到在线核并汇聚应答；
> 单核与 `HEPU_HOST_TEST` 下退化为空实现。启动期实测 shootdown 次数 2 核
> 0x13FA / 4 核 0x3BEE / 8 核 0x8BD6，无异常。
>
> **SB-21（IPI 闭环）已完成**：0x50 从「空处理器 + 复用 tick 计数」改成真正
> 的消息通道 —— 每核一个 `{pending,type,arg}` 槽，`RESCHEDULE` / `TLB_FLUSH` /
> `STOP` 三类分发，处理完清 pending 并累加 per-CPU ack；槽忙时拒收并计入
> `g_ipi_dropped`（绝不覆盖）。新增 `smp_ipi_snapshot/wait_ack` 汇聚，启动期
> 自检给所有 AP 发一次整表 TLB_FLUSH 并等应答。2/4/8 核实测 ack 全数返回。
>
> **SB-20（AP 启动协议）已完成**：`ap_started_count` 改 `__sync_fetch_and_add`
> 原子递增（+ 每核到位位图），trampoline 参数按 CR3→RSP→entry 顺序发布并
> 在发 IPI 前 `mfence`，超时会点名缺失核 APIC ID，AP 栈底加 16KB 越界哨兵
> 并在屏障后复核。4 核 / 8 核冷启动实测 AP 全到位。
>
> SB-15（gfx）与 SB-16（窗口）随后完成：`gfx64_present` 改为显式目标参数
> （不再临时改全局 `gfx_target`），新增 `g_gfx_lock` 包 present/set_target*/
> set_mode、`g_ttf_lock` 包字形槽查找（单出口，光栅化在锁外）、`g_wm_lock`
> 包窗口槽扫描/z_order 重排。均为 SB-25 预埋，当前只有 BSP 桌面主循环一个
> 写者；锁只圈簿记与提交段，帧内容绘制不持锁。
>
> 对账、偏离论证、以及两条本计划未覆盖的 SB-25 阻塞
> （`context_switch_64` 不支持 `old == NULL`、AP 把 idle 当自己的当前进程且
> 没有调度循环）见 [SMP_PROGRESS_0_58_4.md](./SMP_PROGRESS_0_58_4.md)。

> 定位：把当前 x86_64 单体内核（C）从「AP 能上电、但临界区基本靠全局 cli/sti 与全局变量冒充 per-CPU」的状态，分阶段硬化到「多核并发下共享状态有明确同步原语、跨核 TLB/IPI 闭环、调度器可安全被多核进入」的状态。
>
> 本轮范围约束（硬性）：
>
> - 本文件是**计划**，本轮不改动任何 `.c/.h/Makefile`，不删除任何源码文件；
> - 版本号不 bump；本包不包含镜像两路径统一、USB 事件驱动重构（两项仍不做）；
> - `window64.c` 与 32 位遗留树的处置仅在附录以「处置建议（不含执行）」形式列出，需用户拍板，本轮不动；
> - 所有「现状」断言均附 `file:line` 证据，行号基于 HEAD `ef3c7cc`。

## 一、现状事实基线（源码核实）

### 1.1 AP 核启动路径

| 环节 | 证据 file:line | 现状说明 |
|---|---|---|
| 核信息表 | `kernel/arch/x86_64/smp.c:22-25` | `cpu_table[MAX_CPUS]`、`cpu_count`、`bsp_cpu_id`、`ap_started_count` 均为静态全局量；`MAX_CPUS=16`（`smp.h:21`） |
| AP 就绪屏障 | `kernel/arch/x86_64/smp.c:29` | `g_apic_idt_ready` 由 BSP 的 `irq64_init()` 结束时置 1，AP 在 C 入口自旋等待（`smp.c:102-104`） |
| Trampoline | `kernel/arch/x86_64/ap_trampoline.S:1-73` | 16 位实模式 → 32 位保护模式 → 64 位长模式；32 位段用 `ap_gdt`；CR3 取 `0x9000`、RSP 取 `0x9008`、入口取 `0x9010`；标记写入 `0x9100..0x9128` |
| AP C 入口 | `kernel/arch/x86_64/smp.c:69-124` | 顺序：FPU/SSE 使能（77）→ 读 APIC ID（80）→ 填 per-CPU 槽（83-87）→ `ap_started_count++`（91）→ 重载内核 GDT（99）→ 等 IDT 就绪（102）→ `idt64_load_for_ap`（108）→ `apic64_enable_for_ap`（112）→ `apic64_init_timer`（116）→ `sti`（120）→ `hlt` 死循环（121-123） |
| 每 AP 栈 | `kernel/arch/x86_64/smp.c:175-176` | `ap_stacks[MAX_CPUS][16384]` 静态数组，每核 16KB；栈顶经 `0x9008` 传给 trampoline。该栈只服务 idle/hlt 循环，**不是**系统调用/中断内核栈 |
| AP LAPIC 使能 | `kernel/arch/x86_64/apic64.c:489-503` | `apic64_enable_for_ap()`：TPR=0（492）、SVR 置使能位（495）、LVT 错误屏蔽（498）、清 ESR（501-502）。注释明确 SVR 软件使能位是 per-CPU，BSP 的 `apic64_init()` 不会替 AP 启用 |
| BSP LAPIC 初始化 | `kernel/arch/x86_64/apic64.c:200-248` | 映射 APIC/IOAPIC MMIO（207-210）、读 ID/版本（214-215）、TPR/SVR/LVT 配置（231-242）；`apic_mode=0` PIC 兼容模式（245-246） |
| SMP 初始化 | `kernel/arch/x86_64/smp.c:246-350` | ACPI MADT 优先（255-268），CPUID 后备（271-280）；trampoline 复制到 `0x8000`（291）；BSP 先起定时器（302）；遍历 ACPI 核调 `smp_start_single_ap`（312-317）；最后用 `ap_started_count` 自旋同步，带 5e8 次超时（325-343） |
| AP 调度现状 | `kernel/arch/x86_64/smp.c:134-143` | `smp_timer_tick()` 里 AP 参与调度的分支被 `&& 0` 禁用（140）：**AP 在线但不跑调度，所有进程仍由 BSP 运行** |
| IPI 向量 | `kernel/arch/x86_64/lib64.c:443-448` | 向量 `0x50` 分发到 `smp_ipi_handler()`；发送侧 `apic64_send_ipi / _all / _others` 在 `apic64.c:454-481` |

### 1.2 现有「锁」与原子手段

| 手段 | 证据 file:line | 现状与缺口 |
|---|---|---|
| arch 自旋锁 | `kernel/arch/x86_64/spinlock.c:13-115`；结构 `spinlock.h:12-16` | 有 `xchgl`（25）与 `lock cmpxchgl`（38）；`spinlock_lock` 的递归判断「锁值==1 且 owner==本核」是非原子读改写（52-55）；释放时 `lock->lock=0` 是普通写（87），未用原子释放；**无 irqsave/irqrestore 变体**；无内存屏障封装 |
| 堆锁（关中断 + 原子交换） | `kernel/arch/x86_64/kheap64.c:63-87` | `heap_lock_word`：保存 RFLAGS → `cli` → `__atomic_exchange_n` acquire 自旋（69-78）；释放 `__atomic_store_n` release 后按原 IF 决定是否 `sti`（80-86）。这是当前唯一较完整的「中断安全 + 多核」临界区 |
| 内核日志锁 | `kernel/arch/x86_64/klog64.c:12-23` | `klog_lock` 用 `__sync_lock_test_and_set / release`；`klog_buf/head/len`（9-11）在锁内更新 |
| 全局 cli 冒充锁 | `kernel/arch/x86_64/gfx64.c:557-593` | `gfx64_put_string()` 保存 RFLAGS 后 `cli`（559-560），整段字形栅格化与帧缓冲写入在关中断态完成，退出时按原 IF 恢复（590-591）。关中断只阻止本核中断，**不阻止其他核并发写同一缓冲** |
| PMM 无锁 | `kernel/arch/x86_64/pmm64.c:223-294` | `pmm64_alloc_page()`（223）、`pmm64_alloc_pages()`（297）、`pmm64_free_page()`（274）、`pmm64_reserve_range()`（260）全程无自旋锁、无关中断；位图按字节读改写 |
| 原子原语覆盖度 | `kernel/arch/x86_64/spinlock.c:21-44`；`kheap64.c:74,82`；`klog64.c:16,23` | 现存原子操作只有 32 位 `xchg/cmpxchg` 与编译器内建；**没有对外的 64 位原子加/减/CAS 类型与 helper**；`grep lfence/mfence/sfence/__sync_synchronize` 在 arch 目录无命中，仅有 `"memory"` 编译屏障 |
| 未编入构建的遗留 SMP 栈 | `kernel/sched/smp.c:1-315`；`kernel/include/smp.h:1-86` | 该目录不在 64 位 Makefile 的 `vpath`（`kernel/arch/x86_64/Makefile:154`），不进 64 位镜像。其中 `smp_cpu_id()` 恒返回 0（`kernel/sched/smp.c:46-52`），`eflags_save/restore` 用 `pushfl/popl` 32 位栈操作（175-185），`spinlock_t` 字段定义与 arch 版不一致（`include/smp.h:55-59` vs `spinlock.h:12-16`）。仅作归属风险记录，本轮不改 |

### 1.3 全局共享可变状态清单

| # | 共享状态 | 证据 file:line | 多核并发时的具体风险 |
|---|---|---|---|
| S1 | 全局 tick `g_kernel_tick64` | 定义 `lib64.c:349`；递增 `lib64.c:415` | 每个核的本地 APIC 定时器（向量 `0x40`）中断都会执行 `g_kernel_tick64++`；`uint64_t` 的读-改-写非原子，两核同时递增会丢 tick；下游 `start_tick`/运行时长（`user_process.c:1645,2505`）与栈种子（`user_stack64.c:239`）读数会抖动 |
| S2 | 中断计数 `irq_count[256]` | 定义 `irq64_simple.c:32`；递增 `247 / 254 / 336` | 多核中断并发 `irq_count[vector]++`，计数丢失；诊断/看门狗依据的中断频率失真 |
| S3 | 用户异常累计 `g_user_exception_count` | `lib64.c:351` | 多核异常路径并发递增，计数偏低；启动自检电池结论不可靠 |
| S4 | PMM 全局分配器 | 结构 `pmm64.c:55`；位图 `122`；分配 `223-254`；释放 `274-294` | 两核扫到同一空闲位、同时置位 → 同一物理页被分给两个进程/两个页表；`used/free/next_free` 计数错乱；`reserve_range` 与分配交叉可把已保留页再发出 |
| S5 | 内核堆空闲链表与统计 | `kheap64.c:38-59`；锁 `63-87` | 空闲链表已有锁保护；但 `kmalloc_calls/kfree_calls/kmalloc_total_bytes/kheap_peak/kmalloc_fail`（44-48）与 `kmem_trace`（57-59）的更新未全部走同一把锁，统计与 trace 槽可被并发覆盖；锁本身是单字自旋锁，无死锁检测/持有者记录 |
| S6 | 用户进程表 `user_procs[8]` | `user_process.c:80`；当前槽 `84`；建槽扫描 `278,288,618,628`；按 PID 查找 `844,903,1070,1190,1202,1425` | 两核同时 fork/exec 扫到同一空闲槽 → 双进程写同一 `user_process_t`；`active/pid/page_table/mem_pages` 交叉污染；`current_user_proc` 是全局单值，多核进入内核时互相覆盖 |
| S7 | 文件描述符表 `sys_files_tbl[9][MAX_FILES]` | `syscall64_full.c:227-234`；复位 `237-241` | 表按「当前进程槽位」索引，槽位本身由全局 `current_user_proc` 决定；同一进程的并发 open/close/read/write 可交错写同一 fd 项；复位（进程退出）与并发使用可撞车 |
| S8 | TCP 连接表 `tcp_conns[8]` | `net64_tcp.c:72`；ISN `74`；建槽扫描 `182-185`；收发 `205,221,230,258,309,406` | 两次 connect 扫到同一空闲槽；`state/used/isn/收发缓冲指针` 在中断（网卡收包）与系统调用（send/close）之间并发改写；ISN 递增非原子 |
| S9 | UDP socket 表 `udp_sockets[UDP_MAX_PORTS]` | `net64_udp.c:29`；建槽 `90-94` | bind/send/收包中断并发，端口槽位重复占用或 `used` 标记被清掉 |
| S10 | IP 标识计数器 `ip_id_counter` | `net64_ip.c:67` | 多核同时发包取 ID，同一 ID 重复；分片重组在对端可能误并 |
| S11 | 调度器就绪队列 | `process64_full.c:130-131`；入队 `224-232`；出队 `234-243`；调度 `415-465` | 多核进入 `add_to_ready_queue/remove_from_ready_queue/schedule` 时，队列数组与计数并发读写 → 进程重复入队、出队错位、`chosen` 槽位与队列内容不一致 |
| S12 | 图形帧缓冲与 TTF 缓存 | `gfx64.c:16,28,30,31,140-142,350-351`；cli 段 `557-593` | `back_buffer/front_buffer/gfx_target/ttf_cache/ttf_cache_next` 全局共享；除 `gfx64_put_string` 的 cli 段外，其余绘制路径无同步；两核画到同一像素行、TTF 缓存槽竞争 |
| S13 | 窗口表与焦点 | `window64.c:24-26`（`windows[16]/window_count/focus_window`）；桌面全局 `desktop64.c:48-50` | 鼠标/触摸事件核与绘制核并发改窗口链表、Z 序、焦点；`window_count` 增减非原子 |
| S14 | ext2 挂载/超级块/块缓存 | `ext2_64.c:50,51,54,59,66,73,128` | `ext2_sb/ext2_gdt/ext2_ctx/ext2_cache[64]/ext2_mount_offsets[4]` 全局；文件系统调用与后台 fsck/缓存回收并发，块缓存槽与位图/目录项可被交叉改写 |
| S15 | 冒充 per-CPU 的全局数组 | `smp.c:32-33`（`per_cpu_current_proc[MAX_CPUS]`、`per_cpu_timer_ticks[MAX_CPUS]`） | 以「APIC ID 下标」代替真正的 per-CPU 存储；无 GS 基址机制保护，下标越界/ID 重映射时直接踩邻槽；定时器计数在 `smp_timer_tick` 与 `smp_ipi_handler`（`smp.c:160`）间混写 |
| S16 | 系统调用内核栈槽 | `syscall64_simple.c:39,43-44,157-162`；`entry_64.S:85` | `percpu_data` 是单个静态结构；`g_syscall_kstack_slot/g_syscall_ustack_slot` 是全局单值；`MSR_KERNEL_GS_BASE` 只在 BSP 写过一次（159）。AP 上任何系统调用都会复用 BSP 的内核栈槽 |
| S17 | FPU 保存区 | `lib64.c:346`；使用 `607-624` | `fpu_save_area[8][512]`，下标由 APIC ID `& 7` 得到（613-615）；`MAX_CPUS=16`，ID ≥ 8 的核会与低编号核复用同一保存区；中断嵌套上下文未额外隔离 |
| S18 | LAPIC 全局状态 | `apic64.c:214-215`（`apic_id/apic_version`）；`apic_initialized/apic_mode`（244-246） | AP 路径读取这些全局量做判断，BSP 的初始化值被 AP 共享；AP 上的 LAPIC 状态没有独立存储 |

### 1.4 缺失的 SMP 基础设施

| 设施 | 现状证据 | 缺口 |
|---|---|---|
| TLB shootdown | 本地刷新仅 `vmm64.c:197,217,246,273,287,403,462` 的 `VMM_INVLPG`（宏定义 `46/48`） | 改页表后只刷新当前核 TLB；其他核的旧映射仍在，跨核 munmap/COW 拆页/权限回收后可能访问到已失效或已改归属的物理页；无「广播 IPI → 各核 invlpg → 汇聚应答」闭环 |
| IPI 投递与处理 | 发送 `apic64.c:454-481`；处理 `smp.c:154-162`；向量分发 `lib64.c:443-448` | `smp_ipi_handler()` 只复用 tick 计数（160），无消息表、无目标核/应答机制；除 INIT/STARTUP 外没有通用固定投递 IPI 的使用方；无 reschedule IPI、无 stop IPI |
| per-CPU 变量机制 | `syscall64_simple.c:32-39,157-159`；`entry_64.S:74,169,209,247` | `swapgs` 已在系统调用/异常入口出现，但 `MSR_KERNEL_GS_BASE` 未按核设置，无 `wrgsbase`/GS 段相对寻址的 per-CPU 区；现存「per-CPU」全靠全局数组按下标冒充（见 S15/S16） |
| 原子 CAS / 原子算术 | `spinlock.c:34-44` 仅 32 位 cmpxchg | 无 64 位 `atomic_t`、无 `atomic_inc/dec/add/sub/cas` 统一接口；无显式内存屏障（`lfence/mfence/sfence`）封装 |
| 每核 GDT/TSS/IST | 单 TSS `gdt64.c:59`；RSP0 `162,206`；IST1-3 `245-247`；AP 重载内核 GDT `smp.c:99` | 全核共享 BSP 的一份 TSS 与同一组 IST 栈；AP 没有自己的 TSS 描述符与 RSP0；从用户态切入内核（中断/系统调用）时栈指针来自 BSP 那份 TSS |
| LAPIC 每核状态（SVR/TPR/LVT） | BSP `apic64.c:231-242`；AP `489-503` | AP 只重写 TPR/SVR/LVT_ERROR；定时器 LVT 在 `apic64_init_timer` 配置；各核 TPR/中断优先级、LVT 屏蔽位没有独立状态表，调试与诊断无法按核区分 |
| 每核中断/内核栈 | AP 栈 `smp.c:175-176`；系统调用栈槽 `syscall64_simple.c:43` | AP 的 16KB 栈只用于 idle 循环；AP 进入中断/系统调用时没有独立内核栈，IST 栈也只有 BSP 一份 |

## 二、逐项改造清单

> 六要素：编号 / 位置（file:行）/ 现状风险 / 改法（具体到函数与代码结构）/ 回归测试点 / 预估行数（区间 + 估算依据）/ 所属阶段。
> 行数估算口径：以「新增静态锁 + 在现有函数出入口加 acquire/release + 必要的注释与诊断」为基准；不含重构数据结构布局、不换新调度器。

### 阶段 1：低风险高收益 —— 原子计数与输出串行化

| 编号 | 位置 file:行 | 现状风险（并发后果） | 改法（函数与代码结构） | 回归测试点 | 预估行数 | 阶段 |
|---|---|---|---|---|---|---|
| SB-01 | `kernel/arch/x86_64/spinlock.h`、`spinlock.c:21-44`；新增 `kernel/arch/x86_64/atomic64.h` | 无统一 64 位原子接口，各文件散落编译器内建，屏障口径不一致 | 在 `atomic64.h` 内联提供 `atomic_u32/atomic_u64` 包装与 `atomic_inc/dec/add/sub/load/store/cas`，底层用 `lock xadd` / `lock cmpxchg` 汇编；`mb()/rmb()/wmb()` 封装 `mfence/lfence/sfence`；arch 现有 `atomic_xchg/atomic_cmpxchg` 改调新接口，保持 ABI 兼容 | 主机测试编译通过；新增主机桩：多线程并发 inc 同一计数，结束值等于线程数×次数；CAS 成功/失败路径各跑一遍 | 40–70 行（头文件 ~25 行内联声明 + .c ~20 行汇编封装；依据：每条原子算术 3–5 行汇编） | 1 |
| SB-02 | `kernel/arch/x86_64/lib64.c:349,415` | 多核 `0x40` 中断并发 `g_kernel_tick64++` 丢 tick，下游时长/种子读数抖动 | `g_kernel_tick64` 改 `volatile uint64_t` + `atomic64_add(…,1)`（SB-01 接口）；读取处（`user_process.c:1645,2505`、`user_stack64.c:239`）走 `atomic_load`；不改变 tick 语义 | QEMU `-smp 2/4` 启动后 `smp_get_cpu_ticks` 与全局 tick 单调不减；运行用户进程一段时间，时长统计不为 0、不回绕异常 | 2–4 行（仅替换递增/读取表达式） | 1 |
| SB-03 | `kernel/arch/x86_64/irq64_simple.c:32,247,254,336` | 多核中断并发递增 `irq_count[vector]` 丢计数，诊断频率失真 | 三处 `irq_count[vector]++` 改 `atomic_inc`（SB-01）；`irq_count` 类型保持 `uint64_t`，结构不动；新增按向量读取的 `irq64_count_of(vector)` 供诊断，内部 `atomic_load` | 主机桩不覆盖中断；QEMU 多核下跑定时器/网卡中断，`irq_count` 总和与中断派发次数对账一致；无计数倒退 | 6–10 行（三处调用点替换 + 一个读取函数） | 1 |
| SB-04 | `kernel/arch/x86_64/lib64.c:351` | 多核异常并发递增，自检电池计数偏低 | `g_user_exception_count` 递增点改 `atomic_inc`，读取点 `atomic_load`；类型与对外符号不变 | 主机桩模拟两次异常路径并发调用计数；QEMU 下触发已知异常，计数与 dump 条数一致 | 3–5 行 | 1 |
| SB-05 | `kernel/arch/x86_64/lib64.c:19-20,108,202-210` | `vga_x/vga_y`、串口缓冲、输出钩子在多核打印时交错覆盖，光标错位、日志串行撕裂 | 新增静态 `print_lock`（irqsave 自旋锁，复用 SB-01/arch 锁结构）；`vga_puts()` 入口 acquire、出口（含 `\n`/滚动/钩子分支）release；`serial_puts_local()` 不再单独加锁，由上层临界区覆盖；锁内保存 IF，释放时按原 IF 恢复；临界区只做字节输出，不调可能再获取锁的路径 | 主机桩：多线程并发 `vga_puts/serial_puts` 风格字符串，输出无半行交错；QEMU 多核启动日志完整，无字符覆盖；单线程主机 6 项测试不回归 | 25–45 行（锁定义 + acquire/release 包裹 + IF 保存恢复；依据：`vga_puts` 现有分支 202-210 逐处收口） | 1 |
| SB-06 | `kernel/arch/x86_64/kheap64.c:44-48,57-59` | 堆统计计数与 trace 槽未全部走 `heap_lock`，并发下统计/trace 错乱 | 核对 `kmalloc/kfree` 路径：所有统计字段在 `heap_lock_acquire` 临界区内更新；`kmem_trace` 写槽加同一把锁或改为无锁环形（先选加锁，改动小）；锁释放路径复核 `heap_lock_word` 只由持有者清 0 | 现有主机 heap 测试（`heap_test.c`，ASan/UBSan）通过；新增主机桩：并发 malloc/free，`heap_used` 平衡、无重复 trace 槽覆盖 | 10–20 行（统计语句收口，不改块头布局） | 1 |
| SB-07 | `kernel/arch/x86_64/net64_ip.c:67` | 多核并发发包取 IP ID 重复，分片误并 | `ip_id_counter` 改 `atomic_inc`（回绕语义保持）；发包取 ID 处一条原子自增 | 主机桩：并发取 ID，窗口内无重复；QEMU 多核 ping/发包不出现分片重组报错 | 8–15 行 | 1 |
| SB-08 | `kernel/arch/x86_64/lib64.c:346,607-624` | 保存区只有 8 槽，ID ≥ 8 的核复用低编号核 FPU 状态；下标 `& 7` 与 `MAX_CPUS=16` 不匹配 | 先核对：保存区槽位数扩到 `MAX_CPUS`，下标去掉 `& 7` 改为以合法核 ID 索引；fxsave/fxrstor 仍在中断临界区内；补一条断言：核 ID `< MAX_CPUS`，越界则报错停机而非踩邻槽 | QEMU `-smp` 核数 ≤ 8 现状不回归；核数 > 8（如 `-smp 12`）启动无 FPU 状态损坏、无浮点异常；中断压测后用户态浮点结果正确 | 15–25 行（槽位定义 + 下标修正 + 断言） | 1 |

### 阶段 2：分配器与内核对象表 —— 粗粒度加锁

| 编号 | 位置 file:行 | 现状风险（并发后果） | 改法（函数与代码结构） | 回归测试点 | 预估行数 | 阶段 |
|---|---|---|---|---|---|---|
| SB-09 | `kernel/arch/x86_64/pmm64.c:55,122,223-254,260-271,274-294,297-317` | 无锁分配/释放 → 同一物理页双发、计数错乱、保留页被再分配 | 新增静态 `pmm_lock`（irqsave 自旋锁）；`alloc_page/alloc_pages/free_page/reserve_range` 入口加锁、出口释放；位图扫描与 `used/free/next_free/alloc_count/free_count` 更新整体在临界区内；锁内不调用 `kmalloc`（避免锁序反转）；预留调试钩子：锁内断言位图位与计数一致 | 主机 aspace/heap 测试不回归；QEMU 多核并发 fork/exec + 退出回收，跑压测 10 分钟无 panic；不变量：`used+free+reserved ≈ total`，无重复物理页分配记录 | 30–50 行（四函数包裹 + 锁定义 + 不变量断言；依据：alloc/free 现有各 ~20 行，加锁不改变扫描逻辑） | 2 |
| SB-10 | `kernel/arch/x86_64/user_process.c:80,84,278,288,618,628,844,903,1070,1190,1202,1425` | 双进程抢同一槽；`current_user_proc` 全局互相覆盖；退出复位与并发使用撞车 | 新增 `proc_table_lock`（irqsave）；槽位扫描/占用/释放/按 PID 查找路径在锁内完成「判断 + 写槽」；`current_user_proc` 的切换点（如 `1642`）与读取点成对收口；先表级粗锁，不做每槽锁；锁内记录持有者 PID 便于诊断 | 主机 elf/aspace/winslot 测试不回归；QEMU 多核并发 fork 多个用户进程再退出，槽位无重复占用、无悬空 `page_table`；`current_user_proc` 在系统调用进入/返回期间不被其他核改写 | 35–60 行（查表与建槽函数包裹 + 锁定义；依据：现有 8 个查找/占用点逐一收口） | 2 |
| SB-11 | `kernel/arch/x86_64/syscall64_full.c:227-241` | 同进程并发 fd 操作交错；退出复位与使用撞车 | 新增 `fd_table_lock`（表级，irqsave）；open/close/read/write/lseek 对 `sys_files` 的访问在锁内；`syscall_fd_table_reset()` 与使用路径互斥；`fd_table_index()` 只做槽位解析，不持锁；先不做每 fd 一把锁 | 主机 ext2/elf 测试不回归；QEMU 多核并发打开/读写/关闭文件与管道，fd 不串槽、读写缓冲不撕裂；退出后复位槽位无残留项 | 25–40 行 | 2 |
| SB-12 | `kernel/arch/x86_64/net64_tcp.c:72,74,169,182-185,205,221,230,258,309,406` | connect/收包中断/close 并发写同一连接槽；ISN 重复；状态机跳变竞争 | 新增 `tcp_lock`（irqsave；网卡收包是中断上下文，必须可在中断态获取）；建槽扫描、状态迁移、收发缓冲指针更新、close 释放整体在锁内；`tcp_isn` 改原子递增；锁粒度先全表一把；标注后续可按连接细分 | QEMU 多核并发建立 TCP 连接、发包、主动关闭；无重复槽、无使用野指针；连接状态迁移日志一致；压测 5 分钟无断言失败 | 30–50 行 | 2 |
| SB-13 | `kernel/arch/x86_64/net64_udp.c:29,78,90-94` | bind/收包/发送并发，端口槽重复占用或 `used` 被清 | 新增 `udp_lock`（irqsave）；bind 建槽、send、收包分发路径在锁内；`udp_sockets[i].used` 判断与置位在同一临界区 | QEMU 多核并发 bind/send/recv，端口无重复占用；收包不写到已关闭槽位 | 20–35 行 | 2 |
| SB-14 | `kernel/arch/x86_64/ext2_64.c:50,51,54,59,66,73,82-168,491` | 文件系统调用与缓存回收并发，块缓存/位图/目录项交叉改写 | 新增 `fs_lock`（表级 irqsave）；mount、块缓存查槽/填槽、目录项查找、写回路径在锁内；`ext2_cache` 槽位分配与标记在同一临界区；先粗锁，不引入缓冲树细粒度锁 | 主机 ext2 测试（含夹具）通过；QEMU 多核并发读写文件、创建/删除目录，缓存槽无双重占用；文件内容一致、无目录项错乱 | 40–70 行（缓存查填与目录路径包裹；依据：缓存现有 64 槽扫描逻辑保持不变） | 2 |
| SB-15 | `kernel/arch/x86_64/gfx64.c:16,28,30,31,140-142,350-351,557-593` | 绘制路径并发写帧缓冲与 TTF 缓存；现有 cli 段只防本核中断 | 新增 `gfx_lock`（irqsave）；把 `gfx64_put_string` 的 cli 段替换为锁临界区（保留 IF 保存/恢复语义）；`ttf_cache_next` 槽位分配在锁内原子推进；帧缓冲指针/目标缓冲切换在锁内读取；临界区不做长耗时计算 | 主机 winslot 测试不回归；QEMU 多核下桌面/终端并发绘制，像素无半行交错、TTF 缓存无撕裂；单线程启动路径不回归 | 30–50 行 | 2 |
| SB-16 | `kernel/arch/x86_64/window64.c:24-26`；`desktop64.c:48-50` | 事件处理与绘制并发改窗口链表/Z 序/焦点 | 新增 `window_lock`（irqsave）；窗口创建/移动/缩放/关闭/焦点切换/遍历绘制路径在锁内；`window_count` 增减在临界区；屏幕尺寸等只读全局在初始化后不再写，不加锁 | 主机 winslot 测试通过；QEMU 多核并发鼠标/触摸事件 + 窗口重绘，焦点与 Z 序一致，无野窗口指针 | 25–40 行 | 2 |

### 阶段 3：结构性 per-CPU 与上下文隔离

| 编号 | 位置 file:行 | 现状风险（并发后果） | 改法（函数与代码结构） | 回归测试点 | 预估行数 | 阶段 |
|---|---|---|---|---|---|---|
| SB-17 | `kernel/arch/x86_64/syscall64_simple.c:32-39,43-44,157-162`；`entry_64.S:74,85,169,209,247`；`smp.c:32-33` | 单份 percpu 结构与全局栈槽；AP 系统调用复用 BSP 栈；无 GS 基址 per-CPU 区 | 定义 `percpu_data_t percpu[ MAX_CPUS ]`（内核栈、用户栈、当前核编号、当前进程指针等）；BSP 初始化填 0 号槽；AP 在 `ap_c_entry` 早期把本核槽地址写入 `MSR_KERNEL_GS_BASE`（`0xC0000102`，必要时配合 `wrgsbase`）；`entry_64.S` 的 `swapgs` 后改 GS 相对寻址取本核槽，淘汰 `g_syscall_kstack_slot/g_syscall_ustack_slot` 全局单值（保留过渡别名一个版本周期）；`smp.c` 的 `per_cpu_current_proc/per_cpu_timer_ticks` 迁入 per-CPU 区 | QEMU `-smp 2/4` 下多核并发系统调用（write/brk/mmap/fork）栈不互相踩踏；无 `#GP/#SS/#DF`；每核栈指针互不相同；单核心启动路径不回归 | 120–180 行（per-CPU 结构定义 + AP MSR 编程 + 入口汇编取槽改造 + 栈槽迁移；依据：汇编入口 4 处 swapgs 点逐处适配，C 侧槽位初始化 ~30 行） | 3 |
| SB-18 | `kernel/arch/x86_64/gdt64.c:59,162,204-212,217-247`；`smp.c:99` | 全核共享一份 TSS/RSP0/IST；AP 中断切入内核无独立栈 | 扩 `tss64_t tss[MAX_CPUS]` 与每核 IST 栈数组（DF/NMI/MC 各一槽）；GDT 中为每核预留 TSS 描述符（或每核一份 GDT 副本，选改动小的方案）；AP 在 `ap_c_entry` 重载 GDT 后加载自身 TSS，填本核 RSP0 与 IST；BSP 路径保持现有单 TSS 行为兼容 | QEMU 多核下用户态中断/系统调用切入内核使用本核 RSP0；构造双重故障场景走 IST1 不踩当前栈；`-smp 4` 反复压力切换无栈溢出/串栈 | 60–100 行（TSS 数组 + 描述符表 + AP 加载流程；依据：现有单 TSS 初始化 ~30 行，数组化与描述符复制翻倍） | 3 |
| SB-19 | `kernel/arch/x86_64/apic64.c:214-215,244-246,489-503`；`smp.c:80` | LAPIC ID/模式/初始化标志被 AP 共享；诊断无法按核区分 | 把 `apic_id/apic_version/apic_initialized` 改为 per-CPU 存储（SB-17 的 per-CPU 区或独立数组）；BSP 初始化值只进 0 号槽；AP 读取本核槽；`apic64_enable_for_ap()` 只写本核 TPR/SVR/LVT，不回写全局模式标志；补每核 LVT/TPR 状态读取诊断接口 | QEMU 多核启动日志中各核 APIC ID 正确；AP 上触发 spurious/错误中断不改变 BSP 全局模式判断；无越界读邻核状态 | 25–40 行 | 3 |
| SB-20 | `kernel/arch/x86_64/smp.c:83-120,175-188,312-317,325-343`；`ap_trampoline.S:38-52` | AP 启动参数（CR3/RSP/入口）在低内存固定地址并发写入；`ap_started_count++` 非原子；初始化顺序靠注释约束，超时诊断不足 | `ap_started_count` 改原子递增；trampoline 参数写入加显式内存屏障，按 CR3→RSP→entry 顺序发布，最后再发 STARTUP IPI；固化 AP 初始化顺序清单（FPU → GDT → IDT → LAPIC enable → timer → sti）并在每步打核 ID 标记；同步屏障记录 expected/started 与超时核 ID；AP 栈顶加栈哨兵字，越界可诊断；保留现有固定地址协议不动 | QEMU `-smp 2/4/8` 反复冷启动 20 次，AP 全部到位或明确报超时核 ID；无双核共享同一栈；启动期无首帧面板丢失回归 | 40–70 行（原子计数 + 屏障 + 顺序诊断 + 栈哨兵；依据：`smp_start_single_ap` 参数段 180-188 收口） | 3 |

### 阶段 4：IPI 闭环与调度器并发

| 编号 | 位置 file:行 | 现状风险（并发后果） | 改法（函数与代码结构） | 回归测试点 | 预估行数 | 阶段 |
|---|---|---|---|---|---|---|
| SB-21 | `kernel/arch/x86_64/smp.c:154-162`；`apic64.c:454-481`；`lib64.c:443-448` | IPI 只有空处理；无消息、无应答、无汇聚；跨核协同（唤醒/刷新/停核）无通道 | 定义 IPI 消息类型：`RESCHEDULE`、`TLB_FLUSH`、`STOP`；每核 IPI pending 位图 + 消息槽（per-CPU，SB-17）；`smp_send_ipi(target, msg)` 写目标核槽并经 `apic64_send_ipi` 投递；`0x50` 处理程序按消息分发，处理完清 pending 并写应答计数；`smp_ipi_wait_ack(targets, mask)` 自旋汇聚；区分「广播/点对点/其他核」三种投递复用现有 `_all/_others` | QEMU 多核：发送 RESCHEDULE IPI 后目标核进入处理；TLB_FLUSH IPI 有应答计数；STOP IPI 可停住指定核；无丢失消息、无应答死锁；中断上下文可安全调用 | 70–110 行（消息结构 + 发送/分发/汇聚 + 三类处理 stub；依据：现有空 handler ~10 行，框架化后翻倍） | 4 |
| SB-22 | `kernel/arch/x86_64/vmm64.c:46,48,197,217,246,273,287,403,462` | 页表修改后只刷新本核 TLB，其他核旧映射残留 | 新增 `flush_tlb_others(vaddr, page_count, target_mask)`：在页表修改路径（unmap/COW 拆页/权限回收/CR3 相关）调用；收集在线核位图 → 经 SB-21 发 `TLB_FLUSH` IPI → 各核对指定页 `invlpg`（或全局刷新）→ 汇聚应答后返回；本地 `VMM_INVLPG` 保留；主机测试桩下 IPI 为空实现，保持可编译 | 主机 aspace 测试不回归；QEMU 多核：核 A 解除映射后核 B 再访问该地址应触发缺页/错误映射，而非命中旧 TLB；压测 mmap/munmap/COW 无残留访问 | 50–80 行（位图收集 + 调用点接入 + 各核刷新分发；依据：现有 7 处本地刷新点逐一评估是否需要跨核） | 4 |
| SB-23 | `kernel/arch/x86_64/process64_full.c:130-131,213,224-243,342,415-465` | 就绪队列数组/计数/选中槽位并发读写 → 重复入队、出队错位 | 新增 `rq_lock`（irqsave）；`add_to_ready_queue/remove_from_ready_queue/process64_schedule` 的队列判断、读写、计数更新整体在锁内；当前运行进程指针走 per-CPU（依赖 SB-17）；先保证「多核进入队列不破坏结构」，AP 参与调度仍由独立开关控制（对应 `smp.c:140` 的禁用位）；锁内不做阻塞式等待 | 主机 elf/aspace 测试不回归；QEMU 多核下压测进程创建/退出/时间片轮转，队列无重复条目、无空指针选中；BSP 单核心调度行为不回归 | 50–80 行（三个队列函数包裹 + 锁定义 + 当前进程指针 per-CPU 接入；依据：schedule 现有 ~50 行选队逻辑保持不变） | 4 |

### 阶段 5：独立评估项（不在本包承诺内）

> **包状态（2026-11-13）**：SB-24 已完成归属标注；SB-25 按本计划约定
> 「不在本包实现」，单独立项评估。至此 SB-01..SB-24 全部落地。

| 编号 | 位置 file:行 | 现状风险（并发后果） | 改法（函数与代码结构） | 回归测试点 | 预估行数 | 阶段 |
|---|---|---|---|---|---|---|
| SB-24 | `kernel/sched/smp.c:1-315`；`kernel/include/smp.h:1-86` | 未编入构建的遗留 SMP 栈与 arch 版锁结构/接口同名不同义，后续维护易误接 | 仅做归属标注：在两份文件头注释标明「未进入 64 位镜像，接口以 `kernel/arch/x86_64/smp.h`、`spinlock.h` 为准」；不删代码、不改行为；是否清理留待独立立项 | 64 位镜像构建产物与本轮一致；`make -C kernel/arch/x86_64` 无新告警 | 0–10 行（注释） | 5 |
| SB-25 | `kernel/arch/x86_64/smp.c:134-143` | AP 调度被 `&& 0` 禁用；多核算力闲置 | 待 SB-17/21/23 全部落地后，单独评估放开 AP 调度与负载均衡：每核运行队列、唤醒 IPI、迁移策略；本包不实现 | 单独验收：多核进程分布到多核运行；负载均衡无进程丢失/重复运行 | 本包不估行，单列评估 | 5 |

## 三、分阶段落地顺序

### 阶段 1：原子计数与输出串行化（低风险高收益）

- **目标**：消除最容易观察到的计数丢失与日志撕裂；先把原子原语与屏障接口立起来，供后续阶段复用。
- **包含条目**：SB-01、SB-02、SB-03、SB-04、SB-05、SB-06、SB-07、SB-08。
- **退出准则**（可测）：
  1. `bash tools/host-tests/run_host_tests.sh` 仍为 6/6 通过；
  2. QEMU `-smp 4` 冷启动 10 次，`g_kernel_tick64` 单调不减，日志无半行交错；
  3. 主机桩并发计数压测结束值与理论值一致（无丢失）；
  4. 无新增 `#GP/#DF/triple fault` 记录。
- **预估总行数**：109–194 行；**风险等级**：低（改动集中在计数表达式与输出临界区，不动数据结构布局与调度路径）。

### 阶段 2：分配器与内核对象表粗粒度加锁

- **目标**：PMM、堆统计、进程表、fd 表、TCP/UDP/IP、ext2、图形与窗口表在多核下有明确互斥；先表级粗锁，保证结构不被并发破坏。
- **包含条目**：SB-09、SB-10、SB-11、SB-12、SB-13、SB-14、SB-15、SB-16。
- **退出准则**（可测）：
  1. 主机 6 项测试通过；ext2 夹具读写一致；
  2. QEMU `-smp 4` 下并发 fork/exec/退出、并发文件读写、TCP/UDP 收发各压测 10 分钟无 panic、无重复槽位分配；
  3. PMM 不变量（`used+free+reserved ≈ total`）全程成立；堆 `heap_used` 无单边泄漏；
  4. 锁竞争统计（现有锁监控接口可复用）无持锁超时告警。
- **预估总行数**：235–395 行；**风险等级**：中（临界区覆盖面广，需排查锁序与中断上下文加锁合法性，但不改变数据结构语义）。

### 阶段 3：结构性 per-CPU 与上下文隔离

- **目标**：建立真正的 per-CPU 存储、每核 TSS/RSP0/IST、每核 LAPIC 状态，固化 AP 启动顺序与栈隔离。
- **包含条目**：SB-17、SB-18、SB-19、SB-20。
- **退出准则**（可测）：
  1. 主机测试通过；
  2. QEMU `-smp 2/4/8` 各冷启动 20 次，AP 全部到位或明确报出超时核 ID；
  3. 多核并发系统调用期间各核内核栈指针互不相同；IST 双重故障栈不被踩踏；
  4. `MAX_CPUS` 范围内无保存区/状态数组越界复用。
- **预估总行数**：245–390 行；**风险等级**：高（涉及入口汇编、MSR/GDT/TSS 改造，是本包最容易引入启动级故障的阶段；每一步需可独立回退到全局数组 + BSP 单栈）。

### 阶段 4：IPI 闭环与调度器并发

- **目标**：打通跨核 IPI 消息/应答通道，落地 TLB shootdown，就绪队列可被多核安全进入。
- **包含条目**：SB-21、SB-22、SB-23。
- **退出准则**（可测）：
  1. 主机测试通过；
  2. IPI 发送后目标核处理并有应答计数，无消息丢失；
  3. 核 A 解除映射后核 B 不命中旧 TLB；mmap/munmap/COW 压测无残留访问；
  4. 就绪队列压测无重复条目、无空指针选中；BSP 单核心调度行为与改造前一致。
- **预估总行数**：170–270 行；**风险等级**：高（IPI 死锁/应答汇聚超时、TLB 残留访问都是难复现问题；需先在 QEMU 多核稳定后再上真机）。

### 阶段 5：独立评估（本包不实施）

- **目标**：遗留 SMP 栈归属标注；AP 参与调度与负载均衡单独立项。
- **包含条目**：SB-24、SB-25。
- **退出准则**：不适用（评估项）。
- **预估总行数**：SB-24 0–10 行；SB-25 本包不估行；**风险等级**：待定。

## 四、回归测试方案

### 4.1 可用现有主机测试覆盖的部分

运行命令：`bash tools/host-tests/run_host_tests.sh`，当前基线 6/6：`uaccess / aspace / heap / elf / winslot / ext2`。

| 条目 | 主机可覆盖内容 | 说明 |
|---|---|---|
| SB-01 原子原语 | 新增主机桩：多线程（pthread）并发 inc/dec/CAS，校验结束值与 CAS 路径 | 主机无 APIC/中断，用线程模拟并发竞争；`KHEAP_HOST_TEST` 同款桩编译方式 |
| SB-06 堆统计 | 现有 `heap_test.c`（ASan/UBSan）并发 malloc/free 路径 | 统计/trace 一致性在主机桩加断言 |
| SB-09 PMM | `aspace_test.c` 间接覆盖页表/页分配路径 | 主机桩无真实位图锁，锁逻辑需 QEMU 复核 |
| SB-14 ext2 | 现有 `ext2_test.c` + 现场生成夹具 | 缓存槽并发需主机多线程桩补充 |
| SB-15/SB-16 图形/窗口 | 现有 `winslot_test.c` 槽位分配/回收不变量 | 帧缓冲并发绘制只能 QEMU/真机观察 |
| SB-22 TLB 刷新 | 主机桩下 IPI 为空实现，保证可编译 | 跨核语义必须 QEMU 多核验证 |

### 4.2 必须 QEMU 多核验证的部分

QEMU 二进制：`tools/bin/qemu-system-x86_64`（7.2.0，未编 user 网络后端）；多核用 `-smp N` 启动。

| 验证项 | 场景与观察不变量 |
|---|---|
| AP 启动（SB-20） | `-smp 2/4/8` 反复冷启动；观察 `[SMP] All N APs ready` 或明确超时核 ID；每核 APIC ID 正确；无双核共享栈 |
| 计数一致性（SB-02/03/04/07） | 多核跑定时器 + 网卡中断；`g_kernel_tick64` 单调不减；`irq_count` 总和与中断派发次数对账；IP ID 窗口内无重复 |
| 输出串行化（SB-05） | 多核并发打印启动日志/终端输出；无字符覆盖、无半行交错、光标不错位 |
| PMM/堆并发（SB-06/09） | 多核并发 fork/exec/退出 + 大量 brk/mmap；`used+free+reserved ≈ total`；无重复物理页；堆无泄漏、无野指针 |
| 进程/fd/网络表（SB-10/11/12/13） | 并发创建进程、并发 open/read/write/close、并发 TCP connect/send/close、UDP bind/send；无重复槽、fd 不串槽、无野连接指针 |
| 文件系统（SB-14） | 并发创建/读写/删除文件与目录；缓存槽无双重占用；目录项与文件内容一致 |
| per-CPU/栈/TSS（SB-17/18/19） | 多核并发系统调用；各核 RSP0/内核栈互不相同；FPU 保存区不串核；用户态切入内核无 `#SS/#DF` |
| IPI 与 shootdown（SB-21/22） | 发 RESCHEDULE/TLB_FLUSH/STOP IPI，校验应答计数；核 A 改映射后核 B 不命中旧 TLB |
| 调度队列（SB-23） | 压测进程创建/退出/时间片轮转；队列无重复条目、无空指针选中；单核心行为不回归 |

**压测思路（不变量观察清单）**：

1. 并发 fork/exec 多个用户进程，再批量退出：观察槽位、物理页、fd、连接表的分配/回收平衡；
2. 并发写文件（创建 + 追加 + 删除）：观察 ext2 缓存与目录项一致性；
3. 并发 TCP/UDP 发包与连接建立/关闭：观察状态机与槽位；
4. 全程记录：tick 单调不减、计数无倒退、无重复分配、无野指针解引用、无 `#GP/#DF/#SS/triple fault`。

### 4.3 只能真机验证的部分

- LAPIC 定时器校准与跨核时序（QEMU 总线频率与真机不同）；
- IST 双重故障/NMI/机器检查栈在真实异常下的切换；
- 跨核缓存一致性边角（ store buffer / 伪共享导致的偶发撕裂）；
- ACPI MADT 真实拓扑与 APIC ID 映射；
- 长稳压测（小时级）下的锁竞争与内存碎片化表现。

## 五、风险与不做的理由（含可独立 revert 边界）

| 项 | 本轮处置 | 不做/缓做的理由 | 可独立 revert 的边界 |
|---|---|---|---|
| 真正 per-CPU 机制（SB-17） | 阶段 3 实施 | 需要 GS 基址/MSR 编程、入口汇编取槽改造与每核栈配套，工作量与启动风险都高；必须先有阶段 1 的原子接口与阶段 2 的表级锁打底 | 回退到全局数组按下标冒充 per-CPU + BSP 单内核栈；`MSR_KERNEL_GS_BASE` 与全局栈槽别名保留一个版本周期，便于摘除此阶段改动 |
| 每核 TSS/IST（SB-18） | 阶段 3 实施 | 依赖 SB-17 的每核栈与核编号；GDT 描述符改造影响全部中断/系统调用切入路径 | 回退到单 TSS + 单组 IST 栈；AP 继续复用 BSP TSS（现状行为） |
| TLB shootdown（SB-22） | 阶段 4 实施 | 必须先有可靠 IPI 通道（SB-21）；在 IPI 应答汇聚未验证前上 shootdown，残留 TLB 访问更难定位 | 回退到仅本地 `invlpg`；页表修改路径不跨核刷新（现状行为） |
| 调度器并发与 AP 调度（SB-23/SB-25） | SB-23 阶段 4 做队列安全；SB-25 阶段 5 独立评估 | 多核调度涉及运行队列划分、迁移策略、唤醒时机，属调度器重构；本包先保证队列结构不被破坏，不放开 AP 跑调度 | SB-23 回退到单核进入队列（BSP 调度现状）；SB-25 不实施即保持 `smp.c:140` 的禁用位 |
| 锁粒度细化（每槽/每连接锁） | 本包只做表级粗锁 | 细粒度锁需要先摸清锁序与中断上下文约束；一次做粗锁可快速消除数据破坏，后续再按热点优化 | 每张表的锁可单独摘除回到无锁现状；粗锁实现不改变表结构，revert 不涉及数据迁移 |
| 镜像两路径统一 | 明确不做 | 属启动镜像/引导链治理，与 SMP 硬化无依赖；并行单元已有相关规划，本包不混入 | 不适用（本包无改动） |
| USB 事件驱动重构 | 明确不做 | 属驱动模型重构；USB 中断路径的并发问题在 SB-12/13 表级锁覆盖范围内先止血，重构单列 | 不适用（本包无改动） |
| 遗留 32 位树与重复 SMP 栈代码删除 | 不做（见附录） | 删除涉及传统 `-m32` 构建目标与历史兼容，需用户拍板；本包仅做归属标注（SB-24） | SB-24 仅注释，删除注释即恢复原状 |

## 六、附录：处置建议（不含执行）

> 以下两项只记录现状与选项，**需用户拍板，本轮不动代码、不删除文件、不改构建**。

### 6.1 `window64.c`

- **现状**：`kernel/arch/x86_64/window64.c`，267 行；已编入 64 位构建（`kernel/arch/x86_64/Makefile:112`）。
- **全局状态**：`windows[MAX_WINDOWS]`、`window_count`、`focus_window`（`window64.c:24-26`）；窗口结构含坐标、尺寸、标题、Z 序、焦点与绘制回调。
- **与本包关系**：其并发保护由 SB-16（`window_lock`）覆盖；是否保留该文件本身不在本包决策范围。

| 选项 | 内容 | 风险 | 回退方式 |
|---|---|---|---|
| 保留缩减 | 保留文件，仅清理未使用的字段/回调，并发访问纳入 SB-16 锁保护 | 低；需核对桌面/任务栏对窗口字段的实际引用 | 从版本控制恢复整文件即可 |
| 归档 | 移入归档目录，64 位构建移除对应 Makefile 行；若有其他模块引用窗口表，需先解耦 | 中；可能影响桌面外壳/任务管理器的窗口枚举功能 | 恢复文件与 Makefile 行 |
| 隔离出 CI | 保留源码但不编入默认镜像，单独构建目标验证 | 低；但会留下「源码在编/不在编」双状态，需文档注明 | 恢复 Makefile 编入行 |

### 6.2 32 位遗留树（`kernel/arch/x86/`）

- **现状**：`kernel/arch/x86/` 下 120 个 `.c`，合计约 45,564 行；由根 `Makefile` 的传统 `-m32` 构建目标使用（`CFLAGS` `Makefile:17`、`ASFLAGS` `28`、汇编源码 `36-40`）；不在 64 位 Makefile 的 `vpath`（`kernel/arch/x86_64/Makefile:154`），不进入 64 位镜像。
- **历史备注**：根 `Makefile:15-16` 记录 32 位源码为传统 K&R 风格，CI 编译器版本下会告警/报错，后续补头文件单列事项。

| 选项 | 内容 | 风险 | 回退方式 |
|---|---|---|---|
| 保留缩减 | 维持现有双构建目标；逐步给 32 位源码补头文件、收紧风格 | 低；工作量分散，不影响 64 位镜像 | 不适用（现状保留） |
| 归档 | 整树移到归档目录，冻结 `-m32` 目标；确认无文档/脚本仍引用后再移除 | 中；若仍有 32 位兼容演示或老机型需求，会失去构建路径 | 从归档恢复目录与根 Makefile 目标 |
| 隔离出 CI | 源码保留，但 CI 默认只编 64 位目标，32 位目标转为手动可选 | 低；需要在 CI 脚本与文档中明确两套构建边界 | 恢复 CI 默认包含 32 位目标 |

### 6.3 拍板事项

1. `window64.c` 选保留缩减 / 归档 / 隔离出 CI 哪一项；
2. 32 位遗留树是否允许在后续版本冻结或归档；
3. 上述任一项确认前，本包计划与实施均不触及这两处。

## 七、SB-25 解锁前置清单（AUDIT_R3 补充，2026-11-14）

> 来源：`docs/security/AUDIT_R3.md` 的 R3-3 / R3-5 / R3-7 与同轮审计中顺带确认的几条。
> 这一节回答一个之前没写清的问题：**打开 AP 调度之前，还有什么东西会立刻崩。**

打开 `smp.c` 里 `if (cpu_id != bsp_cpu_id && 0)` 的那个 `0`，不只是放开一个分支，
它同时把一批**单核假设**摊到台面上。这些假设在当前状态下永远不会暴露，也不会被任何
现有测试捕获 —— 因为只有一个 CPU 在跑。所以它们的危险程度被系统性低估了。

### 7.1 必须先在 SB-25 之前清掉的

| 项 | 单核假设 | 位置（以符号为准） | 两个 CPU 同时跑起来之后的后果 |
|---|---|---|---|
| **fork 父上下文单份** | 同一时刻只有一次 fork 在途，所以父进程快照可以放全局 | `syscall64_full.c` 的 `g_fork_parent_{rip,rsp,rflags,cr3,kstack,ustack,retval,gprs[]}` | 两 CPU 同时 fork，后者覆盖前者。被覆盖的那个父进程恢复时 **GPR 全是另一个进程的**，rip/rsp 也可能错位 —— 表现为随机崩在用户态任意地址，且极难复现 |
| **readdir 静态表** | syscall 不可重入 | `syscall64_full.c` `sys_readdir` 的 `static char names[64][256]`（16 KB BSS）+ `static uint8_t types[64]` | 两 CPU 同时列目录踩同一块 BSS：A 的条目被 B 覆盖，两边都可能拿到**别人目录的内容**。这是数据正确性问题，不只是性能 |
| **裸盘读写静态缓冲** | 同上 | `syscall64_full.c` `sys_disk_read` / `sys_disk_write` 各一份 `static uint8_t kb[64*512]`（各 32 KB BSS） | 两 CPU 同时读写盘会互相覆盖缓冲。这两个是**特权路径**（PID==0 闸），后果是往错盘位写错数据 |

### 7.2 已经在阶段 1 覆盖、但要核对是否真的覆盖到 AP 的

- **SB-08（FPU 保存区）**：8 个槽 + `& 7` 下标，与 `MAX_CPUS=16` 不匹配。这一条本来就在
  阶段 1，但它的危害**只在 AP 真正跑用户线程时才显现** —— 也就是说 SB-25 打开之前
  它一直是"测不出来的正确性问题"。建议把 SB-08 的验收从"启动不回归"改成
  "`-smp 12` 下跑浮点密集用户程序结果正确"，否则等于没验。

### 7.3 顺序建议（这是本节的实际用途）

1. 清 7.1 三处（fork 快照、readdir 静态表、裸盘静态缓冲）—— 纯数据结构改造，可在单核下先合入并回归；
2. 把 SB-08 的验收条件换成多核浮点实测；
3. 再打开 `&& 0`。

倒过来做（先开开关再清）几乎一定会得到"一开就崩、崩在哪说不清"的局面，因为上面三处
任何一个都能单独造成随机崩溃。

### 7.4 一处行号纠错

本文档此前写"SB-25 不实施即保持 `smp.c:140` 的禁用位"。实测该判断所在行已漂移到
`smp.c:289`。引用时请以 `cpu_id != bsp_cpu_id` 这个条件作为锚点，不要用行号。
