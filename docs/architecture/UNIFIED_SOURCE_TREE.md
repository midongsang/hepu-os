---
title: UNIFIED_SOURCE_TREE
status: active
sys_version: 0.63.0
updated: 2026-11-11
owner: docs
source_of_truth: null
supersedes: null
---

> ⚠️ **版本说明**：sys_version 已对齐 0.62.1，但正文口径仍为 v0.13.x ~ v0.33.x 设计稿。源码事实请以 `kernel/arch/x86_64/Makefile` 实际编入为准。


# 赫普OS 统一源码树进度

**更新日期**: 2026-10-14
**版本**: v0.10.134

## 概述

统一源码树目标：将32位(x86)和64位(x86_64)内核共享的代码提取到 `kernel/shared/` 目录，
减少代码重复，便于同步修复和功能演进。

原则：渐进式迁移，先共享数据结构和接口定义，再共享算法实现，最后共享驱动。
32位稳定优先，不强制32位立即引用shared。

## 重大进展 (v0.10.134)

### 32位共享模块集成成功 ✅
- 32位Makefile已排除x86_64目录，避免64位特定代码污染
- 32位kernel.c添加x86_64_detect实现（CPUID检测长模式）
- 4个共享C模块已成功编译并链接到32位内核：
  - driver_framework.o (统一驱动框架)
  - ext2_algo.o (ext2文件系统算法)
  - gfx_algo.o (图形算法)
  - lib_shared.o (字符串/内存函数)
- 32位内核大小：4.1MB（功能不受影响）

### APIC纯APIC模式验证成功 ✅
- Local APIC初始化（PIC兼容模式）
- I/O APIC初始化（ID=0, Ver=0x20, MaxRedir=42）
- IMCR切换到APIC模式
- PIC屏蔽，中断重定向到I/O APIC
- 纯APIC模式下系统完全稳定运行，桌面环境启动正常
- Shell命令：`apic mode`（查看当前模式）、`apic switch`（切换到纯APIC模式）

## 目录结构

```
kernel/shared/
├── security/
│   └── user_access.h          # 用户空间访问安全接口（copy_from_user等）
├── fs/
│   ├── ext2_shared.h          # ext2文件系统数据结构（超级块/inode/目录项）
│   ├── ext2_algo.h            # ext2共享算法接口（上下文+回调模式）
│   └── ext2_algo.c            # ext2共享算法实现（inode/数据块/目录/位图/路径）
├── gfx/
│   └── gfx_shared.h           # 图形子系统共享（颜色/矩形/帧缓冲/后端操作表）
├── mm/
│   └── mm_shared.h            # 内存管理共享（页大小/区域类型/对齐宏）
├── net/
│   └── net_shared.h           # 网络协议栈共享（MAC/IPv4/协议常量）
└── drivers/
    ├── driver_framework.h     # 统一驱动框架接口
    └── driver_framework.c     # 统一驱动框架实现
```

## 已完成

### 1. 安全接口共享 (security/user_access.h) ✅
- 定义 copy_from_user / copy_to_user / copy_str_from_user
- 定义 user_strlen / verify_user_read / verify_user_write
- 定义 hepu_is_user_address / hepu_user_space_limit
- **64位**: user_access64.c 已引用共享头，实现架构相关地址校验
- **32位**: sys_mem.c 已有等价实现，待迁移到共享接口

### 2. ext2文件系统共享 (fs/ext2_shared.h) ✅
- 超级块结构 (ext2_superblock_t, 1024字节)
- 块组描述符 (ext2_group_desc_t, 32字节)
- inode结构 (ext2_inode_t, 128字节)
- 目录项结构 (ext2_dir_entry_t, 变长)
- 文件类型/模式标志/魔数常量
- **64位**: ext2_64.c 已引用
- **32位**: 待迁移

### 3. 图形子系统共享 (gfx/gfx_shared.h) ✅
- 颜色格式 (gfx_color_t, 32位ARGB)
- 点/矩形/尺寸数据结构
- 帧缓冲信息 (gfx_framebuffer_t)
- 字体信息 (gfx_font_t)
- 图形后端操作表 (gfx_backend_ops_t) - 接口（操作表）模式
- 窗口/鼠标光标结构
- 基础算法：点在矩形内、矩形相交、alpha混合、颜色插值（static inline）
- **64位**: gfx64.c / desktop64.c 已引用
- **32位**: 待迁移

