---
title: X86_64_PROGRESS
status: frozen
sys_version: 0.13.65
updated: 2026-10-24
owner: history
source_of_truth: null
supersedes: null
---

# 赫普OS 64位内核开发进展报告

**版本**: V0.10.6  
**日期**: 2026-10-10  
**状态**: P0阶段 - **64位内核启动成功！** 所有核心模块初始化完成

## 重大突破（2026-10-10）

**64位内核完全启动成功！**

启动日志确认所有模块初始化成功：
- ✅ Stage2引导器（实模式→保护模式→长模式→跳转内核）
- ✅ 长模式激活
- ✅ CPU x86_64检测
- ✅ SCE（系统调用扩展）启用
- ✅ NXE（不可执行）启用
- ✅ 物理内存管理（64MB总内存，1MB已用，62MB空闲）
- ✅ 虚拟内存（使用引导器页表）
- ✅ GDT（使用引导器GDT）
- ✅ IDT（256个中断条目）
- ✅ 系统调用（测试成功）
- ✅ 进程管理（idle和init进程）
- ✅ IDE驱动（读取扇区0成功）
- ✅ 键盘驱动
- ✅ RTC实时时钟

## 一、已完成工作

### 1.1 64位内核核心框架
- **内核主入口**: `kernel/kernel_main_64.c`
  - 64位内核启动信息显示
  - 长模式检查
  - CPU特性检测
  - SCE（系统调用扩展）启用
  - NXE（不可执行）启用
  - EFER MSR显示
  - Multiboot信息接收

- **基础库函数**: `kernel/arch/x86_64/lib64.c`
  - VGA文本模式输出（vga_clear, vga_puts, vga_putchar, vga_put_hex）
  - 内存操作（memset, memcpy）
  - 字符串操作（strlen, strcpy, strcmp）
  - 桩函数（syscall_handler_64, irq_handler_64, process_exit_64, syscall64_init）

- **物理内存管理**: `kernel/arch/x86_64/pmm64.c`
  - 物理页面分配器（位图管理）
  - 页面分配/释放
  - 内存统计信息
  - 支持最大64TB物理内存

- **虚拟内存管理**: `kernel/arch/x86_64/vmm64.c`
  - 4级页表（PML4/PDPT/PD/PT）
  - 身份映射（前4GB）
  - 页面映射/取消映射
  - 虚拟地址转物理地址
  - 页错误处理

- **GDT/TSS初始化**: `kernel/arch/x86_64/gdt64.c`
  - 64位全局描述符表（7个条目）
  - 任务状态段（TSS）
  - 内核栈设置（RSP0）

- **中断处理**: `kernel/arch/x86_64/irq64_simple.c`
  - 64位IDT初始化（256个中断）
  - 异常处理（0-31号异常）
  - 中断计数统计
  - 中断启用/禁用

- **系统调用**: `kernel/arch/x86_64/syscall64_simple.c`
  - syscall/sysret机制
  - STAR/LSTAR/CSTAR/SFMASK MSR设置
  - 5个基础系统调用（hello/putchar/puts/exit/getpid）

- **进程管理**: `kernel/arch/x86_64/process64_simple.c`
  - 进程控制块（PCB）
  - 进程创建/终止
  - 轮询调度器框架
  - 进程列表显示

- **x86_64功能实现**: `kernel/arch/x86_64/x86_64.c`
  - CPU特性检测
  - 长模式检查
  - MSR读写
  - SCE/NXE启用
  - 页表操作框架（待完善）
  - GDT/IDT/TSS初始化框架（待完善）

- **汇编入口**: `kernel/arch/x86_64/entry_64.S`
  - Multiboot头
  - 64位入口点
  - 中断/异常入口框架
  - 系统调用入口框架
  - 上下文切换框架

### 1.2 构建系统
- **Makefile**: `kernel/arch/x86_64/Makefile`
  - 支持`make`编译64位内核
  - 使用VPATH查找源文件
  - 编译选项：-m64 -mcmodel=kernel -mno-red-zone
  - 输出：`build_x86_64/hepu-kernel64.bin`

- **链接脚本**: `kernel/arch/x86_64/x86_64.ld`
  - 内核加载地址：0x100000
  - 入口点：x86_64_entry
  - 段布局：.text, .rodata, .data, .bss

### 1.3 UEFI引导器64位支持
- **配置解析**: 添加`kernel_arch`配置项
  - `kernel_arch=64`或`kernel_arch=x86_64` → 64位模式
  - 默认32位模式

- **64位跳转函数**: `tools/hepboot/uefi/jump_kernel_64.S`
  - 保持长模式不切换
  - 64位GDT代码段（L=1）
  - 直接跳转到内核入口

- **入口地址修正**: 64位内核入口点为0x1009e5（ELF e_entry）
  - 32位内核入口：0x101000
  - 64位内核入口：0x1009e5

### 1.4 编译验证
```
$ file build_x86_64/hepu-kernel64.elf
ELF 64-bit LSB executable, x86-64, version 1 (SYSV), statically linked

$ size build_x86_64/hepu-kernel64.bin
12540 bytes (12.2 KB)

$ nm build_x86_64/hepu-kernel64.elf | grep entry
00000000001009e5 T x86_64_entry
```

