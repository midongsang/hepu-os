---
title: HEPBOOT_DESIGN
status: active
sys_version: 0.54.6
updated: 2026-11-01
owner: boot
source_of_truth: null
supersedes: null
---

> ⚠️ **版本说明**：sys_version 已对齐 0.54.6，但正文口径仍为 v0.13.x ~ v0.33.x 设计稿。源码事实请以 `kernel/arch/x86_64/Makefile` 实际编入为准。


# HepuBoot 引导系统设计文档

## 概述

HepuBoot（赫普引导）是赫普OS的自研引导加载程序，基于x86实模式汇编实现，支持MBR/ISO混合引导。HepuBoot的设计目标是：轻量、快速、安全、可扩展，同时学习GRUB等成熟引导器的优点。

## 版本历史

| 版本 | 日期 | 主要变更 |
|------|------|----------|
| v1.00 | 2026-08 | 初始版本，基础MBR引导 |
| v1.02 | 2026-09 | 重命名为HepuBoot，学习GRUB架构，三模式降级框架 |
| v1.03 | 2026-09-22 | F8引导菜单（正常/安全/恢复）、内核参数传递（hepu.safe/hepu.recovery）、安全/恢复模式实际行为（禁用USB/触屏/splash/PS2鼠标）、引导失败计数自动恢复（内核侧实现，LBA10存储，fail_count>=3自动进恢复模式） |

## GRUB优点学习与借鉴

### 1. Multiboot规范兼容
- **GRUB做法**：完整实现Multiboot规范，通过multiboot info结构传递内存映射、framebuffer、启动设备等信息
- **HepuBoot现状**：已实现基础multiboot info（内存、mmap），VBE framebuffer字段已添加但bootloader层面VBE设置有问题
- **改进计划**：完善multiboot info所有字段，确保与GRUB兼容

### 2. 模块化架构
- **GRUB做法**：核心（kernel.img）+ 动态模块（mod），文件系统驱动、图形支持、加密等作为可加载模块
- **HepuBoot现状**：单文件汇编，所有功能编译在一起
- **改进计划**：拆分核心与模块，stage1（MBR）→ stage2（核心）→ 可选模块（文件系统/图形）

### 3. 配置文件驱动
- **GRUB做法**：人类可读的grub.cfg文本配置，支持菜单、默认选项、超时、内核参数
- **HepuBoot现状**：硬编码配置，无配置文件
- **改进计划**：支持从引导分区读取hepboot.cfg，配置默认内核、超时、显示模式等

### 4. 命令行界面
- **GRUB做法**：类Bash命令行，可在引导前编辑内核参数、手动引导、调试问题
- **HepuBoot现状**：无命令行
- **改进计划**：提供最小命令行（help、boot、ls、cat、set），用于调试和恢复

### 5. 多系统引导
- **GRUB做法**：支持直接加载Linux/BSD内核，链式加载Windows等其他引导器
- **HepuBoot现状**：仅支持加载赫普OS内核
- **改进计划**：支持链式加载（chainloader），可引导其他操作系统

### 6. LBA与大磁盘支持
- **GRUB做法**：使用LBA模式，突破1024柱面限制，支持GPT分区
- **HepuBoot现状**：使用LBA（INT 13H AH=42H），仅支持MBR
- **改进计划**：添加GPT支持，UEFI支持（长期）

### 7. 图形模式支持
- **GRUB做法**：gfxpayload设置高分辨率VBE模式，通过multiboot info传递framebuffer，设置后直接进保护模式
- **HepuBoot现状**：VBE设置在SeaBIOS下有兼容性问题（VBE BIOS调用破坏实模式状态）
- **改进计划**：
  - 短期：三模式降级（文本→VGA→VBE），VBE失败自动回退
  - 中期：学习GRUB的VBE设置时机（最后阶段设置，设置后直接进保护模式）
  - 长期：VBE 3.0保护模式接口，或内核态KMS风格模式设置

### 8. 恢复模式
- **GRUB做法**：grub rescue模式，配置损坏时进入最小命令行修复
- **HepuBoot现状**：无恢复模式
- **改进计划**：引导失败时进入HepuBoot Rescue最小命令行

## UEFI优点学习与借鉴

