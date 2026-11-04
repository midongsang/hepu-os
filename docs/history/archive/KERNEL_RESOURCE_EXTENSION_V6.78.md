---
title: KERNEL_RESOURCE_EXTENSION_V6.78
status: frozen
sys_version: 0.13.65
updated: 2026-10-24
owner: history
source_of_truth: null
supersedes: null
---

# 赫普OS 内核资源扩展设计文档 V6.78

## 概述

V6.78在V6.77基础上完成存储显存集成与swap磁盘自动检测，实现存储-backed显存和磁盘swap分区的自动识别。

## 一、存储-backed显存（SBVR）集成

### 1.1 设计目标

- 无独立显卡或显存不足时，从存储分区分出显存分区
- 与VVRAM接口统一，对上层透明
- 支持Hepu显存分区标识，显示技术名称和大小

### 1.2 VVRAM接口扩展

```c
// virtual_vram.h
typedef enum {
    VVRAM_TECH_RAM = 0,      // 内存-backed
    VVRAM_TECH_DEVICE = 1,   // 设备显存
    VVRAM_TECH_SHARED = 2,   // 共享内存
    VVRAM_TECH_COMPRESSED = 3, // 压缩显存
    VVRAM_TECH_STORAGE = 4,  // 存储-backed (V6.78新增)
} vvram_tech_t;

typedef struct {
    ...
    int is_storage;  // V6.78: 是否存储-backed
    ...
} vvram_region_t;

int vvram_mount_storage_vram(uint32_t dev_id, uint64_t start_lba, uint64_t total_sectors);
```

### 1.3 读写路径

```
vvram_read()
  ├─ is_storage == 0 → 内存读写（原路径）
  └─ is_storage == 1 → sbvr_read() 分块读写
       ├─ 计算块索引和块内偏移
       ├─ 调用ide_read_sectors读取
       └─ 缓存优化

vvram_write()
  ├─ is_storage == 0 → 内存读写（原路径）
  └─ is_storage == 1 → sbvr_write() 分块读写
       ├─ 计算块索引和块内偏移
       ├─ 调用ide_write_sectors写入
       └─ 延迟写回
```

### 1.4 SBVR驱动（storage_vram.c）

| 函数 | 功能 |
|------|------|
| sbvr_init | 初始化存储显存驱动 |
| sbvr_format | 格式化存储显存分区（头部+位图） |
| sbvr_alloc_block | 分配显存块 |
| sbvr_free_block | 释放显存块 |
| sbvr_read | 读取数据 |
| sbvr_write | 写入数据 |
| sbvr_sync | 同步缓存到磁盘 |
| sbvr_stats | 获取统计信息 |

### 1.5 分区格式

```
+------------------+ 0
|  SBVR Header     | 1扇区
|  - magic "SBVR"  |
|  - version       |
|  - total_blocks  |
|  - block_size    |
+------------------+ 1
|  位图区域         | N扇区
|  (每bit表示一个块) |
+------------------+ 1+N
|  数据区域         |
|  (块大小=64KB)    |
+------------------+
```

## 二、Swap磁盘分区自动检测

### 2.1 设计目标

- 启动时自动扫描MBR分区表
- 识别Linux swap(0x82)和Hepu swap分区
- 未找到时回退内存swap
- 自动降级（8MB失败降级4MB）

### 2.2 检测流程

```
swap_auto_detect()
  ├─ 读取MBR（LBA 0）
  ├─ 检查magic 0xAA55
  ├─ 遍历4个分区表项
  │   ├─ type == 0x82 (Linux swap) → 找到
  │   └─ type == Hepu自定义 → 找到
  ├─ 找到 → swap_init_disk(dev, start_lba, size)
  └─ 未找到 → 回退内存swap (8MB)
```

### 2.3 初始化顺序修正（BUG-26）

**问题**：原swap_auto_detect在hepu_mem_init中调用，此时IDE未初始化。

**修复**：移到ide_init之后调用。

```c
// init_sequence.c 正确顺序
ide_init();           // 先初始化IDE
...
swap_auto_detect();   // 再检测磁盘swap
```

## 三、BUG-26：IDE函数参数顺序错误

### 3.1 问题描述

错误使用：
```c
ide_read_sectors(lba, count, buf);  // 错误！
```

实际签名：
```c
ide_read_sectors(uint32_t lba, uint8_t *buffer, uint32_t count);
```

### 3.2 影响范围

- swap.c中所有IDE调用
- storage_vram.c中所有IDE调用

### 3.3 返回值检查错误

错误检查：
```c
if (ide_read_sectors(...) != 0) { ... }  // 错误！返回扇区数，成功返回count
```

正确检查：
```c
if (ide_read_sectors(lba, buf, count) < count) { ... }
```

### 3.4 修复清单

| 文件 | 修改点 |
|------|--------|
| kernel/mm/swap.c | swap_init_disk, swap_format_disk, swap_read_slot, swap_write_slot |
| kernel/drivers/storage_vram.c | sbvr_read, sbvr_write, sbvr_format, sbvr_sync |

## 四、验证结果

### 4.1 启动日志

```
[SWAP] init OK (8MB)
[SWAP] auto-detecting disk swap...
[SWAP] no disk swap partition found, using memory swap
[PAE] selftest PASS (data structures)
[VRAM] selftest: done (pass=7, fail=0)
```

### 4.2 用户态测试

14个用户态程序全部rc=0。

## 五、后续规划

1. 存储显存自动挂载（启动时检测SBVR分区）
2. swap分区自动创建工具
3. 存储显存与内存显存的混合使用策略
4. 显存分区管理工具（创建/格式化/调整大小）
