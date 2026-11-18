# arch/aarch64 —— ARM64 支持（M0 已通，M1d 已通）

## 已完成（M0）

`arch_port_aarch64.c`：`arch_ticks()`（CNTVCT_EL0）、`arch_console_puts()`（PL011 @ 0x0900_0000）、
`arch_poweroff()`（PSCI SYSTEM_OFF）。与 x86 侧共用 `kernel/include/arch_port.h` 同一套声明。

`m0/bare/`：裸机最小镜像（boot.S 设栈 → bare_main → 打点 → 关机）。
验证方式（QEMU virt，无 UEFI）：

```
aarch64-linux-gnu-gcc -ffreestanding -fno-stack-protector -nostdlib -fno-builtin \
  -Ikernel/include -c kernel/arch/aarch64/m0/bare/bare_main.c -o /tmp/bare_main.o
aarch64-linux-gnu-gcc -ffreestanding -nostdlib -c kernel/arch/aarch64/m0/bare/boot.S -o /tmp/boot.o
aarch64-linux-gnu-gcc -ffreestanding -nostdlib -Ikernel/include \
  -c kernel/arch/aarch64/arch_port_aarch64.c -o /tmp/ap.o
aarch64-linux-gnu-gcc -nostdlib -static -ffreestanding -T kernel/arch/aarch64/m0/bare/m0.ld \
  -o /tmp/m0.elf /tmp/boot.o /tmp/bare_main.o /tmp/ap.o
qemu-system-aarch64 -M virt -cpu cortex-a53 -m 512 -kernel /tmp/m0.elf \
  -display none -serial file:/tmp/m0bare.log
```

实测输出：

```
[HEPU-A64] bare metal M0 alive
[HEPU-A64] cntvct_el0 = 48440
[HEPU-A64] arch_port on aarch64 works; poweroff via PSCI
```

## 未通（卡点，勿当成果）

`m0/hepu_m0_a64.c` + `m0/Makefile` 是 **UEFI 版**（拿 GOP 填色），编译能出 `BOOTAA64.EFI`，
但 **binutils 2.38 的 `objcopy --output-target=efi-app-aarch64` 生成的不是合规 EFI image**：
PE 头缺 optional header（`magic` 读到 `.text` 的 0x742e、subsystem=0），固件因此不加载、
直接掉 PXE。x86 同流程出的 `hepboot_uefi.efi` 是正常的（magic 0x20b、subsystem 0xa），
说明是 aarch64 路径的缺陷，不是脚本问题。

下一步可选：① 换 lld（`lld` 包可装，LLVM 的 PE 输出更规范）；② 自建 PE 包装脚本。
在 PE 这一关通过之前，UEFI 引导这条腿不算通。

## 环境依赖（本机已装）

`gcc-aarch64-linux-gnu`、`qemu-system-arm`（含 qemu-system-aarch64 6.2）、
`qemu-efi-aarch64`（/usr/share/qemu-efi-aarch64/QEMU_EFI.fd）、`dosfstools`/`mtools`/`gdisk`。
gnu-efi 的 aarch64 产物：`make -C tools/gnu-efi ARCH=aarch64 CC=aarch64-linux-gnu-gcc`
（生成 tools/gnu-efi/aarch64/，已入 .gitignore 的话不必提交）。

## UEFI 引导：已通（M0 完成，2026-11-18）

`m0/hepu_m0_a64.c` 现在能经 UEFI 走到我们的 EFI 应用，拿到 GOP 并整屏填色：

```
BdsDxe: starting Boot0002 "UEFI Misc Device 2" from PciRoot(0x0)/Pci(0x2,0x0)
[HEPU-A64] M0: reached UEFI entry on aarch64
[M0] GOP res = 800x600
[M0] framebuffer filled (2 bands)
[HEPU-A64] M0 complete (GOP + PL011)
[HEPU-A64] powering off via PSCI
```

实拍：`docs/assets/aarch64-m0-framebuffer.jpg`（上深蓝/下墨绿两色带 + 固件打印的一行字）。

### 这一关踩过的四个坑（按发现顺序）

1. **`objcopy --output-target=efi-app-aarch64` 产出的 PE 不合规**（binutils 2.38；缺 optional header，
   magic 读到 0x742e、subsystem=0）。→ 不用它。
2. **正确做法是"不另造 PE 头"**：gnu-efi 的 `crt0-efi-aarch64.S` 已把完整 PE 头写在 `.text.head`，
   `_start` 用 `_DYNAMIC` 自举重定位。EFI 文件 = `.text`（从偏移 0，含 PE 头）+ `.data` 按节表 VirtualAddress 落位。
   见 `tools/pe_flatten_aarch64.py`。
