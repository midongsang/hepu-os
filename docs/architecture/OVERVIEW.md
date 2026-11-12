---
title: OVERVIEW
status: active
sys_version: 0.58.0
updated: 2026-11-12
owner: architecture
source_of_truth: kernel/arch/x86_64/Makefile
supersedes: null
---

# 赫普OS 系统架构设计文档（64位实况）

> 本文档以 `kernel/arch/x86_64/Makefile` 中 **实际编入的 C_SOURCES/ASM_SOURCES** 为唯一事实源。
> 源码在仓库但未编入 Makefile 的模块一律标注「不在 64 位构建」，不再伪装 ✅。

---

## 一、系统概述

赫普OS（Hepu OS）当前为 **x86_64 单体内核**架构，C 语言核心 + 汇编入口 + 无 Rust 用户态打包（源码在 `user/rust/` 但未接入 `user/Makefile` 与镜像脚本）。
内核单体直链驱动、网络栈、文件系统、图形桌面，不走微内核 IPC 路线。

| 项目 | 值 |
|------|-----|
| 当前版本 | **0.57.0**（`docs/status/VERSIONING.md` frontmatter；本文正文中"0.54.6"为撰写时的历史基线，规格以 frontmatter 为准） |
| 内核架构 | x86_64 长模式 |
| 编译约束 | -m64 -mcmodel=small -mno-red-zone -ffreestanding -fno-pie |
| 工具链 | gcc / binutils (x86_64 宿主) |
| 内核地址 | 高半区直接映射 KERNEL_DIRECT_BASE = 0xFFFF800000000000 |
| 用户窗口 | [0x40000000, 0x80000000) 隔离私有 PML4 |
| 内存上限 | 2GB 以上支持（stage2 E801 探测，内核 pmm64_init 读 0x90100） |
| 默认图形 | VBE framebuffer（gfx64.c）；回退文本终端（shell64 + term64） |

### 设计理念
- **系统归人，数据归己，自由可控，极致纯净**
- 单体直链，不走微内核 IPC（V0.54 阶段不搞信号量/共享内存/消息队列/互斥锁）
- 安全优先：用户指针逐页校验（`user_access64.c`）、SMEP/SMAP/NX/W^X、fork 独立页表 + COW
- 渐进式完善：先最小可用，再逐步完善；Makefile 收录即视为「可用」，源码散落仓库但未编入一律不计

---

## 二、系统架构

