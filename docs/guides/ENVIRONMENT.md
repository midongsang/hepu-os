---
title: ENVIRONMENT
status: active
sys_version: 0.54.6
updated: 2026-11-01
owner: guides
source_of_truth: null
supersedes: null
---

> ⚠️ **版本说明**：sys_version 已对齐 0.54.6，但正文口径仍为 v0.13.x ~ v0.33.x 设计稿。源码事实请以 `kernel/arch/x86_64/Makefile` 实际编入为准。


# HepuOS 开发环境维护指南

## 环境问题诊断

### 问题1：QEMU频繁丢失
**原因**：沙箱环境每次会话重置，apt安装的系统包丢失。
**解决方案**：
1. 每次会话开始时运行 `source env_setup.sh`
2. 将QEMU二进制文件备份到 `tools/bin/` 目录
3. 使用静态编译的QEMU（可选）

### 问题2：/tmp目录被清理
**原因**：系统定期清理/tmp目录。
**解决方案**：
1. 重要文件放在项目目录下，不要放在/tmp
2. gnu-efi等编译产物备份到 `tools/gnu-efi/`
3. 临时文件使用 `/tmp/hepu_*` 前缀，便于识别

### 问题3：apt源不稳定
**原因**：默认archive.ubuntu.com连接不稳定。
**解决方案**：
1. 使用阿里云镜像源（已配置在env_setup.sh中）
2. 备选：清华源、中科大源

### 问题4：loop设备不可用
**原因**：沙箱环境限制loop设备访问。
**解决方案**：
1. 使用mtools直接操作FAT32镜像（需安装mtools）
2. 或使用Python脚本创建FAT32镜像
3. 或在更新镜像时使用dd直接写入

### 问题5：OVMF固件无法安装
**原因**：apt源不稳定或包名变化。
**解决方案**：
1. 手动下载OVMF固件到 `tools/firmware/`
2. 或使用EDK2编译的OVMF

## 环境初始化流程

每次新会话开始时：

```bash
# 1. 进入项目目录
cd /home/user/Doubao/chats/38442532206397698/hepu_os_src/hepu-os

# 2. 运行环境初始化脚本
source env_setup.sh

# 3. 验证工具
which qemu-system-i386
ls tools/bin/

# 4. 开始开发
```

## 持久化工具清单

| 工具 | 用途 | 持久化位置 |
|------|------|-----------|
| qemu-system-i386 | BIOS引导测试 | tools/bin/ |
| qemu-system-x86_64 | UEFI引导测试 | tools/bin/ |
| OVMF_CODE.fd | UEFI固件 | tools/firmware/ |
| gnu-efi | UEFI引导器编译 | tools/gnu-efi/ |
| mtools | FAT32镜像操作 | tools/bin/ |

## 常用命令

### 编译内核
```bash
cd /home/user/Doubao/chats/38442532206397698/hepu_os_src/hepu-os
make
```

### 编译BIOS引导器
```bash
KERNEL=build_v250/hepu-kernel.bin
HDD_SECTORS=$(( ($(stat -c%s "$KERNEL") + 511) / 512 ))
gcc -m32 -DDEBUG_BOOT -DKERNEL_LBA=50 -DKERNEL_SECTORS=$HDD_SECTORS -DKERNEL_ENTRY=0x101000 -c tools/hepboot/hepboot.S -o /tmp/hepu_bios_boot/hepboot.o
ld -T tools/hepboot/hepboot.ld -m elf_i386 /tmp/hepu_bios_boot/hepboot.o -o /tmp/hepu_bios_boot/hepboot.elf
objcopy -O binary /tmp/hepu_bios_boot/hepboot.elf /tmp/hepu_bios_boot/hepboot_stage2.bin
truncate -s 8192 /tmp/hepu_bios_boot/hepboot_stage2.bin
```

### 编译UEFI引导器
```bash
cd tools/hepboot/uefi
make
```

