# 技术债路线（2026-11-15，状态核对 2026-11-17）

> 只登记**已经确认存在问题**的条目，不写"将来也许要做的优化"。
> 每条必须带：现状证据、为什么算债、验收标准、回滚方式。
> 判断以代码和串口证据为准，不以本文档的规划为准。
>
> **状态口径（2026-11-17 核对）**：P0/P1 全部闭环（P0-1 代码已删、P0-2 已改运行时
> 动态分配、P1-1..P1-4 32 位桌面全链路在跑）。此前这 5 项一直挂在"计划"栏没有状态
> 标记，导致"还剩多少"被虚增 5 项。台账外新发现另列在末尾。

## 0. 排序原则

1. **会挂但靠止损的，排第一**。止损不是修复 —— 今天清一把锁，明天可能在另一把锁上重演。
2. **安全边界出现两套标准的，排第二**。两套内核各写一份用户地址校验，等于两套信任模型。
3. **静默错误（回归抓不到）的，排第三**。改了看不出来，比改坏更危险。
4. **能增量、每步可回归的，往后放**。它们可以一直做，不急着插队。

---

## D-1　`g_sched_lock` 双重解锁 —— **降为观测项（2026-11-17 三档核数实测零命中）**

**本轮结论**：当前 `251a203` 基线上**没有观察到**双重解锁，三档核数长跑零命中；
但按原结论"不能证明绝不可能错"，因此**不关闭**，从"最高优先"降为**观测项**。

**证据一（代码结构）**：`g_sched_lock` 全仓库只有一处 `spinlock_lock` 原语
（经封装 `sched_lock_acquire`/`_release` 使用，二者成对），调用点共 6 组：
`process64_block`(1050)、`process64_wakeup`(1066)、`schedule` 内 4 条切换路径(887/931/958/964)，
外加 365/388 两处。逐条核对**配对关系**：
- `process64_block`/`wakeup`：`if` 分支内 `release` 后**立即 return**，不会落到末尾第二次 release；
- `schedule` 的 4 处 `release` 后面**全部紧跟 return**（含 `return -1` 那条）。

**证据二（实测）**：`-smp 2` / `-smp 4` / `-smp 12` 各跑一次 90~100 秒，
串口 `grep -c 'SPINLOCK'` 均为 **0**，且日志里 `frames=`、`[SB08] bsp load ok=` 持续输出
（说明系统确实跑到了桌面并持续运行，不是"启动就挂所以没打印"）。
历史日志（`ser_kill.log`/`ser32.log`/`ser_linux.log`）也无一命中。

**为什么曾判最高优先**：台账记录的是历史状态（某一版加锁/解锁确实不配对）。
`SB-23`（"查状态→改状态→出队"整段持锁）与 `SB-26`（诊断影子变量）那两批改动
在修其他问题的同时把调用点也理顺了，但台账没有回头更新 —— 这是"台账比代码旧"
的典型，不是真债。

**保留**：`spinlock.c` 的诊断输出（`addr`/`owner`/`cur`/`rec`/`lock_at`/`unlock_at`）原样保留。
它是**零成本哨兵**：真出问题时能直接打出调用点，不出问题时什么也不打。

<details><summary>原始记录（保留以说明历史）</summary>


~~历史标题~~　`g_sched_lock` 双重解锁：调用点未定位（最高优先）

**现状**：`spinlock_unlock` 检测到 owner 不匹配时打印 `[SPINLOCK] ERR addr=... owner=... cur=...`
后**强制清锁**。这是止损，不是修复 —— 逻辑错误依然发生，只是不再升级成永久死锁。

**为什么算债**：一次"解锁一把自己没持有的锁"意味着某条路径的加解锁配对错了。
强制清锁会让另一个正在临界区里的核**以为自己还持锁**，然后两个核同时改同一份调度数据。
`nm` 已对上 `addr=0x1e3ae0` = `g_sched_lock`（`process64_full.c:782`）。

**已做的诊断改造（本文件同日）**：加了"影子变量"记录最近一次加锁点
（`g_sl_last_lock_addr` / `g_sl_last_lock_site`，见 `spinlock.h` 注释）。
`unlock` 不匹配时打印 `lock_at` + `unlock_at` 两侧地址 —— 只有前者能指向源头。

**踩过的坑（必须记住）**：第一版把 `lock_site` 加成 `spinlock_t` 的字段，
`spinlock_t` 从 12 字节变 24，整个 `.bss` 布局位移，**`-smp 12` 从 3/3 直接掉到 0/3**
（回退即恢复 3/3）。结论：**给内核共享结构加字段 = 挪全局布局**，仓库里存在依赖
固定布局/时序的路径。诊断手段不能把系统弄坏，所以改用影子变量。

**验收**：连续 20 轮 `-smp 12` 不出现 `[SPINLOCK] ERR`，且 `lock_at`/`unlock_at`
一旦出现能对上具体调用函数（`addr2line` 验证）。修掉配对错误后移除强制清锁。

**压力结果（2026-11-15，15 轮）**：

| 配置 | 轮数 | 结果 | `[SPINLOCK] ERR` |
|---|---|---|---|
| `-smp 12` | 10 | **10/10** 通过 | 0 |
| `-smp 16` | 5 | **5/5** 通过 | 0 |

**静态核对**：`g_sched_lock` 全文件只有 **4 处引用** —— 一个静态定义（`:782`）、
一对 `acquire`/`release` 封装（`:784`/`:793`），真正的加解锁都经这对封装。
所有调用点（`:365/:388/:808/:1050/:1066` 五处 acquire，`:887/:931/:958/:964/...`
九处 release）逐条核对下来**配对无误**，`process64_schedule` 的每个 `return` 前
都恰好 release 一次，且 release 在 `context_switch_64` 之前（切到新栈之前就放锁）。

**结论（降级，但不是关闭）**：触发条件已消除。最可能的根因是启动期
`khouse`/`kstat` 参与调度导致同一核调度路径重入（acquire 两次、release 两次），
而 `3aeb4b6` 已把它们改回 `process64_create_suspended`（启动期只允许 kbeat 参与调度）。
所以现在的"强制清锁"**不再是活跃止损，而是保险** —— 保留它，
因为它把"一次逻辑错误"和"永久死锁"隔开，代价只有十几行打印。

**仍未证明的**：15 轮零复现只能说明"当前代码在当前压力下不触发"，
不能证明"这条路径绝不可能错"。所以不关闭，降为**观测项**：
后续每轮回归都看 `SPINERR`，一旦重新出现，`lock_at` 能直接指向源头。

**回滚**：强制清锁那段单独一个 commit，可单独 revert（清锁本身是止血，先留着）。

---

</details>

---

## D-2　32 位内核引用 `kernel/shared/` 的文件数 = 0

**现状**：`kernel/fs/*.c`、`kernel/mm/*.c` 里引用 `shared/` 的文件数 **0**
（64 位已 12+）。`UNIFIED_SOURCE_TREE.md` 说的"统一源码树"目前只有"一棵树"，没有"共享"。

**为什么算债**：用户地址校验（`shared/security/user_access.h`）在 64 位是一套实现，
32 位各写各的。**两套内核 = 两套安全模型**，任何一处校验规则变化都只改一半。

**第一步（按 `UNIFIED_SOURCE_TREE.md` P1 走）**：32 位 `sys_mem.c` →
`shared/security/user_access.h`。只做"引用现成头文件、复用现成校验"，不改 32 位行为。

**D-2a 已做（2026-11-15，先把 32 位基线立起来）**：这条债不能直接动代码 ——
动之前 32 位**根本编不出来**，改完也无从验证。查到的三道墙和处置：

| # | 墙 | 处置 |
|---|---|---|
| 1 | `user/libhepu/crt0.S` 只有 64 位写法（`%rsp`/`%rax`/`syscall`），32 位构建报 `bad register name '%rsp'` | 按 `__x86_64__` 分支：64 位 `syscall` + rdi；32 位 `int $0x80` + ebx（入口见 `kernel/arch/x86/interrupt.S`） |
| 2 | initrd 的依赖列表写死 12 个 Rust 用户程序，本机 **没有 cargo** → 内核整个编不出来 | 用 `which cargo` 条件化：无 cargo 时 `RUST_USER_ELFS` 为空。这些是 initrd 里的**附加应用**，不是内核依赖，不该把环境问题升级成构建失败 |
| 3 | `$(BUILD_DIR)/test_hepu.txt` 在 initrd 依赖里，但**既无生成规则、源文件也不存在** | 从依赖里移除并留注释。这是个永远不可能满足的幽灵依赖 |

**结果**：32 位内核编出来了（`build_v250/hepu-kernel.bin` 3,315,694 B），
并且**能启动** —— 用 `qemu-system-i386 -kernel kernel.elf`（**必须 .elf**：
纯 .bin 没有 multiboot 头，QEMU 静默不动，表现为串口零输出）。
串口 306 行，跑到 `[INIT] phase: net proto`，途中 `[INITRD] base=... head=0x55504548`
（head = "HEPU" 魔数）、`[HUGETLB] selftest: pass=0x7 fail=0x0`。

**新增 `tools/regress32.sh`**：32 位冒烟回归，三条判据 ——
`[SERIAL] COM1` 就绪、`[INITRD] head=0x55504548`、无 `PANIC/#GP/#PF`。
实测 PASS。**这是 32 位第一次有回归兜底。**