```
┌──────────────────────────────────────────────────────────┐
│                    用户态应用层（ELF64）                    │
│  ┌────────┐  ┌────────┐  ┌──────────┐  ┌────────────┐   │
│  │hepushell│ │shell64 │ │install64 │ │hello/sec_test│   │
│  └────────┘  └────────┘  └──────────┘  └────────────┘   │
│  入口：entry_64.S sysretq  trampoline / fork_trampoline_64│
├──────────────────────────────────────────────────────────┤
│                    系统调用层（syscall_handler_64）          │
│  原生号表 syscall_table[SYSCALL_MAX]                       │
│  Linux x86_64 兼容翻译 linux_map[350]（按进程 env_type 判定）│
│  已接入：fork/execve/waitpid/sigaction/mmap/pipe/ioctl/    │
│          socket 全套/epoll/futex/clock_gettime/...       │
├──────────────────────────────────────────────────────────┤
│                    单体内核层（全部直链 hepu-kernel64.bin）  │
│  ┌────────────┐ ┌─────────────┐ ┌─────────────────┐      │
│  │调度+进程   │ │内存管理      │ │中断+时间        │      │
│  │process64   │ │pmm64/vmm64  │ │irq64/apic64     │      │
│  │fork/COW    │ │user_aspace  │ │pit64/acpi       │      │
│  └────────────┘ └─────────────┘ └─────────────────┘      │
│  ┌────────────┐ ┌─────────────┐ ┌─────────────────┐      │
│  │文件系统    │ │网络协议栈    │ │安全地基        │      │
│  │ext2_64     │ │ARP/IP/ICMP  │ │user_access64    │      │
│  │blk_cache   │ │UDP/TCP/DHCP │ │SMEP+SMAP+NX    │      │
│  │swap64      │ │DNS/HTTP     │ │crypto(XTEA)    │      │
│  │partition   │ │socket64     │                 │      │
│  └────────────┘ └─────────────┘ └─────────────────┘      │
├──────────────────────────────────────────────────────────┤
│                    驱动层（全部编入 Makefile）               │
│  ┌────────────┐ ┌─────────────┐ ┌─────────────────┐      │
│  │存储        │ │网络网卡      │ │输入            │      │
│  │ide64/ahci64│ │rtl8139_64   │ │keyboard64       │      │
│  │usb64+ehci  │ │e1000_64     │ │ps2mouse64       │      │
│  │  +xhci     │ │pci64        │ │usb64(HID复合)  │      │
│  └────────────┘ └─────────────┘ └─────────────────┘      │
│  ┌────────────┐ ┌─────────────┐ ┌─────────────────┐      │
│  │显示+音频   │ │USB控制器    │ │通用HAL          │      │
│  │gfx64(VBE)  │ │ehci64/xhci64││storage_hal64    │      │
│  │ttf64/font  │ │usb64(core)  │ │network_hal64    │      │
│  │ac9764      │ │             │ │driver_framework │      │
│  └────────────┘ └─────────────┘ └─────────────────┘      │
├──────────────────────────────────────────────────────────┤
│                    硬件抽象层                                │
│  x86_64 架构 / GDT+IDT / PIC(8259)+APIC / ACPI / PCIe MMIO│
│  VBE framebuffer / UART 串口日志 / 8042 PS/2 控制器        │
├──────────────────────────────────────────────────────────┤
│                    引导层                                   │
│  multiboot_entry.S（Stage2 32 位 → 64 位长模式跳转）       │
│  混合镜像：MBR + ESP(FAT32) + ext2 根分区（LBA13000+）      │
│  tools/build_esp.py + tools/make_gpt.py 生成（替代 losetup）│
└──────────────────────────────────────────────────────────┘
```

> **未编入 64 位构建**的模块（源码可能在 32 位遗留树或用户态目录，但 64 位 Makefile 没有）：
> NVMe、UHCI、VFS、fscrypt、ext4/exFAT/NTFS、WebSocket、IPC 信号量/共享内存/消息队列/互斥锁、FTP/SMTP/POP3、第三方库（zlib/libpng/libjpeg）。
> 其中 IPv6、virtio-gpu、virtio-blk、Secure Boot 签名验证在仓库内无对应源码；Rust 用户态打包未接入 `user/Makefile` 与镜像脚本。
> TLS 1.2 协议代码已编入 64 位 Makefile（见 3.4 / 5.4），但未接入 HTTP 调用链。

---

## 三、内核子系统（实况）

### 3.1 进程管理 ✅（已编入）
- **进程模型**：固定槽位进程表，每进程独立 PML4（隔离默认开启 `HEPU_USER_ISOLATION=1`）
- **调度器**：时间片轮转 + sched_yield，irq64 APIC 计时
- **fork**：`sys_fork` → `uaspace_fork_copy`（深拷贝页表）+ `fork_trampoline_64`（子进程从父进程 syscall 返回点继续，`rax=0`）
  - 安全地基：fork 走 COW 框架（`swap64` 目录有页换出原语），真实写时复制在缺页处理中落地
- **execve**：ELF64 加载器 `elf64_load`（边界检查 + 规范入口 + 栈 16 字节对齐），支持 PIE/ET_DYN
- **信号**：`sigaction/sigprocmask/kill/tgkill/rt_sigreturn` 全套
- **系统调用编号**：原生号表 `syscall_table[SYSCALL_MAX]` + Linux 兼容翻译 `linux_map[350]`（按进程 `user_process_t.env_type` 判定）
- **PT_INTERP 解释器加载**：已接入 exec 路径（`user_process.c:1938-2128`）：检测 PT_INTERP → 从 ext2 读取解释器（ld.so）→ 按偏置装入库区 `[0x60000000,0x70000000)` → 映射进进程并把入口切到解释器，`AT_BASE/AT_ENTRY/AT_PHDR` 填好。DT_NEEDED 符号查找/重定位/TLS 初始化交由用户态 ld.so 完成；内核遗留的 `hepu_dynlink_relocate()`（`user_process.c:1523`，含 DT_NEEDED 遍历与 JMPREL 重定位）已不再被 exec 调用（`user_process.c:2140-2150`）。

