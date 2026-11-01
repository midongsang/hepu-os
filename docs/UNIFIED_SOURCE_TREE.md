# 赫普OS 统一源码树进度

**更新日期**: 2026-10-14
**版本**: v0.10.134

## 概述

统一源码树目标：将32位(x86)和64位(x86_64)内核共享的代码提取到 `kernel/shared/` 目录，
减少代码重复，便于同步修复和功能演进。

原则：渐进式迁移，先共享数据结构和接口定义，再共享算法实现，最后共享驱动。
32位稳定优先，不强制32位立即引用shared。

## 重大进展 (v0.10.134)

### 32位共享模块集成成功 ✅
- 32位Makefile已排除x86_64目录，避免64位特定代码污染
- 32位kernel.c添加x86_64_detect实现（CPUID检测长模式）
- 4个共享C模块已成功编译并链接到32位内核：
  - driver_framework.o (统一驱动框架)
  - ext2_algo.o (ext2文件系统算法)
  - gfx_algo.o (图形算法)
  - lib_shared.o (字符串/内存函数)
- 32位内核大小：4.1MB（功能不受影响）

### APIC纯APIC模式验证成功 ✅
- Local APIC初始化（PIC兼容模式）
- I/O APIC初始化（ID=0, Ver=0x20, MaxRedir=42）
- IMCR切换到APIC模式
- PIC屏蔽，中断重定向到I/O APIC
- 纯APIC模式下系统完全稳定运行，桌面环境启动正常
- Shell命令：`apic mode`（查看当前模式）、`apic switch`（切换到纯APIC模式）

## 目录结构

```
kernel/shared/
├── security/
│   └── user_access.h          # 用户空间访问安全接口（copy_from_user等）
├── fs/
│   ├── ext2_shared.h          # ext2文件系统数据结构（超级块/inode/目录项）
│   ├── ext2_algo.h            # ext2共享算法接口（上下文+回调模式）
│   └── ext2_algo.c            # ext2共享算法实现（inode/数据块/目录/位图/路径）
├── gfx/
│   └── gfx_shared.h           # 图形子系统共享（颜色/矩形/帧缓冲/后端操作表）
├── mm/
│   └── mm_shared.h            # 内存管理共享（页大小/区域类型/对齐宏）
├── net/
│   └── net_shared.h           # 网络协议栈共享（MAC/IPv4/协议常量）
└── drivers/
    ├── driver_framework.h     # 统一驱动框架接口
    └── driver_framework.c     # 统一驱动框架实现
```

## 已完成

### 1. 安全接口共享 (security/user_access.h) ✅
- 定义 copy_from_user / copy_to_user / copy_str_from_user
- 定义 user_strlen / verify_user_read / verify_user_write
- 定义 hepu_is_user_address / hepu_user_space_limit
- **64位**: user_access64.c 已引用共享头，实现架构相关地址校验
- **32位**: sys_mem.c 已有等价实现，待迁移到共享接口

### 2. ext2文件系统共享 (fs/ext2_shared.h) ✅
- 超级块结构 (ext2_superblock_t, 1024字节)
- 块组描述符 (ext2_group_desc_t, 32字节)
- inode结构 (ext2_inode_t, 128字节)
- 目录项结构 (ext2_dir_entry_t, 变长)
- 文件类型/模式标志/魔数常量
- **64位**: ext2_64.c 已引用
- **32位**: 待迁移

### 3. 图形子系统共享 (gfx/gfx_shared.h) ✅
- 颜色格式 (gfx_color_t, 32位ARGB)
- 点/矩形/尺寸数据结构
- 帧缓冲信息 (gfx_framebuffer_t)
- 字体信息 (gfx_font_t)
- 图形后端操作表 (gfx_backend_ops_t) - 接口（操作表）模式
- 窗口/鼠标光标结构
- 基础算法：点在矩形内、矩形相交、alpha混合、颜色插值（static inline）
- **64位**: gfx64.c / desktop64.c 已引用
- **32位**: 待迁移

### 4. 内存管理共享 (mm/mm_shared.h) ✅
- 页大小/对齐宏
- 内存区域类型枚举
- **64位**: 待引用
- **32位**: 待迁移

### 5. 网络协议栈共享 (net/net_shared.h) ✅
- MAC地址/IPv4地址结构
- 协议常量
- **64位**: 待引用
- **32位**: 待迁移

### 6. 驱动框架 (drivers/driver_framework.c/h) ✅
- 统一驱动注册/查找框架
- **64位**: drv_register64.c / storage_hal64.c 已引用
- **32位**: ✅已集成编译（v0.10.134）

