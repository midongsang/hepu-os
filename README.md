# 赫普OS / HEPU Hyper Evolve OS

> 系统归人，数据归己，自由可控，极致纯净

赫普OS是一款自研64位操作系统，支持SMP多处理器、双内核混合引导、完整图形桌面、ext2文件系统和TCP/IP网络协议栈。

## 当前版本

**v0.13.0** — SMP 4核止血版（AP核安全停机，AP调度临时禁用）

> 源码树内部分文件仍保留旧版本字符串（v0.10.x/v0.12.x），属迭代中未同步，以交付镜像版本为准。

## 核心特性

- **64位长模式内核**：166KB，-O2优化，58个模块
- **SMP多处理器**：4核并行，per-CPU调度，负载均衡，IPI核间中断
- **双内核混合引导**：64位内核（166KB）+ 32位内核（4.1MB），启动菜单选择
- **三重引导支持**：BIOS + UEFI + CD-ROM
- **图形桌面环境**：1024x768真彩色，9个应用，TrueType字体
- **ext2文件系统**：完整文件/目录操作（创建/读写/删除）
- **完整网络协议栈**：ARP/IP/ICMP/UDP/TCP/DHCP/DNS/HTTP
- **用户态程序支持**：ELF64加载，ulib C库（30+函数），21个POSIX系统调用
- **统一源码树**：13个共享模块（驱动/文件系统/图形/库/调试/安全）
- **75个Shell命令**：完整的命令行界面

## 项目结构

```
hepu-os/
├── kernel/
│   ├── kernel_main_64.c       # 64位内核主入口
│   ├── arch/x86_64/           # 64位架构相关（58个模块）
│   │   ├── entry_64.S         # 汇编入口
│   │   ├── process64_full.c   # 进程管理与调度
│   │   ├── smp.c              # SMP多处理器
│   │   ├── apic64.c           # APIC中断控制器
│   │   ├── ext2_64.c          # ext2文件系统
│   │   ├── gfx64.c            # 图形子系统
│   │   ├── syscall64_full.c   # POSIX系统调用
│   │   └── ...
│   ├── shared/                # 统一源码树（32/64位共享）
│   │   ├── drivers/           # 驱动框架
│   │   ├── fs/                # ext2共享算法
│   │   ├── gfx/               # 共享图形算法
│   │   ├── lib/               # 共享库函数
│   │   ├── debug/             # 调试与错误处理
│   │   └── security/          # 安全接口
│   └── fonts/                 # 字体文件
├── user/                      # 用户态程序
│   ├── ulib.h                 # 用户态C库
│   ├── hello.c                # Hello World
│   └── shell.c                # 用户态Shell
├── tools/
│   ├── hepboot/               # 自研引导器
│   │   ├── hepboot_mbr.S      # MBR引导桩
│   │   ├── x86_64/stage2_64.S # Stage2（双内核菜单）
│   │   └── uefi/              # UEFI引导器
│   ├── make_single_img64.sh   # BIOS镜像构建
│   ├── build_uefi_dual.py     # UEFI镜像构建
│   └── build_iso.py           # ISO镜像构建
├── docs/                      # 文档
└── Makefile                   # 32位内核构建
```

## 快速开始

### 编译64位内核
```bash
cd kernel/arch/x86_64
make
# 输出: build_x86_64/hepu-kernel64.bin (166KB)
```

### 编译32位内核
```bash
make
# 输出: build_v250/hepu-kernel.bin (4.1MB)
```

### 构建BIOS镜像
```bash
bash tools/make_single_img64.sh
# 输出: /tmp/hepu64_single.img (257MB)
```

### QEMU测试
```bash
# 需要系统安装 qemu-system-x86_64

# 单核测试
qemu-system-x86_64 -m 128 -bios tools/firmware/bios-256k.bin \
  -drive format=raw,file=/tmp/hepu64_single.img -vga std

# 4核SMP测试
qemu-system-x86_64 -m 128 -smp 4 -bios tools/firmware/bios-256k.bin \
  -drive format=raw,file=/tmp/hepu64_single.img -vga std
```

### 环境要求
- **编译**：gcc（x86_64交叉编译）、nasm、make
- **运行**：qemu-system-x86_64（含OVMF固件可测UEFI）
- **脚本**：python3（镜像构建脚本依赖）