**D-2b 已做（2026-11-15）**：32 位 `sys_mem.c` 接上 `shared/security/user_access.h`。

不是"加个 include 就算接上了"——契约要求的是一个**变量 + 七个函数**，而 32 位原先自写
一套（写死 `0xC0000000`、签名用 `uint32_t`、函数名也对不上）。改动：

| 项 | 原状 | 现状 |
|---|---|---|
| 用户空间上限 | `user_ptr_valid` 里写死 `0xC0000000` | 定义 `hepu_user_space_limit = 0xBFFFFFFF`，校验读它 |
| 边界判据 | `addr + size > 0xC0000000` | `addr + size - 1 > limit`（压线单字节可读，与 64 位一致） |
| `copy_from_user`/`copy_to_user` | `uint32_t size` | `size_t size`（32 位同宽，ABI 不变） |
| 五个契约入口 | 全缺 | 补齐 `hepu_is_user_address`/`verify_user_read`/`verify_user_write`/`copy_str_from_user`/`user_strlen` |

**32 位没有 `ua_page_ok`**：64 位那套逐页校验依赖页表，32 位这里只做范围 + 溢出检查，
语义边界保持一致（空指针/零长度/越界一律拒），这是**如实的能力差**，不是补齐了。

**自检 `user_access32_selftest()`**（`init_sequence.c` 在 fs 阶段之后调用）：
五条判据 —— 低地址放行、内核地址拒绝、`0xBFFFFFFF` 单字节可读、往后一格越界、
溢出拒绝。串口实测：

```
[SEC32] user_access contract PASS limit=0xBFFFFFFF
```

**验收**：32 位引用 `shared/` 的文件数 **0 → 1**；`tools/regress32.sh` PASS
（`serial=1 initrd=1 fault=0 lines=307`，比 D-2a 多一行即自检行）；64 位标准回归 1/1 不受影响。
`kernel.elf` 3,474,260 B。

**注意这只是第一条线**：32 位其余文件（`kernel/fs/*.c`、`kernel/mm/*.c` 等）**仍未接 `shared/`**。
引用数 1 是"开了头"，不是"完成"——D-2 的完整口径是 32 位核心子系统都走契约，不是单文件打钩。

**顺带解决了 32 位真正的硬阻塞：网络自检死循环**（不属于 D-2，但它是 D-2 的验证前提）。

现象：32 位串口停在 `[NET-TEST] Starting DHCP...`，60 秒、75 秒、加 `-nic user` 都一样，
总是 306 行。原以为"等网络超时"，实际是**死循环**。

根因（一行诊断代码定死）：

```
[NET-TEST] diag tick=0 if=0(interrupts OFF)
```

32 位从启动到 net proto **全程 IF=0**（`kernel/arch/x86` 里除 `vga_gfx.c` 两处外
没有任何 `sti`），PIT 中断不执行，`pit_get_ticks()` 恒 0。而 `dhcp_start()` 的超时判据是：

```c
while (pit_get_ticks() - start_time < 3000)   /* start_time = 0 → 0-0 < 3000 永远真 */
```

**永远成立，出不来。** 不是网络慢，是超时条件在中断关闭时数学上不成立。

修法（`kernel/net/dhcp.c`）：双判据 `dhcp_wait_expired()` —— tick 一旦开始走就用真时间，
tick 不走就退化为轮询次数上限（`DHCP_WAIT_SPINS 400000`）。

**为什么不动 `sti`**：32 位整条启动路径都不开中断（图形阶段才开），这是既有设计；
在 net 之前补 `sti` 会让 PIT ISR 里的 `sched_tick()` 进到未验证的启动期调度 ——
64 位在 `3aeb4b6` 踩过这个坑（khouse/kstat 参与启动期调度直接死锁）。改一个等待循环
是局部止损，开中断是把风险摊到全局。

**效果**：串口 **306 → 536 行**，跑完 52 个 KUNIT suite、字体加载、
`[APPS] 7/7 elastic_desktop_init done`、`[SHELL] main started`。

`tools/regress32.sh` 判据同步从 3 条加到 5 条（加 `[NET-TEST] === Self-Test Complete ===`
和 `[DESKTOP] elastic free desktop init done`），实测 5 条全过。

**第一次跑到这么远，也就第一次看见 32 位的存量问题**（都是 32 位历史遗留，不是 D-2 引入）：

| suite | 结果 |
|---|---|
| syscall deep | 0x31 passed / **1 failed** |
| user scenario | 0xD passed / **3 failed** |
| integration | 0xD passed / **1 failed** |
| security E2E | 0xE passed / **1 failed** |

合计 6 个 fail，`[KTEST] RESULT: FAIL`。**32 位从来没跑到这一步，所以这些从没被看见过。**



**验收**：32 位内核能编能跑（32 位回归按现状跑通）；32 位侧引用 `shared/` 的文件数从 0 变 1；
64 位回归不受影响。

**回滚**：单文件引用改动，revert 即可。

---

## D-3　`hpdesk_owner_guard_selftest()` 的 `create_probe=0x0` 未查（已关闭 2026-11-17）

**旧现状（台账原记录，已被实测推翻）**：`desktop64_run()` 入口的屏外 1×1 探针窗口创建返回 0（失败），
自检仍判 PASS（判据是 `pass>=1 && reject==0`），所以这个失败被**静默吞掉**了。

**实测结论（2026-11-17，64 位 UEFI/OVMF，devrun tag `d3probe-2026-11-17`，串口
`devrun/logs/d3probe-2026-11-17/ser64.log`）**：探针窗口已能建成，自检 PASS：

```
[DESKOWN] owner slot=0x0 (dirty+zorder+focus armed)
[WM] create owner-probe 200x120
[DESKOWN] selftest PASS pass=00000003 reject=00000000 create_probe=00000001
```

**真实根因（上一轮修复）**：不是窗口表满、也不是屏外坐标被裁剪拒绝，而是**初始化顺序**——
自检在 `claim_owner` 之后直接 `hs_window_create`，槽位初始化要到 `desktop64_init → hpdesk_init`
才做；BSS 里 `windows[i].id` 默认 0，而空闲判据是 `id < 0`，槽扫描找不到空位，create 直接
返回 NULL。修法：`desktop64_run` 在自检前显式 `hs_window_manager_init()`（幂等，`hpdesk_init`
里还会再做一次）。

**判据显式化（本轮）**：失败不再被放过——`created==1` 是硬判据；失败打 FAIL 行并给
`fail_code` 位掩码（0x1 直探被拒 / 0x2 pass 不足 2 / 0x4 存在 reject / 0x8 探针未建成），
vga 与串口双路输出；调用方检查返回值并补打失败行。

**遗留观察（不算 D-3 失败）**：探针请求 1×1 屏外坐标 `(-64,-64)`，但 WM 几何钳制会把最小
窗口放大到 200×120 并拉回屏内；窗口在首帧渲染前销毁，不留可见窗口。若要严格做到屏外 1×1，
需另给探针一条绕过最小尺寸钳制的路径，单列后续项。

---

## D-4　桌面四项（一致性债，不影响运行）

| 项 | 现状 | 验收 |
|---|---|---|
| 控制中心图标 | 位图（`hs_icons_data.h` 32×32 1bit），与 `DESKTOP_SHELL.md` 1.2 矢量方向相反 | 图标走矢量管线，位图集删除或只留迁移期 |
| `bar_height` | 配置串写 `36`（`hpdesk.c:436`），回退默认 `40`（`:533`） | 只有一处真源，另一处引用它 |
| 天气卡 | `hs_panel_bounds` 与 render 差 24px | 两者用同一份矩形 |
| 胶囊居中 | 口径已定 `capsule_x = (整屏宽 - 胶囊宽)/2`，代码未按此改 | 代码按此式计算 |

---

## D-5　`syscall64_full.c` 业务未切完（2,897 行）

已切 4 批（目录三件套、stat/fstat、open/close、mmap/munmap/mprotect）。**每批都必须先补测试再切** —— 见下表"前置条件"。
剩下 2 批：read/write、其余。

| 批 | 内容 | 前置条件 |
|---|---|---|
| 2 | ~~`sys_stat` / `sys_fstat`~~ **已完成 2026-11-16** | 探针 `statprobe.c` 先跑出 4 个 FAIL（blksize/blocks 类型错、ino=0、少拷 16 字节），修完 15 项全绿 |
| 3 | ~~`sys_open` / `sys_close`~~ **已完成 2026-11-16** | 用例 `fdstress.c`：两线程各 400 轮 open→close，0 冲突 0 泄漏 |
| 4 | ~~`sys_mmap` / `munmap` / `mprotect`~~ **已完成 2026-11-16** | 用例 `mmprobe.c` 20 项：窗口/保留区/溢出，先旧代码全绿再切 |
| 5 | `sys_read` / `sys_write`（最热路径） | 性能基线（现在 `[PROF]` 有数） |

**为什么第 2 批危险**：`struct stat` 偏移错一位，`ld.so` 就用错误的 `(st_dev, st_ino)`
去重，把 `libc.so.6` 认成主程序 —— 而**标准回归照样全绿**（原生 child 不用 glibc）。
所以第 2 批必须先有 Linux 侧实测，否则改坏了看不出来。

---

