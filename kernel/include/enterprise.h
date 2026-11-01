#ifndef ENTERPRISE_H
#define ENTERPRISE_H

#include <stdint.h>

#define ENT_MAX_POLICIES 32
#define ENT_MAX_APPS 64
#define ENT_MAX_CERTS 16
#define ENT_MAX_VPN_PROFILES 8
#define ENT_MAX_WIFI_PROFILES 16

/* MDM管理状态 */
typedef enum {
    MDM_STATE_UNMANAGED = 0,
    MDM_STATE_ENROLLING,
    MDM_STATE_MANAGED,
    MDM_STATE_COMPLIANT,
    MDM_STATE_NONCOMPLIANT,
    MDM_STATE_WIPED,
} mdm_state_t;

/* 策略类型 */
typedef enum {
    POLICY_PASSWORD = 0,
    POLICY_ENCRYPTION,
    POLICY_APP_INSTALL,
    POLICY_APP_UNINSTALL,
    POLICY_CAMERA,
    POLICY_MICROPHONE,
    POLICY_BLUETOOTH,
    POLICY_WIFI,
    POLICY_USB,
    POLICY_SCREENSHOT,
    POLICY_SCREEN_RECORD,
    POLICY_CLIPBOARD,
    POLICY_LOCATION,
    POLICY_NFC,
    POLICY_FACTORY_RESET,
    POLICY_SAFE_MODE,
    POLICY_DEBUGGING,
    POLICY_UNKNOWN_SOURCES,
    POLICY_DATA_ROAMING,
    POLICY_VPN,
    POLICY_EMAIL,
    POLICY_WORK_PROFILE,
    POLICY_CUSTOM,
} policy_type_t;

/* 策略值 */
typedef struct {
    int used;
    int id;
    policy_type_t type;
    char name[64];
    int enabled;
    int value_int;
    char value_str[256];
    uint64_t last_modified;
    char modified_by[64];
} policy_t;

/* 应用控制 */
typedef struct {
    char package[64];
    int allowed;
    int required;
    int install_on_enroll;
    int version_code;
    char version_name[32];
} app_control_t;

/* VPN类型 */
typedef enum {
    VPN_TYPE_PPTP = 0,
    VPN_TYPE_L2TP,
    VPN_TYPE_IPSEC,
    VPN_TYPE_OPENVPN,
    VPN_TYPE_WIREGUARD,
    VPN_TYPE_SSL,
} vpn_type_t;

/* VPN配置 */
typedef struct {
    int used;
    int id;
    char name[64];
    vpn_type_t type;
    char server[128];
    int port;
    char username[64];
    char password[128];
    char pre_shared_key[128];
    char certificate[128];
    int always_on;
    int mtu;
    int dns1, dns2;
    char search_domain[128];
    int routes[16];
    int route_count;
} vpn_profile_t;

/* WiFi配置 */
typedef struct {
    int used;
    int id;
    char ssid[32];
    int security;  /* 0=open,1=WEP,2=WPA,3=WPA2,4=WPA3,5=EAP */
    char password[128];
    char identity[64];
    char ca_cert[128];
    char client_cert[128];
    int auto_connect;
    int hidden;
    int metered;
    int proxy_type;  /* 0=none,1=manual,2=PAC */
    char proxy_host[128];
    int proxy_port;
    char proxy_pac[256];
} ent_wifi_profile_t;

/* 工作资料 */
typedef struct {
    int enabled;
    int profile_id;
    char profile_name[64];
    char owner[64];
    int isolation_level;  /* 0=none,1=basic,2=strict */
    int app_count;
    app_control_t apps[ENT_MAX_APPS];
    int data_sharing;  /* 个人→工作 0=禁止,1=允许,2=提示 */
    int clipboard_sharing;
    int contact_sharing;
    int calendar_sharing;
    int camera_access;
    int microphone_access;
    int location_access;
    int storage_isolation;
    char work_directory[256];
    uint64_t creation_time;
    uint64_t last_sync;
} work_profile_t;

