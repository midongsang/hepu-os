#ifndef BLK_MQ_H
#define BLK_MQ_H

#include <stdint.h>

/* blk-mq常量 */
#define BLK_MQ_MAX_SW_QUEUES    8     /* 最大软件提交队列数（per-CPU） */
#define BLK_MQ_MAX_HW_QUEUES    4     /* 最大硬件队列数 */
#define BLK_MQ_MAX_REQUESTS     256    /* 每个队列最大请求数 */
#define BLK_MQ_REQUEST_SIZE     4096   /* 请求数据大小 */

/* 请求类型 */
#define BLK_MQ_OP_READ    0
#define BLK_MQ_OP_WRITE   1
#define BLK_MQ_OP_FLUSH   2
#define BLK_MQ_OP_DISCARD 3

/* 请求状态 */
#define BLK_MQ_REQ_FREE      0
#define BLK_MQ_REQ_QUEUED    1
#define BLK_MQ_REQ_DISPATCH  2
#define BLK_MQ_REQ_COMPLETED 3
#define BLK_MQ_REQ_ERROR     4

/* 块请求 */
typedef struct {
    uint32_t id;           /* 请求ID */
    uint32_t op;           /* 操作类型 */
    uint32_t lba;          /* 逻辑块地址 */
    uint32_t count;        /* 扇区数 */
    uint8_t  *data;        /* 数据缓冲区 */
    uint32_t status;       /* 状态 */
    uint32_t sw_queue;     /* 所属软件队列 */
    uint32_t hw_queue;     /* 所属硬件队列 */
    uint64_t submit_time;  /* 提交时间 */
    uint64_t complete_time; /* 完成时间 */
} blk_mq_request_t;

/* 软件提交队列（per-CPU） */
typedef struct {
    uint32_t id;
    uint32_t cpu;              /* 所属CPU */
    uint32_t depth;            /* 队列深度 */
    uint32_t queued;           /* 已排队请求数 */
    uint32_t completed;        /* 已完成请求数 */
    uint32_t hw_queue_map;     /* 映射到的硬件队列 */
    blk_mq_request_t *requests; /* 请求数组 */
    int      initialized;
} blk_mq_sw_queue_t;

/* 硬件队列 */
typedef struct {
    uint32_t id;
    uint32_t depth;
    uint32_t queued;
    uint32_t completed;
    uint32_t sw_queue_count;   /* 映射的软件队列数 */
    uint32_t sw_queues[BLK_MQ_MAX_SW_QUEUES]; /* 映射的软件队列 */
    int      initialized;
} blk_mq_hw_queue_t;

/* blk-mq统计 */
typedef struct {
    uint32_t total_requests;
    uint32_t read_requests;
    uint32_t write_requests;
    uint32_t completed_requests;
    uint32_t error_requests;
    uint32_t sw_queue_count;
    uint32_t hw_queue_count;
    uint32_t merge_count;       /* 请求合并次数 */
    uint64_t total_latency;     /* 总延迟 */
} blk_mq_stats_t;

/* 初始化blk-mq */
int blk_mq_init(uint32_t sw_queues, uint32_t hw_queues);

/* 销毁blk-mq */
void blk_mq_exit(void);

/* 提交IO请求 */
int blk_mq_submit(uint32_t op, uint32_t lba, uint32_t count, void *data);

/* 分发请求到硬件队列 */
int blk_mq_dispatch(uint32_t hw_queue);

/* 完成请求 */
int blk_mq_complete(uint32_t request_id, int error);

/* 轮询完成的请求 */
int blk_mq_poll(blk_mq_request_t *requests, uint32_t max_count);

/* 获取统计信息 */
void blk_mq_get_stats(blk_mq_stats_t *stats);

/* 打印blk-mq信息 */
void blk_mq_dump(void);

/* 自测 */
void blk_mq_selftest(void);

#endif
