# 技术债路线（2026-11-15）

> 只登记**已经确认存在问题**的条目，不写"将来也许要做的优化"。
> 每条必须带：现状证据、为什么算债、验收标准、回滚方式。
> 判断以代码和串口证据为准，不以本文档的规划为准。

## 0. 排序原则

1. **会挂但靠止损的，排第一**。止损不是修复 —— 今天清一把锁，明天可能在另一把锁上重演。
2. **安全边界出现两套标准的，排第二**。两套内核各写一份用户地址校验，等于两套信任模型。
3. **静默错误（回归抓不到）的，排第三**。改了看不出来，比改坏更危险。
4. **能增量、每步可回归的，往后放**。它们可以一直做，不急着插队。

---

## D-1　`g_sched_lock` 双重解锁：调用点未定位（**最高优先**）

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

## D-3　`hpdesk_owner_guard_selftest()` 的 `create_probe=0x0` 未查

**现状**：`desktop64_run()` 入口的屏外 1×1 探针窗口创建返回 0（失败），
自检仍判 PASS（判据是 `pass>=1 && reject==0`），所以这个失败被**静默吞掉**了。

**为什么算债**：自检里有一个"本该成功却失败"的动作，而判据把它放过。
这类"绿灯但有异常"的自检比没有自检更危险。

**第一步**：查 `hs_window_create` 在 `desktop64_run()` 入口为何失败（窗口表满？屏外坐标被裁剪拒绝？）。
**验收**：`create_probe` 变成非 0；或明确记录"屏外窗口按设计被拒"并把判据改成显式期望。

---

## D-4　桌面四项（一致性债，不影响运行）

| 项 | 现状 | 验收 |
|---|---|---|
| 控制中心图标 | 位图（`hs_icons_data.h` 32×32 1bit），与 `DESKTOP_SHELL.md` 1.2 矢量方向相反 | 图标走矢量管线，位图集删除或只留迁移期 |
| `bar_height` | 配置串写 `36`（`hpdesk.c:436`），回退默认 `40`（`:533`） | 只有一处真源，另一处引用它 |
| 天气卡 | `hs_panel_bounds` 与 render 差 24px | 两者用同一份矩形 |
| 胶囊居中 | 口径已定 `capsule_x = (整屏宽 - 胶囊宽)/2`，代码未按此改 | 代码按此式计算 |

---

## D-5　`syscall64_full.c` 业务未切完（3,189 行）

已切 1 批（目录三件套）。**下一批必须先补测试再切** —— 见下表的"必须先有探针"。

| 批 | 内容 | 前置条件 |
|---|---|---|
| 2 | `sys_stat` / `sys_fstat`（144 字节 struct stat 填充） | **必须有 stat 探针**：逐字段核对 `st_ino`/`st_size`/`st_mode` |
| 3 | `sys_open` / `sys_close`（fd 表加锁） | 需要 fd 复用的并发用例 |
| 4 | `sys_mmap` / `munmap` / `mprotect`（用户地址校验） | 需要越界/未映射地址的拒绝用例 |
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
D-1（定位双重解锁） → D-2（32 位接入 shared） → D-3（create_probe） → D-5 第 2 批（先补探针） → D-4（桌面四项）
```

D-1 排第一是因为它是**唯一一条"会挂且靠止损"**的。D-2 排第二是因为它决定"双微内核"
是不是真的成立。D-4 虽然项数多但都是视觉一致性，可以随时插空做。

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
