---
title: REAL_HARDWARE_TEST
status: active
sys_version: 0.54.12
updated: 2026-11-11
owner: guides
source_of_truth: null
supersedes: null
---

# 赫普OS 真机上机 Checklist（人站在机器前也能照做）

本文档面向**没有 gdb、没有 QEMU monitor** 的真机现场。每一步都写成：目的 → 操作（具体按键/命令/插拔）→ 预期现象（屏幕 + 串口末尾真实字符串）→ 通过判据 → 失败判据 → 取证动作。所有"预期串口输出"均引用源码里真实存在的打印字符串（`文件:行` 可查），不编造。

> 适用范围：64 位内核（`build_x86_64/hepu-kernel64.bin`），镜像由 `tools/make_single_img64.sh` 产出（`/tmp/hepu64_single.img`，256MB raw）。32 位内核路径与 UEFI 路径见各自章节。

---

## 0 前置：真机可观测性（阻塞项）

**在插 U 盘之前，先把可观测性搭起来。否则黑屏之后你什么都看不到。**

赫普OS 的可观测通道只有三条，且前两条在真机上常常不可用：

| 通道 | 出处 | 真机是否可用 |
|------|------|--------------|
| 串口 COM1（0x3F8，115200 8N1） | stage2 `tools/hepboot/x86_64/stage2_64.S:53`；内核 `kernel/kernel_main_64.c:185` | 仅当主板有物理 RS-232 口 + USB 转 TTL/RS-232 线时可用 |
| VGA 文本输出（0xB8000） | `kernel/arch/x86_64/lib64.c:153`（直写 VGA text buffer） | 仅当机器还在 legacy VGA 文本模式时可见；一旦 VBE 切到图形模式，字符走帧缓冲，无 TTF 时黑屏 |
| `hepu_panic` / `[EXCEPTION]` 寄存器转储 | `kernel/arch/x86_64/lib64.c:451`、`:582` | 走上述两条通道，不额外落盘 |

**关键事实（源码核实）：**

- `vga_puts()` 在 `lib64.c:204` 第一行就调 `serial_puts_local()`，所以**所有 vga_puts 输出同时镜像到 COM1**。串口线接上后，屏幕上能看到的每一个字串口上都有一份。
- stage2 在 `stage2_64.S:53` 初始化串口（115200 8N1，`stage2_64.S:576-582`），**早于任何磁盘读**。所以串口上从 `"[Stage2_64] Starting..."` 开始就有全量日志。
- 内核 `uart64_init(0)` 在 `kernel_main_64.c:185`，是 `x86_64_kernel_main()` 里继 `x86_64_enable_fpu_early()` 之后的第一条实质调用（`kernel_main_64.c:181-186`），之后立刻打 `"=== HepuOS 64-bit Kernel booting ===\n"`。
- **崩溃后停机而非重启**：内核态异常在 `lib64.c:577` `while(1) hlt;`；`hepu_panic` 在 `lib64.c:602` 同样 `hlt`。不会自动重启丢证据。但 MBR 读盘失败后是**等按键重启**（`hepboot_mbr.S:84-95`），不是停机——拍照前别乱按键盘。
- **无蜂鸣、无磁盘日志、无 LED**：仓库里没有任何 PC speaker beep 调用，也没有把日志写盘的代码。黑屏 + 串口没接 = 零证据。

### 0.1 真机取证三件套（最小补齐方案，不实施）

| 件 | 用途 | 现状缺口 |
|----|------|----------|
| 串口数据线（USB-A 转 RS-232 / TTL 3.3V，接主板 COM1） | 全量日志 | 代码已就绪，只需硬件 |
| 屏幕（VGA / HDMI）+ 相机 | 拍最后一帧 | 代码已就绪 |
| 最小验证内核（只打串口、不切 VBE、不 init 设备） | 在无显示无串口转接板时仍能判断 MBR/stage2/长模式是否活着 | **不存在**。当前内核一进来就 `vbe_setup_common` 切图形模式（`stage2_64.S:76`），无 TTF 时黑屏。最小验证内核属于缺口项 HW-07 |

**现场最小配置（强烈建议）：** 一台带物理 COM1 口的旧机（或 USB 转 RS-232 线），串口终端设为 115200 8N1；接一根 VGA/HDMI 显示器。否则不要上机。

---

## 1 硬件准备与启动介质

### 1.1 目标机型建议（风险从低到高）

| 优先级 | 平台特征 | 原因 |
|--------|----------|------|
| 首选 | 有物理 RS-232 COM1 口、有 SATA、有 PS/2 键盘、BIOS 可关 Secure Boot 并开 CSM | 串口直出、SATA 可测 AHCI、PS/2 不依赖 USB HID |
| 次选 | 有 USB 转 RS-232、USB 键盘、UEFI 可切 CSM | 串口走 USB-serial 仍可用；USB 键盘依赖 EHCI/HID（见 HW-06） |
| 避免 | 纯 UEFI 无 CSM、纯 NVMe 无 SATA、无 VGA 文本模式 | 默认镜像无 ESP（HW-01）、NVMe 无驱动（HW-04）、VBE 切图形后无 TTF 黑屏 |

**必须记录的机器信息清单**（上机前在纸上写下，否则失败时无法归因）：

- 主板 / 芯片组型号（如 "Intel H61 / Q77"）
- CPU 型号与是否支持长模式（CPUID 0x80000001:EDX bit29）
- 内存容量（<2GB 会触发 V0.13.63 历史问题，见附录 A.1）
- 显卡（集成 / 独立，是否 VESA 2.0 VBE）
- 网卡型号（e1000 82540EM / RTL8139 / 其他）
- BIOS 版本、启动模式（Legacy/UEFI/CSM）、Secure Boot 状态
- 启动介质：U 盘品牌 / 容量 / 分区表类型（MBR/GPT）

### 1.2 制作启动 U 盘

**目的**：把镜像写入 U 盘，让 BIOS 把它当硬盘引导。

**操作**（在 Linux 主机上）：

```bash
# 1. 先构建内核与镜像（在仓库根目录）
make -C kernel/arch/x86_64
bash tools/make_single_img64.sh
# 产物: /tmp/hepu64_single.img (256MB raw)

# 2. 确认 U 盘设备名（！！！千万别写错）
lsblk -o NAME,SIZE,MODEL,TRAN
# 假设 U 盘是 /dev/sdX（sdb/sdc...，绝不是 sda=你的硬盘）

# 3. 整盘写入（不是写分区）
sudo dd if=/tmp/hepu64_single.img of=/dev/sdX bs=4M status=progress conv=fsync
sync
```

**设备名替换警告**：`of=/dev/sdX` 必须换成 `lsblk` 看到的 U 盘整块设备（如 `/dev/sdb`），**不是** `/dev/sdb1`。写错成系统硬盘会清空主机。

**预期现象**：
- 主机上 `fdisk -l /tmp/hepu64_single.img` 应看到 1 个活动分区（type 0x83，LBA 13000 起）。
- dd 写完后，U 盘插真机，BIOS 启动菜单里能看到该 U 盘为 "USB HDD" / "U盘型号"。

**通过判据**：U 盘在真机 BIOS 启动列表中可见。

**失败判据**：BIOS 完全看不到 U 盘 → 换 USB 口（插主板后置口，不要用前面板/HUB）；换 U 盘品牌。

### 1.3 镜像构建路径对 U 盘内容的影响（已知未决项）

`tools/make_single_img64.sh` 有两套 ext2 写入路径，**写入的文件集不对齐**：

- **主路径（losetup + mount）**：`tools/make_single_img64.sh:109-149`。只写字体、HepuShell 配置、glibc 动态 hello（`/bin/glibchello.elf`）。**不写** `/bin/*.elf` 用户态程序。
- **备用路径（debugfs）**：`tools/make_single_img64.sh:151-260`。写全量 `/bin/hello.elf`、`/bin/shell.elf`、`/bin/ps.elf`、`/bin/install64.elf` 等几十个用户态程序。

**现场含义**：如果你的构建环境没有 loop 设备权限（落备用路径），U 盘里有完整用户态；如果走主路径，U 盘里**几乎没有用户态程序**，到桌面后 `ls /bin` 会是空的。这不是 bug，是待拍板项（HW-08）。上机时如果目标只是"内核能不能起来"，走哪条路径都行；如果要进 Shell 跑命令，**确保走备用路径**或手工 `debugfs -w` 补写。

---

## 2 BIOS / UEFI 设置

**目的**：让真机按预期方式引导赫普OS 默认镜像。

### 2.1 默认镜像（`make_single_img64.sh`）的启动模式

核实结论：`make_single_img64.sh` 产出的是**纯 BIOS/MBR raw 镜像**。证据：
- `tools/make_single_img64.sh:39` 写 MBR 到 LBA0；
- `tools/make_single_img64.sh:87` 写 stage2 到 LBA34；
- `tools/make_single_img64.sh:92` 写 64 位内核到 LBA2000；
- `tools/make_single_img64.sh:109` 起建 ext2 分区；
- **全程没有创建 ESP、没有写 GPT、没有放 BOOTX64.EFI**。

因此默认镜像：

| 设置项 | 要求 | 依据 |
|--------|------|------|
| 启动模式 | **必须 Legacy/CSM（BIOS 模式）** | 镜像只有 MBR，没有 ESP/BOOTX64.EFI |
| Secure Boot | **必须关闭** | 镜像无签名；且默认走 CSM 时 Secure Boot 本就不生效 |
| CSM | **必须开启**（即允许 Legacy OpROM） | stage2 依赖 INT13/INT10 实模式 BIOS 服务 |
| UEFI 原生模式 | **不可引导默认镜像** | 无 ESP，见 HW-01 |

### 2.2 UEFI 路径（独立构建，非默认镜像）

仓库里存在 UEFI 引导器源码 `tools/hepboot/uefi/hepboot_uefi.c`，能通过 `BS->LocateProtocol` 拿 GOP（`hepboot_uefi.c:315`），有 `ExitBootServices` 重试循环（`hepboot_uefi.c:1403`），`build_uefi_image.sh:34` 会把引导器放到 `/EFI/BOOT/BOOTX64.EFI`（UEFI 默认路径）。但：

- `tools/hepboot/uefi/build_uefi_image.sh:11` 拷贝的是 **32 位内核** `build_v250/hepu-kernel.bin`，**不是 64 位内核**。
- `tools/make_single_img64.sh` 不调用任何 UEFI 构建脚本。
- `tools/build_uefi_dual.py:14` 的 PROJECT 路径硬编码指向另一个会话目录（已失效）。

因此 UEFI 路径当前**不是一个可直接上机的产物**，属缺口 HW-02。如果一定要试 UEFI：需手工 `make -C tools/hepboot/uefi` 出 `hepboot_uefi.efi`，放进 FAT32 ESP 的 `\EFI\BOOT\BOOTX64.EFI`，并自行解决 64 位内核加载问题。

---

## 3 分阶段验证路线（总览表）

| 阶段 | 目的 | 通过判据 | 阻塞级别 |
|------|------|----------|----------|
| P0 串口 | 确认终端能收到 COM1 | 上电后串口立刻出 `[Stage2_64] Starting...` | 硬阻塞（无串口则后面全靠相机） |
| P1 MBR | 确认 BIOS 把 U 盘当启动盘 | 屏幕左上角出现 `H1` | 高 |
| P2 Stage2 | 确认 stage2 读盘成功、进长模式 | 屏幕出现 `64` `OK` `PM` `LM`；串口出现 `Kernel loaded OK` → `LONG MODE` → `LM>` | 高 |
| P3 内核早期 | 确认内核入口、PMM、分页 | 串口出现 `=== HepuOS 64-bit Kernel booting ===` → `[OK] Long mode active` | 高 |
| P3.5 APIC 定时器 | 确认 LAPIC 定时器校准通过（**真机最常见的"看起来像黑屏其实是挂死"**） | 串口在 `[INIT] Initializing APIC (step 1: Local APIC only)...` 之后出现 `[APIC] timer calibrated: tick_hz=... ICR=...`（或回退行 `[APIC] WARN: timer calibration failed, fallback ICR=6250`），并继续到 `[OK] APIC initialized` | **硬阻塞**（见 HW-01a） |
| P4 存储 | 确认能读 rootfs | 串口出现 `[BOOT] step6 done (ext2)` | **硬阻塞（SATA 路径死，见 HW-03）** |
| P5 桌面 | 确认 VBE/TTF/Shell | 串口出现 `[DESKTOP64] HepuShell started as default desktop` | 中 |

