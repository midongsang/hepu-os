#ifndef VRAM_H
#define VRAM_H

#include <stdint.h>

/* 显存区域类型 */
#define VRAM_REGION_VGA_TEXT   0  /* VGA文本模式：0xB8000-0xBFFFF (32KB) */
#define VRAM_REGION_VGA_GFX    1  /* VGA图形模式：0xA0000-0xAFFFF (64KB) */
#define VRAM_REGION_EXTENDED   2  /* 扩展显存（VBE/LFB） */

/* 显存分配标志 */
#define VRAM_ALIGN_4K   0x1  /* 4KB对齐 */
#define VRAM_ALIGN_64K  0x2  /* 64KB对齐（适合位平面） */

/* 初始化显存分配器 */
void vram_init(void);

/* 分配显存，返回物理地址，失败返回0 */
uint32_t vram_alloc(uint32_t size, uint32_t flags);

/* 释放显存 */
void vram_free(uint32_t addr, uint32_t size);

/* 注册扩展显存区域 */
int vram_register_region(uint32_t phys_addr, uint32_t size, int type);

/* 统计信息 */
void vram_stats(uint32_t *total, uint32_t *used, uint32_t *free);

/* 打印显存区域信息 */
void vram_dump(void);

/* 自测 */
void vram_selftest(void);

#endif
