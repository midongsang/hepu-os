/* ============================================================================
 * 赫普OS - 数据备份与迁移
 * ========================================================================== */

#include <stdint.h>
#include "backup_restore.h"
#include "vga.h"
#include "string.h"

static backup_restore_t br;

void backup_restore_init(void)
{
    memset(&br, 0, sizeof(backup_restore_t));
    br.initialized = 1;
    br.enabled = 1;
    br.config.auto_backup = 1;
    br.config.auto_backup_wifi_only = 1;
    br.config.auto_backup_charging = 1;
    br.config.auto_backup_interval_hours = 168;  /* 每周 */
    br.config.encrypt_backups = 1;
    br.config.compress_backups = 1;
    br.config.include_photos = 1;
    br.config.include_app_data = 1;
    br.config.include_system_settings = 1;
    br.config.max_backups = 5;
    br.config.default_location = BR_LOC_LOCAL;
    br.current_state = BR_STATE_IDLE;

    vga_puts("[BACKUP] data backup & migrate init done \n");
    vga_printf(" auto backup: %s, encryption: %s, compression: %s\n",
               br.config.auto_backup ? " on " : " close ",
               br.config.encrypt_backups ? " on " : " close ",
               br.config.compress_backups ? " on " : " close ");
}

int br_backup_create(const char *name, br_backup_type_t type, br_location_t location)
{
    for (int i = 0; i < BR_MAX_BACKUPS; i++) {
        if (!br.backups[i].used) {
            memset(&br.backups[i], 0, sizeof(br_backup_t));
            br.backups[i].used = 1;
            br.backups[i].id = i;
            if (name) strncpy(br.backups[i].name, name, 127);
            br.backups[i].type = type;
            br.backups[i].location = location;
            br.backups[i].create_time = 1;
            br.backups[i].encrypted = br.config.encrypt_backups;
            strncpy(br.backups[i].device_model, "HEPU-Phone", 63);
            strncpy(br.backups[i].os_version, "V0.34", 31);
            br.backups[i].state = BR_STATE_IDLE;
            br.backup_count++;
            return i;
        }
    }
    return -1;
}

int br_backup_add_item(int backup_id, br_backup_type_t type, const char *path)
{
    (void)type; (void)path;
    if (backup_id < 0 || backup_id >= BR_MAX_BACKUPS || !br.backups[backup_id].used) return -1;
    br.backups[backup_id].item_count++;
    return 0;
}

int br_backup_start(int backup_id)
{
    if (backup_id < 0 || backup_id >= BR_MAX_BACKUPS || !br.backups[backup_id].used) return -1;
    br.backups[backup_id].state = BR_STATE_BACKING_UP;
    br.current_state = BR_STATE_BACKING_UP;
    br.current_backup_id = backup_id;
    br.backups[backup_id].state = BR_STATE_COMPLETED;
    br.backups[backup_id].size = 1024 * 1024 * 512;  /* 512MB */
    br.backups[backup_id].compressed_size = 1024 * 1024 * 384;
    br.backups[backup_id].verified = 1;
    br.current_state = BR_STATE_IDLE;
    br.total_backups++;
    br.total_data_backed_up += br.backups[backup_id].size;
    return 0;
}

int br_backup_cancel(int backup_id)
{
    if (backup_id < 0 || backup_id >= BR_MAX_BACKUPS || !br.backups[backup_id].used) return -1;
    br.backups[backup_id].state = BR_STATE_CANCELLED;
    br.current_state = BR_STATE_IDLE;
    return 0;
}

int br_backup_delete(int backup_id)
{
    if (backup_id < 0 || backup_id >= BR_MAX_BACKUPS || !br.backups[backup_id].used) return -1;
    br.backups[backup_id].used = 0;
    br.backup_count--;
    return 0;
}

int br_backup_get(int backup_id, br_backup_t *backup)
{
    if (backup_id < 0 || backup_id >= BR_MAX_BACKUPS || !br.backups[backup_id].used || !backup) return -1;
    memcpy(backup, &br.backups[backup_id], sizeof(br_backup_t));
    return 0;
}

int br_backup_list(br_backup_t *backups, int max)
{
    if (!backups || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < BR_MAX_BACKUPS && count < max; i++) {
        if (br.backups[i].used) memcpy(&backups[count++], &br.backups[i], sizeof(br_backup_t));
    }
    return count;
}