> **P3.5 特别提示**：真机上若屏幕已出现内核早期输出（`[OK] Long mode active` 等）但整屏静止、无 `[APIC] timer calibrated` 也无 `[OK] APIC initialized`，不是"显示坏了"，而是 APIC 校准时在 PIT tick 自旋里永久挂死（HW-01a）。QEMU 下不暴露此缺陷。

---

## 4 启动链验证（BIOS 路径）

### 4.1 MBR 阶段

**目的**：确认 BIOS 已把控制权交给赫普OS MBR。

**操作**：插 U 盘、开机、进 BIOS 选 U 盘为第一启动项。

**预期现象**：
- 屏幕：左上角第一行第 0 个字符位置出现白色 `H1`（`tools/hepboot/hepboot_mbr.S:41-44`，VGA 直写 0xB800）。
- 串口：无输出（MBR 不初始化串口，只 VGA 直写）。

**通过判据**：看到 `H1`。

**失败判据**：
- 全黑什么都没有 → BIOS 没把 U 盘当启动盘（见 §13 表第 1 行）。
- 屏幕出现 `E!` 然后滚动 `MBR READ ERR` → MBR 起来了但读 stage2 失败（`hepboot_mbr.S:65-98`）。**此时按任意键会重启**，先拍照再按键。

**取证动作**：拍屏幕第一帧；串口此时无日志，拍不到东西。

### 4.2 Stage2 阶段

**目的**：确认 stage2 被读入、初始化串口、显示菜单、加载内核、切长模式。

**操作**：MBR 显示 `H1` 后无需按键（菜单 2 秒自动选 64 位）。

**预期现象（按顺序）**：

| 顺序 | 屏幕（0xB8000 文本） | 串口（0x3F8 115200） | 出处 |
|------|----------------------|----------------------|------|
| 1 | 第 2 行出现亮青色 `64` | `[Stage2_64] Starting...` | `stage2_64.S:63-69`、`:1418` |
| 2 | 启动菜单文字 | `[Stage2_64] Boot menu starting...` | `stage2_64.S:1446`、`:1451` |
| 3 | 第 2 行变绿 `OK` | `[Stage2_64] Loading kernel...` → `[Stage2_64] Kernel loaded OK` → `[Stage2_64] Kernel copied to 0x100000` | `stage2_64.S:394-401`、`:1419-1421` |
| 4 | 第 4 行亮青色 `PM` | `[Stage2_64] Entering protected mode...` → `[Stage2_64] Protected mode OK` | `stage2_64.S:1071-1077`、`:1425-1426` |
| 5 | 第 5 行亮青色 `LM` | `[Stage2_64] CPU supports long mode` → `[Stage2_64] Page tables built, enabling paging...` → `LM>` → `JMP>` → 然后是 0x100000 处前两字节的 hex 回显 | `stage2_64.S:1299-1305`、`:1427`、`:1429`、`:1288-1354` |

**通过判据**：串口出现 `LM>` 后紧跟着 `JMP>`，且 hex 回显不是 `00-00`（说明内核头真的被读进来了）。

**失败判据**：
- 串口停在 `[Stage2_64] Starting...` 之后没有菜单 → `detect_lm_cap` 或 `load_boot_config` 卡了。
- 串口出现 `[Stage2_64] ERROR: Disk read failed!`（`stage2_64.S:1422`）→ INT13 读内核 LBA2000 失败 3 次重试仍失败（重试逻辑在 `stage2_64.S:343-351`）。换 SATA 直连、换 U 盘口。
- 屏幕第 5 行出现红底白字 `NO` → CPU 不支持长模式（`stage2_64.S:1236-1247`），换 64 位 CPU。
- 串口出现 `[Stage2_64] ERROR: CPU does NOT support long mode!` 后 `hlt` 死循环。

**取证动作**：串口全量保存（`sudo cat /dev/ttyUSB0 > stage2.log`）；拍屏幕第 1-5 行。

### 4.3 内核早期阶段

**目的**：确认内核入口被跳到、串口继续、长模式活着、APIC 定时器能校准。

**操作**：stage2 跳 `jmp *0x100000`（`stage2_64.S:1354`）后无需操作。

**预期串口输出（按顺序）**：

```
=== HepuOS 64-bit Kernel booting ===
[OK] Long mode active
[INFO] CPU supports x86_64: Yes
[INIT] Enabling SCE (System Call Extensions)...
[INIT] Enabling NXE (No-Execute)...
[INIT] Enabling FPU/SSE...
[INIT] Initializing physical memory manager (2GB max)...
[INFO] PMM physical memory: 0x... MB
[OK] Physical memory manager initialized
[BOOT] step1 done
[BOOT] step2 done (drv_init)
[INIT] Initializing kernel heap (32MB)...
[OK] Kernel heap initialized
[INIT] Initializing PIT timer...
[OK] PIT timer initialized
[INIT] Initializing 64-bit virtual memory (4-level paging)...
[OK] Switched to kernel page tables
[BOOT] step3 done (paging)
[INIT] Early IDT (crash-safe)...
[OK] Early IDT installed
[INIT] Initializing APIC (step 1: Local APIC only)...
[APIC] timer calibrated: tick_hz=... ICR=...
[OK] APIC initialized
[BOOT] step4 done (APIC)
```

出处：`kernel/kernel_main_64.c:186`、`:196`、`:219-309`、`:360-363`；APIC 校准行 `kernel/arch/x86_64/apic64.c:572-574`。

**通过判据**：串口出现 `[BOOT] step4 done (APIC)`。

**失败判据**：
- 串口在 `LM>` `JMP>` 之后**立刻断流**（没有 `=== HepuOS 64-bit Kernel booting ===`）→ 跳到 0x100000 后立刻三重故障或页错误。这是 V0.13.63 历史上"黑屏回 BIOS"的典型表现，见附录 A.1。
- 串口出现 `[ERROR] Not in long mode!` 后死循环（`kernel_main_64.c:198-199`）。
- 串口停在 `[INIT] Initializing APIC (step 1: Local APIC only)...` 之后，**既没有** `[APIC] timer calibrated: tick_hz=... ICR=...`，**也没有** `[APIC] WARN: timer calibration failed, fallback ICR=6250`，更没有 `[OK] APIC initialized` → **HW-01a：APIC 定时器校准在 PIT tick 自旋里永久挂死**。整屏静止、无报错。
- 串口出现 `[EXCEPTION] vector=... rip=... CR2=...`（`lib64.c:451-483`）→ 内核态异常，**屏幕也会停在那行，拍照 + 抄串口**。

**取证动作**：把串口最后 50 行整段抄下；如果看到 `[EXCEPTION]`，把 `vector=`、`error=`、`rip=`、`CR2=`、`PML4e=`、`PDPTe=`、`PDe=`、`PTe=` 全抄下；如果停在 APIC 初始化行，把最后一行原样记下并记录机型/芯片组。

---

## 5 启动链验证（UEFI 路径）

> 当前默认镜像不走 UEFI。本节仅在你按 §2.2 手工构建了 UEFI 镜像后使用。

**目的**：确认 UEFI 固件能加载 `\EFI\BOOT\BOOTX64.EFI`、GOP 取帧缓冲、ExitBootServices 成功并跳到内核。

**前置操作**：
1. BIOS 设为 UEFI 原生模式（关 CSM）、Secure Boot 关闭。
2. 用 `tools/hepboot/uefi/build_uefi_image.sh` 产出 `build/hepu-os-uefi-v*.img`，dd 到 U 盘。
3. 注意：该脚本当前拷贝 32 位内核（`build_uefi_image.sh:11`），64 位 UEFI 引导未打通（HW-02）。

**预期现象**：
- UEFI 启动菜单能看到 U 盘为 "UEFI: U盘型号"。
- 引导器阶段屏幕显示 `HepuBoot UEFI v*.*` 文字，串口（如果引导器也接 COM1）出现 `[GOP] 枚举 ...` 等（`hepboot_uefi.c:425`）。
- ExitBootServices 成功后串口出现 `HepuBoot: ExitBootServices OK\n`（`hepboot_uefi.c:1418`）。

**通过判据**：串口出现 ExitBootServices OK 后跳到内核。

**失败判据**：
- UEFI 启动菜单看不到 "UEFI: U盘" → ESP 未被识别，检查 U 盘分区是否为 FAT32 ESP 类型。
- 引导器打印 `[GOP] 不支持Graphics Output Protocol`（`hepboot_uefi.c:318`）→ 固件 GOP 缺失。
- ExitBootServices 失败返回（`hepboot_uefi.c:1412`）→ 内存映射问题。

**取证动作**：抄引导器阶段所有串口输出。

---

## 6 存储验证（SATA/AHCI）

### 6.1 现状（源码核实，重要）

这是真机上**最容易卡住的一步**，且当前代码在 SATA 路径上是死的：

- `ahci64.c:93` 定义了 `ahci_port_info_t.total_sectors`，但**全文件从未给它赋值**。
- `ahci64.c:240-246` `ahci64_disk_present()` 要求 `total_sectors > 0` 才返回真 → 恒为假。
- **从未发 ATA IDENTIFY（0xEC）命令**。`ahci64_exec_cmd` 只被 READ_DMA_EXT(0x25)（`ahci64.c:357`）和 WRITE_DMA_EXT(0x35)（`ahci64.c:370`）调用。
- `kernel_main_64.c:563` `have_ahci = ahci64_disk_present()` → 恒 0；于是 `kernel_main_64.c:606` 必然打印 `[INIT] No SATA/USB disk, falling back to IDE`。
- `storage_hal64.c:74` raw_read 有 AHCI 分支但因 disk_present 恒假永不触发；`storage_hal64.c:90-111` raw_write **连 AHCI 分支都没有**（读写不对称）。

### 6.2 验证步骤

**目的**：确认内核能否读到 rootfs。

**操作**：内核跑到 `[BOOT] step4 done (APIC)` 之后继续等待。

**预期串口输出（按顺序）**：

```
[INIT] Initializing 64-bit IDE driver...
[INIT] Detecting IDE drive capacity...
[INIT] Parsing MBR partition table...
[INIT] Early AHCI/SATA init...
[AHCI64] Initializing SATA host adapter...
[AHCI64] Found AHCI at PCI 0:X.X
[AHCI64] port N: SATA HDD
[AHCI64] Initialized: N SATA device(s)
[INIT] Probing USB boot...            <-- 仅当 IDE MBR 没找到 ext2 时
[INIT] USB mass storage found, using as root device   <-- 或
[INIT] No SATA/USB disk, falling back to IDE          <-- SATA 路径死时实际会看到这行
[INIT] Initializing 64-bit ext2 filesystem...
[BOOT] step6 done (ext2)
```

出处：`kernel_main_64.c:422`、`:427`、`:432`、`:554`、`ahci64.c:389`、`:407`、`:221-232`、`:449`、`kernel_main_64.c:575`、`:594`、`:606`、`:611`、`:614`。

**通过判据**：串口出现 `[BOOT] step6 done (ext2)`。

