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
   **进度（2026-11-15，第 1 步的验证兜底已补）**：新增 `tools/regress_linux.sh`。
   用 `BOOT_SELFTEST=1` 重建内核（启动电池里有 `/bin/hello_linux.elf` 一条），QEMU 起
   来抓串口，要求三条同时成立：`[LINUX] Running /bin/hello_linux.elf...`（exec 没被拒）、
   `Hello Linux!`（程序真的跑到了 write(1)）、`[LINUX] returned`（正常退出）。
   只有第一条不算通过 —— exec 成功但程序跑挂一样会打第一条。
   实测 `exec=1 hello=1 returned=1` → PASS。这把"翻译表搬走后 Linux 程序还跑不跑"
   从等价性推理变成了实跑证据。
   两个踩过的坑记在脚本里：① 串口日志是二进制且多核会撕行，必须先 `strings` 再判；
   ② 必须 `grep -F` —— `[LINUX]` 在正则里是字符类，不用 `-F` 一辈子匹配不上。
   注意：`BOOT_SELFTEST=1` 的电池本身含"故意的坏地址测试"，出现 EXCEPTION 属预期，
   判据不看 EXCEPTION 计数。
   **进度（2026-11-15，第 2 步）**：ELF 的 ABI 判定也搬进了同一个网关。
   `elf64_is_linux_abi()` 的函数体（PT_INTERP / NT_GNU_ABI_TAG / PIE /
   链接基址在原生区外 四条判据）整段移到 `compat_gate_detect_elf()`，
   `elf64.c` 只留一个薄封装转调，**判定逻辑只有一份**。理由：`elf64.c` 是 ELF
   解析模块，"这个 ELF 属于哪套 ABI" 是兼容层的判断，不该由解析模块下结论；
   解析原语（`elf64_verify` / `find_interp` / `has_gnu_abi_tag` / `link_base`）
   仍留在 `elf64.c`，搬走的只有结论。
   为让网关能调 `has_gnu_abi_tag`，该函数去掉 `static`（解析原语本身没动）。
   这一步开始动真实判定，所以两条回归都跑了：
   ① `tools/regress_linux.sh` → `exec=1 hello=1 returned=1` PASS
     （外来 ABI 仍被判成兼容域，Linux 程序仍真跑得起来）；
   ② 标准回归 `-smp 2` 2/2（原生程序没被误判成兼容域，号表没走错）。
   二进制 937,128 B（+128 B）。
   **进度（2026-11-15，第 3 步）**：syscall 主干里的 4 处 `env_type` 判定收进网关，
   `syscall64_full.c` 里再也搜不到 `user_process_current_env_type`（引用数 0）。
   真正有行为差异的只有两条策略，收成 `compat_gate_lenient_unknown()`：
   兼容域对"号段越界 / 表里没有"的调用**静默 + 按 0 返回**（glibc 启动期要探测
   几十个号，逐个打日志会把串口冲干净，返回 -ENOSYS 又会让部分老逻辑走不通）；
   原生域照旧打日志 + 返回 -ENOSYS。另外两处是 `HEPU_TRACE_LINUX_SYSCALL`
   编译开关内的跟踪打印，改调 `compat_gate_active()`。
   **意义**：syscall 主干此后不再知道"有没有 Linux 这回事"，它只知道网关给的
   一条策略结论 —— 这是把兼容语义从主干剥离出来的关键一步。
   回归：`regress_linux.sh` PASS（exec=1 hello=1 returned=1）、标准回归 2/2，
   二进制 937,128 B（不变，纯重构）。