### 1. 原生保护模式（解决BUG-19）
- **UEFI做法**：UEFI固件直接运行在保护模式，引导器也是保护模式C代码，无需实模式/保护模式切换
- **HepuBoot现状**：16位实模式汇编，需要切换到保护模式，代码布局高度敏感（BUG-19）
- **改进计划**：UEFI版本用C语言开发，彻底解决地址布局敏感问题

### 2. GOP图形输出协议（解决BUG-17）
- **UEFI做法**：GOP（Graphics Output Protocol）原生支持高分辨率，无需VBE BIOS调用
- **HepuBoot现状**：VBE BIOS调用在SeaBIOS下有兼容性问题（BUG-17）
- **改进计划**：UEFI版本使用GOP设置高分辨率，原生支持framebuffer

### 3. UEFI变量存储（NVRAM）
- **UEFI做法**：引导项、配置、引导失败计数存储在UEFI变量（NVRAM）中，持久化且标准
- **HepuBoot现状**：引导失败计数和配置存储在固定LBA（LBA10/LBA11），非标准
- **改进计划**：UEFI版本使用UEFI变量存储引导失败计数和配置

### 4. ESP分区与配置文件
- **UEFI做法**：ESP（EFI System Partition）是FAT32标准分区，引导文件和配置文件直接存放
- **HepuBoot现状**：配置文件存储在固定LBA，无文件系统
- **改进计划**：UEFI版本从ESP分区读取hepboot.cfg文本配置文件

### 5. Secure Boot安全引导
- **UEFI做法**：只允许加载经过签名的引导程序和内核，防止Bootkit/Rootkit
- **HepuBoot现状**：无安全引导
- **改进计划**：UEFI版本支持Secure Boot（shim+微软签名或自有密钥）

### 6. GPT分区表与大磁盘支持
- **UEFI做法**：原生支持GPT分区表，支持>2TB大磁盘
- **HepuBoot现状**：仅支持MBR，最大2TB
- **改进计划**：UEFI版本支持GPT分区表

### 7. 模块化驱动模型
- **UEFI做法**：UEFI驱动模型，可在引导阶段加载文件系统、网络等驱动
- **HepuBoot现状**：单文件汇编，所有功能编译在一起
- **改进计划**：UEFI版本可加载UEFI驱动模块

### 8. 网络引导（PXE/iPXE）
- **UEFI做法**：原生支持PXE网络引导，企业部署和无盘工作站
- **HepuBoot现状**：无网络引导
- **改进计划**：UEFI版本支持PXE/iPXE网络引导

### 9. 测量引导（TPM）
- **UEFI做法**：使用TPM记录引导过程中每个组件的哈希，可远程验证系统完整性
- **HepuBoot现状**：无测量引导
- **改进计划**：UEFI版本支持TPM测量引导（企业安全需求）

## HepuBoot双架构设计（BIOS + UEFI）

### 架构定位
HepuBoot将同时支持传统BIOS和现代UEFI两种架构，共享设计理念但独立实现：

| 方面 | BIOS版本（当前） | UEFI版本（规划） |
|------|-----------------|-----------------|
| 开发语言 | 16位汇编 | C语言 |
| 运行模式 | 实模式→保护模式 | 原生保护模式 |
| 磁盘接口 | BIOS INT13 LBA | UEFI Block IO |
| 图形支持 | VGA/VBE（有兼容性问题） | GOP原生高分辨率 |
| 分区表 | MBR | GPT |
| 配置存储 | 固定LBA | ESP分区文件 + UEFI变量 |
| 安全引导 | 无 | Secure Boot |
| 代码布局敏感 | 是（BUG-19） | 否 |

### 共享设计理念
1. **三模式显示降级** - 文本→VGA→VBE/GOP，高模式失败自动回退
2. **自恢复模式** - 引导失败计数+自动恢复模式+F8引导菜单
3. **配置文件驱动** - hepboot.cfg统一配置格式
4. **Multiboot兼容** - 通过multiboot info传递信息给内核

## HepuBoot三模式降级机制

### 设计原则
从低到高尝试显示模式，高模式失败自动回退到低模式，保证系统始终能启动：

```
文本模式 (80x25) ← VGA 320x200 ← VBE 高分辨率
  兜底模式           基础图形         高级图形
  始终可用           大部分设备可用    需VBE BIOS支持
```