### 4. 内存管理共享 (mm/mm_shared.h) ✅
- 页大小/对齐宏
- 内存区域类型枚举
- **64位**: 待引用
- **32位**: 待迁移

### 5. 网络协议栈共享 (net/net_shared.h) ✅
- MAC地址/IPv4地址结构
- 协议常量
- **64位**: 待引用
- **32位**: 待迁移

### 6. 驱动框架 (drivers/driver_framework.c/h) ✅
- 统一驱动注册/查找框架
- **64位**: drv_register64.c / storage_hal64.c 已引用
- **32位**: ✅已集成编译（v0.10.134）

### 7. ext2共享算法 (fs/ext2_algo.c/h) ✅ (P0-2)
- ext2_context_t上下文：超级块+块读写回调+临时缓冲区
- 核心算法：inode读写、数据块读写（直接/间接/双重/三重间接）
- 目录查找、路径解析、块分配/释放、inode分配
- 目录项添加、文件读取（支持偏移）
- 架构无关：通过ext2_read_block_fn/ext2_write_block_fn回调访问存储
- **64位**: 已加入编译，ext2_64.c可逐步迁移到共享算法
- **32位**: ✅已集成编译（v0.10.134）

### 8. 图形共享算法 (gfx/gfx_algo.c) ✅ (P0-1)
- 共享图形算法：矩形填充、线条、alpha混合、字体渲染
- **64位**: gfx64.c已引用，TTF渲染改为alpha混合
- **32位**: ✅已集成编译（v0.10.134）

### 9. 共享库函数 (lib/lib_shared.c/h) ✅ (P0-3)
- 字符串操作：strlen/strcmp/strncmp/strcpy/strncpy/strcat/strchr/strrchr
- 内存操作：memset/memcpy/memmove/memcmp/memchr
- 转换：atoi/itoa
- **64位**: 已加入编译
- **32位**: ✅已集成编译（v0.10.134）

## 64位安全漏洞移植状态

| 漏洞 | 严重程度 | 64位状态 | 说明 |
|------|---------|---------|------|
| C: 用户指针无校验 | 🔴致命 | ✅已修复 | syscall64_full接入copy_from_user；syscall64_simple的SYS_PUTS已修复 |
| D: 数组越界写 | 🟠高危 | ✅已有防护 | 进程表遍历查找(非pid索引)；文件描述符有fd<MAX_FILES检查 |
| G: 内核地址泄露 | 🟡中危 | ✅无泄露 | sys_brk返回用户空间地址(0x500000)；不返回内核指针 |
| A: ring3无内存隔离 | 🔴致命 | ✅已实现 | 每进程独立CR3/PML4（HEPU_USER_ISOLATION默认开）+ SMEP + SMAP + W^X |
| B: 系统调用暴露内核堆 | 🔴致命 | ⚠️部分 | sys_malloc未实现；用户态使用brk分配 |
| E: signal存任意函数指针 | 🟠高危 | ⚠️部分 | sigaction已实现（process64_set_signal_handler）；rt_sigreturn/sigprocmask为桩 |
| F: 沙箱绕过 | 🟠高危 | ⚠️部分 | seccomp STRICT 模式已实现并接线（`seccomp64.c`，`sys_seccomp` `syscall64_full.c:989/2717`）；BPF 过滤器暂不支持；namespace/能力边界等其他沙箱维度仍缺 |
| H: 共享内存无权限 | 🟡中危 | ❌未实现 | 64位共享内存未实现 |

## 后续计划

### P1 - 近期
1. 32位sys_mem.c迁移到shared/security/user_access.h接口
2. 64位vmm64.c引用mm_shared.h
3. 64位net64_*.c引用net_shared.h

### P2 - 中期
4. 提取ext2通用算法到shared/fs/ext2_shared.c（块映射/目录遍历）
5. 提取图形基础绘制到shared/gfx/gfx_shared.c（fill_rect/draw_line）
6. 32位gfx引用gfx_shared.h

