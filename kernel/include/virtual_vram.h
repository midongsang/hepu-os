#ifndef VIRTUAL_VRAM_H
#define VIRTUAL_VRAM_H

#include <stdint.h>

/* 虚拟显存常量 */
#define VVRAM_MAGIC          0x5656524D  /* "VVRM" */
#define VVRAM_BLOCK_SIZE     4096
#define VVRAM_DEFAULT_SIZE   (4 * 1024 * 1024)  /* V6.99: 默认4MB虚拟显存(适应小内存系统，原64MB) */
#define VVRAM_NAME_LEN       32

/* 显存技术类型 */
#define VVRAM_TECH_NONE      0  /* 无显存技术 */
#define VVRAM_TECH_VGA       1  /* VGA显存 */
#define VVRAM_TECH_VBE       2  /* VBE/LFB显存 */
#define VVRAM_TECH_HEPU      3  /* Hepu虚拟显存技术 */
#define VVRAM_TECH_STORAGE   4  /* V6.78: 存储分区显存技术 */

/* 显存区域信息 */
typedef struct {
    uint32_t type;            /* 技术类型 */
    uint32_t phys_addr;       /* 物理地址（虚拟显存为0） */
    uint32_t size;            /* 总大小 */
    uint32_t used;            /* 已使用 */
    char     name[VVRAM_NAME_LEN];  /* 名称 */
    int      is_virtual;      /* 是否虚拟显存 */
} vvram_region_info_t;

/* 虚拟显存统计 */
typedef struct {
    uint32_t total_vram;      /* 总显存（物理+虚拟） */
    uint32_t used_vram;       /* 已使用显存 */
    uint32_t physical_total;   /* 物理显存总量 */
    uint32_t physical_used;    /* 物理显存已用 */
    uint32_t virtual_total;    /* 虚拟显存总量 */
    uint32_t virtual_used;     /* 虚拟显存已用 */
    uint32_t region_count;     /* 显存区域数 */
    int      has_gpu;          /* 是否有物理显卡 */
} vvram_stats_t;

/* 初始化虚拟显存系统 */
int vvram_init(uint32_t virtual_size);

/* 从存储设备创建虚拟显存分区 */
int vvram_create_partition(uint32_t dev, uint32_t start_block, uint32_t size_blocks);

/* 挂载虚拟显存分区 */
int vvram_mount_partition(uint32_t dev, uint32_t start_block);

/* V6.78: 挂载存储-backed显存分区 (使用sbvr驱动) */
int vvram_mount_storage_vram(uint32_t dev_id, uint64_t start_lba, uint64_t total_sectors);

/* 分配显存（优先物理显存，不足时用虚拟显存），返回句柄 */
int vvram_alloc(uint32_t size, uint32_t flags);

/* 释放显存 */
void vvram_free(int handle);

/* 读取显存数据到系统内存 */
int vvram_read(int handle, uint32_t offset, void *buf, uint32_t len);

/* 写入显存数据 */
int vvram_write(int handle, uint32_t offset, const void *buf, uint32_t len);

/* 获取显存区域信息 */
int vvram_get_region_info(uint32_t index, vvram_region_info_t *info);

/* 获取显存统计 */
void vvram_get_stats(vvram_stats_t *stats);

/* 打印显存信息（包括物理显存和Hepu虚拟显存） */
void vvram_dump(void);

/* 自测 */
void vvram_selftest(void);

/* V6.80: 在IDE初始化后重试自动挂载存储显存 */
void vvram_try_auto_mount(void);

#endif
