# arch/aarch64 —— ARM64 支持（M0 已通，M1c 已通）

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

## 还没做的（M1 起点）

- 从 EFI 环境下降到 EL1、建页表和异常向量（现在仍跑在固件的 EL1 环境里，没换栈/没关 MMU 接管）
- GIC 初始化、定时器中断（CNTV 已在用，但没开中断）
- 接入复用层：把 `shared/desktop`、`gfx64` 的绘制基元接到 GOP 帧缓冲上，看能不能出界面

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

**下一步：**
- 把 identity map 从白名单 2MB 扩到全 RAM（按固件内存地图补块）；
- GIC 初始化 + 定时器中断（CNTV），走通 el1_irq 向量；
- 退出 BootServices、切自己的栈与页表，进入正式内核入口。

**复现命令：** 见本文档 M0 段（`-cpu max` + `-bios QEMU_EFI.fd` + `-drive if=virtio` + `-device ramfb`），
`kernel/arch/aarch64/m1` 目录直接 `make`，产物 `M1.EFI` 拷成 `EFI/BOOT/BOOTAA64.EFI`。
