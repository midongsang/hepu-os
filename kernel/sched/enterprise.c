/* ============================================================================
 * 赫普OS - 企业功能
 * MDM设备管理 + VPN + 工作资料隔离 + 证书策略 + 合规检查 + 远程管理
 * ========================================================================== */

#include <stdint.h>
#include "enterprise.h"
#include "vga.h"
#include "string.h"

static enterprise_t ent;

void enterprise_init(void)
{
    memset(&ent, 0, sizeof(enterprise_t));
    ent.initialized = 1;
    ent.mdm_state = MDM_STATE_UNMANAGED;
    ent.active_vpn = -1;
    ent.checkin_interval = 3600000;  /* 1小时 */

    /* 默认策略 */
    policy_set(POLICY_PASSWORD, 1, 6, "min_length=6");
    policy_set(POLICY_ENCRYPTION, 1, 1, "full_disk");
    policy_set(POLICY_CAMERA, 1, 1, "allowed");
    policy_set(POLICY_USB, 1, 1, "mtp_only");

    /* 工作资料默认关闭 */
    ent.work_profile.enabled = 0;
    ent.work_profile.isolation_level = 1;

    vga_puts("[ENT] enterprise features init done \n");
    vga_printf(" policy: %d, MDM state: %d\n", ent.policy_count, ent.mdm_state);
}

int mdm_enroll(const char *server, const char *user, const char *org)
{
    if (!server || !ent.initialized) return -1;
    ent.mdm_state = MDM_STATE_ENROLLING;
    strncpy(ent.mdm_server, server, 255);
    if (user) strncpy(ent.enrollment_user, user, 63);
    if (org) strncpy(ent.organization, org, 127);
    /* 生成设备ID */
    strncpy(ent.device_id, "HEPU-DEVICE-001", 63);
    ent.mdm_state = MDM_STATE_MANAGED;
    ent.compliance.compliant = 1;
    vga_printf("[ENT] MDM registered ok: %s\n", server);
    return 0;
}

int mdm_unenroll(void)
{
    ent.mdm_state = MDM_STATE_UNMANAGED;
    ent.mdm_server[0] = '\0';
    ent.enrollment_user[0] = '\0';
    ent.organization[0] = '\0';
    ent.device_id[0] = '\0';
    vga_puts("[ENT] MDM unregister \n");
    return 0;
}

int mdm_get_state(mdm_state_t *state)
{
    if (!state) return -1;
    *state = ent.mdm_state;
    return 0;
}

int mdm_checkin(void)
{
    if (ent.mdm_state != MDM_STATE_MANAGED) return -1;
    ent.last_checkin = 0;  /* 当前时间 */
    compliance_check();
    return 0;
}

int mdm_set_server(const char *server)
{
    if (!server) return -1;
    strncpy(ent.mdm_server, server, 255);
    return 0;
}

int mdm_get_device_id(char *id, int len)
{
    if (!id || len <= 0) return -1;
    strncpy(id, ent.device_id, len - 1);
    id[len - 1] = '\0';
    return 0;
}

int policy_set(policy_type_t type, int enabled, int value, const char *value_str)
{
    if (!ent.initialized) return -1;
    /* 查找已有策略 */
    for (int i = 0; i < ENT_MAX_POLICIES; i++) {
        if (ent.policies[i].used && ent.policies[i].type == type) {
            ent.policies[i].enabled = enabled;
            ent.policies[i].value_int = value;
            if (value_str) strncpy(ent.policies[i].value_str, value_str, 255);
            return i;
        }
    }
    /* 创建新策略 */
    for (int i = 0; i < ENT_MAX_POLICIES; i++) {
        if (!ent.policies[i].used) {
            memset(&ent.policies[i], 0, sizeof(policy_t));
            ent.policies[i].used = 1;
            ent.policies[i].id = i;
            ent.policies[i].type = type;
            ent.policies[i].enabled = enabled;
            ent.policies[i].value_int = value;
            if (value_str) strncpy(ent.policies[i].value_str, value_str, 255);
            { ent.policies[i].name[0] = 'p'; ent.policies[i].name[1] = 'o'; ent.policies[i].name[2] = 'l'; ent.policies[i].name[3] = 'i'; ent.policies[i].name[4] = 'c'; ent.policies[i].name[5] = 'y'; ent.policies[i].name[6] = '_'; ent.policies[i].name[7] = '0' + type; ent.policies[i].name[8] = 0; }
            ent.policy_count++;
            return i;
        }
    }
    return -1;
}