### 3.2 内存管理 ✅（已编入）
- **物理内存**：`pmm64` 位图分配器，stage2 E801 探测 → 内核写 0x90100
- **虚拟内存**：四级页表，低身份映射 1GB + 高半区直接映射 0xFFFF800000000000
- **用户态隔离**：`user_aspace64` 私有 PML4/PDPT，内核映射清 U/S 位（ring3 不可访问）
- **安全地基**（已落地）：
  - **SMEP**：`cr4.smep=1`（CPUID.7.0.EBX[7] 检测后开启），ring0 不得执行用户页
  - **SMAP**：`cr4.smap=1`（CPUID.7.0.EBX[20] 检测后开启），`copy_from/to_user` 用 `stac/clac` 包裹
  - **NX/W^X**：`EFER.NXE=1`，sys_brk 分页映射带 `PT_NX`
  - **fork COW**：页级复制 + 换出框架
- **内核堆**：`kheap64` 8MB，动态分配（防 BSS 越界）
- **块缓存**：`blk_cache64` 256 槽 × 512B = 128KB，LRU + 写穿
- **内存压力管理**：`memory_pressure.c` 挂桌面主循环 tick，自动回收 ext2 缓存

### 3.3 文件系统 ⚠️（部分）
| 组件 | 状态 | 源码落点 |
|------|------|---------|
| ext2 文件系统 | ✅ 完整 | `ext2_64.c` + `ext2_algo.c` |
| 块缓存 | ✅ | `blk_cache64.c` |
| 页换出（框架） | ⚠️ 有原语无调用者 | `swap64.c`（`vmm64_swap_out`/`swap_in`，但内存压力路径未接入进程级换出） |
| MBR 分区解析 | ✅ | `partition64.c` |
| mkfs（内核版） | ✅ | `ext2_64_mkfs`（4096B 块大小） |
| 自动扩展 | ✅ | `ext2_64_expand_to_partition` |
| 三区制挂载 | ✅ | P1 系统 + P2 配置(/cfg) + P3 自由(/data) |
| fsck | ✅ | `ext2_fsck64.c` |
| VFS 抽象层 | ❌ 不在 64 位构建 | 源码仅存于 32 位遗留树 `kernel/fs/vfs.c`，未编入 64 位 Makefile |
| fscrypt 加密 | ❌ 不在 64 位构建 | 源码仅存于 32 位遗留树 `kernel/fs/fscrypt.c`（`kernel/include/fscrypt.h`），未编入 64 位 Makefile |
| ext4/exFAT/NTFS/FAT | ❌ 不在 64 位构建 | 源码仅存于 32 位遗留树 `kernel/fs/ext4.c`/`exfat.c`/`ntfs.c`/`vfat.c`，未编入 64 位 Makefile |

### 3.4 网络协议栈 ⚠️（部分）
| 层 | 协议 | 状态 | 源码落点 |
|----|------|------|---------|
| 物理 | RTL8139 PCI 网卡 | ✅ | `rtl8139_64.c` |
| 物理 | e1000 PCI 千兆 | ✅ | `e1000_64.c` |
| 链路 | ARP | ✅ | `net64_arp.c` |
| 网络 | IPv4 | ✅ | `net64_ip.c` |
| 网络 | ICMP | ✅ | `net64_icmp.c` |
| 网络 | IPv6 | ❌ | 源码不存在 |
| 传输 | TCP | ✅ | `net64_tcp.c`（重传定时器已接线） |
| 传输 | UDP | ✅ | `net64_udp.c` |
| 应用 | DHCP | ✅ | `net64_dhcp.c`（自动 client，桌面主循环 poll） |
| 应用 | DNS | ✅ | `net64_dns.c` |
| 应用 | HTTP/1.0（明文） | ✅ | `net64_http.c` |
| 应用 | TLS/HTTPS | ⚠️ 代码已编入、未接入调用链 | `kernel/net/tls.c`/`tls_record.c`/`tls_crypto.c`/`tls_handshake.c`/`tls_client.c`（Makefile:96-100，共 2327 行）；启动期 `hepu_tls_init()`（`kernel_main_64.c:1107`）初始化子模块，但 HTTP 客户端 `net64_http.c` 无 https/443 路由，顶层会话 API 为模拟握手桩（`tls.c:197`），端到端加密收发未打通 |
| 应用 | WebSocket/FTP/SMTP/POP3 | ❌ 不在 64 位构建 | WebSocket 源码仅存于 32 位遗留树 `kernel/net/websocket.c`；FTP/SMTP/POP3 仅用户态样例 `user/apps/{ftp,smtp,pop3}_client/`，均未编入 64 位内核或用户态构建 |
| Socket API | BSD 兼容 | ✅ | `socket64.c`（socket/connect/accept/bind/listen/sendto/recvfrom 等） |
| 可观测 | 链路检测 + 真实速率 | ✅ | 任务管理器 DHCP 绑定状态 + `net_hal_get_stats` DL/UL KB/s |

