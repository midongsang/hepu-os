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