**失败判据（当前 SATA 真机的必然结果）**：
- 串口出现 `[AHCI64] port N: SATA HDD`（说明端口枚举到了盘），但接着是 `[INIT] No SATA/USB disk, falling back to IDE`，然后 ext2 挂载失败 / 卡住。这就是 HW-03。
- 串口出现 `[AHCI64] No AHCI controller found`（`ahci64.c:431`）→ PCI 扫描没找到 class 0x01/sub 0x06，可能是 BIOS 把 SATA 设成了 IDE 兼容模式。去 BIOS 把 SATA 模式从 "IDE" 改成 "AHCI" 再试。
- 串口出现 `[AHCI64] No SATA devices found`（`ahci64.c:460`）→ HBA 找到了但端口全空，检查 SATA 线/电源线/盘是否通电。

**取证动作**：把 `[AHCI64]` 开头的所有行抄下；如果 ext2 卡住，抄最后 30 行。

### 6.3 SATA 真机最小改动清单（不实施，仅记录）

| 缺口 | 证据 file:line | 改法一句话 |
|------|----------------|------------|
| 从不发 IDENTIFY，total_sectors 从未赋值 | `ahci64.c:93`、`:243` | 在 `ahci64_port_init` 成功后加一条 IDENTIFY DEVICE(0xEC) FIS，从返回的 16-19 字解析 48-bit LBA count 写入 `ahci_ports[port_num].total_sectors` |
| raw_write 无 AHCI 分支 | `storage_hal64.c:90-111` | 在 raw_write 里镜像 raw_read 的 AHCI 探测块，调 `ahci64_write_disk` |
| AHCI 只扫 bus 0 | `ahci64.c:396` | 多总线或 PCI 桥枚举（当前够用，列低优先级） |

---

## 7 存储验证（NVMe / IDE / USB）

### 7.1 NVMe

**现状**：仓库里**没有 nvme64.c**，Makefile 也不编任何 NVMe 驱动（`kernel/arch/x86_64/Makefile` 全文无 nvme）。`storage_hal64.c:23` 的注释提到 `device_type: 2=NVMe`，但没有任何代码产生这个值。

**结论**：纯 NVMe 机器**无法启动 rootfs**（HW-04）。上机时必须接一块 SATA 盘或 U 盘。

### 7.2 IDE / PATA

**现状**：`ide64.c` 已编入（`Makefile:30`），作为最后回退（`storage_hal64.c:86`、`:110`）。现代机器（2010 年后）基本没有 PATA 口；如果 BIOS 把 SATA 设成 "IDE 兼容模式"，IDE 驱动能看到盘，但 DMA/PIO 支持未经真机验证。

**通过判据**：串口出现 `[INIT] Detecting IDE drive capacity...` 后跟合理容量数字，且 ext2 挂载成功。

### 7.3 USB mass storage

**现状**：`usb64.c:602` `usb_storage_read_sectors`、`usb64.c:622` `usb_storage_write_sectors` 已编入（`Makefile:102`）。启动 U 盘时内核会走这条路径（`kernel_main_64.c:574-600`）。

**关键依赖**：现代主板没有 UHCI（PIIX），必须走 EHCI（`ehci64.c`，`Makefile:103`）。但 V0.13.64 历史记录（附录 A.2）指出 QEMU 的 EHCI 异步列表不执行，真机行为未验证。

**预期串口**：
```
[INIT] Probing USB boot...
[INIT] UHCI no storage, trying EHCI...
[INIT] USB mass storage found, using as root device
[BOOT] step6 done (ext2)
```

**失败判据**：停在 `[INIT] UHCI no storage, trying EHCI...` 之后无下文 → EHCI 枚举失败（HW-06）。

## 8 网络验证

### 现状与证据

| 能力 | 证据 file:line | 结论 |
|---|---|---|
| 64 位构建编入的网卡驱动 | `kernel/arch/x86_64/Makefile:35`（rtl8139_64.c）、`:36`（e1000_64.c）、`:39-48`（net64 协议栈）、`:77`（network_hal64.c） | 仅编入 RTL8139 与 e1000 两个驱动；无独立 virtio-net 源文件编入 |
| Intel e1000 驱动存在性 / 编入 / PCI 探测 | `kernel/arch/x86_64/e1000_64.c:112`（e1000_init）；`Makefile:36`；`e1000_64.c:118-123`（bus 0..7 / dev 0..31 / func 0..7 扫描，Vendor 0x8086，Device 0x100E/0x100F/0x1000） | 驱动存在且编入；PCI 探测仅匹配 3 个 82540/82545 级设备 ID，现代 Intel I219/I225/I350 等不在表内 |
| Realtek RTL8139 驱动存在性 / 编入 / PCI 探测 | `kernel/arch/x86_64/rtl8139_64.c:133`（rtl8139_init）；`Makefile:35`；`rtl8139_64.c:138`（pci_find_device(0x10EC, 0x8139)） | 驱动存在且编入；仅匹配 RTL8139（0x8139），RTL8168/8169 等千兆芯片不在表内 |
| RTL8139 收发是否接入协议栈 | `kernel/arch/x86_64/net64_eth.c:135`（eth64_send → e1000_send）、`:149`（eth64_recv → e1000_recv）；`rtl8139_64.c:193`（rtl8139_send）、`:234`（rtl8139_recv）定义后无协议栈调用方 | RTL8139 的 send/recv 未接入以太网抽象层；协议栈收发实际只走 e1000 路径 |
| virtio-net | `kernel/arch/x86_64/network_hal64.c:17`（NET_DEV_VIRTIO 枚举占位）；`Makefile` 无 virtio-net 源文件；全仓库无 0x1AF4 vendor ID 匹配 | 仅有类型枚举占位，无驱动实现 |
| PCI 枚举方式 | `kernel/arch/x86_64/pci64.c:9-10`（0xCF8/0xCFC 配置端口）；`:26-32`（pci_config_read32） | 走传统 IO 端口配置空间，未使用 MMCONFIG |
| 多 bus / 多 function 覆盖 | `pci64.c:62-64`（pci_find_class_bar0 只扫 bus 0）；`:81-83`（pci_find_device 扫 bus 0..255 / dev 0..31 / func 0..7）；`e1000_64.c:118-120`（bus 0..7） | class 匹配路径只扫 bus 0；vendor/device 路径扫 256 bus；经 PCIe 桥挂在下游 bus 的设备在 class 路径下可能漏扫 |
| BAR 类型与 64 位 BAR 处理 | `pci64.c:89-95`（只取第一个 I/O 空间型 bit0=1 的 BAR）；`e1000_64.c:125-126`（只读 BAR0 低 32 位，`bar0 & 0xFFFFFFF0`）；`ehci64.c:276`、`xhci64.c:856`（pci_find_class_bar0 取 BAR0） | 未区分 I/O/MMIO BAR 类型，未读高位 BAR1，未检查 64 位 BAR；未处理 BAR 未分配（读回 0）的情况 |
| MMIO 映射方式 | `e1000_64.c:144-146`（vmm64_map_page 恒等映射 virt==phys，0x20000 字节）；`ehci64.c:291-294`（恒等映射 1MB）；`xhci64.c:866`（恒等映射 16MB） | MMIO 一律恒等映射，未走独立 vmm 重映射；高于内核恒等区的物理地址需核实可达性 |
| 网卡中断接线 | `e1000_64.c:213`（E1000_IMS=0 屏蔽中断，轮询）；`rtl8139_64.c:175`（IMR=0x0000 屏蔽中断，轮询）；`apic64.c:398/400/404` 仅使能 PIT/键盘/鼠标 GSI | 网卡不接中断，全部靠主循环轮询；网卡 GSI 未在 IOAPIC 编程 |
| DHCP 客户端状态机 | `kernel/arch/x86_64/net64_dhcp.c:12-16`（DISCOVER/OFFER/REQUEST/ACK/NAK 定义）；`:344`（Sending Discover）；`:367-368`（收 OFFER 后 Sending Request）；`:392-397`（收 ACK 绑定 / NAK 回 INIT） | 四阶段报文类型齐全；但无 DISCOVER 重传、无超时回退、无租约续期 |
| DNS 查询与压缩指针防护 | `kernel/arch/x86_64/net64_dns.c:137-145`（压缩指针解析，`ptr >= packet_len || ptr >= pos || ++jumps > 64` 三重防护）；`:206`（无 DHCP DNS 时兜底 0x08080808=8.8.8.8）；`:244`（重试 3000 次）；`:302`（忙等空循环） | 压缩指针有循环深度与单调性防护；DNS 服务器硬编码兜底 8.8.8.8；等待响应靠固定次数轮询 + 忙等，无真实超时计时 |
| 硬编码 IP / 网关 | `kernel/kernel_main_64.c:1017-1018`（静态 10.0.2.15/24，网关 10.0.2.2）；`net64_eth.c:72-73`（netmask 默认 0xFFFFFF00，gateway 0） | e1000 初始化成功后无条件写死 QEMU 用户网段静态地址，DHCP 结果是否覆盖未在代码中核实 |
| ARP / 邻居解析 | `kernel/arch/x86_64/net64_arp.c:110-133`（arp64_request 广播请求）；`:8-9`（表 16 项，ARP_TIMEOUT=300）；`net64_ip.c:113`（下一跳走网关）、`:129`（arp64_request） | ARP 请求/应答路径存在；表项小、超时按计数器而非真实秒；无代理 ARP、无多网关 |
| 校验和 / MTU 假设 | `net64_ip.c:101`（IP 头校验和软件计算）；`net64_tcp.c:135`（TCP 校验和软件计算）；`net64_udp.c:123`（UDP 校验和软件计算）；`net64_eth.c:75`（MTU=1500） | 收发校验和均由软件计算，未启用硬件卸载；MTU 固定 1500，未协商 jumbo frame |
| 本机可验证性 | 环境事实：本机 QEMU 未编译 user 网络后端，`-netdev user` 报错 | 本节所有联网结论仅为代码层核实，DHCP/DNS/ARP/真实链路必须上机确认，本机无法回归 |

### 上机验证步骤

1. **目的**：确认真机是否探测到网卡并完成初始化。
   - **操作**：真机从 U 盘启动，进入桌面；观察启动日志中网卡相关行。
   - **预期屏幕现象**：出现 e1000/RTL8139 初始化或未找到的提示行。
   - **预期串口输出（源码真实字符串）**：`[E1000] Initializing...`（`e1000_64.c:114`）、`[E1000] Found at MMIO `（`e1000_64.c:134`）、`[OK] E1000 initialized`（`e1000_64.c:223`）；未匹配时 `[E1000] Device not found`（`e1000_64.c:153`）、`[RTL8139] Device not found`（`rtl8139_64.c:140`）、`[NET] No NIC found, network stack idle`（`kernel_main_64.c:1022`）。
   - **通过判据**：串口出现 `[OK] E1000 initialized` 或 `[OK] RTL8139 initialized`（`rtl8139_64.c:188`），且后续 `[OK] Network up (RTL8139 + TCP/IP, static 10.0.2.15)`（`kernel_main_64.c:1019`）。
   - **失败判据**：仅出现 `[NET] No NIC found, network stack idle`，或反复出现 `[E1000] Device not found` / `[RTL8139] Device not found`。

2. **目的**：确认真实链路上 DHCP 是否拿到地址。
   - **操作**：真机接入可提供 DHCP 的局域网（路由器），启动后等待桌面主循环轮询；查看串口 DHCP 状态机输出。
   - **预期屏幕现象**：桌面天气/网络状态不报错；串口可见 Discover/Request 阶段打印。
   - **预期串口输出（源码真实字符串）**：`[DHCP64] Sending Discover...`（`net64_dhcp.c:344`）、`[DHCP64] Sending Request...`（`net64_dhcp.c:368`）；异常时 `[DHCP64] NAK received, restarting`（`net64_dhcp.c:397`）。
   - **通过判据**：状态机进入 BOUND，串口无 NAK 循环；用 ping/HTTP 应用可通。
   - **失败判据**：长时间停在 SELECTING/REQUESTING，反复 NAK，或最终仍使用静态 10.0.2.15 地址。

