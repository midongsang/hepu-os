# 赫普OS 引导器版本变更记录（CHANGELOG）

> 记录 tools/bootloader.S 的开发迭代版本、关键变更和状态。
> **版本号说明**：V6.x 为引导器内部开发迭代号，非系统发布版本。
> 系统/镜像正式版本为 **V1.02**（镜像文件名 hepu-os-v1.02.*），引导器固件版本 v1.1，文档版本 V3.24。正式版本体系见 DESIGN.md §14。
> 最后更新：引导器迭代 V6.46（对应系统镜像 V1.02）

---

## 版本总览

| 版本 | 日期 | 关键变更 | 状态 | stage2大小 |
|------|------|----------|------|-----------|
| V6.32 | 2026-09-20 | 16位代码段回实模式方案，修复清PE后CS.base重置 | 回实模式成功 | ~1800B |
| V6.35 | 2026-09-20 | 完全重写read_slice，手动编码16位rel16 | SEG1正常，SEG2递归 | ~1850B |
| V6.37 | 2026-09-20 | 栈位置移到0x7C00 | SEG1恢复正常 | ~1860B |
| V6.38 | 2026-09-20 | INT13包装器方案（修改IVT） | 失败（IVT偏移bug） | ~1900B |
| V6.39 | 2026-09-20 | 递归检测方案（变量在.bss） | 部分生效，变量被覆盖 | ~1920B |
| V6.40 | 2026-09-20 | ATA PIO直接读盘方案 | 失败（BSY死循环） | ~1950B |
| V6.41 | 2026-09-20 | 递归检测变量移到0x4000，循环入口检测 | SEG1受干扰 | ~1930B |
| V6.42 | 2026-09-20 | 回实模式后FS/GS清零 | SEG1正常，SEG2仍递归 | ~1867B |
| V6.43 | 2026-09-21 | 回实模式后ESP/EFLAGS高16位清零 | 未解决递归 | ~1879B |
| V6.44 | 2026-09-21 | 修复jc错误检测+递归检测+ESP/EFLAGS/FS/GS清零 | SEG2仍zero | ~1931B |
| **V6.45** | **2026-09-21** | **显式加载IDTR+INT13 AH=00磁盘重置（最终修复！）** | **系统启动到内核！** | ~1960B |
| **V6.46** | **2026-09-21** | **巩固版：移除递归检测workaround+读盘重试+清理诊断标记** | **3次启动验证稳定** | **1784B** |

---

## V6.46 巩固版（当前版本）

### 变更内容
1. **移除递归检测 workaround**：根因已由 V6.45 的 IDTR+磁盘重置解决，移除 read_slice 入口递归检测、INT13 前标志保存、0x4000 变量初始化
2. **修复 INT13 错误检测**：将错误的 `testw $1, %ax; jnz`（测试 AL 位0）改为正确的 `jc`（测试 CF 进位标志）
3. **添加读盘重试机制**：每扇区 INT13 失败时自动重试最多 3 次（`read_retry` 变量）
4. **清理 VGA 诊断标记**：移除 P0/M1/R1/P1/P2/RS 等开发期诊断标记，保留 read_fail 错误信息显示
5. **清理 .bss 段**：移除 8 个已废弃变量（saved_ret_ip/saved_sp/orig_int13_*/wrapper_saved_sp/int13_ret_ip/int13_saved_sp/in_int13）

### 验证结果
- stage2 大小：1931B → **1784B**（节省 147 字节，硬限制 2048B）
- 连续 **3 次 QEMU 启动全部成功**（SEG1=OK, SEG2=OK, Kernel=OK）
- 内核运行到 shell，执行用户态进程（winargs.exe、macho_demo）

---

## V6.45 最终修复版

### 根因
保护模式→实模式切换后，BIOS 的中断描述符表缓存或磁盘控制器内部状态被扰乱，导致 INT13 AH=42 处理程序行为异常（切换栈且不切回），IRET 从错误栈弹出垃圾返回地址→递归到 read_slice 入口→恶性循环。

### 修复
在 `rm_read_seg2`（回实模式后读 SEG2 的入口）中，FS/GS 清零之后、读盘之前，添加：
1. **显式加载 IDTR**：`lidt` 加载 BIOS IVT（base=0, limit=0x3FF）
2. **INT13 AH=00 磁盘重置**：让 BIOS 重新初始化磁盘控制器内部状态

