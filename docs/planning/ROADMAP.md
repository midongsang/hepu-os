---
title: 赫普OS 开发路线图（唯一优先级来源）
status: active
sys_version: 0.55.1
updated: 2026-11-11
owner: planning
source_of_truth: kernel/arch/x86_64/Makefile
supersedes: DEVELOPMENT_ROADMAP.md, PRIORITY_ROADMAP.md, X86_64_DEVELOPMENT_PLAN.md
---

# 赫普OS 开发路线图（ROADMAP）

> 本文件为 **唯一优先级来源**。每条一行，带源码落点，避免腐化。
> 状态定义：未开始 | 进行中 | 验证中 | ✅ 已完成（已完成项完整索引见 `status/VERSIONING.md`）。
> 事实源：`kernel/arch/x86_64/Makefile` 中实际编入的 C_SOURCES/ASM_SOURCES。
> 上次全面修订：2026-11-01（对齐 0.54.6 实况，基于 `syscall64_full.c` dispatch 表逐行核对）。

---

## P0 阻塞（不做就无法交付）

| # | 事项 | 源码落点 | 依据 | 状态 | 验收 |
|---|------|---------|------|------|------|
| P0-1 | 安装器闭环：MBR+mkfs+引导区+内核拷贝 | `user/apps/install64/install64.c` | STORAGE_MEMORY_PLAN | ✅ **代码完成 (v0.19.2)**，待真机双盘验证 | 空盘装完可重启进桌面 |
| P0-2 | 安全地基：地址空间隔离 / SMEP / SMAP / W^X / fork COW | `user_aspace64.c` / `vmm64.c` / `irq64_simple.c` / `user_access64.c` / `swap64.c` | AUDIT_64BIT + OPEN_BUGS SEC-01/02/04/05/07/18 | ⚠️ **隔离主路径已完成；fork/clone 地址空间复制与部分安全项未闭环**。已完成（SEC-01 主路径）：每进程私有 PML4/PDPT 默认开（`Makefile:205` `USER_ISOLATION ?= 1`）、SMEP/SMAP 按 CR4 真实状态门控、brk 分页 + EFER.NXE + PT_NX（用户堆 W^X）、ELF 边界检查 + 用户指针逐页校验、ring3 异常只终止自身。边界：物理内存 >1GB 时内核恒等映射覆盖用户窗口，自动退回旧模式（`user_aspace64.c:76-77`）。未闭环（SEC-02 部分）：COW 缺页框架 `vmm64_try_cow`（`vmm64.c:432-457`，PAGE_COW bit9）在单进程/缺页路径可用；但 fork/clone 地址空间复制未接入该框架——clone 子线程直接共享父进程页表指针（`syscall64_full.c:931` `child+24 = parent page_table`），未做私有副本也未打 COW 标记。仍待解决：SEC-04 capability/uid 能力模型未建（裸盘/reboot/mkfs 仅 PID==0 临时闸；真实用户进程 PID 恒 ≥100，`user_process.c:289/629`）；SEC-05 `sys_readdir` 静态 `names[64][256]`/`types[64]` 不可重入（`syscall64_full.c:1606-1607`）；SEC-07 路径 ≥255 截断而非拒绝（`syscall64_full.c:451`）；SEC-18 ext2 偶发挂死根因未复现。 | 用户程序直接访问内核地址触发 #PF 且只终止该进程；fork/clone 私有副本与能力模型为后续项 |
| P0-3 | 高 LBA 读回 / USB 大容量存储写路径 / sendkey 验证 | `ext2_64.c` / `usb64.c` / `keyboard64.c` | VERSIONING 已知限制 | ⚠️ **sendkey QEMU 端到端已验证；存储读写与真机仍待验证**。sendkey：PS/2 链路每次按键 `g_kbd_irq_count` +2（`keyboard64.c:144/148`），环形缓冲 head/tail 前移并被消费（`keyboard64.c:33-34/277-282`）；HMP `sendkey 2` 使 OOBE 从 1/4 推进到 2/4（像素差 73616 px），账户页输入 h/e/p/u 回显 "hepu"（`tools/gui_test.py:173/221-222`）；挂 `-device piix3-usb-uhci -device usb-kbd` 后走 USB HID 队列同样可用；OOBE 语言页只绑定数字键 1/2（Tab/方向键/回车在该页未绑定属应用设计），真机仍待验证。控制器口径：64 位 `usb64.c` 先探 UHCI（PIIX3 0x7020/0x7112，`usb64.c:342-343`），无则回退 EHCI（`ehci64.c`）/XHCI（`xhci64.c`，`usb64.c:347/357-358`）；Makefile:102-104 编 usb64/ehci64/xhci64。USB BOT 写路径 `usb_storage_write_sectors`（`usb64.c:560`，挂 `storage_hal64.c:97-98`）代码已在，真机写盘待验证；高 LBA 读回待真机。 | sendkey 真机通过；USB 写盘与高 LBA 实机通过 |
| P0-4 | 消除硬编码 PROJECT_ROOT | `kernel/arch/x86_64/Makefile` | 实测 | ✅ **已完成 (v0.18.5)** | 换路径直接 make |
| P0-5 | 蓝牙输入栈（USB BT 适配器 HCI/L2CAP/HID-over-GATT） | `kernel/arch/x86_64/usb64_bt.c`（待建） | 用户需求：无线鼠标/键盘（2.4G HID 已支持，蓝牙鼠标需 BT 栈） | 未开始 | 蓝牙鼠标可配对并驱动指针 |
| P0-6 | 驱动在线下载前的签名校验 | `kernel/arch/x86_64/kmod64.c` + crypto 模块 | P0-2 已编入 crypto.c（XTEA），需扩到公钥验证 | 未开始 | 未签名驱动被拒绝加载 |

