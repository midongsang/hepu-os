#ifndef ENTERPRISE2_H
#define ENTERPRISE2_H

#include <stdint.h>

#define ENT2_MAX_POLICIES 32
#define ENT2_MAX_CERTIFICATES 32
#define ENT2_MAX_AUDIT_LOGS 256
#define ENT2_MAX_CONTAINERS 8
#define ENT2_MAX_APPS_RESTRICTED 64
#define ENT2_MAX_VPN_PROFILES 4

/* MDM策略类型 */
typedef enum {
    ENT_POLICY_PASSCODE = 0,
    ENT_POLICY_ENCRYPTION,
    ENT_POLICY_RESTRICTIONS,
    ENT_POLICY_WIFI,
    ENT_POLICY_VPN,
    ENT_POLICY_EMAIL,
    ENT_POLICY_APP_CONFIG,
    ENT_POLICY_COMPLIANCE,
    ENT_POLICY_SECURITY,
    ENT_POLICY_NETWORK,
    ENT_POLICY_CUSTOM,
} ent_policy_type_t;

/* 策略状态 */
typedef enum {
    ENT_POLICY_INACTIVE = 0,
    ENT_POLICY_ACTIVE,
    ENT_POLICY_ENFORCED,
    ENT_POLICY_PENDING,
    ENT_POLICY_FAILED,
} ent_policy_state_t;

/* MDM策略 */
typedef struct {
    int used;
    int id;
    char name[64];
    ent_policy_type_t type;
    ent_policy_state_t state;
    uint64_t applied_time;
    uint64_t modified_time;
    int priority;
    /* 通用配置 */
    int passcode_required;
    int passcode_min_length;
    int passcode_complex;
    int passcode_max_age_days;
    int passcode_history;
    int max_failed_attempts;
    int auto_lock_minutes;
    /* 加密 */
    int storage_encryption;
    int backup_encryption;
    /* 限制 */
    int allow_camera;
    int allow_screenshot;
    int allow_usb_file_transfer;
    int allow_ota_update;
    int allow_factory_reset;
    int allow_sideload;
    int allow_unknown_sources;
    int allow_debugging;
    int allow_nfc;
    int allow_bluetooth;
    int allow_wifi;
    int allow_hotspot;
    int allow_vpn;
    /* 合规 */
    int jailbreak_detection;
    int compliance_check_interval;
    int noncompliance_action;  /* 0=none,1=warn,2=block,3=wipe */
} ent_policy_t;

/* 容器 */
typedef struct {
    int used;
    int id;
    char name[64];
    int active;
    int isolated;
    /* 容器配置 */
    int data_isolation;
    int network_isolation;
    int clipboard_sharing;
    int file_sharing;
    int camera_access;
    int microphone_access;
    int location_access;
    int contacts_access;
    int photo_access;
    /* 容器应用 */
    int app_ids[32];
    int app_count;
    /* 容器存储 */
    uint64_t storage_limit_mb;
    uint64_t storage_used_mb;
    /* 容器网络 */
    int vpn_only;
    int vpn_profile_id;
} ent_container_t;

/* 证书类型 */
typedef enum {
    ENT_CERT_ROOT = 0,
    ENT_CERT_INTERMEDIATE,
    ENT_CERT_CLIENT,
    ENT_CERT_SERVER,
    ENT_CERT_CODE_SIGNING,
    ENT_CERT_EMAIL,
    ENT_CERT_DEVICE,
    ENT_CERT_CA,
} ent_cert_type_t;

/* 证书 */
typedef struct {
    int used;
    int id;
    char name[64];
    ent_cert_type_t type;
    char issuer[128];
    char subject[128];
    char serial[64];
    char fingerprint_sha256[64];
    uint64_t valid_from;
    uint64_t valid_to;
    int key_size;
    char algorithm[32];
    int trusted;
    int expired;
    int revoked;
    int in_keystore;
} ent_certificate_t;

/* 审计日志类型 */
typedef enum {
    ENT_AUDIT_LOGIN = 0,
    ENT_AUDIT_LOGOUT,
    ENT_AUDIT_POLICY_CHANGE,
    ENT_AUDIT_APP_INSTALL,
    ENT_AUDIT_APP_UNINSTALL,
    ENT_AUDIT_DATA_ACCESS,
    ENT_AUDIT_DATA_EXPORT,
    ENT_AUDIT_CONFIG_CHANGE,
    ENT_AUDIT_SECURITY_EVENT,
    ENT_AUDIT_COMPLIANCE,
    ENT_AUDIT_REMOTE_COMMAND,
    ENT_AUDIT_WIPE,
    ENT_AUDIT_LOCK,
    ENT_AUDIT_UNLOCK,
    ENT_AUDIT_CERTIFICATE,
    ENT_AUDIT_VPN,
    ENT_AUDIT_NETWORK,
    ENT_AUDIT_ERROR,
} ent_audit_type_t;

/* 审计日志 */
typedef struct {
    int used;
    int id;
    ent_audit_type_t type;
    uint64_t timestamp;
    char user[64];
    char action[128];
    char target[128];
    int result;  /* 0=success,1=fail,2=denied */
    char ip_address[48];
    char device_id[64];
    int severity;  /* 0=info,1=warning,2=error,3=critical */
} ent_audit_log_t;

