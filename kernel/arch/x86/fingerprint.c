/* ============================================================================
 * 赫普OS - 内核只读版权指纹 (HEPU Kernel Fingerprint)
 *
 * 这是赫普OS开源保护机制的核心技术实现：
 * 1. 指纹结构编译进内核二进制，位于固定可识别位置
 * 2. 即使OEM厂商删除UI层的" 关于本机 "来源标注，
 *    审计工具仍可从内核二进制中提取此指纹
 * 3. 启动时自检，指纹被篡改则强制警示
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include "fingerprint.h"

/* 指纹魔数 "HEPU" */
#define HEPU_FP_MAGIC 0x48455055

/* 简单的字符串比较 */
static int __attribute__((unused)) fp_strcmp(const char *a, const char *b)
{
    while (*a && *b && *a == *b) { a++; b++; }
    return *a - *b;
}

/* 简单的字符串长度 */
static size_t fp_strlen(const char *s)
{
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

/* ============================================================================
 * 校验内核版权指纹完整性
 * 返回 valid=1 表示指纹未被篡改
 * ========================================================================== */
fingerprint_result_t fingerprint_verify(const hepu_fingerprint_t *fp)
{
    fingerprint_result_t result = {0, "unknown"};

    if (!fp) {
        result.reason = "fingerprint pointer is null";
        return result;
    }

    /* 检查魔数 */
    if (fp->magic != HEPU_FP_MAGIC) {
        result.reason = "magic number corrupted";
        return result;
    }

    /* 检查名称字段 */
    if (fp_strlen(fp->name) == 0) {
        result.reason = "name field cleared";
        return result;
    }

    /* 检查许可证字段 */
    if (fp_strlen(fp->license) == 0) {
        result.reason = "license field cleared";
        return result;
    }

    /* 检查开源标志位 */
    if (!(fp->flags & 0x1)) {
        result.reason = "GPL open-source flag cleared";
        return result;
    }

    result.valid = 1;
    result.reason = "ok";
    return result;
}

/* ============================================================================
 * 从内核镜像中提取指纹（供审计工具使用的接口声明）
 * 实际实现中，审计工具会扫描内核二进制寻找 HEPU_FP_MAGIC
 * ========================================================================== */
const hepu_fingerprint_t *fingerprint_locate(void)
{
    /* 链接器会将 hepu_fingerprint 放在 .data 段
     * 审计工具通过魔数扫描定位 */
    extern hepu_fingerprint_t hepu_fingerprint;
    return &hepu_fingerprint;
}
