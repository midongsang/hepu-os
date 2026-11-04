/* ============================================================================
 * 赫普OS - 内核只读版权指纹 强化版
 * ========================================================================== */

#include <stdint.h>
#include "fp_enhanced.h"
#include "vga.h"
#include "string.h"

static fp_manager_t fpm;

/* 内置默认指纹(编译进内核) */
static const fp_enhanced_t default_fp = {
    .magic = FP_MAGIC,
    .fp_version = FP_VERSION,
    .kernel_version = 0x002600,
    .flags = FP_FLAG_GPLV3 | FP_FLAG_COMMUNITY,
    .build_time = 20260822,
    .build_number = 1,
    .name = "HEPU Hyper Evolve OS",
    .short_name = "HEPU OS",
    .license = "GPLv3 + HEPU 附加 款 ",
    .source = "https://github.com/hepu-os/hepu",
    .author = "HEPU Community",
    .oem_vendor = "",
    .kernel_hash = {0},
    .signature = {0},
    .self_checksum = 0,
};

void fp_enhanced_init(void)
{
    memset(&fpm, 0, sizeof(fp_manager_t));
    fpm.initialized = 1;
    fpm.enabled = 1;
    fpm.fingerprint_loaded = 0;
    fpm.boot_verified = 0;
    fpm.tamper_detected = 0;

    /* 加载默认指纹 */
    memcpy(&fpm.fingerprint, &default_fp, sizeof(fp_enhanced_t));
    fpm.fingerprint_loaded = 1;

    /* 注册强制来源标注(三处) */
    fp_label_register(FP_LABEL_ABOUT,
        " 本 device kernel based on HEPU Hyper Evolve OS Hepu OS 开源 microkernel \n 源码: https://github.com/hepu-os/hepu",
        1);
    fp_label_register(FP_LABEL_BOOT,
        "HEPU Hyper Evolve OS | GPLv3 Open Source | https://github.com/hepu-os/hepu",
        1);
    fp_label_register(FP_LABEL_DOWNLOAD,
        " 本刷机 packages based on HEPU Hyper Evolve OS Hepu OS 开源 microkernel, kernel 源码: https://github.com/hepu-os/hepu",
        1);
    /* 附加标注位置 */
    fp_label_register(FP_LABEL_KERNEL_LOG,
        "HEPU OS V0.26 | GPLv3 | https://github.com/hepu-os/hepu", 0);
    fp_label_register(FP_LABEL_SETTINGS,
        " system kernel: HEPU Hyper Evolve OS(开源)", 0);

    vga_puts("[FP] kernel fingerprint enhanced edition init done \n");
    vga_printf(" fingerprint: %s V%d.%d | %s | markers: %d points(%d mandatory)\n",
               fpm.fingerprint.short_name,
               (fpm.fingerprint.kernel_version >> 16) & 0xFF,
               (fpm.fingerprint.kernel_version >> 8) & 0xFF,
               fpm.fingerprint.license,
               fpm.label_count, 3);
}

/* 指纹操作 */
int fp_load(const fp_enhanced_t *fp)
{
    if (!fp) return -1;
    if (fp->magic != FP_MAGIC) return -1;
    memcpy(&fpm.fingerprint, fp, sizeof(fp_enhanced_t));
    fpm.fingerprint_loaded = 1;
    return 0;
}

