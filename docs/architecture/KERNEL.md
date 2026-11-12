---
title: KERNEL
status: active
sys_version: 0.57.0
updated: 2026-11-12
owner: architecture
source_of_truth: kernel/arch/x86_64/Makefile
supersedes: null
---

# 赫普OS 64位内核架构设计：遵循系统包袱准则

> **核心总原则**：严格划分稳定契约边界，模块之间只依赖契约，不依赖对方内部实现；允许废弃模块，但禁止偷偷在核心路径加临时补丁。

> **现状口径（2026-11-12 核对）**：本文件第七节的"阶段1：契约化（当前）"**尚未完成**——
> 下表模块清单是**目标契约 + 实际文件落点**的混合记录，`xxx_ops` 目前只有 `storage_ops`
> 一族被 `storage_hal64.c` 真正统一接入；`cpu_ops` / `irq_ops` / `input_ops` 多为薄封装或直调函数。
> 另两处与"分层"相关的现状偏差，请先读 `DESIGN.md` 约束一注记与 `planning/MODULARITY.md`：
> ① 兼容层（`kernel/compat/*`、`linux_map[350]`）**编在内核里**，不是独立上层服务；
> ② 桌面（`gfx64`→`hepushell`/`hpdesk`→`hs_apps`）也在内核里，ring3 没有窗口/输入契约。

## 一、64位内核分层架构

```
用户态应用（64位）
————【稳定契约层：syscall ABI、数据结构、错误码】————
内核上层：进程管理、VFS、内存抽象、IPC、网络协议栈
————【子系统契约：ops结构体，仅暴露函数指针】————
内核底层：物理内存管理、中断控制器、定时器、总线抽象
————【硬件抽象层 HAL】————
硬件（x86_64 CPU、IDE、键盘、RTC、VBE等）
```

## 二、模块边界规则

### 2.1 严格分层依赖
- ✅ 上层只能调用下层暴露的接口函数
- ✅ 上层不能直接访问下层的结构体成员
- ❌ 禁止跨层直接访问（如进程管理直接操作PTE）
- ❌ 禁止下层调用上层（循环依赖）

### 2.2 子系统契约模式
每个子系统对外只暴露：
1. `struct xxx_ops` 函数表（稳定契约）
2. 初始化函数 `xxx_init()`
3. 配置结构体（仅含必要字段）

内部结构体完全私有，放在`.c`文件中，不对外暴露。

### 2.3 当前64位内核模块清单