### 3.5 图形子系统 ✅（已编入）
| 组件 | 状态 | 源码落点 |
|------|------|---------|
| 显示后端 | VBE framebuffer | ✅ `gfx64.c` + `vg64.c` |
| 字体 | TrueType 解析 | ✅ `ttf64.c` + `font64.c` + `font_data.c` |
| CJK 中文 | GB2312 全汉字集 | ✅ `cjk_font_data.c` |
| 抗锯齿渲染 | AALine/AAFill | ✅ `gfx_algo.c`（shared） |
| 窗口管理 | 创建/移动/缩放/Z轴 | ✅ `window64.c` |
| 桌面环境 | hepushell（Quickshell 风格） | ✅ `hepushell.c` + `desktop64.c` |
| 桌面应用 | 终端/文件管理器/任务管理器/设置/计算器等 | ✅ `term64.c`/`filemgr64.c`/`taskmgr64.c`/`syssettings64.c`/`hs_apps.c` |
| virtio-gpu 虚拟化 | ❌ 不在构建 | 源码不存在 |
| GOP UEFI 协议 | ❌ 不在构建 | 内核不依赖 UEFI GOP，Stage2 用 VBE 设置 framebuffer |

### 3.6 安全机制 ⚠️（部分）
| 组件 | 状态 | 源码落点 |
|------|------|---------|
| 用户指针逐页校验 | ✅ | `user_access64.c`（CR3 四级页表 P/U/S/R/W 校验） |
| 地址空间隔离（私有页表） | ✅ | `user_aspace64.c`（默认开启） |
| SMEP（内核不可执行用户页） | ✅ | `irq64_simple.c`（CR4.SMEP） |
| SMAP（ring0 不可访问用户页） | ✅ | `irq64_simple.c`（CR4.SMAP，copy_*_user 用 STAC/CLAC） |
| brk 分页 + NX/W^X | ✅ | `vmm64.c`（用户堆带 PT_NX） |
| fork COW + 独立地址空间 | ⚠️ fork 已接入 / clone 未实现（ENOSYS） | fork 走 `uaspace_fork_copy`（深拷贝页表 + 共享物理页 + 清 W + `UA_COW` 标记，`user_aspace64.c:165`，由 `sys_fork` 调用 `syscall64_full.c:1875`）；**clone 未实现**：原 `sys_clone` 结构体偏移全错（active 写到 rip 位、页表写到 rflags 位），子槽永不调度，属返回 pid 的空实现；v0.57.1 已改为返回 `-38 ENOSYS`，真线程需 trampoline + 独立内核栈（见 `OPEN_BUGS` SEC-02、`FIXES_0_57_0.md` §1.82） |
| 用户程序异常只终止自身 | ✅ | vec40 用户异常 trampoline，不再整机 hlt |
| 加密原语 | ⚠️ XTEA/XTS 在用，公钥/摘要套件已编入但上层调用有限 | `kernel/crypto/` 共 11 个文件 3552 行全部编入（Makefile:85-95）：XTEA + XTS 页级加密（`crypto.c:33/168`）为在用路径；AES-128/256(-GCM/CBC)、SHA-256/512、HMAC、HKDF、ECDHE、RSA、X.509 代码已编译，主要供 TLS 子模块（`tls_crypto.c:48-55`）与 `auth64.c:71` 使用；RSA/X.509 证书校验暂无独立上层调用方 |
| Secure Boot 签名验证 | ❌ | 仓库内无对应源码，引导器不传签名校验 |
| fscrypt 文件加密 | ❌ 不在 64 位构建 | 源码仅存于 32 位遗留树 `kernel/fs/fscrypt.c`，未编入 64 位 Makefile |
| CSPRNG ChaCha20 | ✅ 已接线 | `kernel/crypto/csprng.c:162/214/296`（ChaCha20 块函数 + init + get_bytes），Makefile:94；启动期 `csprng_init()`（`kernel_main_64.c:754`），`getrandom` 系统调用 `sys_getrandom` → `csprng_get_bytes`（`syscall64_full.c:1315/1328`，表项 `:2740`） |