int fp_verify(fp_verify_result_t *result)
{
    if (!result) return -1;
    memset(result, 0, sizeof(fp_verify_result_t));
    fpm.total_checks++;

    if (!fpm.fingerprint_loaded) {
        result->valid = 0;
        result->reason = " fingerprint not load ";
        return -1;
    }

    /* 检查魔数 */
    if (fpm.fingerprint.magic != FP_MAGIC) {
        result->valid = 0;
        result->tampered = 1;
        result->tamper_type = FP_TAMPER_MAGIC_BROKEN;
        result->reason = " fingerprint magic 被破 bad ";
        fpm.tamper_detected = 1;
        fpm.tamper_type = FP_TAMPER_MAGIC_BROKEN;
        fpm.tamper_detections++;
        return -1;
    }

    /* 检查系统名称 */
    if (strlen(fpm.fingerprint.name) == 0) {
        result->valid = 0;
        result->tampered = 1;
        result->tamper_type = FP_TAMPER_NAME_CHANGED;
        result->reason = " system name 被 delete ";
        fpm.tamper_detected = 1;
        fpm.tamper_type = FP_TAMPER_NAME_CHANGED;
        fpm.tamper_detections++;
        return -1;
    }

    /* 检查许可证 */
    if (strlen(fpm.fingerprint.license) == 0) {
        result->valid = 0;
        result->tampered = 1;
        result->tamper_type = FP_TAMPER_LICENSE_REMOVED;
        result->reason = " 许可证声明被 delete ";
        fpm.tamper_detected = 1;
        fpm.tamper_type = FP_TAMPER_LICENSE_REMOVED;
        fpm.tamper_detections++;
        return -1;
    }

    /* 检查源码地址 */
    if (strlen(fpm.fingerprint.source) == 0) {
        result->valid = 0;
        result->tampered = 1;
        result->tamper_type = FP_TAMPER_SOURCE_REMOVED;
        result->reason = " 源码 addr 被 delete ";
        fpm.tamper_detected = 1;
        fpm.tamper_type = FP_TAMPER_SOURCE_REMOVED;
        fpm.tamper_detections++;
        return -1;
    }

    /* 检查来源标注 */
    int required = 0, present = 0;
    for (int i = 0; i < fpm.label_count; i++) {
        if (fpm.labels[i].required) {
            required++;
            if (fpm.labels[i].present) present++;
        }
    }
    result->labels_required = required;
    result->labels_present = present;
    result->labels_missing = required - present;

    result->valid = 1;
    result->tampered = 0;
    result->reason = " fingerprint verify ok ";
    return 0;
}

int fp_self_check(void)
{
    /* 简化自校验: 检查关键字段 */
    if (fpm.fingerprint.magic != FP_MAGIC) return -1;
    if (strlen(fpm.fingerprint.name) == 0) return -1;
    if (strlen(fpm.fingerprint.license) == 0) return -1;
    return 0;
}

const fp_enhanced_t *fp_get(void)
{
    return &fpm.fingerprint;
}

/* 篡改检测 */
int fp_detect_tamper(fp_tamper_type_t *type)
{
    if (!fpm.fingerprint_loaded) {
        if (type) *type = FP_TAMPER_STRUCT_CORRUPT;
        return -1;
    }
    if (fpm.fingerprint.magic != FP_MAGIC) {
        if (type) *type = FP_TAMPER_MAGIC_BROKEN;
        return -1;
    }
    if (strlen(fpm.fingerprint.name) == 0) {
        if (type) *type = FP_TAMPER_NAME_CHANGED;
        return -1;
    }
    if (strlen(fpm.fingerprint.license) == 0) {
        if (type) *type = FP_TAMPER_LICENSE_REMOVED;
        return -1;
    }
    if (strlen(fpm.fingerprint.source) == 0) {
        if (type) *type = FP_TAMPER_SOURCE_REMOVED;
        return -1;
    }
    return 0;
}

int fp_report_tamper(fp_tamper_type_t type, const char *detail)
{
    fpm.tamper_detected = 1;
    fpm.tamper_type = type;
    fpm.tamper_detections++;
    fpm.warnings_issued++;
    fp_evidence_add(type, detail);
    return 0;
}

int fp_is_tampered(void)
{
    return fpm.tamper_detected;
}

const char *fp_tamper_type_name(fp_tamper_type_t type)
{
    switch (type) {
        case FP_TAMPER_NONE: return " no 篡改 ";
        case FP_TAMPER_NAME_CHANGED: return " system name 被修改 ";
        case FP_TAMPER_LICENSE_REMOVED: return " 许可证声明被 delete ";
        case FP_TAMPER_SOURCE_REMOVED: return " 源码 addr 被 delete ";
        case FP_TAMPER_MAGIC_BROKEN: return " fingerprint magic 被破 bad ";
        case FP_TAMPER_CHECKSUM_FAIL: return " verify & failed ";
        case FP_TAMPER_SIGNATURE_FAIL: return " sig verify failed ";
        case FP_TAMPER_STRUCT_CORRUPT: return " fingerprint struct 损 bad ";
        default: return " unknown 篡改 ";
    }
}

/* 来源标注 */
int fp_label_register(fp_label_location_t location, const char *text, int required)
{
    for (int i = 0; i < FP_MAX_LABELS; i++) {
        if (!fpm.labels[i].used) {
            memset(&fpm.labels[i], 0, sizeof(fp_source_label_t));
            fpm.labels[i].used = 1;
            fpm.labels[i].location = location;
            if (text) strncpy(fpm.labels[i].text, text, 127);
            fpm.labels[i].required = required;
            fpm.labels[i].present = 0;
            fpm.label_count++;
            return i;
        }
    }
    return -1;
}