### 真机启动
- 使用Rufus **DD模式**写入.img到U盘
- 或使用balenaEtcher
- BIOS设置：Legacy模式，USB第一启动

## 系统调用（21个POSIX兼容）

| 调用号 | 名称 | 功能 |
|--------|------|------|
| 1 | write | 写入文件/控制台 |
| 2 | read | 读取文件/键盘 |
| 3 | open | 打开文件 |
| 4 | close | 关闭文件 |
| 5 | fstat | 文件状态 |
| 6 | exit | 退出进程 |
| 7 | fork | 创建子进程 |
| 8 | lseek | 移动文件指针 |
| 10 | unlink | 删除文件 |
| 11 | execve | 执行程序 |
| 12 | brk | 内存分配 |
| 13 | time | 获取时间 |
| 14 | sleep | 休眠 |
| 20 | getpid | 获取进程ID |
| 21 | access | 文件访问检查 |
| 64 | getppid | 获取父进程ID |
| 79 | getcwd | 当前目录 |
| 82 | rename | 重命名 |
| 83 | mkdir | 创建目录 |
| 84 | rmdir | 删除目录 |

## SMP多处理器

- **4核支持**：BSP + 3个AP全部在线
- **per-CPU调度**：每个CPU独立Local APIC定时器（1000Hz）
- **负载均衡**：进程轮询分配，工作窃取机制
- **IPI核间中断**：支持单目标/广播/其他CPU
- **自旋锁**：保护共享数据结构

## 双内核混合引导

镜像布局：
```
MBR (LBA0) → Stage2 (LBA34-65, 32KB)
  → 64位内核 (LBA2000+, 166KB)
  → 32位内核 (LBA3000+, 4.1MB)
  → ext2根分区 (LBA9000+, 251MB)
```

Stage2启动菜单：5秒倒计时，默认64位，可选择32位。

## 开发状态

### 已完成
- ✅ 64位内核基础（58个模块）
- ✅ SMP多处理器（4核，负载均衡，IPI）
- ✅ 双内核混合引导（64位+32位）
- ✅ 三重引导（BIOS+UEFI+CD-ROM）
- ✅ 图形桌面（9个应用，TrueType字体）
- ✅ ext2文件系统（完整操作）
- ✅ 网络协议栈（TCP/IP+HTTP）
- ✅ 用户态程序（ELF64+ulib库）
- ✅ 21个POSIX系统调用
- ✅ 统一源码树（13个共享模块）
- ✅ 75个Shell命令

### 进行中 / 已知问题
- 🔄 SMP AP核 LAPIC 定时器 triple-fault 根因（疑缺内核 GDT/TSS 加载，ap_trampoline 需 lgdt/ltr）
- 🔄 AP核启用 LAPIC 定时器后的渲染波动（v0.13.0 已临时禁用 AP 调度止血）

### 版本历史
- v0.10.146: SMP基础（4核在线，APIC定时器）
- v0.10.147: 进程迁移与负载均衡
- v0.10.148: IPI核间中断
- v0.10.149: 用户态C库ulib
- v0.10.150: POSIX系统调用扩展（21个）
- v0.10.151: 稳定版本
- v0.10.152: 统一源码树推进（debug模块）
- v0.10.153: 32位内核验证
- v0.10.154: 启动优化
- v0.10.170: 图形桌面与应用迭代（内核启动输出版本）
- v0.11.0~0.11.9: 内核持续迭代（VFS/网络/图形完善）
- v0.12.0: 稳定版本（shell显示版本）
- v0.12.1: 稳定版本修复
- **v0.13.0: SMP 4核止血版**
  - AP核停在安全 hlt，AP调度临时禁用（规避渲染波动）
  - SMP 4核渲染波动三层根因定位：apic_mode 启动时未切换 / AP核 LAPIC 未使能 / AP核不加载 IDT
  - TTF渲染 FPU 保护优化（控制中心稳定性）

## 开源协议

核心代码采用 **GPLv3** 许可证。

## 命名释义

- **赫**：赫然革新、突破壁垒
- **普**：普惠自由、全设备通用
- **HEPU**：Hyper Evolve Pure Union