4. **framebuffer 契约**：把"写屏"从 `gfx64` 抽成一个最小接口（map + blit + flip），
   合成逻辑整体搬到用户态。这一步做完，① 和 ② 一起解决。
   （现状：第一层 `display_backend.h` 已落地，lfb + mirror 双后端，等价自检通过。）
   **进度（2026-11-15，第 1 步：map 的信息面）**：`map + blit + flip` 里，
   blit / flip 等价于现役 `present_region` / `present`，已具备；缺的是 **map** ——
   用户态不知道显存在哪，一切都无从谈起。所以先补"可映射描述"：
   `display_backend.h` 增 `fb_desc_t`（物理基址/宽高/pitch/bpp/格式/是否双缓冲）
   与 `gfx64_fb_desc()`；新 syscall `SYS_FB_DESC(229)` 把描述拷给用户态。
   描述**不进 vtable**：两个后端共享同一块物理显存，mirror 只是换了提交目标，
   它是 gfx64 的全局属性，塞进 vtable 会破坏这层刻意保留的 3 入口最小面。
   端到端探针 `user/apps/fbdesc.c` 打进镜像并在启动电池里跑，实测
   `[FBDESC] phys=0x80000000 w=1920 h=1080 pitch=7680 bpp=32 fmt=1 flags=0`
   → `[FBDESC] contract PASS`。
   **探针立刻抓出的两个事实**：① `phys` 第一版按十进制打却加了 `0x` 前缀，
   打出 `0x2147483648` 看着像坏值，实际就是 `0x80000000`（QEMU 的 LFB 基址）——
   取证工具的格式错误会伪装成内核 bug，已修成 `out_hex`。② `flags=0` 表示
   **此刻没有后台缓冲**：双缓冲是 `gfx64_set_target_back()` 首次调用时才 lazy
   分配的，启动电池跑在桌面之前。这是如实描述，不是缺陷；但它说明
   "用户态拿描述直接写显存"这条路此刻并不安全（会和内核提交打架），
   真正开放 map 必须同时给出"前台所有权"语义。
   **仍是半步**：只给了信息，没给映射动作。真 map 需要内核批一个物理页段
   映射给 ring3，并定义"谁拥有前台"——那一步才可能把合成搬出去。