int br_backup_get_progress(int backup_id, int *progress, br_state_t *state)
{
    if (backup_id < 0 || backup_id >= BR_MAX_BACKUPS || !br.backups[backup_id].used) return -1;
    if (progress) *progress = br.progress;
    if (state) *state = br.backups[backup_id].state;
    return 0;
}

int br_backup_verify(int backup_id)
{
    if (backup_id < 0 || backup_id >= BR_MAX_BACKUPS || !br.backups[backup_id].used) return -1;
    br.backups[backup_id].verified = 1;
    return 0;
}

int br_restore_start(int backup_id)
{
    if (backup_id < 0 || backup_id >= BR_MAX_BACKUPS || !br.backups[backup_id].used) return -1;
    br.current_state = BR_STATE_RESTORING;
    br.current_backup_id = backup_id;
    br.current_state = BR_STATE_COMPLETED;
    br.total_restores++;
    br.total_data_restored += br.backups[backup_id].size;
    return 0;
}

int br_restore_cancel(void)
{
    br.current_state = BR_STATE_IDLE;
    return 0;
}

int br_restore_get_progress(int *progress, br_state_t *state)
{
    if (progress) *progress = br.progress;
    if (state) *state = br.current_state;
    return 0;
}

int br_restore_select_items(int backup_id, int *item_ids, int count)
{
    (void)backup_id; (void)item_ids; (void)count;
    return 0;
}

int br_migration_start(br_location_t source, br_location_t target)
{
    memset(&br.migration, 0, sizeof(br_migration_t));
    br.migration.active = 1;
    br.migration.session_id = 1;
    br.migration.source = source;
    br.migration.target = target;
    br.migration.state = BR_STATE_PREPARING;
    return 0;
}

int br_migration_scan_source(void)
{
    br.migration.total_items = 50;
    br.migration.total_size = 1024UL * 1024 * 1024 * 8;  /* 8GB */
    br.migration.state = BR_STATE_IDLE;
    return br.migration.total_items;
}

int br_migration_select_items(int *item_ids, int count)
{
    (void)item_ids;
    br.migration.total_items = count;
    return 0;
}

int br_migration_begin(void)
{
    br.migration.state = BR_STATE_BACKING_UP;
    br.migration.progress = 50;
    br.migration.speed_kbps = 50000;
    br.migration.estimated_minutes = 5;
    return 0;
}

int br_migration_cancel(void)
{
    br.migration.active = 0;
    br.migration.state = BR_STATE_CANCELLED;
    return 0;
}

int br_migration_get_status(br_migration_t *status)
{
    if (!status) return -1;
    memcpy(status, &br.migration, sizeof(br_migration_t));
    return 0;
}

int br_migration_complete(void)
{
    br.migration.state = BR_STATE_COMPLETED;
    br.migration.progress = 100;
    br.migration.active = 0;
    return 0;
}

int br_cloud_upload(int backup_id)
{
    if (backup_id < 0 || backup_id >= BR_MAX_BACKUPS || !br.backups[backup_id].used) return -1;
    br.backups[backup_id].location = BR_LOC_CLOUD;
    br.backups[backup_id].state = BR_STATE_UPLOADING;
    br.backups[backup_id].state = BR_STATE_COMPLETED;
    return 0;
}

int br_cloud_download(int backup_id)
{
    if (backup_id < 0 || backup_id >= BR_MAX_BACKUPS || !br.backups[backup_id].used) return -1;
    return 0;
}

int br_cloud_list(br_backup_t *backups, int max)
{
    if (!backups || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < BR_MAX_BACKUPS && count < max; i++) {
        if (br.backups[i].used && br.backups[i].location == BR_LOC_CLOUD)
            memcpy(&backups[count++], &br.backups[i], sizeof(br_backup_t));
    }
    return count;
}

int br_cloud_delete(int backup_id)
{
    return br_backup_delete(backup_id);
}

int br_set_config(const br_config_t *config)
{
    if (!config) return -1;
    memcpy(&br.config, config, sizeof(br_config_t));
    return 0;
}

int br_get_config(br_config_t *config)
{
    if (!config) return -1;
    memcpy(config, &br.config, sizeof(br_config_t));
    return 0;
}

int br_set_auto_backup(int enable)
{
    br.config.auto_backup = enable ? 1 : 0;
    return 0;
}

int br_set_encryption(int enable)
{
    br.config.encrypt_backups = enable ? 1 : 0;
    return 0;
}

int br_get_stats(backup_restore_t *stats)
{
    if (!stats) return -1;
    memcpy(stats, &br, sizeof(backup_restore_t));
    return 0;
}