| 层级 | 模块 | 契约接口 | 内部实现 |
|------|------|----------|----------|
| HAL | CPU | `cpu_ops` | `x86_64.c`（CR4 SMEP/SMAP、EFER NXE、TSC） |
| HAL | 中断 | `irq_ops` | `irq64_simple.c`（PIC(8259) + Local APIC，PIT/ACPI 计时） |
| HAL | 定时器 | `timer_ops` | `pit64.c` + `apic64.c` + `acpi.c` |
| HAL | IDE/PATA | `storage_ops` | `ide64.c` |
| HAL | SATA AHCI | `storage_ops` | `ahci64.c`（BAR5 高区 MMIO 映射 + storage_hal 接入） |
| HAL | 键盘 | `input_ops` | `keyboard64.c`（PS/2 8042 IRQ1，读-改-写配置字节 + 0xF4 开扫描） |
| HAL | USB | `usb_ops` | `usb64.c` + `ehci64.c`(USB2) + `xhci64.c`(USB3，复合 HID 多端点) |
| HAL | PCI | `pci_ops` | `pci64.c`（总线遍历 + `pci_find_class_bar0` + BAR MMIO 映射） |
| HAL | VBE framebuffer | `gfx_ops` | `gfx64.c` + `vg64.c`（Stage2 设置模式 → 内核读 0x6000 处信息） |
| HAL | RTC | `rtc_ops` | `rtc64.c` |
| HAL | 音频 | `ac97_ops` | `ac9764.c` |
| HAL | APIC / ACPI | `apic_ops` / `acpi_ops` | `apic64.c` + `acpi.c`（高半区 phys_to_virt 访问 >=1GB ACPI 表） |
| 内核底层 | 物理内存 | `pmm_ops` | `pmm64.c`（stage2 E801 探测 → 0x90100） |
| 内核底层 | GDT/TSS | `gdt_ops` | `gdt64.c` |
| 内核底层 | 块缓存 | `blk_cache_ops` | `blk_cache64.c`（256 槽 × 512B，LRU + 写穿） |
| 内核底层 | PCIe MMIO 映射 | - | `vmm64_map_page`（pci64/ahci64/ehci64 复用） |
| 内核上层 | 虚拟内存 | `vmm_ops` | `vmm64.c`（四级页表 + 高半区 0xFFFF800000000000 + EFER.NXE） |
| 内核上层 | 地址空间隔离 | `aspace_ops` | `user_aspace64.c`（每进程私有 PML4/PDPT，默认开启 `HEPU_USER_ISOLATION=1`） |
| 内核上层 | 进程管理 | `process_ops` | `process64_full.c`（fork/execve/waitpid 全套） |
| 内核上层 | 系统调用（入口壳） | - | `syscall64_simple.c`（MSR_STAR/LSTAR/SFMASK 初始化 + `syscall_handler_64` 入口壳 → **调 full.c 的 dispatch**） |
| 内核上层 | 系统调用（完整表） | `syscall_ops` | `syscall64_full.c`（`syscall_table[SYSCALL_MAX]` 100+ wrap 函数 + `linux_map[350]` 兼容翻译表，按进程 `env_type` 判定） |
| 内核上层 | ELF64 装载 | - | `elf64.c`（边界检查 + 规范入口 + 栈 16 对齐 + PIE/ET_DYN）；PT_INTERP 解释器由 `user_process.c:1938-2128` 加载入库区并切换入口 |
| 内核上层 | 文件系统 | `fs_ops` | `ext2_64.c` + `ext2_algo.c`（多挂载 slot + 自动 mkfs + 根分区自动扩展） |
| 内核上层 | 网络协议栈 | `net_ops` | `net64_*.c`（ARP/IP/ICMP/UDP/TCP/DHCP/DNS/HTTP 明文 + socket64） |
| 内核上层 | 图形子系统 | `gfx_ops` | `gfx64.c` + `font64.c` + `ttf64.c` + `cjk_font_data.c` + `gfx_algo.c` |
| 内核上层 | 桌面环境 | - | `hepushell.c`（Quickshell 风格 Dock/面板/控制中心/启动器/窗口管理） |
| 基础库 | libc | 标准函数 | `lib64.c` + `lib_shared.c` |

## 三、接口版本化规则

### 3.1 禁止多版本分支
❌ 错误：
```c
void process_schedule() {
    if (scheduler_version == 1) { old_schedule(); }
    else if (scheduler_version == 2) { new_schedule(); }
}
```

✅ 正确：
```c
struct scheduler_ops {
    void (*schedule)(void);
    void (*yield)(void);
};

struct scheduler_ops scheduler_v1_ops = { ... };
struct scheduler_ops scheduler_v2_ops = { ... };

// 启动时选择整个ops实例，不在函数内加if
```

### 3.2 渐进替换流程
1. 新实现：完全独立文件，实现同一套ops接口
2. 启动参数选择：`scheduler=v1` 或 `scheduler=v2`
3. 并行测试：同一套测试用例
4. 验证完成后删除旧实现整套代码

## 四、ABI设计规则

### 4.1 实验版ABI（当前阶段）
- 所有64位系统调用号、结构体布局标记为 `EXPERIMENTAL`
- 开发阶段可随时改动
- 只给内部测试驱动/app使用

### 4.2 稳定版ABI（未来）
- 一旦标记稳定，不能随意增删字段、调整对齐
- 破坏性修改需要版本大升级

### 4.3 当前64位系统调用表（v0.54.6）

系统调用分两路处理：
- **原生 Hepu ABI**：`syscall64_full.c` 中 `syscall_table[SYSCALL_MAX]`，按进程 `env_type == ENV_NATIVE` 时直接命中
- **Linux x86_64 兼容**：`linux_map[350]` 翻译表（`number` 经 `linux_map[number]` 映射后查 `syscall_table`），按进程 `env_type == ENV_LINUX_COMPAT` 时触发