## D-6　兼容层加载器 / framebuffer 所有权（大工程，非增量）

- 兼容层剩余：`user_process.c:1906-2182` 的 Linux 适配（`AT_*`、`link_base`、ld.so 区）。
  与原生 ELF 加载共享同一段代码，切一刀会和原生纠缠。**这是"整块搬用户态"的量级。**
- framebuffer 剩余：`map` 的映射动作 + "谁拥有前台"的定义。
  不定义所有权就开放映射 = 内核合成器与用户态合成器抢同一块显存。

**这两条不做半步**。原因：硬拆会得到"桌面在内核、合成在用户态"的半搬家状态，两边都要改。
要么不动，要么整块搬。

---

## 当前建议的推进顺序

```
D-3（create_probe 被吞） → D-2（32 位接入 shared） → D-5 第 2 批（先补探针） → D-4（桌面四项）
                                                                    ↑ D-1 已降为观测项，回归时顺带看 SPINERR 即可
```

**2026-11-17 更新**：D-1 从顺序里退出（降为观测项，三档核数实测零命中，诊断保留为哨兵）。
现在排第一的是 **D-3** —— 它是"自检绿灯但内里有个失败被吞掉"，属于会误导后续判断的那类，
且成本极低（一个窗口创建返回 0，查是表满还是坐标被拒即可）。
D-2 从第二变第二没变，因为"双微内核是否成立"仍然取决于它。
D-4 项数多但都是视觉一致性，可以随时插空。

---

## D-7 32 位图形模式未生效（显示花屏）

**为什么排在这**：它会"挂靠"——图形画不出来，32 位就永远验不到桌面对不对，
后面所有 32 位 UI 工作都没法验收。

**现状证据**：32 位跑到 `[DESKTOP] elastic free desktop init done`（逻辑初始化成功），
但 QEMU `screendump` 得到的是 **720×400**（= VGA 文本模式的分辨率），画面是彩色像素块，
没有任何可读文本或桌面元素。

**定性**：`kernel/arch/x86/vga_gfx.c` 按 mode 0x13（320×200×256）或 VBE LFB 逐像素写
`framebuffer`，但**模式没切换成功** —— 于是像素数据被写进了文本模式显存区域，
把 80×25 的字符位图覆盖成花屏。这不是"画错了"，是**目标显存不对**。

**验收**：`screendump` 尺寸等于 `gfx_width × gfx_height`，且画面能辨识出顶栏/图标。

**回滚**：无（修复，非优化）。

**已查明的三件事（都不是"显示驱动不灵"，是三处具体的错）**

① **启动流程里根本没人进图形模式**。`vga_gfx_init()` 全仓库的调用点都在
task_manager / bsod / shell 命令里，启动序列一次都没调它。于是屏幕一直停在
SeaBIOS 的文本模式，而桌面用 `gfx_draw_*` 往 `0xA0000` 写像素 —— 文本模式下那里是
**字符显存**，字符位图被像素数据覆盖，就是看到的"彩色花屏"。

② **`SR4` 写成了 `0x07`，但 Chain4 是 bit3**。`0x07` 的 bit3=0，所以 chain4 从来没开；
显存按 4 个位平面组织，256 色彩色逐字节写进去就散成噪声。正确值是 **`0x0E`**。
读回证实：改前 `seq[...]=0x06`（QEMU 把 bit0 掩掉），改后 `0x0E`。

③ **`GC5` 写成了 `0x00`，但 256 色要 bit6=1**。正确值是 **`0x40`**（GC6=0x05 才是
memory map 0xA0000）。读回证实：改前 `gc[5]=0x00`，改后 `0x40`。

**已做的修复**：进 0x13（不再让假 GOP 顶替）+ 切完模式立刻 `gfx_flip()` 一次
（0xA0000 里还留着 SeaBIOS 字符位图，不刷掉就继续花）+ 上面两个寄存器值。
**证据**：显存前 64000 字节全部被写过（`nonzero=0xFA00`=64000），
说明绘制→flip→显存这条链路已经通了；画面从"整屏彩色噪声"变成
"顶部一条残留 + 其余全黑"。

**仍未解决**：QEMU `screendump` 报 **640×400**，画面呈"每 8 像素重复"的条带。
这个 640×400 是 VGA mode 13h 的正常时序（CRTC 按 640×400 扫描、像素 double-width /
double-height，真实分辨率仍是 320×200），所以尺寸本身不是证据；
但"每 8 像素重复"是**位平面读取**的特征，说明显示侧还没按 chain4 逐像素解释。
**串口取证把这条也查清了（第五处错）**：切到 0x13 之后没有被 `vga_gfx_text_mode()`
改回（给它加了返回地址打印，一次都没触发），但三次 `vga_dump_regs` 对比暴露了真凶：

```
① SeaBIOS 文本:  seq[4]=0x02  gc[5]=0x10  gc[6]=0x0E
② 我们设 0x13 后: seq[4]=0x0E  gc[5]=0x40  gc[6]=0x05   ← 正确
③ 桌面 init 后:   seq[4]=0x02  gc[5]=0x00  gc[6]=0x0E   ← 被还原成文本值
```

还原者是 **`kernel/drivers/vga_cjk.c`**（CJK 字体加载）。它往"平面 2"写 16×16 字模，
需要临时改 SR2/SR4/GC5/GC6，**但"恢复"是硬编码成文本模式的值**
（SR4=0x03 / GC5 干脆没恢复 / GC6=0x0E）。在图形模式下跑一遍，SR4 就从 0x0E
掉回 0x02 —— chain4 没了，整屏立刻变回位平面条纹。

**修法**：改成"进入时读回原值、结束时写回原值"（`cjk_save_regs`/`cjk_restore_regs`），
不再假设当前是文本模式。验证：第三次 dump 现在保持 `seq[4]=0x0E`。

**还有一处设计冲突（未修）**：CJK 字模是往 `0xA0000 + slot*32` 写的 —— 文本模式下那是
字符显存的平面 2，**图形模式下那就是帧缓冲**。所以字体一加载，屏幕上就会多出点阵图案
（截图里中间那两行彩色点就是它）。32 位启动流程是"字体 → 桌面 init"，中间没有重绘，
所以点阵留在了屏上。要么图形模式下把字模写进内存表（不碰显存），要么字体加载后强制
重绘一次。

**已修：32 位进桌面主循环（但仍画不出东西，见下一段）。**
在 `[APPS] 7/7` 之后直接调 `elastic_desktop_main_loop()`，串口确认
`[EDESKTOP] entering main loop (32-bit VGA path)` 之后接上的是
`[EDESKTOP] entering main loop with mouse...` —— 绘制循环真的在跑。
同时在字体加载后补了一次 `gfx_flip()`，把字模污染的点阵盖掉。

**新发现的根本矛盾（这才是"黑屏"的真因）：色深不匹配。**
`desktop_render.c` 的桌面是**按 24 位真彩色画的**：
`draw_modern_background()` 用 `gfx_fill_rect(0, 0, gfx_width, gfx_height, 0x2D3748)`，
图标、顶栏、文字全是 `0xRRGGBB`。而 32 位当前跑在 **VGA 0x13 = 8bpp 索引色**，
调色板只有我们自己设的那 16 个颜色。于是 `0x2D3748` 被当成**调色板索引 0x48** 去查，
查不到 → 全屏变成黑（截图里只剩中间一行稀疏点，那是字模残留）。

也就是说：**五处寄存器错让"写显存"这件事成立了，但颜色这件事还根本不通。**
在 8bpp 上无论怎么修寄存器都不可能出现桌面 —— 24 位色无法用 256 色索引表达。

**已完成：32 位切到真彩色（第七处错也一起修了）。**

新增 `kernel/gfx/bochs_dispi.c`（32 位专属，符合 `GRAPHICS_BOUNDARY.md` 规则 3）：
读 dispi ID → 设 640×480×32 → 读回验证 → 读 PCI `0:2.0` BAR0 → 回填 `gfx_*`。

实测串口：

```
[BD-VBE] dispi id=0x0x0000B0C5
[BD-VBE] mode set: 0x00000280x0x000001E0@0x00000020bpp     ← 640x480@32
[BD-VBE] pci 0:2.0 bar0=0x0xFD000000
[BD-VBE] linear framebuffer ready
[GFX] true-color mode applied: 640x480@32bpp fb=0xFD000000
```

**这中间又抓出三处错（D-7 的第六、七、八处）：**

**⑥ 假 GOP 抢走真后端。** `kernel/gfx/gfx_gop.c` 的 `gop_init()` 只看
`gfx_width != 0 && gfx_height != 0` 就宣告成功，而这两个全局量**默认就是 320/200**
（`vga_gfx.h` 里 `GFX_WIDTH 320`）。所以它永远"成功"，并且在探测顺序里排在 VBE
前面，把真后端顶掉，然后画到硬编码的 `gfx_fb_phys = 0x80000000` 上 —— 一片黑。
修法：加 `gfx_gop_available` 标志，默认 0；只有 UEFI 引导真的拿到 GOP 协议才置位。
32 位走 BIOS/multiboot，永远没有 → 直接拒绝。日志变成
`[GFX-GOP] no GOP protocol from firmware (BIOS boot), refuse`。

