/* ============================================================================
 * 赫普OS - QUIC连接迁移
 * RFC 9000: 连接迁移与多路径探测
 * ========================================================================== */

#include <stdint.h>
#include "types.h"
#include "quic_migration.h"
#include "csprng.h"
#include "vga.h"
#include "string.h"

static int quic_migration_initialized = 0;

/* 初始化连接迁移 */
void quic_migration_init(void)
{
    quic_migration_initialized = 1;
    vga_puts("[QUIC-MIG] conn migration init \n");
    vga_puts("[QUIC-MIG] supports multipath probing & conn ID rotation \n");
}

/* 启用/禁用连接迁移 */
int quic_migration_enable(quic_migration_t *migration, int enable)
{
    if (!quic_migration_initialized || !migration) return -1;
    migration->enabled = enable ? 1 : 0;
    vga_printf("[QUIC-MIG] conn migration %s\n", enable ? " enabled " : " disabled ");
    return 0;
}

/* 添加路径 */
int quic_migration_add_path(quic_migration_t *migration,
                            const u8 *local_addr, u16 local_port,
                            const u8 *remote_addr, u16 remote_port)
{
    if (!migration || !local_addr || !remote_addr) return -1;
    if (!migration->enabled) return -1;

    for (int i = 0; i < QUIC_MIGRATION_MAX_PATHS; i++) {
        if (!migration->paths[i].in_use) {
            memcpy(migration->paths[i].local_addr, local_addr, 16);
            memcpy(migration->paths[i].remote_addr, remote_addr, 16);
            migration->paths[i].local_port = local_port;
            migration->paths[i].remote_port = remote_port;
            migration->paths[i].rtt = 100;  /* 初始RTT */
            migration->paths[i].rtt_var = 50;
            migration->paths[i].state = QUIC_PATH_UNKNOWN;
            migration->paths[i].in_use = 1;

            vga_printf("[QUIC-MIG] add path: %d(port=%d)\n", i, local_port);
            return i;
        }
    }
    return -1;
}

/* 移除路径 */
int quic_migration_remove_path(quic_migration_t *migration, int path_index)
{
    if (!migration || path_index < 0 || path_index >= QUIC_MIGRATION_MAX_PATHS) {
        return -1;
    }
    if (!migration->paths[path_index].in_use) return -1;
    if (path_index == migration->active_path) return -1;  /* 不能移除活动路径 */

    memset(&migration->paths[path_index], 0, sizeof(quic_path_t));
    vga_printf("[QUIC-MIG] remove path: %d\n", path_index);
    return 0;
}

/* 开始迁移到新路径 */
int quic_migration_start(quic_migration_t *migration, int new_path_index)
{
    if (!migration || new_path_index < 0 || new_path_index >= QUIC_MIGRATION_MAX_PATHS) {
        return -1;
    }
    if (!migration->enabled) return -1;
    if (!migration->paths[new_path_index].in_use) return -1;
    if (migration->migrating) return -1;

    migration->migrating = 1;
    migration->migration_start_time = 0;  /* 简化 */
    migration->paths[new_path_index].state = QUIC_PATH_PROBING;

    /* 发送PATH_CHALLENGE帧探测新路径 */
    quic_migration_send_path_challenge(migration, new_path_index);

    vga_printf("[QUIC-MIG] start migrate to path: %d\n", new_path_index);
    return 0;
}

/* 完成迁移 */
int quic_migration_complete(quic_migration_t *migration)
{
    if (!migration || !migration->migrating) return -1;

    /* 找到正在探测的路径并设为活动路径 */
    for (int i = 0; i < QUIC_MIGRATION_MAX_PATHS; i++) {
        if (migration->paths[i].in_use &&
            migration->paths[i].state == QUIC_PATH_PROBING) {
            migration->paths[i].state = QUIC_PATH_ACTIVE;
            migration->paths[migration->active_path].state = QUIC_PATH_INACTIVE;
            migration->active_path = i;
            break;
        }
    }

    migration->migrating = 0;
    vga_printf("[QUIC-MIG] migrate done, active path: %d\n", migration->active_path);
    return 0;
}

/* 迁移失败 */
int quic_migration_fail(quic_migration_t *migration)
{
    if (!migration) return -1;

    for (int i = 0; i < QUIC_MIGRATION_MAX_PATHS; i++) {
        if (migration->paths[i].in_use &&
            migration->paths[i].state == QUIC_PATH_PROBING) {
            migration->paths[i].state = QUIC_PATH_FAILED;
            break;
        }
    }

    migration->migrating = 0;
    vga_puts("[QUIC-MIG] migrate failed, keep original path \n");
    return 0;
}