3. **目的**：确认 DNS 解析在真实路由器下可用。
   - **操作**：桌面内触发一次域名访问（天气/浏览器），观察串口 DNS 结果。
   - **预期屏幕现象**：域名解析成功并加载内容；失败时提示网络错误。
   - **预期串口输出（源码真实字符串）**：失败路径 `[HTTP64] DNS resolution failed`（`net64_http.c:105`）。
   - **通过判据**：不出现 `[HTTP64] DNS resolution failed`，域名对应 IP 可连通。
   - **失败判据**：稳定复现 `[HTTP64] DNS resolution failed`，或长时间卡在 DNS 等待轮询。

4. **目的**：核实静态配置与真机网段是否冲突。
   - **操作**：对比真机所在局域网网段（如 192.168.1.0/24、192.168.0.0/24）与内核写死的 10.0.2.15/24、网关 10.0.2.2。
   - **预期屏幕现象**：若网段不符，ARP 无应答，外网不可达。
   - **预期串口输出（源码真实字符串）**：`[OK] Network up (RTL8139 + TCP/IP, static 10.0.2.15)`（`kernel_main_64.c:1019`）。
   - **通过判据**：真机网段与静态配置一致，或 DHCP 成功覆盖静态地址。
   - **失败判据**：串口显示静态 10.0.2.15，但真机网段并非 10.0.2.0/24。

### 缺口与最小补齐清单

| 编号 | 缺口 | 证据 | 阻塞级别 | 最小改动方向（一句话，不实施） |
|---|---|---|---|---|
| HW-20 | 协议栈收发只接 e1000 驱动，RTL8139 的 send/recv 定义后无调用方；即便 RTL 初始化成功，帧也不会从 RTL 路径收发 | `net64_eth.c:135`、`:149`；`rtl8139_64.c:193`、`:234`；`network_hal64.c:79-87` | 高 | 在 eth64_send/recv 中按已初始化的实际驱动做双路径分发，或把 RTL 收发挂到同一 ops 表 |
| HW-21 | 网卡 PCI ID 覆盖极窄：e1000 仅 0x100E/0x100F/0x1000，RTL 仅 0x8139；无 RTL8168/8169、无 Intel I219/I225、无 virtio-net | `e1000_64.c:123`；`rtl8139_64.c:138`；`network_hal64.c:17`；`Makefile:35-36` | 高 | 按 class 0x0200/0x0201 枚举并扩充常见 ID 表，补 virtio-net 探测占位 |
| HW-22 | PCI 枚举走 0xCF8/0xCFC，无 MMCONFIG；class 匹配路径只扫 bus 0，多 bus/桥接下游设备可能漏扫 | `pci64.c:9-10`、`:62-64`；`e1000_64.c:118` | 中 | class 扫描扩到多 bus，并在固件提供 MCFG 时支持 MMCONFIG 访问 |
| HW-23 | BAR 未区分 I/O/MMIO 类型，未读高位 BAR1，未检查 64 位 BAR，未处理 BAR 未分配；MMIO 恒等映射 | `pci64.c:89-95`；`e1000_64.c:125-126`、`:144-146`；`xhci64.c:866` | 中 | 读 BAR 类型位与 64 位标志，拼合高低位 BAR，BAR=0 时报错跳过而非按 0 映射 |
| HW-24 | 静态 IP/网关写死 10.0.2.15/10.0.2.2；DNS 兜底硬编码 8.8.8.8；DHCP 成功后是否覆盖静态地址未核实 | `kernel_main_64.c:1017-1018`；`net64_dns.c:206` | 高 | DHCP 绑定成功后以 OFFER/ACK 下发的 IP/掩码/网关/DNS 覆盖静态值，静态配置仅作兜底 |
| HW-25 | 两块网卡均屏蔽中断纯轮询，收包依赖桌面主循环 net64_poll；桌面未起或主循环阻塞时收不到帧 | `e1000_64.c:213`；`rtl8139_64.c:175`；`desktop64.c:152-153` | 中 | 为网卡接 IOAPIC 中断或在更早的轮询点（调度 tick）泵收包 |
| HW-26 | DHCP 状态机无 DISCOVER 重传、无超时回退、无租约续期；NAK 后仅回 INIT 重来 | `net64_dhcp.c:343-347`、`:379-399` | 中 | 加 Discover 重传次数与超时，绑定后按租约定时续期 |
| HW-27 | e1000 DMA 描述符地址直接填静态缓冲虚拟地址，未做 virt→phys 转换；非恒等映射内核下 DMA 可能读错页 | `e1000_64.c:183`、`:198`；对照 RTL 路径 `rtl8139_64.c:97-105` 已做 virt_to_phys | 低 | 描述符/数据缓冲地址统一走 vmm64_virt_to_phys 转换后再写 DMA 寄存器 |
| HW-28 | 本机 QEMU 网络后端未编译，DHCP/DNS/ARP/真实链路无法在本机回归；ARP 表仅 16 项、超时按计数器计 | 环境事实（QEMU 无 user 网络后端）；`net64_arp.c:8-9`、`:189-190` | 中 | 上机前在代码层补齐超时/重传，真机上分阶段验证 ARP→DHCP→DNS→TCP |

## 9 显示与输入验证

### 现状与证据

| 能力 | 证据 file:line | 结论 |
|---|---|---|
| GOP framebuffer 获取路径 | `tools/hepboot/uefi/hepboot_uefi.c:1425-1436`（ExitBootServices 前把 w/h/pitch/bpp/fb 写到 0x6000）；`kernel/arch/x86_64/gfx64.c:755-759`（early 镜像从 0x6000 读）、`:787-793`（正式初始化从 0x6000 读） | 内核不直接调 UEFI GOP，帧缓冲信息依赖引导器写入 0x6000 固定内存；内核入口 RSI mb_info=0（`entry_64.S:629`） |
| 分辨率 / stride 传递 | `gfx64.c:788-790`（width/height/pitch 从 0x6000 读）；`:241-245`（set_mode 保存 width/height/pitch/bpp）；行跨距按 pitch 计算（`:274`、`:277`、`:291`、`:294`） | 行跨距使用 pitch 而非 width，pitch 由引导器 `pixels_per_scanline*(bpp/8)` 算出（`hepboot_uefi.c:276`、`:1432`） |
| 分辨率假设 | `gfx64.c:27`（GFX_MAX_BUF=1024*768*4，back buffer 上限）；`kernel/kernel_main_64.c:954`（mouse64_init(1024, 768) 硬编码）；`usb64.c:1357-1362`（轮询时取真实宽高钳制） | back buffer 按 1024x768x4 上限分配；鼠标初始尺寸硬编码 1024x768；高于该分辨率的画面存在缓冲/钳制风险 |
| 像素格式假设 | `gfx64.c:273-280`（32bpp 直接写 color；24bpp 按 B,G,R 字节序）；无 16bpp 分支；`hepboot_uefi.c:369-378`（仅按 mask 位数算 bpp，未记录 R/G/B 位序） | 内核未按 GOP PixelFormat（RGB/BGR/RGB565）判定通道序；16bpp 无渲染分支 |
| ExitBootServices 后帧缓冲有效性 | `hepboot_uefi.c:1396-1403`（ExitBootServices 重试 5 次）；`gfx64.c:760-763`（w/h/fb 为 0 则 early 镜像放弃）；`:795-797`（正式初始化拿不到信息返回 -1） | 内核未校验 EBS 后帧缓冲是否仍被固件保留/未被重映射；部分独显/核显平台 EBS 后 GOP 内存可能失效 |
| VGA 文本/图形回退 | `gfx64.c:823-825`（gfx_initialized=0 时打印 "No VBE mode set, using text mode" 并返回 -1）；`term64.c:103`（终端标注 VGA Text Mode） | 无 640x480 图形模式或 80x25 文本模式的实际设置代码；回退路径只返回错误，不切模式 |
| PS/2 键盘初始化序列 | `keyboard64.c:115`（0xAE 启用第一端口）；`:122-131`（读配置字节，置 bit0 开 IRQ1、清 bit4 时钟禁用，写回 0x60）；`:136`（0xF4 打开扫描）；`:140`（初始化完成） | 有 8042 配置字节读改写；未做键盘自检（0xFF/0xAA）与扫描码集显式选择；中断驱动 IRQ1 |
| PS/2 键盘中断 vs 轮询 | `irq64_simple.c:217`（注册 IRQ1 → keyboard64_interrupt）；`apic64.c:400`（acpi_irq_to_gsi(1) 使能 IOAPIC 引脚） | 键盘走中断；IRQ1 的 GSI 翻译与极性/触发位依赖 ACPI 与 IOAPIC 编程 |
| PS/2 鼠标初始化与读取 | `ps2mouse64.c:85`（ps2mouse_init）；`:93-107`（启用 AUX、读配置置 bit1）；`:112-127`（0xFF 复位 + 滚轮探测）；`:139`（0xF4 使能报告）；`:256-257`（mouse64_init 失败也启用裸轮询）；`:56`、`:65`（等待循环 100000 次上限） | 鼠标以轮询读包为主；IRQ12 虽在 `apic64.c:404` 使能，但配置读取有超时兜底；无 PS/2 口的真机上初始化会超时 |
| USB 控制器枚举顺序 | `usb64.c:342-343`（UHCI 0x8086:0x7020/0x7112）；`:347`（EHCI 失败后试）；`:357-358`（再试 XHCI）；`ehci64.c:276`（class 0x0C/0x03/0x20）；`xhci64.c:856`（class 0x0C/0x03/0x30） | 顺序 UHCI→EHCI→XHCI；UHCI 为 32 位 I/O 遗留协议；现代 Intel/AMD 真机通常仅 XHCI |
| USB HID 轮询 | `desktop64.c:116-117`（桌面内 usb64_init）；`usb64.c:1353`（usb64_poll）；`:1369-1372`（XHCI 事件 + 键盘 flush）；`:1376`（无 UHCI/EHCI 时提前 return） | XHCI 键盘有 flush 路径；`:1376` 在仅 XHCI 平台提前返回，XHCI 鼠标 HID 报告未进入鼠标遍历路径 |
| USB 中断传输周期 | `usb64.c:191`（周期 QH 挂载点）；`ehci64.c:150`（ehci64_int_ep_poll）；`xhci64.c:978`（xhci64_poll） | 周期传输靠主循环轮询驱动，未绑定 USB 帧/微帧中断；轮询频率受桌面帧率影响 |

### 上机验证步骤

1. **目的**：确认真机 UEFI 路径下内核能否拿到帧缓冲并出画面。
   - **操作**：UEFI 模式从 U 盘启动，观察屏幕是否出现桌面/启动日志。
   - **预期屏幕现象**：深蓝底桌面或启动日志文本可见，分辨率与引导器所选 GOP 一致。
   - **预期串口输出（源码真实字符串）**：`[GFX64] early fb text mirror active: `（`gfx64.c:777`）、`[GFX64] Mode set: `（`gfx64.c:248`）；拿不到信息时 `[GFX64] No VBE info from bootloader`（`gfx64.c:796`）、`[GFX64] early fb: no info`（`gfx64.c:761`）。
   - **通过判据**：串口出现 Mode set 行且宽高/bpp/pitch 与真机屏幕一致，画面颜色与形状正常。
   - **失败判据**：黑屏，串口停在 `[GFX64] No VBE info from bootloader`，或 early fb 未激活。

2. **目的**：核对像素格式与通道序在真机上是否正确（颜色不反、不偏色）。
   - **操作**：进入桌面后观察红/绿/蓝图标与背景色；必要时在不同 bpp 的机器上各测一次。
   - **预期屏幕现象**：红、绿、蓝原色显示正确，无红蓝互换、无灰度错乱。
   - **预期串口输出（源码真实字符串）**：`[GFX64] Mode set: <w>x<h>x<bpp> pitch=<pitch> fb=<addr>`（`gfx64.c:248-257`）。
   - **通过判据**：32bpp/24bpp 下原色正确；若串口 bpp=16，需先标记为未支持。
   - **失败判据**：红蓝互换、整屏偏色，或 bpp=16 时画面花屏/静止。