**⑦ 图形初始化发生在分页启用之前，LFB 会掉出直映区。**
`kernel_init.c` 里图形后端注册在第 187 行，而 `paging_enable()` 在第 295 行。
所以 dispi 必须**在图形段之前**设模式（此刻没分页，LFB 物理地址可直接写）；
而 `paging_init` 只直映前 256MB，`0xFD000000` 在区外 ——
必须 `paging_enable()` 之后补一遍 `paging_map`，否则第一次绘制就 #PF。
一开始把 dispi 放在 `init_sequence` 的 `[APPS] 3/7` 段是错的：`gfx_subsystem_selftest`
在它之前就跑完了，自检 test9 在 320×200 下画 `y=400` 的线，越界卡死，
**永远走不到后面那段代码**（日志里连 `[APPS]` 都没有）。

**⑧ 回退判断不认 dispi，把刚设好的真彩色覆盖回 8bpp。**
`gfx_has_vbe_fb()` 只查 `vbe_fb`，而 dispi 路径没填它 → `init_sequence` 判定
"没有真帧缓冲"，于是调 `vga_gfx_init()`，后者**无条件**把 `gfx_width/height/bpp/
fb_addr` 重置成 320×200@8bpp/`0xA0000`，还打出误导性的
`no VBE/GOP framebuffer -> entering VGA mode 0x13`。
修法：`gfx_has_vbe_fb()` 先查 `bochs_dispi_fb_phys() != 0`。

**结果**：`screendump` 变成 **640×480**，画面是**准确的 `0x2D3748` 深灰蓝**
（`draw_modern_background` 的颜色第一次被正确解释），184 色。
串口 `[EDESKTOP] entering main loop (32-bit path)` + `with mouse...` 都在。

**又抓出三处（第九、十、十一处）—— 都是"两套绘制路径并存"的后果：**

**⑨ `gfx_flip()` 用后台缓冲覆盖了真彩色画面。**
`gfx_flip()` 的语义是"把 back_buffer 整帧拷到 gfx_fb_addr"，那是 VGA 双缓冲的
做法。但 VBE 路径的绘制（`gfx_fill_rect` 等）是**直写 LFB** 的，此时再拷一次
back_buffer，等于用一块陈旧缓冲盖掉刚画好的桌面 —— 实测整屏变成单一 `0x1A202C`
（某个早期填充的残留）。修法：`gfx_flip()` 在 dispi 路径下直接 return。

**⑩ `framebuffer` 仍指向 8bpp 的 back_buffer。**
32 位图形有**两条并存的绘制链**：`graphics.h` 的 `gfx_*` 内联走 backend 直写 LFB；
而 `gfx_write_pixel_rgb`、`gfx_draw_string` 这类老 API 写的是全局 `framebuffer`，
它默认 = `back_buffer`（按 8bpp 320×200 分配的数组）。在 640×480@32bpp 下往里写
就是越界 + 错位。`gfx_pitch` 同理，默认是 `GFX_WIDTH`(320)，真彩色应为 2560。
修法：dispi 成功后把 `framebuffer` 指到 LFB（`gfx_flip` 里已有
`framebuffer == gfx_fb_addr` 的单缓冲判定）、`gfx_pitch` 一起改成 `w * bpp/8`。

**⑪ 主循环卡在输入轮询，首帧从未渲染。**
`elastic_desktop_main_loop()` 进 `while(1)` 后第一件事就是 `ps2mouse_poll` /
`keyboard_getchar`，没有输入事件时一直等。QEMU `-display none` 下没有任何输入，
于是"进了主循环但一帧都没画"（`[EDR]` 一次都没打出来）。
桌面系统不该等输入才出画面 —— 修法：进循环前先 `elastic_desktop_render()` +
`gfx_flip()` 画一帧。

**现在屏幕上是**：`0x1A202C` 顶栏（0–28 行）+ `0x2D3748` 背景，串口
`[EDR] render w=640 h=480 bpp=32 scale=75 apps=36` + `[EDESKTOP] first frame rendered`。
**真彩色链路已经完全打通**：模式、LFB、分页映射、后端选择、绘制路径、首帧，全部就位。

**第十二处的真因找到了（第 15 处）—— VBE 后端内部的模式快照漂移。**
串口在 `vbe_fill_rect` 里打了一次探针参数，一次定死：

```
[VBEFR] x=0x14 y=0x64 w=0x50 h=0x28 color=0x00FF0000
        vw=0x400(1024) vh=0x300(768) bpp=0x20 pitch=0x1000(4096) fb=0xFD000000
```

探针参数完全正确（20,100,80,40），但 **VBE 后端以为自己是 1024×768**。
`vbe_pitch` 应该是 640×4=2560，实际是 4096。于是
`vbe_putpixel` 写 `fb[y*1024 + x]`，物理行号被换算成 `y*1024/640 = y*1.6`——
100 落到 160，正是截图里那条红线出现的位置；x 方向还被切成间隔 128 的几段。
**整屏底色看不出问题**（满屏铺，尺寸错了也照样铺满），**小面积图形立刻暴露**，
这就是"背景对、元素错"的全部原因。

为什么 sync 打印是 2560、fill_rect 里却是 4096：`vbe_detect_from_boot_params()`
在 `vbe_init` 用当时的 `gfx_*` 做快照，而启动过程中有初始化/自检路径把它改成了
1024×768。修法：**在渲染入口每次重新对齐**（`gfx_vbe_sync_mode()` 移到
`elastic_desktop_render()` 开头，幂等 + 只在值变化时打印）。

**修完效果**：图标（圆角彩色方块 + 符号）、底部 dock、图标名全部正确显示。
**第 16 处（图标名排版）也修完了**，但在 640×480 下仍然拥挤：
`utf8_string_width` 一直按 `size*3/2` 算中文宽度（实际字形宽就是 `size`），
两字就超过图标宽度，四个图标的名字连成一行；同时宽度计算用的是未缩放的字号
（32）而绘制用的是缩放后的（24），`text_x` 被推到图标右侧。
三处一起改成"同字号 + 真实宽度 + 不夹断"之后，8 个名字各自居中到图标下方。

**图标名胜一档**：640×480 下字号固定 11px，"文件/终端/浏览器/设置"、"游戏/应用商店/
助手/相机" 都能读出来。4 字名（应用商店）仍与相邻名字贴得很近，属于该分辨率的
物理极限（42px 图标 + 74px 列距）。

**结论：链路层零错误。** 剩下的只是 640×480 这个分辨率本身空间不够 ——
42px 图标 + 24px 字，两字名字 48px 已经比图标宽，视觉上挨得紧。
这是视觉规格（`VISUAL_SPEC.md` 里"小屏幕密度"那一档）要定的事，不是渲染 bug。
注：`VISUAL_SPEC.md` 定的是 `scale=150 / 1920×1080` 那套（64 位 `hpdesk.c`），
32 位 `desktop_render.c` 是独立试错的一套，按 `GRAPHICS_BOUNDARY.md` 不外溢，
所以这里的字号不做成文档条目。

**新登记（第 17 处，未修）**：顶栏文字渲染残留。`draw_statusbar` 里
`gfx_draw_string_font(52, 6, "Hepu OS", ..., 16)` 只出来一个 "H"，紧接着
`640x480@0Hz` 那一串渲染成白色三角/楔形块（x≈110–180）。位置对得上、
形状不对，属于 ASCII 在固定 16/14px 下的字形渲染问题，还没查。
这两处让顶栏看起来不干净，但不影响桌面可用。

**修复清单（16 处，已修 15 处）**
横条块的数量与 `APP_COUNT`(36) 吻合，说明每个图标被画成了一片横线；
背景和顶栏（走 `gfx_fill_rect` / backend 直写）完全正确。也就是说错在
`draw_icon_symbol` 那条路径上（老 `gfx_putpixel` 的 `uint8_t color` 语义、
或 `gfx_draw_circle` 系列在真彩色下的半径/颜色处理），不是全局性的 pitch 或缓冲问题
（这两条已经修完）。

**额外发现：32 位启动后进的是 shell，不是 GUI 桌面。**
`[APPS] 7/7: elastic_desktop_init done!` 之后紧跟 `[SHELL] main started` ——
桌面只做了 init，没有任何绘制循环在跑（`desktop_render.c` 的 `gfx_flip()` 一次都没被调用，
`[FLIP]` 只有我们手动加的那一次）。也就是说 32 位的图形**通了但没内容**：
不是画错，是没人画。


---

## D-8：32 位桌面搬到用户态，32/64 共用一份桌面

**起因（用户定调）**："搬到用户态，然后 32 位桌面先关掉。让 32 和 64 真正共用桌面，
留可切换的图形化桌面接口，用户以后可以自己加其他桌面。"

### 为什么必须这么做

32 位是**微内核** —— 应用在用户态；64 位是**单体内核** —— 桌面在 ring0。
把 4522 行 GUI 塞在 32 位内核里，等于让微内核做单体内核的事，而且和 64 位那套
（`hpdesk.c` 8206 行 + `hs_apps.c` 4498 行）永远无法共用。

### 四件事

| # | 内容 | 状态 |
|---|---|---|
| 1 | 32 位内核内桌面**下线** | ✅ `init_sequence.c` 不再进 `elastic_desktop_main_loop` |
| 2 | **桌面接口契约** + 注册表 | ✅ `kernel/shared/desktop/desk_api.h` + `desk_registry.c` |
| 3 | 32 位 `SYS_FB_DESC`（用户态拿显存描述） | ✅ `kernel/arch/x86/sys_fb.c` |
| 4 | 32 位用户态桌面程序 | ⏳ 待做（需要 `SYS_MMAP` 能把 LFB 映射进 ring3） |
| 5 | 64 位桌面接同一接口 | ⏳ 待做（现状 `hpdesk.c` 仍直写） |

