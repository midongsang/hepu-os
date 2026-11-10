---
title: BOOTLOADER_DEFINITION_PLAN
status: active
sys_version: 0.54.11
updated: 2026-11-01
owner: boot
source_of_truth: null
supersedes: null
---

> ⚠️ **版本说明**：sys_version 已对齐 0.54.6，但正文口径仍为 v0.13.x ~ v0.33.x 设计稿。源码事实请以 `kernel/arch/x86_64/Makefile` 实际编入为准。


# HepuBoot 全局定义规划与命名规范

## 版本

见 frontmatter `sys_version`（当前 0.54.6）。

## 目的
规范引导器中所有宏定义、标签、内存地址的命名和分配，避免重复定义、地址冲突和跳转歧义。

## 一、宏定义规范

### 1.1 命名规则
- 全大写，单词间用下划线分隔
- 前缀标识类别：
  - `KERNEL_` - 内核相关
  - `SEG1_` / `SEG2_` - 分段读取相关
  - `SECTOR_` - 扇区相关
  - `DAP_` - 磁盘地址包相关
  - `E820_` / `MMAP_` - 内存映射相关
  - `GDT_` / `GDTR_` - GDT相关
  - `STACK_` - 栈相关
  - `VBE_` - VBE相关
  - `BOOT_STATE_` - 引导状态相关
  - `MB_` - Multiboot相关
  - `RM16_` - 实模式桩代码相关

### 1.2 条件编译规则
- ISO/HDD双变体必须用 `#ifdef ISO_VARIANT` / `#else` / `#endif` 明确区分
- 禁止在同一变体下重复定义同一宏
- SEG1_SEC等分段宏必须用 `#if` / `#else` / `#endif` 条件编译，禁止裸重复定义

### 1.3 当前宏定义清单（已审计）

| 宏名 | 值 | 用途 | 条件 |
|------|-----|------|------|
| KERNEL_LBA | 21 / 42 | 内核起始LBA | ISO/HDD |
| KERNEL_SECTORS | 动态 | 内核扇区数 | 编译参数 |
| SECTOR_SHIFT | 11 / 9 | 扇区移位(2048/512字节) | ISO/HDD |
| KERNEL_LOAD_ADDR | 0x00100000 | 内核加载地址 | 全局 |
| DAP_BUFFER | 0x00007B00 | DAP缓冲区 | 全局 |
| E820_BUFFER | 0x00009000 | E820临时缓冲 | 全局 |
| MMAP_BUFFER | 0x00009400 | Multiboot mmap区 | 全局 |
| GDT_ADDR | 0x00009800 | GDT地址 | 全局 |
| GDTR_ADDR | 0x00009908 | GDTR伪描述符 | 全局 |
| RM16_STUB_ADDR | 0x00001000 | 实模式桩代码区 | 全局 |
| MBINFO_ADDR | 0x00009700 | Multiboot info结构 | 全局 |
| STACK_ADDR | 0x00008F00 | 保护模式栈顶 | 全局 |
| VBE_CTRL_BUF | 0x00007000 | VBE控制信息缓冲 | 全局 |
| VBE_MODE_BUF | 0x00007200 | VBE模式信息缓冲 | 全局 |
| VBE_VARS | 0x00007400 | VBE变量区 | 全局 |
| VBE_TARGET_MODE | 0x0115 | 目标VBE模式(800x600x24) | 全局 |
| BOOT_STATE_LBA | 10 | 引导状态扇区 | 全局 |
| BOOT_STATE_ADDR | 0x00007E00 | 引导状态地址 | 全局 |
| BOOT_STATE_MAGIC | 0x48455055 | "HEPU"魔数 | 全局 |
| BOOT_FAIL_THRESHOLD | 3 | 失败阈值 | 全局 |
| MB_BOOT_MAGIC | 0x2BADB002 | Multiboot引导魔数 | 全局 |
| MB_HEADER_MAGIC | 0x1BADB002 | Multiboot头魔数 | 全局 |
| SEG1_SEC | min(KERNEL_SECTORS, 1152) | 段1扇区数 | 条件编译 |
| SEG2_SEC | KERNEL_SECTORS - SEG1_SEC | 段2扇区数 | 全局 |
| SEG2_LBA | KERNEL_LBA + SEG1_SEC | 段2起始LBA | 全局 |
| SEG2_DEST | 0x100000 + SEG1_SIZE | 段2目标地址 | 全局 |