### 模式定义
| 模式 | 分辨率 | 颜色 | 帧缓冲地址 | 适用场景 |
|------|--------|------|-----------|----------|
| TEXT | 80x25字符 | 16色 | 0xB8000 | 兜底、服务器、调试 |
| VGA | 320x200 | 256色 | 0xA0000 | 基础图形、旧设备 |
| VBE | 640x480+ | 24/32位 | LFB高地址 | 现代图形、桌面 |

### 降级流程
1. 启动时默认使用文本模式（保证最基本可用性）
2. 尝试设置VGA 320x200模式（INT 10H AX=0013）
   - 成功：使用VGA模式
   - 失败：回退到文本模式
3. 尝试设置VBE高分辨率模式（INT 10H AX=4F02）
   - 成功：使用VBE模式
   - 失败：回退到VGA或文本模式
4. 内核启动后，根据multiboot info中的framebuffer信息选择对应的显示驱动

### 当前状态
- [x] 文本模式：已验证正常
- [x] VGA 320x200：已验证正常（gfxtest通过）
- [ ] VBE高分辨率：bootloader层面SeaBIOS兼容性问题，待修复

## HepuBoot自恢复模式

### 定位
HepuBoot内置自恢复能力，不依赖外部引导器。类似Windows的安全模式，但完全由HepuBoot自身实现：

| 模式 | 触发方式 | 显示模式 | 行为 |
|------|---------|---------|------|
| **正常模式** | 默认 | 文本→VGA→VBE（三模式降级） | 正常引导，加载所有驱动 |
| **安全模式** | 引导菜单选择 / 内核参数 | VGA 640x480 | 禁用问题驱动，最小化启动 |
| **恢复模式** | 引导菜单选择 / 连续失败3次自动进入 | 文本80x25 | 最基础环境，提供修复shell |

### 设计理念
1. **默认快速启动** — 正常模式无菜单，直接引导
2. **失败自动降级** — 高模式失败自动降低模式，引导失败自动进恢复模式
3. **用户可干预** — 启动时按快捷键（如F8）呼出引导菜单
4. **完全自研** — 不依赖GRUB等外部引导器，HepuBoot自身具备恢复能力

### 自恢复机制
1. **三模式显示降级** — VBE失败→VGA，VGA失败→文本，保证有显示
2. **引导失败计数** — 连续失败3次自动进入恢复模式
3. **引导菜单** — 按F8呼出，可选正常/安全/恢复模式
4. **配置文件** — hepboot.cfg可配置默认模式、超时、内核参数

### 实施状态
- [x] 三模式显示降级框架（文本→VGA）
- [x] 引导失败计数和自动恢复（内核侧实现：IDE初始化后读取LBA 10，fail_count>=3自动进恢复模式；shell启动成功后清除计数。bootloader侧call指令因BUG-19暂禁用）
- [x] 引导菜单（F8呼出，正常/安全/恢复模式）
- [x] 内核参数传递（bootloader根据boot_mode设置cmdline: hepu.safe=1 / hepu.recovery=1）
- [x] 内核侧参数解析（multiboot_init解析cmdline，设置mb_safe_mode/mb_recovery_mode标志，提供查询API）
- [x] 安全/恢复模式实际行为（安全模式禁用USB/触屏，恢复模式额外禁用splash/PS2鼠标，显示模式提示）
- [x] 配置文件支持（内核侧实现：LBA 11固定位置存储，无需文件系统；支持default_mode/timeout/extra_cmdline；提供write_config.py工具）
- [ ] VBE高分辨率修复

## HepuBoot架构

### 启动流程
```
BIOS POST
  ↓
MBR (stage1, 512字节, 0x7C00)
  ├─ 读取stage2到0x8000
  └─ 跳转到stage2
       ↓
Stage2 (HepuBoot核心, 0x8000)
  ├─ 初始化段寄存器和栈
  ├─ 显示HepuBoot标识
  ├─ 读取内核到0x10000
  ├─ 收集E820内存映射
  ├─ 显示模式检测与设置（三模式降级）
  ├─ 构建multiboot info
  ├─ 开启A20线
  └─ 进入保护模式，跳转到内核
```

### 文件结构
```
tools/hepboot/
├── hepboot.S          # stage2 核心（原bootloader.S）
├── hepboot_mbr.S      # stage1 MBR（原mbr_stub.S）
├── hepboot.ld         # 链接脚本（原bootloader.ld）
└── make_hybrid.py     # ISO/IMG混合镜像生成
```

## 改进路线图

