/* ============================================================================
 * 赫普OS - microG 服务集成
 * ========================================================================== */

#include <stdint.h>
#include "microg_service.h"
#include "vga.h"
#include "string.h"

static microg_service_t mg;

static const char *service_names[] = {
    " location ", " push notify ", " 地 map service ", " 账号认证 ", " 云 msg ",
    " device 认证 ", " 广告 ID", " 健康 data ", " payment ", " 附近 device ", " 投屏 ", " 云 storage "
};

void microg_service_init(void)
{
    memset(&mg, 0, sizeof(microg_service_t));
    mg.initialized = 1;
    mg.enabled = 1;
    mg.location_mode = 3;  /* 高精度 */
    mg.push_enabled = 1;
    mg.maps_enabled = 1;
    mg.safety_net_enabled = 1;
    mg.ad_tracking_enabled = 0;

    /* 注册所有服务 */
    for (int i = 0; i < 12; i++) {
        mg.services[i].used = 1;
        mg.services[i].type = (mg_service_type_t)i;
        mg.services[i].state = MG_STATE_STOPPED;
        strncpy(mg.services[i].name, service_names[i], 63);
        mg.services[i].enabled = 1;
        mg.services[i].priority = 50;
        mg.service_count++;
    }

    /* 启动核心服务 */
    mg_service_start(MG_SERVICE_LOCATION);
    mg_service_start(MG_SERVICE_PUSH);
    mg_service_start(MG_SERVICE_AUTH);

    vga_puts("[MICROG] microG service init done \n");
    vga_printf(" service: %d, location mode: high precision, push: %s\n",
               mg.service_count, mg.push_enabled ? " on " : " close ");
}

/* 服务管理 */
int mg_service_start(mg_service_type_t type)
{
    for (int i = 0; i < MG_MAX_SERVICES; i++) {
        if (mg.services[i].used && mg.services[i].type == type) {
            if (!mg.services[i].enabled) return -1;
            mg.services[i].state = MG_STATE_RUNNING;
            mg.services[i].start_time = 1;
            return 0;
        }
    }
    return -1;
}

int mg_service_stop(mg_service_type_t type)
{
    for (int i = 0; i < MG_MAX_SERVICES; i++) {
        if (mg.services[i].used && mg.services[i].type == type) {
            mg.services[i].state = MG_STATE_STOPPED;
            return 0;
        }
    }
    return -1;
}

int mg_service_get(mg_service_type_t type, mg_service_t *svc)
{
    if (!svc) return -1;
    for (int i = 0; i < MG_MAX_SERVICES; i++) {
        if (mg.services[i].used && mg.services[i].type == type) {
            memcpy(svc, &mg.services[i], sizeof(mg_service_t));
            return 0;
        }
    }
    return -1;
}

int mg_service_list(mg_service_t *svcs, int max)
{
    if (!svcs || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < MG_MAX_SERVICES && count < max; i++) {
        if (mg.services[i].used) memcpy(&svcs[count++], &mg.services[i], sizeof(mg_service_t));
    }
    return count;
}

int mg_service_enable(mg_service_type_t type, int enable)
{
    for (int i = 0; i < MG_MAX_SERVICES; i++) {
        if (mg.services[i].used && mg.services[i].type == type) {
            mg.services[i].enabled = enable ? 1 : 0;
            if (!enable) mg.services[i].state = MG_STATE_STOPPED;
            return 0;
        }
    }
    return -1;
}

/* 位置服务 */
int mg_location_get_last(mg_location_t *loc)
{
    if (!loc) return -1;
    if (!mg.has_location) return -1;
    memcpy(loc, &mg.last_location, sizeof(mg_location_t));
    return 0;
}

int mg_location_request_updates(int min_time, float min_distance)
{
    (void)min_time; (void)min_distance;
    mg.total_location_requests++;
    /* 模拟位置更新 */
    mg.last_location.latitude = 39.9042;
    mg.last_location.longitude = 116.4074;
    mg.last_location.accuracy = 10.0f;
    mg.last_location.timestamp = 1;
    mg.last_location.provider = 3;
    mg.has_location = 1;
    return 0;
}

int mg_location_remove_updates(void)
{
    return 0;
}

int mg_location_set_mode(int mode)
{
    mg.location_mode = mode;
    return 0;
}

int mg_location_get_mode(int *mode)
{
    if (!mode) return -1;
    *mode = mg.location_mode;
    return 0;
}

