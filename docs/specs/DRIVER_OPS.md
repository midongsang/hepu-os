---
title: 赫普OS 驱动操作接口（DRIVER_OPS）
status: active
sys_version: 0.54.11
updated: 2026-11-01
owner: specs
source_of_truth: kernel/shared/drivers/driver_framework.h
---

# 驱动操作接口（DRIVER_OPS）

> 存储/网络/输入等驱动通过统一 ops 结构注册到驱动框架, storage_hal 等上层经 ops 调用。

## 驱动描述符与操作集

见 `kernel/shared/drivers/driver_framework.h` 中 drv_desc_t / drv_ops_t 定义:
- read/write: 块设备扇区读写 (lba, buffer, count*512)
- 上层: storage_hal_read/write (经 blk_cache64 统一缓存)
- 设备选择: storage_hal_set_device(0=IDE, 1=USB)

## 约束

- ops 回调必须可重入; 中断上下文不得阻塞
- 新驱动注册后必须过 storage_hal 统一层(缓存+选择)
- 驱动实现变更需同步本文件与 SYSCALL_ABI.md

