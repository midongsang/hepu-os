/* ============================================================================
 * 赫普OS - QPACK动态表
 * RFC 9204: QPACK头部压缩 - 动态表
 * ========================================================================== */

#include <stdint.h>
#include "types.h"
#include "qpack_dynamic.h"
#include "vga.h"
#include "string.h"

/* 初始化动态表 */
void qpack_dynamic_init(qpack_dynamic_table_t *table, u32 capacity)
{
    if (!table) return;

    memset(table, 0, sizeof(qpack_dynamic_table_t));
    table->capacity = capacity ? capacity : QPACK_DYNAMIC_DEFAULT_CAPACITY;
    table->initialized = 1;

    vga_printf("[QPACK-DYN] dynamic table init: capacity=%u\n", table->capacity);
}

/* 设置容量 */
int qpack_dynamic_set_capacity(qpack_dynamic_table_t *table, u32 capacity)
{
    if (!table || !table->initialized) return -1;

    table->capacity = capacity;

    /* 如果新容量小于当前大小，驱逐条目 */
    while (table->current_size > table->capacity && table->entry_count > 0) {
        qpack_dynamic_evict(table, table->current_size - table->capacity);
    }

    vga_printf("[QPACK-DYN] set capacity: %u\n", capacity);
    return 0;
}

/* 插入条目 */
int qpack_dynamic_insert(qpack_dynamic_table_t *table, const u8 *name,
                          u32 name_len, const u8 *value,
                          u32 value_len)
{
    if (!table || !table->initialized || !name || name_len == 0) return -1;
    if (name_len > QPACK_DYNAMIC_MAX_NAME) return -1;
    if (value_len > QPACK_DYNAMIC_MAX_VALUE) return -1;

    u32 entry_size = name_len + value_len + QPACK_DYNAMIC_ENTRY_OVERHEAD;

    /* 检查是否超过最大容量 */
    if (entry_size > table->capacity) {
        vga_puts("[QPACK-DYN] entry exceeds max cap , not insert \n");
        return -1;
    }

    /* 驱逐旧条目以腾出空间 */
    if (table->current_size + entry_size > table->capacity) {
        qpack_dynamic_evict(table, table->current_size + entry_size -
                            table->capacity);
    }

    /* 查找空闲槽位（从末尾插入，FIFO驱逐从头部） */
    int slot = -1;
    for (int i = 0; i < QPACK_DYNAMIC_MAX_ENTRIES; i++) {
        if (!table->entries[i].in_use) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        /* 没有空闲槽位，驱逐最旧的 */
        qpack_dynamic_evict(table, entry_size);
        for (int i = 0; i < QPACK_DYNAMIC_MAX_ENTRIES; i++) {
            if (!table->entries[i].in_use) {
                slot = i;
                break;
            }
        }
    }
    if (slot < 0) return -1;

    qpack_dynamic_entry_t *entry = &table->entries[slot];
    memcpy(entry->name, name, name_len);
    memcpy(entry->value, value, value_len);
    entry->name_len = name_len;
    entry->value_len = value_len;
    entry->size = entry_size;
    entry->in_use = 1;

    table->current_size += entry_size;
    table->entry_count++;
    table->insert_count++;

    vga_printf("[QPACK-DYN] insert: slot=%d, name=%.*s, size=%u(total: %u/%u)\n",
               slot, (int)name_len, name, entry_size,
               table->current_size, table->capacity);
    return slot;
}

/* 按索引查找 */
int qpack_dynamic_lookup(const qpack_dynamic_table_t *table, u32 index,
                          u8 *name, u32 *name_len,
                          u8 *value, u32 *value_len)
{
    if (!table || !table->initialized) return -1;
    if (index >= QPACK_DYNAMIC_MAX_ENTRIES) return -1;
    if (!table->entries[index].in_use) return -1;

    const qpack_dynamic_entry_t *entry = &table->entries[index];

    if (name && name_len) {
        u32 len = entry->name_len;
        if (*name_len < len) len = *name_len;
        memcpy(name, entry->name, len);
        *name_len = len;
    }
    if (value && value_len) {
        u32 len = entry->value_len;
        if (*value_len < len) len = *value_len;
        memcpy(value, entry->value, len);
        *value_len = len;
    }

    return 0;
}

