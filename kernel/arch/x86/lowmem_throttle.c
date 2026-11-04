/* ============================================================================
 * 赫普OS - 旧设备低内存节流模式
 * ========================================================================== */

#include <stdint.h>
#include "lowmem_throttle.h"
#include "vga.h"
#include "string.h"

static lowmem_throttle_t lt;

static void lt_apply_default_strategy(lt_level_t level, lt_strategy_t *s)
{
    memset(s, 0, sizeof(lt_strategy_t));
    switch (level) {
        case LT_LEVEL_OFF:
            s->max_windows = 16; s->max_background_apps = 16;
            s->animation_quality = 3; s->render_resolution = 100;
            s->ai_enabled = 1; s->ai_level = 2;
            s->background_freeze = 0; s->cache_limit_mb = 512;
            s->compositor_effects = 2; s->wallpaper_quality = 3;
            s->auto_clean_interval_s = 3600; s->oom_killer_threshold = 95;
            break;
        case LT_LEVEL_LIGHT:
            s->max_windows = 12; s->max_background_apps = 12;
            s->animation_quality = 2; s->render_resolution = 90;
            s->ai_enabled = 1; s->ai_level = 1;
            s->background_freeze = 1; s->cache_limit_mb = 256;
            s->compositor_effects = 1; s->wallpaper_quality = 2;
            s->auto_clean_interval_s = 1800; s->oom_killer_threshold = 90;
            break;
        case LT_LEVEL_MODERATE:
            s->max_windows = 8; s->max_background_apps = 8;
            s->animation_quality = 1; s->render_resolution = 80;
            s->ai_enabled = 0; s->ai_level = 0;
            s->background_freeze = 1; s->cache_limit_mb = 128;
            s->compositor_effects = 1; s->wallpaper_quality = 1;
            s->auto_clean_interval_s = 600; s->oom_killer_threshold = 85;
            break;
        case LT_LEVEL_AGGRESSIVE:
            s->max_windows = 4; s->max_background_apps = 4;
            s->animation_quality = 0; s->render_resolution = 70;
            s->ai_enabled = 0; s->ai_level = 0;
            s->background_freeze = 1; s->cache_limit_mb = 64;
            s->compositor_effects = 0; s->wallpaper_quality = 0;
            s->auto_clean_interval_s = 300; s->oom_killer_threshold = 80;
            break;
        case LT_LEVEL_EXTREME:
            s->max_windows = 2; s->max_background_apps = 2;
            s->animation_quality = 0; s->render_resolution = 50;
            s->ai_enabled = 0; s->ai_level = 0;
            s->background_freeze = 1; s->cache_limit_mb = 32;
            s->compositor_effects = 0; s->wallpaper_quality = 0;
            s->auto_clean_interval_s = 120; s->oom_killer_threshold = 75;
            break;
    }
}

void lowmem_throttle_init(void)
{
    memset(&lt, 0, sizeof(lowmem_throttle_t));
    lt.initialized = 1;
    lt.enabled = 1;
    lt.current_level = LT_LEVEL_OFF;
    lt.mem_level = LT_MEM_UNKNOWN;
    lt.config.enabled = 1;
    lt.config.auto_detect = 1;
    lt.config.show_notification = 1;
    lt.config.allow_override = 1;
    lt.config.aggressive_clean = 0;
    lt.total_memory_kb = 2 * 1024 * 1024;  /* 默认2GB */
    lt.free_memory_kb = 512 * 1024;
    lt.used_memory_kb = lt.total_memory_kb - lt.free_memory_kb;

    /* 系统白名单 */
    const char *sys_apps[] = {"hepu_desktop", "systemui", "phone", "sms",
        "settings", "input_method", "launcher"};
    for (int i = 0; i < 7 && i < LT_MAX_WHITELIST; i++) {
        lt.whitelist[i].used = 1;
        strncpy(lt.whitelist[i].name, sys_apps[i], LT_NAME_LEN - 1);
        lt.whitelist[i].reason = 1;
        lt.whitelist_count++;
    }

    /* 应用默认策略 */
    lt_apply_default_strategy(LT_LEVEL_OFF, &lt.strategy);

    vga_puts("[LOWMEM] lowmem throttle mode init done \n");
    vga_printf(" auto detect: %s, whitelist: %d, default level: close \n",
               lt.config.auto_detect ? " on " : " close ", lt.whitelist_count);
}