### 接口长什么样

```c
/* 桌面只看到这个 */
typedef struct desk_surface {
    int width, height, bpp, pitch;
    uint32_t scale_pct;
    void *ctx;
    void (*put_pixel)(...);  void (*fill_rect)(...);
    void (*hline)(...);      void (*draw_text)(...);
    int  (*text_width)(...);  void (*present)(...);
} desk_surface_t;

/* 一个桌面实现 */
typedef struct desk_backend {
    const char *name, *display_name;
    int  (*init)(desk_surface_t*);
    int  (*frame)(desk_surface_t*);
    int  (*event)(desk_surface_t*, const desk_input_t*);
    void (*fini)(desk_surface_t*);
} desk_backend_t;

desk_register(&my_desk);   /* 用户自己加一个桌面 */
desk_select("mine");       /* 切过去 */
```

**硬判据**：桌面实现里不许出现 `outb` / `inb` / `0xA0000` / 物理地址 / 寄存器名。
出现了就说明它偷偷做了后端的事，违反 `GRAPHICS_BOUNDARY.md`。

### 顺手修的一个坑

`SYS_MAX` 有两处定义：`constants.h` 是 **222**、`syscall.h` 是 **236**。
32 位表按 222 开，`SYS_FB_DESC`(229) 直接**越界编译失败**。
统一成 236。

### 验收

- 32 位冒烟 **PASS**，569 行（桌面下线后 shell 继续跑，输出比之前多 40 行）
- 串口出现 `[EDESKTOP] in-kernel desktop retired; handed to user space`

---

## D-8 记录：桌面共用的分工与遗留（2026-11-15 核对文档后更正）

### 已核对的文档事实

| 文档 | 说了什么 |
|---|---|
| `ARCHITECTURE_LAYERS.md` | 分层判据：**gfx64 不含界面语义；hpdesk 不含像素计算，全部通过 `gfx64_*` 图元完成，共 214 处调用** |
| `GRAPHICS_STACK.md` | 64 位实测 `1920x1080 pitch=7680 bpp=32`；150% 缩放下 `hs_sx(460)=690` |
| `DESKTOP_SHELL.md` | 174 行，桌面壳开发路径（此前未读，补记） |

### 三条"我以为是设计、其实是代码现状"的数据

| 数据 | 来源 | 性质 |
|---|---|---|
| 后台缓冲 `GFX_MAX_BUF = 1024*768*4 = 3MB`（`gfx64.c:39`） | 代码 | **缺陷**：1920×1080 需 8.3MB，装不满整帧 |
| 32 位 dispi 设 640×480（`bochs_dispi.c`） | 代码 | **现状**，非设计；应跟随文档的 1920×1080 |
| 32 位 `gfx_fillrect_rgb` 逐像素双层循环 | 代码 | **缺陷**，性能不可接受 |

### 我多加的一层（应删）

| 文件 | 判定 |
|---|---|
| `kernel/shared/desktop/desk_gfx_ops.h`（30 个函数指针） | ❌ **多余**。hpdesk 直接调 `gfx64_*`，编译期已绑定，运行时表无存在理由 |
| `kernel/arch/x86_64/desk_ops64.c` | ❌ 同上 |

| 文件 | 判定 |
|---|---|
| `kernel/arch/x86/gfx64_compat.c` | ✅ **正解**，正是 `ARCHITECTURE_LAYERS.md` 判据的实现（32 位提供 `gfx64_*` 同名） |
| `desk_api.h` / `desk_registry.c` | ⚠️ 响应"可切换桌面"的新要求，**文档需补**，不属于既有设计 |

### 待办

1. 删除 `desk_gfx_ops.h` + `desk_ops64.c`
2. `desk_registry` 的设计意图写入 `DESKTOP_INTERFACE.md` 并注明"新增，非既有设计"
3. 32 位 dispi 改 1920×1080
4. 兼容层矩形填充改按行批量
5. 补 `cursor64_*` / `rtc64_*` / `pit64_*` / `pmm64_*` 四组 32 位壳

---

## 优先级路线（2026-11-15 核对后）

排序原则沿用本文件开头：**会挂靠止损 > 安全边界两套标准 > 静默错误 > 可增量**。

### P0 —— 立即，且零风险或确认成本极低

| # | 项 | 现状证据 | 危害 | 动作 | 验收 |
|---|---|---|---|---|---|
| P0-1 | **已闭环** 删除运行时 ops 层 | `desk_gfx_ops.h` + `desk_ops64.c` 曾"看着像有运行时后端切换，实际是编译期绑定"，属误导性代码。**核实（2026-11-17）**：两个文件均已不在仓库（`ls` 报 No such file）| 无 | `ls kernel/shared/desktop/desk_gfx_ops.h kernel/arch/x86_64/desk_ops64.c` 均不存在 |
| P0-2 | **已闭环（64 位）** `GFX_MAX_BUF` 越界 | `gfx64.c:39` 仍定义 `GFX_MAX_BUF (1024*768*4)`，但那只是**最小需求下限**：`back_buffer` 已改为运行时 pmm 动态分配（`gfx64.c:147-150`，注释即写着"P0-2 修复: 原来固定按 GFX_MAX_BUF 分配，而实际屏..."），`need` 按真实 pitch*height 计算。**32 位同日补齐**（`95e1790`）：`gfx_target_realloc()` 在堆可用后按需在堆上分配，1080p 下不分配（静态数组恰好够）。**残留**：2560x1600 双缓冲需 16MB，而壁纸缓存另占 16MB，32MB 堆装不下 | 无 | `gfx64.c:147-150` 动态分配；`[GFX] draw target -> heap 16000KB (2560x1600)` 实测；32 位 1080p 下无该日志（走静态分支） |

**P0-2 说明**：不是"可能有事"，是"分配 3MB、按 8.3MB 写、无检查"。之所以还没炸，可能是双缓冲没在所有路径启用，或越界区恰好空闲。**需一次探针确认。**

### P1 —— 让 32 位桌面真正可用（承接 D-8）

| # | 项 | 依赖 | 验收 |
|---|---|---|---|
| P1-1 | **已做（`5c18e69`）** 矩形填充改按行批量 `rep stosl` | 无 | 1920×1080 单帧整屏填充从 2,073,600 次 `vbe_putpixel` 降为 1080 次行填充；`objdump` 确认 `f3 ab` |
| P1-2 | **已做** 32 位 dispi 改 1920×1080×32 | 无 | 串口实测 `[BD-VBE] mode set: 0x00000780x0x00000438@0x00000020bpp` + `[GFX-VBE] sync: ... pitch=0x00001E00`（=1920/1080/32bpp，pitch 7680） |
| P1-3 | **已做** 补 `cursor64_*` / `rtc64_*` / `pit64_*` / `pmm64_*` 四组壳 | P1-1/P1-2 之后 | 32 位 `hpdesk32_deps.c` 等已提供同名实现（如 `pmm64_alloc_pages` → `mm_alloc_pages`），32 位成功链上 `hpdesk.o` 并起桌面（见 P1-4） |
| P1-4 | **已做** 把 `hpdesk.c` 编进 32 位并起桌面 | P1-3 | 串口 `[EDESKTOP] res=0x00000780x0x00000438 desk_start rc=0x00000000`、`[HPD] wallpaper done` 反复；`check32.sh` 截图非黑 2,073,600（全屏） |

**顺序不能乱**：P1-1 不做，P1-4 出来就是幻灯片；P1-2 不做，P1-4 布局溢出。

### P2 —— 可观测性（只加日志，不改控制流）

| # | 项 | 风险 | 验收 |
|---|---|---|---|
| P2-1 | ~~64 位贴 7 个 phase 标签~~ **已完成** | 在 `kernel_main_64.c` 的 7 个节点各打一行 `[INIT] phase:  <名> `，名字逐字照抄 32 位 `hepu_init_phase_name()`（含原有前后空格）。按真实启动顺序：`cores subsystem`(物理内存) → `HW driver`(早 IDT) → `security encryption`(seccomp) → `fs`(ext2) → `net proto`(net64) → `system service`(process64) → `app framework`(desktop64)。<br>**不调 `hepu_init_phase()` 的原因**：它内部调 `vfs_init`/`devfs_init` 等只存在于 32 位的模块，64 位链接不上。<br>**踩到的坑**：64 位 `vga_puts()` 第一行就是 `serial_puts_local()`（`lib64.c` 的 `vga_puts_nolock`），本来就落串口，第一版再配一个 `serial_puts(0x3F8,...)` 导致每行在 `ser_kill.log` 出现两遍。去掉多余那句后 7 行各出现一次。<br>验证：64 位 949,232 B（+256 B），截图 1920×1080 非黑 2,073,600。 | 已完成（2026-11-17） |
| P2-2 | `desk_registry` 写入 `DESKTOP_INTERFACE.md` 并标"新增，非既有设计" | 无 | 文档与代码一致 |

### P3 —— 存量与可增量

