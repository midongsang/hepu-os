#ifndef QPACK_DYNAMIC_H
#include "types.h"
#define QPACK_DYNAMIC_H

#include <stdint.h>

#define QPACK_DYNAMIC_MAX_ENTRIES 100
#define QPACK_DYNAMIC_MAX_NAME 256
#define QPACK_DYNAMIC_MAX_VALUE 1024
#define QPACK_DYNAMIC_DEFAULT_CAPACITY 4096
#define QPACK_DYNAMIC_ENTRY_OVERHEAD 32

/* QPACK动态表条目 */
typedef struct {
    u8  name[QPACK_DYNAMIC_MAX_NAME];
    u8  value[QPACK_DYNAMIC_MAX_VALUE];
    u32 name_len;
    u32 value_len;
    u32 size;  /* name_len + value_len + QPACK_DYNAMIC_ENTRY_OVERHEAD */
    int      in_use;
} qpack_dynamic_entry_t;

/* QPACK动态表 */
typedef struct {
    qpack_dynamic_entry_t entries[QPACK_DYNAMIC_MAX_ENTRIES];
    u32 capacity;     /* 最大容量（字节） */
    u32 current_size; /* 当前使用大小 */
    u32 entry_count;  /* 当前条目数 */
    u32 insert_count; /* 总插入次数 */
    u32 evict_count;  /* 驱逐次数 */
    int      initialized;
} qpack_dynamic_table_t;

void qpack_dynamic_init(qpack_dynamic_table_t *table, u32 capacity);
int qpack_dynamic_set_capacity(qpack_dynamic_table_t *table, u32 capacity);
int qpack_dynamic_insert(qpack_dynamic_table_t *table, const u8 *name,
                          u32 name_len, const u8 *value,
                          u32 value_len);
int qpack_dynamic_lookup(const qpack_dynamic_table_t *table, u32 index,
                          u8 *name, u32 *name_len,
                          u8 *value, u32 *value_len);
int qpack_dynamic_find(const qpack_dynamic_table_t *table, const u8 *name,
                        u32 name_len, const u8 *value,
                        u32 value_len, u32 *index);
int qpack_dynamic_find_name(const qpack_dynamic_table_t *table,
                             const u8 *name, u32 name_len,
                             u32 *index);
int qpack_dynamic_evict(qpack_dynamic_table_t *table, u32 needed_size);
int qpack_dynamic_get_size(const qpack_dynamic_table_t *table);
int qpack_dynamic_get_count(const qpack_dynamic_table_t *table);
int qpack_dynamic_get_stats(const qpack_dynamic_table_t *table,
                             u32 *capacity, u32 *current_size,
                             u32 *entry_count, u32 *insert_count,
                             u32 *evict_count);
int qpack_dynamic_clear(qpack_dynamic_table_t *table);

#endif /* QPACK_DYNAMIC_H */