3. **`EFI_SUBSYSTEM` 必须用链接器 `--defsym=EFI_SUBSYSTEM=10` 传入**，否则 crt0 里的 `.short EFI_SUBSYSTEM`
   求值为 0，固件判 Unsupported。
4. **文件必须补齐到 PE 的 `SizeOfImage`**（crt0 的 `.data` VirtualSize 含 .bss，比实际数据大），
   否则固件同样报 Unsupported。`pe_flatten_aarch64.py` 已做补齐。

### QEMU 侧的两个必要参数

- **`-cpu max`**：用 `cortex-a53` 时固件枚举不到 virtio-blk（连 Boot0002 都不出现）。
- **`-device ramfb`**：没有它 GOP 拿不到（应用里会打 `[M0] no GOP`）。

启动命令：

```
qemu-system-aarch64 -M virt -cpu max -m 512 \
  -bios /usr/share/qemu-efi-aarch64/QEMU_EFI.fd \
  -drive file=<img>,format=raw,if=virtio \
  -device ramfb -vnc 127.0.0.1:5938 -display none
```

镜像：GPT + ESP(FAT32, 1MiB 起, `mkfs.vfat --offset=2048`)，放 `EFI/BOOT/BOOTAA64.EFI`。

## 还没做的（M1 起点 → 现状）

- ✅ 从 EFI 环境下降到 EL1、建页表和异常向量：M1c 建 L0→L3 的 4KB 页表 + VBAR/el1_sync，
  M1d 换成自有 bss 栈并 ExitBootServices
- ✅ GIC 初始化、定时器中断：M1d-2 实测机器为 GICv2，GICD/GICC 使能 + PPI27 + CNTV，20 次中断稳定
- ❌ 接入复用层：把 `shared/desktop`、`gfx64` 的绘制基元接到 GOP 帧缓冲上，看能不能出界面
- ❌ GICv3 路径（ICC_SRE_EL1/ICC_PMR_EL1/ICC_IGRPEN1_EL1 + redistributor）：本轮机器是 v2，未实现
- ❌ 从 M1d 的验证态交接成正式内核入口（现在只是证明"能接管"，还没有真正的 entry 分级）

## M1（进行中）：接管 MMU

目标：从 EFI 环境往自己的 EL1 内核过渡 —— 建页表、切 TTBR0_EL1、设 VBAR、打开中断。

当前 `kernel/arch/aarch64/m1/m1.c` 已做到：

| 步骤 | 状态 | 证据（串口） |
|---|---|---|
| 确认当前异常级 | ✅ | `[M1] CurrentEL = EL1` |
| 读固件 MMU 配置 | ✅ | `[fw] TCR_EL1=0x580803510 (T0SZ=16)`、`TTBR0_EL1=0x5ffff000`、`SCTLR_EL1=0xc5183d` |
| 建 48 位四级页表（L0→L1→L2 2MB block，0-1GB Device / 1-2GB Normal） | ✅ | `[M1] page tables built` |
| 写 MAIR_EL1 / TCR_EL1 / TTBR0_EL1 / SCTLR_EL1（开 MMU） | ✅ | `[dbg] mair ok / tcr ok / ttbr ok / sctlr ok` |
| 刷 TLB 后继续跑 | ✅（M1c） | M1b 之前是 `tlbi vmalle1` 后无输出；M1c 换 4KB L3 白名单映射 + VBAR 后：`[dbg] tlbi ok` → `[M1c] ALIVE after switch/tlbi` → `poweroff`，QEMU exit=0 |

**关键结论（三条，都是实测）：**

1. **不能改 TCR_EL1。** 本机固件用 48 位 VA（`T0SZ=16`）。写一个不同的 T0SZ（试过 25/31）会让**紧接着的取指**按新布局翻译，当场失联。正确做法是**沿用固件的 `0x580803510`**，只换 TTBR0。
2. **去掉 `tlbi` 时系统是活的。** 新表与固件表都是 identity 映射，固件 TLB 里的旧项指向同一物理页，所以不刷新也能继续跑并正常 PSCI 关机（QEMU exit=0）—— 这反过来证明"崩"只发生在 TLB 刷新之后，即**新表本身对被清掉的地址翻译有问题**。
3. **设备区必须非 cache。** 把 0-1GB 改成 Normal 会让取指读到旧 cache，比 Device 属性更早崩（`sctlr before` 之后就没输出）。