int fp_label_check_all(fp_verify_result_t *result)
{
    if (!result) return -1;
    int required = 0, present = 0, missing = 0;
    for (int i = 0; i < fpm.label_count; i++) {
        if (fpm.labels[i].required) {
            required++;
            if (fpm.labels[i].present) present++;
            else missing++;
        }
    }
    result->labels_required = required;
    result->labels_present = present;
    result->labels_missing = missing;
    return missing;
}

int fp_label_get(fp_label_location_t location, fp_source_label_t *label)
{
    if (!label) return -1;
    for (int i = 0; i < fpm.label_count; i++) {
        if (fpm.labels[i].used && fpm.labels[i].location == location) {
            memcpy(label, &fpm.labels[i], sizeof(fp_source_label_t));
            return i;
        }
    }
    return -1;
}

int fp_label_list(fp_source_label_t *labels, int max)
{
    if (!labels || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < FP_MAX_LABELS && count < max; i++) {
        if (fpm.labels[i].used) memcpy(&labels[count++], &fpm.labels[i], sizeof(fp_source_label_t));
    }
    return count;
}

int fp_label_mark_present(fp_label_location_t location)
{
    for (int i = 0; i < fpm.label_count; i++) {
        if (fpm.labels[i].used && fpm.labels[i].location == location) {
            fpm.labels[i].present = 1;
            return 0;
        }
    }
    return -1;
}

/* 合规取证 */
int fp_evidence_add(fp_tamper_type_t type, const char *detail)
{
    for (int i = 0; i < FP_MAX_EVIDENCE; i++) {
        if (!fpm.evidence[i].used) {
            memset(&fpm.evidence[i], 0, sizeof(fp_evidence_t));
            fpm.evidence[i].used = 1;
            fpm.evidence[i].type = type;
            fpm.evidence[i].timestamp = 1;
            if (detail) strncpy(fpm.evidence[i].detail, detail, 255);
            fpm.evidence_count++;
            return i;
        }
    }
    return -1;
}

int fp_evidence_get(int index, fp_evidence_t *evidence)
{
    if (index < 0 || index >= FP_MAX_EVIDENCE || !fpm.evidence[index].used || !evidence) return -1;
    memcpy(evidence, &fpm.evidence[index], sizeof(fp_evidence_t));
    return 0;
}

int fp_evidence_list(fp_evidence_t *evidence, int max)
{
    if (!evidence || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < FP_MAX_EVIDENCE && count < max; i++) {
        if (fpm.evidence[i].used) memcpy(&evidence[count++], &fpm.evidence[i], sizeof(fp_evidence_t));
    }
    return count;
}

int fp_evidence_count(void)
{
    return fpm.evidence_count;
}

/* 启动自检 */
int fp_boot_self_test(void)
{
    fp_verify_result_t result;
    int ret = fp_verify(&result);
    fpm.boot_verified = (ret == 0) ? 1 : 0;

    if (ret != 0) {
        vga_puts("[FP] warn: kernel fingerprint self-test failed !\n");
        vga_printf(" reason: %s\n", result.reason ? result.reason : " unknown ");
        vga_puts(" system may be tampered, check kernel source \n");
    } else {
        vga_puts("[FP] kernel fingerprint self-test ok \n");
        /* 标记内核日志标注为已呈现 */
        fp_label_mark_present(FP_LABEL_KERNEL_LOG);
    }
    return ret;
}

int fp_get_boot_warning(char *buf, int size)
{
    if (!buf || size <= 0) return -1;
    if (!fpm.tamper_detected) {
        strncpy(buf, " kernel fingerprint 正常, system 来源可 verify ", size - 1);
        return 0;
    }
    const char *type_name = fp_tamper_type_name(fpm.tamper_type);
    int pos = 0;
    const char *prefix = "WARNING: kernel tampering detected(";
    while (*prefix && pos < size - 1) buf[pos++] = *prefix++;
    while (*type_name && pos < size - 1) buf[pos++] = *type_name++;
    const char *suffix = "), system source untrusted";
    while (*suffix && pos < size - 1) buf[pos++] = *suffix++;
    buf[pos] = 0;
    return -1;
}

/* 统计 */
int fp_get_stats(fp_manager_t *stats)
{
    if (!stats) return -1;
    memcpy(stats, &fpm, sizeof(fp_manager_t));
    return 0;
}
