/* ============================================================================
 * 赫普OS - 分布式文件系统DFS（从distributed.c拆分）
 * ========================================================================== */

#include <stdint.h>
#include "distributed.h"
#include "vga.h"
#include "string.h"

extern distributed_t dist;

int dist_dfs_mkdir(const char *path)
{
    for (int i = 0; i < DIST_MAX_FILES; i++) {
        if (!dist.dfs.files[i].used) {
            memset(&dist.dfs.files[i], 0, sizeof(dfs_file_t));
            dist.dfs.files[i].used = 1;
            dist.dfs.files[i].id = i;
            if (path) strncpy(dist.dfs.files[i].path, path, 255);
            dist.dfs.files[i].directory = 1;
            dist.dfs.files[i].permissions = 0755;
            dist.dfs.file_count++;
            return i;
        }
    }
    return -1;
}

int dist_dfs_create(const char *path, const void *data, uint64_t size)
{
    (void)data;
    for (int i = 0; i < DIST_MAX_FILES; i++) {
        if (!dist.dfs.files[i].used) {
            memset(&dist.dfs.files[i], 0, sizeof(dfs_file_t));
            dist.dfs.files[i].used = 1;
            dist.dfs.files[i].id = i;
            if (path) strncpy(dist.dfs.files[i].path, path, 255);
            dist.dfs.files[i].size = size;
            dist.dfs.files[i].replication_factor = dist.dfs.replication_default;
            dist.dfs.files[i].permissions = 0644;
            /* 创建数据块 */
            uint32_t sz = (uint32_t)size; uint32_t cs = (uint32_t)dist.dfs.chunk_size; int chunks = (int)((sz + cs - 1) / cs);
            if (chunks > 64) chunks = 64;
            for (int j = 0; j < chunks; j++) {
                for (int k = 0; k < DIST_MAX_CHUNKS; k++) {
                    if (!dist.dfs.chunks[k].used) {
                        dist.dfs.chunks[k].used = 1;
                        dist.dfs.chunks[k].id = k;
                        dist.dfs.chunks[k].file_id = i;
                        dist.dfs.chunks[k].chunk_index = j;
                        dist.dfs.chunks[k].size = dist.dfs.chunk_size;
                        dist.dfs.chunks[k].replica_count = dist.dfs.replication_default;
                        dist.dfs.chunks[k].healthy = 1;
                        dist.dfs.files[i].chunk_ids[j] = k;
                        dist.dfs.chunk_count++;
                        break;
                    }
                }
            }
            dist.dfs.files[i].chunk_count = chunks;
            dist.dfs.file_count++;
            dist.dfs.used_space += size;
            dist.dfs.available_space -= size;
            return i;
        }
    }
    return -1;
}

int dist_dfs_read(const char *path, void *buffer, uint64_t max_size)
{
    (void)buffer;
    dfs_file_t file;
    if (dist_dfs_stat(path, &file) != 0) return -1;
    return (int)(file.size < max_size ? file.size : max_size);
}

int dist_dfs_write(const char *path, const void *data, uint64_t size)
{
    (void)data;
    dfs_file_t file;
    if (dist_dfs_stat(path, &file) != 0) {
        return dist_dfs_create(path, data, size);
    }
    return (int)size;
}

int dist_dfs_delete(const char *path)
{
    for (int i = 0; i < DIST_MAX_FILES; i++) {
        if (dist.dfs.files[i].used && strcmp(dist.dfs.files[i].path, path) == 0) {
            /* 删除数据块 */
            for (int j = 0; j < dist.dfs.files[i].chunk_count; j++) {
                int cid = dist.dfs.files[i].chunk_ids[j];
                if (cid >= 0 && cid < DIST_MAX_CHUNKS) {
                    dist.dfs.chunks[cid].used = 0;
                    dist.dfs.chunk_count--;
                }
            }
            dist.dfs.used_space -= dist.dfs.files[i].size;
            dist.dfs.available_space += dist.dfs.files[i].size;
            dist.dfs.files[i].used = 0;
            dist.dfs.file_count--;
            return 0;
        }
    }
    return -1;
}

int dist_dfs_list(const char *path, dfs_file_t *files, int max)
{
    (void)path;
    if (!files || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < DIST_MAX_FILES && count < max; i++) {
        if (dist.dfs.files[i].used) memcpy(&files[count++], &dist.dfs.files[i], sizeof(dfs_file_t));
    }
    return count;
}

int dist_dfs_stat(const char *path, dfs_file_t *file)
{
    if (!path || !file) return -1;
    for (int i = 0; i < DIST_MAX_FILES; i++) {
        if (dist.dfs.files[i].used && strcmp(dist.dfs.files[i].path, path) == 0) {
            memcpy(file, &dist.dfs.files[i], sizeof(dfs_file_t));
            return 0;
        }
    }
    return -1;
}

int dist_dfs_replicate(int file_id, int factor)
{
    if (file_id < 0 || file_id >= DIST_MAX_FILES || !dist.dfs.files[file_id].used) return -1;
    dist.dfs.files[file_id].replication_factor = factor;
    return 0;
}

int dist_dfs_balance(void)
{
    return 0;
}

int dist_dfs_get_stats(distributed_fs_t *stats)
{
    if (!stats) return -1;
    memcpy(stats, &dist.dfs, sizeof(distributed_fs_t));
    return 0;
}

/* 调度器 */
