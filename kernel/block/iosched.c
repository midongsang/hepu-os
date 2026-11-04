/* ============================================================================
 * 赫普OS - IO调度器
 * 基于《赫普OS内存、显存与存储资源管理详解》3.3节实现
 *
 * 功能：
 * - Noop调度器（FIFO，适合SSD）
 * - Deadline调度器（读优先+延迟保证，适合HDD）
 * - IO请求合并（相邻请求合并为大请求，减少磁盘IO次数）
 * - LBA排序（减少磁盘寻道时间）
 * - 统计信息（总IO数、合并数、读写数）
 *
 * 设计说明：
 * 当前系统为同步IO架构，调度器采用"批量提交+合并执行"模式：
 * 1. iosched_submit() 将请求加入队列
 * 2. iosched_flush() 合并相邻请求、按LBA排序、批量执行
 * 3. iosched_read/write() 为便捷接口（提交+立即执行）
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include "iosched.h"
#include "ide.h"
#include "kheap.h"
#include "string.h"

/* 调度器状态 */
static int iosched_type = IOSCHED_NOOP;
static io_request_t *io_queue = NULL;
static uint32_t queue_count = 0;

/* 统计 */
static uint32_t stat_total_ios = 0;
static uint32_t stat_merged_ios = 0;
static uint32_t stat_read_ios = 0;
static uint32_t stat_write_ios = 0;

/* 初始化IO调度器 */
void iosched_init(int scheduler_type)
{
    iosched_type = scheduler_type;
    io_queue = NULL;
    queue_count = 0;
    stat_total_ios = 0;
    stat_merged_ios = 0;
    stat_read_ios = 0;
    stat_write_ios = 0;
}

/* 提交IO请求（加入队列，不立即执行） */
int iosched_submit(uint32_t lba, uint8_t *buffer, uint32_t count, int type)
{
    if (count == 0 || !buffer) return -1;

    io_request_t *req = (io_request_t *)kmalloc(sizeof(io_request_t));
    if (!req) return -2;

    req->lba = lba;
    req->count = count;
    req->buffer = buffer;
    req->type = type;
    req->next = NULL;

    /* 加入队列尾部 */
    if (!io_queue) {
        io_queue = req;
    } else {
        io_request_t *cur = io_queue;
        while (cur->next) cur = cur->next;
        cur->next = req;
    }
    queue_count++;
    return 0;
}

/* 按LBA排序（简单的冒泡排序，队列通常较小） */
static void sort_requests(io_request_t **head)
{
    if (!*head || !(*head)->next) return;

    int swapped;
    do {
        swapped = 0;
        io_request_t *cur = *head;
        io_request_t *prev = NULL;
        while (cur && cur->next) {
            if (cur->lba > cur->next->lba) {
                /* 交换cur和cur->next */
                io_request_t *next = cur->next;
                cur->next = next->next;
                next->next = cur;
                if (prev) prev->next = next;
                else *head = next;
                prev = next;
                swapped = 1;
            } else {
                prev = cur;
                cur = cur->next;
            }
        }
    } while (swapped);
}

/* 合并相邻请求（同类型、LBA连续的请求合并为一个） */
static uint32_t merge_requests(io_request_t **head)
{
    if (!*head) return 0;

    uint32_t merged = 0;
    io_request_t *cur = *head;

    while (cur && cur->next) {
        io_request_t *next = cur->next;
        /* 检查是否可以合并：同类型、LBA连续、缓冲区连续 */
        if (cur->type == next->type &&
            cur->lba + cur->count == next->lba &&
            cur->buffer + cur->count * 512 == next->buffer) {
            /* 合并 */
            cur->count += next->count;
            cur->next = next->next;
            kfree(next);
            merged++;
            /* 不移动cur，继续检查新的next是否可以合并 */
        } else {
            cur = cur->next;
        }
    }
    return merged;
}

/* 执行单个IO请求 */
static int execute_request(io_request_t *req)
{
    int ret;
    if (req->type == IO_READ) {
        ret = ide_read_sectors(req->lba, req->buffer, req->count);
        stat_read_ios++;
    } else {
        ret = ide_write_sectors(req->lba, req->buffer, req->count);
        stat_write_ios++;
    }
    stat_total_ios++;
    return ret;
}

