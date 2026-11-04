/* ============================================================================
 * 赫普OS - 多队列块层（blk-mq）
 *
 * 功能：
 * - 多软件提交队列（per-CPU），减少锁竞争
 * - 多硬件队列，提高IO并行度
 * - 软件队列到硬件队列的映射
 * - 请求合并（相邻LBA合并）
 * - 请求分发和完成
 * - 统计信息和延迟统计
 *
 * 设计（参考Linux blk-mq）：
 * - 软件队列（Software Queue）：per-CPU提交队列，无锁或细粒度锁
 * - 硬件队列（Hardware Queue）：设备实际队列，多个软件队列映射到一个硬件队列
 * - 请求生命周期：提交→排队→分发→完成
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "blk_mq.h"
#include "kheap.h"
#include "vga.h"

/* 全局状态 */
static blk_mq_sw_queue_t sw_queues[BLK_MQ_MAX_SW_QUEUES];
static blk_mq_hw_queue_t hw_queues[BLK_MQ_MAX_HW_QUEUES];
static blk_mq_stats_t blk_mq_stats;
static uint32_t sw_queue_count = 0;
static uint32_t hw_queue_count = 0;
static uint32_t next_request_id = 1;
static int blk_mq_initialized = 0;

/* ============================================================================
 * 内部函数
 * ========================================================================== */

/* 获取当前CPU（用LBA哈希，使相邻LBA请求在同一队列，便于合并） */
static uint32_t blk_mq_get_cpu(uint32_t lba)
{
    return (lba / 32) % sw_queue_count;  /* 每32扇区(16KB)一个队列区域 */
}

/* 查找空闲请求 */
static blk_mq_request_t *blk_mq_alloc_request(blk_mq_sw_queue_t *sq)
{
    for (uint32_t i = 0; i < sq->depth; i++) {
        if (sq->requests[i].status == BLK_MQ_REQ_FREE) {
            memset(&sq->requests[i], 0, sizeof(blk_mq_request_t));
            sq->requests[i].id = next_request_id++;
            sq->requests[i].status = BLK_MQ_REQ_QUEUED;
            sq->requests[i].sw_queue = sq->id;
            sq->requests[i].hw_queue = sq->hw_queue_map;
            return &sq->requests[i];
        }
    }
    return NULL;
}

/* 尝试合并请求（相邻LBA且同类型） */
static int blk_mq_try_merge(blk_mq_sw_queue_t *sq, uint32_t op,
                              uint32_t lba, uint32_t count)
{
    for (uint32_t i = 0; i < sq->depth; i++) {
        blk_mq_request_t *r = &sq->requests[i];
        if (r->status != BLK_MQ_REQ_QUEUED) continue;
        if (r->op != op) continue;

        /* 检查是否相邻：r->lba + r->count == lba */
        if (r->lba + r->count == lba) {
            r->count += count;
            blk_mq_stats.merge_count++;
            return 1;
        }
        /* 检查是否前置：lba + count == r->lba */
        if (lba + count == r->lba) {
            r->lba = lba;
            r->count += count;
            blk_mq_stats.merge_count++;
            return 1;
        }
    }
    return 0;
}

/* ============================================================================
 * 公共接口
 * ============================================================================ */

int blk_mq_init(uint32_t sw_queues_num, uint32_t hw_queues_num)
{
    if (blk_mq_initialized) return 0;

    if (sw_queues_num == 0) sw_queues_num = 4;
    if (hw_queues_num == 0) hw_queues_num = 2;
    if (sw_queues_num > BLK_MQ_MAX_SW_QUEUES) sw_queues_num = BLK_MQ_MAX_SW_QUEUES;
    if (hw_queues_num > BLK_MQ_MAX_HW_QUEUES) hw_queues_num = BLK_MQ_MAX_HW_QUEUES;

    memset(sw_queues, 0, sizeof(sw_queues));
    memset(hw_queues, 0, sizeof(hw_queues));
    memset(&blk_mq_stats, 0, sizeof(blk_mq_stats));
    next_request_id = 1;

    /* 初始化硬件队列 */
    hw_queue_count = hw_queues_num;
    for (uint32_t i = 0; i < hw_queue_count; i++) {
        hw_queues[i].id = i;
        hw_queues[i].depth = BLK_MQ_MAX_REQUESTS;
        hw_queues[i].initialized = 1;
    }

    /* 初始化软件队列 */
    sw_queue_count = sw_queues_num;
    for (uint32_t i = 0; i < sw_queue_count; i++) {
        sw_queues[i].id = i;
        sw_queues[i].cpu = i;
        sw_queues[i].depth = BLK_MQ_MAX_REQUESTS;
        sw_queues[i].hw_queue_map = i % hw_queue_count;  /* 轮询映射到硬件队列 */
        sw_queues[i].requests = (blk_mq_request_t *)kmalloc(
            sizeof(blk_mq_request_t) * BLK_MQ_MAX_REQUESTS);
        if (!sw_queues[i].requests) return -1;
        memset(sw_queues[i].requests, 0,
               sizeof(blk_mq_request_t) * BLK_MQ_MAX_REQUESTS);
        sw_queues[i].initialized = 1;

        /* 记录软件队列到硬件队列的映射 */
        hw_queues[sw_queues[i].hw_queue_map].sw_queues[
            hw_queues[sw_queues[i].hw_queue_map].sw_queue_count++] = i;
    }

    blk_mq_stats.sw_queue_count = sw_queue_count;
    blk_mq_stats.hw_queue_count = hw_queue_count;
    blk_mq_initialized = 1;

    return 0;
}

