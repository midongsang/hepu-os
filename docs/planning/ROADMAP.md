---
title: 赫普OS 开发路线图（唯一优先级来源）
status: active
sys_version: 0.63.0
updated: 2026-11-26
owner: planning
source_of_truth: kernel/arch/x86_64/Makefile
supersedes: DEVELOPMENT_ROADMAP.md, PRIORITY_ROADMAP.md, X86_64_DEVELOPMENT_PLAN.md
---

# 赫普OS 开发路线图（ROADMAP）

> 本文件为 **唯一优先级来源**。每条一行，带源码落点，避免腐化。
> 状态定义：未开始 | 进行中 | 验证中 | ✅ 已完成（已完成项完整索引见 `status/VERSIONING.md`）。
> 事实源：`kernel/arch/x86_64/Makefile` 中实际编入的 C_SOURCES/ASM_SOURCES。
> 上次全面修订：2026-11-18（对齐 0.62.0 实况；逐条依据见 `docs/planning/ROADMAP_AUDIT_2026-11-18.md`）。
> 2026-11-25：版本号统一到 0.62.1，路线条目与优先级未变。

---

## P0 阻塞（不做就无法交付）

| # | 事项 | 源码落点 | 依据 | 状态 | 验收 |
|---|------|---------|------|------|------|
| P0-1 | 安装器闭环：MBR+mkfs+引导区+内核拷贝 | `user/apps/install64/install64.c` | STORAGE_MEMORY_PLAN；VERSIONING 0.60.7 批次 OPEN-04 | ✅ **QEMU 端到端已闭环 (0.60.7)**：装好的盘可单独 UEFI 启动进桌面（`NONBLACK=2073597`，fsck clean 74/30720 files，含 `/bin` 27 个 ELF）；**真机双盘验证仍待做** | 空盘装完可重启进桌面（QEMU 已验，真机待验） |
| P0-2 | 安全地基：地址空间隔离 / SMEP / SMAP / W^X / fork COW | `user_aspace64.c` / `vmm64.c` / `irq64_simple.c` / `user_access64.c` / `swap64.c` | AUDIT_64BIT + OPEN_BUGS SEC-01/02/04/05/07/18 | ⚠️ **隔离主路径已完成；fork COW 已闭环，clone 已启用（单核内用户线程调度），部分安全项未闭环**。已完成（SEC-01 主路径）：每进程私有 PML4/PDPT 默认开（`Makefile:205` `USER_ISOLATION ?= 1`）、SMEP/SMAP 按 CR4 真实状态门控、brk 分页 + EFER.NXE + PT_NX（用户堆 W^X）、ELF 边界检查 + 用户指针逐页校验、ring3 异常只终止自身。边界：物理内存 >1GB 时内核恒等映射覆盖用户窗口，自动退回旧模式（`user_aspace64.c:76-77`）。未闭环（SEC-02 部分）：COW 缺页框架 `vmm64_try_cow`（`vmm64.c:432-457`，PAGE_COW bit9）在单进程/缺页路径可用；fork 已接入该框架（`uaspace_fork_copy` 深拷贝 PDPT/PD/PT + 物理页共享 + 清 W + `UA_COW`，`user_aspace64.c:165`，由 `sys_fork` 调用 `syscall64_full.c:1875`）；**clone 已启用**——`g_clone_threads_enabled=1`，V0.57.6 单核内用户线程调度闭环（SEC-20）；跨核调度与收尾边界仍见 SEC-20/SB-25。仍待解决（2026-11-25 复核）：**SEC-04 进程凭据已落地，能力对象句柄尚未统一**：能力侧 `kernel/security/capability.c`（32/共享）+ `kernel/arch/x86_64/capability64.c` 已有 mint/derive/revoke/check + 代际失效（`generation` 递增使旧句柄失效，`capability64.c:117-123/201-202`），`capability64.c` 编入 64 位 Makefile:66；拒绝即审计环 `cap_audit_rec_t`（256 条，只记拒绝）+ `SYS_CAP_AUDIT`（commit `bb5565b`、`95eed32`；环实现 `capability64.c:159-167`）。已闭环：① 凭据改为每进程 `proc_cred_t`（`kernel/include/user_cred.h`），uid/gid/euid/egid/umask 按进程隔离；② fork 子进程经 `g_fork_creds[]` 快照继承，子进程 setuid 不污染父进程，exec 继承调用者；③ 裸盘读写/mkfs/reboot 改走 `syscall_priv_allowed()` 的 RAW_DISK/MKFS/REBOOT 能力位，安装器白名单授予能力位并保留旧白名单兜底（`syscall64_full.c`、`user_process.c`）。未闭环：capability64 对象主体仍按 `domain_id` 绑定，未与 uid 主体统一；`sys_kill` 仍仅限自身。真实 ring3 负向测试已补：`/bin/credtest.elf` 以 uid=1000 调 `disk_read` 返回 -1，`[CREDTEST] PASS`。**SEC-05 readdir 已闭环**：0.60.7 撤 `names[64][256]`/`types[64]` 16KB 常驻 BSS，改调用期 `kmalloc/kfree`；2026-11-25 移除全局 `g_readdir_lock` 与 2048B 内部打包上限，改为按用户缓冲真实长度校验。SEC-07 路径超长已拒绝（`copy_user_path` 在 `n >= dstsz-1` 时清空并返回 -1）；SEC-18 ext2 偶发挂死根因未复现。 | 用户程序直接访问内核地址触发 #PF 且只终止该进程；fork/clone 私有副本与能力模型为后续项 |
| P0-3 | 高 LBA 读回 / USB 大容量存储写路径 / sendkey 验证 | `ext2_64.c` / `usb64.c` / `keyboard64.c` | VERSIONING 已知限制；0.60.7 驱动四域实测批次 | ⚠️ **sendkey QEMU 端到端已验证；0.60.7 驱动四域（xHCI/AHCI/E1000/HPET）实测闭环**（修 xHCI BAR0 64 位取值、AHCI 四处 FIS 缺陷、E1000 注册误判、HPET 校准基准）；**USB 写盘与高 LBA 真机读回仍待验证**。sendkey：PS/2 链路每次按键 `g_kbd_irq_count` +2（`keyboard64.c:144/148`），环形缓冲 head/tail 前移并被消费（`keyboard64.c:33-34/277-282`）；HMP `sendkey 2` 使 OOBE 从 1/4 推进到 2/4（像素差 73616 px），账户页输入 h/e/p/u 回显 "hepu"（`tools/gui_test.py:173/221-222`）；挂 `-device piix3-usb-uhci -device usb-kbd` 后走 USB HID 队列同样可用；OOBE 语言页只绑定数字键 1/2（Tab/方向键/回车在该页未绑定属应用设计），真机仍待验证。控制器口径：64 位 `usb64.c` 先探 UHCI（PIIX3 0x7020/0x7112，`usb64.c:342-343`），无则回退 EHCI（`ehci64.c`）/XHCI（`xhci64.c`，`usb64.c:347/357-358`）；Makefile:102-104 编 usb64/ehci64/xhci64。USB BOT 写路径 `usb_storage_write_sectors`（`usb64.c:560`，挂 `storage_hal64.c:97-98`）代码已在，真机写盘待验证；高 LBA 读回待真机。 | sendkey 真机通过；USB 写盘与高 LBA 实机通过 |
| P0-4 | 消除硬编码 PROJECT_ROOT | `kernel/arch/x86_64/Makefile` | 实测 | ✅ **已完成 (v0.18.5)** | 换路径直接 make |
| P0-5 | 蓝牙输入栈（USB BT 适配器 HCI/L2CAP/HID-over-GATT） | `kernel/arch/x86_64/usb64_bt.c`（待建） | 用户需求：无线鼠标/键盘（2.4G HID 已支持，蓝牙鼠标需 BT 栈） | 未开始 | 蓝牙鼠标可配对并驱动指针 |
| P0-6 | 驱动在线下载前的签名校验 | `kernel/arch/x86_64/kmod64.c` + crypto 模块 | P0-2 已编入 crypto.c（XTEA），需扩到公钥验证 | 未开始 | 未签名驱动被拒绝加载 |

