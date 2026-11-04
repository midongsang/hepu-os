---
title: X86_64_ARCHITECTURE
status: frozen
sys_version: 0.13.65
updated: 2026-10-24
owner: history
source_of_truth: null
supersedes: null
---

# 赫普OS 64位内核架构设计：遵循系统包袱准则

> **核心总原则**：严格划分稳定契约边界，模块之间只依赖契约，不依赖对方内部实现；允许废弃模块，但禁止偷偷在核心路径加临时补丁。

## 一、64位内核分层架构

```
用户态应用（64位）
————【稳定契约层：syscall ABI、数据结构、错误码】————
内核上层：进程管理、VFS、内存抽象、IPC、网络协议栈
————【子系统契约：ops结构体，仅暴露函数指针】————
内核底层：物理内存管理、中断控制器、定时器、总线抽象
————【硬件抽象层 HAL】————
硬件（x86_64 CPU、IDE、键盘、RTC、VBE等）
```

## 二、模块边界规则

### 2.1 严格分层依赖
- ✅ 上层只能调用下层暴露的接口函数
- ✅ 上层不能直接访问下层的结构体成员
- ❌ 禁止跨层直接访问（如进程管理直接操作PTE）
- ❌ 禁止下层调用上层（循环依赖）

### 2.2 子系统契约模式
每个子系统对外只暴露：
1. `struct xxx_ops` 函数表（稳定契约）
2. 初始化函数 `xxx_init()`
3. 配置结构体（仅含必要字段）

内部结构体完全私有，放在`.c`文件中，不对外暴露。

### 2.3 当前64位内核模块清单

| 层级 | 模块 | 契约接口 | 内部实现 |
|------|------|----------|----------|
| HAL | CPU | `cpu_ops` | `x86_64.c` |
| HAL | 中断 | `irq_ops` | `irq64_simple.c` |
| HAL | 定时器 | `timer_ops` | 待实现 |
| HAL | IDE | `storage_ops` | `ide64.c` |
| HAL | 键盘 | `input_ops` | `keyboard64.c` |
| HAL | RTC | `rtc_ops` | `rtc64.c` |
| 内核底层 | 物理内存 | `pmm_ops` | `pmm64.c` |
| 内核底层 | GDT/TSS | `gdt_ops` | `gdt64.c` |
| 内核上层 | 虚拟内存 | `vmm_ops` | `vmm64.c` |
| 内核上层 | 进程管理 | `process_ops` | `process64_simple.c` |
| 内核上层 | 系统调用 | `syscall_ops` | `syscall64_simple.c` |
| 基础库 |  libc | 标准函数 | `lib64.c` |

## 三、接口版本化规则

### 3.1 禁止多版本分支
❌ 错误：
```c
void process_schedule() {
    if (scheduler_version == 1) { old_schedule(); }
    else if (scheduler_version == 2) { new_schedule(); }
}
```

✅ 正确：
```c
struct scheduler_ops {
    void (*schedule)(void);
    void (*yield)(void);
};

struct scheduler_ops scheduler_v1_ops = { ... };
struct scheduler_ops scheduler_v2_ops = { ... };

// 启动时选择整个ops实例，不在函数内加if
```

### 3.2 渐进替换流程
1. 新实现：完全独立文件，实现同一套ops接口
2. 启动参数选择：`scheduler=v1` 或 `scheduler=v2`
3. 并行测试：同一套测试用例
4. 验证完成后删除旧实现整套代码

## 四、ABI设计规则

### 4.1 实验版ABI（当前阶段）
- 所有64位系统调用号、结构体布局标记为 `EXPERIMENTAL`
- 开发阶段可随时改动
- 只给内部测试驱动/app使用

### 4.2 稳定版ABI（未来）
- 一旦标记稳定，不能随意增删字段、调整对齐
- 破坏性修改需要版本大升级

### 4.3 当前64位系统调用表（实验版）