---

## 四、驱动支持（对照 Makefile 实况）

### 4.1 存储驱动
| 驱动 | 状态 | 源码落点 | 备注 |
|------|------|---------|------|
| IDE (PATA) | ✅ | `ide64.c` | 标准 PATA 控制器 |
| AHCI (SATA) | ✅ | `ahci64.c` | BAR5 高区 MMIO 映射 + storage_hal 接入 |
| USB Mass Storage | ✅ | `usb64.c`（BOT 协议） | Bulk-Only Transport + SCSI INQUIRY/READ10/WRITE10 |
| NVMe (PCIe SSD) | ❌ 不在 64 位 Makefile | 源码仅存于 32 位遗留树 `kernel/drivers/nvme.c`（32 位 Makefile:131 编入），64 位树无 `nvme64.c` |
| virtio-blk | ❌ | 仓库内无对应源码，不在 Makefile |

### 4.2 网络驱动
| 驱动 | 状态 | 源码落点 |
|------|------|---------|------|
| RTL8139 | ✅ | `rtl8139_64.c` | 10/100 PCI |
| e1000 | ✅ | `e1000_64.c` | 千兆 PCI |
| PCIe NIC | ❌ | **不在 Makefile** | 无其他有线/无线 |

### 4.3 输入驱动
| 驱动 | 状态 | 源码落点 | 备注 |
|------|------|---------|------|
| PS/2 键盘 (8042) | ✅ | `keyboard64.c` | 中断 IRQ1，读-改-写 配置字节 + 0xF4 开扫描 |
| PS/2 鼠标 | ✅ | `ps2mouse64.c` | IRQ12 |
| USB HID（键盘/鼠标/复合） | ✅ | `usb64.c` | 单设备多端点支持（无线接收器形态） |
| 蓝牙 HCI/L2CAP/HID-over-GATT | ❌ | **不在 Makefile** | 无 `usb64_bt.c` |
| 2.4G 无线 HID 接收器 | ✅ | `usb64.c` | 通过复合 HID 接口兼容 |

### 4.4 音频驱动
| 驱动 | 状态 | 源码落点 |
|------|------|---------|------|
| AC'97 声卡 | ✅ | `ac9764.c` | AC97 标准音频控制器 |

### 4.5 显示驱动
| 驱动 | 状态 | 源码落点 | 备注 |
|------|------|---------|------|
| VBE framebuffer | ✅ | `gfx64.c` + `vg64.c` | Stage2 VBE 模式设置 → 内核读 0x6000 处信息 |
| UEFI GOP 协议 | ❌ | **不在 Makefile** | Stage2 不用 UEFI 引导，内核无 GOP 实现 |
| virtio-gpu | ❌ | **不在 Makefile** | 源码不存在 |

### 4.6 USB 驱动
| 驱动 | 状态 | 源码落点 | 备注 |
|------|------|---------|------|
| EHCI（USB 2.0） | ✅ | `ehci64.c` | QH/qTD/异步列表（模拟器异步列表不执行，真机行为待验证） |
| XHCI（USB 3.0） | ✅ | `xhci64.c` | TRB/EP context/DCI 多端点，复合 HID 支持 |
| UHCI（USB 1.1） | ❌ 不在 64 位 Makefile | 源码仅存于 32 位遗留树 `kernel/drivers/usb/uhci.c`（`uhci.h`）；64 位由 EHCI/XHCI 接管 |
| USB 核心（设备管理） | ✅ | `usb64.c` | 枚举/配置/驱动匹配 |