void blk_mq_exit(void)
{
    if (!blk_mq_initialized) return;

    for (uint32_t i = 0; i < sw_queue_count; i++) {
        if (sw_queues[i].requests) {
            kfree(sw_queues[i].requests);
            sw_queues[i].requests = NULL;
        }
    }

    blk_mq_initialized = 0;
}

int blk_mq_submit(uint32_t op, uint32_t lba, uint32_t count, void *data)
{
    if (!blk_mq_initialized || count == 0) return -1;

    /* 选择软件队列（按LBA哈希，使相邻请求同队列便于合并） */
    uint32_t cpu = blk_mq_get_cpu(lba);
    blk_mq_sw_queue_t *sq = &sw_queues[cpu];

    /* 尝试合并请求 */
    if (blk_mq_try_merge(sq, op, lba, count)) {
        return 0;  /* 合并成功，不需要新请求 */
    }

    /* 分配新请求 */
    blk_mq_request_t *req = blk_mq_alloc_request(sq);
    if (!req) return -2;

    req->op = op;
    req->lba = lba;
    req->count = count;
    req->data = data;
    req->submit_time = 0;  /* TODO: 时间戳 */

    sq->queued++;
    blk_mq_stats.total_requests++;
    if (op == BLK_MQ_OP_READ) blk_mq_stats.read_requests++;
    if (op == BLK_MQ_OP_WRITE) blk_mq_stats.write_requests++;

    return req->id;
}

int blk_mq_dispatch(uint32_t hw_queue_id)
{
    if (!blk_mq_initialized || hw_queue_id >= hw_queue_count) return -1;

    blk_mq_hw_queue_t *hq = &hw_queues[hw_queue_id];
    int dispatched = 0;

    /* 遍历映射到该硬件队列的所有软件队列 */
    for (uint32_t i = 0; i < hq->sw_queue_count; i++) {
        blk_mq_sw_queue_t *sq = &sw_queues[hq->sw_queues[i]];

        for (uint32_t j = 0; j < sq->depth; j++) {
            blk_mq_request_t *req = &sq->requests[j];
            if (req->status == BLK_MQ_REQ_QUEUED) {
                req->status = BLK_MQ_REQ_DISPATCH;
                hq->queued++;
                dispatched++;
            }
        }
    }

    return dispatched;
}

int blk_mq_complete(uint32_t request_id, int error)
{
    if (!blk_mq_initialized) return -1;

    /* 查找请求 */
    for (uint32_t i = 0; i < sw_queue_count; i++) {
        blk_mq_sw_queue_t *sq = &sw_queues[i];
        for (uint32_t j = 0; j < sq->depth; j++) {
            blk_mq_request_t *req = &sq->requests[j];
            if (req->id == request_id && req->status != BLK_MQ_REQ_FREE) {
                req->status = error ? BLK_MQ_REQ_ERROR : BLK_MQ_REQ_COMPLETED;
                req->complete_time = 0;  /* TODO: 时间戳 */
                sq->completed++;
                sq->queued--;
                blk_mq_stats.completed_requests++;
                if (error) blk_mq_stats.error_requests++;
                return 0;
            }
        }
    }
    return -2;  /* 请求未找到 */
}

int blk_mq_poll(blk_mq_request_t *requests, uint32_t max_count)
{
    if (!blk_mq_initialized || !requests) return 0;

    uint32_t count = 0;

    for (uint32_t i = 0; i < sw_queue_count && count < max_count; i++) {
        blk_mq_sw_queue_t *sq = &sw_queues[i];
        for (uint32_t j = 0; j < sq->depth && count < max_count; j++) {
            blk_mq_request_t *req = &sq->requests[j];
            if (req->status == BLK_MQ_REQ_COMPLETED || req->status == BLK_MQ_REQ_ERROR) {
                memcpy(&requests[count], req, sizeof(blk_mq_request_t));
                /* 释放请求 */
                memset(req, 0, sizeof(blk_mq_request_t));
                count++;
            }
        }
    }

    return count;
}

void blk_mq_get_stats(blk_mq_stats_t *stats)
{
    if (!stats) return;
    memcpy(stats, &blk_mq_stats, sizeof(blk_mq_stats_t));
}