## 二、内存地址分配规划

### 2.1 低内存布局（0x00000 - 0x9FFFF）

```
0x00000 - 0x003FF  IVT (中断向量表, 1KB)
0x00400 - 0x004FF  BIOS数据区 (BDA, 256B)
0x00500 - 0x00FFF  保留
0x01000 - 0x01FFF  RM16_STUB_ADDR (实模式桩代码, 4KB)
0x02000 - 0x06FFF  保留
0x07000 - 0x071FF  VBE_CTRL_BUF (VBE控制信息, 512B)
0x07200 - 0x073FF  VBE_MODE_BUF (VBE模式信息, 512B)
0x07400 - 0x075FF  VBE_VARS (VBE变量, 512B)
0x07600 - 0x07AFF  保留 (DAP临时区0x7000在read_slice中使用)
0x07B00 - 0x07BFF  DAP_BUFFER (磁盘地址包, 256B)
0x07C00 - 0x07DFF  MBR加载区/实模式栈 (512B)
0x07E00 - 0x07FFF  BOOT_STATE_ADDR (引导状态, 512B)
0x08000 - 0x08FFF  stage2代码区 (HepuBoot, 4KB)
0x09000 - 0x096FF  保留
0x09700 - 0x097FF  MBINFO_ADDR (Multiboot info, 256B)
0x09800 - 0x09907  GDT_ADDR (GDT, 264B)
0x09908 - 0x0990F  GDTR_ADDR (GDTR伪描述符, 8B)
0x09910 - 0x09917  临时IDTR (8B)
0x09918 - 0x09FFF  保留
0x0A000 - 0x0FFFF  VGA显存/保留
0x10000 - 0x8FFFF  内核加载缓冲 (低内存, 512KB, SEG1读取区)
0x90000 - 0x9FFFF  扩展BIOS数据区 (EBDA)
```

### 2.2 高内存布局（0x100000+）

```
0x100000 - 0x4FFFFF  内核加载区 (4MB)
0x500000 - ...        内核运行时堆/栈
```

### 2.3 引导期变量区（0x4000 - 0x40FF）

```
0x4000  ret_ip (递归检测返回IP)
0x4002  saved_sp (递归检测保存SP)
0x4004  in_int13 (递归检测标志)
0x4010  remaining (SEG2分块剩余扇区)
0x4014  current LBA (SEG2分块当前LBA)
0x4018  destination (SEG2分块目标地址)
0x401C  chunk_size (SEG2分块大小)
0x4020  int13_ext_supported (INT13扩展支持标志)
0x4030  vbe_info_signature (VBE信息签名"VBEI")
```

## 三、标签命名规范

### 3.1 命名标签规则
- 全小写，单词间用下划线分隔
- 函数/过程标签：动词_名词（如 `read_slice`, `enter_protected`, `collect_e820`）
- 循环标签：名词_loop（如 `e820_loop`, `chunk_loop`）
- 完成标签：名词_done（如 `e820_done`, `chunk_done`）
- 失败标签：名词_fail（如 `chs_fail`, `read_fail`, `e801_fail`）
- 返回标签：名词_ret（如 `read_ret`, `e820_ret`）

### 3.2 数字标签使用规则（关键！）
- 数字标签（1:, 2:, 3:）只能用于**局部短距离跳转**（前后50行内）
- **禁止**在错误处理路径和正常路径使用相同的数字标签
- `jxx 1f` 向前引用时，必须确保前方最近的 `1:` 是目标标签
- 如果错误处理路径在正常路径之前，错误处理路径必须使用 `3:`/`4:` 等不冲突的标签
- 复杂逻辑优先使用命名标签，避免数字标签歧义

### 3.3 已审计的命名标签清单