### 测试BIOS引导
```bash
qemu-system-i386 -drive if=ide,format=raw,file=build/hepu-os-hybrid-v0.10.5.img -boot c -m 512M -serial file:/tmp/serial.log -vga std
```

### 测试UEFI引导
```bash
qemu-system-x86_64 -bios tools/firmware/OVMF_CODE.fd -drive if=ide,format=raw,file=build/hepu-os-hybrid-v0.10.5.img -m 512M
```

## 故障排查

### QEMU启动后无输出
1. 检查镜像是否正确：`ls -la build/*.img`
2. 检查引导器是否更新：`dd if=build/hepu-os-hybrid-v0.10.5.img bs=512 count=1 2>/dev/null | xxd | tail -3`
3. 检查串口输出：`cat /tmp/serial.log`

### 编译失败
1. 检查依赖：`which gcc ld objcopy`
2. 检查32位支持：`gcc -m32 -E - < /dev/null`
3. 检查gnu-efi：`ls /tmp/gnu-efi-3.0.13/inc/efi.h`

### 镜像更新后无法启动
1. 检查MBR签名：最后两字节应为0x55AA
2. 检查stage2位置：LBA34（偏移17408字节）
3. 检查内核位置：LBA50（偏移25600字节）
4. 检查内核大小：`ls -la build_v250/hepu-kernel.bin`

## 外部权威参考资源

以下资源在内核开发遇到"为什么 x86 硬件是这样"、"MSR/IO APIC 到底怎么写"、"x86-64 长模式规范细节"等问题时，**优先查这些**，不要靠猜或 Grep 旧代码：

### OSDev Wiki（首查）

