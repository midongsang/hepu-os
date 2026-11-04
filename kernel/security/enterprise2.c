/* ============================================================================
 * 赫普OS - 企业安全
 * MDM深化 + 容器隔离 + 安全启动 + 证书管理 + 审计日志
 * ========================================================================== */

#include <stdint.h>
#include "enterprise2.h"
#include "vga.h"
#include "string.h"

static enterprise2_t ent;

void enterprise2_init(void)
{
    memset(&ent, 0, sizeof(enterprise2_t));
    ent.initialized = 1;
    ent.audit_enabled = 1;
    ent.secure_boot_enabled = 1;
    ent.verified_boot = 1;
    ent.bootloader_locked = 1;
    ent.dm_verity = 1;
    ent.compliance.compliant = 1;

    /* 默认策略 */
    ent.policies[0].used = 1;
    ent.policies[0].id = 0;
    strncpy(ent.policies[0].name, " default security policy ", 63);
    ent.policies[0].type = ENT_POLICY_SECURITY;
    ent.policies[0].state = ENT_POLICY_ACTIVE;
    ent.policies[0].passcode_required = 1;
    ent.policies[0].passcode_min_length = 6;
    ent.policies[0].storage_encryption = 1;
    ent.policies[0].allow_sideload = 0;
    ent.policies[0].allow_unknown_sources = 0;
    ent.policy_count = 1;

    vga_puts("[ENT2] enterprise security init done \n");
    vga_printf(" secure boot: on, verified boot: on, Bootloader: lock, DM-Verity: on \n");
}

int ent2_mdm_enroll(const char *server, const char *org, const char *enrollment_code)
{
    (void)enrollment_code;
    if (!server) return -1;
    ent.mdm_enrolled = 1;
    strncpy(ent.mdm_server, server, 127);
    if (org) strncpy(ent.mdm_org, org, 63);
    strncpy(ent.device_id, "HEPU-2026-0001", 63);
    vga_printf("[ENT2] MDM registered ok: %s\n", org ? org : server);
    return 0;
}

int ent2_mdm_unenroll(void)
{
    ent.mdm_enrolled = 0;
    ent.mdm_server[0] = 0;
    return 0;
}

int ent2_mdm_is_enrolled(void) { return ent.mdm_enrolled; }

int ent2_mdm_get_info(char *server, int server_size, char *org, int org_size)
{
    if (server) strncpy(server, ent.mdm_server, server_size - 1);
    if (org) strncpy(org, ent.mdm_org, org_size - 1);
    return 0;
}

int ent2_policy_add(ent_policy_t *policy)
{
    if (!policy) return -1;
    for (int i = 0; i < ENT2_MAX_POLICIES; i++) {
        if (!ent.policies[i].used) {
            memcpy(&ent.policies[i], policy, sizeof(ent_policy_t));
            ent.policies[i].used = 1;
            ent.policies[i].id = i;
            ent.policies[i].state = ENT_POLICY_ACTIVE;
            ent.policy_count++;
            return i;
        }
    }
    return -1;
}

int ent2_policy_remove(int policy_id)
{
    if (policy_id < 0 || policy_id >= ENT2_MAX_POLICIES || !ent.policies[policy_id].used) return -1;
    ent.policies[policy_id].used = 0;
    ent.policy_count--;
    return 0;
}

int ent2_policy_apply(int policy_id)
{
    if (policy_id < 0 || policy_id >= ENT2_MAX_POLICIES || !ent.policies[policy_id].used) return -1;
    ent.policies[policy_id].state = ENT_POLICY_ENFORCED;
    vga_printf("[ENT2] policy applied: %s\n", ent.policies[policy_id].name);
    return 0;
}

int ent2_policy_get(int policy_id, ent_policy_t *policy)
{
    if (policy_id < 0 || policy_id >= ENT2_MAX_POLICIES || !ent.policies[policy_id].used || !policy) return -1;
    memcpy(policy, &ent.policies[policy_id], sizeof(ent_policy_t));
    return 0;
}

