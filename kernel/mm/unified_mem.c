/* ============================================================================
 * 赫普OS - 统一内存（Unified Memory）
 * 基于《赫普OS内存、显存与存储资源管理详解》实现
 *
 * 实现：
 * - 整合物理内存、显存、压缩内存的统一分配接口
 * - 根据大小和用途自动选择最合适的内存类型
 * - 内存迁移：在不同类型内存之间迁移数据
 * - 统计信息：各类型内存使用情况
 * - 简化版实现（内存版，后续可与真实显存/zram集成）
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "unified_mem.h"
#include "kheap.h"
#include "vga.h"

#define UMEM_MAX_BLOCKS  256    /* 最大跟踪块数 */
#define UMEM_VRAM_THRESHOLD (4 * 1024 * 1024)  /* 大于4MB优先用显存 */
#define UMEM_COMPRESS_THRESHOLD (64 * 1024)     /* 大于64KB可考虑压缩 */

/* 全局统一内存管理器 */
static umem_block_t umem_blocks[UMEM_MAX_BLOCKS];
static umem_stats_t umem_stats;
static int umem_initialized = 0;

/* 查找空块槽位 */
static int umem_find_free_slot(void)
{
    for (int i = 0; i < UMEM_MAX_BLOCKS; i++) {
        if (!umem_blocks[i].used) return i;
    }
    return -1;
}

/* 查找指针对应的块 */
static int umem_find_block(void *ptr)
{
    for (int i = 0; i < UMEM_MAX_BLOCKS; i++) {
        if (umem_blocks[i].used && umem_blocks[i].ptr == ptr) return i;
    }
    return -1;
}

/* 根据大小和标志选择内存类型 */
static int umem_select_type(uint32_t size, uint32_t flags)
{
    /* 如果指定了特定类型，直接使用 */
    if (flags & UMEM_FLAG_VRAM) return UMEM_TYPE_VRAM;
    if (flags & UMEM_FLAG_COMPRESSED) return UMEM_TYPE_COMPRESSED;

    /* 大内存优先用显存（如果可用） */
    if (size >= UMEM_VRAM_THRESHOLD && (flags & UMEM_FLAG_VRAM)) {
        return UMEM_TYPE_VRAM;
    }

    /* 中等大小可考虑压缩内存 */
    if (size >= UMEM_COMPRESS_THRESHOLD && size < UMEM_VRAM_THRESHOLD) {
        /* 简化：默认用普通内存，压缩内存需要压缩/解压开销 */
        return UMEM_TYPE_NORMAL;
    }

    /* 默认普通内存 */
    return UMEM_TYPE_NORMAL;
}

/* ============================================================================
 * 公共接口
 * ========================================================================== */

int umem_init(void)
{
    if (umem_initialized) return 0;

    memset(umem_blocks, 0, sizeof(umem_blocks));
    memset(&umem_stats, 0, sizeof(umem_stats));

    /* 初始化各类型内存总量（简化版，假设各有16MB） */
    for (int i = 0; i < UMEM_TYPE_COUNT; i++) {
        umem_stats.total_bytes[i] = 16 * 1024 * 1024;  /* 16MB */
        umem_stats.total_blocks[i] = 4096;  /* 4096个4KB页 */
    }

    umem_initialized = 1;
    return 0;
}

void *umem_alloc(uint32_t size, uint32_t flags)
{
    if (!umem_initialized || size == 0) return NULL;

    int type = umem_select_type(size, flags);
    int slot = umem_find_free_slot();
    if (slot < 0) return NULL;

    /* 分配内存（简化版：所有类型都用kmalloc，后续可集成真实显存/zram） */
    void *ptr = kmalloc(size);
    if (!ptr) return NULL;

    memset(ptr, 0, size);

    umem_blocks[slot].ptr = ptr;
    umem_blocks[slot].size = size;
    umem_blocks[slot].type = type;
    umem_blocks[slot].used = 1;

    /* 更新统计 */
    umem_stats.used_blocks[type]++;
    umem_stats.used_bytes[type] += size;
    umem_stats.alloc_count++;

    return ptr;
}

