# 赫普OS V6.77 内核资源扩展设计文档

> 版本: V1.0  
> 日期: 2026-09-24  
> 基于: V6.76 资源扩展（4GB内存/2TB存储/64MB显存）

---

## 1. 设计背景

V6.76完成了基础资源扩展（4GB物理内存/2TB存储/64MB显存），但仍有以下局限：

1. **32位寻址限制**: 4GB物理内存已是32位非PAE模式上限
2. **无磁盘swap**: 现有swap仅为内存模拟，无法利用大容量存储
3. **显存分区未实现**: 用户提出的"从存储划分显存分区"功能仅为空接口
4. **PAE未支持**: 无法利用36位寻址访问64GB内存

V6.77针对以上问题进行扩展。

---

## 2. PAE物理地址扩展

### 2.1 设计目标

- 支持36位物理地址寻址，最大64GB物理内存
- 三级页表结构（PDPT-PD-PT）
- 支持2MB大页
- 与现有32位分页模式兼容

### 2.2 页表结构

```
虚拟地址 (32位):
+--------+--------+--------+------------+
| PDPT   | PD     | PT     | Offset     |
| 2 bits | 9 bits | 9 bits | 12 bits    |
+--------+--------+--------+------------+
  31-30    29-21    20-12    11-0

PDPT (Page Directory Pointer Table): 4项 × 64位 = 32字节
PD   (Page Directory):               512项 × 64位 = 4KB
PT   (Page Table):                   512项 × 64位 = 4KB
```

### 2.3 实现文件

- `kernel/mm/pae.c` - PAE核心实现（280行）
- `kernel/include/pae.h` - 头文件

### 2.4 关键函数

| 函数 | 功能 |
|------|------|
| `pae_cpu_supported()` | 检测CPU是否支持PAE（CPUID EDX bit6） |
| `pae_enable()` | 设置CR4.PAE位启用PAE模式 |
| `pae_create_pdpt()` | 创建PDPT（32字节对齐） |
| `pae_create_pd()` | 创建PD（4KB） |
| `pae_create_pt()` | 创建PT（4KB） |
| `pae_map_page()` | 映射4KB页 |
| `pae_map_2mb_page()` | 映射2MB大页 |
| `pae_load_pdpt()` | 加载PDPT到CR3 |
| `pae_selftest()` | 自测（数据结构测试模式） |

### 2.5 当前状态

- ✅ 数据结构完整实现
- ✅ 自测通过（测试模式，不启用CR4.PAE）
- ⬜ 实际启用PAE模式（需重构内存管理）
- ⬜ 与现有paging.c集成

### 2.6 注意事项

PAE启用会改变页表格式，需谨慎：
1. 当前系统使用32位两级页表，启用PAE需切换到三级
2. 所有页表操作函数需适配PAE格式
3. 建议在独立分支开发，稳定后合并

---

## 3. 存储分区显存 (Storage-Backed VRAM)

### 3.1 设计目标

用户需求：从存储设备划分专用分区作为显存使用，支持：
- 无显卡/小显存设备使用系统存储作为显存
- 划出的显存与显卡显存统一管理
- 无显卡时显示为"Hepu显存分区"

### 3.2 分区格式

```
+------------------+ 0
|  VRAM分区头      | 4KB
|  (sbvr_header_t) |
+------------------+ 4096
|  分配位图        | total_blocks/8 字节
+------------------+ 4096 + bitmap_size
|  显存数据区      | 剩余空间
|  (4KB块为单位)   |
+------------------+
```

### 3.3 分区头部结构

```c
typedef struct {
    uint64_t magic;           /* "HEPUVRAM" */
    uint32_t version;         /* 版本号 */
    uint32_t header_size;     /* 头部大小 */
    uint64_t total_size;      /* 分区总大小 */
    uint64_t data_offset;     /* 数据区偏移 */
    uint64_t bitmap_offset;   /* 位图偏移 */
    uint32_t bitmap_size;     /* 位图大小 */
    uint32_t total_blocks;    /* 总块数 */
    uint32_t used_blocks;     /* 已用块数 */
    uint32_t block_size;      /* 块大小(4096) */
    char     label[32];       /* 分区标签 */
} sbvr_header_t;
```

### 3.4 实现文件

- `kernel/drivers/storage_vram.c` - 存储显存驱动（280行）
- `kernel/include/storage_vram.h` - 头文件

### 3.5 关键函数

| 函数 | 功能 |
|------|------|
| `sbvr_format()` | 在存储设备上格式化显存分区 |
| `sbvr_init()` | 挂载显存分区 |
| `sbvr_alloc_block()` | 分配4KB显存块 |
| `sbvr_free_block()` | 释放显存块 |
| `sbvr_read()` | 读取显存数据（带1块缓存） |
| `sbvr_write()` | 写入显存数据（带脏标记） |
| `sbvr_sync()` | 同步脏缓存和位图到存储 |
| `sbvr_stats()` | 获取统计信息 |

### 3.6 缓存机制

