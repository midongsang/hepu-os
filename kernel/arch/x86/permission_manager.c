/* ============================================================================
 * 赫普OS - 应用权限精细化管理
 * ========================================================================== */

#include <stdint.h>
#include "kstdio.h"
#include "permission_manager.h"
#include "ide_ext2.h"
#include "vga.h"
#include "serial.h"
#include "string.h"

permission_manager_t pm;

/* V2.74: 预置手机敏感数据目录 (幂等)——联系人/短信演示数据落盘 */
void prm_seed_data_dirs(void)
{
    extern int ide_ext2_mkdir(const char *);
    extern int ide_ext2_writefile(const char *, const char *);
    extern int ide_ext2_read_file(const char *, const char *, char *, int);
    extern int ide_ext2_sync(void);
    char probe[64];
    int need_sync = 0;
    if (ide_ext2_read_file("/data/contacts", "contacts.txt", probe, 63) <= 0) {
        ide_ext2_mkdir("data");
        ide_ext2_mkdir("data/contacts");
        ide_ext2_writefile("data/contacts/contacts.txt",
            "张三|13800001111\n李四|13900002222\n王五|13700003333\n");
        need_sync = 1;
        serial_puts(0x3F8, "[PERM] seeded /data/contacts/contacts.txt\n");
    }
    if (ide_ext2_read_file("/data/sms", "sms.txt", probe, 63) <= 0) {
        ide_ext2_mkdir("data");
        ide_ext2_mkdir("data/sms");
        ide_ext2_writefile("data/sms/sms.txt",
            "张三|晚上一起吃饭吗？\n李四|项目进度如何了？\n10086|您的流量已使用80%\n");
        need_sync = 1;
        serial_puts(0x3F8, "[PERM] seeded /data/sms/sms.txt\n");
    }
    /* V2.77: 通话记录数据目录 (幂等 seed) */
    if (ide_ext2_read_file("/data/phone", "calls.txt", probe, 63) <= 0) {
        ide_ext2_mkdir("data");
        ide_ext2_mkdir("data/phone");
        ide_ext2_writefile("data/phone/calls.txt", "");
        need_sync = 1;
        serial_puts(0x3F8, "[PERM] seeded /data/phone/calls.txt\n");
    }
    /* V2.79: 相机照片数据目录 (幂等 seed, 计数 0) */
    if (ide_ext2_read_file("/data/photos", "photos.txt", probe, 63) <= 0) {
        ide_ext2_mkdir("data");
        ide_ext2_mkdir("data/photos");
        ide_ext2_writefile("data/photos/photos.txt", "0\n");
        need_sync = 1;
        serial_puts(0x3F8, "[PERM] seeded /data/photos/photos.txt\n");
    }
    /* V2.84: 浏览器书签数据目录 (幂等 seed) */
    if (ide_ext2_read_file("/data/browser", "bookmarks.txt", probe, 63) <= 0) {
        ide_ext2_mkdir("data");
        ide_ext2_mkdir("data/browser");
        ide_ext2_writefile("data/browser/bookmarks.txt", "");
        need_sync = 1;
        serial_puts(0x3F8, "[PERM] seeded /data/browser/bookmarks.txt\n");
    }
    /* V2.94: 音乐收藏 (幂等 seed, 4 位 0) */
    if (ide_ext2_read_file("/data/music", "favorites.txt", probe, 63) <= 0) {
        ide_ext2_mkdir("data");
        ide_ext2_mkdir("data/music");
        ide_ext2_writefile("data/music/favorites.txt", "0000");
        need_sync = 1;
        serial_puts(0x3F8, "[PERM] seeded /data/music/favorites.txt\n");
    }
    /* V2.90: 多闹钟数据 (幂等 seed, 3 槽默认关闭) */
    if (ide_ext2_read_file("/data/clock", "alarms.txt", probe, 63) <= 0) {
        ide_ext2_mkdir("data");
        ide_ext2_mkdir("data/clock");
        ide_ext2_writefile("data/clock/alarms.txt",
                           "07:00|0\n08:30|0\n00:00|0\n");
        need_sync = 1;
        serial_puts(0x3F8, "[PERM] seeded /data/clock/alarms.txt\n");
    }
    if (need_sync) ide_ext2_sync();
    }