### 4.7 PCI / APIC / ACPI
| 组件 | 状态 | 源码落点 |
|------|------|---------|------|
| PCI 总线遍历 | ✅ | `pci64.c` |
| APIC（本地+I/O） | ✅ | `apic64.c` |
| ACPI 表解析 | ✅ | `acpi.c` | RSDT/MADT 走高半区映射，解决真机 >=1GB ACPI 表访问 |

---

## 五、用户态生态（实况）

### 5.1 C 语言应用（ELF64）
| 应用 | 位置 | 状态 |
|------|------|------|
| hello / hello_linux | `/bin/` | ✅ 可执行（原生 ELF + Linux 静态 ELF 兼容） |
| forktest | `/bin/` | ✅ fork 隔离验证 |
| sec_test | 可选 `/bin/` | ✅ 安全回归（`make BOOT_SELFTEST=1` 编译开关） |
| shell64（文本终端） | 内核内置 | ✅ F12 打开 |
| hepushell（图形桌面） | 内核内置 | ✅ 默认图形 |
| install64（安装向导） | 镜像 `/bin/` | ✅ MBR+mkfs+引导区+内核拷贝闭环 |
| mbrtool | `/bin/` | ✅ 读 MBR 分区表 |

### 5.2 桌面内置应用（hepushell 内嵌）
| 应用 | 状态 | 备注 |
|------|------|------|
| 文件管理器 | ✅ 可用 | 列表/网格两视图、右键菜单、USB 挂载 |
| 任务管理器 | ✅ 可用 | 真实进程列表/CPU/MEM/KHEAP/块缓存/网络速率曲线 |
| 系统设置 | ✅ 可用 | 语言/驱动/通知位置/注册表/文件关联 接线 |
| 终端 | ✅ 可用 | 命令补全、历史、内核命令集（drv/usb/reg/ps/ifconfig） |
| 计算器 | ⚠️ 占位 | Dock 渲染但未实现 |
| 编辑器 | ✅ 可用 | 轻量代码编辑器：16KB 平铺缓冲区/行号/光标/方向键/Ctrl+S 保存（`hs_apps.c:2050/2242/2280`，启动入口 `hepushell.c:4792`） |
| 浏览器 | ❌ 占位 | 提示 "App content rebuilding" |
| 笔记 | ❌ 占位 | 提示 "App content rebuilding" |

### 5.3 Rust 用户态 ❌ 未接入构建
- `user/rust/` 目录下有 15+ crate（app-store、file-server、net-server 等）
- **`user/Makefile` 没有 cargo 目标，`tools/make_single_img64.sh` 不打包 Rust 产物**
- `hello-rust.elf` 是历史产物，不是当前构建链的一部分

### 5.4 网络应用
| 功能 | 状态 | 备注 |
|------|------|------|
| DHCP 自动联网 | ✅ | 桌面主循环 `dhcp64_poll` |
| HTTP 客户端（明文） | ✅ | `net64_http.c` |
| DNS 解析 | ✅ | `net64_dns.c`，IP 字面量直返 |
| TLS/HTTPS | ⚠️ 代码已编入、未接入调用链 | 同 3.4：`kernel/net/tls_*.c` 已编入（Makefile:96-100）并在启动期 init，但 HTTP 客户端未走 https/443，顶层会话为模拟握手桩 |
| FTP/SMTP/POP3/WebSocket | ❌ 不在 64 位构建 | WebSocket 仅 32 位遗留树 `kernel/net/websocket.c`；FTP/SMTP/POP3 仅用户态样例 `user/apps/{ftp,smtp,pop3}_client/`，未编入构建 |

### 5.5 第三方库 ❌ 未接入
- **zlib / libpng / libjpeg**：源码在 `user/third_party/{zlib,libpng,libjpeg}/`，但 `user/Makefile` 未编入、用户态 ELF 不链接任何第三方库
- 用户态链接脚本 `user/user.ld` 只有内置 syscall stub

---

## 六、构建与调试

### 6.1 构建系统
```bash
# 64 位内核（必须在 hepu-v0.54.6 根目录）
make -C kernel/arch/x86_64 BUILD_DIR=$(pwd)/build_x86_64
# 产物：build_x86_64/hepu-kernel64.bin

# 镜像打包（256MB 稀疏单镜像）
bash tools/make_single_img64.sh
# 产物：/tmp/hepu64_single.img（LBA13000+ ext2 分区）
```