### 7. ext2共享算法 (fs/ext2_algo.c/h) ✅ (P0-2)
- ext2_context_t上下文：超级块+块读写回调+临时缓冲区
- 核心算法：inode读写、数据块读写（直接/间接/双重/三重间接）
- 目录查找、路径解析、块分配/释放、inode分配
- 目录项添加、文件读取（支持偏移）
- 架构无关：通过ext2_read_block_fn/ext2_write_block_fn回调访问存储
- **64位**: 已加入编译，ext2_64.c可逐步迁移到共享算法
- **32位**: ✅已集成编译（v0.10.134）

### 8. 图形共享算法 (gfx/gfx_algo.c) ✅ (P0-1)
- 共享图形算法：矩形填充、线条、alpha混合、字体渲染
- **64位**: gfx64.c已引用，TTF渲染改为alpha混合
- **32位**: ✅已集成编译（v0.10.134）

### 9. 共享库函数 (lib/lib_shared.c/h) ✅ (P0-3)
- 字符串操作：strlen/strcmp/strncmp/strcpy/strncpy/strcat/strchr/strrchr
- 内存操作：memset/memcpy/memmove/memcmp/memchr
- 转换：atoi/itoa
- **64位**: 已加入编译
- **32位**: ✅已集成编译（v0.10.134）

## 64位安全漏洞移植状态

| 漏洞 | 严重程度 | 64位状态 | 说明 |
|------|---------|---------|------|
| C: 用户指针无校验 | 🔴致命 | ✅已修复 | syscall64_full接入copy_from_user；syscall64_simple的SYS_PUTS已修复 |
| D: 数组越界写 | 🟠高危 | ✅已有防护 | 进程表遍历查找(非pid索引)；文件描述符有fd<MAX_FILES检查 |
| G: 内核地址泄露 | 🟡中危 | ✅无泄露 | sys_brk返回用户空间地址(0x500000)；不返回内核指针 |
| A: ring3无内存隔离 | 🔴致命 | ⚠️部分 | 用户态进程共享内核页表，待实现独立CR3 |
| B: 系统调用暴露内核堆 | 🔴致命 | ⚠️部分 | sys_malloc未实现；用户态使用brk分配 |
| E: signal存任意函数指针 | 🟠高危 | ⏳待查 | 64位信号机制待实现 |
| F: 沙箱绕过 | 🟠高危 | ⏳待查 | 64位沙箱机制待实现 |
| H: 共享内存无权限 | 🟡中危 | ⏳待查 | 64位共享内存待实现 |

## 后续计划

### P1 - 近期
1. 32位sys_mem.c迁移到shared/security/user_access.h接口
2. 64位vmm64.c引用mm_shared.h
3. 64位net64_*.c引用net_shared.h

### P2 - 中期
4. 提取ext2通用算法到shared/fs/ext2_shared.c（块映射/目录遍历）
5. 提取图形基础绘制到shared/gfx/gfx_shared.c（fill_rect/draw_line）
6. 32位gfx引用gfx_shared.h

### P3 - 长期
7. 驱动框架完全统一（32位和64位共用driver_framework.c）
8. 网络协议栈算法共享（TCP状态机/IP校验和）
9. 内存分配算法共享（slab分配器）

## 引用统计

- shared目录文件数: 8个（6个头文件 + 1个实现 + 1个接口）
- 64位引用shared的文件: 6个（ext2_64, gfx64, desktop64, drv_register64, storage_hal64, user_access64）
- 32位引用shared的文件: 0个（稳定优先，待逐步迁移）

## v0.10.128 更新 - P0-3: 共享字符串/内存库

### 新增文件
- `kernel/shared/lib/lib_shared.h` - 共享库接口（15个函数）
- `kernel/shared/lib/lib_shared.c` - 共享库实现（约200行）

### 包含函数
- 字符串：strlen, strcmp, strncmp, strcpy, strncpy, strcat, strchr, strrchr
- 内存：memset, memcpy, memmove, memcmp, memchr
- 转换：atoi, itoa

### 集成状态
- 64位：已加入Makefile，编译通过（内核156K）
- 32位：待集成（稳定优先）

### 共享模块统计（当前共13个文件）
1. kernel/shared/security/user_access.h
2. kernel/shared/gfx/gfx_algo.c + gfx_shared.h
3. kernel/shared/fs/ext2_algo.c + ext2_algo.h + ext2_shared.h
4. kernel/shared/mm/mm_shared.h
5. kernel/shared/net/net_shared.h
6. kernel/shared/drivers/driver_framework.c + .h
7. kernel/shared/lib/lib_shared.c + .h (新增)