> **完整 syscall 清单**见 `specs/SYSCALL_ABI.md`（由 `tools/gen_syscall_doc.py` 从 `syscall64_full.c` 自动生成）。
> 下表只列**关键类别**，共 100+ 项。

| 类别 | 覆盖的 syscall | 源码落点 | 备注 |
|------|---------------|---------|------|
| 基础 IO | read / write / open / close / lseek | `syscall64_full.c` wrap_read/write/open/close/lseek | |
| 进程 | fork / execve / exit / waitpid / wait4 / getpid / getppid / clone / vfork | | fork 走 COW 框架；execve 走 elf64_load |
| 文件 | unlink / rmdir / mkdir / rename / stat / fstat / statat / readdir / getdents / chmod / fchmod / chown / symlink / link / readlink / access / creat | | |
| 工作目录 | chdir / getcwd | | |
| fd 管理 | dup / dup2 / dup3 / fcntl | | |
| 信号 | kill / tgkill / sigaction / sigprocmask / rt_sigreturn | | 无 BSD signal()，用 sigaction 替代 |
| 内存 | mmap / munmap / mprotect / brk / mremap / mlock / madvise | | mmap 起始 0x50000000，writable+NX |
| 时间 | time / sleep / gettimeofday / clock_gettime / clock_getres / clock_nanosleep | | |
| 网络 | socket / connect / bind / listen / accept / sendto / recvfrom / shutdown / setsockopt / getsockopt / getsockname / getpeername / socketpair / select / pselect6 / readv / writev | | socket64.c 后端 |
| POSIX 杂项 | sched_yield / ioctl / umask / arch_prctl / newuname / sysinfo / getrusage / getrandom | | arch_prctl 用于 glibc FS_BASE TLS 初始化 |
| 兼容层 | exit_group / set_tid_address / set_robust_list / rseq | | glibc 静态 PIE 端到端（v0.33） |
| 内核私有 | ps / net_stats / reboot / disk_read / disk_write / mkfs | ps=200；disk_read/write 需 PID=0；mkfs 需 0x4D4B4653 魔数 |
| EPoll / Futex / Eventfd | epoll_create / epoll_ctl / epoll_wait / futex / eventfd2 | | epoll 三件套与 eventfd2 为桩返回 0（`syscall64_full.c:1268-1273`）；futex 为真实等待/唤醒实现（`syscall64_full.c:1302-1311` → `user_process.c:1689/1713`） |

### 4.4 入口路径（两层壳）

```
用户态代码 → syscall rax=nr,rdi=r1,rsi=r2,rdx=r3,r10=r4,r8=r5
      │
      ▼
entry_64.S 汇编（保存 r15→rbx → 换内核栈 → 调 C 函数）
      │
      ▼
syscall_handler_64() [syscall64_simple.c 第 69 行]
      │
      ├─ ① 先调 syscall64_dispatch() [syscall64_full.c 第 2268 行]
      │      - 如进程是 Linux 兼容态：linux_map 翻译
      │      - 查 syscall_table 命中 wrap 函数
      │      - g_syscall64_handled=1 表示已处理（区分 "errno=-1" vs "未实现"）
      │
      ├─ ② 如果 dispatch 没处理 → 回退到 Hepu 扩展号
      │      - SYS_HELLO=0 / SYS_PUTCHAR=1（启动早期调试）
      │      - SYSCALL_GETPID_NATIVE=20（原生 getpid 号，和 dispatch 不冲突）
      │
      ▼
entry_64.S sysretq 恢复寄存器返回用户态
```

## 五、硬件驱动包袱控制

### 5.1 HAL抽象
所有硬件驱动实现统一的ops接口：
```c
struct storage_ops {
    int (*read_sector)(uint32_t lba, void *buf);
    int (*write_sector)(uint32_t lba, const void *buf);
    int (*init)(void);
};
```

### 5.2 驱动隔离
- 驱动内部可以hack，但hack不能向上泄露
- 不允许为某一块板子在核心代码加 `#ifdef BOARD_XXX`
- 废弃驱动放在 `drivers/legacy/` 目录，默认不编译

