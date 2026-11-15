# 内核边界与分层（KERNEL_SCOPE）

> 目的：在继续往 SMP、下沉、模块化上走之前，先把"哪些代码真的在内核里、它们本来该属于哪一层"定下来。
> 分类依据是**构建清单**，不是目录名 —— 目录里有大量历史文件根本没参与编译。

## 1. 先纠正一个事实：仓库 239k 行，内核只有 89.6k

`kernel/arch/x86_64/Makefile` 的 `C_SOURCES` / `ASM_SOURCES` 是唯一真相：**136 个文件、89,619 行**编进 `hepu-kernel64.bin`。

其余约 30 万行**属于 32 位内核与共享代码**，不参与 64 位构建。仓库是**两套内核、一棵源码树**：
顶层 `Makefile` 用 `find kernel -name "*.c"` 全量编译 32 位内核（只排除 `arch/x86_64/` 与
`sched/scheduler.c`），`arch/x86_64/Makefile` 编译 64 位内核。两者的公共部分按
`UNIFIED_SOURCE_TREE.md` 逐步上移到 `kernel/shared/`（**32 位稳定优先**）。

| 未编译内容 | 行数 | 说明 |
|---|---|---|
| `kernel/arch/x86/`（32 位内核） | 46,162 | **不是死代码**：125 个文件全部被顶层 Makefile 编进 `hepu-kernel.bin`，含它自己的桌面 `desktop_render.c`(4,452)、应用商店、文件管理器、编辑器、播放器。32 位是并行维护的第二套内核（稳定优先） |
| `kernel/sched/` | 22,968 | 71 个文件里**只有 `scheduler.c` 一个被显式排除**（`Makefile:58` 注明"历史死代码，被 process.c/kernel_core.c 取代"）；`mobile.c`(1,233)、`mobile_sms`(969)、`enterprise.c`(479)、`packagemanager`(449) 全部编进 32 位内核 |
| `kernel/kunit/` | 12,827 | 测试，编进 32 位 |
| `kernel/gfx/` | 116,894 | 含 32 位内核的中日韩字库（`fonts/font_cjk.h` 61,172 + `font_default.h` 47,331，被 `gfx_font_manager.c` #include）。与 `arch/x86_64/gfx64.c` 是同名不同实现 |
| `kernel/fs/`、`kernel/drivers/`、`kernel/net/` 等 | 其余 | 同上：32 位实现，与 `arch/x86_64/` 下的 `*_64.c` 并存 |

**结论一**：谈"64 位内核里有什么"时只看这 136 个文件；谈"32 位内核里有什么"时看顶层 Makefile 的收录范围。
**不存在"死代码要清理"这回事** —— 目录里没有参与 64 位构建的文件，绝大多数是 32 位内核的本体。
两套内核的分叉重复（`gfx64` vs `kernel/gfx`、`process64_full` vs `kernel/sched/process.c`、两套 fs）
才是真实维护成本，收敛手段是 `UNIFIED_SOURCE_TREE.md` 的渐进共享，不是删除任何一侧。

## 2. 这 89.6k 行的实际构成

| 类别 | 行数 | 占比 | 该落在哪一层 |
|---|---:|---:|---|
| 桌面 / Shell UI | 18,005 | 20.1% | **ring3** |
| 图形输出（含 12k 字体数据） | 15,575 | 17.4% | 驱动半在 ring0，合成/绘制在 **ring3** |
| 驱动（IDE/AHCI/xHCI/PS2/VGA/RTC/ACPI/VirtIO） | ~11,000 | 12% | ring0 |
| 用户态 ABI（syscall / process / ELF / thread） | 7,387 | 8.2% | ring0（契约层） |
| 文件系统（ext2 / FAT32 / VFS） | 4,654 | 5.2% | ring0 |
| 安全 / 加密 | 4,447 | 5.0% | ring0 |
| 网络 | 3,424 | 3.8% | ring0 |
| 调度 / 进程 | 2,688 | 3.0% | ring0 |
| 中断 / CPU（GDT/IDT/APIC/SMP） | 2,302 | 2.6% | ring0 |
| 内存管理（PMM/VMM） | 1,079 | 1.2% | ring0 |
| 同步原语 | 90 | 0.1% | ring0 |
| 其他（库、散驱动、QML shim、TLS） | ~19,968 | 22.3% | 混合，逐项定 |

**结论二**：内核里最大的两块不是"内存/调度"，而是**桌面（18k）+ 图形（15.6k）= 37.5%**。真正属于"不可让出 ring0"的部分（内存 + 中断 + 调度 + 同步 + 驱动 + fs + net + 安全）合计约 **29k，只占 32%**。

也就是说这个内核现在是"**带完整图形栈和 shell 的单体内核**"，桌面不是寄生在系统上的应用，它本身是内核里最大的一块。

## 3. 判据：什么必须留在 ring0

不是按"重不重要"分，按**三条硬条件**分。任一条成立就必须在 ring0：