void permission_manager_init(void)
{
    memset(&pm, 0, sizeof(permission_manager_t));
    pm.initialized = 1;
    pm.enabled = 1;
    pm.config.onetime_permissions = 1;
    pm.config.fuzzy_location = 1;
    pm.config.empty_info_auth = 1;
    pm.config.permission_logging = 1;
    pm.config.auto_revoke_unused = 1;
    pm.config.auto_revoke_days = 30;
    pm.config.clipboard_warning = 1;
    pm.config.camera_mic_indicator = 1;
    pm.config.approximate_location_default = 1;
    pm.camera_in_use_by = -1;
    pm.mic_in_use_by = -1;
    pm.location_in_use_by = -1;

    vga_puts("[PERM] perm manager init done \n");
    vga_printf(" one-time: %s, fuzzy location: %s, empty info: %s\n",
               pm.config.onetime_permissions ? " on " : " close ",
               pm.config.fuzzy_location ? " on " : " close ",
               pm.config.empty_info_auth ? " on " : " close ");
    /* V2.69: 预注册手机应用——保证 perm.dat 的 app id 稳定对应 */
    prm_app_register("selftest", "com.hepu.selftest");
    prm_app_register("browser", "com.hepu.browser");
    prm_app_register("camera", "com.hepu.camera");
    prm_app_register("gallery", "com.hepu.gallery");
    prm_app_register("sms", "com.hepu.sms");
    prm_app_register("phone", "com.hepu.phone");
    prm_app_register("music", "com.hepu.music");

    /* V2.69: 从磁盘恢复持久化权限 (若存在 perm.dat) */
    if (prm_load_state("/perm.dat") == 0) {
        serial_puts(0x3F8, "[PERM] persisted perms restored from /perm.dat\n");
        /* V2.69: 恢复验证——browser STORAGE 应回到 ALLOWED */
        int bid = prm_app_register("browser", "com.hepu.browser");
        prm_grant_t bperm = PM_GRANT_DENIED;
        prm_get_permission_status(bid, PM_PERM_STORAGE, &bperm);
        if (bperm == PM_GRANT_ALLOWED) {
            serial_puts(0x3F8, "[PERM] browser STORAGE restored: ALLOWED\n");
        }
    }
    /* V2.74: 预置联系人/短信数据目录 (幂等) */
    prm_seed_data_dirs();
    /* V2.76: 首次启动欢迎短信 (幂等, 验证短信落盘) */
    /* V0.10.5-BIOS: 临时禁用，排查BIOS环境下的乱码循环 */
    /* extern void sms_seed_welcome(void);
    sms_seed_welcome(); */
    extern void serial_puts(uint16_t, const char *);
    serial_puts(0x3F8, "[PERM] sms_seed_welcome skipped (BIOS debug)\n");
    serial_puts(0x3F8, "[PERM] permission_manager_init returning...\n");
}

int prm_app_register(const char *name, const char *package)
{
    /* V2.68: 查重——同名或同包已注册则返回原 id (幂等) */
    for (int i = 0; i < PM_MAX_APPS; i++) {
        if (pm.apps[i].used &&
            ((name && pm.apps[i].app_name[0] &&
              strcmp(pm.apps[i].app_name, name) == 0) ||
             (package && pm.apps[i].package[0] &&
              strcmp(pm.apps[i].package, package) == 0))) {
            return i;
        }
    }
    for (int i = 0; i < PM_MAX_APPS; i++) {
        if (!pm.apps[i].used) {
            memset(&pm.apps[i], 0, sizeof(prm_app_perms_t));
            pm.apps[i].used = 1;
            pm.apps[i].app_id = i;
            if (name) strncpy(pm.apps[i].app_name, name, 127);
            if (package) strncpy(pm.apps[i].package, package, 127);
            pm.apps[i].fuzzy_location_radius = 1000.0f;  /* 默认1公里模糊 */
            pm.app_count++;
            return i;
        }
    }
    return -1;
}

