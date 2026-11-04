# 赫普OS / HEPU Hyper Evolve OS

> 系统归人，数据归己，自由可控，极致纯净

赫普OS是一款自研64位操作系统，支持SMP多处理器、双内核混合引导、完整图形桌面、ext2文件系统和TCP/IP网络协议栈。

## 当前版本

**0.54.6** — 当前版本（版本号唯一事实源：`docs/status/VERSIONING.md` frontmatter）

## 核心特性

- **64位长模式内核**：约586KB（599,880字节），-O2优化，98个模块
- **SMP多处理器**：4核并行，per-CPU调度，负载均衡，IPI核间中断
- **双内核混合引导**：64位内核（约586KB）+ 32位内核（约3.85MB），启动菜单选择
- **三重引导支持**：BIOS + UEFI + CD-ROM
- **图形桌面环境**：1024x768真彩色，9个应用，TrueType字体
- **ext2文件系统**：完整文件/目录操作（创建/读写/删除）
- **完整网络协议栈**：ARP/IP/ICMP/UDP/TCP/DHCP/DNS/HTTP
- **用户态程序支持**：ELF64加载，ulib C库，104个已实现系统调用（详见 `docs/specs/SYSCALL_ABI.md`）
- **统一源码树**：13个共享模块（驱动/文件系统/图形/库/调试/安全）
- **70个Shell命令**：完整的命令行界面

## 项目结构

```
hepu-os/
├── kernel/
│   ├── kernel_main_64.c       # 64位内核主入口
│   ├── arch/x86_64/           # 64位架构相关（98个模块）
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
# 输出: build_x86_64/hepu-kernel64.bin (约531KB)
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
# 单核测试
tools/bin/qemu-system-x86_64 -m 128 -bios tools/firmware/bios-256k.bin \
  -drive format=raw,file=/tmp/hepu64_single.img -vga std

# 4核SMP测试
tools/bin/qemu-system-x86_64 -m 128 -smp 4 -bios tools/firmware/bios-256k.bin \
  -drive format=raw,file=/tmp/hepu64_single.img -vga std
```

### 真机启动
- 使用Rufus **DD模式**写入.img到U盘
- 或使用balenaEtcher
- BIOS设置：Legacy模式，USB第一启动

## 系统调用（104个已实现）

系统调用 ABI 由 `tools/gen_syscall_doc.py` 从源码 `kernel/arch/x86_64/syscall64_full.c` 自动生成，完整列表见 [`docs/specs/SYSCALL_ABI.md`](docs/specs/SYSCALL_ABI.md)。

核心调用示例：

| 调用号 | 名称 | 功能 |
|--------|------|------|
| 1 | write | 写入文件/控制台 |
| 2 | read | 读取文件/键盘 |
| 3 | open | 打开文件 |
| 4 | close | 关闭文件 |
| 6 | exit | 退出进程 |
| 7 | fork | 创建子进程 |
| 11 | execve | 执行程序 |
| 12 | brk | 内存分配 |
| 33 | mmap | 内存映射 |
| 39 | getpid | 获取进程ID |
| 60 | exit | 退出（Linux ABI 兼容） |
| 79 | getcwd | 当前目录 |

> 注：系统同时维护 Linux x86_64 调用号翻译表，可直接运行主机 `gcc -static` 编译的原生 Linux ELF。

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
  → 64位内核 (LBA2000+, 最多640KB)
  → 32位内核 (LBA3400+, 最多4.1MB)
  → ext2根分区 (LBA13000+, 224MB)
```

Stage2启动菜单：5秒倒计时，默认64位，可选择32位。

## 开发状态

### 已完成
- ✅ 64位内核基础（98个模块，约531KB）
- ✅ SMP多处理器（4核，负载均衡，IPI）
- ✅ 双内核混合引导（64位+32位）
- ✅ 三重引导（BIOS+UEFI+CD-ROM）
- ✅ 图形桌面（9个应用，TrueType字体）
- ✅ ext2文件系统（完整操作）
- ✅ 网络协议栈（TCP/IP+HTTP）
- ✅ 用户态程序（ELF64+ulib库，Linux静态ELF兼容）
- ✅ 104个已实现系统调用
- ✅ 统一源码树（13个共享模块）
- ✅ 70个Shell命令

### 版本历史

详细迭代记录见 [`docs/status/VERSIONING.md`](docs/status/VERSIONING.md)（版本号唯一事实源）。历史归档见 [`docs/history/`](docs/history/) 目录。

## 开源协议

核心代码采用 **GPLv3** 许可证。

## 命名释义

- **赫**：赫然革新、突破壁垒
- **普**：普惠自由、全设备通用
- **HEPU**：Hyper Evolve Pure Union