int policy_get(policy_type_t type, policy_t *policy)
{
    if (!policy) return -1;
    for (int i = 0; i < ENT_MAX_POLICIES; i++) {
        if (ent.policies[i].used && ent.policies[i].type == type) {
            memcpy(policy, &ent.policies[i], sizeof(policy_t));
            return i;
        }
    }
    return -1;
}

int policy_remove(policy_type_t type)
{
    for (int i = 0; i < ENT_MAX_POLICIES; i++) {
        if (ent.policies[i].used && ent.policies[i].type == type) {
            ent.policies[i].used = 0;
            ent.policy_count--;
            return 0;
        }
    }
    return -1;
}

int policy_list(policy_t *policies, int max)
{
    if (!policies || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < ENT_MAX_POLICIES && count < max; i++) {
        if (ent.policies[i].used) memcpy(&policies[count++], &ent.policies[i], sizeof(policy_t));
    }
    return count;
}

int policy_apply_all(void)
{
    vga_printf("[ENT] app %d policy \n", ent.policy_count);
    return 0;
}

int app_control_set(const char *package, int allowed, int required)
{
    if (!package) return -1;
    for (int i = 0; i < ENT_MAX_APPS; i++) {
        if (ent.work_profile.apps[i].package[0] &&
            strcmp(ent.work_profile.apps[i].package, package) == 0) {
            ent.work_profile.apps[i].allowed = allowed;
            ent.work_profile.apps[i].required = required;
            return 0;
        }
    }
    for (int i = 0; i < ENT_MAX_APPS; i++) {
        if (!ent.work_profile.apps[i].package[0]) {
            strncpy(ent.work_profile.apps[i].package, package, 63);
            ent.work_profile.apps[i].allowed = allowed;
            ent.work_profile.apps[i].required = required;
            ent.work_profile.app_count++;
            return 0;
        }
    }
    return -1;
}

int app_control_get(const char *package, app_control_t *ctrl)
{
    if (!package || !ctrl) return -1;
    for (int i = 0; i < ENT_MAX_APPS; i++) {
        if (ent.work_profile.apps[i].package[0] &&
            strcmp(ent.work_profile.apps[i].package, package) == 0) {
            memcpy(ctrl, &ent.work_profile.apps[i], sizeof(app_control_t));
            return 0;
        }
    }
    return -1;
}

int app_control_list(app_control_t *apps, int max)
{
    if (!apps || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < ENT_MAX_APPS && count < max; i++) {
        if (ent.work_profile.apps[i].package[0]) {
            memcpy(&apps[count++], &ent.work_profile.apps[i], sizeof(app_control_t));
        }
    }
    return count;
}

int vpn_add_profile(const char *name, vpn_type_t type, const char *server, int port)
{
    if (!name || !server) return -1;
    for (int i = 0; i < ENT_MAX_VPN_PROFILES; i++) {
        if (!ent.vpn_profiles[i].used) {
            memset(&ent.vpn_profiles[i], 0, sizeof(vpn_profile_t));
            ent.vpn_profiles[i].used = 1;
            ent.vpn_profiles[i].id = i;
            strncpy(ent.vpn_profiles[i].name, name, 63);
            ent.vpn_profiles[i].type = type;
            strncpy(ent.vpn_profiles[i].server, server, 127);
            ent.vpn_profiles[i].port = port;
            ent.vpn_profiles[i].mtu = 1400;
            ent.vpn_count++;
            return i;
        }
    }
    return -1;
}

int vpn_remove_profile(int profile_id)
{
    if (profile_id < 0 || profile_id >= ENT_MAX_VPN_PROFILES || !ent.vpn_profiles[profile_id].used) return -1;
    ent.vpn_profiles[profile_id].used = 0;
    ent.vpn_count--;
    if (ent.active_vpn == profile_id) ent.active_vpn = -1;
    return 0;
}