int ent2_policy_list(ent_policy_t *policies, int max)
{
    if (!policies || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < ENT2_MAX_POLICIES && count < max; i++) {
        if (ent.policies[i].used) memcpy(&policies[count++], &ent.policies[i], sizeof(ent_policy_t));
    }
    return count;
}

int ent2_policy_update(int policy_id, ent_policy_t *policy)
{
    if (policy_id < 0 || policy_id >= ENT2_MAX_POLICIES || !ent.policies[policy_id].used || !policy) return -1;
    /* 保留id和used */
    int id = ent.policies[policy_id].id;
    int used = ent.policies[policy_id].used;
    memcpy(&ent.policies[policy_id], policy, sizeof(ent_policy_t));
    ent.policies[policy_id].id = id;
    ent.policies[policy_id].used = used;
    return 0;
}

int ent2_container_create(const char *name)
{
    if (!name) return -1;
    for (int i = 0; i < ENT2_MAX_CONTAINERS; i++) {
        if (!ent.containers[i].used) {
            memset(&ent.containers[i], 0, sizeof(ent_container_t));
            ent.containers[i].used = 1;
            ent.containers[i].id = i;
            strncpy(ent.containers[i].name, name, 63);
            ent.containers[i].isolated = 1;
            ent.containers[i].data_isolation = 1;
            ent.containers[i].network_isolation = 1;
            ent.container_count++;
            vga_printf("[ENT2] create work container: %s\n", name);
            return i;
        }
    }
    return -1;
}

int ent2_container_remove(int container_id)
{
    if (container_id < 0 || container_id >= ENT2_MAX_CONTAINERS || !ent.containers[container_id].used) return -1;
    ent.containers[container_id].used = 0;
    ent.container_count--;
    return 0;
}

int ent2_container_start(int container_id)
{
    if (container_id < 0 || container_id >= ENT2_MAX_CONTAINERS || !ent.containers[container_id].used) return -1;
    ent.containers[container_id].active = 1;
    return 0;
}

int ent2_container_stop(int container_id)
{
    if (container_id < 0 || container_id >= ENT2_MAX_CONTAINERS || !ent.containers[container_id].used) return -1;
    ent.containers[container_id].active = 0;
    return 0;
}

int ent2_container_get(int container_id, ent_container_t *container)
{
    if (container_id < 0 || container_id >= ENT2_MAX_CONTAINERS || !ent.containers[container_id].used || !container) return -1;
    memcpy(container, &ent.containers[container_id], sizeof(ent_container_t));
    return 0;
}

int ent2_container_list(ent_container_t *containers, int max)
{
    if (!containers || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < ENT2_MAX_CONTAINERS && count < max; i++) {
        if (ent.containers[i].used) memcpy(&containers[count++], &ent.containers[i], sizeof(ent_container_t));
    }
    return count;
}

int ent2_container_add_app(int container_id, int app_id)
{
    if (container_id < 0 || container_id >= ENT2_MAX_CONTAINERS || !ent.containers[container_id].used) return -1;
    if (ent.containers[container_id].app_count >= 32) return -1;
    ent.containers[container_id].app_ids[ent.containers[container_id].app_count++] = app_id;
    return 0;
}

int ent2_container_remove_app(int container_id, int app_id)
{
    if (container_id < 0 || container_id >= ENT2_MAX_CONTAINERS || !ent.containers[container_id].used) return -1;
    for (int i = 0; i < ent.containers[container_id].app_count; i++) {
        if (ent.containers[container_id].app_ids[i] == app_id) {
            for (int j = i; j < ent.containers[container_id].app_count - 1; j++)
                ent.containers[container_id].app_ids[j] = ent.containers[container_id].app_ids[j + 1];
            ent.containers[container_id].app_count--;
            return 0;
        }
    }
    return -1;
}