**M1b 已排除项：**
- **WXN 不是原因**：固件 `SCTLR_EL1=0xc5183d` 的 bit19(WXN)=1（可写页一律不可执行），
  我们已清掉 WXN 再 tlbi，仍然在 `tlbi` 之后失联（`[dbg] wxn cleared` 之后无输出）。
- **不是串口被吞**：tlbi 之后不做任何打印、直接 PSCI 关机，QEMU 仍不退出（exit=124），
  说明是真崩溃而不是"控制台静音"。
- **不是 2MB block 属性写错**：L2[1][195] 打出来是 0x58600000|0x401|0x4（valid+AF+AttrIdx1），
  与 image 地址 0x5873_6788 完全对得上。

## M1c（已通）：4KB 页（L3）最小 identity map + 异常向量接管

**结论：tlbi 之后串口继续打印，QEMU 退出码 0**（两条判据都满足）。

最终配置（`m1.c` 内 `EXP_*` 开关全默认）：

- 48 位 VA 四级表 L0→L1→L2→L3，L3 叶子 4KB 页；
- 白名单映射：运行时按 pc/sp/页表数组锚点反推的 2MB Normal 块（实测两块：image
  `0x58000000` l2=192、栈 `0x5f600000` l2=251）；UART `0x09000000` 一个 Device 4KB 页；
  GICD `0x08000000` / GICC `0x08010000` 各一个 Device 4KB 页；其余表项填 0；
- 先装 `VBAR_EL1` + el1_sync 向量（打印 ESR/ELR/FAR/EC/ISS）；
- 叶子页描述符 AF=1、AttrIdx1 Normal WB inner shareable、AP=EL1 RW、PXN/UXN=0；设备页 Attr0 nGnRE；
- TCR 不重写（沿用固件 `0x580803510`）；MAIR=`0x04FF`；SCTLR 保 M/C/I、清 WXN。

### 实验记录（假设 → 实验 → 串口/退出码）

| # | 假设 / 改动 | 配置 | 串口关键行（原文） | 退出码 |
|---|---|---|---|---|
| 0 | M1b 基线（2MB block，无 VBAR） | 旧源码 | 末行 `[dbg] tcr ok`，之后无输出 | 124 |
| A | 切表不刷 TLB（旧 TLB 下应活着） | `EXP_FLUSH=0` | `[dbg] no tlbi (baseline contrast)` → `[M1c] ALIVE after switch/tlbi, pc=0x000000005811b9e0` → `[M1c] poweroff` | 0 |
| B | 4KB 页 + `tlbi vmalle1` 热切 | 默认 | `[dbg] tlbi ok` → `[M1c] ALIVE after switch/tlbi, pc=0x000000005811ba38` → `count=0…90` → `second tlbi done` → `poweroff` | 0（复跑 B2 仍 0） |
| C | 只映射 pc 当前 4KB + UART | `EXP_MINIMAL=1` | 末行 `[dbg] about to tlbi`，无 ESR（向量表页/栈页/其余代码页均未映射，异常入口取不到指） | 124 |
| D | 单地址刷 `tlbi vaae1` | `EXP_FLUSH_KIND=1` | `[dbg] tlbi ok` → `[M1c] ALIVE after switch/tlbi, pc=0x000000005811ba40` → `poweroff` | 0 |
| E | 标准序列 M=0 → 换 TTBR → 刷 → M=1 | `EXP_MOFF=1` | `[dbg] M=0` → `tlbi ok` → `[dbg] M=1 again` → `ALIVE ...5811baf0` → `poweroff` | 0 |
| F | 切换后寄存器核对 | 默认 | `post TTBR0=0x000000005812e000 ASID=0 CnP=0`、`post TCR=0x0000000580803510 A1=0`、`post SCTLR=0x0000000000c5183d A=0 SA=1`；F 初版 `post VBAR=0x0000000000001800`（bug），F2 修后 `post VBAR=0x0000000058119800` | 0 |
| G | 故意读未映射 `0x30000000` 验证向量 | `EXP_PROBE=1` | G 初版向量槽位错位，无 ESR 直接 hang；G2 修对齐后：`[EXC] ESR_EL1=0x0000000096000006 ELR_EL1=0x000000005811b058 FAR_EL1=0x0000000030000000`，`EC=37 ISS=0x0000000000000006`（数据 abort，level 2 翻译故障） | 124（向量内 wfi 停机） |
| H | 按固件原值回写一遍 TCR | `EXP_WRITETCR=1` | `[M1c] tcr rewritten (same value)` → `ALIVE ...5811ab18` → `poweroff` | 0 |
| I | L2 直接放 2MB block 描述符（复现 M1b 形态） | `EXP_BLOCK=1` | `tlbi ok` → `ALIVE ...5811ca18` → `poweroff` | 0 |