3. **目的**：确认 PS/2 键盘在真机上可输入。
   - **操作**：桌面出现后敲击字母/回车，观察 shell 或搜索框是否回显。
   - **预期屏幕现象**：按键字符回显，光标/焦点响应。
   - **预期串口输出（源码真实字符串）**：`[KEYBOARD64] Initializing PS/2 keyboard...`（`keyboard64.c:102`）、`[KEYBOARD64] Keyboard initialized`（`keyboard64.c:140`）。
   - **通过判据**：初始化完成后按键有回显；IRQ1 计数随按键增长。
   - **失败判据**：初始化行出现但按键无任何回显（中断未投递），或初始化卡住。

4. **目的**：确认 PS/2 鼠标与 USB HID 在真机上可动。
   - **操作**：先移动 PS/2 鼠标；再插 USB 键盘/鼠标（含 USB 接收器），观察指针与按键。
   - **预期屏幕现象**：指针随移动变化，点击有反馈；USB 设备枚举后可输入。
   - **预期串口输出（源码真实字符串）**：`[MOUSE64] PS/2 mouse ready (aux device, IRQ12)`（`ps2mouse64.c:144`）、`[USB64] UHCI at I/O `（`usb64.c:366`）、`[DESKTOP64] USB HID subsystem ready`（`desktop64.c:117`）；UHCI 缺失时 `[USB64] No UHCI controller found`（`usb64.c:345`）。
   - **通过判据**：至少一条输入通路（PS/2 或 USB）指针可移动、按键可识别。
   - **失败判据**：指针完全不动，或 USB 设备枚举后键盘/鼠标均无响应。

### 缺口与最小补齐清单

| 编号 | 缺口 | 证据 | 阻塞级别 | 最小改动方向（一句话，不实施） |
|---|---|---|---|---|
| HW-30 | 帧缓冲信息完全依赖引导器写 0x6000，内核不校验 EBS 后 GOP 内存是否仍有效；独显/核显平台 EBS 后帧缓冲可能被固件回收或重映射 | `hepboot_uefi.c:1425-1436`；`gfx64.c:755-763`、`:795-797` | 高 | 内核启动后回读帧缓冲首像素做有效性校验，失效时切文本/其他可用模式，而非直接黑屏返回 -1 |
| HW-31 | 无 16bpp（RGB565 等）渲染分支；真机 GOP 给出 16bpp 时 put_pixel/get_pixel 行为未定义 | `gfx64.c:273-280`、`:290-295` | 高 | 补 16bpp 分支（按 5/6/5 位序打包解包），或在引导器侧锁定 32bpp 模式并在内核校验拒绝其他格式 |
| HW-32 | 像素通道顺序未按 GOP PixelFormat 判定；引导器只算 bpp 位数，不记录 R/G/B 位序；24bpp 内核按固定 BGR 序写 | `gfx64.c:275`、`:278-280`；`hepboot_uefi.c:369-378` | 中 | 引导器把 PixelFormat 与三色掩码一并传入 0x6000，内核按掩码做通道搬移 |
| HW-33 | 无真实 VGA 文本/640x480 回退：gfx 初始化失败仅返回 -1，不切换任何模式；0xB8000 文本路径在 UEFI 下不可见 | `gfx64.c:823-825`；`term64.c:103` | 中 | 补 BIOS 文本模式设置（80x25）或固定 640x480 图形回退作为无 GOP 时的兜底控制台 |
| HW-34 | PS/2 键盘依赖 IRQ1 中断投递；IRQ1 的 GSI 翻译、极性/触发位若在真机失效，键盘完全无响应而轮询鼠标正常 | `irq64_simple.c:217`；`apic64.c:400`；`keyboard64.c:126` | 高 | 保留 IRQ1 中断通路的同时补 8042 输出缓冲轮询兜底，并在串口打印 IRQ1 投递计数 |
| HW-35 | 无 PS/2 口的现代真机上鼠标初始化超时后裸轮询空转；8042 配置读取/命令等待均为固定次数循环 | `ps2mouse64.c:56`、`:65`、`:100-110`、`:256-257` | 中 | 检测到 8042 无 AUX 设备时直接跳过 PS/2 鼠标初始化，改由 USB HID 提供指针 |
| HW-36 | USB 先找 UHCI（0x8086:0x7020/0x7112），UHCI 是 32 位 I/O 遗留协议；现代 Intel/AMD 真机通常只有 XHCI，枚举顺序把老协议排在前面 | `usb64.c:342-343`、`:347`、`:357-358`；`ehci64.c:276`；`xhci64.c:856` | 中 | 枚举顺序调整为先 XHCI/EHCI 后 UHCI，按 class 而非固定 vendor/device ID 探测 |
| HW-37 | 仅 XHCI 平台上 usb64_poll 在 UHCI/EHCI 均无时提前 return，XHCI 鼠标走 xhci64 内部设备结构（is_mouse/dx/dy/btn），不经过 g_usb_mice 统一遍历；该独立路径未在真机常见芯片组回归 | `usb64.c:1369-1376`；`xhci64.c:180`、`:500`、`:576-578` | 中 | 统一 XHCI 鼠标事件到 g_usb_mice 遍历或在 XHCI 分支内独立完成坐标/按键合并，上机覆盖 Intel/AMD XHCI 键鼠 |

## 10 ACPI 与中断验证

### 现状与证据

| 能力 | 证据 file:line | 结论 |
|---|---|---|
| RSDP / RSDT / MADT 解析 | `kernel/arch/x86_64/acpi.c:58-95`（RSDP 扫描 EBDA + 0xE0000-0xFFFFF）；`:111`（取 rsdp->rsdt_address）；`:139-146`（RSDT 条目找 APIC 签名）；`:176-227`（遍历 MADT 条目） | 解析 RSDP→RSDT→MADT；未读取 XSDT（`acpi.h:19` xsdt_address 字段存在但未用） |
| MADT 条目消费 | `acpi.c:184-198`（type 0 LAPIC）；`:199-208`（type 1 IOAPIC，记录 address）；`:209-221`（type 2 ISO） | LAPIC/IOAPIC/ISO 三类条目被解析；NMI(type4)、LAPIC 地址覆盖(type5) 未处理 |
| ISO 的 IRQ→GSI 覆盖 | `acpi.c:21-24`（isa_gsi[16] 初值 -1）；`:35-44`（acpi_irq_to_gsi，未解析回退恒等）；`:213`、`:290`（写 acpi_isa_gsi[source_irq]=gsi） | 映射表覆盖全部 16 个 ISA IRQ（0..15），由 ISO 条目逐条改写；非仅 IRQ0 特判——但仅翻译 GSI 号 |
| ISO polarity / trigger 位 | `acpi.h:83`（flags 字段定义）；`acpi.c:211-219`、`:288-291`（只取 source_irq 与 gsi，flags 未读）；`apic64.c:276`（enable_irq 固定 polarity=0 高电平、trigger=0 边沿） | ISO 的极性/触发模式位被丢弃；IOAPIC 重定向表项写死高电平/边沿，level-triggered 或低电平设备会出错 |
| 已在 IOAPIC 使能的中断 | `apic64.c:398`（IRQ0→vector 0x20）；`:400`（IRQ1→0x21）；`:403-405`（IRQ12→0x2C） | 仅 PIT、键盘、PS/2 鼠标三个 GSI 被编程；IRQ8(RTC)、IRQ14/15(IDE)、IRQ3/4 等未接线 |
| 已注册的中断 handler | `irq64_simple.c:216`（IRQ0 → pit64_interrupt）；`:217`（IRQ1 → keyboard64_interrupt） | 仅注册 IRQ0/IRQ1 两个 handler；鼠标/RTC/IDE 无对应 handler 注册 |
| IOAPIC 基址来源 | `apic64.c:59`（IOAPIC_PHYS_ADDR=0xFEC00000 硬编码）；`:209`（映射该硬编码地址）；`acpi.c:201`（acpi_ioapic_address 存入但未被 apic64.c 引用） | IOAPIC 地址未采用 MADT 提供的地址；多 IOAPIC 平台只处理第一片 |
| IOAPIC 重定向表编程 | `apic64.c:251-269`（向量/delivery/dest_mode/polarity/trigger/dest 写入 redtbl）；`:256`（irq > ioapic_max_redir 拒绝）；`:359-362`（先全屏蔽） | 单 IOAPIC、固定物理目标 apic_id；超出 MaxRedir 的 GSI 直接失败；不支持多 IOAPIC 分片 |
| 中断控制器模式切换 | `apic64.c:386-387`（屏蔽主/从 PIC：outb 0x21/0xA1=0xFF）；`:390`（IMCR 切 APIC）；`:408`（屏蔽 LINT0）；`irq64_simple.c:136-155`（PIC 模式下才初始化 8259） | 纯 APIC 模式下屏蔽 PIC 并切 IMCR；未校验 IMCR 写入是否被固件接受，PIC/APIC 混用边角未覆盖 |
| 无 MADT 回退 | `acpi.c:249-251`（无 RSDP 重置恒等并返回 -1）；`:256-259`、`:273-275`（无 RSDT/MADT 同样恒等回退） | 找不到 ACPI 表时按 IRQn=GSIn 恒等假设继续，无告警打印；老固件/无 ACPI 机器上可能路由错误 |

### 上机验证步骤

1. **目的**：第一优先——确认真机 IRQ0(PIT) 的 ISO 覆盖是否被正确翻译（同时决定时钟校准与系统能否起来，见时钟一节 HW-50/HW-51）。
   - **操作**：真机启动，串口观察 ACPI override 打印与后续 APIC 切换日志。
   - **预期屏幕现象**：系统正常进入桌面；若 IRQ0 路由错误，会在校准阶段卡死（见时钟一节）。
   - **预期串口输出（源码真实字符串）**：`[ACPI] IRQ override: ISA IRQ`（`acpi.c:214`）后跟 ` -> GSI`（`acpi.c:216-217`）；`[IOAPIC] Initialized`（`apic64.c:365`）；`[APIC] Switched to pure APIC mode`（`apic64.c:411`）。
   - **通过判据**：串口列出真机全部 ISO 条目，PIT 对应 GSI 被正确编程，系统不卡死。
   - **失败判据**：无 override 打印却按恒等 GSI0 投递，或启动卡死在 APIC 阶段。

2. **目的**：确认键盘（IRQ1）、鼠标（IRQ12）在真机 ISO 覆盖下中断可达。
   - **操作**：进入桌面后敲击键盘、移动鼠标，对照串口 IRQ 计数。
   - **预期屏幕现象**：键盘回显、指针移动。
   - **预期串口输出（源码真实字符串）**：`[IRQ64] PIT+keyboard handlers registered`（`irq64_simple.c:218`）；`[MOUSE64] PS/2 mouse ready (aux device, IRQ12)`（`ps2mouse64.c:144`）。
   - **通过判据**：IRQ1/IRQ12 对应计数随输入增长，无 spurious 风暴。
   - **失败判据**：键盘/鼠标无响应，或串口出现大量 spurious/error 中断。

3. **目的**：核对 IOAPIC 重定向表的极性/触发模式与真机设备一致。
   - **操作**：在带 PCI 网卡/USB 控制器的真机上观察设备中断是否投递；对比 MADT ISO flags 与内核实际写入的 redtbl 项。
   - **预期屏幕现象**：设备中断正常触发，不出现中断丢失或反复触发。
   - **预期串口输出（源码真实字符串）**：`[IOAPIC] ID=`（`apic64.c:341`）、`[IOAPIC] Initialized`（`apic64.c:365`）。
   - **通过判据**：level-triggered/低电平设备的中断能被正确接收并 EOI。
   - **失败判据**：设备中断不来，或同一中断反复触发导致日志刷屏。

