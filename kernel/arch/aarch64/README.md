# arch/aarch64 —— ARM64 支持（M0 已通，M1 未开）

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
| 刷 TLB 后继续跑 | ❌ 失联 | `tlbi vmalle1` 之后没有任何输出，QEMU 不退出 |

**关键结论（三条，都是实测）：**

1. **不能改 TCR_EL1。** 本机固件用 48 位 VA（`T0SZ=16`）。写一个不同的 T0SZ（试过 25/31）会让**紧接着的取指**按新布局翻译，当场失联。正确做法是**沿用固件的 `0x580803510`**，只换 TTBR0。
2. **去掉 `tlbi` 时系统是活的。** 新表与固件表都是 identity 映射，固件 TLB 里的旧项指向同一物理页，所以不刷新也能继续跑并正常 PSCI 关机（QEMU exit=0）—— 这反过来证明"崩"只发生在 TLB 刷新之后，即**新表本身对被清掉的地址翻译有问题**。
3. **设备区必须非 cache。** 把 0-1GB 改成 Normal 会让取指读到旧 cache，比 Device 属性更早崩（`sctlr before` 之后就没输出）。

**下一步（M1b）：**
- 逐项核对 L2 block 描述符（AF/AttrIdx/SH/AP 位），确认 48 位布局下 2MB block 的可执行权限；
- 或改为**全 4KB 页**（L3）避开 block 描述符的细节；
- 之后再加 `VBAR_EL1` 异常向量、GIC 初始化与定时器中断。

**复现命令：** 见本文档 M0 段（`-cpu max` + `-bios QEMU_EFI.fd` + `-drive if=virtio` + `-device ramfb`），
`kernel/arch/aarch64/m1` 目录直接 `make`，产物 `M1.EFI` 拷成 `EFI/BOOT/BOOTAA64.EFI`。
