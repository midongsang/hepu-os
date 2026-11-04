#ifndef APK_PARSER_H
#define APK_PARSER_H

#include <stdint.h>

/* ============================================================================
 * 赫普OS APK 解析器
 * APK/ZIP解析 / AndroidManifest.xml解析 / 资源解析 / 签名验证
 * ========================================================================== */

#define APK_MAX_ENTRIES 1024
#define APK_MAX_PERMISSIONS 128
#define APK_MAX_ACTIVITIES 64
#define APK_MAX_SERVICES 32
#define APK_MAX_RECEIVERS 32
#define APK_MAX_PROVIDERS 16
#define APK_NAME_LEN 256
#define APK_PATH_LEN 512

/* APK组件类型 */
typedef enum {
    APK_COMP_ACTIVITY = 0,
    APK_COMP_SERVICE,
    APK_COMP_RECEIVER,
    APK_COMP_PROVIDER,
} apk_comp_type_t;

/* APK组件 */
typedef struct {
    apk_comp_type_t type;
    char name[APK_NAME_LEN];
    char label[128];
    int exported;
    int enabled;
    char permission[128];
    char intent_filter[256];
} apk_component_t;

/* APK权限 */
typedef struct {
    char name[128];
    int level;  /* 0=normal,1=dangerous,2=signature,3=special */
    int granted;
} apk_permission_t;

/* APK条目(ZIP内文件) */
typedef struct {
    int used;
    char name[APK_NAME_LEN];
    uint64_t offset;
    uint64_t compressed_size;
    uint64_t uncompressed_size;
    int compression_method;  /* 0=stored,8=deflate */
    uint32_t crc32;
    int is_directory;
} apk_entry_t;

/* APK签名 */
typedef struct {
    int valid;
    char signer[256];
    char certificate[512];
    char algorithm[64];
    uint64_t sign_time;
    int signature_version;  /* v1/v2/v3 */
} apk_signature_t;

/* APK信息 */
typedef struct {
    int parsed;
    char path[APK_PATH_LEN];
    char package_name[APK_NAME_LEN];
    char app_name[128];
    char version_name[64];
    int version_code;
    int min_sdk;
    int target_sdk;
    int compile_sdk;
    char install_location[32];
    /* 组件 */
    apk_component_t activities[APK_MAX_ACTIVITIES];
    int activity_count;
    apk_component_t services[APK_MAX_SERVICES];
    int service_count;
    apk_component_t receivers[APK_MAX_RECEIVERS];
    int receiver_count;
    apk_component_t providers[APK_MAX_PROVIDERS];
    int provider_count;
    /* 权限 */
    apk_permission_t permissions[APK_MAX_PERMISSIONS];
    int permission_count;
    /* 条目 */
    apk_entry_t entries[APK_MAX_ENTRIES];
    int entry_count;
    /* 签名 */
    apk_signature_t signature;
    /* 元数据 */
    uint64_t file_size;
    uint64_t install_size;
    char icon_path[256];
    char native_libs[16][64];
    int native_lib_count;
    char features[32][64];
    int feature_count;
} apk_info_t;

/* 解析器状态 */
typedef struct {
    int initialized;
    int parsed_count;
    uint64_t total_parsed_size;
} apk_parser_t;

/* 初始化 */
void apk_parser_init(void);

/* 解析 */
int apk_parse(const char *path, apk_info_t *info);
int apk_parse_manifest(const uint8_t *data, int size, apk_info_t *info);
int apk_parse_resources(const uint8_t *data, int size, apk_info_t *info);

/* 条目操作 */
int apk_list_entries(const char *path, apk_entry_t *entries, int max);
int apk_extract_entry(const char *path, const char *entry_name,
                      uint8_t *buf, int max_size);
int apk_find_entry(const apk_info_t *info, const char *name, apk_entry_t *entry);

/* 组件查询 */
int apk_get_activities(const apk_info_t *info, apk_component_t *comps, int max);
int apk_get_services(const apk_info_t *info, apk_component_t *comps, int max);
int apk_get_main_activity(const apk_info_t *info, apk_component_t *comp);

/* 权限 */
int apk_get_permissions(const apk_info_t *info, apk_permission_t *perms, int max);
int apk_check_permission(const apk_info_t *info, const char *perm);

/* 签名验证 */
int apk_verify_signature(const char *path, apk_signature_t *sig);
int apk_verify_integrity(const char *path);

/* 安装信息 */
int apk_get_install_size(const char *path, uint64_t *size);
int apk_get_native_libs(const char *path, char libs[][64], int max);

/* 统计 */
int apk_parser_get_stats(apk_parser_t *stats);

#endif /* APK_PARSER_H */