> **依赖说明**：
> - P0-5 蓝牙栈与 P0-6 驱动签名涉及不可信输入，须带 fuzz 与签名校验
> - P0-2 安全地基 2026-11-18 复核：隔离主路径已关闭（SEC-01）；COW 缺页框架已就位（`vmm64.c:432-457` + `swap64` 缺页路径），fork 私有副本已接入 COW（`uaspace_fork_copy`），clone 返回 ENOSYS（SEC-02 部分）；进程级 swap 换出未接入内存压力主循环（P1-4）；SEC-04 已落地每进程凭据/fork 快照/高危能力位 + ring3 非 root 负向测试（能力对象句柄与 uid 统一仍待补）；readdir 16KB 静态表已撤且去全局锁/2048B 上限（SEC-05 闭环）；路径长度策略、SEC-18 根因仍待解决

---

## P1 应用生态门槛

| # | 事项 | 源码落点 | 状态 |
|---|------|---------|------|
| P1-1 | syscall 集完善（fork/execve/waitpid/pv/sigaction/kill/mmap/munmap/mprotect/pipe/ioctl/stat/readdir/lseek/chdir/dup/umask/gettimeofday/socket 全套/epoll/futex/clock_gettime/...） | `syscall64_full.c`（dispatch 表 100+ 项，号表见 `specs/SYSCALL_ABI.md` 源码生成） | ⚠️ **主体完成，非全部**（2026-11-18 复核）：pipe/waitpid/ioctl/sigaction/kill/futex/clock_gettime 已有实现；**`epoll_create`/`epoll_ctl`/`epoll_wait`/`eventfd2`/`pselect6` 已由静默桩改为 v1 最小 LT 真实现**（`kernel/arch/x86_64/epoll64.c`/`epoll64.h`，薄壳 `syscall64_full.c:1280-1296`；探针 `user/apps/epolltest.c`，真实 QEMU 串口 23 项断言 `[EPOLL] RESULT: PASS`）：仅水平触发，EPOLLET/EFD_SEMAPHORE→EINVAL，eventfd 读空 EAGAIN，pselect 仅判读位图；限制与后续接入点见 `specs/SYSCALL_ABI.md` 末尾手写小节；`sys_sleep` 为忙等（`syscall64_full.c:2122`，计数空转非 hlt 让出） |
| P1-2 | ELF64 装载 + Linux 兼容层 | `elf64.c` + `syscall64_full.c linux_map[350]` | ✅ **基本完成**：PIE/ET_DYN 装载已跑通（装载偏置 + RELATIVE + SysV 初始栈 + auxv AT_PHDR/AT_ENTRY/AT_PHENT）；**PT_INTERP 交由 P1-7 处理**（内核加载解释器、ET_DYN 的 RELATIVE 交给用户态 ld.so）；IRELATIVE 由内核调用 resolver 写回 GOT；**glibc static-PIE 已端到端跑通并正常 exit_group 退出**（入口 GPR 按 Linux ABI 清零，SEC-10/SEC-13）；ABI 判定按进程 `env_type`（ENV_NATIVE / ENV_LINUX_COMPAT），`.note.ABI-tag` 通道已修复 |
| P1-3 | TLS/HTTPS 网络加密 | `kernel/net/tls12_client.c` + `kernel/crypto/` | ⚠️ **真实 TLS1.2 客户端已打通，HTTPS 路由未接线**：新增 `tls12_client.c`（真实 ClientHello/ServerHello、证书链/SAN/信任锚校验、RSA PKCS#1 v1.5 密钥交换、双向 Finished、AES-128-GCM 记录层），仅支持 `TLS_RSA_WITH_AES_128_GCM_SHA256 (0x009C)`；64 位测试构建 `TLS_E2E_SELFTEST=1` 在 QEMU 内对 `openssl s_server` 完成真实握手并收 3847 字节 `HTTP/1.0 200`（`main_tls_e2e.log`，merge `cab351d`）；`net64_http.c` 仍无 https/443 路由，TLS1.3/ECDSA/ChaCha20/会话恢复/ALPN 未做 |
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
| P2-7 | Dock 占位应用落地（Calculator → Notes → About） | `hs_apps.c` / `notes64.c` / `desktop64.c` Dock 启动请求（`desktop64.c:353`） | ⚠️ **部分（2026-11-18 复核）**：Notes 内核实现已落地（`notes64.c`，启动期 `notes64_init()` 见 `kernel_main_64.c:1237`，经终端 `notes` 命令调用，`term64.c:244/248/252`）；Editor 有窗口实现（`hs_apps.c:4363/4380`）。**待核实**：Dock 图标点击到 GUI 应用的完整接线，以及 Browser/Calculator/About 是否仍渲染占位；旧"5 个全占位"口径不再沿用 |

