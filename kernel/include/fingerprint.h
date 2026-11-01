#ifndef FINGERPRINT_H
#define FINGERPRINT_H

#include <stdint.h>

/* ============================================================================
 * 内核只读版权指纹结构
 * 此结构编译进内核二进制，位于 .data 段
 * 审计工具通过魔数 0x48455055 ("HEPU") 扫描定位
 * ========================================================================== */
typedef struct {
    uint32_t magic;         /* 魔数 "HEPU" = 0x48455055 */
    uint32_t fp_version;    /* 指纹结构版本 */
    uint32_t kernel_version;/* 内核版本 */
    uint32_t flags;         /* bit0=GPLv3开源, bit1=商业授权版 */
    uint32_t build_time;    /* 构建日期 YYYYMMDD */
    char     name[32];      /* 系统名称 */
    char     license[48];   /* 许可证声明 */
    char     source[64];    /* 源码仓库地址 */
    uint8_t  padding[64];   /* 保留对齐 */
} __attribute__((packed)) hepu_fingerprint_t;

/* 校验结果 */
typedef struct {
    int         valid;
    const char *reason;
} fingerprint_result_t;

/* 校验指纹完整性 */
fingerprint_result_t fingerprint_verify(const hepu_fingerprint_t *fp);

/* 定位内核中的指纹结构 */
const hepu_fingerprint_t *fingerprint_locate(void);

#endif
