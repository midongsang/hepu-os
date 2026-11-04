/* ============================================================================
 * 赫普OS - 存储管理深化
 * ========================================================================== */

#include <stdint.h>
#include "storage_manager.h"
#include "vga.h"
#include "string.h"

static storage_manager_t sm;

void storage_manager_init(void)
{
    memset(&sm, 0, sizeof(storage_manager_t));
    sm.initialized = 1;
    sm.enabled = 1;
    sm.config.auto_clean = 1;
    sm.config.auto_clean_days = 30;
    sm.config.clean_cache_when_low = 1;
    sm.config.low_storage_threshold_percent = 15;
    sm.config.notify_large_files = 1;
    sm.config.large_file_threshold_mb = 100;
    sm.config.duplicate_detection = 1;
    sm.config.old_file_days = 30;
    /* 默认存储统计 */
    sm.stats.total_space = 256UL * 1024 * 1024 * 1024;
    sm.stats.used_space = 128UL * 1024 * 1024 * 1024;
    sm.stats.free_space = 128UL * 1024 * 1024 * 1024;
    sm.stats.system_space = 16UL * 1024 * 1024 * 1024;
    sm.stats.app_space = 48UL * 1024 * 1024 * 1024;
    sm.stats.photo_space = 32UL * 1024 * 1024 * 1024;
    sm.stats.video_space = 20UL * 1024 * 1024 * 1024;
    sm.stats.cache_space = 8UL * 1024 * 1024 * 1024;
    sm.stats.usage_percent = 50;

    vga_puts("[STORAGE] storage manager init done \n");
    vga_printf(" total space: 256GB, used: %d%%, auto cleanup: %s\n",
               sm.stats.usage_percent, sm.config.auto_clean ? " on " : " close ");
}

int sm_analyze(void)
{
    sm.stats.usage_percent = (int)(((uint32_t)sm.stats.used_space * 100u) / (uint32_t)sm.stats.total_space);
    return 0;
}

int sm_get_stats(sm_stats_t *stats)
{
    if (!stats) return -1;
    memcpy(stats, &sm.stats, sizeof(sm_stats_t));
    return 0;
}

int sm_get_usage_percent(int *percent)
{
    if (!percent) return -1;
    *percent = sm.stats.usage_percent;
    return 0;
}

int sm_scan_directory(const char *path)
{
    (void)path;
    return 0;
}

int sm_file_list(sm_file_t *files, int max)
{
    if (!files || max <= 0) return 0;
    int count = sm.file_count < max ? sm.file_count : max;
    for (int i = 0; i < count; i++) memcpy(&files[i], &sm.files[i], sizeof(sm_file_t));
    return count;
}

int sm_file_list_by_category(sm_category_t cat, sm_file_t *files, int max)
{
    if (!files || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < SM_MAX_FILES && count < max; i++) {
        if (sm.files[i].used && sm.files[i].category == cat)
            memcpy(&files[count++], &sm.files[i], sizeof(sm_file_t));
    }
    return count;
}

int sm_file_get(int id, sm_file_t *file)
{
    if (id < 0 || id >= SM_MAX_FILES || !sm.files[id].used || !file) return -1;
    memcpy(file, &sm.files[id], sizeof(sm_file_t));
    return 0;
}

int sm_file_delete(int id)
{
    if (id < 0 || id >= SM_MAX_FILES || !sm.files[id].used) return -1;
    uint64_t freed = sm.files[id].size;
    sm.files[id].used = 0;
    sm.file_count--;
    sm.stats.used_space -= freed;
    sm.stats.free_space += freed;
    sm.total_cleaned += freed;
    return 0;
}

int sm_file_get_large(sm_file_t *files, int max, uint64_t threshold)
{
    if (!files || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < SM_MAX_FILES && count < max; i++) {
        if (sm.files[i].used && sm.files[i].size >= threshold)
            memcpy(&files[count++], &sm.files[i], sizeof(sm_file_t));
    }
    return count;
}

