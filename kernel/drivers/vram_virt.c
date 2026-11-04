/* ============================================================================
 * 赫普OS - 显存虚拟化（VRAM Virtualization）
 *
 * 功能：
 * - per-process虚拟显存上下文，地址空间隔离
 * - 虚拟显存到物理显存的映射
 * - 显存上下文切换
 * - 虚拟地址到物理地址转换
 * - 显存读写（通过虚拟地址）
 * - 统计信息
 *
 * 设计：
 * - 每个进程有独立的虚拟显存地址空间
 * - 虚拟显存通过页表映射到物理显存
 * - 上下文切换时切换页表（类似内存虚拟化）
 * - 简化版：用映射表代替页表，无缺页处理
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "vram_virt.h"
#include "vram.h"
#include "kheap.h"
#include "vga.h"

/* 全局状态 */
static vram_virt_context_t contexts[VRAM_VIRT_MAX_CONTEXTS];
static vram_virt_stats_t vv_stats;
static uint32_t current_context = 0xFFFFFFFF;  /* 当前活跃上下文 */
static uint32_t next_context_id = 1;
static uint32_t physical_vram_base = 0xA0000;  /* 物理显存基址（VGA图形） */
static uint32_t physical_vram_size = 64 * 1024; /* 物理显存大小（64KB） */
static uint32_t physical_vram_used = 0;
static int vram_virt_initialized = 0;

/* ============================================================================
 * 内部函数
 * ========================================================================== */

/* 分配物理显存（简化版：线性分配） */
static uint32_t vram_virt_alloc_physical(uint32_t size)
{
    if (physical_vram_used + size > physical_vram_size) return 0;
    uint32_t addr = physical_vram_base + physical_vram_used;
    physical_vram_used += size;
    return addr;
}

/* 查找上下文 */
static vram_virt_context_t *vram_virt_find_context(uint32_t context_id)
{
    for (uint32_t i = 0; i < VRAM_VIRT_MAX_CONTEXTS; i++) {
        if (contexts[i].initialized && contexts[i].id == context_id) {
            return &contexts[i];
        }
    }
    return NULL;
}

/* 查找映射 */
static vram_virt_mapping_t *vram_virt_find_mapping(vram_virt_context_t *ctx, uint32_t vaddr)
{
    for (uint32_t i = 0; i < VRAM_VIRT_MAX_MAPPINGS; i++) {
        vram_virt_mapping_t *m = &ctx->mappings[i];
        if (m->used && vaddr >= m->vaddr && vaddr < m->vaddr + m->size) {
            return m;
        }
    }
    return NULL;
}

/* ============================================================================
 * 公共接口
 * ========================================================================== */

int vram_virt_init(void)
{
    if (vram_virt_initialized) return 0;

    memset(contexts, 0, sizeof(contexts));
    memset(&vv_stats, 0, sizeof(vv_stats));
    current_context = 0xFFFFFFFF;
    next_context_id = 1;
    physical_vram_used = 0;

    vv_stats.total_physical = physical_vram_size;
    vram_virt_initialized = 1;

    return 0;
}

int vram_virt_create_context(uint32_t pid, uint32_t vram_size)
{
    if (!vram_virt_initialized) return -1;

    /* 查找空闲上下文槽 */
    vram_virt_context_t *ctx = NULL;
    for (uint32_t i = 0; i < VRAM_VIRT_MAX_CONTEXTS; i++) {
        if (!contexts[i].initialized) {
            ctx = &contexts[i];
            break;
        }
    }
    if (!ctx) return -2;  /* 上下文已满 */

    memset(ctx, 0, sizeof(vram_virt_context_t));
    ctx->id = next_context_id++;
    ctx->pid = pid;
    ctx->total_vram = vram_size;
    ctx->used_vram = 0;
    ctx->mapping_count = 0;
    ctx->active = 1;
    ctx->initialized = 1;

    vv_stats.context_count++;
    vv_stats.total_virtual += vram_size;

    return ctx->id;
}

int vram_virt_destroy_context(uint32_t context_id)
{
    if (!vram_virt_initialized) return -1;

    vram_virt_context_t *ctx = vram_virt_find_context(context_id);
    if (!ctx) return -2;

    /* 释放所有映射的物理显存（简化版：不释放，因为线性分配） */
    vv_stats.total_mappings -= ctx->mapping_count;
    vv_stats.total_virtual -= ctx->total_vram;
    vv_stats.context_count--;

    if (current_context == context_id) {
        current_context = 0xFFFFFFFF;
    }

    memset(ctx, 0, sizeof(vram_virt_context_t));
    return 0;
}