---

## P3 远期

| # | 事项 | 源码落点 | 状态 |
|---|------|---------|------|
| P3-1 | 兼容层：Linux/Windows/Android/Apple 软件与驱动 | `kernel/shared/compat/` | ❌ **规划中**：Linux 兼容层按进程 env_type 判定已做；Windows 兼容层只有 `gen_winhello.py` 生成器（无加载器）；Mach-O 有 `gen_macho.py` 生成器（无加载器） |
| P3-2 | GPS 定位 + 天气联网显示 | `gps64.c`（已编入 Makefile）；天气取数路径在 `hpdesk.c` | ⚠️ **部分**：gps64.c 存在但无任何网络调用（源码 grep 无 tcp/http/recv/send）；**天气取数已打通**（BUG-59，commit `308e72b`，换国内源 + 解析适配 + 失败重试），但仍走明文 HTTP、无 TLS（P1-3） |
| P3-3 | 驱动在线下载（联网后自动匹配安装） | `kmod64.c`（已编入 Makefile） | ❌ **规划中**：kmod64.c 存在但无 HTTP 下载路径；依赖 P1-3 TLS 签名校验 |
| P3-4 | Rust 用户态服务 | `user/rust/` 目录 | ❌ **未接入构建**：15+ crate 存在但 `user/Makefile` 无 cargo 目标、`make_single_img64.sh` 不打包 Rust 产物；`hello-rust.elf` 是历史产物 |
| P3-5 | 安全审计闭环（crypto/security 模块扩到 AES/SHA/RSA/TLS） | `kernel/crypto/` + `kernel/net/tls12_client.c` | ⚠️ **部分**：AES-GCM/SHA-256/RSA/X.509 已编入并经 TLS1.2 E2E 实测；证书链校验与双向 Finished 已闭环，但 TLS 仅单一 RSA-GCM 套件、未接 HTTP 调用链，安全审计仍需覆盖新 TLS 路径 |
| P3-6 | VFS 抽象层 | 64 位：`kernel/arch/x86_64/vfs64.*`；32 位遗留 `kernel/fs/vfs.c` | 🚧 **进行中（常规文件/目录路径已走 VFS，ioctl/mmap 与多 FS 后端待续）**：vfs64 node/ops/mount/路径解析 + ext2 后端 + 只读伪设备后端已落地；read/write/pread/stat/open/readdir/getdents/unlink/mkdir/rmdir 经 VFS 路由（未命中回退 ext2 直调）；ioctl/mmap/页缓存、getdents fd→路径映射、ext4/exFAT/NTFS 后端为 deferred；详见 `docs/design/VFS64.md` |
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
| COW 缺页框架（fork 已接入；clone 改为 ENOSYS，SEC-02 部分） | v0.15~v0.18 | `vmm64.c:432-457` + `swap64.c`；clone 共享父页表 `syscall64_full.c:931` |
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
| futex / clock_gettime ✅；epoll/eventfd2/pselect6 v1 LT 真实现 | ✅ | `epoll64.c`（注册表/就绪判定/wait/eventfd/pselect）、`syscall64_full.c:1280-1296`（薄壳）；探针 `user/apps/epolltest.c` 串口 PASS；`:1327-1334`（futex 真实等待/唤醒） |
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