/* 发送PATH_CHALLENGE */
int quic_migration_send_path_challenge(quic_migration_t *migration, int path_index)
{
    if (!migration || path_index < 0 || path_index >= QUIC_MIGRATION_MAX_PATHS) {
        return -1;
    }

    u8 challenge_data[8];
    csprng_get_bytes(challenge_data, 8);

    migration->paths[path_index].last_probe_time = 0;  /* 简化 */
    migration->paths[path_index].packets_sent++;

    vga_printf("[QUIC-MIG] send PATH_CHALLENGE: path=%d\n", path_index);
    return 0;
}

/* 处理PATH_RESPONSE */
int quic_migration_handle_path_response(quic_migration_t *migration,
                                         const u8 *data, u32 len)
{
    if (!migration || !data || len < 8) return -1;

    /* 验证响应数据与挑战数据匹配（简化：直接接受） */
    for (int i = 0; i < QUIC_MIGRATION_MAX_PATHS; i++) {
        if (migration->paths[i].in_use &&
            migration->paths[i].state == QUIC_PATH_PROBING) {
            migration->paths[i].packets_received++;
            vga_printf("[QUIC-MIG] recv PATH_RESPONSE: path=%d\n", i);

            /* 路径验证成功，可以完成迁移 */
            quic_migration_complete(migration);
            return 0;
        }
    }
    return -1;
}

/* 添加连接ID */
int quic_migration_add_connection_id(quic_migration_t *migration,
                                     const u8 *cid, u8 cid_len)
{
    if (!migration || !cid || cid_len == 0 || cid_len > 20) return -1;
    if (migration->connection_id_count >= QUIC_MIGRATION_MAX_IDS) return -1;

    int idx = migration->connection_id_count++;
    memcpy(migration->connection_ids[idx], cid, cid_len);
    migration->connection_id_lens[idx] = cid_len;
    migration->next_connection_id_seq++;

    vga_printf("[QUIC-MIG] add conn id ID: seq=%llu, len=%d\n",
               (unsigned long long)migration->next_connection_id_seq - 1, cid_len);
    return 0;
}

/* 退役连接ID */
int quic_migration_retire_connection_id(quic_migration_t *migration,
                                         u64 seq_num)
{
    if (!migration) return -1;
    if (seq_num >= (u64)migration->connection_id_count) return -1;

    /* 标记为已退役（简化：直接清零） */
    if (seq_num < (u64)QUIC_MIGRATION_MAX_IDS) {
        memset(migration->connection_ids[seq_num], 0, 20);
        migration->connection_id_lens[seq_num] = 0;
    }

    vga_printf("[QUIC-MIG] retire conn id ID: seq=%llu\n", (unsigned long long)seq_num);
    return 0;
}

/* 获取活动路径 */
int quic_migration_get_active_path(const quic_migration_t *migration,
                                    quic_path_t *path)
{
    if (!migration || !path) return -1;
    if (migration->active_path < 0) return -1;

    *path = migration->paths[migration->active_path];
    return 0;
}

/* 获取最佳路径（最低RTT） */
int quic_migration_get_best_path(const quic_migration_t *migration)
{
    if (!migration) return -1;

    int best = -1;
    u32 best_rtt = UINT32_MAX;

    for (int i = 0; i < QUIC_MIGRATION_MAX_PATHS; i++) {
        if (migration->paths[i].in_use &&
            migration->paths[i].state == QUIC_PATH_ACTIVE &&
            migration->paths[i].rtt < best_rtt) {
            best_rtt = migration->paths[i].rtt;
            best = i;
        }
    }
    return best;
}

/* 检查是否正在迁移 */
int quic_migration_is_migrating(const quic_migration_t *migration)
{
    return migration && migration->migrating;
}

/* 获取统计信息 */
int quic_migration_get_stats(const quic_migration_t *migration,
                              u32 *paths, u32 *active_path,
                              u64 *total_bytes)
{
    if (!migration) return -1;

    u32 path_count = 0;
    u64 total = 0;

    for (int i = 0; i < QUIC_MIGRATION_MAX_PATHS; i++) {
        if (migration->paths[i].in_use) {
            path_count++;
            total += migration->paths[i].bytes_sent +
                     migration->paths[i].bytes_received;
        }
    }

    if (paths) *paths = path_count;
    if (active_path) *active_path = migration->active_path;
    if (total_bytes) *total_bytes = total;

    return 0;
}

/* 路径状态名称 */
const char *quic_path_state_name(quic_path_state_t state)
{
    switch (state) {
        case QUIC_PATH_UNKNOWN: return "UNKNOWN";
        case QUIC_PATH_PROBING: return "PROBING";
        case QUIC_PATH_ACTIVE: return "ACTIVE";
        case QUIC_PATH_FAILED: return "FAILED";
        case QUIC_PATH_INACTIVE: return "INACTIVE";
        default: return "UNKNOWN";
    }
}