### P3 - 长期
7. 驱动框架完全统一（32位和64位共用driver_framework.c）
8. 网络协议栈算法共享（TCP状态机/IP校验和）
9. 内存分配算法共享（slab分配器）

## 引用统计（2026-11-17 按构建产物重新核实）

上一版的数字（"shared 8 个文件 / 64 位引用 6 个 / 32 位引用 0 个"）全部偏低，成因是
只 grep 了字面 `shared/` 路径，而多数引用走搜索路径形式（`#include "mm/hepu_pmm.h"`），
grep 不到 —— 这类"查不到就当没有"的误判会让人重做已经做完的事。

实测（`find kernel/shared` + `find build_* -path "*shared*" -name "*.o"` + `grep -rl`）：

| 项 | 实测值 |
|---|---|
| `shared/` 文件 | **30**（20 个 `.h` + 10 个 `.c`） |
| 32 位 include `shared/` 头 | 4 个文件（`sys_mem.c` / `gfx32_display.c` / `gfx64_compat.c` / `init_sequence.c`） |
| 32 位**编进** `shared/` 实现 | **10 个**：`desk_surface` `desk_registry` `desk_runtime` `desk_hpdesk` `desk_demo` `gfx_algo` `lib_shared` `ext2_algo` `debug_shared` `driver_framework` |
| 64 位 include `shared/` 头 | 17 个文件 |
| 64 位**编进** `shared/` 实现 | 2 个（`desk_surface`、`gfx_algo`） |

**结论出乎预期：共享在 32 位这边反而更彻底。** 32 位要编 `hpdesk.c` 全家桶，顺带把
`desk_*` 五个 shared 实现全编了进去；64 位因为另有 `desktop64.c` 这套入口，只用到
`desk_surface` 与 `gfx_algo`（后者见下面的"影子实现"说明）。

### 三个"编了但没接上"的实现（重要，别误接）

这三个文件 32/64 都编，但**调用方为 0**：

| 文件 | 内容 | 为什么没接 | 接上去会怎样 |
|---|---|---|---|
| `shared/gfx/gfx_algo.c` | `gfx_algo_backend_t *ops` 那套抽象（fill_rect / draw_line / 圆角 / 渐变） | 现行桌面走 `desk_surface`（12 核心 + 17 可选），两代抽象未对接 | 要大改调用点，且碰绘制路径 |
| `shared/lib/lib_shared.c` | `shared_strlen` / `shared_memcpy` / `shared_memset` … | 命名空间独立（`shared_` 前缀），与两边自己的 `memcpy` 不冲突也没人换 | **性能退步**：它是逐字节循环，不是 `rep movs`。内核热路径替换成它会直接变慢 |
| `shared/debug/debug_shared.c` | 调试辅助 | 两边各有自己的 vga/serial 输出路径 | 同上，属可选替换 |

**所以"让 32 位接 shared"不等于"把所有 shared 都接上"。** `shared_*` 那套内存函数是
给非热路径与将来的用户态服务用的参考实现，**不要**拿它替换内核 `memcpy`/`memset`；
`gfx_algo` 要不要接，取决于是否决定把 `desk_surface` 退成 `gfx_algo_backend_t` 的一种
——那是架构选择，不是顺手活。

### P1 第 1 条已完成

`32位 sys_mem.c → shared/security/user_access.h` 已落地：该文件 include 了共享声明面，
并按契约定义了 `hepu_user_space_limit = 0xBFFFFFFF`（32 位用户空间 3GB，内核从
`0xC0000000` 起）。注意它必须是**变量**：契约里是 `extern` 声明，缺定义要么链接失败，
要么默认 0 让所有用户指针校验失败。

## v0.10.128 更新 - P0-3: 共享字符串/内存库

### 新增文件
- `kernel/shared/lib/lib_shared.h` - 共享库接口（15个函数）
- `kernel/shared/lib/lib_shared.c` - 共享库实现（约200行）

### 包含函数
- 字符串：strlen, strcmp, strncmp, strcpy, strncpy, strcat, strchr, strrchr
- 内存：memset, memcpy, memmove, memcmp, memchr
- 转换：atoi, itoa