int vram_virt_switch_context(uint32_t context_id)
{
    if (!vram_virt_initialized) return -1;

    if (context_id != 0xFFFFFFFF) {
        vram_virt_context_t *ctx = vram_virt_find_context(context_id);
        if (!ctx) return -2;
    }

    current_context = context_id;
    vv_stats.context_switches++;
    return 0;
}

vram_virt_context_t *vram_virt_get_current(void)
{
    if (current_context == 0xFFFFFFFF) return NULL;
    return vram_virt_find_context(current_context);
}

int vram_virt_map(uint32_t context_id, uint32_t vaddr, uint32_t size, uint32_t flags)
{
    if (!vram_virt_initialized) return -1;

    vram_virt_context_t *ctx = vram_virt_find_context(context_id);
    if (!ctx) return -2;

    /* 检查虚拟地址空间是否足够 */
    if (vaddr + size > ctx->total_vram) return -3;

    /* 检查是否已映射 */
    if (vram_virt_find_mapping(ctx, vaddr)) return -4;

    /* 查找空闲映射槽 */
    vram_virt_mapping_t *m = NULL;
    for (uint32_t i = 0; i < VRAM_VIRT_MAX_MAPPINGS; i++) {
        if (!ctx->mappings[i].used) {
            m = &ctx->mappings[i];
            break;
        }
    }
    if (!m) return -5;  /* 映射表已满 */

    /* 分配物理显存 */
    uint32_t paddr = vram_virt_alloc_physical(size);
    if (!paddr) return -6;  /* 物理显存不足 */

    m->vaddr = vaddr;
    m->paddr = paddr;
    m->size = size;
    m->flags = flags;
    m->used = 1;

    ctx->used_vram += size;
    ctx->mapping_count++;
    vv_stats.total_mappings++;

    return 0;
}

int vram_virt_unmap(uint32_t context_id, uint32_t vaddr)
{
    if (!vram_virt_initialized) return -1;

    vram_virt_context_t *ctx = vram_virt_find_context(context_id);
    if (!ctx) return -2;

    vram_virt_mapping_t *m = vram_virt_find_mapping(ctx, vaddr);
    if (!m) return -3;

    ctx->used_vram -= m->size;
    ctx->mapping_count--;
    vv_stats.total_mappings--;

    memset(m, 0, sizeof(vram_virt_mapping_t));
    return 0;
}

uint32_t vram_virt_virt_to_phys(uint32_t context_id, uint32_t vaddr)
{
    if (!vram_virt_initialized) return 0;

    vram_virt_context_t *ctx = vram_virt_find_context(context_id);
    if (!ctx) return 0;

    vram_virt_mapping_t *m = vram_virt_find_mapping(ctx, vaddr);
    if (!m) {
        vv_stats.page_faults++;
        return 0;  /* 缺页 */
    }

    return m->paddr + (vaddr - m->vaddr);
}

int vram_virt_read(uint32_t context_id, uint32_t vaddr, void *buf, uint32_t len)
{
    if (!vram_virt_initialized || !buf) return -1;

    uint32_t paddr = vram_virt_virt_to_phys(context_id, vaddr);
    if (!paddr) return -2;  /* 缺页 */

    /* 简化版：直接从物理地址读取（真实实现需要检查权限） */
    memcpy(buf, (void *)paddr, len);
    return len;
}

int vram_virt_write(uint32_t context_id, uint32_t vaddr, const void *buf, uint32_t len)
{
    if (!vram_virt_initialized || !buf) return -1;

    uint32_t paddr = vram_virt_virt_to_phys(context_id, vaddr);
    if (!paddr) return -2;  /* 缺页 */

    /* 简化版：直接写入物理地址（真实实现需要检查写权限） */
    memcpy((void *)paddr, buf, len);
    return len;
}

void vram_virt_get_stats(vram_virt_stats_t *stats)
{
    if (!stats) return;
    memcpy(stats, &vv_stats, sizeof(vram_virt_stats_t));
}

void vram_virt_dump(void)
{
    vram_virt_stats_t stats;
    vram_virt_get_stats(&stats);

    vga_printf("=== VRAM Virtualization ===\n");
    vga_printf("[Contexts] Active: %d, Switches: %d\n",
                stats.context_count, stats.context_switches);
    vga_printf("[Mappings] Total: %d, Page faults: %d\n",
                stats.total_mappings, stats.page_faults);
    vga_printf("[Memory] Virtual: %d KB, Physical: %d KB\n",
                stats.total_virtual / 1024, stats.total_physical / 1024);

    for (uint32_t i = 0; i < VRAM_VIRT_MAX_CONTEXTS; i++) {
        if (contexts[i].initialized) {
            vga_printf("  [CTX %d] PID=%d, VRAM=%d KB, Used=%d KB, Maps=%d%s\n",
                        contexts[i].id, contexts[i].pid,
                        contexts[i].total_vram / 1024,
                        contexts[i].used_vram / 1024,
                        contexts[i].mapping_count,
                        (current_context == contexts[i].id) ? " [ACTIVE]" : "");
        }
    }
    vga_printf("===========================\n");
}