| 调用号 | 名称 | 参数 | 返回值 | 状态 |
|--------|------|------|--------|------|
| 0 | SYS_HELLO | 无 | 0 | 实验 |
| 1 | SYS_PUTCHAR | char c | 0 | 实验 |
| 2 | SYS_PUTS | const char *s | 0 | 实验 |
| 3 | SYS_EXIT | int code | 不返回 | 实验 |
| 4 | SYS_GETPID | 无 | pid_t | 实验 |

## 五、硬件驱动包袱控制

### 5.1 HAL抽象
所有硬件驱动实现统一的ops接口：
```c
struct storage_ops {
    int (*read_sector)(uint32_t lba, void *buf);
    int (*write_sector)(uint32_t lba, const void *buf);
    int (*init)(void);
};
```

### 5.2 驱动隔离
- 驱动内部可以hack，但hack不能向上泄露
- 不允许为某一块板子在核心代码加 `#ifdef BOARD_XXX`
- 废弃驱动放在 `drivers/legacy/` 目录，默认不编译

### 5.3 当前64位驱动状态
- ✅ IDE驱动：已实现PIO模式
- ✅ 键盘驱动：已实现PS/2
- ✅ RTC驱动：已实现
- ⏳ VBE驱动：待实现
- ⏳ PCI总线：待实现
- ⏳ AHCI驱动：待实现

## 六、编码与构建规范

### 6.1 文件命名
- 核心模块：`xxx.c` + `xxx.h`（头文件只放对外接口）
- 内部实现细节不放在头文件
- 平台相关：`xxx_arch.c`

### 6.2 函数命名
- 对外接口：`xxx_verb()` 如 `pmm_alloc_page()`
- 内部函数：`_xxx_verb()` 如 `_pmm_find_free()`
- 中断处理：`xxx_interrupt()`

### 6.3 错误处理
- 所有可能失败的函数返回 `int`，0表示成功，负数表示错误
- 错误码统一在 `<errno.h>` 中定义
- 禁止静默失败

### 6.4 注释规范
- 每个对外接口必须有Doxygen风格注释
- 复杂算法必须说明思路
- 临时hack必须标注 `// HACK: 原因 + 修复计划`

## 七、64位内核重构计划

### 阶段1：契约化（当前）
- [ ] 为每个模块定义 `xxx_ops` 结构体
- [ ] 内部结构体移到 `.c` 文件
- [ ] 头文件只放对外接口
- [ ] 消除跨层直接访问

### 阶段2：分层整理
- [ ] 明确HAL层和内核层边界
- [ ] 硬件特例下沉到HAL
- [ ] 消除循环依赖

### 阶段3：版本化接口
- [ ] 系统调用ABI版本化
- [ ] 驱动接口版本化
- [ ] 启动参数选择实现版本

### 阶段4：驱动隔离
- [ ] 实现统一的 `storage_ops`、`input_ops`
- [ ] 驱动注册机制
- [ ] 废弃驱动隔离

## 八、禁止事项清单

1. ❌ 禁止在核心路径加临时补丁（必须走正式接口）
2. ❌ 禁止跨层直接访问结构体成员
3. ❌ 禁止在函数内加多版本if分支
4. ❌ 禁止为特定硬件在核心代码加#ifdef
5. ❌ 禁止静默失败（必须返回错误码）
6. ❌ 禁止循环依赖
7. ❌ 禁止复制粘贴代码（必须抽象成函数）
8. ❌ 禁止过早冻结实验版ABI

## 九、验收标准

64位内核代码符合系统包袱准则的验收标准：
1. 每个模块都有明确的 `xxx_ops` 契约接口
2. 头文件不包含内部结构体定义
3. 没有跨层直接访问
4. 没有多版本if分支
5. 硬件特例全部在HAL层
6. 所有对外接口有完整注释
7. 错误处理完整，无静默失败
8. 构建系统支持选择性编译驱动