- 单块4KB读写缓存
- 脏标记（cache_dirty）
- 块切换时自动刷回
- sbvr_sync()强制同步

### 3.7 当前状态

- ✅ 分区格式定义
- ✅ 格式化/挂载/分配/释放/读写完整实现
- ✅ 单块读写缓存
- ⬜ 与VVRAM子系统集成（注册为虚拟显存区域）
- ⬜ 用户态工具（创建/挂载显存分区）

---

## 4. Swap页交换增强

### 4.1 V6.76版本局限

- 仅内存模拟swap（4MB区域）
- 512槽上限
- 无磁盘swap分区支持

### 4.2 V6.77改进

| 特性 | V6.76 | V6.77 |
|------|-------|-------|
| 槽位上限 | 512 | 2048 |
| 内存swap大小 | 4MB | 8MB（可降级4MB） |
| 磁盘swap | ❌ | ✅ 接口已定义 |
| 自动降级 | ❌ | ✅ 8MB失败降级4MB |

### 4.3 磁盘swap分区格式

```
+------------------+ 0
|  Swap头部        | 4KB
|  (swap_disk_header_t) |
+------------------+ 4096
|  槽位数据区      | 每槽4KB (8扇区)
+------------------+
```

### 4.4 关键函数

| 函数 | 功能 |
|------|------|
| `swap_init()` | 初始化内存swap（8MB，自动降级） |
| `swap_format_disk()` | 格式化磁盘swap分区 |
| `swap_init_disk()` | 挂载磁盘swap分区 |
| `swap_out()` | 换出页到swap（内存/磁盘自动选择） |
| `swap_in()` | 从swap换入页 |
| `swap_swapout_until()` | 内存压力触发批量换出 |
| `swap_total_kb()` | 获取总大小 |
| `swap_used_kb()` | 获取已用大小 |
| `swap_is_disk_mode()` | 是否磁盘模式 |

### 4.5 当前状态

- ✅ 内存swap扩展到8MB（2048槽）
- ✅ 自动降级机制（8MB→4MB）
- ✅ 磁盘swap分区接口定义
- ✅ 磁盘模式读写逻辑实现
- ⬜ 磁盘分区自动检测和挂载
- ⬜ 与页面回收机制深度集成

---

## 5. 验证结果

### 5.1 编译验证

- 内核编译通过，无错误
- 内核二进制: 1.7MB
- 新文件: pae.c/h, storage_vram.c/h, swap.c增强

### 5.2 运行验证 (QEMU + OVMF UEFI)

```
[SWAP] init OK (8MB)
[PAE] selftest: start
[PAE] selftest PASS (data structures)
[VVRAM] selftest: done (pass=7, fail=0)
```

### 5.3 用户态程序验证

所有14个用户态程序全部成功（rc=0）：
- linuxbrk.elf, linuxcrt.elf
- winhello.exe, winargs.exe
- macho_demo
- hello-rust.elf
- cat.elf, echo.elf, sysinfo.elf
- fs-client.elf, ipc-e2e-test.elf
- 其他4个程序

---

## 6. 修改文件清单

### 新增文件
| 文件 | 行数 | 功能 |
|------|------|------|
| `kernel/mm/pae.c` | 280 | PAE物理地址扩展 |
| `kernel/include/pae.h` | 50 | PAE头文件 |
| `kernel/drivers/storage_vram.c` | 280 | 存储分区显存驱动 |
| `kernel/include/storage_vram.h` | 30 | 存储显存头文件 |

### 修改文件
| 文件 | 修改内容 |
|------|----------|
| `kernel/mm/swap.c` | 槽位512→2048，新增磁盘swap接口，自动降级 |
| `kernel/include/swap.h` | 新增磁盘swap函数声明 |
| `kernel/arch/x86/kernel_init.c` | 新增PAE自测调用 |

---

## 7. 后续规划

### 7.1 短期 (V6.78)
1. **存储显存与VVRAM集成**: 将sbvr注册为VVRAM虚拟区域
2. **swap磁盘自动检测**: 启动时扫描swap分区并自动挂载
3. **PAE实际启用**: 在支持PAE的CPU上启用36位寻址

### 7.2 中期 (V6.80)
1. **HugePage 1GB大页**: 支持PDPE/G位
2. **共享内存增强**: 更大的shm段，tmpfs支持
3. **文件系统buffer cache**: 集成page_cache到VFS读写路径

### 7.3 长期 (V7.0)
1. **64位内核**: x86_64长模式支持
2. **NUMA支持**: 多节点内存管理
3. **持久内存**: NVDIMM支持

---

## 8. 经验总结

1. **渐进式扩展**: 先扩展上限，再优化机制，最后集成
2. **自动降级**: 资源扩展必须有降级机制，适配小内存设备
3. **双输出调试**: 关键初始化同时输出到VGA和串口，避免日志丢失
4. **测试模式**: PAE等危险功能先用测试模式验证数据结构，再实际启用
5. **工具丢失问题**: apt源常被重置，需记录国内镜像源配置

---

*本文档随V6.77开发持续更新。*