int prm_app_unregister(int app_id)
{
    if (app_id < 0 || app_id >= PM_MAX_APPS || !pm.apps[app_id].used) return -1;
    pm.apps[app_id].used = 0;
    pm.app_count--;
    return 0;
}

int prm_app_get(int app_id, prm_app_perms_t *perms)
{
    if (app_id < 0 || app_id >= PM_MAX_APPS || !pm.apps[app_id].used || !perms) return -1;
    memcpy(perms, &pm.apps[app_id], sizeof(prm_app_perms_t));
    return 0;
}

int prm_app_list(prm_app_perms_t *apps, int max)
{
    if (!apps || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < PM_MAX_APPS && count < max; i++) {
        if (pm.apps[i].used) memcpy(&apps[count++], &pm.apps[i], sizeof(prm_app_perms_t));
    }
    return count;
}

/* V2.70: 轻量应用列表——只拷精简字段, 防止 UI 栈上分配大结构溢出 */
int prm_app_list_brief(prm_app_brief_t *apps, int max)
{
    if (!apps || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < PM_MAX_APPS && count < max; i++) {
        if (pm.apps[i].used) {
            apps[count].used = 1;
            apps[count].app_id = pm.apps[i].app_id;
            strncpy(apps[count].app_name, pm.apps[i].app_name, PM_NAME_LEN - 1);
            apps[count].app_name[PM_NAME_LEN - 1] = 0;
            strncpy(apps[count].package, pm.apps[i].package, PM_NAME_LEN - 1);
            apps[count].package[PM_NAME_LEN - 1] = 0;
            int cnt = 0;
            for (int p = 0; p < PM_MAX_PERMISSIONS; p++)
                if (pm.apps[i].grants[p] != PM_GRANT_DENIED) cnt++;
            apps[count].grant_count = cnt;
            count++;
        }
    }
    return count;
}

int prm_request_permission(int app_id, prm_permission_t perm)
{
    if (app_id < 0 || app_id >= PM_MAX_APPS || !pm.apps[app_id].used) return -1;
    pm.total_permission_checks++;
    /* V2.66: 统一授权判定——ALLOWED/ONETIME/WHILE_USING/FUZZY/EMPTY 均视为已授权 */
    prm_grant_t g = pm.apps[app_id].grants[perm];
    if (g == PM_GRANT_ALLOWED || g == PM_GRANT_ONETIME ||
        g == PM_GRANT_WHILE_USING || g == PM_GRANT_FUZZY ||
        g == PM_GRANT_EMPTY) {
        /* V2.71: 权限使用日志——应用启动请求权限即记录 */
        pm.apps[app_id].usage_count[perm]++;
        pm.apps[app_id].last_used[perm] = 1;
        prm_log_access(app_id, perm, 0, NULL);
        /* V2.72: 权限使用指示器——应用请求即标记使用中 */
        if (perm == PM_PERM_CAMERA) pm.camera_in_use_by = app_id;
        if (perm == PM_PERM_MICROPHONE) pm.mic_in_use_by = app_id;
        if (perm == PM_PERM_LOCATION) pm.location_in_use_by = app_id;
        return 1;
    }
    /* 默认拒绝，需要用户授权 */
    return 0;
}

int prm_grant_permission(int app_id, prm_permission_t perm, prm_grant_t grant)
{
    if (app_id < 0 || app_id >= PM_MAX_APPS || !pm.apps[app_id].used) return -1;
    pm.apps[app_id].grants[perm] = grant;
    return 0;
}

int prm_revoke_permission(int app_id, prm_permission_t perm)
{
    if (app_id < 0 || app_id >= PM_MAX_APPS || !pm.apps[app_id].used) return -1;
    pm.apps[app_id].grants[perm] = PM_GRANT_DENIED;
    return 0;
}

