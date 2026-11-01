/* ============================================================================
 * 赫普OS - 快照（Snapshot）
 * 基于《赫普OS内存、显存与存储资源管理详解》3.2节实现
 *
 * 实现：
 * - 写时复制（COW）块级快照
 * - 最多8个快照，每个快照跟踪1024个块
 * - 快照创建/删除/冻结
 * - 写时复制：修改块前先复制原块到快照存储
 * - 快照恢复：从快照存储恢复块
 * - 内存版实现（快照数据存储在内存中，后续可持久化到磁盘）
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "snapshot.h"
#include "kheap.h"
#include "vga.h"

#define SNAPSHOT_BLOCK_SIZE  4096

/* 全局快照管理器 */
static snapshot_manager_t snap_mgr;

/* 快照存储（内存版，每个块4KB） */
static uint8_t *snapshot_storage = NULL;
static uint32_t snapshot_storage_blocks = 0;
static uint32_t snapshot_storage_used = 0;

/* 分配快照存储块 */
static int snapshot_alloc_storage_block(void)
{
    if (snapshot_storage_used >= snapshot_storage_blocks) return -1;
    return snapshot_storage_used++;
}

/* ============================================================================
 * 公共接口
 * ========================================================================== */

int snapshot_init(void)
{
    if (snap_mgr.initialized) return 0;

    memset(&snap_mgr, 0, sizeof(snap_mgr));
    snap_mgr.next_id = 1;

    /* V6.85: 直接分配较小的存储（256块=1MB），避免32MB大分配导致OOM死锁
     * 原设计：8快照*1024块*4KB=32MB，但内核堆仅8MB，首次分配必失败，
     * kmalloc OOM处理持有锁调用vga_printf可能导致递归死锁。 */
    snapshot_storage_blocks = 256;  /* 256 * 4KB = 1MB */
    snapshot_storage = (uint8_t *)kmalloc(snapshot_storage_blocks * SNAPSHOT_BLOCK_SIZE);
    if (!snapshot_storage) {
        /* 再次降级：64块=256KB */
        snapshot_storage_blocks = 64;
        snapshot_storage = (uint8_t *)kmalloc(snapshot_storage_blocks * SNAPSHOT_BLOCK_SIZE);
        if (!snapshot_storage) return -1;
    }
    snapshot_storage_used = 0;
    memset(snapshot_storage, 0, snapshot_storage_blocks * SNAPSHOT_BLOCK_SIZE);

    snap_mgr.initialized = 1;
    return 0;
}

int snapshot_create(const char *name)
{
    if (!snap_mgr.initialized) return -1;

    /* 查找空槽位 */
    int idx = -1;
    for (int i = 0; i < SNAPSHOT_MAX_COUNT; i++) {
        if (snap_mgr.snapshots[i].state == SNAPSHOT_STATE_EMPTY) {
            idx = i;
            break;
        }
    }
    if (idx < 0) return -2;  /* 快照数已满 */

    snapshot_t *snap = &snap_mgr.snapshots[idx];
    snap->id = snap_mgr.next_id++;
    snap->state = SNAPSHOT_STATE_ACTIVE;
    snap->create_time = 0;  /* TODO: 时间戳 */
    snap->block_count = 0;
    memset(snap->block_map, 0, sizeof(snap->block_map));

    if (name) {
        strncpy(snap->name, name, SNAPSHOT_NAME_LEN - 1);
        snap->name[SNAPSHOT_NAME_LEN - 1] = '\0';
    } else {
        snprintf(snap->name, SNAPSHOT_NAME_LEN, "snap-%d", snap->id);
    }

    return snap->id;
}

int snapshot_delete(uint32_t id)
{
    if (!snap_mgr.initialized) return -1;

    for (int i = 0; i < SNAPSHOT_MAX_COUNT; i++) {
        if (snap_mgr.snapshots[i].id == id &&
            snap_mgr.snapshots[i].state != SNAPSHOT_STATE_EMPTY) {
            memset(&snap_mgr.snapshots[i], 0, sizeof(snapshot_t));
            return 0;
        }
    }
    return -2;  /* 快照不存在 */
}

