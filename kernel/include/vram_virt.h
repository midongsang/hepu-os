#ifndef VRAM_VIRT_H
#define VRAM_VIRT_H

#include <stdint.h>

/* 显存虚拟化常量 */
#define VRAM_VIRT_MAX_CONTEXTS    16     /* 最大虚拟显存上下文数 */
#define VRAM_VIRT_MAX_MAPPINGS    64     /* 每个上下文最大映射数 */
#define VRAM_VIRT_PAGE_SIZE       4096   /* 显存页大小 */

/* 映射标志 */
#define VRAM_VIRT_MAP_READ    0x1  /* 可读 */
#define VRAM_VIRT_MAP_WRITE   0x2  /* 可写 */
#define VRAM_VIRT_MAP_EXEC    0x4  /* 可执行（显存通常不需要） */
#define VRAM_VIRT_MAP_SHARED  0x8  /* 共享映射 */

/* 虚拟显存映射 */
typedef struct {
    uint32_t vaddr;        /* 虚拟地址（进程显存地址空间） */
    uint32_t paddr;        /* 物理地址（真实显存地址） */
    uint32_t size;         /* 映射大小 */
    uint32_t flags;        /* 映射标志 */
    int      used;
} vram_virt_mapping_t;

/* 虚拟显存上下文（per-process） */
typedef struct {
    uint32_t id;           /* 上下文ID */
    uint32_t pid;          /* 所属进程ID */
    uint32_t total_vram;   /* 虚拟显存总量 */
    uint32_t used_vram;    /* 已使用显存 */
    uint32_t mapping_count; /* 映射数 */
    vram_virt_mapping_t mappings[VRAM_VIRT_MAX_MAPPINGS];
    int      active;       /* 是否活跃 */
    int      initialized;
} vram_virt_context_t;

/* 显存虚拟化统计 */
typedef struct {
    uint32_t context_count;    /* 活跃上下文数 */
    uint32_t total_mappings;   /* 总映射数 */
    uint32_t total_virtual;    /* 总虚拟显存 */
    uint32_t total_physical;   /* 总物理显存 */
    uint32_t page_faults;      /* 显存缺页次数 */
    uint32_t context_switches;  /* 上下文切换次数 */
} vram_virt_stats_t;

/* 初始化显存虚拟化 */
int vram_virt_init(void);

/* 创建虚拟显存上下文 */
int vram_virt_create_context(uint32_t pid, uint32_t vram_size);

/* 销毁虚拟显存上下文 */
int vram_virt_destroy_context(uint32_t context_id);

/* 切换当前虚拟显存上下文 */
int vram_virt_switch_context(uint32_t context_id);

/* 获取当前上下文 */
vram_virt_context_t *vram_virt_get_current(void);

/* 映射虚拟显存到物理显存 */
int vram_virt_map(uint32_t context_id, uint32_t vaddr, uint32_t size, uint32_t flags);

/* 取消映射 */
int vram_virt_unmap(uint32_t context_id, uint32_t vaddr);

/* 虚拟地址转物理地址 */
uint32_t vram_virt_virt_to_phys(uint32_t context_id, uint32_t vaddr);

/* 读取虚拟显存 */
int vram_virt_read(uint32_t context_id, uint32_t vaddr, void *buf, uint32_t len);

/* 写入虚拟显存 */
int vram_virt_write(uint32_t context_id, uint32_t vaddr, const void *buf, uint32_t len);

/* 获取统计信息 */
void vram_virt_get_stats(vram_virt_stats_t *stats);

/* 打印显存虚拟化信息 */
void vram_virt_dump(void);

/* 自测 */
void vram_virt_selftest(void);

#endif