int prm_check_permission(int app_id, prm_permission_t perm, int *granted)
{
    if (app_id < 0 || app_id >= PM_MAX_APPS || !pm.apps[app_id].used) return -1;
    pm.total_permission_checks++;
    prm_grant_t g = pm.apps[app_id].grants[perm];
    int allow = 0;
    if (g == PM_GRANT_ALLOWED || g == PM_GRANT_WHILE_USING ||
        g == PM_GRANT_ONETIME || g == PM_GRANT_FUZZY ||
        g == PM_GRANT_EMPTY) {
        /* 检查一次性权限是否过期 */
        if (g == PM_GRANT_ONETIME && pm.apps[app_id].onetime_expiry[perm] > 0) {
            /* 简化：假设未过期 */
            allow = 1;
        } else {
            allow = 1;
        }
    }
    if (!allow) pm.total_permission_denials++;
    if (granted) *granted = allow;
    /* 记录使用 */
    if (allow) {
        pm.apps[app_id].usage_count[perm]++;
        pm.apps[app_id].last_used[perm] = 1;
        prm_log_access(app_id, perm, 0, NULL);
        /* 更新使用指示器 */
        if (perm == PM_PERM_CAMERA) pm.camera_in_use_by = app_id;
        if (perm == PM_PERM_MICROPHONE) pm.mic_in_use_by = app_id;
        if (perm == PM_PERM_LOCATION) pm.location_in_use_by = app_id;
    }
    return 0;
}

int prm_get_permission_status(int app_id, prm_permission_t perm, prm_grant_t *grant)
{
    if (app_id < 0 || app_id >= PM_MAX_APPS || !pm.apps[app_id].used || !grant) return -1;
    *grant = pm.apps[app_id].grants[perm];
    return 0;
}

int prm_grant_onetime(int app_id, prm_permission_t perm, uint64_t duration_ms)
{
    if (app_id < 0 || app_id >= PM_MAX_APPS || !pm.apps[app_id].used) return -1;
    pm.apps[app_id].grants[perm] = PM_GRANT_ONETIME;
    pm.apps[app_id].onetime_expiry[perm] = duration_ms;
    pm.total_onetime_grants++;
    return 0;
}

int prm_onetime_expired(int app_id, prm_permission_t perm, int *expired)
{
    if (app_id < 0 || app_id >= PM_MAX_APPS || !pm.apps[app_id].used) return -1;
    if (pm.apps[app_id].grants[perm] != PM_GRANT_ONETIME) {
        if (expired) *expired = 0;
        return 0;
    }
    /* 简化：假设未过期 */
    if (expired) *expired = 0;
    return 0;
}

int prm_extend_onetime(int app_id, prm_permission_t perm, uint64_t duration_ms)
{
    if (app_id < 0 || app_id >= PM_MAX_APPS || !pm.apps[app_id].used) return -1;
    pm.apps[app_id].onetime_expiry[perm] += duration_ms;
    return 0;
}

int prm_set_fuzzy_location(int app_id, float radius_m)
{
    if (app_id < 0 || app_id >= PM_MAX_APPS || !pm.apps[app_id].used) return -1;
    pm.apps[app_id].fuzzy_location_radius = radius_m;
    pm.apps[app_id].grants[PM_PERM_LOCATION] = PM_GRANT_FUZZY;
    return 0;
}

int prm_get_fuzzy_location(int app_id, float *radius_m)
{
    if (app_id < 0 || app_id >= PM_MAX_APPS || !pm.apps[app_id].used || !radius_m) return -1;
    *radius_m = pm.apps[app_id].fuzzy_location_radius;
    return 0;
}

int prm_fuzzy_location_coords(float lat, float lon, float *fuzzy_lat, float *fuzzy_lon, float radius)
{
    if (!fuzzy_lat || !fuzzy_lon) return -1;
    /* 简化：在半径内随机偏移 */
    float offset = radius * 0.00001f;  /* 粗略转换 */
    *fuzzy_lat = lat + offset;
    *fuzzy_lon = lon + offset;
    return 0;
}

int prm_set_empty_auth(int app_id, prm_permission_t perm, int enable)
{
    if (app_id < 0 || app_id >= PM_MAX_APPS || !pm.apps[app_id].used) return -1;
    if (enable) pm.apps[app_id].grants[perm] = PM_GRANT_EMPTY;
    else pm.apps[app_id].grants[perm] = PM_GRANT_DENIED;
    return 0;
}

int prm_get_empty_data(prm_permission_t perm, void *buf, int size)
{
    if (!buf || size <= 0) return -1;
    memset(buf, 0, size);
    (void)perm;
    return 0;
}