int snapshot_freeze(uint32_t id)
{
    if (!snap_mgr.initialized) return -1;

    for (int i = 0; i < SNAPSHOT_MAX_COUNT; i++) {
        if (snap_mgr.snapshots[i].id == id &&
            snap_mgr.snapshots[i].state == SNAPSHOT_STATE_ACTIVE) {
            snap_mgr.snapshots[i].state = SNAPSHOT_STATE_FROZEN;
            return 0;
        }
    }
    return -2;
}

int snapshot_cow_before_write(uint32_t block, const void *data)
{
    if (!snap_mgr.initialized || !data) return -1;

    /* 遍历所有活跃快照，检查是否已跟踪该块 */
    for (int i = 0; i < SNAPSHOT_MAX_COUNT; i++) {
        snapshot_t *snap = &snap_mgr.snapshots[i];
        if (snap->state != SNAPSHOT_STATE_ACTIVE) continue;

        /* 检查是否已跟踪该块 */
        int found = 0;
        for (uint32_t j = 0; j < snap->block_count; j++) {
            if (snap->block_map[j].orig_block == block && snap->block_map[j].valid) {
                found = 1;
                break;
            }
        }
        if (found) continue;  /* 已跟踪，不需要再次复制 */

        /* 检查块映射表是否已满 */
        if (snap->block_count >= SNAPSHOT_MAX_BLOCKS) continue;

        /* 分配快照存储块 */
        int storage_blk = snapshot_alloc_storage_block();
        if (storage_blk < 0) continue;

        /* 复制原块到快照存储 */
        memcpy(snapshot_storage + storage_blk * SNAPSHOT_BLOCK_SIZE, data, SNAPSHOT_BLOCK_SIZE);

        /* 记录块映射 */
        snapshot_block_map_t *map = &snap->block_map[snap->block_count];
        map->orig_block = block;
        map->snap_block = storage_blk;
        map->valid = 1;
        snap->block_count++;
    }

    return 0;
}

int snapshot_restore_block(uint32_t id, uint32_t block, void *data)
{
    if (!snap_mgr.initialized || !data) return -1;

    for (int i = 0; i < SNAPSHOT_MAX_COUNT; i++) {
        snapshot_t *snap = &snap_mgr.snapshots[i];
        if (snap->id != id || snap->state == SNAPSHOT_STATE_EMPTY) continue;

        /* 查找块映射 */
        for (uint32_t j = 0; j < snap->block_count; j++) {
            if (snap->block_map[j].orig_block == block && snap->block_map[j].valid) {
                memcpy(data, snapshot_storage + snap->block_map[j].snap_block * SNAPSHOT_BLOCK_SIZE,
                       SNAPSHOT_BLOCK_SIZE);
                return 0;
            }
        }
        return -2;  /* 块不在快照中 */
    }
    return -3;  /* 快照不存在 */
}

int snapshot_get_info(uint32_t id, snapshot_t *info)
{
    if (!snap_mgr.initialized || !info) return -1;

    for (int i = 0; i < SNAPSHOT_MAX_COUNT; i++) {
        if (snap_mgr.snapshots[i].id == id &&
            snap_mgr.snapshots[i].state != SNAPSHOT_STATE_EMPTY) {
            memcpy(info, &snap_mgr.snapshots[i], sizeof(snapshot_t));
            return 0;
        }
    }
    return -2;
}

int snapshot_list(snapshot_t *list, uint32_t max_count)
{
    if (!snap_mgr.initialized || !list) return -1;

    uint32_t count = 0;
    for (int i = 0; i < SNAPSHOT_MAX_COUNT && count < max_count; i++) {
        if (snap_mgr.snapshots[i].state != SNAPSHOT_STATE_EMPTY) {
            memcpy(&list[count], &snap_mgr.snapshots[i], sizeof(snapshot_t));
            count++;
        }
    }
    return count;
}

/* ============================================================================
 * 自测
 * ========================================================================== */