### 验证
系统成功启动到内核，VGA 显示 `[HEPU]`、`[SYSCALL]`、`[PAGING]`、`[SHELL]`、`[MACHO]` 等内核输出。

---

## 关键里程碑

- **V6.32**：回实模式成功（16位代码段方案）
- **V6.37**：SEG1 正常加载（栈位置 0x7C00）
- **V6.45**：**系统启动到内核**（IDTR+磁盘重置解决递归中断）
- **V6.46**：代码巩固稳定（1784B，3次启动验证）

---

## 已放弃方案记录

| 方案 | 版本 | 失败原因 |
|------|------|----------|
| AH=02 CHS读盘 | V4.95 | SeaBIOS磁盘几何限制，LBA≥2016越界 |
| 独立栈0x6000/0x9000 | V6.36 | BIOS仍切换栈 |
| INT13包装器（修改IVT） | V6.38 | IVT写入绝对地址而非偏移，早期崩溃 |
| ATA PIO直接读盘 | V6.40 | 发送读命令后BSY一直为1 |
| v517低地址桩方案 | — | base=0x1000描述符+ljmp到0x0100，不可行 |

---

## 备份文件

| 文件 | 说明 |
|------|------|
| `bootloader.S.v645_working` | V6.45 备份（含递归检测，IDTR+磁盘重置首次成功） |
| `bootloader.S.v619_stable` | V6.19 备份（注意：同样存在递归问题，非真正稳定版） |
| `bootloader.S.v382.bak` / `.v382_dap.bak` | 项目原有备份 |
| `bootloader.S.v517.bak` | 项目原有备份（含完整低地址桩实现参考） |
| `bootloader.S.v620_stub.bak` | 项目原有备份 |

---

## V1.02 — Rust用户态支持（2026-09-21）

### 新增功能

- **Rust用户态应用支持**：首个Rust用户态应用hello-rust.elf在赫普OS上成功运行
  - Rust nightly 1.100.0工具链，中科大镜像源
  - 自定义目标hepu-i686.json，LOAD段地址0x08048000
  - hepu-sys系统调用绑定库（Linux i386 ABI）
  - 基于brk的简单内存分配器
  - 自定义panic handler
  - Makefile集成，`make`自动构建Rust应用
- **Rust开发文档**：docs/RUST_DEVELOPMENT.md，包含环境搭建、项目结构、系统调用ABI、开发指南、常见问题

### 修复BUG

- **BUG-17：根文件系统挂载后/dev节点丢失**
  - 根因：fs2_mount挂载ext2到"/"时，vfs_mount直接替换root->children，导致/dev/console丢失
  - 影响：所有ELF应用write(1,...)返回-1，无输出
  - 修复：binfmt_elf中添加fallback，找不到/dev/console时自动重新创建
  - 文件：kernel/compat/binfmt_elf.c, kernel/fs/devfs.c（console_ops导出为全局）

- **BUG-18：诊断代码栈不平衡导致longjmp返回地址被覆盖**
  - 根因：elf_mainline_save中大量诊断输出导致PIC代码栈指针偏移48字节，返回地址被0x3F8覆盖
  - 诊断证据：ret前esp=0x06B30A80 ret_addr=0x000003F8
  - 修复：移除关键路径中的诊断代码
  - 文件：kernel/compat/binfmt_elf.c

### 技术验证

- **C核心+汇编最底层+Rust用户态**技术路线完全可行
- Rust应用通过Linux i386系统调用ABI（int 0x80）与内核交互
- 内核通过linux_abi_translate将Linux调用号翻译为HEPU号
- 应用输出同时显示在VGA文本模式和串口（COM1）

### 已知限制

- hepu-sys库的内存分配器不支持free/realloc
- sleep_ms使用yield循环代替nanosleep
- 仅支持静态链接，无动态链接
- 仅支持i686架构

### 后续计划

- 完善hepu-sys库（更多系统调用、完善分配器、nanosleep）
- 编写更多Rust示例应用
- 选择用户态服务用Rust重写（文件服务/网络服务等）
- 编写Rust开发教程和最佳实践
