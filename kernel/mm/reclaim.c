/* ============================================================================
 * 赫普OS - 内存回收子系统 (V1.02新增)
 * 实现：LRU页回收、内存压力检测、回收水位管理
 * ========================================================================== */
#include <stdint.h>
#include <stddef.h>
#include "reclaim.h"
#include "mm.h"
#include "vga.h"
#include "string.h"

#define LRU_MAX_PAGES 256
#define RECLAIM_MAGIC 0x52434C4D  /* "RCLM" */

/* 内存压力等级：定义见reclaim.h（宏） */

/* LRU页节点 */
typedef struct lru_page {
    uint8_t  used;
    uint8_t  referenced;    /* 引用位（模拟硬件访问位） */
    uint32_t phys_addr;
    uint32_t age;           /* 驻留时间 */
    struct lru_page *next;
    struct lru_page *prev;
} lru_page_t;

static lru_page_t lru_pages[LRU_MAX_PAGES];
static lru_page_t *lru_head = NULL;   /* 最近使用 */
static lru_page_t *lru_tail = NULL;   /* 最久未使用 */
static uint32_t lru_count = 0;
static uint32_t reclaim_initialized = 0;

/* 内存水位 */
static uint32_t watermarks[3];   /* 正常/高/严重阈值(空闲页数) */
static uint32_t total_pages_cache;
static uint32_t reclaimed_total = 0;
static uint32_t reclaim_calls = 0;
static uint32_t pressure_history[4];

/* 更新内存压力 */
static int update_pressure(void)
{
    uint32_t total, used, free_pages;
    mm_stats(&total, &used, &free_pages);
    total_pages_cache = total;

    int p = MEM_OK;
    if (free_pages <= watermarks[2]) p = MEM_PRESSURE_CRITICAL;
    else if (free_pages <= watermarks[1]) p = MEM_PRESSURE_HIGH;
    else if (free_pages <= watermarks[0]) p = MEM_PRESSURE_NORMAL;

    pressure_history[p]++;
    return p;
}

/* 初始化内存回收子系统 */
void reclaim_init(void)
{
    memset(lru_pages, 0, sizeof(lru_pages));
    lru_head = lru_tail = NULL;
    lru_count = 0;
    reclaimed_total = 0;
    reclaim_calls = 0;
    memset(pressure_history, 0, sizeof(pressure_history));
    reclaim_initialized = 1;

    /* 默认水位：空闲页 < 5%/3%/1% 时触发各等级压力 */
    uint32_t total, used, free_pages;
    mm_stats(&total, &used, &free_pages);
    total_pages_cache = total;
    watermarks[0] = total / 20;   /* 5% */
    watermarks[1] = total / 33;   /* 3% */
    watermarks[2] = total / 100;  /* 1% */

    vga_printf("[RECLAIM] mem reclaim subsystem init done(LRU=%d, watermarks:%d/%d/%d)\n",
               LRU_MAX_PAGES, watermarks[0], watermarks[1], watermarks[2]);
}

/* 注册一个可回收页到LRU */
int reclaim_add_page(uint32_t phys_addr)
{
    if (!reclaim_initialized) return -1;
    if (lru_count >= LRU_MAX_PAGES) {
        return -2;  /* LRU已满 */
    }
    for (int i = 0; i < LRU_MAX_PAGES; i++) {
        if (!lru_pages[i].used) {
            lru_pages[i].used = 1;
            lru_pages[i].referenced = 1;
            lru_pages[i].phys_addr = phys_addr;
            lru_pages[i].age = 0;
            lru_pages[i].next = NULL;
            lru_pages[i].prev = NULL;
            /* 添加到头部（最近使用） */
            if (lru_head) {
                lru_head->prev = (struct lru_page *)&lru_pages[i];
                lru_pages[i].next = lru_head;
            }
            lru_head = (lru_page_t *)&lru_pages[i];
            if (!lru_tail) lru_tail = (lru_page_t *)&lru_pages[i];
            lru_count++;
            return 0;
        }
    }
    return -1;
}

