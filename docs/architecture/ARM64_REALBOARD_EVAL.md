---
title: ARM64_REALBOARD_EVAL
status: draft-evaluation
sys_version: 0.63.0
updated: 2026-11-18
owner: architecture
source_of_truth: kernel/arch/aarch64/arch_port_aarch64.c, kernel/arch/aarch64/m1/m1.c
supersedes: null
---

# ARM64 真板（RK3588S）移植可行性评估

> **评估基线**：工作树分支 `docs/arm64-realboard-eval`，基于 main HEAD `391e1db`（GitHub `midongsang/hepu-os`）；
> 评估日期 2026-11-18。本机无 RK3588S 硬件，也无任何 ARM64 真板。
>
> **免责声明**：本文所有结论基于公开资料与仓库代码静态对照，未在 RK3588S 实机验证。
> 凡公开资料无法确认的条目，一律标注「未确认」并说明缺少的资料；本文不包含任何工期估算或进度量化。

## 摘要（结论先行）

- **总体判断**：现有 ARM64 代码只在 QEMU `virt` 机器上验证过（UEFI 引导 → GOP 出图 → 4KB 页表接管 → 全 RAM 映射，见 `kernel/arch/aarch64/README.md` M1d-1 段）。移植到 RK3588S 真板**可行但不能直接搬**：串口、PSCI conduit、GIC v2→v3、内存锚点四类必须改，通用定时器读取、EFI/GOP 协议层、VBAR 安装机制本身不需要改。
- **第 1 节（启动链）一句话结论**：QEMU 链是「EDK2 固件直接加载 PE/COFF 应用」，真板链是「BootROM → DDR 初始化 → TF-A BL31 → U-Boot（BL33）→ U-Boot EFI 加载器加载 `BOOTAA64.EFI`（或 U-Boot 直接引导裸镜像）」；现有 `BOOTAA64.EFI` 能否被真板 U-Boot 加载，取决于 ESP/路径、PE/COFF 合规性、`CONFIG_EFI_LOADER`/`CONFIG_CMD_BOOTEFI` 开关、U-Boot 是否提供 GOP，以及 ExitBootServices 后的内存与中断状态。
- **第 2 节（代码级清单）一句话结论**：串口（PL011 → DW8250 类 UART）、PSCI（HVC → 预期 SMC）、GIC（v2 MMIO → v3 GICD/GICR）、内存锚点（0x4000_0000 → 固件 map / DT）四项**必须改**；定时器计数读取、GOP 协议取帧缓冲、VBAR 向量安装**不需要改机制**，但像素格式/stride、CNTFREQ、保留内存等细节**待确认**。
- **第 3 节（QEMU 覆盖边界）一句话结论**：QEMU 只覆盖了单核 MMU/串口/PSCI 关机/GOP 出图；GICv3 ITS/LPI/MSI、cache 一致性与 DMA/IOMMU、真板 DDR 保留区与 TrustZone、电源域/DVFS、PSCI CPU_ON 多核启动这五类**QEMU 未验**，原因逐条见第 3 节。
- **第 4 节（路径对比）一句话结论**：推荐先走 **路径 A（继续 UEFI，由 U-Boot EFI 加载器加载现有 EFI 应用形态）**，前置条件最少、可复用现有 M0/M1 代码；路径 B（BL31 后裸机直接接管）作为 A 跑通后的备选，早期初始化负担全部上移到镜像自身。
- **第 5 节（下一步）一句话结论**：最小可跑通顺序为「板级信息收集 → 串口后端抽象 → PSCI conduit 可配置 → 镜像入 ESP 由 bootefi 加载出串口 → 内存映射核对 → GOP 出图 → GICv3 接管 → 多核启动」，每一步的通过判据是具体串口输出或可见画面，见第 5 节。

## 目录