int lowmem_throttle_detect_memory(void)
{
    /* 根据总内存判断等级 */
    if (lt.total_memory_kb <= 512 * 1024) lt.mem_level = LT_MEM_512MB;
    else if (lt.total_memory_kb <= 1024 * 1024) lt.mem_level = LT_MEM_1GB;
    else if (lt.total_memory_kb <= 2 * 1024 * 1024) lt.mem_level = LT_MEM_2GB;
    else if (lt.total_memory_kb <= 3 * 1024 * 1024) lt.mem_level = LT_MEM_3GB;
    else if (lt.total_memory_kb <= 4 * 1024 * 1024) lt.mem_level = LT_MEM_4GB;
    else if (lt.total_memory_kb <= 6 * 1024 * 1024) lt.mem_level = LT_MEM_6GB;
    else if (lt.total_memory_kb <= 8 * 1024 * 1024) lt.mem_level = LT_MEM_8GB;
    else if (lt.total_memory_kb <= 12 * 1024 * 1024) lt.mem_level = LT_MEM_12GB;
    else lt.mem_level = LT_MEM_16GB_PLUS;

    /* 自动设置节流等级 */
    if (lt.config.auto_detect) {
        lt_level_t new_level;
        if (lt.mem_level <= LT_MEM_512MB) new_level = LT_LEVEL_EXTREME;
        else if (lt.mem_level <= LT_MEM_1GB) new_level = LT_LEVEL_AGGRESSIVE;
        else if (lt.mem_level <= LT_MEM_2GB) new_level = LT_LEVEL_MODERATE;
        else if (lt.mem_level <= LT_MEM_4GB) new_level = LT_LEVEL_LIGHT;
        else new_level = LT_LEVEL_OFF;
        lowmem_throttle_set_level(new_level);
    }
    return (int)lt.mem_level;
}

int lowmem_throttle_set_level(lt_level_t level)
{
    if (level == lt.current_level) return 0;
    lt_level_t old = lt.current_level;
    lt.current_level = level;
    lt_apply_default_strategy(level, &lt.strategy);

    /* 记录事件 */
    for (int i = 0; i < 64; i++) {
        if (!lt.records[i].used) {
            memset(&lt.records[i], 0, sizeof(lt_record_t));
            lt.records[i].used = 1;
            lt.records[i].id = i;
            lt.records[i].event = (level > old) ? LT_EVENT_ENTER_THROTTLE : LT_EVENT_EXIT_THROTTLE;
            lt.records[i].timestamp = 1;
            lt.record_count++;
            break;
        }
    }
    return 0;
}

int lowmem_throttle_get_level(lt_level_t *level)
{
    if (!level) return -1;
    *level = lt.current_level;
    return 0;
}

int lowmem_throttle_auto_adjust(void)
{
    if (!lt.config.auto_detect) return -1;
    int usage = 0;
    lowmem_throttle_get_usage_percent(&usage);
    /* 内存使用超过阈值时提升等级 */
    if (usage > 90 && lt.current_level < LT_LEVEL_EXTREME)
        lowmem_throttle_set_level(lt.current_level + 1);
    else if (usage < 50 && lt.current_level > LT_LEVEL_OFF)
        lowmem_throttle_set_level(lt.current_level - 1);
    return (int)lt.current_level;
}

int lowmem_throttle_apply_strategy(const lt_strategy_t *strategy)
{
    if (!strategy) return -1;
    memcpy(&lt.strategy, strategy, sizeof(lt_strategy_t));
    return 0;
}

int lowmem_throttle_get_strategy(lt_strategy_t *strategy)
{
    if (!strategy) return -1;
    memcpy(strategy, &lt.strategy, sizeof(lt_strategy_t));
    return 0;
}

int lowmem_throttle_auto_clean(void)
{
    uint64_t freed = lt.strategy.cache_limit_mb * 1024 / 2;  /* 估算释放一半缓存 */
    lt.memory_saved_kb += freed;
    lt.total_cleanups++;
    lt.free_memory_kb += freed;
    if (lt.used_memory_kb > freed) lt.used_memory_kb -= freed;
    return (int)(freed / 1024);  /* 返回MB */
}

int lowmem_throttle_kill_background(int count)
{
    int killed = 0;
    /* 简化：模拟杀死后台应用 */
    for (int i = 0; i < count && i < 10; i++) {
        lt.total_apps_killed++;
        killed++;
    }
    return killed;
}