void blk_mq_dump(void)
{
    blk_mq_stats_t stats;
    blk_mq_get_stats(&stats);

    vga_printf("=== blk-mq Multi-Queue Block Layer ===\n");
    vga_printf("[Queues] SW queues: %d, HW queues: %d\n",
                stats.sw_queue_count, stats.hw_queue_count);
    vga_printf("[Requests] Total: %d, Read: %d, Write: %d\n",
                stats.total_requests, stats.read_requests, stats.write_requests);
    vga_printf("[Completed] %d, Errors: %d, Merges: %d\n",
                stats.completed_requests, stats.error_requests, stats.merge_count);

    for (uint32_t i = 0; i < hw_queue_count; i++) {
        vga_printf("  [HW Q%d] queued: %d, completed: %d, SW queues: %d\n",
                    i, hw_queues[i].queued, hw_queues[i].completed,
                    hw_queues[i].sw_queue_count);
    }
    vga_printf("====================================\n");
}

/* ============================================================================
 * 自测
 * ========================================================================== */
void blk_mq_selftest(void)
{
    extern void serial_puts(uint16_t, const char *);
    extern void serial_puthex(uint16_t, uint32_t);
    #define BMLOG(s) serial_puts(0x3F8, s)

    BMLOG("[BLK-MQ] selftest: start\n");
    int pass = 0, fail = 0;

    /* 测试1：初始化（4个SW队列，2个HW队列） */
    int ret = blk_mq_init(4, 2);
    if (ret == 0) {
        blk_mq_stats_t stats;
        blk_mq_get_stats(&stats);
        if (stats.sw_queue_count == 4 && stats.hw_queue_count == 2) {
            BMLOG("[BLK-MQ] test1 init: PASS (sw=4, hw=2)\n");
            pass++;
        } else {
            BMLOG("[BLK-MQ] test1 init: FAIL\n");
            fail++;
        }
    } else {
        BMLOG("[BLK-MQ] test1 init: FAIL\n");
        fail++;
    }

    /* 测试2：提交读请求 */
    static uint8_t data_buf[4096];
    int id1 = blk_mq_submit(BLK_MQ_OP_READ, 100, 8, data_buf);
    if (id1 > 0) {
        BMLOG("[BLK-MQ] test2 submit read: PASS (id=");
        serial_puthex(0x3F8, id1);
        BMLOG(")\n");
        pass++;
    } else {
        BMLOG("[BLK-MQ] test2 submit read: FAIL\n");
        fail++;
    }

    /* 测试3：提交写请求 */
    int id2 = blk_mq_submit(BLK_MQ_OP_WRITE, 200, 16, data_buf);
    if (id2 > 0) {
        BMLOG("[BLK-MQ] test3 submit write: PASS (id=");
        serial_puthex(0x3F8, id2);
        BMLOG(")\n");
        pass++;
    } else {
        BMLOG("[BLK-MQ] test3 submit write: FAIL\n");
        fail++;
    }

    /* 测试4：请求合并（相邻LBA） */
    int id3 = blk_mq_submit(BLK_MQ_OP_WRITE, 216, 8, data_buf);
    /* 200+16=216，应该合并到id2 */
    blk_mq_stats_t stats;
    blk_mq_get_stats(&stats);
    if (stats.merge_count >= 1) {
        BMLOG("[BLK-MQ] test4 request merge: PASS (merges=");
        serial_puthex(0x3F8, stats.merge_count);
        BMLOG(")\n");
        pass++;
    } else {
        BMLOG("[BLK-MQ] test4 request merge: FAIL\n");
        fail++;
    }

    /* 测试5：分发请求 */
    int dispatched = blk_mq_dispatch(0);
    dispatched += blk_mq_dispatch(1);
    if (dispatched >= 2) {
        BMLOG("[BLK-MQ] test5 dispatch: PASS (dispatched=");
        serial_puthex(0x3F8, dispatched);
        BMLOG(")\n");
        pass++;
    } else {
        BMLOG("[BLK-MQ] test5 dispatch: FAIL\n");
        fail++;
    }

    /* 测试6：完成请求 */
    ret = blk_mq_complete(id1, 0);
    ret |= blk_mq_complete(id2, 0);
    if (ret == 0) {
        blk_mq_get_stats(&stats);
        if (stats.completed_requests >= 2) {
            BMLOG("[BLK-MQ] test6 complete: PASS (completed=");
            serial_puthex(0x3F8, stats.completed_requests);
            BMLOG(")\n");
            pass++;
        } else {
            BMLOG("[BLK-MQ] test6 complete: FAIL\n");
            fail++;
        }
    } else {
        BMLOG("[BLK-MQ] test6 complete: FAIL\n");
        fail++;
    }

    /* 测试7：轮询完成请求 */
    static blk_mq_request_t completed_reqs[16];
    int polled = blk_mq_poll(completed_reqs, 16);
    if (polled >= 2) {
        BMLOG("[BLK-MQ] test7 poll: PASS (polled=");
        serial_puthex(0x3F8, polled);
        BMLOG(")\n");
        pass++;
    } else {
        BMLOG("[BLK-MQ] test7 poll: FAIL (polled=");
        serial_puthex(0x3F8, polled);
        BMLOG(")\n");
        fail++;
    }

    BMLOG("[BLK-MQ] selftest: done (pass=");
    serial_puthex(0x3F8, pass);
    BMLOG(", fail=");
    serial_puthex(0x3F8, fail);
    BMLOG(")\n");

    #undef BMLOG
}