> **依赖说明**：
> - P0-5 蓝牙栈与 P0-6 驱动签名涉及不可信输入，须带 fuzz 与签名校验
> - P0-2 安全地基 2026-11-11 复核：隔离主路径已关闭（SEC-01）；COW 缺页框架已就位（`vmm64.c:432-457` + `swap64` 缺页路径），但 fork/clone 私有副本未接入 COW（SEC-02 部分）；进程级 swap 换出未接入内存压力主循环（P1-4）；capability 模型、readdir 静态表、路径长度策略、SEC-18 根因仍待解决

---

## P1 应用生态门槛

| # | 事项 | 源码落点 | 状态 |
|---|------|---------|------|
| P1-1 | syscall 集完善（fork/execve/waitpid/pv/sigaction/kill/mmap/munmap/mprotect/pipe/ioctl/stat/readdir/lseek/chdir/dup/umask/gettimeofday/socket 全套/epoll/futex/clock_gettime/...） | `syscall64_full.c`（dispatch 表 100+ 项） | ✅ **已完成**（2026-11-01 复核：pipe/waitpid/ioctl/sigaction/kill 均已编入 syscall_table，fork COW 框架已就位；无 BSD signal() 语义但 sigaction 替代） |
| P1-2 | ELF64 装载 + Linux 兼容层 | `elf64.c` + `syscall64_full.c linux_map[350]` | ✅ **基本完成**：PIE/ET_DYN 装载已跑通（装载偏置 + RELATIVE + SysV 初始栈 + auxv AT_PHDR/AT_ENTRY/AT_PHENT）；**PT_INTERP 交由 P1-7 处理**（内核加载解释器、ET_DYN 的 RELATIVE 交给用户态 ld.so）；IRELATIVE 由内核调用 resolver 写回 GOT；**glibc static-PIE 已端到端跑通并正常 exit_group 退出**（入口 GPR 按 Linux ABI 清零，SEC-10/SEC-13）；ABI 判定按进程 `env_type`（ENV_NATIVE / ENV_LINUX_COMPAT），`.note.ABI-tag` 通道已修复 |
| P1-3 | TLS/HTTPS 网络加密 | `kernel/net/tls_*.c` + `kernel/crypto/` | ⚠️ **部分落地**：crypto 11 个文件（XTEA/XTS + AES-128/256-GCM/CBC、SHA-256/512、HMAC、HKDF、ECDHE、RSA、X.509）已编入 Makefile:85-95；TLS 5 个文件（`tls.c`/`tls_record.c`/`tls_crypto.c`/`tls_handshake.c`/`tls_client.c`，2327 行）已编入 Makefile:96-100 并在启动期 `hepu_tls_init()`（`kernel_main_64.c:1107`）初始化；但 HTTP 客户端 `net64_http.c` 无 https/443 路由，顶层会话为模拟握手桩，端到端 HTTPS 未打通 |
| P1-4 | swap / 内存压力闭环（进程级换出） | `swap64.c` + `memory_pressure.c` | ⚠️ **框架已有但未闭环**：`vmm64_swap_out`/`swap_in` 原语 + 槽位图管理 + swap.hepu 懒扩展（ext2 文件后端）+ 缺页自动换入路径均已实现；但 `memory_pressure.c` 只回收 ext2 块缓存，未在压力等级 1/2 触发进程页换出（无调用者） |
| P1-5 | /cfg /data 挂载策略与文件级读写实机验证 | `ext2_64.c`（mount slot 1/2） | 验证中 |
| P1-6 | 桌面 P1 增强（令牌化、图标字体、壁纸 Crossfade、主题取色） | `hepushell.c` + `gfx64.c` | ❌ **未开始**：hepushell Dock 目前渲染字母/位图，无图标字体；gfx64 有 `gfx64_blur_region()` 可复用但 Crossfade 无接线；设计令牌（Design Tokens）体系未建立 |
| P1-7 | 动态链接器 ld-linux.so.2 + DT_NEEDED 共享库 | `elf64.c` + `user_process.c` + `syscall64_full.c` | ✅ **已完成（用户态 ld.so 方案）**：内核按 PT_INTERP 加载 `/lib64/ld-linux-x86-64.so.2` 并给出 AT_BASE/AT_ENTRY/AT_PHDR 等 auxv，DT_NEEDED 依赖链、GNU hash、RELR/TLS 全部由 glibc 的 ld.so 在用户态完成；内核不为 ET_DYN 应用 RELATIVE 重定位。配套修复：`openat`/`fstat` 从桩改为真实现（含 st_dev/st_ino，否则 ld.so 把 libc 误判成主程序）、`pread64`、文件偏移 `mmap(MAP_FIXED)`、syscall 入口第 6 参数（mmap offset）丢失。验收：`exec /bin/glibchello.elf` 端到端输出 `Hello, World!`（tools/gui_test.py --scenario dyn） |