int prm_log_access(int app_id, prm_permission_t perm, int duration_ms, const char *detail)
{
    if (!pm.config.permission_logging) return 0;
    int idx = pm.log_pos % PM_MAX_LOGS;
    memset(&pm.logs[idx], 0, sizeof(prm_log_entry_t));
    pm.logs[idx].used = 1;
    pm.logs[idx].id = idx;
    pm.logs[idx].app_id = app_id;
    pm.logs[idx].permission = perm;
    pm.logs[idx].timestamp = 1;
    pm.logs[idx].duration_ms = duration_ms;
    if (detail) strncpy(pm.logs[idx].detail, detail, 255);
    pm.log_pos++;
    if (pm.log_count < PM_MAX_LOGS) pm.log_count++;
    return 0;
}

int prm_get_logs(prm_log_entry_t *logs, int max)
{
    if (!logs || max <= 0) return 0;
    int count = pm.log_count < max ? pm.log_count : max;
    for (int i = 0; i < count; i++) {
        int idx = (pm.log_pos - count + i) % PM_MAX_LOGS;
        memcpy(&logs[i], &pm.logs[idx], sizeof(prm_log_entry_t));
    }
    return count;
}

int prm_get_app_logs(int app_id, prm_log_entry_t *logs, int max)
{
    if (!logs || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < PM_MAX_LOGS && count < max; i++) {
        if (pm.logs[i].used && pm.logs[i].app_id == app_id)
            memcpy(&logs[count++], &pm.logs[i], sizeof(prm_log_entry_t));
    }
    return count;
}

int prm_clear_logs(void)
{
    for (int i = 0; i < PM_MAX_LOGS; i++) pm.logs[i].used = 0;
    pm.log_count = 0;
    pm.log_pos = 0;
    return 0;
}

/* V2.71: 权限使用聚合统计——遍历全部日志, 按 (app, perm) 聚合计次 */
int prm_log_stats(prm_app_perm_stat_t *stats, int max)
{
    if (!stats || max <= 0) return 0;
    int n = 0;
    /* V2.71: 倒序聚合——最新使用 (app,perm) 排最前 */
    for (int i = PM_MAX_LOGS - 1; i >= 0 && n < max; i--) {
        if (!pm.logs[i].used) continue;
        int found = 0;
        for (int j = 0; j < n; j++) {
            if (stats[j].app_id == pm.logs[i].app_id &&
                stats[j].permission == (int)pm.logs[i].permission) {
                stats[j].count++;
                found = 1;
                break;
            }
        }
        if (!found) {
            stats[n].used = 1;
            stats[n].app_id = pm.logs[i].app_id;
            stats[n].permission = (int)pm.logs[i].permission;
            stats[n].count = 1;
            n++;
        }
    }
    return n;
}

int prm_group_get(prm_permission_t perm, prm_group_t *group)
{
    (void)perm;
    if (!group) return -1;
    memset(group, 0, sizeof(prm_group_t));
    return 0;
}

int prm_group_list(prm_group_t *groups, int max)
{
    (void)groups; (void)max;
    return 0;
}

int prm_auto_revoke_check(void)
{
    int revoked = 0;
    for (int i = 0; i < PM_MAX_APPS; i++) {
        if (pm.apps[i].used && !pm.apps[i].restricted) {
            /* 简化：检查长期未使用的权限 */
            for (int p = 0; p < PM_MAX_PERMISSIONS; p++) {
                if (pm.apps[i].grants[p] == PM_GRANT_ALLOWED &&
                    pm.apps[i].usage_count[p] == 0) {
                    pm.apps[i].grants[p] = PM_GRANT_DENIED;
                    revoked++;
                    pm.total_auto_revokes++;
                }
            }
        }
    }
    return revoked;
}

int prm_auto_revoke_app(int app_id)
{
    if (app_id < 0 || app_id >= PM_MAX_APPS || !pm.apps[app_id].used) return -1;
    for (int p = 0; p < PM_MAX_PERMISSIONS; p++)
        pm.apps[app_id].grants[p] = PM_GRANT_DENIED;
    return 0;
}


/* 扩展功能已拆分到 permission_manager_ext.c */