## 二、内核符号表

| 符号 | 地址 | 说明 |
|------|------|------|
| x86_64_kernel_main | 0x100000 | C语言主函数 |
| vga_puts | 0x1002f0 | VGA字符串输出 |
| x86_64_entry | 0x1009e5 | 汇编入口点（ELF entry） |
| process_start_64 | 0x101ac8 | 进程启动（未使用） |
| x86_64_entry_64 | 0x101ae0 | 备用入口（未使用） |
| __bss_start | 0x104000 | BSS段起始 |

## 三、待完成工作

### P0阶段（当前）
- [x] 64位内核能编译
- [ ] 64位内核能启动（QEMU测试）
- [ ] 基础输出正常（VGA文本模式）
- [ ] 引导器正确加载64位内核

### P1阶段
- [ ] 64位内存管理（PML4页表）
- [ ] 64位中断处理（IDT）
- [ ] 64位系统调用（syscall/sysret）
- [ ] 64位进程管理
- [ ] 64位驱动适配

### P2阶段
- [ ] 64位文件系统
- [ ] 64位网络协议栈
- [ ] 64位图形子系统
- [ ] 32位兼容层（运行32位应用）

### P3阶段
- [ ] 完整64位用户空间
- [ ] 64位应用商店
- [ ] 性能优化
- [ ] 真机测试

## 四、环境问题记录

### 4.1 QEMU依赖库缺失
**问题**: 持久化的QEMU二进制文件依赖以下库，但会话切换后丢失：
- libpmem.so.1
- libfdt.so.1
- libslirp.so.0
- librdmacm.so.1
- libibverbs.so.1
- liburing.so.2
- libfuse3.so.3
- libaio.so.1
- libndctl.so.6
- libdaxctl.so.1

**已尝试**:
- 从阿里云镜像下载部分库（libpmem1, libfdt1, libaio1, liburing2, libfuse3成功）
- librdmacm1, libibverbs1, libslirp0下载失败

**解决方案**:
- 短期：等待环境恢复，或使用系统QEMU
- 长期：下载静态编译的QEMU，或使用Docker容器

### 4.2 gnu-efi丢失
**问题**: /tmp目录定期清理，导致gnu-efi丢失

**解决方案**:
- 已备份到`tools/gnu-efi/`目录
- Makefile已更新使用项目目录的gnu-efi

### 4.3 mtools丢失
**问题**: mcopy等命令丢失，无法操作FAT32镜像

**解决方案**:
- 待安装mtools包
- 或使用Python脚本操作FAT32镜像

## 五、关键技术决策

### 5.1 内核加载地址
- 32位内核：0x100000加载，入口0x101000
- 64位内核：0x100000加载，入口0x1009e5（ELF e_entry）

### 5.2 编译选项
- `-m64`: 64位代码
- `-mcmodel=kernel`: 内核代码模型（2GB地址空间）
- `-mno-red-zone`: 禁用红区（中断安全）
- `-mno-mmx -mno-sse -mno-sse2`: 禁用SIMD（简化上下文切换）
- `-ffreestanding`: 独立环境（无标准库）
- `-fno-stack-protector`: 禁用栈保护（无__stack_chk_fail）

### 5.3 引导方式
- UEFI引导：原生64位，推荐
- BIOS引导：需从实模式切换到长模式，复杂
- 当前主线：UEFI引导64位内核

## 六、下一步计划

1. **修复QEMU环境**：下载静态编译QEMU或安装依赖库
2. **测试64位内核启动**：使用QEMU+OVMF验证
3. **完善基础功能**：内存管理、中断处理、系统调用
4. **集成32位驱动**：逐步移植现有32位驱动到64位
5. **性能优化**：64位特有优化（寄存器、指令集）

## 七、文件清单

### 新增文件
- `kernel/kernel_main_64.c` - 64位内核主入口
- `kernel/arch/x86_64/lib64.c` - 64位基础库函数
- `kernel/arch/x86_64/Makefile` - 64位内核构建系统
- `kernel/arch/x86_64/x86_64.ld` - 64位内核链接脚本
- `tools/hepboot/uefi/jump_kernel_64.S` - 64位跳转函数
- `docs/X86_64_PROGRESS.md` - 本文档

### 修改文件
- `tools/hepboot/uefi/hepboot_uefi.c` - 添加kernel_arch配置和64位跳转
- `tools/hepboot/uefi/Makefile` - 添加jump_kernel_64.o和本地gnu-efi路径
- `kernel/arch/x86_64/x86_64.c` - 移除重复的x86_64_kernel_main
- `kernel/arch/x86_64/entry_64.S` - 添加Multiboot信息接收

### 已有骨架文件（待集成）
- `kernel/arch/x86_64/irq64.c` - 64位中断处理
- `kernel/arch/x86_64/process_64.c` - 64位进程管理
- `kernel/arch/x86_64/syscall64.c` - 64位系统调用
- `kernel/arch/x86_64/syscall64_compat.c` - 64位系统调用兼容层
- `kernel/arch/x86_64/x86_64.h` - 64位头文件