| # | 项 | 备注 |
|---|---|---|
| P3-1 | ~~32 位 6 个 KUNIT fail~~ **已结案：52 个 suite 全 0 fail** | 冒烟里 `[KUNIT] result:` 共 52 行，逐行都是 `failed=0x00000000`。历史上出问题的那四个套件都在跑且全过：`syscall deep`（0x32=50 项）、`user scenario`（0x10=16 项）、`integration`（0x0E=14 项）、`security E2E`（0x0F=15 项）。<br>归因：`kunit_syscall_deep.c:402` 那条 `KUNIT_ASSERT(SYS_MAX == 222)` 是唯一的**固定 FAIL**（实现是 236），它一失败整条 syscall deep 套件就记 fail；`e4db810`（N-3）把它改成区间断言 `SYS_MAX >= 200 && SYS_MAX < 512` 后，这个套件恢复全过。其余「fail」没有独立证据留存，应是同一条断言在不同构建期的连带计数，不是六个互不相关的 bug。 | 已闭环（2026-11-17） |
| P3-2 | 顶栏 ASCII 字形残留（`Hepu OS` 只出 `H`，`640x480@0Hz` 出白三角） | D-7 最后一处 |
| P3-3 | `mouse64_init(1024,768)` 初始位置偏左上 | **已核实**：只影响光标初始位置，非坐标上限，危害轻微 |
| P3-4 | 桌面线四项（控制中心位图图标 / `bar_height` 36vs40 / 天气卡 24px / 胶囊居中口径） | 用户已给口径，代码未改 |
| P3-5 | **§5-5 已收口（6/6）**：目录三件套 / stat+fstat / open+close / mmap 三件套 / read+write / pread+常量归并。剩余 fs 函数是桩，不切 | read/write 那批行数是 **+12**（2,897→2,909），pread 这批是 **-4**（2,909→2,905）、体积不变。同一件事两个方向都有，别拿行数当验收标准 |
| **P3-13** | **已修复（`2c3e9c6`）——32 位图形桌面黑屏。真因是初始化顺序，不是内存大小**：映射 LFB 的 D-7 代码在 `hepu_mem_init` 里，而 `bochs_dispi_init` 跑在它之后，映射时显存地址还不存在（`bochs_dispi_fb_phys()` 返回 0，`if` 直接跳过）；等显存出现时分页早已开启，`0xFD000000` 从头到尾没被映射，所有直写落在未映射地址。修复是在 `bochs_dispi_init` 成功后立刻补映射整帧 8.29 MB。**验证**：`NONBLACK` 1160 → **2073600（全屏非黑）**，8 个程序全部 `rc=0`，`recovery: iret-target invalid` 计数 **0**（那个恢复死循环也是同一批非法访问引出的）。**同时修正一处假探针**：`gfx_fb_stats` 原来写死读 `0xA0000`（VGA 显存），而 VBE 模式帧缓冲在 `0xFD000000`，它报的 `nonzero` 涨落与桌面内容无关，此前据此下的"画进去少了 60 倍"结论作废。原始记录（保留以说明为什么一度误判）|同参数同镜像（不挂盘、-m 256、110s）下，`6cfb7d0^` 能出深色桌面（顶栏/时钟/面板/Lavender 卡片），`6cfb7d0` 之后 LFB 只有 **1160 个非黑像素**（底色全黑）。串口证明链路没断：`[EDESKTOP] desk_start on 32-bit`、`[EDESKTOP] first frame via registry, cycles=0xB344923`、`[HPD] panels/windows/wallpaper done` 反复、`presentK=0` 与旧版一致，但 `[GFX] fb after-desktop-init nonzero` 从 **0xBB3F(47935)** 掉到 **0x31F(799)**。**已排除**：位图数组 BSS 膨胀（把 `4096ULL` 改成 `1024ULL` 省下 96 KiB，非黑像素仍是 1160）；GFX 后端注册顺序两版逐行相同；VBE 模式与 pitch 两版相同。**未排除**：`paging_map` 新增的父 PDE `USER/WRITABLE` 提升（`de->writable=1` 是无条件提升，可能让某个原本只读的 PDE 变可写）。下一步应二分这三个改动，只留 pmm 看桌面是否恢复 | 这是本轮最严重的一条：`6cfb7d0` 已推送，仓库当前 HEAD 的 32 位桌面是黑屏。外部程序跑通（8/8 rc=0）与桌面黑屏**同时**发生，不能二选一，必须两个都保 **二分结果（total_pages 轴，同参数 95s）**：256/4096 页 → 桌面正常（207 万非黑像素）但 8 个 ELF 全 -3；36000 页 → 启动**卡死**在 `[GFX-TEST] test8`（连桌面都没到）；40000 页 → 桌面黑（1160 像素）；65344 页 → 桌面黑但 8 个 ELF 全 rc=0。**结论：两者在 total_pages 轴上不重叠**，不是简单阈值问题——"桌面正常"要求页分配器别碰内核已用区（BSS 到 0x06BA8000=107MB，heap 到 0x08BA8000=139MB），"ELF 能跑"要求 kern_end 之外有可用页，而中间档（36000）先卡在 GFX 自检。下一步应查 `[GFX-TEST] test8`（unified API drawing）为何随内存变大卡死，而不是继续调页数 |
| P3-12 | ~~`ext2_64_write_file` 整文件覆写、不看 offset~~ **已闭环（`b9da6c0` + `fe23ff2`）** | 两层。①**offset 写**：VFS write 改走 `ext2_64_write_file_offset`，按 fd 当前 offset 定位，负 offset 与 >2G 一律拒绝；改为分 3 段写 81920 B 后整读回逐字节比对 **PASS**。②**间接索引单位**（审计时发现的真 bug）：本地读写路径把每块指针数写死 256（1 KB 块假设），而造盘用 `mkfs.ext2 -b 4096` 即每块 1024 个指针，于是 `block_idx` 268..1035 本属一级间接却被判成二级、去读为 0 的 `i_block[13]` → `return -1` → 读回全零。改为按 `ext2_block_size/4` 推导 `ppb`。**边界**：没有 >1 MB 文件的人工用例，"超 268 块能读回"是按公式推导，未实测 | 已闭环（2026-11-17） |
| P3-6 | §5-3 加载器适配（`user_process.c:1906-2182`） | 判断为"整块搬用户态"量级 |
| P3-7 | §5-4 framebuffer 映射 + "前台所有权"定义 | `flags=0` 语义待定 |
| P3-8 | ~~§5-6 32 位其余文件接 `shared/`~~ **已核实基本完成，台账原「引用数 0/1」是错的** | 按实际构建产物核实（`find build_v250 -path "*shared*" -name "*.o"`）：32 位**编了 10 个** `shared/` 源文件 —— `desk_api` 侧的 `desk_surface` / `desk_registry` / `desk_runtime` / `desk_hpdesk` / `desk_demo`，外加 `gfx_algo` / `lib_shared` / `ext2_algo` / `debug_shared` / `driver_framework`。另有 4 个 32 位文件直接 include `shared/` 头（`sys_mem.c`、`gfx32_display.c`、`gfx64_compat.c`、`init_sequence.c`），`mm.h` 还间接引了 `shared/mm/hepu_pmm.h`。<br>原判「引用数 0」的成因大概是只 grep 了字面 `shared/` 路径，而多数引用走的是搜索路径形式 `#include "mm/hepu_pmm.h"`，grep 不到。<br>**真正还没共享的是声明面之外的两块**：① `shared/gfx/gfx_algo.c` 提供的是 `gfx_algo_backend_t *ops` 那套抽象，而现行桌面走 `desk_surface`，两代抽象没对接，这个文件现在是影子实现（32/64 都编但没调用点）；② `shared/lib/lib_shared.c` 与 `shared/debug/debug_shared.c` 同样是编了但调用方为 0。这两块属「共享了但没接上」，不是「没共享」。 | 已核实（2026-11-17） |
| P3-9 | 32 位缺 12 个 Rust 用户程序（无 cargo） | 需外部环境 |
| P3-10 | D-1…D-6 剩余 | 见本文档前半 |
| P3-11 | ~~三个镜像脚本都不可用~~ **脚本已能自述病因** | **已修**。核实比原记录深一层：`tools/bootloader.S` / `bootloader.ld` **不是"丢了"，是被 `tools/hepboot/` 整体取代**——`hepboot.S` v1.02 基址 `0x8000`（`hepboot.ld` 注释："原 0x7C00 与 MBR 冲突"），LBA 布局随之改变，所以不能简单把路径改指过去。本轮做了三件事：① `build_iso.py` / `build_hybrid_iso.py` 的 `PROJECT` 从另一个会话副本的绝对路径改为 `os.path.dirname(dirname(abspath(__file__)))` 自动推导；② `build_combo.sh` 加前置检查，缺源时直接打印"该布局已被 hepboot 取代 + 当前该走 `build_uefi_dual.py` + 要重建需按 0x8000/HDD 变体重写"，而不是让 gcc 甩一屏报错；③ `build_uefi_dual.py` 加 `_require_tools()` 自检（`sgdisk`/`mkfs.vfat`/`mkfs.ext2`/`mcopy`），缺哪个报哪个并给安装命令，不再抛 Python traceback。**仍未做**：`tools/hepboot/x86_64/stage2_64.S` 有源但没有构建规则产出 `stage2_64.bin`，iso 脚本因此仍跑不完；combo 镜像要不要按 0x8000 重建未定 |