### 集成状态
- 64位：已加入Makefile，编译通过（内核156K）
- 32位：待集成（稳定优先）

### 共享模块统计（当前共13个文件）
1. kernel/shared/security/user_access.h
2. kernel/shared/gfx/gfx_algo.c + gfx_shared.h
3. kernel/shared/fs/ext2_algo.c + ext2_algo.h + ext2_shared.h
4. kernel/shared/mm/mm_shared.h
5. kernel/shared/net/net_shared.h
6. kernel/shared/drivers/driver_framework.c + .h
7. kernel/shared/lib/lib_shared.c + .h (新增)

## 为什么剩下的接不上（2026-11-17 实测）

本轮试了两个方向，都被实测挡住。结论比"还没做"重要，记下来免得重复试。

### 1. `net3.h` / `net.h` → `shared/net/net_shared.h`：**字段命名分叉，不能直接 include**

直觉做法是让 32 位的 `kernel/include/net3.h`（只重定义 `ipv4_addr_t` / `mac_addr_t`）
和 `kernel/include/net.h`（重定义 eth/arp/ip/icmp/tcp/udp 六个头）改成 include 共享头。
试了，编译立刻报 **6 处 `conflicting types`**（`eth_header_t` / `arp_packet_t` /
`icmp_header_t` / `udp_header_t` / `tcp_header_t` / `net_checksum`），说明 32 位还有一处
定义在用。查下去发现 32 位网络栈其实有**三套并行定义**：

| 位置 | 命名风格 | 引用者 |
|---|---|---|
| `kernel/include/net.h` | 无前缀：`type` / `hw_type` / `sender_mac` / `dst_mac[6]` | 3 个文件 |
| `kernel/include/eth.h`、`arp.h`、`icmp.h`、`tcp.h` | `hepu_` 前缀：`hepu_eth_header_t` 等 | 各协议模块 |
| `kernel/net/ip.c`、`udp.c`、`tcp.c` **文件内部** | 又各自 `typedef` 一遍无前缀版 | 仅本文件 |

三套互不 include，所以从不冲突，但也不共享。而 `net_shared.h` 的字段名是第三套
（`ethertype` / `htype` / `sha` / `dest_mac` 为 `mac_addr_t` 结构而非 `uint8_t[6]`）——
**语义相同、字段名和类型都不同**。直接 include 进来不是"复用"，是制造第 4 套。

**要合并就得改所有访问点**（`pkt->type` → `pkt->ethertype`，`pkt->sender_mac[i]` →
`pkt->sha.addr[i]` …），属于纯改名工程，收益是"少一份定义"，风险是碰整个 32 位网络栈。
**判断：不划算，除非将来重写 32 位协议栈。**

### 2. `shared/mm/hepu_pmm.h`：**12 个 `static inline` 只有声明、没有实现、没有调用**

`hepu_pmm_init` / `alloc_page` / `alloc_pages` / `free_page` / `get_stats` … 全部是
`static inline` 原型，函数体为空。grep 全仓库，`hepu_pmm_*` 的**实际调用点为 0**。
两边都 include 了它（`kernel/include/mm.h` 与 `arch/x86_64/include/pmm64.h`），
但因为 `static inline` 未使用不报错，编译一直绿 —— 它是一份**设计残留**，
不是"已完成的统一接口"。谁要真用，得先把 12 个函数体补出来。

### 3. `pmm64.h` 里"`mm_shared.h` 定义"只是一句注释

`kernel/arch/x86_64/include/pmm64.h:96` 写着 `/* get_stats → 填充 mm_phys_stats_t
（mm_shared.h 定义） */`，但**该文件并没有 `#include mm_shared.h`**。它是靠别的包含路径
间接拿到的。这类"注释声称共享、实际没 include"最容易骗过后来的审计。

### 4. 已知不能接的两个（原文档已记，这里补一句为什么）

- `shared/lib/lib_shared.c`：`shared_memcpy` 是**逐字节循环**，内核热路径替过去会直接变慢。
- `shared/gfx/gfx_algo.c`：`gfx_algo_backend_t` 与现行 `desk_surface` 是两代抽象，
  对接等于重画一遍调用点，属于架构选择而非顺手活。