/* ============================================================================
 * 自测
 * ========================================================================== */
void vram_virt_selftest(void)
{
    extern void serial_puts(uint16_t, const char *);
    extern void serial_puthex(uint16_t, uint32_t);
    #define VVLOG(s) serial_puts(0x3F8, s)

    VVLOG("[VRAM-VIRT] selftest: start\n");
    int pass = 0, fail = 0;

    /* 测试1：初始化 */
    int ret = vram_virt_init();
    if (ret == 0) {
        vram_virt_stats_t stats;
        vram_virt_get_stats(&stats);
        if (stats.total_physical > 0) {
            VVLOG("[VRAM-VIRT] test1 init: PASS (physical=");
            serial_puthex(0x3F8, stats.total_physical / 1024);
            VVLOG("KB)\n");
            pass++;
        } else {
            VVLOG("[VRAM-VIRT] test1 init: FAIL\n");
            fail++;
        }
    } else {
        VVLOG("[VRAM-VIRT] test1 init: FAIL\n");
        fail++;
    }

    /* 测试2：创建上下文 */
    int ctx1 = vram_virt_create_context(100, 256 * 1024);  /* PID=100, 256KB虚拟显存 */
    if (ctx1 > 0) {
        VVLOG("[VRAM-VIRT] test2 create ctx: PASS (id=");
        serial_puthex(0x3F8, ctx1);
        VVLOG(")\n");
        pass++;
    } else {
        VVLOG("[VRAM-VIRT] test2 create ctx: FAIL\n");
        fail++;
    }

    /* 测试3：映射虚拟显存 */
    ret = vram_virt_map(ctx1, 0, 4096, VRAM_VIRT_MAP_READ | VRAM_VIRT_MAP_WRITE);
    if (ret == 0) {
        VVLOG("[VRAM-VIRT] test3 map 4KB: PASS\n");
        pass++;
    } else {
        VVLOG("[VRAM-VIRT] test3 map 4KB: FAIL\n");
        fail++;
    }

    /* 测试4：虚拟地址转物理地址 */
    uint32_t paddr = vram_virt_virt_to_phys(ctx1, 100);
    if (paddr != 0) {
        VVLOG("[VRAM-VIRT] test4 virt2phys: PASS (paddr=0x");
        serial_puthex(0x3F8, paddr);
        VVLOG(")\n");
        pass++;
    } else {
        VVLOG("[VRAM-VIRT] test4 virt2phys: FAIL\n");
        fail++;
    }

    /* 测试5：上下文切换 */
    int ctx2 = vram_virt_create_context(200, 128 * 1024);
    ret = vram_virt_switch_context(ctx2);
    if (ret == 0 && vram_virt_get_current() != NULL) {
        VVLOG("[VRAM-VIRT] test5 switch ctx: PASS\n");
        pass++;
    } else {
        VVLOG("[VRAM-VIRT] test5 switch ctx: FAIL\n");
        fail++;
    }

    /* 测试6：未映射地址缺页 */
    uint32_t fault_paddr = vram_virt_virt_to_phys(ctx2, 0x100000);
    vram_virt_stats_t stats;
    vram_virt_get_stats(&stats);
    if (fault_paddr == 0 && stats.page_faults >= 1) {
        VVLOG("[VRAM-VIRT] test6 page fault: PASS (faults=");
        serial_puthex(0x3F8, stats.page_faults);
        VVLOG(")\n");
        pass++;
    } else {
        VVLOG("[VRAM-VIRT] test6 page fault: FAIL\n");
        fail++;
    }

    /* 测试7：销毁上下文 */
    ret = vram_virt_destroy_context(ctx1);
    ret |= vram_virt_destroy_context(ctx2);
    vram_virt_get_stats(&stats);
    if (ret == 0 && stats.context_count == 0) {
        VVLOG("[VRAM-VIRT] test7 destroy ctx: PASS (contexts=0)\n");
        pass++;
    } else {
        VVLOG("[VRAM-VIRT] test7 destroy ctx: FAIL\n");
        fail++;
    }

    VVLOG("[VRAM-VIRT] selftest: done (pass=");
    serial_puthex(0x3F8, pass);
    VVLOG(", fail=");
    serial_puthex(0x3F8, fail);
    VVLOG(")\n");

    #undef VVLOG
}