### 6.2 调试工具
| 工具 | 用法 |
|------|------|
| QEMU 启动（图形） | `qemu-system-x86_64 -m 512M -drive file=/tmp/hepu64_single.img -vga std` |
| QEMU 启动（文本） | 加 `-vga none` 强制 shell64 |
| QEMU 监控 | `-monitor unix:/tmp/hepu_mon.sock,server,nowait` → `sendkey`/`screendump`/`mouse_move` |
| 串口日志 | `-serial file:/tmp/hepu.log` |
| GDB 远程调试 | `qemu-system-x86_64 -s -S ...` → gdb `target remote :1234` |
| debugfs | 读 ext2 镜像交叉验证（宿主工具） |

### 6.3 引导工具
| 工具 | 功能 |
|------|------|
| `tools/make_single_img64.sh` | 生成单镜像（稀疏 + 可选 gz） |
| `tools/make_hybrid_img.sh` | 生成 MBR+GPT 混合镜像 |
| `tools/build_esp.py` | 直写 ESP 分区 Fat32（替代 losetup） |
| `tools/make_gpt.py` | 手写 GPT（保护性 MBR + ESP C12A7328 + ext2 0FC63DAF） |
| `tools/fat32_generator.py` | Fat32 卷创建 |

---

## 七、系统参数（实测）

| 参数 | 值 | 来源 |
|------|-----|------|
| 内核大小 | 约 4-6 MB | `ls -lh build_x86_64/hepu-kernel64.bin` |
| 代码规模 | 100+ C 文件 + 3 ASM | Makefile C_SOURCES/ASM_SOURCES |
| 最大物理内存 | 依赖 stage2 E801 探测（实测 2GB 正常） | `pmm64_init` |
| 进程表上限 | 固定槽位 | `process64_full.c` |
| 默认分辨率 | 800×600 或 1024×768 | VBE 模式 |
| 块缓存 | 256 槽 × 512B | `blk_cache64.c` |
| swap 槽 | 512 × 4KB = 2MB | `swap64.c` |
| socket 池 | 固定数组 | `socket64.c` |
| syscall 编号上限 | SYSCALL_MAX | `syscall64_full.c` |

---

## 八、版本历史（关键里程碑）

| 版本 | 主要内容 |
|------|----------|
| V0.10.x~0.13.x | 32 位 → 64 位迁移，图形桌面 hepushell、USB HID、ext2 |
| **V0.13.43** | **64 位用户态 syscall 链路打通**（syscall 栈布局三 offset 修复） |
| V0.13.46~0.13.49 | 进程管理：ps/kill/看门狗 + 三区制安装向导 + 磁盘 syscall |
| V0.13.54~0.13.59 | 根分区自动扩展 + 出厂镜像 256MB + 统一块缓存 + 多实例挂载 + swap 页换出框架 |
| V0.13.62 | UEFI 引导链重构（build_esp.py + make_gpt.py）+ 真机黑屏回 BIOS 修复 |
| V0.13.63 | AHCI 存储接入 + 内存 E801 探测 + 读盘重试 + ACPI 高半区访问修复 |
| V0.13.64~0.13.66 | EHCI → XHCI 驱动 + 复合 HID + e1000 RX 全链路 + DHCP 自动联网 |
| V0.13.67 | XHCI 多端点复合 HID（无线接收器形态） |
| V0.13.68 | 安全地基：用户指针逐页校验 + 边界检查 + 每进程 fd 表 + ring3 异常只终止自身 |
| V0.14~0.18 | 内存隔离阶段 1（私有 PML4 默认开启）+ 阶段 2（fork 深拷贝 / 高半区 / SMEP / SMAP / brk 分页 NX/W^X） |
| V0.19~0.20 | 安装器闭环 + crypto 入 64 位 + stat/readdir/mmap/munmap/mprotect/chdir/dup/umask/gettimeofday 新 syscall + Linux 翻译表 |
| V0.33 | PIE/ET_DYN 装载跑通 + Linux syscall 翻译按进程 env_type 判定 + glibc static-PIE 端到端 |
| **V0.54.6** | **里程碑：动态链接 P1-7 落地 + 命令规范统一**（该行保留为历史里程碑；当前发布版本以 frontmatter `sys_version` 与 `VERSIONING.md` 为准） |