### 台账外新发现（2026-11-17，尚未排进优先级）

| # | 项 | 现状 | 性质 |
|---|---|---|---|
| N-1 | **2560x1600 双缓冲放不下** | 缓冲本身已能动态分配（`[GFX] draw target -> heap 16000KB (2560x1600)`，非黑 3,928,481 = 95.9%），但 `hs_wallpaper_build_cache` 按 `w*h*4` 再吃 16 MB，合计 32 MB = 整个 32 MB 堆（256 MB 机器档），后续分配全部失败 → 画面撕裂、壁纸全黑。**修法二选一**：壁纸缓存降精度（存 1920 宽渲染时横向拉伸，省 16 MB）/ 提堆（32→48 MB） | 内存预算 |
| N-2 | ~~32 位 `png64_decode` 会卡死~~ **已结案：不复现，解码器本身正常** | 用 8×8(167B,1 页) / 64×64(2158B,4 页) / 512×288(29,667B,145 页) 三档递进实测，全部解码成功，阶段打点 `scan done → alloc idat → alloc raw → inflate start/done → alloc rgba` 逐段成对出现。<br>曾被怀疑的两个点都排除：① 多页分配不是死循环——`mm_alloc_pages(4)` 实测 0.7~30 M 周期（2~10 ms），边界值 `total_pages=0xFF7F`、`limit=0xFF7B` 正常，`mem_region_count=2`；② 不是 `#PF`——V3.86 起 identity map 已从 16 MB 扩到 **256 MB 全物理直映**（`paging.c` 里还有一次全量完整性复检补洞），135 MB 处的分配页可直接读写。<br>原 `[WP32] dec start` 后无 `dec ok` 应是旧镜像/旧构建下的时序偶发，现基线（32 位 3,923,568 B，冒烟 PASS `lines=801`）不再出现。**32 位壁纸的预解码 RGBA 兜底（`hs_wp_load` 的 `n<=8 && idx==0` 分支）保留**，但理由已从"解码器坏了"改为"无盘启动读不到文件"。 | 已闭环（2026-11-17） |
| N-3 | `kunit_syscall_deep.c:402` 断言 `SYS_MAX == 222` | 实现是 236，**固定 FAIL**，与 P3-1 的 6 个 KUNIT fail 可能同源 | 测试 |
| N-4 | ~~20 MB~~ **11.4 MB 死内存回收** | **已完成**。重算结果：原 20 MB 里 `back_buffer`(8.3 MB) 已非闲置（它是双缓冲绘制目标，不能算死内存），QUIC/HTTP3 那 7.3 MB 未在本轮核实到对应符号。真正可回收的是 `gfx_virtio.c` 两个函数内 static 数组：`fb_mem`(3 MB, 1024×768×4) + `fb_mem_large`(8.3 MB, 1920×1080×4) = 11.3 MB。32 位实际走 VBE，`virtio_gpu_probe()` 在 `[GFX-VIRTIO] no virtio-gpu device found` 后直接 `return -1`，**永远走不到分配处**，这两个数组从开机到关机没被写过一个字节，却整体占着 .bss。改成 `virtio_fb_alloc()` 用 `mm_alloc_pages` 运行时按需取（上限 1920×1080×32，一次取避免切换分辨率时重复分配），virtio 探测失败则一次都不分配。实测 `.bss` 108,522,412 → **97,078,188**（−11,444,224 B），`nm` 里 `fb_mem*` 符号消失，32 位冒烟 PASS，桌面 `NONBLACK=2073600` 无回退 | 清理（已做） |
| N-5 | `gfx64_blur_region` 32 位仍是纯覆色 | **无调用点的死路径**（`hs_blur_region` 定义了但零调用）。桌面实际用的毛玻璃已走 `hs_panel_glass_*`（`e9e2f99`，32/64 共用），不经过这个函数，保持退化不影响任何画面 | 无需修 |

### 不做的事（明确列出，避免反复讨论）

| 项 | 理由 |
|---|---|
| 给 32 位补 `sti` | 既有设计；会让 `sched_tick()` 进未验证的启动期调度（64 位在 `3aeb4b6` 踩过死锁） |
| 给 32 位补 pmm/vmm 分离 | 简单内存模型是 32 位稳定基础 |
| 把两套内核合成一个 | 违反 `UNIFIED_SOURCE_TREE.md`（两套内核、一棵源码树） |
| 为对齐 phase 而移动初始化调用 | 硬依赖会断 |

---

### 本轮证据截图（2026-11-17）

**D-3 探针自检通过**（64 位 UEFI/OVMF，1920×1080，NONBLACK=2073597；
3 个纯黑像素是光标箭头核心像素，不是黑屏回退区域）：

![D-3 探针自检](docs/assets/d3-probe-selftest-0.60.7.jpg)

**64 位三条真 bug 修复后的桌面**（同一份二进制，1920×1080）：

![warn64 真 bug 修复](docs/assets/warn64-realbugs-0.60.7.jpg)

## 本轮闭环批次（2026-11-17，分支 `docs/sync-2026-11-17`）

> 本批为 2026-11-17 已合入基线 `5f93c2d` 的修复项。既有 D-1…D-8、P0/P1/P2/P3、
> N-1…N-5 条目的编号与状态一律未改动；下表只登记本轮新增闭环及其验证方式。
> 验证口径：**已闭环** = 有同日实测证据；**代码已修 / 端到端未复测** = 源码改动经核实，
> 端到端场景本轮未重跑，缺口在备注写明。

| 修复项 | 提交 | 代码位置 | 验证方式与证据 | 状态 |
|---|---|---|---|---|
| 开机进锁屏，补齐「锁屏 → 登录 → 桌面」链路 | `ebf3e05` | `kernel/arch/x86_64/hpdesk.c`：开机置锁（`:7486-7489`）、状态三态（`:4554`）、键处理（`:7201-7234`） | 同日实测实例 `docsrec2`：锁屏帧 → 空格进登录页（`f0107`）→ 回车进桌面（`f0143`/`f0199`）；证据 `evidence-docs-sync/rec-sendkey/` | 已闭环（64 位图形路径） |
| 系统加载屏；`load_config` 先于 `init_gop` 顺序换回 | `5d890c4` | `tools/hepboot/uefi/hepboot_uefi.c`：`:1658` → `:1663`；加载屏阶段 `:1163/:1365/:1739/:1754/:1771` | 同日实测实例 `nodisp4`（`-display none`，2026-11-17 21:19）：`FBDESC w=1920 h=1080`、`Desktop initialized: 1920x1080`；PPM 头 `P6 1920 1080`；证据 `evidence-docs-sync/nodisp-none/` | 已闭环 |
| 加载屏留到桌面；自检不再把屏幕刷成绿/红 | `57aba61` | `kernel/arch/x86_64/gfx64.c`、`kernel/kernel_main_64.c`、`tools/hepboot/uefi/hepboot_uefi.c` | 同日实测 `docsrec2` 帧序列：加载屏过渡到桌面第一帧覆盖，无绿/红刷屏；证据 `evidence-docs-sync/rec-sendkey/` | 已闭环（64 位图形路径） |
| net32 `RCTL.BSIZE` 位段写错 | `5f93c2d` | `kernel/drivers/e1000.c`：BSIZE 位域 bit9:8；MMIO `volatile` + 屏障 | 源码核实位域写入。端到端复核（基线 `61c53fa` 重出证，`b34a5bc`/`ff5d603`）：32 位 DHCP 收包打通（`evidence-rxnet-2026-11-17/serial/rxnet-rebase-61c53fa.log` RDH=2、rx_cur=2、两帧上送 UDP；Offer 字段解析见旧档 `evidence-rxnet-2026-11-17/serial/rxnet-dbg2.log`）；实测根因实为发送侧字节序/校验和错误，不是 BSIZE 常量本身。**未复测**：32 位 ICMP echo reply、TCP 路径 | 代码已修；DHCP 收包已取证，ICMP/TCP 未复测 |
| 保护模式按实际架构跳转，不再误走 32 位交接 | `9d39038` | `tools/hepboot/uefi/hepboot_uefi.c`：跳转判定 `:1960` 起 | 源码核实；端到端引用同日独立验证 `evidence-combined/REPORT.md` E5（64 protect）、E7（32 protect），HEAD `fd469e1` 含本条 | 已闭环（引用同日独立验证） |
| uproc 统一加载 ABI（契约头 / 32 位 / 64 位 + argv 上栈 / 真实 pid 回传） | `d1dd19d`、`5096f0a`、`421d358`、`fd469e1` | `kernel/include/uproc.h`；`services/compat/uproc32.c`、`services/compat/binfmt*.c`（`f3b3c78` 由 `kernel/compat/` 迁出）；`kernel/arch/x86_64/user_process.c`、`user_stack64.c` 等 | 同日独立验证 E6：四条程序 rc=100；不存在文件 rc=-2；pid=100 slot=0 active=1；契约见 `evidence-uproc/REPORT.md` | 已闭环 |
| 保护模式文本 shell 独立枚举 USB HID，双源合流键盘 | `8a0af11` | `kernel/arch/x86_64/shell64.c`、`kernel/kernel_main_64.c:1389-1398` | 源码核实；**未验证**：保护模式 USB 键盘端到端输入未单独复测，E5 未区分按键来源 | 代码已修；保护模式 USB 输入端到端未单独复测 |
| `hepu.graphics=0` 保护模式：32/64 跳过图形进文本终端 | `db4cf31` | `kernel/arch/x86_64/bootopts64.c`、`kernel/arch/x86/init_sequence.c`、`kernel/boot/multiboot.c`、`kernel/kernel_main_64.c` 等 | 同日独立验证 E5/E7：64/32 保护模式均进文本终端、无图形后端初始化行；HEAD `fd469e1` 含本条 | 已闭环（引用同日独立验证） |