/* 按名称和值查找（完全匹配） */
int qpack_dynamic_find(const qpack_dynamic_table_t *table, const u8 *name,
                        u32 name_len, const u8 *value,
                        u32 value_len, u32 *index)
{
    if (!table || !table->initialized || !name) return -1;

    for (u32 i = 0; i < QPACK_DYNAMIC_MAX_ENTRIES; i++) {
        if (table->entries[i].in_use &&
            table->entries[i].name_len == name_len &&
            table->entries[i].value_len == value_len &&
            memcmp(table->entries[i].name, name, name_len) == 0 &&
            memcmp(table->entries[i].value, value, value_len) == 0) {
            if (index) *index = i;
            return 0;
        }
    }
    return -1;
}

/* 按名称查找（名称匹配，返回第一个） */
int qpack_dynamic_find_name(const qpack_dynamic_table_t *table,
                             const u8 *name, u32 name_len,
                             u32 *index)
{
    if (!table || !table->initialized || !name) return -1;

    for (u32 i = 0; i < QPACK_DYNAMIC_MAX_ENTRIES; i++) {
        if (table->entries[i].in_use &&
            table->entries[i].name_len == name_len &&
            memcmp(table->entries[i].name, name, name_len) == 0) {
            if (index) *index = i;
            return 0;
        }
    }
    return -1;
}

/* 驱逐条目（FIFO，从最旧的开始） */
int qpack_dynamic_evict(qpack_dynamic_table_t *table, u32 needed_size)
{
    if (!table || !table->initialized) return -1;

    u32 evicted = 0;

    while (table->current_size > 0 &&
           table->current_size + needed_size > table->capacity &&
           table->entry_count > 0) {
        /* 找到最旧的条目（第一个in_use的） */
        for (int i = 0; i < QPACK_DYNAMIC_MAX_ENTRIES; i++) {
            if (table->entries[i].in_use) {
                table->current_size -= table->entries[i].size;
                table->entry_count--;
                table->evict_count++;
                memset(&table->entries[i], 0, sizeof(qpack_dynamic_entry_t));
                evicted++;
                break;
            }
        }
    }

    if (evicted > 0) {
        vga_printf("[QPACK-DYN] evict %u entries(needs %u bytes)\n",
                   evicted, needed_size);
    }
    return evicted;
}

/* 获取当前大小 */
int qpack_dynamic_get_size(const qpack_dynamic_table_t *table)
{
    return table ? (int)table->current_size : -1;
}

/* 获取条目数 */
int qpack_dynamic_get_count(const qpack_dynamic_table_t *table)
{
    return table ? (int)table->entry_count : -1;
}

/* 获取统计信息 */
int qpack_dynamic_get_stats(const qpack_dynamic_table_t *table,
                             u32 *capacity, u32 *current_size,
                             u32 *entry_count, u32 *insert_count,
                             u32 *evict_count)
{
    if (!table) return -1;
    if (capacity) *capacity = table->capacity;
    if (current_size) *current_size = table->current_size;
    if (entry_count) *entry_count = table->entry_count;
    if (insert_count) *insert_count = table->insert_count;
    if (evict_count) *evict_count = table->evict_count;
    return 0;
}

/* 清除动态表 */
int qpack_dynamic_clear(qpack_dynamic_table_t *table)
{
    if (!table || !table->initialized) return -1;

    memset(table->entries, 0, sizeof(table->entries));
    table->current_size = 0;
    table->entry_count = 0;

    vga_puts("[QPACK-DYN] dynamic table cleared \n");
    return 0;
}