### 多架构移植（arch_port + ARM64 M0–M1d）

> 第二架构线，2026-11-18 按 `kernel/arch/aarch64/` 源码与 HEAD 提交核对。

| 里程碑 | 状态 | 源码落点 / 依据 |
|--------|------|----------------|
| arch_port 抽象层（`arch_ticks` / `arch_console_puts` / `arch_poweroff` 三移植点，刻意避开已被 H-HAL 占用的 `hal_*` 前缀） | ✅ 已落地（commit `3afe24d` 引入移植点、`889b8cb` 第二架构实现） | 声明 `kernel/include/arch_port.h`；实现 `arch/x86_64/arch_port_x86_64.c`（64 位 Makefile:159）、`arch/x86/arch_port_x86_32.c`、`arch/aarch64/arch_port_aarch64.c`（41 行：CNTVCT_EL0 / PL011 @0x09000000 / PSCI SYSTEM_OFF）。注：`arch_port.h:19` 注释仍写 arm64「待建」，与实况不符，待同步 |
| ARM64 M0：UEFI 引导 + GOP 800×600 帧缓冲填色 + PSCI 关机 | ✅ 已通（`b0e1809`） | `arch/aarch64/m0/hepu_m0_a64.c`；PE 包装 `tools/pe_flatten_aarch64.py`；串口证据见 `arch/aarch64/README.md` |
| M1a：自建页表并切 TTBR0_EL1（48 位布局） | ✅（`2b64fd0`） | `arch/aarch64/m1/m1.c` |
| M1b：排除 WXN 假设，锁定 tlbi 后崩溃为真崩溃（非串口静音/非 2MB block 属性错） | 实验结论（`f79826a`） | `m1.c`；结论记录 `arch/aarch64/README.md` M1b 段 |
| M1c：4KB 页（L3）白名单映射 + VBAR 异常向量，tlbi 后存活、QEMU exit=0 | ✅（`54c7bb6`） | `m1.c` |
| M1d-1：GetMemoryMap 驱动全 RAM 混合映射（整 2MB 对齐 L2 block，首尾不齐 L3 4KB 页）+ 远端 RAM 写读回 | ✅（`391e1db`） | `m1.c` |
| M1d-2：GIC 版本判定 + 虚拟定时器 PPI27 中断计数（EOIR 后重 arm） | ✅（`a60ffdb`） | `m1.c` GICD/GICC @0x08000000/0x08010000 |
| M1d-3：ExitBootServices + 描述符静态副本 + 自有 bss 栈切换 | ✅ **已完成**（M1d 三步，2026-11-18 前收口）：ExitBootServices 返回 Status=0；切换 16KB 自有 bss 栈后继续运行；退出 Boot Services 后 GOP `0x58430000` 仍可写（写 `0x00ff00ff` 读回一致）。QEMU `exit=0` | 代码在 `kernel/arch/aarch64/m1/m1.c`；M1d 自检三次全 exit=0（全 RAM 混合映射 244 block+5376 页、GICv2+虚拟定时器 20 次 PPI27、EBS 换栈后 GOP 可写） |
| 复用层接入：`shared/desktop`、`gfx64` 绘制基元接到 GOP 帧缓冲出界面 | ❌ 未开始 | — |