void snapshot_selftest(void)
{
    extern void serial_puts(uint16_t, const char *);
    extern void serial_puthex(uint16_t, uint32_t);
    #define SLOG(s) serial_puts(0x3F8, s)

    SLOG("[SNAPSHOT] selftest: start\n");
    int pass = 0, fail = 0;

    /* 测试1：初始化 */
    int ret = snapshot_init();
    if (ret == 0) {
        SLOG("[SNAPSHOT] test1 init: PASS\n");
        pass++;
    } else {
        SLOG("[SNAPSHOT] test1 init: FAIL\n");
        fail++;
    }

    /* 测试2：创建快照 */
    int id1 = snapshot_create("test-snap-1");
    if (id1 > 0) {
        SLOG("[SNAPSHOT] test2 create snapshot: PASS (id=");
        serial_puthex(0x3F8, id1);
        SLOG(")\n");
        pass++;
    } else {
        SLOG("[SNAPSHOT] test2 create snapshot: FAIL\n");
        fail++;
    }

    /* 测试3：写时复制（COW） */
    static uint8_t orig_data[4096];
    static uint8_t restored_data[4096];
    for (int i = 0; i < 4096; i++) orig_data[i] = i & 0xFF;

    ret = snapshot_cow_before_write(100, orig_data);
    if (ret == 0) {
        snapshot_t info;
        snapshot_get_info(id1, &info);
        if (info.block_count == 1) {
            SLOG("[SNAPSHOT] test3 COW: PASS (block_count=1)\n");
            pass++;
        } else {
            SLOG("[SNAPSHOT] test3 COW: FAIL (block_count=");
            serial_puthex(0x3F8, info.block_count);
            SLOG(")\n");
            fail++;
        }
    } else {
        SLOG("[SNAPSHOT] test3 COW: FAIL\n");
        fail++;
    }

    /* 测试4：从快照恢复块 */
    memset(restored_data, 0, 4096);
    ret = snapshot_restore_block(id1, 100, restored_data);
    if (ret == 0 && memcmp(orig_data, restored_data, 4096) == 0) {
        SLOG("[SNAPSHOT] test4 restore block: PASS\n");
        pass++;
    } else {
        SLOG("[SNAPSHOT] test4 restore block: FAIL\n");
        fail++;
    }

    /* 测试5：重复COW不重复跟踪 */
    ret = snapshot_cow_before_write(100, orig_data);
    snapshot_t info;
    snapshot_get_info(id1, &info);
    if (info.block_count == 1) {
        SLOG("[SNAPSHOT] test5 duplicate COW dedup: PASS\n");
        pass++;
    } else {
        SLOG("[SNAPSHOT] test5 duplicate COW dedup: FAIL (block_count=");
        serial_puthex(0x3F8, info.block_count);
        SLOG(")\n");
        fail++;
    }

    /* 测试6：冻结快照 */
    ret = snapshot_freeze(id1);
    if (ret == 0) {
        snapshot_get_info(id1, &info);
        if (info.state == SNAPSHOT_STATE_FROZEN) {
            SLOG("[SNAPSHOT] test6 freeze: PASS\n");
            pass++;
        } else {
            SLOG("[SNAPSHOT] test6 freeze: FAIL (state=");
            serial_puthex(0x3F8, info.state);
            SLOG(")\n");
            fail++;
        }
    } else {
        SLOG("[SNAPSHOT] test6 freeze: FAIL\n");
        fail++;
    }

    /* 测试7：删除快照 */
    ret = snapshot_delete(id1);
    if (ret == 0) {
        snapshot_t list[SNAPSHOT_MAX_COUNT];
        int count = snapshot_list(list, SNAPSHOT_MAX_COUNT);
        if (count == 0) {
            SLOG("[SNAPSHOT] test7 delete: PASS\n");
            pass++;
        } else {
            SLOG("[SNAPSHOT] test7 delete: FAIL (count=");
            serial_puthex(0x3F8, count);
            SLOG(")\n");
            fail++;
        }
    } else {
        SLOG("[SNAPSHOT] test7 delete: FAIL\n");
        fail++;
    }

    SLOG("[SNAPSHOT] selftest: done (pass=");
    serial_puthex(0x3F8, pass);
    SLOG(", fail=");
    serial_puthex(0x3F8, fail);
    SLOG(")\n");

    #undef SLOG
}