5. **syscall 转发层切分**：把 `syscall64_full.c` 的业务搬进 fs/mm，syscall 只剩分发。
   **进度（2026-11-15，第 1 批：目录三件套）**：新建 `fs_dir_sys.c` + `fs_sys.h`，
   把 `sys_unlink` / `sys_mkdir` / `sys_rmdir` 从 `syscall64_full.c` 切出去，
   行为一字未改（只是换了住址），`syscall64_full.c` 里只留三行声明。
   选这一簇的理由：**依赖面最小** —— 只需 `copy_user_path` + `ext2_64` 的四个入口，
   不碰 fd 表、不碰锁、不碰进程上下文，切出去之后行为不可能变。
   配套把 `copy_user_path` 去掉 `static` 并收进 `fs_sys.h`：路径拷入是 syscall
   的**边界动作**，归 syscall 层所有，但实现要跨文件可见 —— 不复制第二份。
   证据：链接通过（三个符号都在）、标准回归 2/2、`syscall64_full.c` 3203 → 3189 行、
   二进制 937,640 B（+256 B）。
   **诚实缺口**：目录操作的错误码路径（EEXIST 之类）本来就没有单测，这次也没补；
   依据是"逐字搬移 + 链接通过 + 回归通过"，不是实测。下一批如果动到有分支逻辑的
   （`sys_stat` 的字段填充、fd 表锁），必须先补、后切。
   **进度（2026-11-16，第 2 批：stat / fstat）**：按要求**先补单测、后切**。
   单测是 `user/apps/statprobe.c`：按字节偏移读原始缓冲，不看结构体定义
   （结构体定义本身可能就是错的，用错的尺子量错的布）。它第一次跑出 4 个 FAIL，
   正是文档预言的那类：`st_blksize@56 = 0xE00000200`（高 32 位其实是 blocks 的值）、
   `st_blocks@64 = 0x0`（offset 64 实际是 st_atime）、`st_ino = 0x0`（所有文件同一
   inode）、`[128..143]` 哨兵未覆盖（内核只拷 128 字节）。根因是
   `struct linux_stat` 把 `st_blksize`/`st_blocks` 写成 `int32_t`。
   **修 + 切同时做**：新 `fs_stat_sys.c` 按偏移填 144 字节，`stat` 与 `fstat` 共用
   `fill_stat()`；inode 走 `ext2_64_stat_ino`（一直存在，只是没人用）。
   切出去的关键不是删代码，"同一契约原来有两套实现"才是：`sys_fstat` 是对的
   （逐偏移 144 字节）、`sys_stat` 是错的（结构体 128 字节），一对一错比两个都错
   难查 —— 比对两边只会得出"实现方式不同"。fd 表不 extern，只加一个
   `fs_fd_get_path()`（锁在内部取、返回路径拷贝）；fd 表锁这一前提已由 SB-11 满足，
   所以这批没有"先补锁"的负债。
   证据：探针 15 项全绿（含 `fstat st_ino == stat st_ino` 交叉校验）、
   64 位回归 2/2、32 位冒烟 668 行 PASS、`syscall64_full.c` 3189 → 3114 行、
   二进制 947,376 B（+128 B）。
   **这一批真正的价值不是"少 75 行"，而是把 stat 契约变成了可判读的**：
   在此之前偏移错了只会安静地错，标准回归不会红。
   **进度（2026-11-16，第 3 批：open / close）**：这批和第 2 批不同 —— stat 只
   **读** fd 表，open/close **改**它（找空槽、写 used、清 used）。所以边界不是
   "加一个查询函数"，而是两个**写操作**：`fs_fd_alloc()`（锁内找空槽 + 写
   path/mode/offset）、`fs_fd_release()`（锁内置 used=0），都留在 syscall 层；
   `fs_open_sys.c` 只负责语义（路径拷入、O_CREAT 要不要造文件、非法 fd 报什么）。
   先补的测试是 `user/apps/fdstress.c`：两条 `CLONE_VM` 真线程共享 fd 表各跑
   400 轮 open→close，检查三件事 —— 号必须落在 `[3, MAX_FILES)`（0/1/2 是 stdio
   保留槽位）、同一 fd 不能被两线程同时持有（"锁位置不对"最直接的指纹）、
   跑完后下一个 open 必须拿到 3（不泄漏）。切之前跑出 `open_fail=0 close_fail=0
   bad_range=0 conflict=0 leak_probe_fd=3`，切之后逐项不变。
   **命名冲突（值得记）**：内核里已有一组 `sys_open` / `sys_close`，是
   `user_lib.c` 的**用户侧** wrapper（把调用转成 syscall）。同名时链接直接
   `multiple definition`。所以受理端叫 `fs_do_open` / `fs_do_close` ——
   内置用户库的 `sys_*` 是 syscall 的**发起方**，fs 里的是**受理方**，是相反的
   两端，名字必须能区分，否则下次看到 `sys_open` 根本不知道是发还是收。
   证据：fdstress PASS（切前切后一致）、statprobe 15 项仍全绿、64 位回归 2/2、
   32 位冒烟 668 行 PASS、`syscall64_full.c` 3114 → 3104 行、二进制 947,632 B。
   **诚实缺口**：`open(O_CREAT)` 是"先占 fd 再建文件"，建文件失败时 fd 仍被占着；
   这是原有行为，fdstress 测不到（它不建文件）。另外 fdstress 的两线程在 QEMU
   里不一定真跑在两个物理核上，所以它证明的是"锁把分配与写入罩住了"，
   不是"SMP 下不会撞"——后者要靠 `-smp 12/16` 的长跑（D-1 那条观测项）。
   **进度（2026-11-16，第 4 批：mmap / munmap / mprotect）**：这批的难点不是代码，
   是**校验逻辑散在业务里**——窗口校验 5 处、长度溢出、保留区相交、配额检查、
   arena 推进、部分失败回滚。漏掉任何一条 `if` 都变成"以前拒绝的地址现在不
   拒绝了"，而标准回归看不出来（正常程序不碰非法地址）。
   用例 `user/apps/mmprobe.c` 20 项，测的不是"能分配"而是**该拒绝的真的拒绝了**：
   长度 0、超窗口、跨窗口上下界、撞 brk/栈/vDSO、`mprotect` 未映射地址、
   `munmap` 内核地址与跨窗口。先在**未切分**的旧代码上跑全绿（那一步证明判据
   本身对），切完再跑，20 项逐项一致。
   **边界判断变了**：前三批都是"表/锁留 syscall 层"，这批是**窗口常量整体搬走**。
   理由：`USER_WIN_*` / `USER_MMAP_*` / `USER_BRK_*` 不是分发层的知识，而是
   **用户地址空间的定义** —— 谁定义这个空间、谁判断地址合不合法，就该住在一起。
   留在 syscall 层的结果是搬出去的文件还得重写一份常量，同一件事两个真相。
   顺带把文件映射的 `sys_files[fd].path` 换成 `fs_fd_get_path()`（与第 3 批同一边界）。
   证据：mmprobe 20 项 PASS、statprobe/fdstress 仍 PASS、64 位回归 2/2、
   **glibc 端到端 PASS**（`exec=1 hello=1 returned=1`，这条覆盖了文件映射路径 ——
   ld.so 用 `mmap(fd,off)` 映射 libc 的 PT_LOAD，mmprobe 只测匿名映射，测不到它）、
   32 位冒烟 668 行 PASS、`syscall64_full.c` 3104 → 2897 行、二进制 947,632 B（不变）。
   **诚实缺口**：`user_access64.c` 里另有一份 `HEPU_USER_WIN_END`（用户指针访问的
   边界校验），语义相同但字面不同源。这批没合并它 —— 合并要先确认两者真的一致，
   而这属于"用户指针访问"那条路径，不是 mm 分区契约。

   **进度（2026-11-16，第 5 批前置：read / write 的用例与边界勘探）**：这批只做到
   "用例 + 前置修复"，**切分本身未做**，原因写在下面。
   和前三批不同，`read`/`write` 不只做分发，它们牵三样不属于分发层的东西：
   ① **fd offset 的读改回写**（read 之后 `offset += len`，且必须在 `g_fd_lock` 内）；
   ② **管道 IPC**（`is_pipe` 分支里是 `PIPE_BUF_SIZE` 环形缓冲 + `g_pipe_lock`）；
   ③ **stdio 特判**（`fd==0` 读键盘阻塞、`fd==1||2` 逐字节镜像串口）。
   而这三样依赖的全部是 `syscall64_full.c` 里 `static` 的 `sys_files_tbl` / `g_fd_lock`
   / `fd_snapshot()` / `g_pipes`。所以这批要先补两个边界 API
   （`fs_fd_get_offset` / `fs_fd_add_offset`，锁内完成）才谈得上搬。

   用例 `user/apps/rwprobe.c` 12 项，关键不是"读一次有数据"，而是 **offset 真的在前进**：
   `seek(0)` → 读 64 → 再读 64 → 断言两段内容不同；再 `seek(0)` 重读 → 断言与第一段
   逐字节相同。这两步合起来才能卡住"搬家时把 `+= len` 丢了"—— 那种错误在单次读的
   回归里完全看不见。其余判据：`count>4096` 截断到 4096、`count==0` 返回 0、
   只读 fd 上 `write<0`、只写 fd 上 `read<0`、`lseek(-1)` 被拒绝。

   **用例过程里踩到两个自己的错，记下来免得下次重犯**：
   - 探针里的 syscall 号写成了 Linux 号（read=0 / write=1），而 Hepu 原生是
     **read=9 / write=10**。症状是内核打 `[SYSCALL64] Unimplemented syscall: 0`
     并返回 0 —— 看起来像系统坏了，其实是探针自己传错号。
   - 一开始把「`lseek(-1)` 之后 `read<0`」当判据，跑出来 FAIL。查下去发现
     `sys_lseek` 在 `newpos<0` 时**直接返回 -1 且不改 offset**，负偏移根本注入不进
     fd 表；`sys_read` 里那条 `offset<0` 检查是**防御性**的，用户侧构造不出来。
     判据已改为"失败的 seek 不留副作用"（seek 失败后 offset 仍为 0、read 仍满 64）——
     这才是可达、且一旦顺序写反就会真出事的那一面。

   **用例查出一个真实缺陷并已修**：`sys_pipe` 分配 fd 从 **0** 开始找空槽，
   于是管道读端拿到 `fd 0`、写端拿到 `fd 1`（实测 `pipe fds = [0, 1]`）。后果：
   `read(读端)` 命中 `sys_read` 的 `fd==0` 分支 → 阻塞在 `hlt` 等键盘 → **进程永久挂死**；
   `write(写端)` 命中 `fd==1` 分支 → 内容直接镜像到串口，**管道从未被使用**。
   这和**第 3 批 `open` 踩的是同一个坑** —— 那批已经立下 `FS_FD_STDIO_RESERVED 3`
   这条约定，`pipe` 没看见它。修法：把常量提到 `fs_sys.h`（唯一来源），
   `sys_pipe` 的分配起点从 3 开始。修后 `pipe fds = [3, 4]`、`[RW] RESULT: PASS`、
   0 FAIL。这条缺陷在 `open` 批之后一直存在，但没有任何用户程序同时用过
   `open` 与 `pipe`，所以标准回归一直是绿的。

   **进度（2026-11-16，第 5 批：read / write 切进 `fs_rw_sys.c`，已完成）**
   **进度（2026-11-16，"其余"：pread 切分 + fd 常量归并，已完成，§5-5 收口）**

   这一批只动了两件小事，但它把 §5-5 收成了。原以为"其余"是一堆桩函数
   （`sys_creat` 直接 `return 3`、`sys_link` `return 0`、socket 系全空），切它们
   只会重演第 5 批的负容量，所以没动。真正值得切的只有 `sys_pread`——它是**唯一
   一个和 read 同源、却语义相反**的：按调用方给的 offset 读，读完**不回写** fd 游标。
   ld.so 就靠这个反复读 ELF 头和 PT_DYNAMIC 而不破坏文件游标。

   **为什么没有并进 `fs_do_read`。** 两者的差别只在一个动作（要不要 `fs_fd_set_offset`），
   合并成一个函数加 `no_offset_update` 布尔参数是最省的写法，但那个参数一旦被传错，
   症状是"动态链接器加载顺序悄悄变了"——静态程序上完全看不出来，只有 glibc 端到端
   能抓到。宁愿多一个函数，也不要在同一个入口里放一个默认值会错的分支。

   **顺手归并了一处"两处真相"。** `syscall64_full.c` 里原本有个
   `#define FS_FD_ALLOC_START 3`，而第 5 批我在 `fs_sys.h` 立了
   `FS_FD_STDIO_RESERVED 3` 当唯一来源。同一个值两个名字，正是 `sys_pipe`
   那次事故的成因（它没看见这条约定，从 0 开始分配，读端拿到 fd 0 → `read` 命中
   stdin 分支 → 进程永久阻塞在 `hlt`）。现在 `FS_FD_ALLOC_START` 只是
   `#define FS_FD_ALLOC_START FS_FD_STDIO_RESERVED` 的别名，不再是第二个真相。

   **容量**：`syscall64_full.c` 2,909 → **2,905（-4）**；`fs_rw_sys.c` 112 → **143（+31）**；
   二进制 947,952 → **947,952 B（不变）**。四个人日里第一次出现"行数减、体积不动"，
   因为这次是真搬走而不是加边界。

   **验证**：glibc 端到端 PASS（`exec=1 hello=1 returned=1 exc=1(handled=1)`）——
   这条是 pread 的唯一硬判据，ld.so 读 PT_DYNAMIC 走的就是它；64 位回归 2/2
   （`desk=1 EXC=0 PASS=1 FAIL=0`）。

   **§5-5 到此收口：6 条收敛顺序全部落地**（目录三件套 / stat+fstat / open+close /
   mmap 三件套 / read+write / pread+常量归并）。剩下的 fs 函数是桩，不切。


   边界判断（与前四批不同，也和第 5 批前置的设想不同）：原计划补两个窄接口
   `fs_fd_get_offset` / `fs_fd_add_offset`，真正动手时发现不够 —— read/write 要
   **同时**用到 path + mode + is_pipe + pipe_buf + offset，分五次去取，中间夹一个
   并发 close 就会拼出"路径来自 A、offset 来自 B"的缝合快照。所以改成一个
   `fs_fd_snapshot(fd, fs_fd_info_t*)` 锁内一次取全，加一个 `fs_fd_set_offset`。
   这其实就是把原本 static 的 `fd_snapshot()` 提升为公开边界并补 offset 字段。

   切分轴：只有"真文件的读与写 + 读位置维护"进了 `fs_rw_sys.c`（112 行）。
   两类东西**故意留在 syscall 层**：
   - **fd 0/1/2 的 stdio 特判**：fd 0 从键盘读（阻塞 `hlt` 等中断），fd 1/2 逐字节
     镜像串口。这是**终端**，不是文件系统。
   - **管道**：`PIPE_BUF_SIZE` 环形缓冲 + `g_pipe_lock`，是 **IPC**，不是文件分发。
   按 §5-5 的分层轴"谁定义数据、谁操作它"，两者都不归 fs。
   分流依据是 syscall 层取好的那一份快照 —— 由它取，是因为"是不是管道"这个判断
   和随后的语义调用必须看**同一份**视图。

   **这批的容量是负的，如实记下**：`syscall64_full.c` 2,897 → **2,909 行（+12）**，
   `fs_rw_sys.c` 112 行，二进制 947,632 → **947,952 B（+320）**。
   原因有两层：搬走的核心逻辑本来就不长（旧实现里 read/write 各自 ~100 行，其中
   一半是 stdin/管道/串口外壳，不搬）；而新加的边界 API（快照 + offset 回写）是
   纯增量。**前四批的"减薄"叙事在这批不成立** —— 当被切函数的大部分是外围分流时，
   切分会让总行数变多。换到的是单文件职责变窄、文件读写语义可以独立测。
   把它写成负数是故意的：如果只记"又切出去一批"，下一批会误以为切分必然减行数。

   一个明确的取舍写进了 `fs_rw_sys.c` 的头注释：ext2 的 IO 和 `copy_to_user` 都不能在
   `g_fd_lock` 内做（持锁做 IO 会堵住别的核的 open/close，`copy_to_user` 缺页会把锁
   带进异常路径），所以流程是"锁外读数据 → 锁内把 offset 推到新值"。代价是两个并发
   read 同一个 fd 会各自从同一 offset 读到相同内容（与 Linux 语义一致，本就不保证并发
   read 的原子性），但**不会**丢字节或互相覆盖 offset。

   证据：rwprobe 12 项 PASS、statprobe/fdstress/mmprobe 仍 PASS、
   64 位回归 2/2（`desk=1 EXC=0 PASS=1 FAIL=0`）、
   **glibc 端到端 PASS**（`exec=1 hello=1 returned=1 exc=1(handled=1)` —— 这条最有分量：
   glibc 的 printf/read 全走 syscall 9/10，切分若把 offset 或 copy 路径弄坏，它会先崩）。
   **诚实缺口**：`ext2_64_write_file` 仍是整文件覆写、不看 offset（这是既有语义，不是
   这批引入的，但它是"write 之后 offset 前进 n 却写不到 n 位置"这个语义裂缝的来源，
   属另一条待定技术债）；32 位侧未跑冒烟（B1 agent 正在改 `pmm.c`/`paging.c`/`process.c`，
   此刻跑 32 位构建拿到的是它的半成品，结果不可信）。
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