int sm_file_get_old(sm_file_t *files, int max, int days)
{
    (void)days;
    if (!files || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < SM_MAX_FILES && count < max; i++) {
        if (sm.files[i].used && sm.files[i].is_old)
            memcpy(&files[count++], &sm.files[i], sizeof(sm_file_t));
    }
    return count;
}

int sm_detect_duplicates(void)
{
    sm.duplicate_count = 0;
    return 0;
}

int sm_get_duplicates(sm_duplicate_group_t *groups, int max)
{
    if (!groups || max <= 0) return 0;
    int count = sm.duplicate_count < max ? sm.duplicate_count : max;
    for (int i = 0; i < count; i++) memcpy(&groups[i], &sm.duplicates[i], sizeof(sm_duplicate_group_t));
    return count;
}

int sm_get_duplicate_files(int group_id, sm_file_t *files, int max)
{
    if (group_id < 0 || group_id >= SM_MAX_FILES / 2 || !sm.duplicates[group_id].used) return -1;
    if (!files || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < sm.duplicates[group_id].file_count && count < max; i++) {
        int fid = sm.duplicates[group_id].file_ids[i];
        if (fid >= 0 && fid < SM_MAX_FILES && sm.files[fid].used)
            memcpy(&files[count++], &sm.files[fid], sizeof(sm_file_t));
    }
    return count;
}

int sm_delete_duplicate(int group_id, int keep_index)
{
    if (group_id < 0 || group_id >= SM_MAX_FILES / 2 || !sm.duplicates[group_id].used) return -1;
    for (int i = 0; i < sm.duplicates[group_id].file_count; i++) {
        if (i != keep_index) {
            int fid = sm.duplicates[group_id].file_ids[i];
            if (fid >= 0 && fid < SM_MAX_FILES && sm.files[fid].used)
                sm_file_delete(fid);
        }
    }
    return 0;
}

int sm_app_register(const char *name, const char *package)
{
    for (int i = 0; i < SM_MAX_APPS; i++) {
        if (!sm.apps[i].used) {
            memset(&sm.apps[i], 0, sizeof(sm_app_storage_t));
            sm.apps[i].used = 1;
            sm.apps[i].id = i;
            if (name) strncpy(sm.apps[i].app_name, name, 127);
            if (package) strncpy(sm.apps[i].package, package, 127);
            sm.apps[i].cache_clearable = 1;
            sm.app_count++;
            return i;
        }
    }
    return -1;
}

int sm_app_update(int app_id, uint64_t app_size, uint64_t data_size, uint64_t cache_size)
{
    if (app_id < 0 || app_id >= SM_MAX_APPS || !sm.apps[app_id].used) return -1;
    sm.apps[app_id].app_size = app_size;
    sm.apps[app_id].data_size = data_size;
    sm.apps[app_id].cache_size = cache_size;
    sm.apps[app_id].total_size = app_size + data_size + cache_size;
    return 0;
}

int sm_app_get(int app_id, sm_app_storage_t *app)
{
    if (app_id < 0 || app_id >= SM_MAX_APPS || !sm.apps[app_id].used || !app) return -1;
    memcpy(app, &sm.apps[app_id], sizeof(sm_app_storage_t));
    return 0;
}

int sm_app_list(sm_app_storage_t *apps, int max)
{
    if (!apps || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < SM_MAX_APPS && count < max; i++) {
        if (sm.apps[i].used) memcpy(&apps[count++], &sm.apps[i], sizeof(sm_app_storage_t));
    }
    return count;
}

int sm_app_clear_cache(int app_id)
{
    if (app_id < 0 || app_id >= SM_MAX_APPS || !sm.apps[app_id].used) return -1;
    uint64_t freed = sm.apps[app_id].cache_size;
    sm.apps[app_id].cache_size = 0;
    sm.apps[app_id].total_size -= freed;
    sm.total_cache_cleared += freed;
    sm.stats.cache_space -= freed;
    sm.stats.free_space += freed;
    return (int)(freed / 1024 / 1024);
}