4. **目的**：确认无 MADT / 老固件平台的回退行为。
   - **操作**：在可关闭 ACPI 或老 BIOS 的机器上启动（如条件允许），观察回退路径。
   - **预期屏幕现象**：系统按恒等映射继续启动，或明确打印回退告警。
   - **预期串口输出（源码真实字符串）**：`[ACPI] RSDP not found`（`acpi.c:102`）、`[ACPI] MADT not found`（`acpi.c:149`）。
   - **通过判据**：回退路径行为明确，中断路由可预期。
   - **失败判据**：无 RSDP/MADT 时静默按恒等假设，中断静默丢失。

### 缺口与最小补齐清单

| 编号 | 缺口 | 证据 | 阻塞级别 | 最小改动方向（一句话，不实施） |
|---|---|---|---|---|
| HW-40 | ISO 的 polarity/trigger 位未消费，IOAPIC 表项写死高电平/边沿；真机 level-triggered、低电平有效设备（PCI 外设、部分 RTC）中断不投递或反复触发 | `acpi.h:83`；`acpi.c:211-219`、`:288-291`；`apic64.c:276` | 硬阻塞 | 解析 ISO flags，把 polarity(bit1/0) 与 trigger(bit3) 透传到 apic64_set_irq_redirect 的对应位 |
| HW-41 | 仅 IRQ0/IRQ1/IRQ12 在 IOAPIC 使能且注册 handler；IRQ8(RTC)、IRQ14/15(IDE)、IRQ3/4 等真机中断未接线，对应设备中断全部丢失 | `apic64.c:398`、`:400`、`:403-405`；`irq64_simple.c:216-217` | 硬阻塞 | 按 ISO 表为需要的 ISA IRQ 批量编程 IOAPIC 并注册对应 handler，未用的保持屏蔽 |
| HW-42 | IOAPIC 物理地址硬编码 0xFEC00000，未用 MADT IOAPIC 条目地址；多 IOAPIC 平台只映射/编程第一片，GSI 超 MaxRedir 直接拒绝 | `apic64.c:59`、`:209`、`:256`；`acpi.c:201` | 高 | 按 MADT type1 条目记录每片 IOAPIC 的地址与 GSI base，多片分片编程 redtbl |
| HW-43 | 只解析 RSDT，未读 XSDT；RSDP revision>1 的 64 位系统表未消费；无 RSDP/MADT 时静默恒等回退，无告警 | `acpi.c:111`、`:255`；`acpi.h:19`；`acpi.c:250`、`:257`、`:274` | 高 | 按 RSDP revision 选择 XSDT/RSDT，回退路径打印明确告警并导出 isa_gsi 表便于排查 |
| HW-44 | 纯 APIC 模式仅屏蔽 PIC 并写 IMCR，未校验切换结果；PIC/APIC 混用边角（LINT0/LINT1、NMI 路由）未覆盖 | `apic64.c:386-390`、`:408`；`irq64_simple.c:136-155` | 中 | 读回 IMCR/PIC 屏蔽位校验切换成功，补齐 LINT0/LINT1/NMI 的模式确认 |
| HW-45 | ISO 解析失败（无 RSDP/RSDT/MADT）静默重置恒等映射，串口无任何回退告警；真机若固件表扫描失败，IRQ0 会按 GSI0 错误投递 | `acpi.c:249-251`、`:256-259`、`:273-275` | 中 | 回退恒等分支打印来源与缺失表类型，并在 APIC 切换前 dump isa_gsi 映射表 |

## 11 时钟验证

### 现状与证据

| 能力 | 证据 file:line | 结论 |
|---|---|---|
| 校准基准 | `apic64.c:507`（CAL_WINDOW_MS=50）；`:528`（extern pit64_get_ticks）；`:530-532`（对齐下一个 ch0 tick 边界）；`:536-539`（等窗口结束，读 CCR 差值） | 以 PIT channel 0 软件 tick（1ms/tick）为基准实测 LAPIC 在 16 分频下的计数频率 |
| 校准期间中断状态 | `apic64.c:520-552`（calibrate_timer 全函数无 cli/sti/pushf/popf）；`irq64_simple.c:233`（irq64_init 末尾 sti）；`kernel_main_64.c:315`（irq64_init）先于 `:382` smp_init | 校准期间中断保持使能（未被 cli 包裹）；`pit_ticks` 依赖 IRQ0 中断真的被投递并处理 |
| PIT tick 递增位置 | `pit64.c:53`（pit64_interrupt_handler 内 pit_ticks++）；`:103`（pit64_interrupt 内 pit_ticks++）；`:92-95`（get_ticks 直接返回计数） | tick 只在 IRQ0 中断处理里递增；中断不投递则计数冻结 |
| 校准自旋超时上限 | `apic64.c:531`（`while (pit64_get_ticks() == t)` 等边界，无迭代上限）；`:537`（`while (pit64_get_ticks() < target)` 等 50ms 窗口，无迭代上限） | 两处裸自旋，无超时计数、无降级返回；tick 不推进即永久挂住 |
| 初始化先后顺序 | `kernel_main_64.c:298`（pit64_init）→ `:315`（irq64_init，末尾 sti）→ `:361`（apic64_init）→ `:368`（acpi_init_irq_overrides）→ `:375`（switch_to_apic_mode）→ `:382`（smp_init）；`smp.c:301-302`（BSP apic64_init_timer）；`smp.c:115-116`（AP apic64_init_timer） | PIT 初始化先于校准，顺序本身正确；风险不在"PIT 未初始化"，而在中断投递链路在纯 APIC 模式下是否生效 |
| 校准失败回退 | `apic64.c:564`（g_apic_timer_hz==0 且校准失败）；`:567`（打印 WARN）；`:568`（回退 ICR=6250）；`:570-571`（成功时 hz/1000，下限 100） | 失败回退保守值 6250（100MHz 总线假设），有告警打印；回退后不再二次校准 |
| 合理性区间校验 | `apic64.c:543`（ticks==0 失败）；`:545`（actual_ms==0 失败）；`:548`（hz 不在 0.5MHz~100MHz 失败） | 校准结果有区间过滤，越界返回 -1 走回退 |
| AP 核复用 BSP 结果 | `apic64.c:511`（g_apic_timer_hz 全局）；`:564`（AP 进入时若已非 0 直接复用）；`smp.c:115-116`（AP 调 apic64_init_timer） | AP 不各自实测，复用 BSP 校准值；各核总线频率不同时未分别测量 |
| 其他时钟源 | `rtc64.c:121-137`（RTC 只读 CMOS 墙钟，周期中断位被注释未开）；`x86_64.c:205`（仅探测 TSC 特性位，未用于校时） | 系统 tick 源为 LAPIC timer + PIT；未用 TSC/HPET 做交叉校验；RTC IRQ8 未接 |
| 真机频率自适应 | `apic64.c:542-548`（用 CCR 递减量/真实窗口长度算 hz）；`:524`（DCR=16 与最终配置一致） | 校准是实测 CCR 递减，理论上自适应不同 APIC 总线频率；需上机核对实测值 |

### 上机验证步骤

1. **目的**：判断启动卡死是否发生在 LAPIC 定时器校准阶段。
   - **操作**：真机启动，记录串口最后出现的一行；对照校准相关打印的先后顺序。
   - **预期屏幕现象**：若卡在本阶段，屏幕静止、桌面未出现、无报错刷屏。
   - **预期串口输出（源码真实字符串）**：校准成功 `[APIC] timer calibrated: tick_hz=`（`apic64.c:572`）后跟 ` ICR=`（`:574-575`）；校准失败 `[APIC] WARN: timer calibration failed, fallback ICR=6250`（`apic64.c:567`）；此前应有 `[INIT] Initializing APIC (step 1: Local APIC only)...`（`kernel_main_64.c:360`）与 `[OK] APIC initialized`（`kernel_main_64.c:362`）。
   - **通过判据**：串口出现 `[APIC] timer calibrated: tick_hz=... ICR=...`，且 ICR 数值合理（约等于总线频率/16/1000）。
   - **失败判据**：串口停在 `[INIT] Initializing APIC` 之后、`[APIC] timer calibrated` 之前（或完全没有该行），屏上无 `[OK] APIC initialized`，即可判定卡在校准自旋。

2. **目的**：在无 monitor 条件下判断 tick 是否为 1kHz（时钟可信度主判据）。
   - **操作**：进入桌面后打开手机秒表，对照桌面/串口显示的系统时间连续观察 60 秒；或在串口按固定 tick 间隔观察时间戳增长。
   - **预期屏幕现象**：屏幕时钟秒数与手机秒表同步走动，不快不慢。
   - **预期串口输出（源码真实字符串）**：校准行 `[APIC] timer calibrated: tick_hz=`（`apic64.c:572`）；PIT 初始化 `[PIT64] Frequency: 1000 Hz (1ms tick)`（`pit64.c:77-88`）。
   - **通过判据**：60 秒对比误差在 1 秒以内；tick_hz 与 ICR 比值对应约 1kHz。
   - **失败判据**：时钟明显走快/走慢（数倍偏差），或停走；若出现 WARN 回退行，说明已落到 ICR=6250 保守值。

3. **目的**：核对校准结果在真机不同 APIC 总线频率下是否自适应。
   - **操作**：在两台总线频率不同的真机（或同机不同固件版本）上记录 tick_hz 与 ICR。
   - **预期屏幕现象**：系统稳定运行，调度时间片无明显错乱。
   - **预期串口输出（源码真实字符串）**：`[APIC] timer calibrated: tick_hz=<hex> ICR=<hex>`（`apic64.c:572-576`）。
   - **通过判据**：两台机器 tick_hz 随总线频率不同而不同，ICR 均使目标频率约 1kHz。
   - **失败判据**：tick_hz 落在 0.5MHz~100MHz 区间外被拒，或统一回退到 ICR=6250。

4. **目的**：核对 SMP 下 AP 核复用 BSP 校准值是否可靠。
   - **操作**：多核真机启动，观察各核 LAPIC timer 是否投递 tick；任务管理器/串口查看 per-CPU tick 计数。
   - **预期屏幕现象**：多核均产生 timer tick，无核卡死或调度停滞。
   - **预期串口输出（源码真实字符串）**：`[SMP] BSP APIC timer enabled`（`smp.c:305`）。
   - **通过判据**：BSP 校准值被 AP 复用后各核 tick 正常。
   - **失败判据**：AP 核不产生 tick，或复用回退值后 AP 侧时间片偏差明显。

### 缺口与最小补齐清单

| 编号 | 缺口 | 证据 | 阻塞级别 | 最小改动方向（一句话，不实施） |
|---|---|---|---|---|
| HW-50 | 校准两处裸自旋无超时上限：若 PIT IRQ0 中断未投递，`pit_ticks` 不推进，`:531`/`:537` 永久挂死，桌面出现前屏幕静止、无报错；本机 QEMU 因中断链路正常而无法暴露 | `apic64.c:530-532`、`:536-537`；`pit64.c:53`、`:103`、`:92-95`；`kernel_main_64.c:298`、`:315`、`:361`、`:382`；`smp.c:301-302` | 硬阻塞 | 给两处自旋加迭代/时间超时上限，超时后返回 -1 走 ICR=6250 回退，并把失败原因（哪一处自旋超时）打印到串口 |
| HW-51 | 校准基准依赖 PIT ch0 中断投递链路：顺序上 PIT 先于校准（无"PIT 未初始化"问题），但纯 APIC 模式切换后若 IRQ0 的 GSI 翻译/IOAPIC 表项在真机未生效，基准 tick 冻结即触发 HW-50 死锁；与 ACPI 一节 IRQ0 回归项互为因果 | `kernel_main_64.c:298`→`:315`→`:361`→`:375`→`:382`；`apic64.c:398`；`pit64.c:103` | 高 | 校准前显式确认 PIT tick 已在推进（读两次 get_ticks 比对），未推进则放弃校准走回退，不在自旋中等候 |
| HW-52 | 校准失败回退 ICR=6250 基于 100MHz 总线假设；真机总线频率偏差时 tick 偏离 1kHz，调度时间片、TCP 超时/重传随之偏差；回退后无二次校准 | `apic64.c:567-568`；`net64_tcp.c:412`（TCP 定时器挂在 PIT/调度 tick 上） | 中 | 回退路径记录偏差量级并在后续空闲时重试校准，TCP/超时路径标注 tick 频率来源 |
| HW-53 | AP 核直接复用 BSP 校准结果，不各自实测；BSP 走回退值时 AP 同样使用回退值；各核 APIC 总线频率差异未覆盖 | `apic64.c:511`、`:564`、`:570`；`smp.c:115-116` | 中 | AP 核启动时独立做一次轻量校准，或在 SMP 初始化时校验各核总线频率一致性 |
| HW-54 | 时钟源仅 LAPIC timer + PIT，未用 TSC/HPET 交叉校验；RTC 只读墙钟、IRQ8 周期中断未开；长时间运行 tick 漂移无监控 | `rtc64.c:121-137`；`x86_64.c:205`；`apic64.c:555-583` | 中 | 引入 TSC/HPET 作为对照时基，定期比对 LAPIC tick 频率，漂移超阈值时告警或重校准 |