int vpn_connect(int profile_id)
{
    if (profile_id < 0 || profile_id >= ENT_MAX_VPN_PROFILES || !ent.vpn_profiles[profile_id].used) return -1;
    ent.active_vpn = profile_id;
    vga_printf("[ENT] VPN connect: %s(%s)\n", ent.vpn_profiles[profile_id].name,
               ent.vpn_profiles[profile_id].server);
    return 0;
}

int vpn_disconnect(void)
{
    if (ent.active_vpn < 0) return -1;
    vga_puts("[ENT] VPN disconnect \n");
    ent.active_vpn = -1;
    return 0;
}

int vpn_get_active(vpn_profile_t *profile)
{
    if (ent.active_vpn < 0 || !profile) return -1;
    memcpy(profile, &ent.vpn_profiles[ent.active_vpn], sizeof(vpn_profile_t));
    return 0;
}

int vpn_list(vpn_profile_t *profiles, int max)
{
    if (!profiles || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < ENT_MAX_VPN_PROFILES && count < max; i++) {
        if (ent.vpn_profiles[i].used) memcpy(&profiles[count++], &ent.vpn_profiles[i], sizeof(vpn_profile_t));
    }
    return count;
}

int ent_wifi_add_profile(const char *ssid, int security, const char *password)
{
    if (!ssid) return -1;
    for (int i = 0; i < ENT_MAX_WIFI_PROFILES; i++) {
        if (!ent.wifi_profiles[i].used) {
            memset(&ent.wifi_profiles[i], 0, sizeof(ent_wifi_profile_t));
            ent.wifi_profiles[i].used = 1;
            ent.wifi_profiles[i].id = i;
            strncpy(ent.wifi_profiles[i].ssid, ssid, 31);
            ent.wifi_profiles[i].security = security;
            if (password) strncpy(ent.wifi_profiles[i].password, password, 127);
            ent.wifi_profiles[i].auto_connect = 1;
            ent.wifi_count++;
            return i;
        }
    }
    return -1;
}

int ent_wifi_remove_profile(int profile_id)
{
    if (profile_id < 0 || profile_id >= ENT_MAX_WIFI_PROFILES || !ent.wifi_profiles[profile_id].used) return -1;
    ent.wifi_profiles[profile_id].used = 0;
    ent.wifi_count--;
    return 0;
}

int ent_wifi_connect(int profile_id)
{
    if (profile_id < 0 || profile_id >= ENT_MAX_WIFI_PROFILES || !ent.wifi_profiles[profile_id].used) return -1;
    vga_printf("[ENT] WiFi connect: %s\n", ent.wifi_profiles[profile_id].ssid);
    return 0;
}

int ent_wifi_list(ent_wifi_profile_t *profiles, int max)
{
    if (!profiles || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < ENT_MAX_WIFI_PROFILES && count < max; i++) {
        if (ent.wifi_profiles[i].used) memcpy(&profiles[count++], &ent.wifi_profiles[i], sizeof(ent_wifi_profile_t));
    }
    return count;
}

int workprofile_create(const char *name, const char *owner)
{
    if (!name) return -1;
    ent.work_profile.enabled = 1;
    ent.work_profile.profile_id = 1;
    strncpy(ent.work_profile.profile_name, name, 63);
    if (owner) strncpy(ent.work_profile.owner, owner, 63);
    ent.work_profile.isolation_level = 2;
    ent.work_profile.data_sharing = 2;  /* 提示 */
    ent.work_profile.clipboard_sharing = 0;
    ent.work_profile.storage_isolation = 1;
    strncpy(ent.work_profile.work_directory, "/data/work/", 255);
    vga_printf("[ENT] create work profile: %s\n", name);
    return 0;
}

int workprofile_remove(void)
{
    memset(&ent.work_profile, 0, sizeof(work_profile_t));
    ent.work_profile.enabled = 0;
    vga_puts("[ENT] remove work profile \n");
    return 0;
}