> **依赖说明**：
> - P1-2 动态链接器复用 `elf64_load`（已加固，边界检查见 AUDIT_64BIT 漏洞 D）
> - P1-3 TLS 协议实现须先扩 crypto（AES-GCM/SHA-256/RSA 至少 128-bit）
> - P1-4 swap 闭环在 ext2 文件后端（`/rootfs/swap.hepu`）已验证槽读写一致，但内存压力触发路径未接入
> - P1-7 共享库支持是 glibc 动态程序的前置条件；P1-7 已按「用户态 ld.so」落地，Qt6/FFmpeg 等需要更多 .so 的场景仍待逐个补齐依赖

---

## P2 成熟度

| # | 事项 | 源码落点 | 状态 |
|---|------|---------|------|
| P2-1 | 三区制：重装保留 /cfg、默认装 /data 的软件安装器 | `install64.c` | ❌ **未开始**（当前安装向导写三区后重启无冲突；重装覆盖 /cfg 的逻辑未实现） |
| P2-2 | 大文件 IO 性能（统一块缓存落地全部驱动） | `blk_cache64.c` | ✅ **已完成（基础）**：块缓存已挂 storage_hal 层，全部存储驱动经统一缓存；写穿路径 `blk_cache_invalidate` 已提供 |
| P2-3 | 任务管理器完善 | `taskmgr64.c`（352 行） | ⚠️ **部分**：真实进程列表/CPU/MEM/KHEAP/块缓存/网络速率曲线已接线；**无结束进程（K+PID 快捷键）、无进程中断、无完整网络速率面板** |
| P2-4 | 文件管理器完善 | `filemgr64.c`（278 行） | ⚠️ **部分**：列表/网格两视图 + 右键菜单 + 搜索过滤 + USB 挂载/卸载已接线；**无共享、无图标预览、无完整右键菜单** |
| P2-5 | 系统设置完善 | `syssettings64.c` | ⚠️ **部分**：语言切换 + 驱动管理 + 通知位置 + 注册表/文件关联管理行已接线；**无权限控制、无软件管理面板、无文件关联完整编辑器** |
| P2-6 | 多语言与系统语言切换 | `l10n64.c`（103 行） | ⚠️ **部分**：中英字符串表 + 语言切换接线；接入面窄（只有 hepushell 部分组件用） |
| P2-7 | Dock 占位应用落地（Calculator → Notes → About） | `hepushell.c` Dock 渲染 | ❌ **未开始**：5 个占位应用（Browser/Editor/Calculator/Notes/About）渲染 "App content rebuilding"；Calculator 最简单可先做 |

