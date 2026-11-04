/* ============================================================================
 * 赫普OS - 设备标识随机化
 * ========================================================================== */

#include <stdint.h>
#include "id_randomizer.h"
#include "vga.h"
#include "string.h"

static id_randomizer_t ir;

/* 简单伪随机数生成器（xorshift32） */
static uint32_t ir_rand_state = 0x12345678;

static uint32_t ir_rand(void)
{
    ir_rand_state ^= ir_rand_state << 13;
    ir_rand_state ^= ir_rand_state >> 17;
    ir_rand_state ^= ir_rand_state << 5;
    return ir_rand_state;
}

static void ir_random_hex(char *buf, int len)
{
    const char *hex = "0123456789abcdef";
    for (int i = 0; i < len - 1; i++)
        buf[i] = hex[ir_rand() & 0xF];
    buf[len - 1] = 0;
}

static void ir_random_mac(uint8_t *mac)
{
    for (int i = 0; i < IR_MAC_LEN; i++)
        mac[i] = (uint8_t)(ir_rand() & 0xFF);
    /* 设置本地管理位（bit 1 of first octet），清除组播位 */
    mac[0] = (mac[0] & 0xFE) | 0x02;
}

void id_randomizer_init(void)
{
    memset(&ir, 0, sizeof(id_randomizer_t));
    ir.initialized = 1;
    ir.enabled = 1;
    ir.config.enabled = 1;
    ir.config.default_policy = IR_POLICY_PER_APP;
    ir.config.randomize_device_id = 1;
    ir.config.randomize_android_id = 1;
    ir.config.randomize_advertising_id = 1;
    ir.config.randomize_serial = 1;
    ir.config.protect_imei = 1;
    ir.config.randomize_wifi_mac = 1;
    ir.config.randomize_bt_mac = 1;
    ir.config.mac_randomization_interval_s = 3600;
    ir.config.ad_id_reset_on_demand = 1;
    ir.config.system_apps_get_real = 1;

    /* 生成全局随机标识 */
    ir_random_hex(ir.global_device_id, 16);
    ir_random_hex(ir.global_android_id, 16);
    ir_random_hex(ir.global_advertising_id, 36);  /* UUID格式 */
    ir_random_hex(ir.global_serial, 12);
    ir_random_mac(ir.global_wifi_mac);
    ir_random_mac(ir.global_bt_mac);

    vga_puts("[IDRAND] device ID randomizer init done \n");
    vga_printf(" policy: per-app, MAC random: %s, IMEI protected: %s\n",
               ir.config.randomize_wifi_mac ? " on " : " close ",
               ir.config.protect_imei ? " on " : " close ");
}

int id_randomizer_get(ir_id_type_t type, const char *package, char *buf, int size)
{
    if (!buf || size <= 0) return -1;
    ir.total_id_requests++;

    /* 系统应用获取真实值（简化：包名以hepu.开头） */
    if (ir.config.system_apps_get_real && package &&
        strncmp(package, "hepu.", 5) == 0) {
        /* 返回全局随机值作为" real value "（裸机无真实硬件ID） */
        switch (type) {
            case IR_ID_DEVICE: strncpy(buf, ir.global_device_id, size - 1); break;
            case IR_ID_ANDROID: strncpy(buf, ir.global_android_id, size - 1); break;
            case IR_ID_ADVERTISING: strncpy(buf, ir.global_advertising_id, size - 1); break;
            case IR_ID_SERIAL: strncpy(buf, ir.global_serial, size - 1); break;
            default: strncpy(buf, "0000000000000000", size - 1);
        }
        buf[size - 1] = 0;
        return 0;
    }

    /* 每应用策略：查找或创建应用标识 */
    ir_app_id_t *app = NULL;
    if (package) {
        for (int i = 0; i < IR_MAX_APP_IDS; i++) {
            if (ir.app_ids[i].used && strcmp(ir.app_ids[i].package, package) == 0) {
                app = &ir.app_ids[i];
                break;
            }
        }
        if (!app) {
            for (int i = 0; i < IR_MAX_APP_IDS; i++) {
                if (!ir.app_ids[i].used) {
                    memset(&ir.app_ids[i], 0, sizeof(ir_app_id_t));
                    ir.app_ids[i].used = 1;
                    strncpy(ir.app_ids[i].package, package, IR_PACKAGE_LEN - 1);
                    ir_random_hex(ir.app_ids[i].device_id, 16);
                    ir_random_hex(ir.app_ids[i].android_id, 16);
                    ir_random_hex(ir.app_ids[i].advertising_id, 36);
                    ir_random_hex(ir.app_ids[i].serial, 12);
                    ir_random_mac(ir.app_ids[i].wifi_mac);
                    ir_random_mac(ir.app_ids[i].bt_mac);
                    ir.app_ids[i].create_time = 1;
                    ir.app_id_count++;
                    app = &ir.app_ids[i];
                    break;
                }
            }
        }
        if (app) {
            app->last_access = 1;
            app->access_count++;
        }
    }

    const char *value = NULL;
    if (app) {
        switch (type) {
            case IR_ID_DEVICE: value = app->device_id; break;
            case IR_ID_ANDROID: value = app->android_id; break;
            case IR_ID_ADVERTISING: value = app->advertising_id; break;
            case IR_ID_SERIAL: value = app->serial; break;
            case IR_ID_IMEI:
            case IR_ID_MEID:
                value = "000000000000000";  /* 受保护，返回空IMEI */
                ir.total_tracking_blocked++;
                break;
            default: value = ir.global_device_id;
        }
    } else {
        switch (type) {
            case IR_ID_DEVICE: value = ir.global_device_id; break;
            case IR_ID_ANDROID: value = ir.global_android_id; break;
            case IR_ID_ADVERTISING: value = ir.global_advertising_id; break;
            case IR_ID_SERIAL: value = ir.global_serial; break;
            default: value = "0000000000000000";
        }
    }

    strncpy(buf, value, size - 1);
    buf[size - 1] = 0;
    return 0;
}