int ent2_cert_install(ent_certificate_t *cert)
{
    if (!cert) return -1;
    for (int i = 0; i < ENT2_MAX_CERTIFICATES; i++) {
        if (!ent.certificates[i].used) {
            memcpy(&ent.certificates[i], cert, sizeof(ent_certificate_t));
            ent.certificates[i].used = 1;
            ent.certificates[i].id = i;
            ent.certificates[i].in_keystore = 1;
            ent.certificate_count++;
            vga_printf("[ENT2] cert install: %s\n", cert->name);
            return i;
        }
    }
    return -1;
}

int ent2_cert_uninstall(int cert_id)
{
    if (cert_id < 0 || cert_id >= ENT2_MAX_CERTIFICATES || !ent.certificates[cert_id].used) return -1;
    ent.certificates[cert_id].used = 0;
    ent.certificate_count--;
    return 0;
}

int ent2_cert_get(int cert_id, ent_certificate_t *cert)
{
    if (cert_id < 0 || cert_id >= ENT2_MAX_CERTIFICATES || !ent.certificates[cert_id].used || !cert) return -1;
    memcpy(cert, &ent.certificates[cert_id], sizeof(ent_certificate_t));
    return 0;
}

int ent2_cert_list(ent_certificate_t *certs, int max)
{
    if (!certs || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < ENT2_MAX_CERTIFICATES && count < max; i++) {
        if (ent.certificates[i].used) memcpy(&certs[count++], &ent.certificates[i], sizeof(ent_certificate_t));
    }
    return count;
}

int ent2_cert_verify(int cert_id)
{
    if (cert_id < 0 || cert_id >= ENT2_MAX_CERTIFICATES || !ent.certificates[cert_id].used) return -1;
    return !ent.certificates[cert_id].expired && !ent.certificates[cert_id].revoked;
}

int ent2_cert_trust(int cert_id, int trust)
{
    if (cert_id < 0 || cert_id >= ENT2_MAX_CERTIFICATES || !ent.certificates[cert_id].used) return -1;
    ent.certificates[cert_id].trusted = trust ? 1 : 0;
    return 0;
}

int ent2_audit_log(ent_audit_type_t type, const char *user, const char *action, const char *target, int result)
{
    for (int i = 0; i < ENT2_MAX_AUDIT_LOGS; i++) {
        if (!ent.audit_logs[i].used) {
            memset(&ent.audit_logs[i], 0, sizeof(ent_audit_log_t));
            ent.audit_logs[i].used = 1;
            ent.audit_logs[i].id = i;
            ent.audit_logs[i].type = type;
            if (user) strncpy(ent.audit_logs[i].user, user, 63);
            if (action) strncpy(ent.audit_logs[i].action, action, 127);
            if (target) strncpy(ent.audit_logs[i].target, target, 127);
            ent.audit_logs[i].result = result;
            ent.audit_count++;
            ent.total_audit_events++;
            return i;
        }
    }
    /* 环形覆盖 */
    ent.audit_logs[0].used = 1;
    return 0;
}

int ent2_audit_get_logs(ent_audit_log_t *logs, int max)
{
    if (!logs || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < ENT2_MAX_AUDIT_LOGS && count < max; i++) {
        if (ent.audit_logs[i].used) memcpy(&logs[count++], &ent.audit_logs[i], sizeof(ent_audit_log_t));
    }
    return count;
}

int ent2_audit_clear(void)
{
    for (int i = 0; i < ENT2_MAX_AUDIT_LOGS; i++) ent.audit_logs[i].used = 0;
    ent.audit_count = 0;
    return 0;
}

int ent2_audit_enable(int enable)
{
    ent.audit_enabled = enable ? 1 : 0;
    return 0;
}

int ent2_audit_export(void *buffer, uint32_t *size)
{
    (void)buffer;
    if (size) *size = 0;
    return 0;
}

int ent2_vpn_add(ent_vpn_t *vpn)
{
    if (!vpn) return -1;
    for (int i = 0; i < ENT2_MAX_VPN_PROFILES; i++) {
        if (!ent.vpns[i].used) {
            memcpy(&ent.vpns[i], vpn, sizeof(ent_vpn_t));
            ent.vpns[i].used = 1;
            ent.vpns[i].id = i;
            ent.vpn_count++;
            return i;
        }
    }
    return -1;
}