---

## P3 远期

| # | 事项 | 源码落点 | 状态 |
|---|------|---------|------|
| P3-1 | 兼容层：Linux/Windows/Android/Apple 软件与驱动 | `kernel/shared/compat/` | ❌ **规划中**：Linux 兼容层按进程 env_type 判定已做；Windows 兼容层只有 `gen_winhello.py` 生成器（无加载器）；Mach-O 有 `gen_macho.py` 生成器（无加载器） |
| P3-2 | GPS 定位 + 天气联网显示 | `gps64.c`（已编入 Makefile） | ⚠️ **部分**：gps64.c 存在但无任何网络调用；天气显示依赖 HTTP（已做）+ 无 TLS |
| P3-3 | 驱动在线下载（联网后自动匹配安装） | `kmod64.c`（已编入 Makefile） | ❌ **规划中**：kmod64.c 存在但无 HTTP 下载路径；依赖 P1-3 TLS 签名校验 |
| P3-4 | Rust 用户态服务 | `user/rust/` 目录 | ❌ **未接入构建**：15+ crate 存在但 `user/Makefile` 无 cargo 目标、`make_single_img64.sh` 不打包 Rust 产物；`hello-rust.elf` 是历史产物 |
| P3-5 | 安全审计闭环（crypto/security 模块扩到 AES/SHA/RSA/TLS） | `kernel/crypto/` + `kernel/net/tls_*.c` | ⚠️ **部分**：AES-GCM/SHA-256/RSA/X.509 等套件代码已编入（Makefile:85-95），TLS 协议代码已编入但未接入 HTTP 调用链；证书链校验、端到端 HTTPS 仍待闭环 |
| P3-6 | VFS 抽象层 | `kernel/fs/vfs.c`（32 位遗留） | ❌ **64 位未建**：当前 ext2 硬编码；ext4/exFAT/NTFS 等仅有 32 位遗留源码，未接入 64 位主线 |
| P3-7 | NVMe / virtio-gpu 驱动 | NVMe：`kernel/drivers/nvme.c`（32 位遗留）；virtio-gpu 无源码 | ❌ **不在 64 位 Makefile**：64 位树无 `nvme64.c`/virtio-gpu 驱动 |
| P3-8 | Secure Boot 签名验证 | 引导器待建 | ❌ **源码不存在** |
| P3-9 | IPC 原语（信号量/共享内存/消息队列/互斥锁） | `kernel/ipc/ipc_sem.c`/`ipc_mutex.c`/`ipc_fifo.c`（32 位遗留） | ❌ **64 位未实现**：64 位 syscall 表无 shmget/semget/msgget；V0.54 阶段不走微内核路线 |
| P3-10 | IPv6 网络栈 | 待建 | ❌ **源码不存在** |

---

## 已完成（里程碑索引，完整记录见 VERSIONING.md）

以下事项在 `kernel/arch/x86_64/Makefile` 中确认为已编入的源码：

