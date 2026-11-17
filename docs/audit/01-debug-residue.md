# 1. 调试残留清查：自检程序与开机自动 exec 调用点

- 审计基线：`5f93c2d`；行号为该基线编号。
- main 复核状态：基线之后 main 已到 `61c53fa`。其中 (D) 桌面循环常驻探针、(F) 文本 shell autostart 已由 `0202461` 默认关闭；其余条目状态见 §1.2、§1.6。

## 1.1 结论速览

- **已确认**：64 位内核存在三类开机自动执行通道——(A) 内置静态进程；(B) `HEPU_BOOT_SELFTEST` 自检电池（编译期开关，默认关）；(C) 桌面成功后的无条件 autorun；另有 (D) 桌面循环内核态探针、(E) 可选宏探针、(F) 文本 shell 兜底 autostart。
- **已确认（纠正任务前提）**：`fbsyn.elf` 在基线已被 `if (0)` 关闭（`kernel/kernel_main_64.c:918`）；`pie_probe.elf` 位于电池宏内（`kernel/kernel_main_64.c:1061-1065`），**默认发布构建不跑**，仅 `make BOOT_SELFTEST=1` 时编译进电池运行。任务前提中"pie_probe 仍在自动跑"与代码不符。quiet 实例串口日志实测 `PIEPROBE/FBSYN/FBDESC/STATP` 命中数 = 0。
- **开关可读性问题（非运行缺陷）**：电池已有编译期开关且默认关闭，但默认值只写在 Makefile 里，源码注释又写"默认关"，两处来源分散、容易被误读为默认开；建议在 Makefile 该行补注释说明默认值来源。
- **已确认**：32 位内核开机自动创建 `init`、`procb` 两个静态进程，并在初始化流程中串跑一串内核态 `*_selftest()`；其中 IDE 两个自测由 `HEPU_BOOT_DEBUG` 控制，而该宏在 32 位顶层 Makefile 中**默认开启**（`Makefile:25`）。

## 1.2 64 位开机自动执行通道

通道 (A)：内置静态进程（隔离模式下跳过）

| 行号 | 调用 | 程序/用途 | 开关 |
|------|------|-----------|------|
| `kernel/kernel_main_64.c:877` | `hepu_exec_static("hello", user_program_hello)` | 内置 hello 静态自测 | `#if defined(HEPU_USER_ISOLATION) && HEPU_USER_ISOLATION` 时跳过（`:873-878`） |

通道 (B)：自检电池，整体在 `#if defined(HEPU_BOOT_SELFTEST) && HEPU_BOOT_SELFTEST` 内（`kernel/kernel_main_64.c:895`，`#else` 提示在 `:1097-1099`）。
宏定义点：`kernel/arch/x86_64/Makefile:230-232`，仅 `BOOT_SELFTEST=1` 时 `CFLAGS += -DHEPU_BOOT_SELFTEST`，**编译期开关、默认不定义**。注意默认值只存在于 Makefile，源码注释另写"默认关"，两处来源分散；建议在 Makefile 该行补注释写清默认值来源，避免被误读成默认开。