---

## 12 取证方法（串口 / 屏显 / 最小验证内核）

### 12.1 串口取证

- 硬件：USB-RS232 线（PL2303 / CH340）接主板 COM1（DB9），终端设 115200 8N1。
- 采集命令（Linux 主机）：`stty -F /dev/ttyUSB0 115200 cs8 -cstopb -parenb; cat /dev/ttyUSB0 > hepu_boot_$(date +%s).log`。
- 从 stage2 第一行 `"[Stage2_64] Starting..."` 到桌面 `"[DESKTOP64] HepuShell started as default desktop"` 或崩溃行，全量保存。

### 12.2 屏显取证

- V0.20.8 的黑屏字符定位表（保留，见附录 A.3）仍然有效：屏幕上出现什么字符 = 卡在哪一步。
- 一旦 VBE 切到图形模式（`stage2_64.S:76` `vbe_setup_common`），VGA 文本输出停止，无 TTF 时屏幕全黑。**此时唯一证据就是串口**。

### 12.3 崩溃现场取证

- 内核态异常：串口出现 `[EXCEPTION] vector=... error=... rip=...`，对 #PF 还会有 `CR2=` 和页表四级转储（`lib64.c:459-483`）。抄全。
- `hepu_panic`：串口出现 `*** HEPU PANIC ***` + 消息 + `Stack trace:` + 最多 8 个返回地址 + `System halted.`（`lib64.c:584-602`）。抄全，机器会停在那。
- **不要按电源键重启**直到抄完/拍完。

### 12.4 挂死与"正常但没画出来"的区分

- **真机若黑屏且无串口，无法区分"挂死"与"正常但没画出来"**：HW-01a 这类永久自旋、没有任何报错的挂死，在黑屏现场看起来就是"机器没反应"，和"VBE 切图形后没画出来"完全无法区分。这正是可观测性被列为 §0 前置阻塞的原因——没有串口，你既不知道它停在哪一行，也不知道它是死了还是在跑。
- 黑屏后过几秒自动回 BIOS 启动菜单 → 三重故障（CPU 指令不兼容 / 页表问题）。
- 黑屏后一直不回 → 内核 hlt 住了，可能是异常已打印但你没看到，也可能是 HW-01a 这类自旋挂死。

---

## 13 失败判据速查与故障排查表

| 现象 | 串口末尾 | 卡在哪 | 下一步 |
|------|----------|--------|--------|
| 全黑，什么都没有 | 无 | MBR 没跑起来，BIOS 没把 U 盘当启动盘 | §1.2 重写 U 盘；换 USB 口；BIOS 启动模式确认 Legacy/CSM |
| 左上角 `H1` 后黑屏 | 无（MBR 不打串口） | stage2 读盘失败 | 接串口重看；换 SATA 直连；重试 INT13（已在 `stage2_64.S:343` 加 3 次） |
| `E! MBR READ ERR` | 无 | LBA34 读不出 | dd 是否整盘写；U 盘分区表是否被 BIOS 接受 |
| 串口停在 `[Stage2_64] Starting...` | Starting | serial_init 之后、菜单之前 | 极少见；拍屏看是否有 `64` |
| 串口出现 `ERROR: Disk read failed!` | Disk read failed | LBA2000 读内核 3 次失败 | U 盘慢盘；换口；SATA 直连试 |
| 屏幕红 `NO` | `ERROR: CPU does NOT support long mode!` | CPU 非 x86_64 | 换 CPU |
| 串口 `JMP>` 后断流 | 无 `=== HepuOS ...` | 跳内核即三重故障 | 拍屏；历史上是内存 <2GB 或页表问题（附录 A.1） |
| 屏幕已出现内核早期输出（`[OK] Long mode active` 等），然后**整屏静止、无报错、不推进到桌面**；串口最后停在 `[INIT] Initializing APIC (step 1: Local APIC only)...`，**没有** `[APIC] timer calibrated: tick_hz=... ICR=...`，也没有 `[APIC] WARN: timer calibration failed, fallback ICR=6250`，也没有 `[OK] APIC initialized` | 末尾 = `[INIT] Initializing APIC (step 1: Local APIC only)...` | **HW-01a：LAPIC 定时器校准在 PIT tick 自旋里永久挂死**（IRQ0 实际投递链路不成立时 `pit_ticks` 不推进） | 先换一台 IRQ0 未被 ACPI ISO 重定向到非 GSI2 的机器复测；用串口确认最后一行原样抄下；记录机型/芯片组/BIOS 版本回报；现场无其它按键可救（`hlt`/自旋） |
| 串口 `[EXCEPTION] vector=0d/0e` | 含 rip/CR2 | 内核态页错误/通用保护 | 抄 CR2 和四级页表 |
| 串口 `[AHCI64] No AHCI controller found` | No AHCI controller | BIOS SATA 模式是 IDE/AHCI 未开 | BIOS 改 SATA=AHCI |
| 串口 `[AHCI64] port N: SATA HDD` 后 `falling back to IDE` | No SATA/USB disk | **HW-03：total_sectors 未赋值** | 这是代码缺口，不是机器问题 |
| 串口 `[INIT] Probing USB boot...` 后断流 | UHCI no storage, trying EHCI | EHCI 枚举失败（HW-06） | 换 USB 2.0 口；USB 键盘另插 |
| 串口 `[BOOT] step6 done (ext2)` 后黑屏 | step6 done | VBE/TTF/桌面初始化 | 见 §9 显示与输入验证 |
| 串口 `[DESKTOP64] HepuShell started as default desktop` | started | 启动成功 | 进 Shell |
| 无限自动重启回 BIOS | 无串口日志 | 三重故障循环 | 接串口；大概率页表/内存问题 |

---

## 14 缺口清单（按阻塞程度排序）