### 内核基础
| 里程碑 | 版本 | 源码落点 |
|--------|------|---------|
| x86_64 长模式启动 | v0.10.6 | `multiboot_entry.S` + `entry_64.S` |
| 用户态 syscall 链路打通（栈布局三 offset 修复） | v0.13.43 | `entry_64.S` + `syscall64_full.c` |
| ELF64 加载器 + 边界检查 | v0.13.43~v0.13.68 | `elf64.c` |
| 每进程私有 PML4 地址空间隔离（默认开启） | v0.14 | `user_aspace64.c` |
| 高半区直接映射 / RAM>1GB 支持 | v0.15 | `vmm64.c` |
| COW 缺页框架（fork/clone 私有副本未闭环，SEC-02 部分） | v0.15~v0.18 | `vmm64.c:432-457` + `swap64.c`；clone 共享父页表 `syscall64_full.c:931` |
| SMEP + SMAP + brk 分页 NX/W^X | v0.16~v0.18 | `irq64_simple.c` + `vmm64.c` |
| 用户指针逐页校验 + ring3 异常只终止自身 | v0.13.68 | `user_access64.c` + `lib64.c` |

### 文件系统与存储
| 里程碑 | 版本 | 源码落点 |
|--------|------|---------|
| ext2 文件系统完整实现 | v0.13+ | `ext2_64.c` + `ext2_algo.c` |
| 统一块缓存层（LRU 256 槽 × 512B） | v0.13.58 | `blk_cache64.c` |
| swap 页换出/换入原语 | v0.13.59 | `swap64.c` |
| MBR 分区解析 + 三区制挂载 | v0.13.51 | `partition64.c` + `ext2_64_mount_slot` |
| 根分区自动扩展到全分区 | v0.13.54 | `ext2_64_expand_to_partition` |
| 内核版 mkfs + 开机自动格式化 P2/P3 | v0.13.56 | `ext2_64_mkfs` |
| AHCI SATA 驱动接入存储 HAL | v0.13.63 | `ahci64.c` |
| ext2 fsck | v0.13+ | `ext2_fsck64.c` |

### 网络协议栈
| 里程碑 | 版本 | 源码落点 |
|--------|------|---------|
| RTL8139 + e1000 双网卡驱动 | v0.13+ | `rtl8139_64.c` + `e1000_64.c` |
| ARP / IPv4 / ICMP / UDP / TCP | v0.13+ | `net64_*.c` |
| DHCP 自动联网（桌面主循环 poll） | v0.13.65 | `net64_dhcp.c` |
| DNS 解析 | v0.13.65 | `net64_dns.c` |
| HTTP/1.0 明文客户端 | v0.13+ | `net64_http.c` |
| TCP 重传定时器接线 | v0.54.6 | `net64_tcp.c` |
| Socket API（BSD 兼容）+ TCP 重传恢复 | v0.54.6 | `socket64.c` |

### 输入 / USB
| 里程碑 | 版本 | 源码落点 |
|--------|------|---------|
| PS/2 键盘 + IRQ1 + 0xF4 开扫描 | v0.54.6 | `keyboard64.c` |
| PS/2 鼠标 | v0.13+ | `ps2mouse64.c` |
| EHCI（USB 2.0）驱动 | v0.13.64 | `ehci64.c` |
| XHCI（USB 3.0）驱动 + 复合 HID + 多端点 | v0.13.66~v0.13.67 | `xhci64.c` |
| USB BOT Mass Storage + SCSI READ10/WRITE10 | v0.13.35~v0.13.41 | `usb64.c` |
| USB 挂载 + U 盘 ext2 读写 | v0.13.36~v0.13.41 | `usb64.c` + `storage_hal64.c` |

### 图形桌面
| 里程碑 | 版本 | 源码落点 |
|--------|------|---------|
| VBE framebuffer 渲染 | v0.13+ | `gfx64.c` + `vg64.c` |
| TrueType 字体解析 | v0.13+ | `ttf64.c` + `font64.c` |
| CJK 中文字体（GB2312 全汉字集） | v0.13+ | `cjk_font_data.c` |
| hepushell（Quickshell 风格图形桌面） | v0.13+ | `hepushell.c` |
| 窗口管理 / 托盘 / Dock / 控制中心 / 锁屏 | v0.13+ | `hepushell.c` + `window64.c` |
| 终端（F12 打开，Tab 补全，命令历史） | v0.13+ | `term64.c` |
| 通知系统 | v0.13.33~v0.13.34 | hepushell 内嵌 |
| AC'97 音频驱动 | v0.13+ | `ac9764.c` |