/* VPN配置 */
typedef struct {
    int used;
    int id;
    char name[64];
    int type;  /* 0=IPSec,1=OpenVPN,2=WireGuard,3=L2TP,4=PPTP,5=SSL */
    char server[128];
    int port;
    char username[64];
    int mtu;
    int always_on;
    int per_app;
    int connected;
    uint64_t connect_time;
    uint64_t bytes_sent;
    uint64_t bytes_received;
} ent_vpn_t;

/* 合规状态 */
typedef struct {
    int compliant;
    int last_check;
    int next_check;
    int issues[16];
    int issue_count;
    char details[256];
} ent_compliance_t;

/* 远程命令 */
typedef enum {
    ENT_CMD_LOCK = 0,
    ENT_CMD_UNLOCK,
    ENT_CMD_WIPE,
    ENT_CMD_RESET_PASSCODE,
    ENT_CMD_INSTALL_APP,
    ENT_CMD_REMOVE_APP,
    ENT_CMD_APPLY_POLICY,
    ENT_CMD_REMOVE_POLICY,
    ENT_CMD_LOCATE,
    ENT_CMD_MESSAGE,
    ENT_CMD_REBOOT,
    ENT_CMD_SHUTDOWN,
    ENT_CMD_ENABLE_KIOSK,
    ENT_CMD_DISABLE_KIOSK,
} ent_command_t;

/* 企业安全状态 */
typedef struct {
    int initialized;
    /* MDM */
    int mdm_enrolled;
    char mdm_server[128];
    char mdm_org[64];
    char device_id[64];
    int enrollment_time;
    /* 策略 */
    ent_policy_t policies[ENT2_MAX_POLICIES];
    int policy_count;
    /* 容器 */
    ent_container_t containers[ENT2_MAX_CONTAINERS];
    int container_count;
    /* 证书 */
    ent_certificate_t certificates[ENT2_MAX_CERTIFICATES];
    int certificate_count;
    /* 审计 */
    ent_audit_log_t audit_logs[ENT2_MAX_AUDIT_LOGS];
    int audit_count;
    int audit_enabled;
    /* VPN */
    ent_vpn_t vpns[ENT2_MAX_VPN_PROFILES];
    int vpn_count;
    /* 合规 */
    ent_compliance_t compliance;
    /* 安全启动 */
    int secure_boot_enabled;
    int verified_boot;
    int bootloader_locked;
    int dm_verity;
    /* Kiosk模式 */
    int kiosk_mode;
    int kiosk_app_id;
    /* 统计 */
    uint64_t total_audit_events;
    int policy_violations;
    int security_events;
} enterprise2_t;

/* 初始化企业安全 */
void enterprise2_init(void);

/* MDM管理 */
int ent2_mdm_enroll(const char *server, const char *org, const char *enrollment_code);
int ent2_mdm_unenroll(void);
int ent2_mdm_is_enrolled(void);
int ent2_mdm_get_info(char *server, int server_size, char *org, int org_size);

/* 策略管理 */
int ent2_policy_add(ent_policy_t *policy);
int ent2_policy_remove(int policy_id);
int ent2_policy_apply(int policy_id);
int ent2_policy_get(int policy_id, ent_policy_t *policy);
int ent2_policy_list(ent_policy_t *policies, int max);
int ent2_policy_update(int policy_id, ent_policy_t *policy);

/* 容器管理 */
int ent2_container_create(const char *name);
int ent2_container_remove(int container_id);
int ent2_container_start(int container_id);
int ent2_container_stop(int container_id);
int ent2_container_get(int container_id, ent_container_t *container);
int ent2_container_list(ent_container_t *containers, int max);
int ent2_container_add_app(int container_id, int app_id);
int ent2_container_remove_app(int container_id, int app_id);

/* 证书管理 */
int ent2_cert_install(ent_certificate_t *cert);
int ent2_cert_uninstall(int cert_id);
int ent2_cert_get(int cert_id, ent_certificate_t *cert);
int ent2_cert_list(ent_certificate_t *certs, int max);
int ent2_cert_verify(int cert_id);
int ent2_cert_trust(int cert_id, int trust);

/* 审计日志 */
int ent2_audit_log(ent_audit_type_t type, const char *user, const char *action, const char *target, int result);
int ent2_audit_get_logs(ent_audit_log_t *logs, int max);
int ent2_audit_clear(void);
int ent2_audit_enable(int enable);
int ent2_audit_export(void *buffer, uint32_t *size);

/* VPN */
int ent2_vpn_add(ent_vpn_t *vpn);
int ent2_vpn_remove(int vpn_id);
int ent2_vpn_connect(int vpn_id);
int ent2_vpn_disconnect(int vpn_id);
int ent2_vpn_list(ent_vpn_t *vpns, int max);

/* 合规 */
int ent2_compliance_check(void);
int ent2_compliance_get(ent_compliance_t *compliance);
int ent2_compliance_is_compliant(void);

/* 远程命令 */
int ent2_command_execute(ent_command_t cmd, const char *arg);
int ent2_command_lock(void);
int ent2_command_wipe(void);
int ent2_command_locate(double *lat, double *lng);

/* 安全启动 */
int ent2_secure_boot_enable(int enable);
int ent2_secure_boot_status(void);
int ent2_verify_boot(void);
int ent2_lock_bootloader(int lock);

/* Kiosk模式 */
int ent2_kiosk_enable(int app_id);
int ent2_kiosk_disable(void);
int ent2_kiosk_is_active(void);

/* 统计 */
int ent2_get_stats(int *policies, int *containers, int *certs, int *audit_events, int *violations);

#endif /* ENTERPRISE2_H */
