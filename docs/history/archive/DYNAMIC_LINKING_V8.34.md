---
title: DYNAMIC_LINKING_V8.34
status: frozen
sys_version: 0.13.65
updated: 2026-10-24
owner: history
source_of_truth: null
supersedes: null
---

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

### 已完成
- [x] 动态链接器框架（ldso.h/c）
- [x] ELF结构定义
- [x] 符号解析框架
- [x] 重定位处理框架
- [x] 共享库管理结构

### 待完成
- [ ] 内核ELF加载器支持ET_DYN
- [ ] 共享库文件系统加载
- [ ] 完整的PLT/GOT处理
- [ ] 延迟绑定（Lazy Binding）
- [ ] 动态链接器启动集成
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