### 系统调用
| 里程碑 | 版本 | 源码落点 |
|--------|------|---------|
| fork / execve / exit / waitpid | ✅ | `syscall64_full.c` dispatch 表 |
| mmap / munmap / mprotect / stat / readdir | ✅ | `syscall64_full.c` |
| pipe / ioctl / kill / sigaction / sigprocmask | ✅ | `syscall64_full.c` |
| socket / connect / accept / bind / listen / sendto / recvfrom | ✅ | `syscall64_full.c` + `socket64.c` |
| futex / clock_gettime ✅；epoll/eventfd2 为桩返回 0 | ✅/桩 | `syscall64_full.c:1268-1273`（epoll/eventfd2 桩）、`:1302-1311`（futex 真实等待/唤醒） |
| Linux 兼容翻译表（350 项，按进程 env_type 判定） | v0.33.0 | `syscall64_full.c linux_map[350]` |
| PIE/ET_DYN 装载 + glibc static-PIE 端到端 | v0.33.0 | `elf64.c` + `syscall64_full.c` |
| PT_INTERP 解释器加载（v0.33.1 曾拒绝，后被落地取代） | v0.54.x | `user_process.c:1938-2128` + `elf64.c`；DT_NEEDED 交用户态 ld.so |

### 构建与工具
| 里程碑 | 版本 | 源码落点 |
|--------|------|---------|
| 消除硬编码 PROJECT_ROOT | v0.18.5 | Makefile / make_single_img64.sh |
| UEFI 引导链（build_esp.py + make_gpt.py） | v0.13.62 | `tools/` |
| 稀疏镜像（256MB→1.5MB 物理占用） | v0.13.55 | `make_single_img64.sh` |
| 安装向导闭环（MBR+mkfs+引导区+内核拷贝） | v0.19.2 | `install64.c` |
| crypto.c（XTEA + XTS）编入 64 位 | v0.19.6 | `kernel/crypto/crypto.c` |

---

## 变更日志

| 日期 | 变更 |
|------|------|
| 2026-11-11 | **跨文档对齐 OPEN_BUGS 台账**：P0-2 去掉"阶段 1+2 全部完成"口径，隔离主路径（SEC-01）标已完成并附 `Makefile:205`/`user_aspace64.c:76-77` 证据；fork/clone 地址空间复制/COW 降级为 SEC-02 部分（`vmm64.c:432-457`、`syscall64_full.c:931`）；补列 SEC-04/05/07/18 待解决项。P0-3：sendkey 标 QEMU 端到端已验证（PS/2 + USB HID 两链路），真机待验；控制器口径更正为先探 UHCI、无则 EHCI/XHCI 回退；USB 写路径待真机。里程碑索引：fork COW 改部分、epoll/eventfd2 标桩、PT_INTERP 改"解释器已落地"。 |
| 2026-11-01 | **全面修订至 0.54.6 实况**：sys_version 从 0.33.1 → 0.54.6；P1-1 状态从 "⏳ pipe/signal/waitpid/ioctl（卡 SEC-03 fork）" 改为 "✅ 已完成"（dispatch 表逐行核对）；P0-2 安全地基明确阶段 1+2 全部完成；新增 P1-3 TLS/HTTPS 明确标注 crypto.c 只有 XTEA；新增 P1-4 swap 闭环状态从"进行中"细化为"框架已有但未闭环"；修正 over 10 处版本描述与 Makefile 实况对齐；新增"已完成"里程碑索引（6 大类） |
| 原版本 | 初始版本（v0.33.1 口径，update 字段错误写 2026-11-05） |

---

*文档结束 · 事实源：kernel/arch/x86_64/Makefile · syscall64_full.c dispatch 表*