int id_randomizer_get_mac(ir_id_type_t type, const char *package, uint8_t *mac, int len)
{
    if (!mac || len < IR_MAC_LEN) return -1;
    ir_app_id_t *app = NULL;
    if (package) {
        for (int i = 0; i < IR_MAX_APP_IDS; i++) {
            if (ir.app_ids[i].used && strcmp(ir.app_ids[i].package, package) == 0) {
                app = &ir.app_ids[i];
                break;
            }
        }
    }
    if (app) {
        if (type == IR_ID_MAC_WIFI) memcpy(mac, app->wifi_mac, IR_MAC_LEN);
        else if (type == IR_ID_MAC_BT) memcpy(mac, app->bt_mac, IR_MAC_LEN);
        else memcpy(mac, app->wifi_mac, IR_MAC_LEN);
    } else {
        if (type == IR_ID_MAC_BT) memcpy(mac, ir.global_bt_mac, IR_MAC_LEN);
        else memcpy(mac, ir.global_wifi_mac, IR_MAC_LEN);
    }
    return 0;
}

int id_randomizer_reset(ir_id_type_t type)
{
    switch (type) {
        case IR_ID_DEVICE: ir_random_hex(ir.global_device_id, 16); break;
        case IR_ID_ANDROID: ir_random_hex(ir.global_android_id, 16); break;
        case IR_ID_ADVERTISING: ir_random_hex(ir.global_advertising_id, 36); break;
        case IR_ID_SERIAL: ir_random_hex(ir.global_serial, 12); break;
        case IR_ID_MAC_WIFI: ir_random_mac(ir.global_wifi_mac); break;
        case IR_ID_MAC_BT: ir_random_mac(ir.global_bt_mac); break;
        default: return -1;
    }
    ir.total_randomizations++;
    return 0;
}

int id_randomizer_reset_all(void)
{
    ir_random_hex(ir.global_device_id, 16);
    ir_random_hex(ir.global_android_id, 16);
    ir_random_hex(ir.global_advertising_id, 36);
    ir_random_hex(ir.global_serial, 12);
    ir_random_mac(ir.global_wifi_mac);
    ir_random_mac(ir.global_bt_mac);
    /* 重置所有应用标识 */
    for (int i = 0; i < IR_MAX_APP_IDS; i++) {
        if (ir.app_ids[i].used) {
            ir_random_hex(ir.app_ids[i].device_id, 16);
            ir_random_hex(ir.app_ids[i].android_id, 16);
            ir_random_hex(ir.app_ids[i].advertising_id, 36);
            ir_random_hex(ir.app_ids[i].serial, 12);
            ir_random_mac(ir.app_ids[i].wifi_mac);
            ir_random_mac(ir.app_ids[i].bt_mac);
        }
    }
    ir.total_randomizations++;
    return 0;
}

int id_randomizer_reset_advertising_id(void)
{
    ir_random_hex(ir.global_advertising_id, 36);
    for (int i = 0; i < IR_MAX_APP_IDS; i++) {
        if (ir.app_ids[i].used)
            ir_random_hex(ir.app_ids[i].advertising_id, 36);
    }
    ir.total_randomizations++;
    return 0;
}