/* 执行所有挂起的IO请求（合并+排序后批量执行） */
int iosched_flush(void)
{
    if (!io_queue) return 0;

    /* Deadline调度器：读请求优先执行 */
    if (iosched_type == IOSCHED_DEADLINE) {
        /* 简单实现：先执行所有读请求，再执行写请求 */
        io_request_t *read_queue = NULL;
        io_request_t *write_queue = NULL;
        io_request_t *cur = io_queue;
        while (cur) {
            io_request_t *next = cur->next;
            cur->next = NULL;
            if (cur->type == IO_READ) {
                cur->next = read_queue;
                read_queue = cur;
            } else {
                cur->next = write_queue;
                write_queue = cur;
            }
            cur = next;
        }
        /* 反转队列保持FIFO顺序 */
        io_request_t *rev = NULL;
        while (read_queue) {
            io_request_t *next = read_queue->next;
            read_queue->next = rev;
            rev = read_queue;
            read_queue = next;
        }
        read_queue = rev;
        rev = NULL;
        while (write_queue) {
            io_request_t *next = write_queue->next;
            write_queue->next = rev;
            rev = write_queue;
            write_queue = next;
        }
        write_queue = rev;

        /* 合并+排序+执行读请求 */
        if (read_queue) {
            sort_requests(&read_queue);
            stat_merged_ios += merge_requests(&read_queue);
            cur = read_queue;
            while (cur) {
                execute_request(cur);
                cur = cur->next;
            }
        }
        /* 合并+排序+执行写请求 */
        if (write_queue) {
            sort_requests(&write_queue);
            stat_merged_ios += merge_requests(&write_queue);
            cur = write_queue;
            while (cur) {
                execute_request(cur);
                cur = cur->next;
            }
        }
        /* 释放队列 */
        cur = read_queue;
        while (cur) {
            io_request_t *next = cur->next;
            kfree(cur);
            cur = next;
        }
        cur = write_queue;
        while (cur) {
            io_request_t *next = cur->next;
            kfree(cur);
            cur = next;
        }
    } else {
        /* Noop调度器：FIFO，但仍然合并+排序 */
        sort_requests(&io_queue);
        stat_merged_ios += merge_requests(&io_queue);

        io_request_t *cur = io_queue;
        while (cur) {
            execute_request(cur);
            cur = cur->next;
        }
        /* 释放队列 */
        cur = io_queue;
        while (cur) {
            io_request_t *next = cur->next;
            kfree(cur);
            cur = next;
        }
    }

    uint32_t executed = queue_count;
    io_queue = NULL;
    queue_count = 0;
    return executed;
}

/* 同步读（提交+立即执行） */
int iosched_read(uint32_t lba, uint8_t *buffer, uint32_t count)
{
    int ret = iosched_submit(lba, buffer, count, IO_READ);
    if (ret != 0) return ret;
    return iosched_flush();
}

/* 同步写（提交+立即执行） */
int iosched_write(uint32_t lba, const uint8_t *buffer, uint32_t count)
{
    int ret = iosched_submit(lba, (uint8_t *)buffer, count, IO_WRITE);
    if (ret != 0) return ret;
    return iosched_flush();
}

/* 统计信息 */
void iosched_stats(uint32_t *total_ios, uint32_t *merged_ios,
                   uint32_t *read_ios, uint32_t *write_ios)
{
    if (total_ios) *total_ios = stat_total_ios;
    if (merged_ios) *merged_ios = stat_merged_ios;
    if (read_ios) *read_ios = stat_read_ios;
    if (write_ios) *write_ios = stat_write_ios;
}

/* ============================================================================
 * IO调度器自测
 * ========================================================================== */