void umem_free(void *ptr)
{
    if (!umem_initialized || !ptr) return;

    int slot = umem_find_block(ptr);
    if (slot < 0) return;

    int type = umem_blocks[slot].type;
    uint32_t size = umem_blocks[slot].size;

    kfree(ptr);

    memset(&umem_blocks[slot], 0, sizeof(umem_block_t));

    /* 更新统计 */
    umem_stats.used_blocks[type]--;
    umem_stats.used_bytes[type] -= size;
    umem_stats.free_count++;
}

void *umem_realloc(void *ptr, uint32_t new_size)
{
    if (!umem_initialized) return NULL;
    if (!ptr) return umem_alloc(new_size, UMEM_FLAG_ANY);
    if (new_size == 0) {
        umem_free(ptr);
        return NULL;
    }

    int slot = umem_find_block(ptr);
    if (slot < 0) return NULL;

    uint32_t old_size = umem_blocks[slot].size;
    int type = umem_blocks[slot].type;

    /* 分配新内存 */
    void *new_ptr = kmalloc(new_size);
    if (!new_ptr) return NULL;

    /* 复制数据 */
    uint32_t copy_size = old_size < new_size ? old_size : new_size;
    memcpy(new_ptr, ptr, copy_size);
    if (new_size > old_size) {
        memset((uint8_t *)new_ptr + old_size, 0, new_size - old_size);
    }

    /* 释放旧内存 */
    kfree(ptr);

    /* 更新块信息 */
    umem_blocks[slot].ptr = new_ptr;
    umem_blocks[slot].size = new_size;

    /* 更新统计 */
    umem_stats.used_bytes[type] -= old_size;
    umem_stats.used_bytes[type] += new_size;

    return new_ptr;
}

int umem_get_type(void *ptr)
{
    if (!umem_initialized || !ptr) return -1;
    int slot = umem_find_block(ptr);
    if (slot < 0) return -1;
    return umem_blocks[slot].type;
}

uint32_t umem_get_size(void *ptr)
{
    if (!umem_initialized || !ptr) return 0;
    int slot = umem_find_block(ptr);
    if (slot < 0) return 0;
    return umem_blocks[slot].size;
}

int umem_migrate(void *ptr, int new_type)
{
    if (!umem_initialized || !ptr) return -1;
    if (new_type < 0 || new_type >= UMEM_TYPE_COUNT) return -2;

    int slot = umem_find_block(ptr);
    if (slot < 0) return -3;

    int old_type = umem_blocks[slot].type;
    if (old_type == new_type) return 0;  /* 已经是目标类型 */

    uint32_t size = umem_blocks[slot].size;

    /* 简化版：所有类型都用kmalloc，只改变类型标记，不重新分配 */
    /* （真实实现需要在不同内存类型之间迁移数据） */
    umem_blocks[slot].type = new_type;

    /* 更新统计 */
    umem_stats.used_blocks[old_type]--;
    umem_stats.used_bytes[old_type] -= size;
    umem_stats.used_blocks[new_type]++;
    umem_stats.used_bytes[new_type] += size;
    umem_stats.migrate_count++;

    return 0;
}

void umem_get_stats(umem_stats_t *stats)
{
    if (!stats) return;
    memcpy(stats, &umem_stats, sizeof(umem_stats_t));
}

/* ============================================================================
 * 自测
 * ========================================================================== */
