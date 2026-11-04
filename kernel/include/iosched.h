#ifndef IOSCHED_H
#define IOSCHED_H

#include <stdint.h>

/* IO请求类型 */
#define IO_READ   0
#define IO_WRITE  1

/* 调度器类型 */
#define IOSCHED_NOOP      0  /* FIFO，适合SSD */
#define IOSCHED_DEADLINE  1  /* 读优先+延迟保证，适合HDD */

/* IO请求 */
typedef struct io_request {
    uint32_t lba;           /* 起始LBA */
    uint32_t count;         /* 扇区数 */
    uint8_t  *buffer;       /* 数据缓冲区 */
    int      type;          /* IO_READ / IO_WRITE */
    struct io_request *next;
} io_request_t;

/* 初始化IO调度器 */
void iosched_init(int scheduler_type);

/* 提交IO请求（异步，加入队列） */
int iosched_submit(uint32_t lba, uint8_t *buffer, uint32_t count, int type);

/* 执行所有挂起的IO请求（同步，合并+排序后批量执行） */
int iosched_flush(void);

/* 同步读写（提交+立即执行） */
int iosched_read(uint32_t lba, uint8_t *buffer, uint32_t count);
int iosched_write(uint32_t lba, const uint8_t *buffer, uint32_t count);

/* 统计信息 */
void iosched_stats(uint32_t *total_ios, uint32_t *merged_ios,
                   uint32_t *read_ios, uint32_t *write_ios);

/* 自测 */
void iosched_selftest(void);

#endif