| 链接 | 说明 | 赫普OS 对应落点 |
|------|------|----------------|
| [OSDev Expanded Main Page](https://wiki.osdev.org/Expanded_Main_Page) | 全站导航页，777 篇文章，按 Introduction / Design / Hardware / Reference 分类 | **开发排障第一站**：遇到新问题先在这里搜 |

#### 赫普OS 高频使用页

| 话题 | OSDev 页面 | Hepu 源码落点 |
|------|-----------|-------------|
| x86-64 长模式规范 | [x86-64](https://wiki.osdev.org/X86-64) | `multiboot_entry.S` + `irq64_simple.c`（EFER.NXE、CR0.PE、CR4.SMEP/SMAP） |
| 中断门 / IDT / 异常 | [Interrupts](https://wiki.osdev.org/Interrupts) · [Interrupt Descriptor Table](https://wiki.osdev.org/Interrupt_Descriptor_Table) | `irq64_simple.c`（64 位 IDT） |
| 8259 PIC / Local APIC / IO APIC | [PIC](https://wiki.osdev.org/PIC) · [APIC](https://wiki.osdev.org/APIC) | `irq64_simple.c`（主 IRQ 控制器）+ `apic64.c`（定时器/EOI） |
| x86-64 页表（四级） | [Paging](https://wiki.osdev.org/Paging) | `vmm64.c`（PML4/PDPT/PD/PT + 1G/2M/4K + EFER.NXE + 高半区 0xFFFF800000000000） |
| MSR（STAR/LSTAR/SFMASK/GS_BASE） | [Model Specific Registers](https://wiki.osdev.org/Model_Specific_Registers) | `syscall64_simple.c`（MSR_STAR=0xC0000081 / LSTAR=0xC0000082 / SFMASK=0xC0000084 / GS_BASE=0xC0000102） |
| System V ABI x86-64 | [System V ABI](https://wiki.osdev.org/System_V_ABI) · [Calling Conventions](https://wiki.osdev.org/Calling_Conventions) | `entry_64.S` 保存/恢复 r15→rbx；C 侧参数 rdi/rsi/rdx/r10/r8 |
| PS/2 8042 控制器（键盘 IRQ 修复） | [PS/2 Keyboard](https://wiki.osdev.org/PS/2_Keyboard) | `keyboard64.c`（读-改-写配置字节 bit0 + 0xF4 Enable Scanning） |
| PCI 配置空间 + MMIO 映射 | [PCI](https://wiki.osdev.org/PCI) | `pci64.c` + `vmm64_map_page`（ahci64/ehci64 BAR 高区 4KB 页映射） |
| VBE（VESA BIOS Extensions） | [Drawing In a Linear Framebuffer](https://wiki.osdev.org/Drawing_In_a_Linear_Framebuffer) · [VGA Hardware](https://wiki.osdev.org/VGA_Hardware) | `gfx64.c` + `vg64.c`（Stage2 设置模式 → 内核读 0x6000 处信息） |
| AHCI SATA | [AHCI](https://wiki.osdev.org/AHCI) · [SATA](https://wiki.osdev.org/SATA) | `ahci64.c`（BAR5 高区 MMIO + storage_hal 接入） |
| RTL8139 | [RTL8139](https://wiki.osdev.org/RTL8139) | `rtl8139_64.c` |
| ELF 可执行格式 | [ELF](https://wiki.osdev.org/ELF) | `elf64.c`（ET_EXEC/ET_DYN PIE 装载 + PT_INTERP 拒绝） |
| GDT 切换（32→64） | [Global Descriptor Table](https://wiki.osdev.org/Global_Descriptor_Table) | `gdt64.c` + `multiboot_entry.S` |
| CMOS / PIT 定时器 | [Programmable Interval Timer](https://wiki.osdev.org/Programmable_Interval_Timer) · [CMOS](https://wiki.osdev.org/CMOS) | `pit64.c` + `rtc64.c` |
| UEFI | [UEFI](https://wiki.osdev.org/UEFI) | Stage2 走 multiboot 协议而非 UEFI GOP；UEFI 引导路径单独 build_esp.py |
| APIC timer | [APIC Timer](https://wiki.osdev.org/APIC_timer) | `apic64.c` |
| ACPI（RSDT/MADT 定位） | [ACPI](https://wiki.osdev.org/ACPI) · [RSDP](https://wiki.osdev.org/RSDP) | `acpi.c`（高半区 phys_to_virt 访问 >=1GB ACPI 表） |
| Floppy FDC | [Floppy Disk Controller](https://wiki.osdev.org/Floppy_Disk_Controller) | 不使用（直接 IDE/AHCI） |

### QEMU 用户手册

- [QEMU Wiki](https://wiki.osdev.org/QEMU) — OSDev 上的 QEMU OSD 开发专用页（推荐）
- [QEMU Documentation](https://www.qemu.org/docs/master/) — 官方用户手册
  - `-m` 内存大小、`-vga std/none` 图形、`-monitor unix:` 监控通道、`-serial file:` 串口日志、`-netdev` 网卡模型

### Intel / AMD 官方文档

| 文档 | 说明 |
|------|------|
| [Intel 64 and IA-32 Software Developer's Manual, Volume 3](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html) | **x86-64 权威规范**：MSR 列表、CPUID leaf、异常/中断向量、页表、Ring 切换 |
| AMD64 Architecture Programmer's Manual | AMD 对应版本，MSR 定义可能略有不同 |
| [UEFI Specification (UEFI Forum)](https://uefi.org/specifications) | UEFI 引导链（Stage2/HEPBOOT 参考） |
| PCI Local Bus Specification | PCI 配置空间 / MMIO 规范 |

### QEMU monitor 常用命令（自动化测试）

```bash
# 启动时挂 unix monitor
qemu-system-x86_64 ... -monitor unix:/tmp/hepu_mon.sock,server,nowait
# 注入键盘输入
sendkey h/e/l/p/ret
# 截图
screendump /tmp/x.ppm
# 鼠标移动/点击
mouse_move dx dy
mouse_button 1
mouse_button 0
```

> ⚠️ `-vga none` 下 QEMU 没有图形控制台输入路径，`sendkey` 不会被键盘驱动收到——这不是键盘驱动 bug。