---

## 九、已知缺口（按 Makefile 未编入计）

以下功能**源码可能存在于仓库但不在 64 位 Makefile C_SOURCES 中**，因此不可用：

1. **NVMe**、**UHCI（USB 1.1）** — 仅 32 位遗留树有源码（`kernel/drivers/nvme.c`、`kernel/drivers/usb/uhci.c`），未编入 64 位 Makefile；**virtio-gpu** 仓库内无源码
2. **VFS 抽象层** — 64 位 ext2 硬编码，无统一文件操作接口（`kernel/fs/vfs.c` 仅 32 位遗留）
3. **fscrypt / 加密文件系统** — 仅 32 位遗留树 `kernel/fs/fscrypt.c`，未编入 64 位构建
4. **IPv6** — 只有 IPv4，仓库内无 IPv6 源码
5. **TLS 1.2 / HTTPS** — 协议代码已编入 64 位（`kernel/net/tls_*.c`，Makefile:96-100）并在启动期 init，但未接入 HTTP/443 调用链，顶层会话为模拟握手桩；crypto 套件（AES/SHA/RSA/X509）已编入，见 3.6
6. **WebSocket / FTP / SMTP / POP3** — WebSocket 仅 32 位遗留树；FTP/SMTP/POP3 仅用户态样例，均未编入 64 位构建
7. **蓝牙 HCI/L2CAP/HID-over-GATT** — 仅 32 位遗留树 `kernel/net/{bluetooth,bt2,bt_gatt,hci,l2cap,rfcomm}.c`，64 位无 `usb64_bt.c`，未接入
8. **IPC 信号量/共享内存/消息队列/互斥锁** — 64 位未实现（`kernel/ipc/ipc_sem.c`/`ipc_mutex.c`/`ipc_fifo.c` 仅 32 位遗留）；64 位仅有命名通道服务 `hsipc.c`，非 POSIX IPC
9. **Secure Boot 签名验证** — 仓库内无源码，引导器不传签名校验
10. **Rust 用户态打包** — `user/rust/` 有 crate，但 `user/Makefile` 未接 cargo、镜像脚本不打包 Rust 产物
11. **第三方库（zlib/libpng/libjpeg）** — 源码在 `user/third_party/`，未编入用户态构建
12. **动态链接** — PT_INTERP 解释器加载已接入 exec（`user_process.c:1938-2128`）；内核不自解析 DT_NEEDED 符号/重定位，交由用户态 ld.so（遗留 `hepu_dynlink_relocate()` 已不被调用）
13. **进程级 swap 换出** — `vmm64_swap_out` 有原语但无调用者（`memory_pressure` 只回收 ext2 缓存）
14. **桌面 P1/P2 增强** — 图标字体、壁纸 Crossfade、主题取色、设计令牌（ROADMAP P1-7 未开始）

---

## 十、开发规范

### 代码规范
- C 语言：C11 标准，4 空格缩进
- 汇编：AT&T 语法（entry_64.S / multiboot_entry.S / user_switch.S）
- 内核编译约束：-m64 -mcmodel=small -mno-red-zone -ffreestanding -fno-pie
- 注释：中文注释，函数头说明

### 提交规范
- 每次交付四件套：源码 + 单镜像 img + 混合镜像 img + 数据盘
- 文档版本与镜像版本对应（`sys_version` frontmatter 必须匹配 VERSIONING.md）
- **错误历史完整记录**（docs/status/ 下的 OPEN_BUGS.md 等）
- 规范化修复，避免错删误判

### 安全规范（已编入内核）
- 所有系统调用必须走 `copy_from_user` / `copy_to_user`（`user_access64.c` CR3 四级页表校验）
- 内核堆分配必须检查返回值 + BSS 越界兜底
- ring0 访问用户页需 STAC/CLAC（SMAP），执行用户页禁止（SMEP）
- 用户堆页必须带 NX 位（EFER.NXE + PT_NX，实现 W^X）

---

*文档结束 · 事实源：kernel/arch/x86_64/Makefile*