1. **能改页表 / 关中断 / 写 MSR** —— 只有内核能做。用户态一旦有这个能力，隔离就没了。
2. **被中断上下文调用** —— 中断里不能睡、不能缺页、不能依赖用户态调度。
3. **持有内核不变量（如每次切换必须守恒的队列）**。

反过来，只要**不满足任何一条**，它留在 ring0 就是纯负债：多一份不受隔离保护的代码，多一处 SMP 下的共享状态。

按这三条重判：

| 代码 | 判据命中 | 判定 |
|---|---|---|
| `pmm64/vmm64/gdt64/idt/apic64/smp.c` | 1、2 | 必须 ring0 |
| `sched.c`/`process64`（PCB 与上下文切换） | 1、2 | 必须 ring0 |
| `syscall64_*`、`entry_64.S` | 1、2 | 必须 ring0（这是契约本身） |
| 驱动（ide/ahci/xhci/ps2/rtc/acpi） | 1（MMIO/端口/中断） | 必须 ring0，但应以**驱动接口**对外，不是直接给桌面调 |
| fs / net / crypto | 否 | 可留 ring0，但**必须走 syscall 契约**，不能有内核内直调桌面的反向路径 |
| **`hpdesk.c` / `hs_apps.c` / `shell64.c` / `desktop64.c`** | **否** | **应在 ring3** |
| **`gfx64.c` 的填充/合成/文字排版** | 否（只有 framebuffer 写是特权） | 帧缓冲映射给用户态，合成在 **ring3** |
| `cjk_font_data.c` / `font_data.c`（12k） | 否 | 纯数据，应作为资源文件走 fs |

## 4. 三处越界，按代价排序

**① 桌面 18k 在 ring0（最贵）**
- 后果已经实测到了：打开 AP 调度后试图激活 `khouse`（它做 `hpdesk_mark_dirty`），立刻
  `#GP vector=0xD rip=0x46`。因为桌面状态（脏矩形、窗口 z-order、输入焦点）没有 owner，
  两个核能同时改。
- 这一条不解决，SMP 就只能停在"AP 跑一个不碰桌面的整数线程"，再多核没有意义。

**② 图形 15.6k 在 ring0**
- 其中 12k 是字体位图数据，占内核 BSS/rodata，直接顶到 1 MB 镜像上限（现 931 KB）。
- 合成、抗锯齿、文字排版都是纯用户态可做的事，只要拿到 framebuffer 映射 + 一个 blit 契约。

**③ 用户态 ABI 7.4k 里混着策略**
- `syscall64_full.c` 3,235 行单文件，里面既有 ABI 分发，也有具体业务（readdir 组装、disk 缓冲、fork 快照）。
- 这些应该被"系统调用 → 内核服务"的转发层切开，服务实现在 fs/ 和 mm/ 里。

## 5. 收敛顺序（不动代码，先定边界）

1. **文档锁定归属**（本文）—— 每个模块写清 owner，谁是唯一能改它的核/线程。
2. **桌面状态收 owner**：脏矩形、z-order、焦点三样各自只有一个写入者。
   做完这一步，`khouse/kstat` 才能在多核下跑（现在不能）。
   **进度（2026-11-14）：脏矩形已收 owner，khouse/kstat 已跨核跑通。**
   实现：`hpdesk_claim_owner()` 在桌面主循环入口钉住 owner（进程槽 = 桌面上下文），
   `hpdesk_is_owner()` 判定；非 owner 调 `hpdesk_mark_dirty()` 时**不直接改
   `g_content_dirty`/窗口 `content_dirty`**，只置一个请求位 + 计数，
   由 owner 每帧在 `hpdesk_deskstate_drain()` 里消费执行。
   为什么不能靠加锁：脏矩形是 4 个 int 的并集运算，两个核同时做会算出"只有半个矩形"
   的脏区，锁只能防撕裂不能防逻辑错。
   证据（`-smp 4` 实跑 30 秒）：`[KHOUSE] entered cpu=0x3`、
   `[DESKOWN] defer from slot=0x5`、`[DESKOWN] deferred handled, ext_requests=0x1`；
   `khouse`/`kstat` 均从 `create_suspended` 改为 `create`，跑在 AP 上，#GP 不再出现
   （§7.1.2 记录的 0xD 异常随之消失）。回归 `-smp 2` 2/2、`-smp 12` 1/1 全绿。
   **进度（2026-11-15）：z-order 与焦点也收了**，但收法是**准入**不是队列 ——
   置顶（先摘后追）与焦点（先清后置）都是多步簿记，请求位表达不了"从哪置顶"，所以
   落成 `hpdesk_owner_guard(what)`：owner 放行计数，非 owner 直接拒绝并打一次
   `[DESKOWN] reject <what> from slot=0xN`，启动期（未认领）放行。接在
   `hs_window_create/destroy/focus/show/hide/raise_inner/lower_inner` 与
   `hpdesk_switch_workspace` 九个入口。自检 `[DESKOWN] zorder/focus selftest PASS
   pass=0x2 reject=0x0`；冷启动 3/3、`-smp 12` 1/1 全绿。