/* 证书策略 */
typedef struct {
    int used;
    int id;
    char alias[64];
    char subject[128];
    char issuer[128];
    uint64_t valid_from;
    uint64_t valid_to;
    int key_size;
    int key_type;  /* 0=RSA,1=EC,2=DSA */
    int usage;  /* 位掩码: 1=签名,2=加密,4=认证,8=客户端认证 */
    int is_ca;
    int trust_level;  /* 0=不信任,1=用户,2=系统,3=企业 */
    uint8_t cert_data[512];
    int cert_len;
} cert_policy_t;

/* 合规状态 */
typedef struct {
    int compliant;
    int policy_violations;
    char violations[16][64];
    int violation_count;
    uint64_t last_check;
    uint64_t next_check;
} compliance_status_t;

/* 企业功能状态 */
typedef struct {
    int initialized;
    mdm_state_t mdm_state;
    char mdm_server[256];
    char device_id[64];
    char enrollment_user[64];
    char organization[128];
    /* 策略 */
    policy_t policies[ENT_MAX_POLICIES];
    int policy_count;
    /* VPN */
    vpn_profile_t vpn_profiles[ENT_MAX_VPN_PROFILES];
    int vpn_count;
    int active_vpn;
    /* WiFi */
    ent_wifi_profile_t wifi_profiles[ENT_MAX_WIFI_PROFILES];
    int wifi_count;
    /* 工作资料 */
    work_profile_t work_profile;
    /* 证书 */
    cert_policy_t certs[ENT_MAX_CERTS];
    int cert_count;
    /* 合规 */
    compliance_status_t compliance;
    /* 远程管理 */
    int remote_wipe_pending;
    int remote_lock_pending;
    uint64_t last_checkin;
    uint64_t checkin_interval;
} enterprise_t;

/* 初始化企业功能 */
void enterprise_init(void);

/* MDM管理 */
int mdm_enroll(const char *server, const char *user, const char *org);
int mdm_unenroll(void);
int mdm_get_state(mdm_state_t *state);
int mdm_checkin(void);
int mdm_set_server(const char *server);
int mdm_get_device_id(char *id, int len);

/* 策略管理 */
int policy_set(policy_type_t type, int enabled, int value, const char *value_str);
int policy_get(policy_type_t type, policy_t *policy);
int policy_remove(policy_type_t type);
int policy_list(policy_t *policies, int max);
int policy_apply_all(void);

/* 应用控制 */
int app_control_set(const char *package, int allowed, int required);
int app_control_get(const char *package, app_control_t *ctrl);
int app_control_list(app_control_t *apps, int max);

/* VPN */
int vpn_add_profile(const char *name, vpn_type_t type, const char *server, int port);
int vpn_remove_profile(int profile_id);
int vpn_connect(int profile_id);
int vpn_disconnect(void);
int vpn_get_active(vpn_profile_t *profile);
int vpn_list(vpn_profile_t *profiles, int max);

/* WiFi */
int ent_wifi_add_profile(const char *ssid, int security, const char *password);
int ent_wifi_remove_profile(int profile_id);
int ent_wifi_connect(int profile_id);
int ent_wifi_list(ent_wifi_profile_t *profiles, int max);

/* 工作资料 */
int workprofile_create(const char *name, const char *owner);
int workprofile_remove(void);
int workprofile_get(work_profile_t *profile);
int workprofile_set_isolation(int level);
int workprofile_add_app(const char *package, int allowed);
int workprofile_remove_app(const char *package);
int workprofile_is_enabled(void);

/* 证书策略 */
int cert_install(const char *alias, const uint8_t *cert, int len, int trust_level);
int cert_uninstall(const char *alias);
int cert_get(const char *alias, cert_policy_t *cert);
int cert_list(cert_policy_t *certs, int max);

/* 合规检查 */
int compliance_check(void);
int compliance_get_status(compliance_status_t *status);
int compliance_is_compliant(void);

/* 远程管理 */
int remote_wipe(void);
int remote_lock(void);
int remote_reset_password(const char *new_password);

/* 统计 */
int enterprise_policy_count(void);
int enterprise_vpn_count(void);
int enterprise_cert_count(void);

#endif /* ENTERPRISE_H */