| 行号 | ELF | 程序用途（据源码头注释） |
|------|-----|--------------------------|
| `:904` | `/bin/fbdesc.elf` | framebuffer 契约探针：用户态能否拿到显存物理基址/布局（`user/apps/fbdesc.c:1-10`） |
| `:920` | `/bin/fbsyn.elf` | 双缓冲 acquire→画→flip→release 实测；**额外被 `if (0)` 关闭**（`:918`） |
| `:930` | `/bin/statprobe.elf` | `struct stat` 布局探针（`user/apps/statprobe.c:1-9`） |
| `:939` | `/bin/fdstress.elf` | fd 表并发复用用例（`user/apps/fdstress.c:1-9`） |
| `:948` | `/bin/mmprobe.elf` | mmap/munmap/mprotect 边界用例（`user/apps/mmprobe.c:1-9`） |
| `:958` | `/bin/rwprobe.elf` | read/write 语义与 fd offset 用例（`user/apps/rwprobe.c:1-9`） |
| `:965` | `/bin/sec_test.elf` | 用户地址访问安全回归（`user/sec_test.c:1-9`） |
| `:972` | `/bin/hello_linux.elf` | Linux ABI hello（`user/apps/legacy/hello_linux.c`） |
| `:979` | `/bin/dynhello.elf` | 动态链接测试；注：内核不支持 PT_INTERP，该类程序会被 exec 拒绝（`user/Makefile:286-290` 注释） |
| `:986` | `/bin/dynhello2.elf` | 同上，带内部重定位库 |
| `:1006` | `/bin/hello_static.elf` | 原生静态 ELF 路径 |
| `:1021` | `/bin/crash_jump.elf` | 故意写未映射地址 `0xDEAD0000`，产生预期 #PF（`:1012-1026` 计数说明） |
| `:1029` | `/bin/glibc_test.elf` | glibc 静态 PIE |
| `:1035` | `/bin/hello2.elf` | Linux ABI 回归 |
| `:1040` | `/bin/hello3.elf` | 同上 |
| `:1046` | `/bin/hello4.elf` | 同上 |
| `:1053` | `/bin/hello_c2.elf` | 同上 |
| `:1062` | `/bin/pie_probe.elf` | freestanding static-PIE 探针：初始栈 argv/envp/auxv、加载偏置（`user/apps/pie_probe/pie_probe.c:1-17`） |
| `:1069` | `/bin/linux_pie_demo.elf` | Linux 兼容层 PIE 测试 |
| `:1076` | `/bin/forktest.elf` | fork 端到端测试 |

电池汇总：`:1084-1095` 只统计 `g_user_exception_count` 中"非预期异常数"，**不校验任何探针的视觉/输出判据**（见 1.5 与报告 05）。

通道 (C)：`desktop64_init() == 0` 之后的无条件 autorun（`kernel/kernel_main_64.c:1338-1357`）。

| 行号 | ELF | 用途 |
|------|-----|------|
| `:1339` | `/bin/hello.elf` | ELF64 主路径验证 |
| `:1340` | `/bin/sec_test.elf` | 安全回归（与电池 `:965` 重复执行） |
| `:1341` | `/bin/clone_test.elf` | clone 真线程验证（`user/apps/clone_test.c:1-8`） |
| `:1342` | `/bin/ps.elf` | 进程表读取验证 |
| `:1365` | `/bin/no_such_uproc.elf` | noent 取证探针：不存在路径应返回 `HEPU_E_NOENT(-2)`（`:1360-1366`） |
| `:1369` | `/bin/comptest.elf` | argv 栈回读取证探针（`:1367-1369`） |

通道 (D)：桌面初始化/主循环内的内核态探针（不在电池宏内）。

| 基线行号 | 调用 | 性质与 main 状态 |
|----------|------|------------------|
| `kernel/kernel_main_64.c:1291` | `lua64_selftest()` | Lua 解释器一次性自检（`lua64.c:67`），启动期一次 |
| `kernel/kernel_main_64.c:1375` | `sb08_fpu_probe()` | BSP 侧浮点自检，启动期一次；**保留**，是 SMP/FXSAVE 可用证据，不构成持续负载（`0202461` 明确保留） |
| `kernel/kernel_main_64.c:1381` | `gfx64_display_backend_selftest()` | 显示后端等价性一次性自检 |
| `kernel/arch/x86_64/desktop64.c:296-314` | 每 5 帧调 `sb08_fpu_probe()`，每 200 轮打印 ok/fail | **常驻桌面循环的验收负载**。main `0202461` 已改为 `if (0)` 默认关闭（`desktop64.c:298`），复核时改 1 |

通道 (E)：可选宏探针（默认不编译；宏在 64 位 Makefile 中均未定义）。

| 行号 | 宏 | 探针 |
|------|----|------|
| `kernel/arch/x86_64/desktop64.c:180-187` | `HEPU_ATA_HILBA_TEST` | `ide64_selftest_high_lba()` 高 LBA 写读回环 |
| `kernel/arch/x86_64/desktop64.c:189-195` | `HEPU_FS_STRESS` | `fs64_stress_run(8)` 大负载写入演练 |
| `kernel/arch/x86_64/desktop64.c:197-210` | `HEPU_SEC19_PROBE` | exec `/bin/ps.elf` 跨栈页复现 |