3. **兼容层出内核**（`DESIGN.md` 约束一，权重最高 —— 该文档明确写"违反视为架构级事故"）：
   `kernel/compat/`（`binfmt_elf` / `binfmt_pe` / `linux_abi` / `binfmt_macho`）与
   `syscall64_full.c` 的 `linux_map[350]` 全部编在内核里，没有独立用户态服务。
   化解路径是 `PHILOSOPHY.md` 7.2 阶段 2（用户态 ELF 加载器 + syscall 翻译）。
   排序理由：这是唯一被设计文档定性为"架构级事故"的条目，不应排在任何优化之后。
   **进度（2026-11-15，第 1 步）**：先把"要不要翻译、翻译成几号"收成一个网关入口。
   新增 `compat_gate.c/.h`：`linux_map[350]`（103 个非零映射）从
   `syscall64_full.c:3094` 整段搬出，`syscall64_full.c` 的翻译分支改为调用
   `compat_gate_active()` + `compat_gate_translate_nr()`，**内核里再没有一处直接
   读这张表**。判定条件与表内容一字未改（零行为变化），目的是让"表搬到用户态服务、
   内核侧改成一次 IPC 查询"这一步将来只动实现、不动调用点。
   自检 `[COMPATG] linux_map selftest PASS entries=0x67 bad=0x0`（抽查
   read/write/execve/futex/getdents/exit 六项映射 + 未映射返回 0 两条不变量）；
   回归 `-smp 2` 3/3，二进制 936,616 B（+64 B）。
   **未验**：Linux 兼容端到端（静态 glibc 程序实跑）没在这次回归里覆盖，因为
   `.toolchain/regress.sh` 只跑原生 child；本次的正确性依据是"同表同判定"的等价性，
   不是实测。下一步搬 `elf64_is_linux_abi()` 与 `user_process.c` 的 Linux 适配分支时，
   必须补一个 Linux ELF 实跑用例。
4. **framebuffer 契约**：把"写屏"从 `gfx64` 抽成一个最小接口（map + blit + flip），
   合成逻辑整体搬到用户态。这一步做完，① 和 ② 一起解决。
   （现状：第一层 `display_backend.h` 已落地，lfb + mirror 双后端，等价自检通过。）
5. **syscall 转发层切分**：把 `syscall64_full.c` 的业务搬进 fs/mm，syscall 只剩分发。
6. **32 位侧接入 `kernel/shared/`**（**不是归档**，`UNIFIED_SOURCE_TREE.md` P1）：
   现状是 32 位引用 shared 的文件数 = **0**，64 位已 12+。第一条按文档走：
   32 位 `sys_mem.c` → `shared/security/user_access.h`。

## 6. 与当前 SMP 状态的关系

- `HS_AP_SCHED_ENABLED=1` 已开。**此前** AP 只跑 `kbeat`（纯整数、不碰桌面），那是安全上限。
- **2026-11-14 起**：§5-2 的脏矩形 owner 已落地，`khouse`/`kstat` 可以跨核跑
  （非 owner 只提请求，owner 消费）。所以"会碰桌面状态的线程不许跨核"这条
  现在收窄为"**不许绕过 owner 直接改桌面状态**"。
- **2026-11-15 起**：z-order 与焦点也收 owner 了，但收的方式和脏矩形不同 ——
  这里加的**不是队列，是准入**。原因：置顶（先摘后追）和焦点（先清后置）都是
  多步簿记，跨核提交一个"请求位"无法表达"从哪个位置置顶"，反而会算出错的 z 序；
  而现在的调用者只有桌面主循环一个，加队列等于给一个不存在的竞争修一条路。
  所以落地的是 `hpdesk_owner_guard(what)`：
  - owner 上下文 → 放行并计数；
  - 非 owner → **拒绝执行**，串口打一次 `[DESKOWN] reject <what> from slot=0xN`；
  - 启动期（owner 未认领）→ 放行（那时还没进主循环，不可能有第二个核在改）。
  接在 `hs_window_create / destroy / focus / show / hide / raise_inner / lower_inner`
  与 `hpdesk_switch_workspace` 九个入口上。
- 证据：`[DESKOWN] zorder/focus selftest PASS pass=0x2 reject=0x0`（启动进主循环前
  跑一次，pass 非 0 才说明守卫真的被入口走到，不是接了没走）；
  冷启动 3/3、`-smp 12` 1/1，全部 `desk=1 EXC=0 PASS=1 SPINERR=0`。
- 已知缺口：自检里那次"屏外 1x1 探针窗口"的 create 在启动早期返回 0
  （`create_probe=0x0`），说明窗口系统在 `desktop64_run()` 入口这一刻还没完全就绪。
  它不影响准入守卫（守卫已由 pass 计数证明接线），但登记为待查项。
- `SB-08`（FPU 保存区 8 槽 vs 16 核）仍未实测，需要真实双核浮点负载 —— 现在
  `kstat` 已能跨核跑，可作为下一轮的试验载体。