### 本轮修掉的两个自身 bug

1. **VBAR 取址没重定位**：C 里 `&vector_table` 走 GOT，该 asm 符号的 RELATIVE 槽在 gnu-efi
   自举里没落地，读回 VBAR=`0x1800`（链接偏移）。改成内联汇编 `adrp/add` PC 相对现场算，
   读回 `0x58119800`（运行时地址）。
2. **向量槽位错位**：`b .Lv_sync` 前漏 `.align 7`，同步异常槽（偏移 0x200）落到 hang 分支，
   异常进来直接死循环、打不出 ESR。补齐对齐后 G 组正常打出 ESR/ELR/FAR。

### 结论与遗留

- 4KB L3 白名单映射 + 叶子 AF=1 + 先装 VBAR 后，热切 TTBR0 并 `tlbi vmalle1` 可稳定存活
  （B/B2/F/F2/final 五次 exit=0）。
- 隔离实验里，**单地址刷、标准 M=0 序列、同值回写 TCR、2MB block 描述符形态**各自单独都能跑过；
  M1b 的崩溃在本框架下未能由其中任一单项复现。M1b 与本版剩余差异：整 1GB Device/Normal 全量
  映射、无 VBAR、M1b 原始源码路径；如需彻底归因，下一步在本框架把全量 1GB 映射加回来对照。
- 附注：M1b 记录称固件 SCTLR bit19(WXN)=1，本轮实测 `0xc5183d` 的 bit19=0（SA=1、A=0）。

## M1d-1（已通）：GetMemoryMap 全 RAM 混合映射

**结论：按固件内存描述符建混合映射（整 2MB 用 L2 block、首尾不齐用 L3 4KB 页），tlbi 后 ALIVE，
低地址与远端 RAM 写读回均 OK，QEMU exit=0。**

### 做法与取舍

- `BootServices->GetMemoryMap` 两遍取：第一遍 `Size=0/NULL` 拿所需大小，留两个描述符余量重取；
  缓冲为静态 bss（32KB），`DescriptorSize=48`、`Version=1`、`MapKey` 留存供 M1d-3 用。
- 映射类型：接受 LoaderCode(1)/LoaderData(2)/BootServicesCode(3)/BootServicesData(4)/
  RuntimeServicesCode(5)/RuntimeServicesData(6)/Conventional(7)/ACPIReclaim(9)/ACPINvs(10)/
  Persistent(14)，按 Normal WB(Attr1) inner shareable 映射；跳过 Reserved(0)/Unusable(8)/
  MemoryMappedIO(11)/MemoryMappedIOPortSpace(12)/PalCode(13)。
  取舍理由：跳过的类型要么不可用、要么是 MMIO（设备另由 UART/GIC 4KB Device 页覆盖）；
  RuntimeServices* 先按 Normal 映射，M1d-3 退出 BootServices 后再复核其属性。
- 混合映射：整 2MB 对齐且完全被同一描述符覆盖 → L2 block；首尾不齐的 2MB → 该块内 L3 4KB 页。
  L3 表池静态 48 表（192KB bss），实测只用 14 表。
- 设备页：GICD `0x08000000`、GICC `0x08010000`、PL011 `0x09000000`，各一个 4KB Device 页。

### 证据（`/tmp/m1d-1-map.log`，exit=0）

```
[M1d] MemoryMap: descriptors=29 DescSize=48 Ver=1 Key=00000000000008fe
[M1d] mapped: blocks=244 pages=5376 RAM=509MB L3tables=14
[dbg] tlbi ok
[M1d] ALIVE after switch/tlbi, pc=0x00000000580e92a8
[M1d] low RAM  @0x0000000040000000 w0=0xa55a0001dead0001 r0=0xa55a0001dead0001
[M1d] remote RAM @0x000000005a80c000 w0=0xa55a0001dead0001 r0=0xa55a0001dead0001
[M1d] poweroff
```

描述符清单见日志 `[M1d] [n] type=… start=… pages=… [map]/[skip]`：type0 一段 3MB Reserved、
type11 两段 MMIO（`0x04000000` 64MB、`0x09010000` 4KB）按规则跳过。

### 遗留

- 未处理 `EFI_MEMORY_XP`（执行保护）属性：当前所有映射 PXN/UXN=0，后续内核代码段需要时再按属性细化。
- 509MB 已映射；未映射部分即 Reserved/MMIO，未做访问校验。

