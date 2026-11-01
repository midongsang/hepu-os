#ifndef SNAPSHOT_H
#define SNAPSHOT_H

#include <stdint.h>

/* 快照常量 */
#define SNAPSHOT_MAX_COUNT     8      /* 最大快照数 */
#define SNAPSHOT_MAX_BLOCKS    1024   /* 每个快照最大跟踪块数 */
#define SNAPSHOT_NAME_LEN      32     /* 快照名称长度 */

/* 快照状态 */
#define SNAPSHOT_STATE_EMPTY   0      /* 空 */
#define SNAPSHOT_STATE_ACTIVE  1      /* 活跃（可写时复制） */
#define SNAPSHOT_STATE_FROZEN  2      /* 冻结（只读） */

/* 块映射条目 */
typedef struct {
    uint32_t orig_block;    /* 原始块号 */
    uint32_t snap_block;    /* 快照存储块号 */
    int      valid;         /* 是否有效 */
} snapshot_block_map_t;

/* 快照元数据 */
typedef struct {
    uint32_t id;                       /* 快照ID */
    char     name[SNAPSHOT_NAME_LEN];  /* 快照名称 */
    uint32_t state;                    /* 状态 */
    uint64_t create_time;              /* 创建时间 */
    uint32_t block_count;              /* 跟踪的块数 */
    snapshot_block_map_t block_map[SNAPSHOT_MAX_BLOCKS];  /* 块映射表 */
} snapshot_t;

/* 快照管理器 */
typedef struct {
    snapshot_t snapshots[SNAPSHOT_MAX_COUNT];
    uint32_t   next_id;
    int        initialized;
} snapshot_manager_t;

/* 初始化快照管理器 */
int snapshot_init(void);

/* 创建快照 */
int snapshot_create(const char *name);

/* 删除快照 */
int snapshot_delete(uint32_t id);

/* 冻结快照（只读） */
int snapshot_freeze(uint32_t id);

/* 写时复制：在修改块前调用，将原块复制到快照存储 */
int snapshot_cow_before_write(uint32_t block, const void *data);

/* 从快照恢复块 */
int snapshot_restore_block(uint32_t id, uint32_t block, void *data);

/* 获取快照信息 */
int snapshot_get_info(uint32_t id, snapshot_t *info);

/* 列出所有快照 */
int snapshot_list(snapshot_t *list, uint32_t max_count);

/* 自测 */
void snapshot_selftest(void);

#endif
