---
title: DYNAMIC_LINKING
status: active
sys_version: 0.55.1
updated: 2026-11-11
owner: specs
source_of_truth: null
supersedes: null
---

> ⚠️ **版本说明**：sys_version 已对齐 0.54.6，但正文口径仍为 v0.13.x ~ v0.33.x 设计稿。源码事实请以 `kernel/arch/x86_64/Makefile` 实际编入为准。


> 注: 原文为 i386 重定位方案, 需按 x86_64 重写(PT_INTERP+ET_DYN+R_X86_64_*), 见 planning/ROADMAP.md P1-2。

# 赫普OS 动态链接支持设计文档

## 版本：V8.34

## 概述

本文档描述赫普OS的动态链接支持设计，包括共享库格式、动态链接器实现、用户态程序动态链接构建等。

## 架构

```
用户态程序（动态链接）
  ↓
动态链接器（ld.so）
  ↓
共享库（libhepu.so, libc.so等）
  ↓
内核（内存映射/系统调用）
```

## 共享库格式

- **格式**：ELF 32位共享对象（ET_DYN）
- **架构**：i386
- **基地址**：0x40000000（可重定位）
- **命名**：lib<name>.so.<version>

## 动态链接器

### 位置
`user/ldso/ldso.c`

### 功能
1. **库加载**：从文件系统加载共享库到内存
2. **符号解析**：在所有已加载库中查找符号
3. **重定位**：处理R_386_RELATIVE/R_386_GLOB_DAT/R_386_JMP_SLOT等重定位
4. **依赖处理**：递归加载DT_NEEDED指定的依赖库
5. **初始化**：调用所有库的DT_INIT函数

### 支持的重定位类型
| 类型 | 值 | 说明 |
|------|-----|------|
| R_386_NONE | 0 | 无操作 |
| R_386_32 | 1 | 绝对重定位 |
| R_386_PC32 | 2 | PC相对重定位 |
| R_386_GLOB_DAT | 6 | 全局数据重定位 |
| R_386_JMP_SLOT | 7 | PLT槽重定位 |
| R_386_RELATIVE | 8 | 相对重定位 |
| R_386_COPY | 5 | 符号复制 |

## 用户态程序构建

### 静态链接（当前）
```makefile
$(LD) -m elf_i386 -Ttext=0x20000000 -o app.elf crt0.o app.o libhepu.a
```

### 动态链接（目标）
```makefile
# 编译共享库
$(CC) -m32 -fPIC -shared -o libhepu.so.1 libhepu.o

# 编译动态链接程序
$(CC) -m32 -fPIC -c app.c -o app.o
$(LD) -m elf_i386 -dynamic-linker /lib/ld.so -o app.elf app.o -lhepu
```

## 系统调用支持

### mmap
- 用于映射共享库到进程地址空间
- 支持PROT_READ/PROT_WRITE/PROT_EXEC

### brk/sbrk
- 用于动态链接器的内存分配

## 当前状态

> **64 位实况（2026-11-11 核对，以 `kernel/arch/x86_64/Makefile` 与 `user_process.c` 为准）**：
> - ET_DYN/PIE 装载已支持（`elf64.c:139/263/335`，装载偏置 + RELATIVE + SysV 初始栈 + auxv AT_PHDR/AT_ENTRY）。
> - PT_INTERP 解释器加载已接入 exec：内核从 ext2 读取 ld.so、按偏置装入库区 `[0x60000000,0x70000000)`、映射进进程并把入口切到解释器，填 AT_BASE/AT_ENTRY/AT_PHDR（`user_process.c:1938-2128`）。
> - DT_NEEDED 符号查找/重定位/TLS 初始化交由用户态 ld.so 完成；内核遗留 `hepu_dynlink_relocate()`（`user_process.c:1523`，含 DT_NEEDED 遍历 1560-1576 与 JMPREL 重定位）已不再被 exec 调用（`user_process.c:2140-2150`）。
> - 下文 i386/R_386_* 与 `user/ldso/` 为 32 位历史设计稿，重定位类型与 64 位 R_X86_64_* 不同，保留作设计参考。

### 已完成（32 位 ldso 框架，历史）
- [x] 动态链接器框架（ldso.h/c）
- [x] ELF结构定义
- [x] 符号解析框架
- [x] 重定位处理框架
- [x] 共享库管理结构

### 待完成（32 位设计稿口径；64 位现状见上方实况）
- [x] ~~内核ELF加载器支持ET_DYN~~（64 位已支持 PIE/ET_DYN，`elf64.c`）
- [x] ~~共享库文件系统加载~~（64 位已加载 PT_INTERP 解释器；.so 依赖由用户态 ld.so 完成，内核不自解析 DT_NEEDED）
- [ ] 完整的PLT/GOT处理（内核遗留 `hepu_dynlink_relocate` 未接线；64 位交由 ld.so）
- [ ] 延迟绑定（Lazy Binding）
- [ ] 动态链接器启动集成（用户态 ld.so 侧）
- [ ] libhepu.so构建
- [ ] 示例动态链接程序

## 安全考虑

1. **库验证**：加载共享库时验证ELF头部和签名
2. **地址随机化**：共享库加载地址随机化（ASLR）
3. **只读重定位**：重定位完成后保护重定位段
4. **库路径限制**：限制共享库搜索路径

## 性能考虑

1. **延迟绑定**：PLT条目首次调用时才解析符号
2. **符号缓存**：缓存已解析的符号
3. **预链接**：预重定位共享库减少启动时间

## 参考

- System V ABI i386补充
- ELF格式规范
- Linux ld.so实现