int sm_app_clear_data(int app_id)
{
    if (app_id < 0 || app_id >= SM_MAX_APPS || !sm.apps[app_id].used) return -1;
    uint64_t freed = sm.apps[app_id].data_size;
    sm.apps[app_id].data_size = 0;
    sm.apps[app_id].total_size -= freed;
    return (int)(freed / 1024 / 1024);
}

int sm_app_get_top(sm_app_storage_t *apps, int max)
{
    return sm_app_list(apps, max);
}

int sm_clear_all_cache(void)
{
    uint64_t total = 0;
    for (int i = 0; i < SM_MAX_APPS; i++) {
        if (sm.apps[i].used && sm.apps[i].cache_clearable) {
            total += sm.apps[i].cache_size;
            sm.apps[i].cache_size = 0;
            sm.apps[i].total_size = sm.apps[i].app_size + sm.apps[i].data_size;
        }
    }
    sm.total_cache_cleared += total;
    sm.stats.cache_space = 0;
    sm.stats.free_space += total;
    return (int)(total / 1024 / 1024);
}

int sm_clear_app_cache(int app_id)
{
    return sm_app_clear_cache(app_id);
}

int sm_clear_temp_files(void)
{
    uint64_t freed = 512 * 1024 * 1024;  /* 512MB */
    sm.total_cleaned += freed;
    sm.stats.free_space += freed;
    return (int)(freed / 1024 / 1024);
}

int sm_clear_downloads(int older_than_days)
{
    (void)older_than_days;
    uint64_t freed = 256 * 1024 * 1024;
    sm.total_cleaned += freed;
    return (int)(freed / 1024 / 1024);
}

int sm_get_cache_size(uint64_t *size)
{
    if (!size) return -1;
    *size = sm.stats.cache_space;
    return 0;
}

int sm_find_large_files(uint64_t threshold_mb)
{
    (void)threshold_mb;
    return 0;
}

int sm_get_large_files(sm_file_t *files, int max)
{
    return sm_file_list(files, max);
}

int sm_set_config(const sm_config_t *config)
{
    if (!config) return -1;
    memcpy(&sm.config, config, sizeof(sm_config_t));
    return 0;
}

int sm_get_config(sm_config_t *config)
{
    if (!config) return -1;
    memcpy(config, &sm.config, sizeof(sm_config_t));
    return 0;
}

int sm_set_auto_clean(int enable)
{
    sm.config.auto_clean = enable ? 1 : 0;
    return 0;
}

int sm_set_low_storage_threshold(int percent)
{
    sm.config.low_storage_threshold_percent = percent;
    return 0;
}

int sm_clean_selected(int *file_ids, int count)
{
    uint64_t freed = 0;
    for (int i = 0; i < count; i++) {
        if (file_ids[i] >= 0 && file_ids[i] < SM_MAX_FILES && sm.files[file_ids[i]].used) {
            freed += sm.files[file_ids[i]].size;
            sm.files[file_ids[i]].used = 0;
            sm.file_count--;
        }
    }
    sm.total_cleaned += freed;
    sm.stats.free_space += freed;
    return (int)(freed / 1024 / 1024);
}

int sm_clean_recommended(uint64_t *freed)
{
    uint64_t f = sm.stats.cache_space / 2;
    sm.total_cleaned += f;
    sm.stats.cache_space -= f;
    sm.stats.free_space += f;
    sm.clean_count++;
    if (freed) *freed = f;
    return 0;
}

int sm_get_recommended_clean(sm_file_t *files, int max)
{
    return sm_file_list(files, max);
}

int sm_get_full_stats(storage_manager_t *stats)
{
    if (!stats) return -1;
    memcpy(stats, &sm, sizeof(storage_manager_t));
    return 0;
}