## M1d-2（已通）：GICv2 初始化 + 虚拟定时器 PPI27 中断计数

**结论：PIDR2 判定为 GICv2；使能 distributor/cpu interface、PPI27 指 CPU0，虚拟定时器 100ms 周期，
handler 读 IAR → 计数打印 → 写 EOIR → 重 arm，稳定计数 20 次后关定时器 PSCI 关机，QEMU exit=0。**

### 做法

- 版本判定：读 `GICD_PIDR2`（`0x08000FE8`）bits[7:4]，实测 `0x2b` → version=2（GICv2）；
  不假设版本。**GICv3 路径本轮未实现**：若为 v3 需走 `ICC_SRE_EL1` 使能系统寄存器接口、
  `ICC_PMR_EL1` 优先级掩码、`ICC_IGRPEN1_EL1` 组使能，外加 redistributor 探测与每 CPU 初始化；
  v2 用内存映射的 GICD/GICC 寄存器，v3 走 `ICC_*` 系统寄存器，这是两条路径的主要差异。
- GICv2：`GICD_CTLR` 使能 group0；`GICD_ISENABLER0` bit27 使能 PPI27；
  `GICD_ITARGETSR27` 指 CPU0（按字内字节 lane 读改写）；`GICC_PMR=0xFF`；`GICC_CTLR` 使能。
- 定时器：读 `CNTFRQ_EL0=62500000`；`CNTV_TVAL_EL0=+(freq/10)`（100ms）；`CNTV_CTL_EL0=1`
  （ENABLE=1、IMASK=0）；`msr daifclr, #2` 开 IRQ。
- 向量：IRQ 槽偏移 `0x280`（current EL, SP_EL1 组），桩内保存 x0–x5 → C handler → 恢复 → `eret`。
  handler 读 `GICC_IAR` 取 intid，计数并打印（前 3 次 + 每 10 次），写 `GICC_EOIR` 回写 IAR 值，
  再重 arm `CNTV_TVAL`。计数到 20 后 `CNTV_CTL=0` 关定时器、`daifset #2` 重 mask，再关机。

### 本轮修掉的自身 bug

- **TVVAL 写成负值导致中断风暴**：`CNTV_TVAL_EL0` 是有符号偏移，正值=未来比较值，负值=比较值落在
  过去、定时器立即过期。初版写 `-(freq/10)`，中断以 handler 限速反复重入、主循环被饿死，计数冲到
  2 万+ 超时 124。改正值后 20 次稳定触发、exit=0。
- **EOIR 必要性对照**（`EXP_NO_EOIR=1`）：handler 不写 EOIR 时只进 1 次中断（n=1），之后再无中断，
  主循环永久等待 → 超时 124；证明漏写 EOIR 中断不会再派发。

### 证据

主路径 `/tmp/m1d-2-final.log`，exit=0：

```
[M1d] GICD_PIDR2=0x000000000000002b TYPER=0x0000000000000008 version=2 (GICv2)
[M1d] CNTFRQ=62500000
[M1d] GICv2: D/C enabled, PPI27 -> CPU0
[M1d] timer armed, unmask IRQ
[IRQ] enter vector, n=1 intid=27 ESR=0x0000000000000000
[IRQ] enter vector, n=20 intid=27 ESR=0x0000000000000000
[M1d] IRQ count=20 main_ticks=1477386241 last intid=27
[M1d] poweroff
```

EOIR 对照 `/tmp/m1d-2-noeoir.log`，exit=124（超时）：

```
[M1d] timer armed, unmask IRQ
[IRQ] enter vector, n=1 intid=27 ESR=0x0000000000000000
（此后无新中断，主循环等待至超时）
```

### 遗留

- GICv3 路径（`ICC_SRE/PMR/IGRPEN1` + redistributor）未实现，留待机器换 v3 时补。
- 未处理中断优先级分组、spurious intid（1023）分支；定时器未读 `CNTV_CVAL` 做高精度比对。

## M1d-3（已通）：ExitBootServices + 描述符静态副本 + 自有 bss 栈切换 + GOP FB 写读回

**结论：退出 BootServices 前把 GOP framebuffer、自有栈、描述符缓冲全部映射好；`ExitBootServices`
第 1 次调用即 `Status=0`；退出后只用裸 PL011；切到自有 16KB bss 栈跑 probe（局部变量落在新栈范围
内）；帧缓冲 `0x58430000` 写 `0xff00ff` 读回一致；QEMU exit=0。**

### 做法