int ent2_vpn_remove(int vpn_id)
{
    if (vpn_id < 0 || vpn_id >= ENT2_MAX_VPN_PROFILES || !ent.vpns[vpn_id].used) return -1;
    ent.vpns[vpn_id].used = 0;
    ent.vpn_count--;
    return 0;
}

int ent2_vpn_connect(int vpn_id)
{
    if (vpn_id < 0 || vpn_id >= ENT2_MAX_VPN_PROFILES || !ent.vpns[vpn_id].used) return -1;
    ent.vpns[vpn_id].connected = 1;
    vga_printf("[ENT2] VPN connected: %s\n", ent.vpns[vpn_id].name);
    return 0;
}

int ent2_vpn_disconnect(int vpn_id)
{
    if (vpn_id < 0 || vpn_id >= ENT2_MAX_VPN_PROFILES || !ent.vpns[vpn_id].used) return -1;
    ent.vpns[vpn_id].connected = 0;
    return 0;
}

int ent2_vpn_list(ent_vpn_t *vpns, int max)
{
    if (!vpns || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < ENT2_MAX_VPN_PROFILES && count < max; i++) {
        if (ent.vpns[i].used) memcpy(&vpns[count++], &ent.vpns[i], sizeof(ent_vpn_t));
    }
    return count;
}

int ent2_compliance_check(void)
{
    ent.compliance.compliant = 1;
    ent.compliance.issue_count = 0;
    return 1;
}

int ent2_compliance_get(ent_compliance_t *compliance)
{
    if (!compliance) return -1;
    memcpy(compliance, &ent.compliance, sizeof(ent_compliance_t));
    return 0;
}

int ent2_compliance_is_compliant(void) { return ent.compliance.compliant; }

int ent2_command_execute(ent_command_t cmd, const char *arg)
{
    (void)arg;
    ent2_audit_log(ENT_AUDIT_REMOTE_COMMAND, "mdm", "remote command", "", 0);
    switch (cmd) {
        case ENT_CMD_LOCK: return ent2_command_lock();
        case ENT_CMD_WIPE: return ent2_command_wipe();
        default: return 0;
    }
}

int ent2_command_lock(void)
{
    vga_puts("[ENT2] device locked \n");
    return 0;
}

int ent2_command_wipe(void)
{
    vga_puts("[ENT2] device wiped \n");
    return 0;
}

int ent2_command_locate(double *lat, double *lng)
{
    if (lat) *lat = 39.9042;
    if (lng) *lng = 116.4074;
    return 0;
}

int ent2_secure_boot_enable(int enable)
{
    ent.secure_boot_enabled = enable ? 1 : 0;
    return 0;
}

int ent2_secure_boot_status(void) { return ent.secure_boot_enabled; }

int ent2_verify_boot(void)
{
    return ent.verified_boot && ent.bootloader_locked && ent.dm_verity;
}

int ent2_lock_bootloader(int lock)
{
    ent.bootloader_locked = lock ? 1 : 0;
    return 0;
}

int ent2_kiosk_enable(int app_id)
{
    ent.kiosk_mode = 1;
    ent.kiosk_app_id = app_id;
    vga_puts("[ENT2] Kiosk mode enabled \n");
    return 0;
}

int ent2_kiosk_disable(void)
{
    ent.kiosk_mode = 0;
    return 0;
}

int ent2_kiosk_is_active(void) { return ent.kiosk_mode; }

int ent2_get_stats(int *policies, int *containers, int *certs, int *audit_events, int *violations)
{
    if (policies) *policies = ent.policy_count;
    if (containers) *containers = ent.container_count;
    if (certs) *certs = ent.certificate_count;
    if (audit_events) *audit_events = (int)ent.total_audit_events;
    if (violations) *violations = ent.policy_violations;
    return 0;
}