### 5.3 当前64位驱动状态（v0.54.6）
- ✅ IDE/PATA驱动：`ide64.c`，PIO 模式
- ✅ AHCI SATA驱动：`ahci64.c`，BAR5 高区 MMIO + storage_hal 接入
- ✅ 键盘驱动：`keyboard64.c`，PS/2 8042 IRQ1（读-改-写配置字节 bit0 + 0xF4 开扫描）
- ✅ RTC驱动：`rtc64.c`
- ✅ VBE framebuffer：`gfx64.c` + `vg64.c`，Stage2 VBE 设置模式 → 内核读 0x6000 处信息
- ✅ PCI总线：`pci64.c`，遍历 + BAR MMIO 映射 + `pci_find_class_bar0`
- ✅ USB 核心 + EHCI(USB2) + XHCI(USB3)：`usb64.c` + `ehci64.c` + `xhci64.c`，复合 HID 多端点（无线接收器形态）
- ✅ USB Mass Storage：`usb64.c`，BOT 协议 + SCSI READ10/WRITE10/INQUIRY
- ✅ AC'97 音频：`ac9764.c`
- ✅ APIC / ACPI：`apic64.c` + `acpi.c`（RSDT/MADT 走高半区访问，解决真机 >=1GB ACPI 表访问崩溃）
- ✅ RTL8139 + e1000：`rtl8139_64.c` + `e1000_64.c`，DHCP 自动联网 + 真实链路检测
- ❌ NVMe / virtio-gpu / UHCI / Secure Boot：均不在 64 位 Makefile 中。NVMe（`kernel/drivers/nvme.c`）、UHCI（`kernel/drivers/usb/uhci.c`）仅存于 32 位遗留树；virtio-gpu、Secure Boot 仓库内无源码（见 OVERVIEW.md 九、已知缺口）

## 六、编码与构建规范

### 6.1 文件命名
- 核心模块：`xxx.c` + `xxx.h`（头文件只放对外接口）
- 内部实现细节不放在头文件
- 平台相关：`xxx_arch.c`

### 6.2 函数命名
- 对外接口：`xxx_verb()` 如 `pmm_alloc_page()`
- 内部函数：`_xxx_verb()` 如 `_pmm_find_free()`
- 中断处理：`xxx_interrupt()`

### 6.3 错误处理
- 所有可能失败的函数返回 `int`，0表示成功，负数表示错误
- 错误码统一在 `<errno.h>` 中定义
- 禁止静默失败

### 6.4 注释规范
- 每个对外接口必须有Doxygen风格注释
- 复杂算法必须说明思路
- 临时hack必须标注 `// HACK: 原因 + 修复计划`

## 七、64位内核重构计划

### 阶段1：契约化（当前）
- [ ] 为每个模块定义 `xxx_ops` 结构体
- [ ] 内部结构体移到 `.c` 文件
- [ ] 头文件只放对外接口
- [ ] 消除跨层直接访问

### 阶段2：分层整理
- [ ] 明确HAL层和内核层边界
- [ ] 硬件特例下沉到HAL
- [ ] 消除循环依赖

### 阶段3：版本化接口
- [ ] 系统调用ABI版本化
- [ ] 驱动接口版本化
- [ ] 启动参数选择实现版本

### 阶段4：驱动隔离
- [ ] 实现统一的 `storage_ops`、`input_ops`
- [ ] 驱动注册机制
- [ ] 废弃驱动隔离

## 八、禁止事项清单

1. ❌ 禁止在核心路径加临时补丁（必须走正式接口）
2. ❌ 禁止跨层直接访问结构体成员
3. ❌ 禁止在函数内加多版本if分支
4. ❌ 禁止为特定硬件在核心代码加#ifdef
5. ❌ 禁止静默失败（必须返回错误码）
6. ❌ 禁止循环依赖
7. ❌ 禁止复制粘贴代码（必须抽象成函数）
8. ❌ 禁止过早冻结实验版ABI

## 九、验收标准

64位内核代码符合系统包袱准则的验收标准：
1. 每个模块都有明确的 `xxx_ops` 契约接口
2. 头文件不包含内部结构体定义
3. 没有跨层直接访问
4. 没有多版本if分支
5. 硬件特例全部在HAL层
6. 所有对外接口有完整注释
7. 错误处理完整，无静默失败
8. 构建系统支持选择性编译驱动