| 编号 | 缺口 | 证据 file:line | 影响哪一步 | 阻塞级别 | 最小改动方向（不实施） |
|------|------|----------------|------------|----------|------------------------|
| HW-01a | LAPIC 定时器校准两处 PIT tick 自旋无超时上限；IRQ0 投递链路在真机上若不成立（ACPI ISO 重定向到 GSI≠2、IOAPIC 表项未生效）则 `pit_ticks` 不推进，永久挂死在 APIC 初始化。**该缺陷在 QEMU 下不暴露**（QEMU 的 IRQ0→GSI2 恰好匹配代码特判），故前几轮 QEMU 全绿仍可能真机起不来。 | `kernel/arch/x86_64/apic64.c:531` `while(pit64_get_ticks()==t){}`、`:537` `while(pit64_get_ticks()<target){}`；`kernel/arch/x86_64/pit64.c:53` `pit_ticks++` 仅在 PIT IRQ0 处理里递增；调用链 `kernel_main_64.c:298→315→361→382`、`kernel/arch/x86_64/smp.c:301-302`→`apic64.c:520-552` | §3 P3.5、§4.3、§13 | **硬阻塞** | 给两处自旋加迭代上限（如 100 万次空转），超时返回 -1 走已有的 ICR=6250 回退分支，并打印 `[APIC] WARN: timer calibration failed, fallback ICR=6250`（`apic64.c:567` 已有此打印可复用） |
| HW-01 | 默认镜像无 ESP，纯 UEFI 机器无法引导 | `tools/make_single_img64.sh:39-262`（全程无 ESP/GPT） | §2、§5 | **硬阻塞** | 在 make_single_img64.sh 末尾调 build_esp.py 或 build_uefi_image.sh，把 64 位内核装进 ESP |
| HW-02 | UEFI 构建脚本拷的是 32 位内核，64 位 UEFI 引导未打通 | `tools/hepboot/uefi/build_uefi_image.sh:11` | §5 | 高 | 改拷 `build_x86_64/hepu-kernel64.bin`，并核实 jump_kernel_64.S 与 64 位 entry 对齐 |
| HW-03 | AHCI 从不发 IDENTIFY，total_sectors 从未赋值，SATA 路径成死代码 | `kernel/arch/x86_64/ahci64.c:93`、`:243`；`kernel/kernel_main_64.c:563` | §6 | **硬阻塞** | port_init 后加 IDENTIFY(0xEC) FIS，解析 48-bit LBA count 写入 total_sectors |
| HW-04 | 无 NVMe 驱动 | `kernel/arch/x86_64/Makefile`（无 nvme 源文件）；`storage_hal64.c:23` 注释提到但无代码 | §7.1 | **硬阻塞**（纯 NVMe 机器） | 新增 nvme64.c，编入 Makefile，接入 storage_hal |
| HW-05 | raw_write 无 AHCI 分支，读写不对称 | `kernel/arch/x86_64/storage_hal64.c:90-111` | §6.2 | 高 | raw_write 镜像 raw_read 的 AHCI 探测块 |
| HW-06 | EHCI 真机行为未验证（QEMU 异步列表限制） | `kernel/arch/x86_64/ehci64.c`；历史记录见附录 A.2 | §7.3 | 高 | 真机实测 EHCI 枚举 USB U盘/键鼠；失败则需真机调试 |
| HW-07 | 无"最小验证内核"（不切 VBE、不 init 设备，只打串口） | 当前内核 `kernel_main_64.c:741` 起即 VBE 初始化 | §0.1、§12 | 中 | 加一个 `MINIMAL_BOOT=1` build 选项，跳过 VBE/设备，只串口 |
| HW-08 | make_single_img64.sh 两条 ext2 写入路径文件集不对齐 | `tools/make_single_img64.sh:109-149` vs `:151-260` | §1.3 | 中 | 统一两条路径写入同一组 /bin/*.elf，或显式参数控制 |
| HW-09 | build_uefi_dual.py PROJECT 路径硬编码失效 | `tools/build_uefi_dual.py:14` | §5 | 低 | 改成相对仓库根目录定位 |
| HW-10 | stage2 VBE 切图形后无 TTF 则黑屏，屏显通道失效 | `tools/hepboot/x86_64/stage2_64.S:76` | §12.2 | 中 | 见 §9 显示与输入验证 |
| HW-11 | 串口仅轮询、无中断；16550 FIFO 触发 14 字节 | `kernel/arch/x86_64/uart64.c:114` | §0 | 低 | 真机够用，暂不动 |
| HW-12 | >4GB 内存 E820 精化未做（stage2 用 E801，只到 4GB） | `tools/hepboot/x86_64/stage2_64.S:214` | §4.3 | 低 | 后续加 E820 遍历 |

### 14.1 跨节汇总：阻断上机的条目（一页看完）

> 本表只收录**硬阻塞**与**高**两级；中/低两级与实际改法见各编号段自己的表（HW-20 起在 §8、§9、§10、§11 各自节末）。

| # | 编号 | 缺口（一句话） | 级别 | 阻断哪一步 | 详细表 |
|---|---|---|---|---|---|
| 1 | HW-01a | LAPIC 定时器校准两处 PIT tick 自旋无超时上限；IRQ0 投递链路在真机上若不成立（ACPI ISO … | **硬阻塞** | 启动即挂死（无报错） | §14 缺口清单 |
| 2 | HW-50 | 校准两处裸自旋无超时上限：若 PIT IRQ0 中断未投递，`pit_ticks` 不推进，`:531`/`:537… | **硬阻塞** | 同源：时钟校准死锁 | §11 时钟验证 |
| 3 | HW-01 | 默认镜像无 ESP，纯 UEFI 机器无法引导 | **硬阻塞** | UEFI 机器无法引导 | §14 缺口清单 |
| 4 | HW-03 | AHCI 从不发 IDENTIFY，total_sectors 从未赋值，SATA 路径成死代码 | **硬阻塞** | SATA 盘识别不到 | §14 缺口清单 |
| 5 | HW-04 | 无 NVMe 驱动 | **硬阻塞** | 纯 NVMe 机器无盘 | §14 缺口清单 |
| 6 | HW-41 | 仅 IRQ0/IRQ1/IRQ12 在 IOAPIC 使能且注册 handler；IRQ8(RTC)、IRQ14/1… | **硬阻塞** | 真机中断大量缺失 | §10 ACPI 与中断验证 |
| 7 | HW-40 | ISO 的 polarity/trigger 位未消费，IOAPIC 表项写死高电平/边沿；真机 level-tri… | **硬阻塞** | 中断极性/触发方式不符 | §10 ACPI 与中断验证 |
| 8 | HW-02 | UEFI 构建脚本拷的是 32 位内核，64 位 UEFI 引导未打通 | 高 | UEFI 64 位引导未打通 | §14 缺口清单 |
| 9 | HW-05 | raw_write 无 AHCI 分支，读写不对称 | 高 | AHCI 写路径缺失 | §14 缺口清单 |
| 10 | HW-06 | EHCI 真机行为未验证（QEMU 异步列表限制） | 高 | EHCI 真机未验证 | §14 缺口清单 |
| 11 | HW-20 | 协议栈收发只接 e1000 驱动，RTL8139 的 send/recv 定义后无调用方；即便 RTL 初始化成功，… | 高 | RTL 收发未接协议栈 | §8 网络验证 |
| 12 | HW-21 | 网卡 PCI ID 覆盖极窄：e1000 仅 0x100E/0x100F/0x1000，RTL 仅 0x8139；无… | 高 | 网卡 ID 覆盖极窄 | §8 网络验证 |
| 13 | HW-24 | 静态 IP/网关写死 10.0.2.15/10.0.2.2；DNS 兜底硬编码 8.8.8.8；DHCP 成功后是否… | 高 | IP/DNS 硬编码 | §8 网络验证 |
| 14 | HW-30 | 帧缓冲信息完全依赖引导器写 0x6000，内核不校验 EBS 后 GOP 内存是否仍有效；独显/核显平台 EBS 后… | 高 | EBS 后 GOP 未校验 | §9 显示与输入验证 |
| 15 | HW-31 | 无 16bpp（RGB565 等）渲染分支；真机 GOP 给出 16bpp 时 put_pixel/get_pixe… | 高 | 无 16bpp 渲染 | §9 显示与输入验证 |
| 16 | HW-34 | PS/2 键盘依赖 IRQ1 中断投递；IRQ1 的 GSI 翻译、极性/触发位若在真机失效，键盘完全无响应而轮询鼠… | 高 | 键盘依赖 IRQ1 | §9 显示与输入验证 |
| 17 | HW-42 | IOAPIC 物理地址硬编码 0xFEC00000，未用 MADT IOAPIC 条目地址；多 IOAPIC 平台只… | 高 | IOAPIC 地址写死 | §10 ACPI 与中断验证 |
| 18 | HW-43 | 只解析 RSDT，未读 XSDT；RSDP revision>1 的 64 位系统表未消费；无 RSDP/MADT … | 高 | 只读 RSDT 不读 XSDT | §10 ACPI 与中断验证 |
| 19 | HW-51 | 校准基准依赖 PIT ch0 中断投递链路：顺序上 PIT 先于校准（无"PIT 未初始化"问题），但纯 APIC … | 高 | 校准基准依赖中断链路 | §11 时钟验证 |

**上机顺序建议（硬阻塞未清之前不要指望走完全链路）**：

1. 先解决 `HW-01a`/`HW-50`（校准加超时回退）——否则真机可能连桌面都到不了，且现场无任何线索；
2. 再接串口（§0，代码已就绪，只需硬件），之后所有判据才有依据；
3. BIOS 路径可用，`HW-01`（无 ESP）只影响 UEFI 机器，先用 BIOS/CSM 机器跑通 §4；
4. 存储先走 IDE/USB mass storage（§7.2/§7.3）——SATA(`HW-03`)与 NVMe(`HW-04`)在补齐前必然失败；
5. 网络(`HW-20`/`HW-21`)与显示(`HW-30`/`HW-31`)按 §8/§9 逐项登记现象，不阻塞"读文件"这一步。

---

## 附录 A 历史真机记录（保留原文要点）

### A.1 V0.13.63 真机启动专项排查记录（2026-10-24）

本轮针对「真机黑屏回 BIOS / 卡黑屏」系统性排查，定位并修复 4 类影响真机启动/运行的因素。

**1. 内存探测链（真机 <2GB 崩溃根因）**

- 问题：内核硬编码 2GB 内存（`pmm64_init(2GB)`），stage2 写死 256MB。真机 <2GB 时内核分配不存在的物理页 → 启动即崩（黑屏回 BIOS）。
- 修复：stage2 实模式 `int15 E801` 探测总内存（单次调用，无缓冲区）；调用点固定在**读盘完成之后、进保护模式之前**（`load_64bit_done`）——SeaBIOS 的 VBE/INT13 服务会清零 0x90000 低内存区，过早探测结果丢失；结果写 `0x90100`（64 位：低 32 位 + 高 32 位），避开内核分块加载区与 PML4/页表区；UEFI 引导器（`hepboot_uefi.c`）同样换算内存映射写 `0x90100`；内核 `pmm64_init` 读 `0x90100` 替代硬编码 2GB（下限 256MB 兜底，上限 2GB）；`vmm64_init` 全内存身份映射（2MB 大页，上限 0x80000000）。
- 验证：QEMU `-m 384` 实测 `PMM physical memory: 0x17F MB`（383MB），桌面正常启动。
- 注意：E801 只覆盖到 4GB；内核 PMM 上限 2GB，满足。>4GB 真机后续可加 E820 精化（HW-12）。

**2. AHCI BAR 高区映射（真机 SATA 盘页错误）**

- 问题：`ahci64_init` 直接解引用物理 BAR5 地址。真机 AHCI BAR5 通常在 `0xF0000000+`（PCI MMIO 高区），不在 vmm 身份映射范围（0~2GB）→ 访问寄存器页错误崩溃。QEMU 无 AHCI 故未暴露。
- 修复：访问前把 BAR 所在 2MB 页映射进页表（`vmm64_map_page` + 2MB 大页 + invlpg），再初始化 HBA。代码见 `ahci64.c:439-445`。

**3. AHCI 接入系统盘读取链（真机 SATA rootfs 读不到）**

- 问题：系统盘读取固定走 IDE 驱动（`g_ext2_block_read` 默认 `ide64_read_sectors`、storage HAL 默认 IDE）。真机 SATA 盘是 AHCI 控制器，IDE 驱动读不到 → 内核无法挂载 rootfs。
- 修复：`ahci64.c` 增加盘级入口 `ahci64_disk_present()` / `ahci64_read_disk()` / `ahci64_write_disk()`；storage HAL `storage_hal_raw_read` 有 AHCI 盘时优先走 AHCI；内核启动时 AHCI 盘存在 → `ext2_64_set_block_device(ahci64_read_disk)`。
- **⚠️ 本轮核实**：V0.13.63 当时认为已修复，但 0.54.11 代码里 `ahci64_disk_present()` 要求 `total_sectors > 0` 而 `total_sectors` 从未被赋值（HW-03），所以这条链在真机上仍然是断的。历史修复只接通了"端口枚举 + BAR 映射"，没接通"盘容量识别"。

**4. 读盘重试（真机慢盘/偶发失败）**

- 问题：stage2 INT13 读盘单次执行，失败即 `read_error`（黑屏回 BIOS）。
- 修复：致命读取点（HDD 前 2 次 + CD 前 2 次）加重试 3 次，每次重发 `AH=42h` 扩展读取，3 次失败才报错。代码见 `stage2_64.S:343-351`。

**待真机验证/后续项（保留原表）**：

| 项 | 状态 |
|----|------|
| 32 位内核读取路径重试 | 低优先级，待加 |
| 现代 USB 控制器（EHCI/XHCI） | 已编入并在 `usb64_init()` 回退尝试（`ehci64.c`/`xhci64.c`，Makefile:103-104）；QEMU 异步列表限制，真机行为待验证；老机器 UHCI 走 PS/2 兜底 |
| 真机网卡兼容（e1000 82540EM / rtl8139 为老型号） | 网络功能待真机验证 |
| UEFI BootFailCount 内核不清零 | 仅计数，不影响启动 |
| >4GB 真机 E820 精化 | 内核上限 2GB，暂不阻塞 |

### A.2 V0.13.64: EHCI 模拟器限制记录

- 背景：真机现代主板无 UHCI(PIIX)，需 EHCI(USB 2.0) 支持无线鼠标/键盘等。
- 实现：`kernel/arch/x86_64/ehci64.c`（寄存器/QH/qTD/控制传输/中断端点框架），`usb64.c` 无 UHCI 时自动尝试 EHCI，`pci64.c` 新增 `pci_find_class_bar0`。
- QEMU 7.2 实测：寄存器布局确认（caplen=0x20, USBCMD@0x20, USBSTS@0x24, FRINDEX@0x2C, ASYNCLIST@0x38, CONFIGFLAG@0x40, PORTSC@0x44），端口连接检测正常（ps=0x1003 CCS+PED），复位/地址分配流程可走。
- 已知限制（模拟器）：QEMU usb-ehci 异步列表不执行 —— ASYNCLIST 写入生效（读回正确）、FRINDEX 递增（帧调度在走）、USBCMD RUN+ASEN+PSEN 正确、IAAD 门铃触发、多套设备/控制器均复现：qTD 保持 ACTIVE 不被控制器处理。
- 结论：EHCI 实现按 EHCI 规范构造（异步列表 QH/qTD 链），驱动逻辑与真机硬件行为一致；QEMU 模拟器限制不影响代码正确性，真机行为待实机验证（HW-06）。
- 验证基线：无 UHCI 时 QEMU 无法用 EHCI 传输（上述限制）；有 UHCI 时全链路回归通过（枚举 VID=0x0627/PID=0x0001/cfgs=1，HID 鼠标可用）。

### A.3 v0.20.8: 真机黑屏快速定位

不需要串口，看屏幕上出现的字符：

| 屏幕显示 | 卡在哪 |
|---------|--------|
| 全黑，什么都没有 | MBR 没跑起来，BIOS 没把盘当启动盘 |
| 左上角 "H1" | MBR 跑起来了，正在读 stage2 |
| "H1" 后黑屏 | stage2 读盘失败或 VBE 切换失败 |
| "E! MBR READ ERR" | LBA34 读不出来，换 SATA 直连试 |
| 启动菜单出现 | stage2 OK，卡在内核加载或长模式 |
| 彩色 Logo 出现 | VBE OK，卡在内核驱动初始化 |
| 黑屏自动回 BIOS | 三重错误复位（CPU 指令不兼容/页表问题） |

拍照发回最后一帧即可定位。