/* 标记页被访问（模拟访问位） */
int reclaim_mark_accessed(uint32_t phys_addr)
{
    for (int i = 0; i < LRU_MAX_PAGES; i++) {
        if (lru_pages[i].used && lru_pages[i].phys_addr == phys_addr) {
            lru_pages[i].referenced = 1;
            /* 移到头部 */
            lru_page_t *p = &lru_pages[i];
            if (p != lru_head) {
                if (p->prev) p->prev->next = p->next;
                if (p->next) p->next->prev = p->prev;
                if (p == lru_tail) lru_tail = (lru_page_t *)p->prev;
                p->next = (struct lru_page *)lru_head;
                p->prev = NULL;
                if (lru_head) lru_head->prev = (struct lru_page *)p;
                lru_head = p;
            }
            return 0;
        }
    }
    return -1;
}

/* 从LRU移除页 */
int reclaim_remove_page(uint32_t phys_addr)
{
    for (int i = 0; i < LRU_MAX_PAGES; i++) {
        if (lru_pages[i].used && lru_pages[i].phys_addr == phys_addr) {
            lru_page_t *p = &lru_pages[i];
            if (p->prev) p->prev->next = p->next;
            else lru_head = (lru_page_t *)p->next;
            if (p->next) p->next->prev = p->prev;
            else lru_tail = (lru_page_t *)p->prev;
            p->used = 0;
            p->next = p->prev = NULL;
            lru_count--;
            return 0;
        }
    }
    return -1;
}

/* 回收最久未使用的页（返回被回收的物理地址，0表示无） */
uint32_t reclaim_reclaim_page(void)
{
    if (!reclaim_initialized || !lru_tail) return 0;

    /* 从尾部开始找可回收页（第二遍才强制回收带引用位的） */
    lru_page_t *cur = lru_tail;
    for (int pass = 0; pass < 2 && cur; pass++) {
        lru_page_t *scan = cur;
        while (scan) {
            if (!scan->referenced) {
                /* 找到可回收页 */
                uint32_t addr = scan->phys_addr;
                reclaim_remove_page(addr);
                mm_free_page(addr);
                reclaimed_total++;
                return addr;
            }
            scan = (lru_page_t *)scan->prev;
        }
        /* 第一遍没找到，清除所有引用位再扫 */
        scan = cur;
        while (scan) {
            scan->referenced = 0;
            scan = (lru_page_t *)scan->prev;
        }
    }
    return 0;
}

/* 批量回收直到空闲页达到目标 */
int reclaim_until(uint32_t target_free_pages)
{
    uint32_t total, used, free_pages;
    int reclaimed = 0;

    while (1) {
        mm_stats(&total, &used, &free_pages);
        if (free_pages >= target_free_pages) break;
        if (reclaim_reclaim_page() == 0) {
            break;  /* 无可回收页 */
        }
        reclaimed++;
        if (reclaimed > LRU_MAX_PAGES * 2) break;  /* 防死循环 */
    }
    reclaim_calls++;
    return reclaimed;
}

/* 检查内存压力 */
int reclaim_check_pressure(void)
{
    if (!reclaim_initialized) return MEM_OK;
    int p = update_pressure();

    if (p == MEM_PRESSURE_NORMAL) {
        reclaim_until(watermarks[0]);
    } else if (p == MEM_PRESSURE_HIGH) {
        reclaim_until(watermarks[1]);
    } else if (p == MEM_PRESSURE_CRITICAL) {
        reclaim_until(watermarks[2]);
        /* 非递归复查（避免栈溢出） */
        if (free_pages_now() <= watermarks[2]) {
            vga_printf("[RECLAIM] out of memory ! free:%d/%d pages \n",
                       free_pages_now(), total_pages_cache);
        }
    }
    return p;
}

/* 获取当前空闲页数 */
uint32_t free_pages_now(void)
{
    uint32_t total, used, free_pages;
    mm_stats(&total, &used, &free_pages);
    return free_pages;
}

/* 回收统计 */
void reclaim_stats(uint32_t *reclaimed, uint32_t *calls,
                   uint32_t *lru_used, uint32_t *pressure)
{
    if (reclaimed) *reclaimed = reclaimed_total;
    if (calls) *calls = reclaim_calls;
    if (lru_used) *lru_used = lru_count;
    if (pressure) *pressure = pressure_history[2] | (pressure_history[3] << 16);
}