> 边界说明（2026-11-18 更新）：M1d-3 已完成，ARM64 已退出 Boot Services 并切自有 bss 栈；随后 M2/M2D/M2S 已出界面并完成脏区量化（详见 `kernel/arch/aarch64/README.md` 与 `docs/status/PERF_BASELINE_2026-11-18.md`）。32 位根 Makefile 已排除 `arch/aarch64/%` 源文件（0.62.0 批次第 6 项）。

---

## 变更日志

| 日期 | 变更 |
|------|------|
| 2026-11-18 | **对齐 0.62.0 实况（A 段事实修正）**：front matter sys_version 0.58.0→0.62.0、updated→2026-11-18；P0-1 安装器标 QEMU 端到端闭环（0.60.7 OPEN-04，`NONBLACK=2073597`），真机双盘仍待验；P0-2 SEC-04 拆分——能力对象模型 + 拒绝审计环已建（`capability.c`/`capability64.c`，`bb5565b`/`95eed32`），uid/凭据主体绑定、per-process 凭据继承、高危闸改走 capability check 仍待解决；SEC-05 readdir 16KB 静态表已撤（0.60.7），残留全局锁串行；P0-3 补 0.60.7 驱动四域（xHCI/AHCI/E1000/HPET）实测闭环；P1-1 桩行号更新到 `syscall64_full.c:1255-1263` 并补 pselect6；P3-2 天气取数打通（BUG-59 `308e72b`）仍无 TLS；P2-7 Notes/Editor 部分落地；里程碑索引新增「多架构移植」小节（arch_port + ARM64 M0–M1d）。逐条依据见 `ROADMAP_AUDIT_2026-11-18.md`。 |
| 2026-11-18 | **B 段：epoll/eventfd2/pselect6 桩改 v1 真实现**：`epoll_create/ctl/wait`、`eventfd2`、`pselect6` 由静默 `return 0` 改为最小 LT 实现（`epoll64.c`/`epoll64.h`，薄壳 `syscall64_full.c:1280-1296`，注册表在 `wrap_close` 回收）；探针 `user/apps/epolltest.c` 真实 QEMU 串口 23 项断言 `[EPOLL] RESULT: PASS`（create fd、pipe ADD/DEL、超时 0、就绪 1、EPOLLIN、eventfd 读写计数/读空 EAGAIN、pselect 位图）。v1 限制：仅 LT，EPOLLET→EINVAL，EFD_SEMAPHORE→EINVAL，EPOLLOUT 未分类，eventfd 读空 EAGAIN 不阻塞，pselect 仅读位图，注册表未按进程隔离；语义与接入点见 `specs/SYSCALL_ABI.md` 末尾手写小节。动态 glibchello 在 HEAD 基线与本内核上均复现 vector=0x8 PANIC（rip=0x1001C5），属既有 dyn 兼容问题、非本次改动引入；32 位 `regress32.sh` 在当前 HEAD 基线即链接失败（`hpdesk.c` 引用 `auth64_user_name/auth64_current_role/rtc64_get_timestamp`，定义方 `arch/x86_64/auth64.c`/`rtc64.c` 被 32 位 filter-out，引入提交 `3f0edb1`）；已 stash 本次改动复测基线同样失败，非 epoll 改动引入，属 32 位/桌面负责人待修。 |
| 2026-11-11 | **跨文档对齐 OPEN_BUGS 台账**：P0-2 去掉"阶段 1+2 全部完成"口径，隔离主路径（SEC-01）标已完成并附 `Makefile:205`/`user_aspace64.c:76-77` 证据；fork/clone 地址空间复制/COW 降级为 SEC-02 部分（`vmm64.c:432-457`、`syscall64_full.c:931`）；补列 SEC-04/05/07/18 待解决项。P0-3：sendkey 标 QEMU 端到端已验证（PS/2 + USB HID 两链路），真机待验；控制器口径更正为先探 UHCI、无则 EHCI/XHCI 回退；USB 写路径待真机。里程碑索引：fork COW 改部分、epoll/eventfd2 标桩、PT_INTERP 改"解释器已落地"。 |
| 2026-11-01 | **全面修订至 0.54.6 实况**：sys_version 从 0.33.1 → 0.54.6；P1-1 状态从 "⏳ pipe/signal/waitpid/ioctl（卡 SEC-03 fork）" 改为 "✅ 已完成"（dispatch 表逐行核对）；P0-2 安全地基明确阶段 1+2 全部完成；新增 P1-3 TLS/HTTPS 明确标注 crypto.c 只有 XTEA；新增 P1-4 swap 闭环状态从"进行中"细化为"框架已有但未闭环"；修正 over 10 处版本描述与 Makefile 实况对齐；新增"已完成"里程碑索引（6 大类） |
| 原版本 | 初始版本（v0.33.1 口径，update 字段错误写 2026-11-05） |

---

*文档结束 · 事实源：kernel/arch/x86_64/Makefile · syscall64_full.c dispatch 表*