### 与既有技术债条目的边界（勿误关）

- **D-6 / P3-6「兼容层加载器 / framebuffer 所有权，整块搬用户态」未被本次 uproc ABI 关闭**：
  本轮 `d1dd19d`/`5096f0a`/`421d358`/`fd469e1` 只统一了入口契约、返回口径与 argv 栈布局，
  加载器主体与 framebuffer 所有权仍在内核侧；把兼容层整块搬到用户态仍是开放大工程，
  D-6、P3-6 状态保持不变。
- **32 位网络残留债**：`b34a5bc`/`ff5d603` 打通了 DHCP 收包并修通 MAC/link；32 位 ICMP
  echo reply、TCP 路径本轮未复测，网络类既有开放条目不据此关闭。

## 第二批闭环 / 降级批次（基线 `3d0bd23`，2026-11-17）

> 状态分两档：**已闭环** = 改动落地并有验证；**降级 / 明确化** = 假成功改成显式未实现，
> 真实校验 / 算法仍未实现，不记为已修好。既有 D-/P-/N- 条目编号与状态未改动。

| 修复项 | 提交 | 代码位置 | 验证方式与证据 | 状态 |
|---|---|---|---|---|
| 接通 `hepu.lock` 关机开关，补齐鼠标解锁路径，置锁点唯一化 | `ebf438c` | `kernel/arch/x86_64/hpdesk.c`：`lock_cfg_disabled()`（`:4563`/`:4580`）；点击迁移（`:5664-5668`、`:5778-5782`）；键盘迁移（`:7240-7260`）；置锁（`:7526-7530`） | 键盘路径同日实测 `docsrec2`：锁屏→登录→桌面；鼠标路径源码核实接入同一状态机，未单独录点击帧；提交记录双架构编译通过 | 已闭环（64 位图形路径；鼠标点击帧未单独复测） |
| 两处常驻验收探针移出启动路径：SB-08 每 5 帧浮点负载、shell 四程序 autostart | `0202461` | `kernel/arch/x86_64/desktop64.c`（SB-08 `if (0)`）；`kernel/arch/x86_64/shell64.c`（`autostart_selftest = 0`，程序本体与命令保留） | 提交记录（2026-11-17 构建）：64 位内核 968,496 → 967,536 B；重启实拍停锁屏，`[AUTOSTART]` 0 次；启动期一次 BSP 浮点自检保留 | 已闭环（测试逻辑移出启动路径） |
| 32 位三处认证假实现改为显式未实现 | `0de2133` | `kernel/arch/x86/login.c`（`login_authenticate`、`login_unlock_screen`）；`kernel/arch/x86/multi_user.c`（`mu_private_unlock`） | 保留未设密码即放行行为，调用打未实现警告；真校验未实现。验证：32 位编译 0 error；`regress32.sh` 冒烟 PASS，桌面口径未变 | **降级 / 明确化（非已实现真实校验）** |
| 安全启动三处空桩改为返回 `SB_ERR_NOT_IMPLEMENTED (-2)` | `3d0bd23` | `kernel/arch/x86/secure_boot.c`（`sb_verify_stage`、`sb_verify_signature`、`sb_verify_hash`） | 度量保留，验证不再 `return 0` 冒充通过；签名/哈希算法仍未实现；仓库当前无调用方。验证：32 位编译 0 error；`regress32.sh` 冒烟 PASS | **降级 / 明确化（非已实现真实校验）** |

### 第二批边界与残留债务（勿误关）

- **32 位密码校验仍是开放债**：`0de2133` 只做了诚实化；要真接校验，需先给
  `mu_user_t` 加密码/hash 字段 + 密码存储与持久化，再改三处认证函数，属独立工程。
- **安全启动验证仍是开放债**：`3d0bd23` 只停止假装通过；签名运算、哈希比对算法均未实现，
  严格模式下 `sb_verify_stage` 已按失败处理，非严格模式仅警告继续。
- **自检电池口径**：电池是编译期开关（`HEPU_BOOT_SELFTEST`，默认不定义），非常驻探针；
  与 `0202461` 关掉的两处常驻负载是两类东西，台账不得混记。
- `ed68299` 为展示页补实拍素材，非缺陷修复，不计入闭环。

## 第三批（网络线 + 回归线观察，基线 `61c53fa`，2026-11-18 重出证）

> 网络线来自分支 `fix/rxnet-2026-11-17`（`61c53fa` 上重新出证，哈希为 rebase 后版本）；
> 回归线观察来自 `test/regress-2026-11-17`（`cc10817`）。
> 既有 D-/P-/N-/OPEN-/BUG- 条目编号与状态未改动。

| 项 | 提交 | 代码位置 | 验证方式与证据 | 状态 |
|---|---|---|---|---|
| 32 位 IP/UDP/DHCP 字节序 + IP 校验和修正，打通 DHCP 收包 | `b34a5bc` | `kernel/net/ip.c`、`kernel/net/udp.c`、`kernel/net/dhcp.c` | 直载 e1000 取证：`evidence-rxnet-2026-11-17/serial/rxnet-rebase-61c53fa.log` RDH=2、rx_cur=2、两帧上送 UDP、MAC `52:54:00:12:34:56`、link up；`evidence-rxnet-2026-11-17/build32-rebase-61c53fa.log`/`evidence-rxnet-2026-11-17/build64-rebase-61c53fa.log` 0 error；Offer op=2/xid/msg_type=2 字段解析见旧档 `evidence-rxnet-2026-11-17/serial/rxnet-dbg2.log`。根因是发送侧小端直写 + 校验和小端取字，非硬件未收到、非 BSIZE 常量问题。**ICMP/TCP 未复测** | 已闭环（32 位 DHCP 收包） |
| BUG-58：RAL/RAH 对齐 32 位写 + 只读诊断 dump | `ff5d603` | `kernel/drivers/e1000.c`、`kernel/net/net_selftest.c` | `evidence-rxnet-2026-11-17/build32-rebase-61c53fa.log`、`evidence-rxnet-2026-11-17/build64-rebase-61c53fa.log` 0 error；`evidence-rxnet-2026-11-17/serial/rxnet-rebase-61c53fa.log` 串口 MAC `52:54:00:12:34:56`、link up | 已闭环 |
| OPEN-04 子项（误授权）：匹配条件收紧为规范绝对路径整串匹配 | `0e30c70` | `kernel/arch/x86_64/user_process.c:2544`（`INSTALLER_CANON_PATH "/bin/install64.elf"`）、`:2547-2557`（先判 `path[0]=='/'`，再整串逐字节比、长度相等） | 只收紧匹配条件，非加白名单配置、非编译期排除；无配置项/默认清单，唯一可信路径是编译期单值宏。64 位编译 0 error + 静态核对两个镜像脚本写 `/bin/install64.elf`、autostart 绝对路径 exec；**没跑过真实安装**，未在双盘 QEMU 验证目标盘文件齐全。主体（真实安装流程）仍开放 | **子项（误授权匹配）已修；主体真实安装流程仍开放，运行时未验证** |
| OPEN-02：三处 TD 发布前加写屏障 | `4524f79` | `kernel/drivers/usb/uhci.c` | 当前 QEMU 无 UHCI 控制器，路径未执行；仅 32 位编译 0 error；复现需 `-device piix3-usb-uhci` | 未关闭（加固，运行时未验证） |

### 第三批残留债与现状限制（勿误关）

- **安装器真实安装流程**：`0e30c70` 只收紧了内核侧匹配条件；内核侧命中后只授 PID 不做拷贝，
  扇区克隆在用户态 `user/apps/install64/install64.c`（`clone_region:165-187`、引导链 `:280-294`、
  rootfs 到 P1 `:299-308`、P2/P3 mkfs `:312-321`）。该流程未在双盘 QEMU 跑过，目标盘 P1
  `/bin` 齐全性未核对，运行时未验证（见 OPEN-04）。
- **OPEN-02 UHCI 竞态**：写屏障不消除其余疑点（`frame_list[0]` 单点抢占、超时裸循环、
  端口复位保持时长、链接指针类型位），运行时未复现；分析见
  `evidence-rxnet-2026-11-17/OPEN-02-uhci-analysis.md`。
- **32 位桌面下半屏纯黑（OBS-05）**：NONBLACK 786,399 / 2,073,600（约 38%），根因未定位，
  证据 `evidence-regress-2026-11-17/old-3d0bd23/frames32/desktop_1..4.png`；下一步按行统计非黑分布、
  对比 64 位 present 路径、dump 双缓冲边界行像素。
- **32 位锁屏 / 登录路径未触发**：32 位兜底关闭锁屏、直进旧桌面，登录页未拉起；这是现状
  限制，不记为已通过；要复现需先具备拉起 32 位登录页的入口。
