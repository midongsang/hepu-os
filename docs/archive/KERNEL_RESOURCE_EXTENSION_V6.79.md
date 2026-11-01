# 赫普OS 内核资源扩展设计文档 V6.79 - PAE物理地址扩展

## 概述

V6.79实现PAE（Physical Address Extension）物理地址扩展支持，将寻址能力从32位（4GB）扩展到36位（64GB）。当前完成CPU检测、数据结构和分页模块，实际页表切换待独立分支。

## 一、PAE技术背景

### 1.1 什么是PAE

PAE是Intel x86处理器的物理地址扩展技术，通过增加页表项宽度（32位→64位）和增加一级页表（PDPT），将物理寻址能力从32位扩展到36位，支持最大64GB物理内存。

### 1.2 页表结构对比

| 特性 | 非PAE（32位） | PAE（36位） |
|------|--------------|-------------|
| 页表级数 | 2级（PD+PT） | 3级（PDPT+PD+PT） |
| 页表项大小 | 4字节 | 8字节 |
| PDPT | 无 | 4项，32字节 |
| PD项数 | 1024 | 512 |
| PT项数 | 1024 | 512 |
| 大页大小 | 4MB | 2MB |
| 最大物理内存 | 4GB | 64GB |
| CR4.PAE位 | 0 | 1 |

### 1.3 虚拟地址分解

```
非PAE:
31        22 21        12 11         0
┌──────────┬───────────┬─────────────┐
│ PD索引   │ PT索引    │ 页内偏移    │
│ (10位)   │ (10位)    │ (12位)      │
└──────────┴───────────┴─────────────┘

PAE:
31 30 29  21 20       12 11         0
┌─┬───────┬───────────┬─────────────┐
│ │PDPT索引│ PD索引    │ 页内偏移    │
│ │(2位)  │ (9位)     │ (12位)      │
└─┴───────┴───────────┴─────────────┘
      PT索引(9位)
```

## 二、数据结构实现（pae.c/pae.h）

### 2.1 类型定义

```c
// PDPT: 页目录指针表，4项，每项8字节，共32字节
typedef struct { uint64_t e[4]; }   pae_pdpt_t;

// PD: 页目录，512项，每项8字节，共4KB
typedef struct { uint64_t e[512]; } pae_pd_t;

// PT: 页表，512项，每项8字节，共4KB
typedef struct { uint64_t e[512]; } pae_pt_t;
```

### 2.2 页表项标志

| 标志 | 位 | 说明 |
|------|-----|------|
| PAE_PRESENT | 0 | 存在位 |
| PAE_WRITE | 1 | 可写 |
| PAE_USER | 2 | 用户态可访问 |
| PAE_PWT | 3 | 写透缓存 |
| PAE_PCD | 4 | 缓存禁用 |
| PAE_ACCESSED | 5 | 已访问 |
| PAE_DIRTY | 6 | 已修改 |
| PAE_PS | 7 | PD项: 2MB大页 |
| PAE_GLOBAL | 8 | 全局页 |
| PAE_NX | 63 | 禁止执行 |

### 2.3 核心函数

| 函数 | 功能 |
|------|------|
| pae_cpu_supported | 检测CPU是否支持PAE（CPUID） |
| pae_enable | 启用CR4.PAE位 |
| pae_map_page | 映射4KB页面 |
| pae_map_2mb_page | 映射2MB大页 |
| pae_selftest | 自测（数据结构验证） |

## 三、PAE分页管理模块（paging_pae.c）

### 3.1 设计目标

- 提供与非PAE分页对等的API
- 支持PDPT/PD/PT三级页表管理
- 支持2MB大页
- 统一void*接口，便于双模式切换

### 3.2 核心API

```c
void paging_pae_init(uint32_t mem_size_kb);
void paging_pae_enable(void);
int  paging_pae_map(void *pdpt, uint32_t virt, uint64_t phys, uint32_t flags);
void paging_pae_unmap(void *pdpt, uint32_t virt);
uint64_t paging_pae_get_phys(void *pdpt, uint32_t virt);
void paging_pae_switch(void *pdpt);
void *paging_pae_create_dir(void);
```

### 3.3 映射流程

```
paging_pae_map(virt, phys)
  ├─ 计算PDPT索引 (virt>>30)&0x3
  ├─ 计算PD索引 (virt>>21)&0x1FF
  ├─ 计算PT索引 (virt>>12)&0x1FF
  ├─ PDPT项不存在 → 分配PD页，设置PDPT项
  ├─ PD项不存在或为大页 → 分配PT页，设置PD项
  └─ 设置PT项 = phys | flags
```

### 3.4 2MB大页支持

当PD项设置PAE_PS位时，该PD项直接指向2MB物理页，无需PT：

```c
// 映射2MB大页
pd->e[didx] = (phys & 0xFFFFFFE00000ULL) | PAE_PRESENT | PAE_WRITE | PAE_PS;
```

## 四、CPU检测集成（paging.c）

### 4.1 检测流程

```c
void paging_init(uint32_t mem_size_kb) {
    if (pae_cpu_supported()) {
        use_pae = 1;
        vga_puts("[PAGING] CPU supports PAE (36-bit, 64GB max)\n");
        // 当前仍使用非PAE分页，完整切换待独立分支
        vga_puts("[PAGING] PAE detected, legacy paging active\n");
    }
    ...
}
```

### 4.2 启动日志

```
[PAGING] CPU supports PAE (64GB max)
[PAGING] PAE detected, legacy paging active
[PAE] selftest: start
[PAE] selftest PASS (data structures)
```

## 五、完整启用PAE的挑战

### 5.1 重构范围评估

| 项目 | 数量 | 说明 |
|------|------|------|
| paging_map调用点 | 77处 | 需适配双模式 |
| page_directory_t引用 | 109处 | 需类型抽象 |
| 汇编硬编码偏移 | page_dir@68 | 不能改变 |
| 页错误处理 | idt.c | 依赖非PAE格式 |
| 进程创建/销毁 | sys_process.c | 页表复制/释放 |

### 5.2 推荐方案：渐进式双模式

**阶段1（已完成）**：
- PAE CPU检测
- PAE数据结构
- PAE分页模块（独立编译）

**阶段2（待开发）**：
- paging.c内部添加PAE分支
- 进程结构page_dir改为void*（偏移不变）
- 页错误处理适配PAE格式

**阶段3（待开发）**：
- 启动时自动选择PAE/非PAE
- 性能优化（大页、TLB）
- 64GB内存实际测试

### 5.3 风险控制

- 非PAE模式保持稳定，PAE作为可选
- 在独立分支开发完整切换
- 充分测试后再合并主线
- 当前86MB环境下PAE无实际收益

## 六、验证结果

### 6.1 自测通过

```
[PAE] selftest: start
[PAE] selftest PASS (data structures)
```

### 6.2 系统稳定

- 内核编译：1.7MB
- 用户态测试：14/14成功
- SWAP：8MB正常
- VVRAM：7/7自测通过

## 七、后续规划

1. **PAE完整页表切换**（独立分支）
   - paging.c双模式重构
   - 进程结构抽象
   - 页错误处理适配
2. **64GB内存支持验证**
   - QEMU配置大内存测试
   - 物理内存管理扩展
3. **性能优化**
   - 2MB大页默认启用
   - TLB刷新优化
   - 页表缓存
4. **NUMA支持**（远期）
   - 多节点内存管理
   - 本地内存优先分配