通道 (F)：文本 shell 兜底路径 autostart（`shell64_run`，仅桌面初始化失败回退时进入）。main `0202461` 已包进 `autostart_selftest` 开关、默认 0（`shell64.c:3463-3464`）；程序本体与命令保留，可手动执行。

| 基线行号 | ELF |
|----------|-----|
| `kernel/arch/x86_64/shell64.c:3464` | `/bin/hello.elf` |
| `:3474` | `/bin/sec_test.elf` |
| `:3481` | `/bin/forktest.elf` |
| `:3488` | `/bin/comptest.elf`（基线返回值未判） |

## 1.3 32 位开机自动执行通道

进程创建（`kernel/arch/x86/kernel.c:714-732`）：

| 行号 | 调用 | 入口符号 / 来源 |
|------|------|-----------------|
| `kernel/arch/x86/kernel.c:716` | `hepu_exec_static("init", user_init_start)` | `user/init/user_init.S:11-12` |
| `:717` | `hepu_exec_static("procb", procb_start)` | `user/apps/procb/procb.S:7-8` |

内核态自测（初始化流程内联调用，非 ELF exec）：

| 行号 | 调用 | 开关 |
|------|------|------|
| `kernel/arch/x86/kernel.c:512` | `binfmt_selftest()` | 无条件 |
| `kernel/arch/x86/kernel_init.c:140` | `vram_selftest()` | 无条件 |
| `:145` | `iosched_selftest()` | 无条件 |
| `:150` | `journal_selftest()` | 无条件 |
| `:153` | `crypto_selftest()` | 无条件 |
| `:157` | `snapshot_selftest()` | 无条件 |
| `:162` | `umem_selftest()` | 无条件 |
| `:167` | `transcomp_selftest()` | 无条件 |
| `:173` | `vvram_selftest()` | 无条件 |
| `:294` | `gfx_subsystem_selftest()` | 无条件 |
| `:336` | `blk_mq_selftest()` | 无条件 |
| `:340` | `vram_virt_selftest()` | 无条件 |
| `:343` | `task_manager_selftest()` | 无条件 |
| `:358` | `dma_selftest()` | 无条件 |
| `:426-427` | `buddy_selftest()` | 无条件 |
| `:515-516` | `vma_selftest()` | 无条件 |
| `:521-522` | `zram_selftest()` | 无条件 |
| `:547-548` | `hugetlb_selftest()` | 无条件 |
| `:552-553` | `pae_selftest()` | 无条件 |
| `kernel/arch/x86/init_sequence.c:327` | `ide_selftest()` | `#ifdef HEPU_BOOT_DEBUG`（`:324`） |
| `:330` | `ide_ext2_selftest()` | 同上 |
| `:333-334` | `user_access32_selftest()` | 无条件 |
| `:353` | `network_selftest()` | 无条件 |
| `:360` | `sec_selftest()` | 无条件 |
| `:588` | `sys_mmap_selftest()` | 无条件（桌面启动前） |
| `kernel/arch/x86/init_sequence.c:507` | `kdebug_init()` | 无条件 |
| `:539` | `hug_debugger_init()` | 无条件 |

`HEPU_BOOT_DEBUG` 默认开启证据：顶层 `Makefile:25` CFLAGS 与 `:28` ASFLAGS 均含 `-DHEPU_BOOT_DEBUG`；注释 `:11-13` 说明"默认开启，不再区分诊断/正式双镜像"。因此 32 位正式内核里 IDE 两个自测**默认编入并运行**。

## 1.4 user/apps 一次性自检/探针程序清单

已列入 `user/Makefile` 构建目标（`user/Makefile:33-42, 71-81, 219-316`）的探针/回归程序：

- 原生探针：`fbdesc.c`、`fbsyn.c`、`statprobe.c`、`fdstress.c`、`mmprobe.c`、`rwprobe.c`、`sec_test.c`（`user/sec_test.c`）、`comptest.c`、`clone_test.c`、`forktest.c`、`ps.c`、`pipe_test.c`、`pie_probe/pie_probe.c`
- 历史回归（`user/apps/legacy/`）：`crash_jump.c`、`hello_static.c`、`hello_linux.c`、`hello2.c`、`hello3.c`、`hello4.c`、`hello_c2.c`、`glibc_test.c`、`dynhello.c`、`dynhello2.c`、`hello_libc.c`、`libtest.c`、`libtest2.c`
- Linux PIE：`linux_pie_demo/`