- [1. 启动链差异](#1-启动链差异)
- [2. 代码级差异清单](#2-代码级差异清单)
- [3. QEMU 无法验证的部分](#3-qemu-无法验证的部分)
- [4. 两条路径对比](#4-两条路径对比)
- [5. 可执行的下一步（最小可跑通顺序）](#5-可执行的下一步最小可跑通顺序)
- [6. 次选对比：Radxa Dragon Q8B](#6-次选对比radxa-dragon-q8b)
- [7. 未确认条目汇总](#7-未确认条目汇总)
- [附录 A：仓库代码出处](#附录-a仓库代码出处)
- [附录 B：公开资料出处](#附录-b公开资料出处)

---

## 1. 启动链差异

### 1.1 两类启动链概述

**QEMU virt + `QEMU_EFI.fd`（本仓库现状）**：QEMU 虚拟出 `virt` 机器，`-bios` 加载 EDK2 固件 `QEMU_EFI.fd`；固件 BDS 从 ESP（FAT32）查找并加载 `EFI/BOOT/BOOTAA64.EFI`，应用跑在固件建立的 EL1 环境里，通过 EFI BootServices 取内存映射、GOP 帧缓冲，最后自行接管页表并经 PSCI 关机。复现命令与必要参数见 `kernel/arch/aarch64/README.md:81-90`。

**RK3588S 典型真板链**：片上 BootROM → DDR 初始化二进制（rkbin TPL/ddr bin）→ TF-A BL31（EL3 运行固件）→ U-Boot（BL33）→ 两条子路径：
- **A 子路径**：U-Boot 的 EFI 加载器加载 ESP 上的 `BOOTAA64.EFI`；
- **B 子路径**：U-Boot 直接引导内核/裸镜像（`bootm`/`bootz`/EFI stub）。

TF-A 官方文档对 AARCH64 Rockchip 平台的链路口径为：`Bootrom → BL1/BL2 → BL31 → BL33 → Linux kernel`，其中 BL1/BL2 与 BL33 可由 U-Boot（TPL+SPL 或仅 SPL）或 Coreboot+Depthcharge 承担（出处见附录 B [4]）。在 Rockchip 实际打包中，`BL31` 为 `rk3588_bl31_v*.elf`，DDR 初始化由 `ROCKCHIP_TPL` 指定的 ddr bin 完成（出处见附录 B [5]）。

### 1.2 逐阶段对照表

| 阶段 | QEMU virt + QEMU_EFI.fd | RK3588S 典型真板 | 对 Hepu 的含义 |
|---|---|---|---|
| 复位 / BootROM | 无片上 BootROM；QEMU 直接把 `-bios` 指定的固件载入 flash 地址区间（QEMU virt：flash 起于 `0x0`，RAM 起于 `0x4000_0000`，出处附录 B [1]） | 片上内部 ROM 从上电介质（eMMC/SD/SPI）读取下一阶段镜像，校验并跳转 | 镜像不负责复位向量；入口形态取决于引导器（EFI 应用或裸机 ELF/raw） |
| DDR 初始化 | 虚拟 RAM 由 `-m` 参数给定，无内存训练、无 DRAM 控制器配置 | TPL/SPL（rkbin ddr bin）做 DDR 训练与控制器初始化，之后 DRAM 才可用 | 路径 A 下该阶段由固件完成；路径 B 下镜像也不能假设 DDR 已按自己的期望布局（仍由固件初始化好，镜像只发现可用区间） |
| 安全固件（EL3） | 默认 `secure=off`，无 EL3 固件；PSCI 由 QEMU 自身截获，仓库现状用 `hvc #0`（`arch_port_aarch64.c:39`；QEMU 实测关机退出，`README.md:28`） | TF-A BL31 常驻 EL3，提供 PSCI（CPU_ON/OFF/SUSPEND、系统关机/复位）、安全内存管理；conduit 预期为 SMC（见 1.3 第 6 条） | 镜像的 PSCI 调用指令（HVC/SMC）必须与 EL3/监控层一致；BL31 预留的安全内存不可触碰 |
| 非安全固件（BL33） | EDK2（`QEMU_EFI.fd`）：DXE 驱动环境、BDS 启动选择、EFI 表与内存映射、GOP | U-Boot 作为 BL33（或 Coreboot+Depthcharge）；负责块设备枚举、DT 准备、显示初始化、EFI 环境搭建 | 真板 EFI 环境由 U-Boot 的 EFI loader 子系统提供，不是完整 EDK2；协议覆盖度以板级 U-Boot 版本/配置为准 |
| 镜像发现与加载 | EDK2 BDS 按 ESP 默认回退路径 `\EFI\BOOT\BOOTAA64.EFI` 查找；PE/COFF 解析、按重定位表加载 | A：U-Boot `bootefi` 从 FAT/ESP 加载 EFI 应用（可显式路径，也可走 bootmgr 启动项）；B：U-Boot `bootm` 加载裸镜像到固定地址跳转 | 路径 A 镜像仍需合规 PE/COFF；路径 B 需要 ELF/raw 镜像与约定的加载地址、启动参数块 |
| 硬件描述与显示 | 硬件信息来自 QEMU 生成的 DTB/ACPI；GOP 由 EDK2 + 显示设备（仓库用 `-device ramfb`，`README.md:79`）提供 | 硬件信息以板级 DT 为主（U-Boot `bootefi <image> [fdt_addr]`，DT 取自 `fdt_addr` 环境变量或 `fdtcontroladdr`，出处附录 B [7]）；显示由 U-Boot video 栈初始化的 simple framebuffer 包装成 GOP（出处附录 B [9]） | 路径 A 下镜像不能再假设设备地址硬编码自 QEMU DT；显示能力取决于板级 U-Boot 是否初始化 VOP/显示通路 |
| 交接状态 | EFI 应用进入时跑在固件 EL1 环境；`ExitBootServices` 后 BootServices 不可再调用，内存映射按 MapKey 锁定，中断控制器保持固件初始化状态 | 同 UEFI 语义：`ExitBootServices` 后 BS 服务不可用；若走 B 子路径，跳转时 x0 等寄存器按引导器约定（Linux 约定 x0=DTB 指针）传递 | 镜像必须在 ExitBootServices 之前取到 memory map、GOP、DP 等所需信息；接管中断前 GIC 仍处于固件配置状态 |

### 1.3 现有 `BOOTAA64.EFI` 在真板被 U-Boot EFI 加载器加载的前提条件

1. **EFI System Partition 与文件路径**
   镜像须放在被 U-Boot 识别为可引导的 FAT 分区上；EFI 规范默认回退路径为 `\EFI\BOOT\BOOTAA64.EFI`（仓库 QEMU 现状同路径，`README.md:90`）。也可在 U-Boot 命令行显式加载：`load mmc 0:1 $kernel_addr_r /EFI/.../xxx.efi` 后 `bootefi $kernel_addr_r $fdt_addr_r`（出处附录 B [7]）。分区类型、FAT 格式、路径大小写是否被板级 U-Boot 接受，**未确认（缺目标板 U-Boot 板级配置与存储布局）**。

2. **PE/COFF 头与重定位**
   镜像必须是合规的 aarch64 PE/COFF EFI 应用：PE optional header 合法（`magic=0x20b`）、Subsystem = EFI Application（10）、节表/重定位表齐全、按 `SizeOfImage` 补齐。仓库现状用 gnu-efi `crt0-efi-aarch64.S` 自带 PE 头 + `tools/pe_flatten_aarch64.py` 平整产出；构建开关 `--defsym=EFI_SUBSYSTEM=10`（`kernel/arch/aarch64/m1/Makefile:10-13`）。历史踩坑记录：binutils `objcopy --output-target=efi-app-aarch64` 曾产出缺 optional header、subsystem=0 的不合规镜像（`README.md:34-37, 66-74`）。U-Boot EFI loader 解析 PE 并按 `.reloc` 重定位；**PE 合规性与板级无关，但容错更差的固件可能比 EDK2 更早拒绝不合规镜像**。

3. **U-Boot 编译开关**
   加载 EFI 应用需要 `CONFIG_EFI_LOADER=y` 与 `CONFIG_CMD_BOOTEFI=y`；要从块设备附加存储还需 `CONFIG_BLK`、`CONFIG_PARTITIONS`（出处附录 B [6]）。目标板 U-Boot defconfig 是否打开这些开关，**未确认（缺目标板 U-Boot defconfig/.config）**。

4. **GOP 由谁提供**
   QEMU 链 GOP 来自 EDK2（仓库配 `ramfb`）。真板 U-Boot 链下，GOP 对象由 U-Boot video 子系统初始化的帧缓冲包装而来（U-Boot `lib/efi_loader/efi_gop.c`，出处附录 B [9]）：前提是板级 U-Boot 初始化了显示通路（VOP/HDMI/eDP 等）并使能 GOP 相关选项。若显示未初始化或 GOP 未暴露，应用侧 `LocateProtocol(GOP)` 会失败——仓库代码已有失败分支：`[M0] no GOP`（`m0/hepu_m0_a64.c:53-58`）。真板 U-Boot 是否提供可用 GOP、给出哪种像素格式，**未确认（缺目标板 U-Boot video 配置与显示通路资料）**。

5. **ExitBootServices 之后的内存与中断状态**
   `ExitBootServices` 成功后：BootServices 句柄/服务不可再调用；内存映射以 MapKey 锁定；此后内存所有权交给镜像。中断控制器保持固件（U-Boot/BL31）初始化后的状态，镜像在自行配置 GIC 之前不应假设中断已按自己的意图打开；仓库 M1 现状尚未开中断（`README.md:95`）。

6. **BL31 提供的 PSCI conduit：SMC 还是 HVC**
   - QEMU virt 现状（无 EL3）：HVC。依据：`arch_port_aarch64.c:39` 为 `hvc #0`，QEMU 实测 PSCI 关机后退出码 0（`README.md:28, 110`）。
   - RK3588S 真板：BL31 运行在 EL3，PSCI 经安全监控层调用；TF-A 平台默认 SMC 调用约定，Linux PSCI 绑定的 `method` 取值为 `smc` 或 `hvc`（出处附录 B [3]）。**目标板实际 conduit 未确认（缺目标板 BL31 构建配置与板级 DT `psci` 节点的 `method` 属性）**，预期为 SMC。
   - 风险：conduit 选错（在只有 EL3 监控的环境发 HVC，或在不截获 SMC 的环境发 SMC）会直接陷入异常/挂死，且早期无串口兜底。

### 1.4 本节结论

路径 A 下，现有 EFI 应用形态**具备被 U-Boot EFI 加载器加载的可能性**，但前提是板级 U-Boot 打开 EFI loader/bootefi、镜像 PE 合规、ESP 路径正确、显示通路可用；其中 GOP 覆盖度与 PSCI conduit 是两项必须在拿到板级资料后先核实的不确定点。

---

## 2. 代码级差异清单

> 基准文件：`kernel/arch/aarch64/arch_port_aarch64.c`（42 行）；同形态副本见 `m1/m1.c`、`m0/hepu_m0_a64.c`。
> 判定分三档：**必须改** / **不需要改** / **待确认**。

### 2.1 串口：PL011 → DesignWare 8250 类 UART

| 项 | 内容 |
|---|---|
| 现状（代码位置 + 常量） | `arch_port_aarch64.c:9` `#define PL011_BASE 0x09000000UL`；`:10` DR 偏移 `0x00`；`:11` FR 偏移 `0x18`；`:12` `FR_TXFF` = bit5；`:22-26` `pl011_putc` 轮询 FR 的 TXFF 位后写 DR。同常量副本：`m1/m1.c:19-21`、`m0/hepu_m0_a64.c:13-15`。寄存器模型为 ARM PL011（AMBA UART）。 |
| 真板对应物 | RK3588S 为 DesignWare APB UART（`snps,dw-apb-uart`，8250 类），`reg-shift=<2>`、`reg-io-width=<4>`（出处附录 B [2]）。实例地址（v6.12 `rk3588-base.dtsi`）：uart0 `0xfd890000`（SPI 331）、uart1 `0xfeb40000`（SPI 332）、**uart2 `0xfeb50000`（SPI 333）**、uart3 `0xfeb60000`（SPI 334）、uart4 `0xfeb70000`（SPI 335）、uart5 `0xfeb80000`、uart6 `0xfeb90000`、uart7 `0xfeba0000`、uart8 `0xfebb0000`、uart9 `0xfebc0000`。时钟来自 CRU 的 `SCLK_UARTx`（波特率时钟）与 `PCLK_UARTx`（APB 时钟）。常见调试控制台为 uart2：ROCK 5B `stdout-path = "serial2:1500000n8"`（出处附录 B [10]）；Orange Pi 5 Pro 同样继承固件 UART2 控制台（出处附录 B [11]）。 |
| 判定 | **必须改**。PL011 的 DR/FR 寄存器布局与 8250 类 THR/LSR 不同；基址、寄存器移位宽度、时钟/分频、引脚复用（pinctrl）都要换成板级配置。 |
| 待确认 | 目标板实际引出哪一个 UART 实例与引脚复用（缺目标板原理图/板级 DT）；UART 输入时钟频率与分频系数（缺 CRU 时钟树配置）；调试串口波特率（示例 1500000，以板级固件/DT 为准）。 |

### 2.2 PSCI 调用：HVC → 预期 SMC

| 项 | 内容 |
|---|---|
| 现状（代码位置 + 常量） | `arch_port_aarch64.c:33-41` `arch_poweroff`：`w0 = 0x8400_0008`（PSCI SYSTEM_OFF），`:39` `hvc #0`；`:35` 注释已注明「QEMU virt 走 hvc，真板可能 smc」。副本：`m1/m1.c:314-318`、`m0/hepu_m0_a64.c:39-46`。现状只实现 SYSTEM_OFF，未实现 CPU_ON / CPU_SUSPEND / SYSTEM_RESET。 |
| 真板对应物 | BL31 提供 PSCI；标准功能号：SYSTEM_OFF `0x8400_0008`、CPU_ON `0x8400_0003`（ARM DEN0022 PSCI 规范，出处附录 B [12]；Linux PSCI 绑定附录 B [3]）。调用指令预期为 `smc #0`。 |
| 判定 | **必须改**：调用指令 HVC→SMC（建议做成可配置/双态探测，保留 QEMU 路径）。功能号本身**不需要改**。CPU_ON 属于现状未实现的新增能力，用于多核启动（见 2.6、第 3 节）。 |
| 待确认 | 目标板 BL31 实际 conduit（缺 BL31 配置/DT `psci` 节点 `method`）；是否存在厂商自定义的复位/休眠调用号。 |

### 2.3 GOP / 帧缓冲假设

| 项 | 内容 |
|---|---|
| 现状（代码位置 + 常量） | `m0/hepu_m0_a64.c:49-82`：`LocateProtocol(EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID)` 取 `FrameBufferBase` 与 `PixelsPerScanLine`，按 32bpp 直接写帧缓冲；`:66-70` 仅接受 BGR/RGB 32bpp，其他像素格式跳过填色。`m1/m1.c` 当前未使用 GOP。QEMU 链 GOP 来自 EDK2 + `ramfb`（`README.md:79`）。 |
| 真板对应物 | U-Boot EFI 链下 GOP 由 U-Boot video 栈的 simple framebuffer 包装（附录 B [9]）；分辨率、像素格式、stride、帧缓冲物理地址由固件 `Mode->Info` 给出，未必是 32bpp，stride 未必等于水平像素数；帧缓冲位于 DRAM 内，页表需给正确的内存属性。 |
| 判定 | **协议层不需要改**（仍走 EFI GOP `LocateProtocol`）；**必须改/待确认**：像素格式分支（现状只认两种 32bpp）、stride 与分辨率适配、帧缓冲内存映射属性。路径 B 下没有 GOP，需自行从 DT simplefb 或固件 fb 描述块取基址。 |
| 待确认 | 目标板 U-Boot 暴露的 GOP 模式列表、像素格式、fb 地址与大小（缺板级 U-Boot video 初始化资料）。 |

### 2.4 定时器（ARM generic timer）

| 项 | 内容 |
|---|---|
| 现状（代码位置 + 常量） | `arch_port_aarch64.c:14-20` `arch_ticks()` 读 `CNTVCT_EL0`；`:16` 注释「QEMU 默认 ~62.5MHz 或 24MHz，取决于板；单调即可」——代码不依赖具体频率。未开定时器中断（`README.md:95`）。 |
| 真板对应物 | ARMv8 通用定时器；RK3588 DT timer 节点 PPI：sec-phys 13、phys 14、**virt 11**、hyp-phys 10、hyp-virt 12（出处附录 B [2]）；参考时钟 `xin24m` = 24MHz 固定时钟；`CNTFREQ_EL0` 由固件写入。 |
| 判定 | **不需要改**：`CNTVCT_EL0` 读取是架构通用行为，与板级无关。 |
| 待确认 | 真板 `CNTFREQ_EL0` 实际值（固件设置，代码未读该寄存器校验）；将来开虚拟定时器中断时，PPI 号以板级 DT 为准（virt = 11）。 |

### 2.5 内存基址与可用 RAM 区间

| 项 | 内容 |
|---|---|
| 现状（代码位置 + 常量） | `m1/m1.c:337` 低 RAM 探针硬编码 `0x4000_0000`；`:179-202` 按 EFI `GetMemoryMap` 描述符映射 RAM；裸机链接基址 `m0/bare/m0.ld:5` = `0x4000_0000`。QEMU virt RAM 起于 `0x4000_0000`（出处附录 B [1]）。 |
| 真板对应物 | RK3588 系列 DRAM 通常映射在物理地址 `0x0` 起，容量与可用区间由板级 DT `memory` 节点定义；需扣除 BL31/OP-TEE/固件保留段与 TrustZone 安全内存。Datasheet 指出外部 DDR 空间可划分为 16 个可编程安全分区，片上 SRAM 部分仅安全态可访问（出处附录 B [13]）。 |
| 判定 | **必须改**：不能把 `0x4000_0000` 当作低 RAM 锚点硬编码。EFI 路径下沿用 `GetMemoryMap` 描述符建表的现状方向正确（`m1/m1.c:220-262`）；裸机路径必须自行解析 DT `memory` / `reserved-memory` 节点。 |
| 待确认 | 目标板 DRAM 容量、保留区/安全内存具体布局（缺板级 DT `memory`/`reserved-memory` 节点与 BL31 内存布局表）。 |

### 2.6 异常向量与 GIC 初始化假设

| 项 | 内容 |
|---|---|
| 现状（代码位置 + 常量） | `m1/m1.c:48-87` `vector_table`（2KB 对齐，每槽 128 字节）；`:388-395` 内联汇编 `adrp/add` 取址后写 `VBAR_EL1`。设备页：`:207` GICD `0x0800_0000`、`:208` GICC `0x0801_0000`（QEMU virt GICv2 MMIO 地址）、`:209` PL011 `0x0900_0000`。GIC 版本判定、中断使能/EOI 尚未实现（`README.md:95`，M1d-2 计划项）。 |
| 真板对应物 | GICv3：GICD `0xfe60_0000`（长度 `0x10000`）、GICR `0xfe68_0000`（长度 `0x100000`），`compatible = "arm,gic-v3"`；MSI 别名 `0xfe61_0000`，`mbi-ranges = <424 56>`，带 `msi-controller`（出处附录 B [2]）。GICv3 没有 v2 传统 MMIO CPU 接口（GICC），CPU 接口走 Redistributor + ICC_* 系统寄存器。SPI 中断号从 32 起算。 |
| 判定 | **必须改**：GIC 基址、GIC 类型（v2 MMIO CPU interface → v3 GICR/系统寄存器）、中断号映射；设备页映射范围。**不需要改**：VBAR 安装机制与向量槽位布局（架构通用）。 |
| 待确认 | 目标板 GIC 是否为完整 v3 实例、ITS 寄存器细节（以板级 DT 为准）。 |

### 2.7 判定汇总

- **必须改**：串口后端（2.1）、PSCI 调用指令（2.2）、GIC 驱动与地址（2.6）、内存锚点/保留区处理（2.5）。
- **不需要改**：通用定时器计数读取（2.4）、GOP 协议获取帧缓冲的流程（2.3 协议层）、VBAR 向量安装机制（2.6 机制层）、PSCI 标准功能号（2.2）。
- **待确认**：目标板 UART 实例/引脚/时钟分频、PSCI conduit、GOP 模式与像素格式、CNTFREQ 值、DRAM 保留/安全内存布局、GIC v3 实例细节。

---

## 3. QEMU 无法验证的部分

> 标注口径：**QEMU 已验** = 仓库代码已在本机 QEMU `virt` 上跑过并留有串口/退出码证据；**QEMU 未验** = 代码路径未执行，或 QEMU 模型本身覆盖不到。本机 QEMU 版本 6.2，`qemu-system-aarch64 -M virt`（`README.md:44`）。

### 3.1 GICv3 ITS / LPI 与 MSI —— **QEMU 未验**

- 现状：仓库只映射了 v2 GICD/GICC 地址（`m1/m1.c:207-208`），未实现 GIC 版本判定，未配置 ITS，未收发任何 LPI/MSI。
- QEMU 覆盖情况：`virt` 机器可选 GICv3 + ITS（`msi=its`），但 **TCG 模式下 ITS 未建模**（出处附录 B [14]）；仓库运行环境为 TCG 软件模拟，不具备完整 ITS/LPI 注入条件。
- 为什么覆盖不到：LPI 由 ITS 配置表（DeviceID/EventID → LPI 映射）触发，QEMU TCG 不模拟该转换；设备 MSI-X 映射、ITS 命令队列、LPI 优先级与 pending 表均无实测路径。真板 RK3588 GICv3 带 msi-controller（附录 B [2]），PCIe/网卡等设备走 MSI。

### 3.2 Cache 一致性与内存属性 —— **部分已验 / 未验**

- **已验部分**：单核下 Device nGnRE 与 Normal WB 的页属性区分、TLB 切换后存活（`README.md:116, 137-138`；`m1/m1.c:108-112` 属性编码）。
- **未验部分**：多核与外设 DMA 场景下的 cache 一致性（inner/outer shareability、cache 维护指令序列、可缓存/不可缓存边界窗口）；真实片上外设 MMIO 的采样属性与 QEMU 模型的差异。
- 为什么覆盖不到：TCG 不模拟真实 cache 层次与一致性故障，单核直写 MMIO 不暴露 coherency 问题；shareability 域行为在虚拟模型里被简化。

### 3.3 DMA 与 IOMMU —— **QEMU 未验**

- 现状：仓库 ARM64 侧未接任何 DMA 设备，未实现 SMMU/IOMMU 驱动。
- QEMU 覆盖情况：`virt` 可选整机 SMMUv3（`-machine iommu=smmuv3`）与用户态 `arm-smmuv3`（附录 B [1]），但仓库未启用、未接线。
- 真板关注点：RK3588 板上有 mmu600 PCIe SMMUv3（`0xfcb00000`，出处附录 B [2]），GPU/媒体/PCIe 等 master 经 IOMMU 做阶梯地址转换；DMA 缓冲区属性、映射权限、streamID 映射均无 QEMU 对照。

### 3.4 真板 DDR 训练后的实际内存布局（保留区 / TrustZone 安全内存）—— **QEMU 未验**

- 现状：`m1/m1.c` 按 EFI 内存描述符映射；探针锚点 `0x4000_0000`（`:337`）。
- QEMU 覆盖情况：RAM 由 `-m` 给定，连续、从 `0x4000_0000` 起，无训练过程，无固件/安全世界占用段，无安全分区。
- 真板关注点：可用 DRAM 需扣除 BL31、OP-TEE（BL32）、固件预留、 TrustZone 安全内存分区；布局随 DDR 容量、固件版本变化（Datasheet：DDR 可分 16 安全分区，附录 B [13]）。EFI 路径下可从内存描述符规避，裸机路径必须解析 DT 保留段。

### 3.5 电源域与 DVFS —— **QEMU 未验**

- 现状：仅 PSCI SYSTEM_OFF，无休眠/调压/频率切换代码。
- QEMU 覆盖情况：`virt` 无真实电源域与调压模型，CPU 频率为恒定模拟；无温度/功耗域。
- 真板关注点：RK3588 大小核（A55 / A76）电源域、DVFS（SCMI/regulator 链路，DT 中 `scmi_clk`、`cpu-supply` 引用，附录 B [2, 10]）、CPU 空闲态（PSCI CPU_SUSPEND）、温度保护。

### 3.6 多核启动（PSCI CPU_ON 与 SMP 拓扑）—— **QEMU 未验**

- 现状：只在单核运行；PSCI 只发过 SYSTEM_OFF，从未调用 CPU_ON（`arch_port_aarch64.c:33-41`、`m1/m1.c:314-318`）。
- QEMU 覆盖情况：`virt` 可配多 CPU 且 PSCI 支持 CPU_ON（QEMU 截获），但仓库代码未执行 secondary 上电路径，因此该路径在 QEMU 上同样**未验**。
- 真板关注点：8 核大小核拓扑（DT `cpus`/`cpu-map`，CPU 节点 reg 亲和性 `0x0/0x100/0x200/0x300/0x400/0x500/0x600/0x700`，`enable-method = "psci"`，附录 B [2]）；CPU_ON 目标亲和性号、secondary 入口约定、secondary 栈与异常向量、簇/核层级电源状态。

---

## 4. 两条路径对比

### 4.1 路径 A：继续 UEFI（U-Boot 提供 EFI 环境，复用现有 `BOOTAA64.EFI` / `M1.c` 形态）

**前置条件**

- 目标板 U-Boot 使能 `CONFIG_EFI_LOADER`、`CONFIG_CMD_BOOTEFI`、块设备/FAT 分区支持（附录 B [6]）；
- 镜像放入 FAT 分区 ESP，路径 `\EFI\BOOT\BOOTAA64.EFI`（或 `bootefi` 显式路径）；
- 板级 U-Boot 初始化显示通路并暴露 GOP（先跑串口文本可不强依赖 GOP）；
- PE/COFF 镜像合规（沿用 gnu-efi crt0 + `pe_flatten_aarch64.py` 流程，附录 A）；
- DT/ACPI 由 U-Boot 传递，镜像按固件给出的内存映射与设备信息工作。

**要新增 / 改造的代码面**

- 串口后端换 DW8250 类驱动（基址/移位/宽度可配置），保留 PL011 路径供 QEMU 回归；
- PSCI conduit 切 SMC（或 HVC/SMC 可配置）；
- GICv3 驱动（GICD + GICR，替代 v2 GICC MMIO）；
- GOP 像素格式 / stride / 分辨率适配，帧缓冲内存属性；
- 严守 EFI 调用边界：ExitBootServices 之前取齐 memory map / GOP / 设备路径，之后不碰 BootServices。

**验证点**

- U-Boot `bootefi` 加载镜像，进入 `efi_main`，串口出现早期打点；
- 内存描述符打印完整，RAM 写读回 OK，未触碰保留段；
- GOP 枚举成功（或明确降级为纯串口）；
- PSCI 关机能回到固件/U-Boot。

**主要风险**

- 不同板级 U-Boot 配置差异大（EFI_LOADER / GOP / ESP 识别度）；
- U-Boot EFI 环境的内存 map、固件保留段与 EDK2 不同；
- GOP 实现完整度（直写 fb vs Blt）随 U-Boot 版本变化；
- PE 合规问题在容错更差的固件上更早暴露。

### 4.2 路径 B：绕开 UEFI，BL31 之后直接接管（裸机，BL31/U-Boot 直接跳转镜像）

**前置条件**

- 引导器能把镜像载入约定物理地址并跳转（U-Boot `bootm`/自制 stub，或作为 TF-A BL33 载荷）；
- 启动参数传递约定明确：DT 指针（x0，Linux 约定）或自定义信息块；
- 镜像自包含：自行初始化页表、串口、GIC、内存区间，不依赖任何 EFI BS 服务与 GOP。

**要新增 / 改造的代码面**

- 裸机入口（参照 `m0/bare/boot.S:1-9`、`m0/bare/m0.ld`）：设栈、判定异常级别；
- 内存发现：解析 DT `memory` / `reserved-memory`（或板级硬编码表）；
- 完整早期初始化：DW8250 串口、GICv3、定时器、cache/MMU 使能；
- 帧缓冲来源：解析 DT simplefb 或固件 fb 描述块；无 GOP 协议；
- PSCI SMC 关机 / CPU_ON；SMP secondary 上电路径与拓扑解析。

**验证点**

- 跳转后串口最早打点出现；
- 内存区间探明且可用；
- GIC 中断可触发与 EOI；
- secondary CPU 被 CPU_ON 拉起并在指定入口打点。

**主要风险**

- 无固件兜底：任何早期初始化错误直接挂死，调试入口只剩串口/JTAG；
- 板级信息（DT、保留内存、时钟树）全部要镜像自己解析；
- 与板级引导镜像打包格式（idbloader / itb / raw）耦合；
- QEMU 可对照的裸机路径只有 M0 最小镜像（无 MMU、无中断、单核，`README.md:8-21`）。

### 4.3 对比表

| 维度 | 路径 A：U-Boot EFI 加载 | 路径 B：BL31 后裸机接管 |
|---|---|---|
| 前置条件 | 板级 U-Boot 开 EFI loader/bootefi；ESP 可用；显示可选 | 引导器可跳转裸镜像；启动参数约定自定 |
| 复用现状 | 直接复用 M0/M1 EFI 应用形态、GetMemoryMap、GOP 流程 | 仅复用 M0 裸机入口骨架；早期初始化基本新写 |
| 新增代码面 | 串口后端、PSCI conduit、GICv3、GOP 适配 | 串口、PSCI、GICv3、内存发现、DT 解析、fb 解析、SMP 全部自包含 |
| 固件兜底 | DDR 训练、DT、fb、保留内存由固件提供 | 镜像自行处理，固件只留 BL31 服务 |
| 调试性 | 串口 + EFI 环境错误信息相对可观测 | 早期挂死概率高，依赖串口/JTAG |
| 主要风险 | 板级 U-Boot 配置/协议覆盖度差异 | 板级信息解析、打包耦合、无兜底 |

### 4.4 推荐结论及依据

**推荐先走路径 A**（UEFI over U-Boot EFI loader），路径 B 作为 A 跑通、板级信息齐备后的备选。

依据：

1. 现有 M0/M1 产物就是 EFI 应用形态，`GetMemoryMap`、GOP、页表接管、ExitBootServices 边界已在 QEMU EDK2 上跑通（`README.md:49-90, 172-214`），路径 A 把 DDR 训练、设备树、帧缓冲、保留内存交给固件，镜像只需替换板级硬件后端；
2. 路径 B 把上述早期负担全部上移到镜像自身，且 QEMU 上没有对应的裸机 MMU/中断/SMP 对照基线，调试成本更高；
3. 两条路径的共同硬前提一致：先改串口后端、把 PSCI conduit 做成可配置、实现 GICv3 驱动；这些工作在 A 路径投入后可被 B 路径复用。

---

## 5. 可执行的下一步（最小可跑通顺序）

> 按依赖关系排列；每步写出通过判据（看到什么串口输出 / 什么现象即算通过）。不含工期估算。

1. **板级信息收集（不写代码）**
   拿到目标板原理图、板级 DT、U-Boot defconfig/`.config`、BL31 版本与固件内存布局；确认调试 UART 实例与地址、PSCI `method`、DRAM 区间与保留段、显示输出通路。
   **通过判据**：板级信息表填齐 UART 地址/波特率/引脚、PSCI conduit、memory 区间、显示 fb 来源；缺失项在第 7 节登记。

2. **串口后端抽象（板级分支，不动 QEMU 路径）**
   在 arch_port 层增加 DW8250 类后端（基址、`reg-shift`、寄存器宽度可配），先实现轮询输出；PL011 路径保留供 QEMU 回归。
   **通过判据**：QEMU 上 PL011 路径输出与现状一致；（有硬件后）真板串口看到第一行早期启动打点。

3. **PSCI conduit 可配置**
   SYSTEM_OFF 调用支持 HVC/SMC 选择（编译开关或运行期探测），功能号沿用 `0x8400_0008`。
   **通过判据**：QEMU 上仍能 PSCI 关机且退出码 0；真板调用后回到固件/整机下电。

4. **EFI 镜像入 ESP，由 `bootefi` 显式加载**
   用现有构建产物放到 FAT 分区 `\EFI\BOOT\BOOTAA64.EFI`；先在 U-Boot 命令行 `bootefi` 手动加载，不接自动启动项。
   **通过判据**：U-Boot 报告镜像已加载，进入 `efi_main`，串口看到 CurrentEL / 固件寄存器一类早期打印（对照 `m1/m1.c:354-370` 输出形态）。

5. **内存映射核对**
   EFI 路径沿用 `GetMemoryMap`；打印描述符清单，核对真板 map 与预期区间，排除保留/安全段后再建映射。
   **通过判据**：描述符清单打印完整；低地址与远端 conventional RAM 写读回均 OK；未访问保留/MMIO 段。

6. **GOP / 出图**
   确认 U-Boot GOP 可用；枚举模式，处理像素格式与 stride，做纯色填色验证。
   **通过判据**：串口打印 GOP 分辨率；画面出现纯色带（对照 M0 两色带形态，`README.md:56-58`）；无 GOP 时明确降级为纯串口文本继续推进。

7. **GICv3 接管**
   实现 GICD/GICR 初始化，接入虚拟定时器 PPI（DT virt = 11）中断计数，EOI 后重 arm。
   **通过判据**：中断计数递增且可重入；串口无异常 hang；异常向量能打出 ESR/ELR/FAR（沿用 `m1/m1.c:89-100` 诊断形态）。

8. **多核启动**
   通过 PSCI CPU_ON 拉起 secondary，核对 MPIDR 亲和性与 DT 拓扑，secondary 设栈与向量后打点。
   **通过判据**：secondary 进入指定入口并打印核号；主核继续运行；无二次上电/亲和性错误。

9. **（路径 B 备选）裸机镜像**
   在 A 跑通、板级信息齐备后，再做 DT 解析 + 自包含早期初始化的裸机形态（内存发现、串口、GIC、定时器、fb 解析、SMP）。
   **通过判据**：脱离 EFI BS 服务后，跳转即可出串口、建映射、接中断。

---

## 6. 次选对比：Radxa Dragon Q8B

> 本节正文不超过 400 字。

**前提更正**：经核对 Radxa 官方产品页，Dragon Q8B 为 Qualcomm Snapdragon 8cx Gen 3 平台，支持标准 UEFI、内置默认设备树、可从统一 ARM ISO 启动（附录 B [15, 16]），**并非 RK3588/RK3588S**。仓库 `docs/planning/MULTIARCH_PORT_EVALUATION.md:55` 将其列入 Rockchip 类候选的描述与官方资料不符，应以官方页为准。

因此「Q8B 作为 RK3588（非 S）变体」的对比不成立：它走高通 QCOM UEFI（EDK2 系）启动链，串口与 GIC 为高通平台定义，与 RK3588S 的 DW8250 / GICv3 地址体系不同，不能复用本文第 2 节的地址结论。

若要找 RK3588（非 S）对照板，可看 Radxa ROCK 5B 等：CPU、GICv3、UART 族与 RK3588S 同源于 `rk3588-base.dtsi`（附录 B [2, 10]），RK3588S 为外设裁减版；启动链同为 BootROM → DDR → TF-A BL31 → U-Boot → EFI/内核，本文启动链与代码级结论对 RK3588 同样适用，差异仅在外设实例数量与引脚复用。

**未确认**：Q8B 的串口基址、GIC 中断号、BL31/SCMI conduit（缺高通平台手册与板级 DT）；本文不据此对 RK3588S 下任何结论。

## 7. 未确认条目汇总

| # | 条目 | 现状判定 | 缺少的资料 |
|---|---|---|---|
| 1 | 目标板调试 UART 实例、引脚复用、基址与波特率 | 待确认（2.1） | 目标板原理图、板级 DT `serial` 节点 |
| 2 | UART 输入时钟频率与分频系数 | 待确认（2.1） | CRU 时钟树配置、板级 DT `clocks` 节点 |
| 3 | BL31 PSCI conduit（SMC / HVC） | 预期 SMC，未确认（1.3-6、2.2） | 目标板 BL31 构建配置、板级 DT `psci` 节点 `method` 属性 |
| 4 | 厂商自定义 PSCI 复位/休眠调用号 | 未确认（2.2） | 目标板 TF-A/BL31 接口文档 |
| 5 | U-Boot 是否使能 `EFI_LOADER`/`CMD_BOOTEFI`/GOP/video | 未确认（1.3-3、1.3-4） | 目标板 U-Boot defconfig / `.config`、video 初始化代码 |
| 6 | ESP 分区类型、FAT 格式、路径识别 | 未确认（1.3-1） | 目标板存储布局与 U-Boot 分区识别配置 |
| 7 | GOP 模式列表、像素格式、stride、fb 地址大小 | 待确认（2.3） | 板级 U-Boot video 初始化输出、显示通路资料 |
| 8 | 真板 `CNTFREQ_EL0` 实际值 | 待确认（2.4） | 固件时钟配置；代码可在早期读 `CNTFREQ_EL0` 自证 |
| 9 | DRAM 容量、保留区 / TrustZone 安全内存布局 | 未确认（2.5、3.4） | 板级 DT `memory`/`reserved-memory` 节点、BL31 内存布局表 |
| 10 | GICv3 完整实例与 ITS 寄存器细节 | 待确认（2.6） | 板级 DT `gic` 节点、GIC TRM 对应章节 |
| 11 | Q8B（骁龙 8cx Gen3）串口/GIC/conduit | 未确认（第 6 节） | 高通平台手册、Q8B 板级 DT/UEFI 配置 |

---

## 附录 A：仓库代码出处

- `kernel/arch/aarch64/arch_port_aarch64.c`
  - `:3-4` 环境注释（目标 QEMU virt，真板只改基址/中断控制器）
  - `:9` `PL011_BASE 0x09000000UL`；`:10-12` DR/FR/TXFF 定义
  - `:14-20` `arch_ticks()` 读 `CNTVCT_EL0`；`:16` 频率注释
  - `:22-26` `pl011_putc`；`:28-31` `arch_console_puts`
  - `:33-41` `arch_poweroff`：`0x8400_0008` + `hvc #0`；`:35` conduit 注释
- `kernel/arch/aarch64/m1/m1.c`
  - `:19-21` PL011 常量副本
  - `:48-87` 异常向量表；`:89-100` 同步异常诊断打印
  - `:108-112` 页表属性编码；`:116-119` 页表静态数组
  - `:160-177` 内存类型白名单；`:179-202` 描述符建表
  - `:205-210` 设备页：GICD `0x0800_0000`、GICC `0x0801_0000`、PL011 `0x0900_0000`
  - `:220-262` `GetMemoryMap` 两遍取；`:264-287` 按 map 建表
  - `:300-312` 切 TTBR0 + TLB 刷；`:314-318` `psci_off`（`hvc #0`）
  - `:337` 低 RAM 探针 `0x4000_0000`；`:349-429` `efi_main`
- `kernel/arch/aarch64/m0/hepu_m0_a64.c`
  - `:13-15` PL011 常量；`:39-46` `psci_off`（`hvc #0`）
  - `:49-82` GOP 枚举与填色；`:53-58` `no GOP` 分支；`:66-70` 32bpp 判定
- `kernel/arch/aarch64/m0/bare/boot.S:1-9` 裸机入口（设栈 → `bare_main`）
- `kernel/arch/aarch64/m0/bare/m0.ld:5` 裸机链接基址 `0x4000_0000`
- `kernel/arch/aarch64/m1/Makefile:7-13` gnu-efi 编译/链接开关（`EFI_SUBSYSTEM=10`）；`:23` `pe_flatten_aarch64.py`
- `kernel/arch/aarch64/m0/Makefile:17,27-30` `BOOTAA64.EFI` 产物与 PE 平整步骤
- `kernel/arch/aarch64/README.md`：`:8-21` M0 裸机复现命令与输出；`:34-37, 66-74` PE 不合规踩坑；`:79` `ramfb`；`:81-90` QEMU 启动命令与 ESP 路径；`:95` 中断未开；`:116, 137-138` 内存属性结论；`:172-214` M1d-1 证据与遗留

## 附录 B：公开资料出处

1. QEMU 官方文档 — ‘virt’ generic virtual platform：RAM `0x4000_0000`、flash `0x0`、PL011、GIC/ITS、可选 SMMUv3、设备地址以 DTB 为准。<https://www.qemu.org/docs/master/system/arm/virt.html>
2. Linux v6.12 `rk3588-base.dtsi`：GICv3（GICD `0xfe600000`、GICR `0xfe680000`、MSI 别名与 ranges）、DW APB UART 实例地址与中断号、通用定时器 PPI、`xin24m` 24MHz、CPU 拓扑与 `enable-method = "psci"`、mmu600 SMMUv3。<https://raw.githubusercontent.com/torvalds/linux/v6.12/arch/arm64/boot/dts/rockchip/rk3588-base.dtsi>
3. Linux 内核 PSCI 设备树绑定：`method = "smc" | "hvc"`，功能号约定。<https://www.kernel.org/doc/Documentation/devicetree/bindings/arm/psci.txt>
4. TF-A 文档 — 7.40 Rockchip SoCs：AARCH64 启动链 `Bootrom → BL1/BL2 → BL31 → BL33 → Linux kernel`，BL33 可为 U-Boot 或 Coreboot。<https://tf-a.docs.trustedfirmware.org/en/stable/plat/rockchip.html>
5. U-Boot 文档 — ROCKCHIP：`BL31`（`rk3588_bl31_v*.elf`）、`ROCKCHIP_TPL`（ddr bin）、`evb-rk3588_defconfig`。<https://docs.u-boot.org/en/v2025.04/board/rockchip/rockchip.html>
6. U-Boot 文档 — UEFI on U-Boot：`CONFIG_CMD_BOOTEFI`、`CONFIG_EFI_LOADER`、`CONFIG_BLK`/`CONFIG_PARTITIONS`。<https://docs.u-boot.org/en/latest/develop/uefi/uefi.html>
7. U-Boot 文档 — `bootefi` 命令：`bootefi <image address> [fdt address]`；DT 取自 `fdt_addr` / `fdtcontroladdr`；硬件描述可为 ACPI 或 DT。<https://docs.u-boot.org/en/latest/usage/cmd/bootefi.html>
8. U-Boot 文档 — QEMU ARM：virt 平台 PL011 基址 `0x9000000`、ARMv8 定时器、PSCI 复位。<https://docs.u-boot.org/en/latest/board/emulation/qemu-arm.html>
9. U-Boot EFI loader GOP 实现（`efi_gop.c` 镜像）：GOP 对象由帧缓冲信息构建并暴露给 EFI 应用。<https://git.ti.com/cgit/ti-u-boot/ti-u-boot/tree/lib/efi_loader/efi_gop.c>
10. Linux v6.12 `rk3588-rock-5b.dts`：`stdout-path = "serial2:1500000n8"`，uart2 使能，CPU regulator/SCMI 时钟引用。<https://raw.githubusercontent.com/torvalds/linux/v6.12/arch/arm64/boot/dts/rockchip/rk3588-rock-5b.dts>
11. Apache NuttX 文档 — Orange Pi 5 Pro（RK3588S）：调试控制台继承固件 UART2。<https://nuttx.apache.org/docs/latest/platforms/arm64/rk3588/boards/orangepi-5-pro/index.html>
12. ARM PSCI 规范（DEN0022）：SYSTEM_OFF / CPU_ON 等标准功能号与调用约定。<https://developer.arm.com/documentation/den0022/latest>
13. Rockchip RK3588 Datasheet V1.4：DDR 可分 16 个可编程安全分区；片上 SRAM 部分仅安全态访问；TrustZone。<https://rockchip.fr/RK3588%20datasheet%20V1.4.pdf>
14. QEMU 7.1 virt 文档：GICv3 ITS 默认选中，但 **TCG 模式下 ITS 未建模**。<https://qemu.eu/doc/7.1/system/arm/virt.html>
15. Radxa 产品页 — Dragon Q8B：Snapdragon 8cx Gen 3，标准 UEFI、内置默认 DT、统一 ARM ISO 启动。<https://radxa.com/products/dragon/q8b/>
16. Radxa 文档 — Q8B BIOS：UEFI 设置入口、启动顺序、设备树加载选项。<https://docs.radxa.com/en/dragon/q8b/low-level-dev/bios>