- **退出前补齐映射**（`SetMemoryAttributes` 退出后不可用，属性须在退出前定好）：
  - GOP：`LocateProtocol` 取 `FrameBufferBase/FrameBufferSize` 与 Mode->Info（实测 800x600，
    `PixelFormat=1` BGR 32bpp，stride=800），`map_fb_region` 按 4KB 补齐（M2 起改 Normal NC）；FB 这段在
    MemoryMap 里是 type0 Reserved（`0x58430000` 768 页），被 M1d-1 规则跳过，须显式补映射。
  - 自有栈在 bss，随 image 段被 type1/type7 覆盖；描述符副本在静态 `map_buf`（32KB bss）。
- **描述符静态副本**：`fetch_memory_map` 两遍取 MapKey/Size，描述符写进静态 bss `map_buf`，
  `map_key` 留存。EBS 之后 pool 内存不再归自己，静态 bss 保留，故退出后仍可访问副本。
- **EBS 重试**：最多 5 次；返回 `EFI_INVALID_PARAMETER(2)` 视为 MapKey 过期，重取 MemoryMap
  刷新 `map_buf`/`map_key` 再试，每次打印 Status。实测 attempt 1 即 `Status=0`，未触发重试。
- **MMIO 区间覆盖**：EBS 内部清理 PCI 时会触碰未映射的 PCIe PIO 窗口（`0x3eff0000`，64KB）与
  ECAM（`0x3f000000`，16MB）；`map_dev_range` 按 2MB Device block 覆盖
  `0x04000000–0x0A000000`、`0x0A000000–0x0C000000`、`0x0C000000–0x0E000000`、
  `0x10000000–0x40000000`；GICD/GICC/UART 再补 4KB Device 页。
- **栈切换**：`new_stack` 16KB、16 字节对齐；asm 桩在旧栈保存 x19/x20/x30，把 sp 切到新栈顶调
  `stack_probe`，再切回旧栈，旧 sp 经 x0 返回。probe 打印局部变量值、`&local`、当前 sp，校验
  落在新栈范围内；不在换栈后 `return` 到旧栈帧。

### 本轮修掉的自身 bug

- **EBS 崩在未映射 MMIO**：第一次跑 EBS 进 data abort，`EC=37 DFSC=level2 translation fault`，
  `FAR=0x3eff0012`，`ELR` 在 BS 代码 `0x5bafc758`（故障指令 `strb w1,[x0]`）。对照 QEMU virt
  内存地图确认 `0x3eff0000` 是 PCIe PIO 窗口；补 Device block 覆盖后 EBS 通过。
- **`stack_probe` 被链接器回收**：初版 `static`、只被内联汇编 `bl` 引用，C 侧不可见，被优化掉后
  `bl` 落空 PLT（符号 U）。改全局 + `__attribute__((used))` 后符号落地。
- **固件遗留定时器 pending 风暴**：装 VBAR 后固件遗留的虚拟定时器 pending 被新向量接住，handler
  无条件重 arm 形成风暴（计数冲到 1100 万，超时 124）。修复：装 VBAR 后先 `daifset #2` mask IRQ、
  `CNTV_CTL=0` 关定时器；加 `timer_armed` 标志（`gic_timer_init` arm 后置 1），handler 仅在
  armed 时重 arm，否则直接关定时器不重 arm。
- **栈桩 sp 槽位错位破坏 x19 全局基址（核心坑）**：初版桩先 `mov x19, sp`（压栈前存旧 sp=S0），
  再 `stp x19,x20,[sp,#-16]!`（压到 S0-16）；恢复时 `mov sp,x19`=S0，`ldp x19,x20,[sp],#16`
  却从 S0 读，而压栈槽在 S0-16，槽位错位 → 错误恢复 x19/x20。x19 是 efi_main 用作全局基址的
  callee-saved 寄存器，被踩成野值，后续经 x19 相对寻址读到错误的 `fb_base`（`0x5b6231f8`，
  conventional RAM，非帧缓冲），FB 校验在错误地址自洽空转；同一错位还使「returned to old sp=」
  字符串前缀丢失。修复：先压栈再 `mov x19, sp`，使恢复 sp 与压栈槽一致；修复后 `fb_base` 全程
  `0x58430000`，前缀恢复显示。

### 证据

主路径 `/tmp/m1d-3-final.log`，exit=0：