int workprofile_get(work_profile_t *profile)
{
    if (!profile) return -1;
    memcpy(profile, &ent.work_profile, sizeof(work_profile_t));
    return 0;
}

int workprofile_set_isolation(int level)
{
    if (level < 0 || level > 2) return -1;
    ent.work_profile.isolation_level = level;
    return 0;
}

int workprofile_add_app(const char *package, int allowed)
{
    return app_control_set(package, allowed, 0);
}

int workprofile_remove_app(const char *package)
{
    if (!package) return -1;
    for (int i = 0; i < ENT_MAX_APPS; i++) {
        if (ent.work_profile.apps[i].package[0] &&
            strcmp(ent.work_profile.apps[i].package, package) == 0) {
            memset(&ent.work_profile.apps[i], 0, sizeof(app_control_t));
            ent.work_profile.app_count--;
            return 0;
        }
    }
    return -1;
}

int workprofile_is_enabled(void) { return ent.work_profile.enabled; }

int cert_install(const char *alias, const uint8_t *cert, int len, int trust_level)
{
    if (!alias || !cert) return -1;
    for (int i = 0; i < ENT_MAX_CERTS; i++) {
        if (!ent.certs[i].used) {
            memset(&ent.certs[i], 0, sizeof(cert_policy_t));
            ent.certs[i].used = 1;
            ent.certs[i].id = i;
            strncpy(ent.certs[i].alias, alias, 63);
            if (len > 512) len = 512;
            memcpy(ent.certs[i].cert_data, cert, len);
            ent.certs[i].cert_len = len;
            ent.certs[i].trust_level = trust_level;
            ent.cert_count++;
            return i;
        }
    }
    return -1;
}

int cert_uninstall(const char *alias)
{
    if (!alias) return -1;
    for (int i = 0; i < ENT_MAX_CERTS; i++) {
        if (ent.certs[i].used && strcmp(ent.certs[i].alias, alias) == 0) {
            ent.certs[i].used = 0;
            ent.cert_count--;
            return 0;
        }
    }
    return -1;
}

int cert_get(const char *alias, cert_policy_t *cert)
{
    if (!alias || !cert) return -1;
    for (int i = 0; i < ENT_MAX_CERTS; i++) {
        if (ent.certs[i].used && strcmp(ent.certs[i].alias, alias) == 0) {
            memcpy(cert, &ent.certs[i], sizeof(cert_policy_t));
            return i;
        }
    }
    return -1;
}

int cert_list(cert_policy_t *certs, int max)
{
    if (!certs || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < ENT_MAX_CERTS && count < max; i++) {
        if (ent.certs[i].used) memcpy(&certs[count++], &ent.certs[i], sizeof(cert_policy_t));
    }
    return count;
}

int compliance_check(void)
{
    ent.compliance.violation_count = 0;
    ent.compliance.compliant = 1;
    /* 检查密码策略 */
    policy_t p;
    if (policy_get(POLICY_PASSWORD, &p) == 0 && p.enabled) {
        /* 原型阶段: 假设符合 */
    }
    /* 检查加密策略 */
    if (policy_get(POLICY_ENCRYPTION, &p) == 0 && p.enabled) {
        /* 原型阶段: 假设符合 */
    }
    ent.compliance.last_check = 0;
    return ent.compliance.compliant ? 0 : -1;
}

int compliance_get_status(compliance_status_t *status)
{
    if (!status) return -1;
    memcpy(status, &ent.compliance, sizeof(compliance_status_t));
    return 0;
}

int compliance_is_compliant(void) { return ent.compliance.compliant; }

int remote_wipe(void)
{
    vga_puts("[ENT] remote wipe device \n");
    ent.remote_wipe_pending = 1;
    return 0;
}

int remote_lock(void)
{
    vga_puts("[ENT] remote lock \n");
    ent.remote_lock_pending = 1;
    return 0;
}

int remote_reset_password(const char *new_password)
{
    (void)new_password;
    vga_puts("[ENT] remote reset pwd \n");
    return 0;
}

int enterprise_policy_count(void) { return ent.policy_count; }
int enterprise_vpn_count(void) { return ent.vpn_count; }
int enterprise_cert_count(void) { return ent.cert_count; }