目录存在但**未出现在 `user/Makefile` 目标**中的应用/演示（构建归属待验证，可能走 initrd/hug 通道，见 1.6）：
`browser/ calculator/ calendar/ cat_c/ clock/ crash_demo/ diag/ dynamic_demo/ editor/ filemgr/ ftp_client/ gfx3d_demo/ gfxtest/ hello/ hello_c/ hello_world/ hpkg/ http_client/ ipc_test/ jsontest/ linux_c_demo/ linux_crt_demo/ linux_demo/ loop_demo/ macho_demo/ mbrtool/ nettest/ notepad/ perf/ pop3_client/ scrapling/ smtp_client/ spin/ sysinfo/ sysmon/ top/ vfs_test/ web_server/ win32_demo/`。
其中 `gfx3d_demo/Makefile`、`hello_world/Makefile` 为独立构建文件，未并入顶层 user Makefile。

## 1.5 调试标记与 TODO 残留

- **未发现** `XXX` 标记（全仓 `kernel/`、`user/` 无命中）。
- `TODO`/`FIXME`/占位实现抽样（均为功能未完成标注，非一次性调试打印）：

| 行号 | 标注 | 内容 |
|------|------|------|
| `kernel/arch/x86/hpkg.c:235` | TODO | `install_time = 0`，未取当前时间 |
| `kernel/arch/x86/hpkg.c:348,354` | TODO | 包数据库仅内存态，未持久化 |
| `kernel/arch/x86_64/hpdesk.c:7844` | TODO | 时间戳未接 RTC |
| `kernel/arch/x86_64/hsservices.c:72,398` | TODO | 未写 AC97 音量寄存器；未检查文件 mtime |
| `kernel/block/blk_mq.c:173,218` | TODO | 时间戳未填 |
| `kernel/drivers/touch_hw.c:39,46,141` | `TODO_HW` | I2C 时序/复位 GPIO/坐标寄存器为框架占位，真机未接 |
| `kernel/fs/hepufs.c:329,702`、`kernel/fs/snapshot.c:84` | TODO | mtime 创建时间置 0 |
| `kernel/net/http2.c:64`、`http3.c:238` | TODO | settings/frame 未实际发送 |
| `user/apps/hpkg/hpkg.c:89,103,117,125,139,153` | TODO | 用户态 hpkg 未接内核安装/移除/更新/查询 |

- 被注释掉的旧自测：`kernel/arch/x86/kernel_init.c:178`（旧 `gfx_selftest` 已注释）；`kernel/arch/x86/kernel_init.c:491-513`（PAE 切换测试整段注释）。

## 1.6 待验证项

1. **发布镜像是否带 `HEPU_BOOT_SELFTEST`**：静态构建脚本默认不传 `BOOT_SELFTEST=1`（`x86_64/Makefile:230`）；quiet 实例串口日志实测 `PIEPROBE/FBSYN/FBDESC/STATP` 命中数 = 0，与"默认关"一致。仍需核对的是：镜像打包脚本 `tools/build_uefi_dual.py:281-301` 把 `user/build`、`user/build/legacy` 下全部 `.elf` 扁平化写入 `/bin`，若某条发布命令打开了 `BOOT_SELFTEST=1`，电池程序即可在镜像里找到。验证方法：检查实际构建命令行是否含 `BOOT_SELFTEST=1`；在 QEMU 串口日志中搜 `[PIEPROBE]`、`[STATP]`、`[FBDESC]`、`[FBSYN]` 等电池标签。
2. **未列目标的应用目录是否经 initrd/hug 打包**：`tools/mkinitrd.py`、`tools/mkhug.py` 的输入清单需逐目录核对；当前仅静态判断未进 `user/Makefile`。
3. **`gdbstub.c`/`kdbg_gdb.c`/`kdebug.c` 是否被链接进 32 位内核**：顶层 Makefile 用 `--gc-sections`（`Makefile:26`），无引用的目标文件会被丢弃；需一次 32 位链接产物（`kernel.elf` 符号表）确认是否保留这些调试入口。