| 标签 | 行号 | 类型 | 用途 |
|------|------|------|------|
| _start | 66 | 入口 | stage2入口 |
| print_error_code_vga | 359 | 函数 | VGA错误码显示 |
| halt_loop | 389 | 循环 | 停机循环(键盘响应+重启) |
| print_str | 446 | 函数 | 字符串打印 |
| print_hex8 | 489 | 函数 | 8位十六进制打印 |
| print_hex16 | 537 | 函数 | 16位十六进制打印 |
| print_dec32 | 567 | 函数 | 32位十进制打印 |
| delay | 624 | 函数 | 延时 |
| check_int13_ext | 655 | 函数 | INT13扩展检测 |
| read_kernel | 675 | 函数 | 读取内核(SEG1) |
| read_seg2 | 690 | 函数 | 读取SEG2(旧版,未使用) |
| read_slice | 698 | 函数 | 扇区读取(简化版) |
| read_sector_chs | 775 | 函数 | CHS方式读取 |
| chs_fail | 845 | 失败 | CHS读取失败 |
| chs_done | 847 | 完成 | CHS读取完成 |
| read_fail | 853 | 失败 | 读取失败 |
| read_ret | 930 | 返回 | 读取返回 |
| collect_e820 | 947 | 函数 | 收集E820内存映射 |
| e820_loop | 955 | 循环 | E820循环 |
| e820_done | 1004 | 完成 | E820完成 |
| build_mbinfo | 1043 | 函数 | 构建Multiboot info |
| enter_protected | 1088 | 函数 | 进入保护模式 |
| protected_mode | 1137 | 标签 | 保护模式入口 |
| code16_switch | 1169 | 标签 | 16位代码段切换 |
| rm_read_seg2 | 1180 | 函数 | 回实模式读SEG2 |
| chunk_loop | 1219 | 循环 | SEG2分块循环 |
| pm_chunk_copy | 1250 | 函数 | 保护模式分块搬移 |
| code16_switch_chunk | 1272 | 标签 | 分块回实模式切换 |

## 四、历史问题记录

### BUG-47: 数字标签重复定义导致跳转错误（致命）
- **发现日期**: 2026-10-08
- **问题**: `call read_kernel` 后的 `jnz 1f` 向前查找最近的 `1:` 标签，但错误处理路径中的 `1:`（键盘轮询循环）在正常路径的 `1:`（R1标记）之前，导致跳转到错误处理路径，系统一直等待键盘输入
- **影响**: 引导器在成功读取SEG1后卡住，不显示R1及后续标记
- **修复**: 错误处理路径的标签改为 `3:`/`4:`，使 `jnz 1f` 正确跳转到正常路径
- **教训**: 数字标签只能用于局部短距离跳转，错误处理路径和正常路径禁止使用相同数字标签

### BUG-48: INT13磁盘重置在关中断状态下调用（待验证）
- **发现日期**: 2026-10-09
- **问题**: 回实模式后INT13 AH=00磁盘重置在关中断状态下调用，BIOS可能等待磁盘中断而永久不返回
- **影响**: SEG2分块读取前系统挂起
- **修复**: INT13磁盘重置前添加 `sti` 开中断
- **状态**: 待QEMU验证

## 五、后续工作
1. 将read_seg2旧版函数移除（未使用）
2. 将关键数字标签改为命名标签，消除歧义
3. 统一所有INT13调用前开中断、调用后关中断的模式
4. 添加编译时标签重复检测脚本

### BUG-49: BIOS INT13内部状态累积导致大扇区数读取卡住（已修复）
- **发现日期**: 2026-10-09
- **问题**: SEG1_SEC > 336扇区（168KB）时，read_slice循环卡住。BIOS INT13在连续读取超过约336扇区后，内部状态出现累积问题。
- **影响**: 无法加载大于168KB的内核段
- **修复**: 每64扇区执行一次磁盘重置（AH=00H），清除BIOS INT13内部状态
- **验证**: SEG1=1100（550KB）验证成功
- **最终配置**: SEG1_SEC=1100，每64扇区磁盘重置

### BUG-50: QEMU -hda参数无法正确识别raw格式镜像（已确认）
- **发现日期**: 2026-10-09
- **问题**: 使用`-hda`参数启动QEMU时，只显示SeaBIOS启动画面，无HepuBoot引导信息
- **影响**: 仅影响QEMU测试
- **解决方案**: 必须使用`-drive if=ide,format=raw,file=...`参数

## 六、当前稳定配置（0.54.6）

| 参数 | 值 | 说明 |
|------|-----|------|
| SEG1_SEC | 1100扇区（550KB） | 实模式读取的内核段大小 |
| SEG2_SEC | KERNEL_SECTORS-1100 | 保护模式读取的内核段大小 |
| 磁盘重置间隔 | 每64扇区 | 清除BIOS INT13内部状态 |
| 内核加载地址 | 0x100000 | 1MB以上 |
| 内核入口 | 0x101000 | 内核入口点 |
| 引导方式 | BIOS+UEFI混合 | 单一镜像支持两种启动模式 |
| 验证状态 | QEMU通过 | 待真机验证 |
