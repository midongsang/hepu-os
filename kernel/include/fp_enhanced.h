#ifndef FP_ENHANCED_H
#define FP_ENHANCED_H

#include <stdint.h>

/* ============================================================================
 * 赫普OS 内核只读版权指纹 - 强化版
 * 启动自检 + 篡改警示 + 合规取证 + 多处来源标注
 * ========================================================================== */

#define FP_MAGIC 0x48455055  /* "HEPU" */
#define FP_VERSION 0x00020000  /* V2.0 强化版 */
#define FP_MAX_LABELS 8
#define FP_LABEL_LEN 128
#define FP_MAX_EVIDENCE 16

/* 指纹标志位 */
#define FP_FLAG_GPLV3        (1 << 0)  /* GPLv3开源 */
#define FP_FLAG_COMMERCIAL   (1 << 1)  /* 商业授权版 */
#define FP_FLAG_OEM          (1 << 2)  /* OEM衍生版 */
#define FP_FLAG_COMMUNITY    (1 << 3)  /* 社区版 */
#define FP_FLAG_SECURE_BOOT  (1 << 4)  /* 安全启动 */
#define FP_FLAG_TAMPERED     (1 << 31) /* 篡改标记(运行时设置) */

/* 篡改检测结果 */
typedef enum {
    FP_TAMPER_NONE = 0,       /* 未检测到篡改 */
    FP_TAMPER_NAME_CHANGED,   /* 系统名称被修改 */
    FP_TAMPER_LICENSE_REMOVED,/* 许可证声明被删除 */
    FP_TAMPER_SOURCE_REMOVED, /* 源码地址被删除 */
    FP_TAMPER_MAGIC_BROKEN,   /* 魔数被破坏 */
    FP_TAMPER_CHECKSUM_FAIL,  /* 校验和失败 */
    FP_TAMPER_SIGNATURE_FAIL, /* 签名验证失败 */
    FP_TAMPER_STRUCT_CORRUPT, /* 结构损坏 */
} fp_tamper_type_t;

/* 来源标注位置 */
typedef enum {
    FP_LABEL_ABOUT = 0,       /* 关于本机页面 */
    FP_LABEL_BOOT,            /* 开机版权提示 */
    FP_LABEL_DOWNLOAD,        /* 刷机下载页 */
    FP_LABEL_SETTINGS,        /* 设置页 */
    FP_LABEL_KERNEL_LOG,      /* 内核日志 */
    FP_LABEL_OTA,             /* OTA更新页 */
    FP_LABEL_RECOVERY,        /* Recovery模式 */
    FP_LABEL_BOOTLOADER,      /* Bootloader */
} fp_label_location_t;

/* 来源标注 */
typedef struct {
    int used;
    fp_label_location_t location;
    char text[FP_LABEL_LEN];
    int required;  /* 是否强制标注 */
    int present;   /* 是否已标注 */
} fp_source_label_t;

/* 取证记录 */
typedef struct {
    int used;
    fp_tamper_type_t type;
    uint64_t timestamp;
    char detail[256];
    uint8_t evidence_hash[32];
} fp_evidence_t;

/* 强化版指纹结构 */
typedef struct __attribute__((packed)) {
    uint32_t magic;           /* 魔数 "HEPU" */
    uint32_t fp_version;      /* 指纹结构版本 */
    uint32_t kernel_version;  /* 内核版本 */
    uint32_t flags;           /* 标志位 */
    uint32_t build_time;      /* 构建日期 YYYYMMDD */
    uint32_t build_number;    /* 构建号 */
    char     name[32];        /* 系统名称 */
    char     short_name[16];  /* 简称 */
    char     license[64];     /* 许可证声明 */
    char     source[96];      /* 源码仓库地址 */
    char     author[64];      /* 作者/维护者 */
    char     oem_vendor[64];  /* OEM厂商(如为衍生版) */
    uint8_t  kernel_hash[32]; /* 内核镜像SHA256 */
    uint8_t  signature[64];   /* 指纹签名 */
    uint32_t self_checksum;   /* 指纹结构自校验 */
    uint8_t  reserved[64];
} fp_enhanced_t;

/* 指纹验证状态 */
typedef struct {
    int valid;
    int tampered;
    fp_tamper_type_t tamper_type;
    const char *reason;
    int labels_present;
    int labels_required;
    int labels_missing;
} fp_verify_result_t;

/* 指纹管理器状态 */
typedef struct {
    int initialized;
    int enabled;
    fp_enhanced_t fingerprint;
    int fingerprint_loaded;
    int boot_verified;
    int tamper_detected;
    fp_tamper_type_t tamper_type;
    /* 来源标注 */
    fp_source_label_t labels[FP_MAX_LABELS];
    int label_count;
    /* 取证 */
    fp_evidence_t evidence[FP_MAX_EVIDENCE];
    int evidence_count;
    /* 统计 */
    uint64_t total_checks;
    uint64_t tamper_detections;
    uint64_t warnings_issued;
} fp_manager_t;

/* 初始化 */
void fp_enhanced_init(void);

/* 指纹操作 */
int fp_load(const fp_enhanced_t *fp);
int fp_verify(fp_verify_result_t *result);
int fp_self_check(void);
const fp_enhanced_t *fp_get(void);

/* 篡改检测 */
int fp_detect_tamper(fp_tamper_type_t *type);
int fp_report_tamper(fp_tamper_type_t type, const char *detail);
int fp_is_tampered(void);
const char *fp_tamper_type_name(fp_tamper_type_t type);

/* 来源标注 */
int fp_label_register(fp_label_location_t location, const char *text, int required);
int fp_label_check_all(fp_verify_result_t *result);
int fp_label_get(fp_label_location_t location, fp_source_label_t *label);
int fp_label_list(fp_source_label_t *labels, int max);
int fp_label_mark_present(fp_label_location_t location);

/* 合规取证 */
int fp_evidence_add(fp_tamper_type_t type, const char *detail);
int fp_evidence_get(int index, fp_evidence_t *evidence);
int fp_evidence_list(fp_evidence_t *evidence, int max);
int fp_evidence_count(void);

/* 启动自检 */
int fp_boot_self_test(void);
int fp_get_boot_warning(char *buf, int size);

/* 统计 */
int fp_get_stats(fp_manager_t *stats);

#endif /* FP_ENHANCED_H */