### 5. 一个**真缺**的东西：64 位没有前台所有权 syscall

`SYS_FB_ACQUIRE(230)` / `SYS_FB_RELEASE(231)` 只在 32 位
（`kernel/arch/x86/syscall.c:219-220` + `sys_fb.c`），**64 位 syscall 表里没有这两项**。
32 位的实现是完整的：`sys_fb_acquire` 先把显存按 `MAP_PHYS` 映射进用户地址空间，
再把前台提交权交给该进程（`desk_fb_owner_acquire`，实现在 **`shared/desktop/desk_runtime.c:43`**）。

但**现在不该补**：64 位桌面（`desktop64.c` / `hpdesk`）跑在内核内，没有任何用户态进程会调，
补上去就是一个没有调用方的接口 —— 正是这份文档第 185 行警告的"假接口"。
要补的正确时机是 §5-4 把合成搬进用户态那一步，届时 64 位的 `sys_mmap` 还得先支持
`MAP_PHYS`（现在只做匿名页分配，`fs_mm_sys.c` 里没有物理映射路径）。

## 三个"编了但调用方为 0"的 shared 模块：逐条结论（2026-11-17 实测）

> 本节接在上一节"为什么剩下的接不上"之后，对三个模块逐条给结论与依据，不改动上面
> 已有的判断。核实方法：`find kernel/shared` 列文件 + 两侧构建规则核对是否真编入 +
> 全仓 grep 导出符号与头文件 include，区分"真实代码引用"与注释 / 头文件守卫名命中。
> 结论三选一：接上 / 标注废弃 / 保留给将来。

### 结论一览

| 模块 | 结论 | 真实调用方 | 编入 | 替代者 / 等待条件 |
|---|---|---|---|---|
| `shared/gfx/gfx_algo.c` | 保留给将来（不接、暂不废弃） | 0 | 32/64 都编 | 现行 `desk_surface_t` + `gfx64_*`；等统一图形算法层或新后端 |
| `shared/lib/lib_shared.c` | 保留给将来用户态服务；内核热路径不接 | 64 位 3 个薄包装 / 5 处调用点；32 位 0 | 32/64 都编 | 32 位 `kernel/lib/string.c`；64 位 `lib64.c` 同名函数 |
| `shared/debug/debug_shared.c` | 标注废弃（现状未启用） | 0 | 32/64 都编（未引用弱符号被 gc-sections 丢弃） | `assert.h` 的 `ASSERT` → `assert.c` 的 `assert_fail`；vga/serial 输出 |

### 1. `shared/gfx/gfx_algo.c` —— 保留给将来

**依据（实测）**

- 导出 17 个 `gfx_algo_*` 函数（颜色插值、alpha 混合、矩形、直线、圆角、三向渐变、
  alpha 位图、8x16 字体、矩形判定、裁剪、clamp），首参统一是
  `gfx_algo_backend_t *ops` 操作表。
- 调用方搜索：文件外 `gfx_algo_*` 真实代码引用 **0**；`gfx_algo_backend_t` 类型在
  文件外 **0 处定义 / 使用**。`gfx64_compat.c` 两处命中均为注释，记录 P1-4 曾因参数
  错位卡死、逐像素圆角过慢而放弃复用。
- 头文件：无配对 `.h`；自身 include `gfx_shared.h`。`gfx_shared.h` 被 64 位
  `gfx64.c` include，但只取 `gfx_framebuffer_t` 等结构体与颜色定义，不调 algo。
- 编入：64 位 `arch/x86_64/Makefile` 显式列出；32 位根 Makefile 的
  `find kernel -name "*.c"` 全量收编。
- 与现行实现的对应：现行桌面走 `desk_surface_t`（12 核心 + 17 可选原语，
  `desk_api.h`），表面层接 `gfx64_*`；32 位由 `gfx64_compat*.c` 凑同名实现，圆角
  按行扫描 + `rep stosl`，不走逐像素 algo。

**结论与代价**：**保留给将来**，现在不接、也不删。

- 接上的路径只有两条：要么让 `desk_surface_t` 包一张 `gfx_algo_backend_t` 操作表把
  两代抽象对上；要么新后端直接基于 ops 表写。