int id_randomizer_reset_for_app(const char *package)
{
    if (!package) return -1;
    for (int i = 0; i < IR_MAX_APP_IDS; i++) {
        if (ir.app_ids[i].used && strcmp(ir.app_ids[i].package, package) == 0) {
            ir_random_hex(ir.app_ids[i].device_id, 16);
            ir_random_hex(ir.app_ids[i].android_id, 16);
            ir_random_hex(ir.app_ids[i].advertising_id, 36);
            ir_random_hex(ir.app_ids[i].serial, 12);
            ir_random_mac(ir.app_ids[i].wifi_mac);
            ir_random_mac(ir.app_ids[i].bt_mac);
            return 0;
        }
    }
    return -1;
}

int id_randomizer_register_app(const char *package)
{
    if (!package) return -1;
    for (int i = 0; i < IR_MAX_APP_IDS; i++) {
        if (ir.app_ids[i].used && strcmp(ir.app_ids[i].package, package) == 0)
            return 0;  /* 已存在 */
        if (!ir.app_ids[i].used) {
            memset(&ir.app_ids[i], 0, sizeof(ir_app_id_t));
            ir.app_ids[i].used = 1;
            strncpy(ir.app_ids[i].package, package, IR_PACKAGE_LEN - 1);
            ir_random_hex(ir.app_ids[i].device_id, 16);
            ir_random_hex(ir.app_ids[i].android_id, 16);
            ir_random_hex(ir.app_ids[i].advertising_id, 36);
            ir_random_hex(ir.app_ids[i].serial, 12);
            ir_random_mac(ir.app_ids[i].wifi_mac);
            ir_random_mac(ir.app_ids[i].bt_mac);
            ir.app_id_count++;
            return 0;
        }
    }
    return -1;
}

int id_randomizer_unregister_app(const char *package)
{
    if (!package) return -1;
    for (int i = 0; i < IR_MAX_APP_IDS; i++) {
        if (ir.app_ids[i].used && strcmp(ir.app_ids[i].package, package) == 0) {
            ir.app_ids[i].used = 0;
            ir.app_id_count--;
            return 0;
        }
    }
    return -1;
}

int id_randomizer_get_app_ids(const char *package, ir_app_id_t *ids)
{
    if (!package || !ids) return -1;
    for (int i = 0; i < IR_MAX_APP_IDS; i++) {
        if (ir.app_ids[i].used && strcmp(ir.app_ids[i].package, package) == 0) {
            memcpy(ids, &ir.app_ids[i], sizeof(ir_app_id_t));
            return 0;
        }
    }
    return -1;
}

int id_randomizer_set_policy(ir_policy_t policy)
{
    ir.config.default_policy = policy;
    return 0;
}

int id_randomizer_get_policy(ir_policy_t *policy)
{
    if (!policy) return -1;
    *policy = ir.config.default_policy;
    return 0;
}

int id_randomizer_set_config(const ir_config_t *config)
{
    if (!config) return -1;
    memcpy(&ir.config, config, sizeof(ir_config_t));
    return 0;
}

int id_randomizer_get_config(ir_config_t *config)
{
    if (!config) return -1;
    memcpy(config, &ir.config, sizeof(ir_config_t));
    return 0;
}

int id_randomizer_set_enabled(int enable)
{
    ir.enabled = enable ? 1 : 0;
    return 0;
}

int id_randomizer_randomize_mac(ir_id_type_t type)
{
    if (type == IR_ID_MAC_WIFI) { ir_random_mac(ir.global_wifi_mac); return 0; }
    if (type == IR_ID_MAC_BT) { ir_random_mac(ir.global_bt_mac); return 0; }
    return -1;
}

int id_randomizer_get_current_mac(ir_id_type_t type, uint8_t *mac)
{
    if (!mac) return -1;
    if (type == IR_ID_MAC_BT) memcpy(mac, ir.global_bt_mac, IR_MAC_LEN);
    else memcpy(mac, ir.global_wifi_mac, IR_MAC_LEN);
    return 0;
}

int id_randomizer_get_history(ir_history_t *history, int max)
{
    if (!history || max <= 0) return 0;
    int count = ir.history_count < max ? ir.history_count : max;
    for (int i = 0; i < count; i++)
        memcpy(&history[i], &ir.history[i], sizeof(ir_history_t));
    return count;
}

int id_randomizer_get_stats(id_randomizer_t *stats)
{
    if (!stats) return -1;
    memcpy(stats, &ir, sizeof(id_randomizer_t));
    return 0;
}

int id_randomizer_is_tracking_blocked(const char *package, int *blocked)
{
    (void)package;
    if (!blocked) return -1;
    *blocked = ir.enabled ? 1 : 0;
    return 0;
}