### P0（当前 - BIOS版本巩固）
- [x] 重命名为HepuBoot
- [x] 三模式降级框架（文本→VGA）
- [x] 自恢复模式设计
- [x] F8引导菜单（正常/安全/恢复）
- [x] 内核参数传递（hepu.safe/hepu.recovery）
- [x] 安全/恢复模式实际行为
- [x] 引导失败计数（内核侧实现）
- [x] 配置文件支持（内核侧实现）
- [ ] VBE高分辨率修复（SeaBIOS兼容性问题）
- [ ] bootloader侧引导失败计数（BUG-19待解决）

### P1（近期 - UEFI版本基础）
- [x] UEFI引导器基础框架（基于gnu-efi标准库，v0.02）
- [x] UEFI应用编译成功（PE32+格式，x86_64）
- [x] UEFI应用在OVMF下成功运行（已验证：HepuBoot界面完整显示，UEFI Firmware: EDK II）
- [x] GPT分区表镜像和ESP分区创建工具
- [x] QEMU + OVMF测试环境搭建
- [x] 解决UEFI应用"Not Found"问题（采用gnu-efi的crt0+链接脚本，详见开发日志(history/devlog/)）
- [x] UEFI文本对齐修复（\r\n换行 + 边框宽度42列）
- [x] ESP分区读取内核文件（v0.03，已验证：读取\hepu-kernel.bin成功，大小0x13B9A6=1292710字节，内核魔数02B0AD1B正确）
- [x] 内核加载到0x100000（v0.04，AllocateAddress分配物理内存，魔数校验通过）
- [x] ExitBootServices（v0.05，MapKey获取成功，退出启动服务）
- [x] x64→32位切换并跳转内核（v0.05，关键技术见开发日志(../history/devlog/00-original-full.md) UEFI-04/05）
- [x] VGA 80x25文本模式设置（v0.05，直接写VGA寄存器，UEFI默认GOP图形模式需主动设置）
- [x] Multiboot info构建（v0.05，UEFI内存映射转换为multiboot格式，内核内存管理正常）
- [x] **内核完整启动到Shell环境**（v0.05，已验证：T0=输出、SHELL main started、[AUTO] ALL DONE所有自动测试通过）
- [x] ESP分区hepboot.cfg配置文件（v0.06，支持timeout/default/cmdline，UEFI freestanding环境需自定义字符串函数）
- [x] UEFI引导菜单（v0.06，正常/安全/恢复三模式，倒计时自动启动，上下键选择）
- [x] UEFI变量存储引导失败计数（v0.07，NV变量存储，>=2次自动安全模式，C键清除）
- [x] UEFI GOP高分辨率图形支持（v0.08，1024x768 32bpp LFB，内核自动识别framebuffer）

### P2（中期 - UEFI高级功能）
- [ ] Secure Boot支持（shim+微软签名）
- [ ] UEFI引导菜单（图形化）
- [ ] 链式加载其他系统（Windows/Linux）
- [ ] 模块化驱动加载
- [ ] BIOS版本模块化架构拆分

### P3（长期 - 企业级特性）
- [ ] 网络引导（PXE/iPXE）
- [ ] 测量引导（TPM）
- [ ] 系统快照与回滚（Btrfs）
- [ ] 休眠/挂起/快速恢复
- [ ] 多语言支持

## 错误历史

详见 [开发日志](../history/devlog/00-original-full.md)

### VBE设置兼容性问题（2026-09-22）
- **现象**：在HepuBoot中调用INT 10H AX=4F02设置VBE模式后，内核无法启动（串口无输出）
- **排查过程**：
  1. 二分定位：setup_vbe只做ret→正常；添加寄存器保存→正常；添加VBE模式信息获取→正常；添加VBE模式设置→失败
  2. 保存恢复所有寄存器（通用/DS/ES/FS/GS/SS/SP）→仍失败
  3. VBE设置移到所有实模式操作最后→仍失败
  4. VBE成功后直接进保护模式不返回→仍失败
- **根因**：SeaBIOS的VBE实现内部切换保护模式设置LFB，切回实模式时破坏了某些底层状态（可能是GDT缓存、内存映射或硬件寄存器）
- **临时方案**：VBE暂禁用，保证文本模式和VGA模式正常启动
- **长期方案**：学习GRUB的VBE设置实现，或使用VBE 3.0保护模式接口，或内核态KMS风格模式设置