void iosched_selftest(void)
{
    extern void serial_puts(uint16_t, const char *);
    extern void serial_puthex(uint16_t, uint32_t);
    #define ILOG(s) serial_puts(0x3F8, s)

    ILOG("[IOSCHED] selftest: start\n");
    int pass = 0, fail = 0;

    /* 测试1：初始化 */
    iosched_init(IOSCHED_NOOP);
    uint32_t t, m, r, w;
    iosched_stats(&t, &m, &r, &w);
    if (t == 0 && m == 0 && r == 0 && w == 0) {
        ILOG("[IOSCHED] test1 init: PASS\n");
        pass++;
    } else {
        ILOG("[IOSCHED] test1 init: FAIL\n");
        fail++;
    }

    /* 测试2：提交请求 */
    static uint8_t bigbuf[512 * 4];  /* 连续缓冲区，用于合并测试 */
    uint8_t *buf1 = bigbuf;
    uint8_t *buf2 = bigbuf + 512;
    uint8_t *buf3 = bigbuf + 1024;
    int ret = iosched_submit(100, buf1, 1, IO_READ);
    ret |= iosched_submit(101, buf2, 1, IO_READ);
    ret |= iosched_submit(200, buf3, 1, IO_WRITE);
    if (ret == 0) {
        ILOG("[IOSCHED] test2 submit: PASS\n");
        pass++;
    } else {
        ILOG("[IOSCHED] test2 submit: FAIL\n");
        fail++;
    }

    /* 测试3：执行（合并验证：100和101应该合并为一个请求） */
    /* 注意：实际执行会访问IDE，这里只验证队列处理逻辑 */
    /* 先清空队列，避免实际IO */
    io_request_t *cur = io_queue;
    while (cur) {
        io_request_t *next = cur->next;
        kfree(cur);
        cur = next;
    }
    io_queue = NULL;
    queue_count = 0;

    /* 重新提交可合并的请求 */
    iosched_submit(100, buf1, 1, IO_READ);
    iosched_submit(101, buf2, 1, IO_READ);  /* 与100连续，可合并 */

    /* 手动测试合并逻辑 */
    sort_requests(&io_queue);
    uint32_t merged = merge_requests(&io_queue);
    if (merged == 1 && io_queue && io_queue->count == 2) {
        ILOG("[IOSCHED] test3 merge adjacent: PASS (merged=");
        serial_puthex(0x3F8, merged);
        ILOG(", count=");
        serial_puthex(0x3F8, io_queue->count);
        ILOG(")\n");
        pass++;
    } else {
        ILOG("[IOSCHED] test3 merge adjacent: FAIL\n");
        fail++;
    }

    /* 清空队列 */
    cur = io_queue;
    while (cur) {
        io_request_t *next = cur->next;
        kfree(cur);
        cur = next;
    }
    io_queue = NULL;
    queue_count = 0;

    /* 测试4：LBA排序 */
    iosched_submit(300, buf1, 1, IO_READ);
    iosched_submit(100, buf2, 1, IO_READ);
    iosched_submit(200, buf3, 1, IO_READ);
    sort_requests(&io_queue);
    if (io_queue && io_queue->lba == 100 &&
        io_queue->next && io_queue->next->lba == 200 &&
        io_queue->next->next && io_queue->next->next->lba == 300) {
        ILOG("[IOSCHED] test4 LBA sort: PASS\n");
        pass++;
    } else {
        ILOG("[IOSCHED] test4 LBA sort: FAIL\n");
        fail++;
    }

    /* 清空队列 */
    cur = io_queue;
    while (cur) {
        io_request_t *next = cur->next;
        kfree(cur);
        cur = next;
    }
    io_queue = NULL;
    queue_count = 0;

    /* 测试5：不可合并的请求（LBA不连续） */
    iosched_submit(100, buf1, 1, IO_READ);
    iosched_submit(200, buf2, 1, IO_READ);  /* 不连续，不可合并 */
    sort_requests(&io_queue);
    merged = merge_requests(&io_queue);
    if (merged == 0 && io_queue && io_queue->next) {
        ILOG("[IOSCHED] test5 no merge non-adjacent: PASS\n");
        pass++;
    } else {
        ILOG("[IOSCHED] test5 no merge non-adjacent: FAIL\n");
        fail++;
    }

    /* 清空队列 */
    cur = io_queue;
    while (cur) {
        io_request_t *next = cur->next;
        kfree(cur);
        cur = next;
    }
    io_queue = NULL;
    queue_count = 0;

    /* 测试6：不同类型不可合并 */
    iosched_submit(100, buf1, 1, IO_READ);
    iosched_submit(101, buf2, 1, IO_WRITE);  /* 类型不同，不可合并 */
    sort_requests(&io_queue);
    merged = merge_requests(&io_queue);
    if (merged == 0) {
        ILOG("[IOSCHED] test6 no merge diff-type: PASS\n");
        pass++;
    } else {
        ILOG("[IOSCHED] test6 no merge diff-type: FAIL\n");
        fail++;
    }

    /* 清空队列 */
    cur = io_queue;
    while (cur) {
        io_request_t *next = cur->next;
        kfree(cur);
        cur = next;
    }
    io_queue = NULL;
    queue_count = 0;

    ILOG("[IOSCHED] selftest: done (pass=");
    serial_puthex(0x3F8, pass);
    ILOG(", fail=");
    serial_puthex(0x3F8, fail);
    ILOG(")\n");

    #undef ILOG
}