void umem_selftest(void)
{
    extern void serial_puts(uint16_t, const char *);
    extern void serial_puthex(uint16_t, uint32_t);
    #define ULOG(s) serial_puts(0x3F8, s)

    ULOG("[UMEM] selftest: start\n");
    int pass = 0, fail = 0;

    /* 测试1：初始化 */
    int ret = umem_init();
    if (ret == 0) {
        ULOG("[UMEM] test1 init: PASS\n");
        pass++;
    } else {
        ULOG("[UMEM] test1 init: FAIL\n");
        fail++;
    }

    /* 测试2：分配普通内存 */
    void *p1 = umem_alloc(1024, UMEM_FLAG_NORMAL);
    if (p1) {
        int type = umem_get_type(p1);
        if (type == UMEM_TYPE_NORMAL) {
            ULOG("[UMEM] test2 alloc normal: PASS\n");
            pass++;
        } else {
            ULOG("[UMEM] test2 alloc normal: FAIL (type=");
            serial_puthex(0x3F8, type);
            ULOG(")\n");
            fail++;
        }
    } else {
        ULOG("[UMEM] test2 alloc normal: FAIL\n");
        fail++;
    }

    /* 测试3：分配显存类型 */
    void *p2 = umem_alloc(4096, UMEM_FLAG_VRAM);
    if (p2) {
        int type = umem_get_type(p2);
        if (type == UMEM_TYPE_VRAM) {
            ULOG("[UMEM] test3 alloc vram: PASS\n");
            pass++;
        } else {
            ULOG("[UMEM] test3 alloc vram: FAIL (type=");
            serial_puthex(0x3F8, type);
            ULOG(")\n");
            fail++;
        }
    } else {
        ULOG("[UMEM] test3 alloc vram: FAIL\n");
        fail++;
    }

    /* 测试4：内存迁移（普通→显存） */
    ret = umem_migrate(p1, UMEM_TYPE_VRAM);
    if (ret == 0) {
        int type = umem_get_type(p1);
        if (type == UMEM_TYPE_VRAM) {
            ULOG("[UMEM] test4 migrate normal->vram: PASS\n");
            pass++;
        } else {
            ULOG("[UMEM] test4 migrate: FAIL (type=");
            serial_puthex(0x3F8, type);
            ULOG(")\n");
            fail++;
        }
    } else {
        ULOG("[UMEM] test4 migrate: FAIL\n");
        fail++;
    }

    /* 测试5：realloc */
    void *p3 = umem_alloc(512, UMEM_FLAG_NORMAL);
    void *p3_new = umem_realloc(p3, 2048);
    if (p3_new) {
        uint32_t size = umem_get_size(p3_new);
        if (size == 2048) {
            ULOG("[UMEM] test5 realloc: PASS (size=2048)\n");
            pass++;
        } else {
            ULOG("[UMEM] test5 realloc: FAIL (size=");
            serial_puthex(0x3F8, size);
            ULOG(")\n");
            fail++;
        }
    } else {
        ULOG("[UMEM] test5 realloc: FAIL\n");
        fail++;
    }

    /* 测试6：释放内存 */
    umem_free(p1);
    umem_free(p2);
    umem_free(p3_new);
    umem_stats_t stats;
    umem_get_stats(&stats);
    if (stats.alloc_count >= 3 && stats.free_count >= 3) {
        ULOG("[UMEM] test6 free: PASS (alloc=");
        serial_puthex(0x3F8, stats.alloc_count);
        ULOG(", free=");
        serial_puthex(0x3F8, stats.free_count);
        ULOG(")\n");
        pass++;
    } else {
        ULOG("[UMEM] test6 free: FAIL\n");
        fail++;
    }

    /* 测试7：统计信息 */
    umem_get_stats(&stats);
    if (stats.total_bytes[UMEM_TYPE_NORMAL] > 0 &&
        stats.total_bytes[UMEM_TYPE_VRAM] > 0) {
        ULOG("[UMEM] test7 stats: PASS\n");
        pass++;
    } else {
        ULOG("[UMEM] test7 stats: FAIL\n");
        fail++;
    }

    ULOG("[UMEM] selftest: done (pass=");
    serial_puthex(0x3F8, pass);
    ULOG(", fail=");
    serial_puthex(0x3F8, fail);
    ULOG(")\n");

    #undef ULOG
}