int lowmem_throttle_update_memory(uint64_t total_kb, uint64_t used_kb, uint64_t free_kb)
{
    lt.total_memory_kb = total_kb;
    lt.used_memory_kb = used_kb;
    lt.free_memory_kb = free_kb;
    return 0;
}

int lowmem_throttle_get_memory(uint64_t *total_kb, uint64_t *used_kb, uint64_t *free_kb)
{
    if (total_kb) *total_kb = lt.total_memory_kb;
    if (used_kb) *used_kb = lt.used_memory_kb;
    if (free_kb) *free_kb = lt.free_memory_kb;
    return 0;
}

int lowmem_throttle_get_usage_percent(int *percent)
{
    if (!percent) return -1;
    if (lt.total_memory_kb > 0) {
        uint32_t used32 = (uint32_t)(lt.used_memory_kb / 1024);
        uint32_t total32 = (uint32_t)(lt.total_memory_kb / 1024);
        if (total32 > 0) *percent = (int)((used32 * 100u) / total32);
        else *percent = 0;
    } else *percent = 0;
    return 0;
}

int lowmem_throttle_check_oom(int *triggered)
{
    if (!triggered) return -1;
    int usage = 0;
    lowmem_throttle_get_usage_percent(&usage);
    *triggered = (usage >= lt.strategy.oom_killer_threshold) ? 1 : 0;
    if (*triggered) {
        lt.oom_events++;
        lowmem_throttle_auto_clean();
    }
    return 0;
}

int lowmem_throttle_whitelist_add(const char *name, int reason)
{
    if (!name) return -1;
    for (int i = 0; i < LT_MAX_WHITELIST; i++) {
        if (!lt.whitelist[i].used) {
            memset(&lt.whitelist[i], 0, sizeof(lt_whitelist_t));
            lt.whitelist[i].used = 1;
            strncpy(lt.whitelist[i].name, name, LT_NAME_LEN - 1);
            lt.whitelist[i].reason = reason;
            lt.whitelist_count++;
            return 0;
        }
    }
    return -1;
}

int lowmem_throttle_whitelist_remove(const char *name)
{
    if (!name) return -1;
    for (int i = 0; i < LT_MAX_WHITELIST; i++) {
        if (lt.whitelist[i].used && strcmp(lt.whitelist[i].name, name) == 0) {
            lt.whitelist[i].used = 0;
            lt.whitelist_count--;
            return 0;
        }
    }
    return -1;
}

int lowmem_throttle_whitelist_check(const char *name, int *whitelisted)
{
    if (!name || !whitelisted) return -1;
    *whitelisted = 0;
    for (int i = 0; i < LT_MAX_WHITELIST; i++) {
        if (lt.whitelist[i].used && strcmp(lt.whitelist[i].name, name) == 0) {
            *whitelisted = 1;
            return 0;
        }
    }
    return 0;
}

int lowmem_throttle_whitelist_list(lt_whitelist_t *list, int max)
{
    if (!list || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < LT_MAX_WHITELIST && count < max; i++) {
        if (lt.whitelist[i].used) memcpy(&list[count++], &lt.whitelist[i], sizeof(lt_whitelist_t));
    }
    return count;
}

int lowmem_throttle_set_config(const lt_config_t *config)
{
    if (!config) return -1;
    memcpy(&lt.config, config, sizeof(lt_config_t));
    return 0;
}

int lowmem_throttle_get_config(lt_config_t *config)
{
    if (!config) return -1;
    memcpy(config, &lt.config, sizeof(lt_config_t));
    return 0;
}

int lowmem_throttle_set_enabled(int enable)
{
    lt.enabled = enable ? 1 : 0;
    if (!enable) lowmem_throttle_set_level(LT_LEVEL_OFF);
    return 0;
}

int lowmem_throttle_get_records(lt_record_t *records, int max)
{
    if (!records || max <= 0) return 0;
    int count = 0;
    for (int i = 63; i >= 0 && count < max; i--) {
        if (lt.records[i].used) memcpy(&records[count++], &lt.records[i], sizeof(lt_record_t));
    }
    return count;
}

int lowmem_throttle_get_stats(lowmem_throttle_t *stats)
{
    if (!stats) return -1;
    memcpy(stats, &lt, sizeof(lowmem_throttle_t));
    return 0;
}

int lowmem_throttle_get_default_strategy(lt_level_t level, lt_strategy_t *strategy)
{
    if (!strategy) return -1;
    lt_apply_default_strategy(level, strategy);
    return 0;
}