/* 推送服务 */
int mg_push_send(const mg_push_message_t *msg)
{
    if (!msg) return -1;
    mg.total_push_messages++;
    for (int i = 0; i < MG_MAX_SERVICES; i++) {
        if (mg.services[i].used && mg.services[i].type == MG_SERVICE_PUSH)
            mg.services[i].total_requests++;
    }
    return 0;
}

int mg_push_receive(mg_push_message_t *msg)
{
    if (!msg) return -1;
    memset(msg, 0, sizeof(mg_push_message_t));
    return 0;
}

int mg_push_register(const char *sender_id, char *token, int max)
{
    (void)sender_id;
    if (!token || max <= 0) return -1;
    strncpy(token, "hepu_push_token_xxxxxxxx", max - 1);
    return 0;
}

int mg_push_unregister(const char *token)
{
    (void)token;
    return 0;
}

/* 账号服务 */
int mg_account_add(const char *email, const char *type, const char *password)
{
    (void)password;
    for (int i = 0; i < MG_MAX_ACCOUNTS; i++) {
        if (!mg.accounts[i].used) {
            memset(&mg.accounts[i], 0, sizeof(mg_account_t));
            mg.accounts[i].used = 1;
            mg.accounts[i].id = i;
            if (email) strncpy(mg.accounts[i].email, email, 127);
            if (type) strncpy(mg.accounts[i].type, type, 31);
            mg.account_count++;
            return i;
        }
    }
    return -1;
}

int mg_account_remove(int id)
{
    if (id < 0 || id >= MG_MAX_ACCOUNTS || !mg.accounts[id].used) return -1;
    mg.accounts[id].used = 0;
    mg.account_count--;
    return 0;
}

int mg_account_get(int id, mg_account_t *acc)
{
    if (id < 0 || id >= MG_MAX_ACCOUNTS || !mg.accounts[id].used || !acc) return -1;
    memcpy(acc, &mg.accounts[id], sizeof(mg_account_t));
    return 0;
}

int mg_account_list(mg_account_t *accounts, int max)
{
    if (!accounts || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < MG_MAX_ACCOUNTS && count < max; i++) {
        if (mg.accounts[i].used) memcpy(&accounts[count++], &mg.accounts[i], sizeof(mg_account_t));
    }
    return count;
}

int mg_account_signin(int id)
{
    if (id < 0 || id >= MG_MAX_ACCOUNTS || !mg.accounts[id].used) return -1;
    mg.accounts[id].authenticated = 1;
    mg.accounts[id].login_time = 1;
    return 0;
}

int mg_account_signout(int id)
{
    if (id < 0 || id >= MG_MAX_ACCOUNTS || !mg.accounts[id].used) return -1;
    mg.accounts[id].authenticated = 0;
    mg.accounts[id].token[0] = 0;
    return 0;
}

int mg_account_get_token(int id, char *token, int max)
{
    if (id < 0 || id >= MG_MAX_ACCOUNTS || !mg.accounts[id].used || !token || max <= 0) return -1;
    if (!mg.accounts[id].authenticated) return -1;
    strncpy(token, "hepu_oauth_token_xxxxxxxx", max - 1);
    return 0;
}

/* 设备认证 */
int mg_safetynet_attest(mg_attest_result_t *result)
{
    if (!result) return -1;
    *result = MG_ATTEST_BASIC;  /* 赫普OS通过基本认证 */
    return 0;
}

int mg_safetynet_verify(const char *nonce, char *result, int max)
{
    (void)nonce;
    if (!result || max <= 0) return -1;
    strncpy(result, "{\"attest\":\"basic\",\"basicIntegrity\":true}", max - 1);
    return 0;
}

/* 广告ID */
int mg_advertising_get_id(char *id, int max)
{
    if (!id || max <= 0) return -1;
    strncpy(id, "00000000-0000-0000-0000-000000000000", max - 1);
    return 0;
}

int mg_advertising_set_tracking(int enable)
{
    mg.ad_tracking_enabled = enable ? 1 : 0;
    return 0;
}

/* 配置 */
int microg_set_enabled(int enable)
{
    mg.enabled = enable ? 1 : 0;
    return 0;
}

int microg_get_enabled(int *enabled)
{
    if (!enabled) return -1;
    *enabled = mg.enabled;
    return 0;
}

int microg_get_stats(microg_service_t *stats)
{
    if (!stats) return -1;
    memcpy(stats, &mg, sizeof(microg_service_t));
    return 0;
}