```
[M1d] GOP 800x600 fmt=1 stride=800
[M1d] FB base=0x0000000058430000 size=3145728
[M1d] FB mapped pages=768
[M1d] IRQ count=20 main_ticks=763470867 last intid=27
[M1d] about to call ExitBootServices, MapKey=0x0000000000000914
[M1d] EBS attempt 1
[M1d] ExitBootServices Status=0
[M1d] after ExitBootServices: bare PL011 only
[M1d] stack switch: old sp=0x000000005f70c9d0 new stack=0x00000000580ee200..0x00000000580f2200
[M1d] on new stack: local=0x1122334455667788 &local=0x00000000580f21f8 sp=0x00000000580f21e0
[M1d] returned to old sp=0x000000005f70c9c0
[M1d] FB px[0] @0x0000000058430000 old=0x0000000000000000 w=0x0000000000ff00ff r=0x0000000000ff00ff OK
[M1d] poweroff
```

对照与定位：

- `/tmp/m1d-3-nirq7.log`（`EXP_SKIP_IRQ=1` 跳过中断直调 EBS），exit=0：EBS Status=0、换栈
  probe OK、FB@`0x58430000` OK；用于确认缺映射与中断状态互不干扰。
- `/tmp/m1d-3-fbdiag.log`（桩修复前）：`fb_base after EBS=0x58430000` 完好，
  `fb_base after stack=0x5b6231f8` 在换栈过程中被踩，把踩栈时刻锁定到栈桩。
- 早期 EBS 崩溃日志 `/tmp/m1d-3-ebs*.log`：`EC=37 DFSC=level2 translation fault FAR=0x3eff0012`。

### 遗留

- FB 映射属性在 M2 改为 Normal NC（AttrIdx2），避免 WB 脏行未清回内存导致截图读到旧内容。
- GICv3 路径未实现（同 M1d-2 遗留）。
- 桩返回的「旧 sp」是压栈后的 sp（比入口 sp 低 16），非严格入口值；不影响换栈校验。
- 退出后未再调用任何 BootServices/Print；GOP 结构体本身退出后不可依赖，仅在退出前缓存了
  base/size。`EFI_MEMORY_XP` 属性未特殊处理。

**下一步：** 进入真正内核态——接管中断/定时器与调度，不再返回 UEFI；细化代码/数据段 PXN/UXN 权限。

**复现命令：** 见本文档 M0 段（`-cpu max` + `-bios QEMU_EFI.fd` + `-drive if=virtio` + `-device ramfb`），
`kernel/arch/aarch64/m1` 目录直接 `make`，产物 `M1.EFI` 拷成 `EFI/BOOT/BOOTAA64.EFI`。

## M2（已通）：ARM64 第一次出界面

**结论：在 M1d 已验证可写的 GOP 帧缓冲上自绘最小界面——整屏底色 + 顶部状态栏 + accent 色块 +
居中放大文字 `HEPU ARM64`；保持 9s 期间定时器中断继续计数，之后 PSCI 关机，QEMU exit=0。
`monitor screendump` 抓回 P6 PPM 800×600，转 PNG 后肉眼 + 采样核对：底色/条/块/文字均符合预期，
字形可读、未倒置错位。截图 `kernel/arch/aarch64/m1/m2-screen.png`。**

### 实测分辨率与像素格式（串口原文）

```
[M1d] GOP 800x600 fmt=1 stride=800
[M1d] FB base=0x0000000058430000 size=3145728
[M2] FB @0x0000000058430000 w=800 h=600 stride=800 fmt=1
```

- `fmt=1` = `PixelBlueGreenRedReserved8BitPerColor`，BGRX 32bpp；内存字节序 B,G,R,X，小端写
  `uint32 = 0x00RRGGBB` 即出正确颜色（截图采样 RGB 与预期一致，已自查字节序）。
- `FrameBufferSize=3145728 = 1024×768×4`，比可见 `800×600×4` 大；寻址按 `PixelsPerScanLine=800`
  作行步长，绘制全部限制在 `w=800 / h=600` 内，未越界。
- screendump 出的 PPM 实测 `P6 w=800 h=600 max=255`，与 GOP `HRes/VRes/stride` 三者一致，
  不存在按 1024×768 画导致右/下被裁的问题。

### 区块坐标 + 颜色表

颜色打包 `0x00RRGGBB`；采样值为 PPM 读出的 RGB。

| 区块 | x | y | w | h | color(0x00RRGGBB) | 采样 RGB |
|---|---|---|---|---|---|---|
| 整屏底色 | 0 | 0 | 800 | 600 | `0x141822` | (20, 24, 34) |
| 顶部状态栏 | 0 | 0 | 800 | 40 | `0x1F6FEB` | (31, 111, 235) |
| accent 色块 | 16 | 8 | 24 | 24 | `0xFF6B35` | (255, 107, 53) |
| 标题文字 | 居中 x=124 y=276 | — | 字 48×48 | — | fg `0xFFFFFF` / bg `0x141822` | 白 |