- 代价：全部绘制调用点要重画 / 重接；algo 的圆角 / 渐变是逐像素路径，32 位 compat
  已实测过性能问题（一个 260x40 胶囊上万次单像素调用）。
- 等待条件：决定统一跨架构图形算法层，或引入不依赖 `gfx64_*` 的新后端时再评估。

### 2. `shared/lib/lib_shared.c` —— 保留给将来用户态服务，内核热路径不接

**依据（实测）**

- 头文件声明 15 个 `shared_*` 函数（8 字符串 + 5 内存 + atoi/itoa），`.c` 全部实现，
  均为逐字节 / 逐字符循环，无 `rep movs` / `rep stos`。
- 调用方搜索：仅 64 位 `kernel/arch/x86_64/lib64.c` 三处薄包装、共 5 处调用点：
  - `memcmp()` → `shared_memcmp`（lib64.c 内 `memcmp` 包装，1 处）
  - `vga_printf()` → `shared_itoa`（`%d` / `%u` / `%x` 共 3 处）
  - `strncpy()` → `shared_strncpy`（lib64.c 内 `strncpy` 包装，1 处）
  - 其余 12 个函数：**0 调用方**；32 位全仓 **0 调用方**。
- 没有任何文件 `#include "lib_shared.h"`；lib64.c 用本地 `extern` 声明。
- 编入：64 位 Makefile 显式列出；32 位 find 全量收编。
- 与现行实现的对应：32 位自有全套在 `kernel/lib/string.c`（memcpy/memset/memmove/
  memcmp/strlen/strcpy/strncpy/strcmp/strncmp/strcat/strchr/strrchr/memchr/atoi…）；
  64 位自有 memset/memcpy/strlen/strcpy/strcmp/strncmp/strchr/strrchr 在 `lib64.c`。
  注意：两侧自有 memcpy/memset **同样是逐字节循环**，shared 版并不带来速度优势，
  它只是一份架构无关的平行命名空间。
- 对上一节"调用方为 0"口径的修正：严格说不是全模块 0 调用方，而是 15 个函数里
  12 个 0 调用方；64 位现存 3 个薄包装属非热路径，统计时不要重复算作"已接通"。

**结论与代价**：**保留给将来用户态服务**（用户态 libhepu / 新服务可复用一份架构
无关实现）；**内核侧不接**——不要用 `shared_memcpy` / `shared_memset` 替换内核同名
热路径。现存 3 个 64 位包装属格式化输出与薄包装，可保留观察；若要清理，直接回落
`lib64.c` / `string.c` 同名实现即可，无迁移成本。

### 3. `shared/debug/debug_shared.c` —— 标注废弃（现状未启用）

**依据（实测）**

- 内容：弱符号 `hepu_debug_assert_fail`；弱空桩 `hepu_debug_puts` /
  `hepu_debug_put_hex` / `hepu_debug_put_dec`；头文件宏 `HEPU_ASSERT` /
  `HEPU_DEBUG_INFO/ERROR/WARN` / `HEPU_FUNC_ENTER/EXIT` 与 inline `hepu_strerror`。
- 调用方搜索：文件外真实引用 **0**；全仓无人 `#include "debug_shared.h"`；弱桩输出
  函数也没有架构侧覆盖实现（arch/sched 下 grep 为 0）。`kernel/include/assert.h`
  命中的 `HEPU_ASSERT_H` 只是头文件守卫宏名，与本模块无关。
- 编入：64 位 Makefile 显式列出；32 位 find 全量收编；未被引用的弱符号经链接器
  `--gc-sections` 实际不进镜像。
- 现行替代者：断言走 `kernel/include/assert.h` 的 `ASSERT(cond)` →
  `assert_fail`（定义在 `kernel/sched/assert.c`）；诊断输出走两边各自的
  `vga_puts` / `serial_puts`。

**结论与等待条件**：**标注废弃（现状未启用）**，新代码不要 include
`debug_shared.h`。将来若要做跨架构统一诊断面（含用户态服务共用同一套调试宏），
再把这套宏与输出桩复活；在此之前它是设计残留，不是"已完成的统一调试接口"。