- 字模：自带 8×8 位图，仅覆盖 `H E P U A R M 6 4` 与空格；放大倍数 `scale=6`（字 48px），
  字间 `gap=8`。`HEPU ARM64` 共 10 字符，文本宽 `10×48 + 9×8 = 552`，
  `x=(800-552)/2=124`，`y=(600-48)/2=276`。
- 绘制耗时 `[M2] draw time=2 ms`（`CNTVCT_EL0` 差值按 `CNTFRQ=62500000` 换算）。

### 保持期中断与关机

```
[M2] framebuffer drawn, holding
[M2] held 9000 ms, IRQ during hold=89 total IRQ=109 vct=000000003c3167a1
[M2] poweroff
```

- 保持 9000ms，期间虚拟定时器中断 89 次（100ms 周期），累计 109；结束后关定时器、mask IRQ，
  PSCI 关机，**exit=0**。

### 本轮发现/修掉的点

- **FB 改 Normal NC**：M1d 里 FB 按 Normal WB 映射；WB 脏行若不主动清回内存，QEMU 抓屏可能读到
  旧内容。MAIR 增加 Attr2=Normal NC(`0x44`)，FB 页用 AttrIdx2 映射，规避缓存一致性问题。
- **EBS 后 GIC 被固件复位**：首次保持期 `IRQ during hold=0`（定时器没再触发）。排查后在保持段
  重新写 `GICD_CTLR=1`、`ISENABLER0` bit27、`GICC_PMR=0xFF`、`GICC_CTLR=1` 再 arm 定时器，
  保持期恢复 89 次中断。说明 ExitBootServices 后 CPU interface/优先级掩码需重新使能。
- **抓图就绪信号时机**：初版把 `framebuffer drawn, holding` 打点放在 9s 等待循环**之后**，monitor
  按这行触发抓图时画面已关机。改为先打就绪信号、再进等待循环，保证抓图落在保持窗内。

### 截图判读（`m2-screen.png`，800×600）

- 底色：左下/右上采样均为 `(20,24,34)`，整屏深底存在且颜色符合预期。
- 状态栏：`(400,20)`、`(700,30)` 均为 `(31,111,235)` 蓝条，横贯顶部 40px。
- accent：`(28,20)`、`(20,12)` 均为 `(255,107,53)` 橙块，位置 `16,8 24×24` 对得上。
- 文字：中央白色像素成 9 个列段（对应 `HEPUARM64` 九个非空格字符），每段宽 48px、字距 8px，
  首段 x=124、居中于 400；放大逐字核对为 `H E P U A R M 6 4`，可读、非色块、未倒置错位。

### 复现（含 screendump 调法）

1. `kernel/arch/aarch64/m1` 下 `make`，产物 `M1.EFI` 拷成 `EFI/BOOT/BOOTAA64.EFI` 放进 ESP。
2. 起 QEMU（前台，`-display none`，加 monitor unix socket）：

```
qemu-system-aarch64 -M virt -cpu max -m 512 \
  -bios /usr/share/qemu-efi-aarch64/QEMU_EFI.fd \
  -drive file=/tmp/m1.img,format=raw,if=virtio -device ramfb \
  -vnc 127.0.0.1:5938 -display none \
  -monitor unix:/tmp/m2-mon.sock,server,nowait \
  -serial file:/tmp/m2-run2.log
```

3. 串口出现 `[M2] framebuffer drawn, holding` 后（9s 保持窗内），连 socket 发：

```
screendump /tmp/m2.ppm
```

4. 环境无 `pnmtopng`，用 python + zlib 手写最小 P6(RGB)→PNG（IHDR 800×600、8bit 真彩、
   filter 0、zlib 压缩 IDAT），输出 `m2-screen.png`。

### 没做 / 存疑

- 字模只覆盖标题用到的字符；无字符串渲染、无滚动、无指针/输入设备。
- 单缓冲，未做 vsync/双缓冲；绘制直接写 FB，未做局部脏区刷新。
- FB 按 Normal NC 映射，整屏填充性能未优化；后续若上复杂 UI 再评估 WC/NC 取舍与缓存清理。
- GICv3 路径仍未实现（同 M1d-2 遗留）；保持期中断只做计数，未测中断嵌套/优先级分组。
- `info display` 在本 QEMU monitor 下为 `unknown command`，分辨率以 GOP Info 与 PPM 头互证为准。
