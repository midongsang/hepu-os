/* ============================================================================
 * 赫普OS - 设备三级支持体系
 * ========================================================================== */

#include <stdint.h>
#include "device_support.h"
#include "vga.h"
#include "string.h"

static device_support_t ds;

/* 硬件项名称表 */
static const char *hw_item_names[] = {
    "CPU", "GPU", " display 屏 ", " 触摸屏 ", " storage ", " memory ",
    "WiFi", " BT ", " move 通信 ", " 通话 ", " SMS ",
    " prev 置 camera ", " next 置 camera ", " fingerprint ", " audio ", " 麦克风 ",
    " 扬声器 ", " 听筒 ", "GPS", " sensors ", "USB",
    " battery ", " 充电 ", "NFC", "LED", " 震动马达 "
};

/* 完整支持必需项 */
static const int required_for_full[] = {
    HW_ITEM_CPU, HW_ITEM_DISPLAY, HW_ITEM_TOUCH, HW_ITEM_RAM,
    HW_ITEM_STORAGE, HW_ITEM_CELLULAR, HW_ITEM_CALL, HW_ITEM_SMS,
    HW_ITEM_CAMERA_BACK, HW_ITEM_FINGERPRINT, HW_ITEM_AUDIO,
    HW_ITEM_BATTERY, HW_ITEM_CHARGING, -1
};

void device_support_init(void)
{
    memset(&ds, 0, sizeof(device_support_t));
    ds.initialized = 1;
    ds.enabled = 1;

    /* 初始化所有硬件项为未测试 */
    for (int i = 0; i < DS_MAX_HW_ITEMS; i++) {
        ds.current_report.items[i].used = 1;
        ds.current_report.items[i].item = (hw_item_t)i;
        if (i < HW_ITEM_BUTT) {
            strncpy(ds.current_report.items[i].name, hw_item_names[i], 63);
        }
        ds.current_report.items[i].status = HW_UNTESTED;
        ds.current_report.items[i].level = SUPPORT_UNKNOWN;
        ds.current_report.items[i].required = 0;
    }
    /* 标记必需项 */
    for (int i = 0; required_for_full[i] >= 0; i++) {
        if (required_for_full[i] < DS_MAX_HW_ITEMS)
            ds.current_report.items[required_for_full[i]].required = 1;
    }
    ds.current_report.item_count = HW_ITEM_BUTT;

    vga_puts("[DS] device 3-tier support init done \n");
}

/* 设备检测 */
int device_support_detect(void)
{
    /* 模拟检测: x86通用平台大部分硬件不存在或未测试 */
    device_support_set_item_status(HW_ITEM_CPU, HW_WORKING, "x86 CPU 正常 ");
    device_support_set_item_status(HW_ITEM_RAM, HW_WORKING, " memory detect 正常 ");
    device_support_set_item_status(HW_ITEM_STORAGE, HW_WORKING, " storage device 正常 ");
    device_support_set_item_status(HW_ITEM_DISPLAY, HW_WORKING, "VGA display 正常 ");
    device_support_set_item_status(HW_ITEM_KEYBOARD, HW_WORKING, "PS/2 keyboard 正常 ");
    device_support_set_item_status(HW_ITEM_TOUCH, HW_NOT_PRESENT, " no 触摸屏 ");
    device_support_set_item_status(HW_ITEM_CELLULAR, HW_NOT_PRESENT, " no 基带 ");
    device_support_set_item_status(HW_ITEM_CALL, HW_NOT_PRESENT, " no 通话功能 ");
    device_support_set_item_status(HW_ITEM_SMS, HW_NOT_PRESENT, " no SMS 功能 ");
    device_support_set_item_status(HW_ITEM_CAMERA_BACK, HW_NOT_PRESENT, " no camera ");
    device_support_set_item_status(HW_ITEM_FINGERPRINT, HW_NOT_PRESENT, " no fingerprint ");
    device_support_set_item_status(HW_ITEM_BATTERY, HW_NOT_PRESENT, " no battery(台式机)");

    ds.current_report.ram_size_mb = 1024;
    ds.current_report.storage_size_gb = 16;
    strncpy(ds.current_report.device_model, "HEPU-Generic-x86", 63);
    strncpy(ds.current_report.device_brand, "HEPU", 63);
    strncpy(ds.current_report.chipset, "generic-x86", 63);

    ds.total_detections++;
    return 0;
}

int device_support_detect_item(hw_item_t item)
{
    if (item >= DS_MAX_HW_ITEMS) return -1;
    /* 简化: 标记为已检测 */
    if (ds.current_report.items[item].status == HW_UNTESTED)
        ds.current_report.items[item].status = HW_NOT_PRESENT;
    return 0;
}

int device_support_set_item_status(hw_item_t item, hw_status_t status, const char *desc)
{
    if (item >= DS_MAX_HW_ITEMS) return -1;
    ds.current_report.items[item].status = status;
    if (desc) strncpy(ds.current_report.items[item].description, desc, 127);
    /* 根据状态设置等级影响 */
    switch (status) {
        case HW_WORKING: ds.current_report.items[item].level = SUPPORT_FULL; break;
        case HW_PARTIAL: ds.current_report.items[item].level = SUPPORT_PARTIAL; break;
        case HW_BROKEN:
        case HW_NOT_PRESENT:
            ds.current_report.items[item].level = ds.current_report.items[item].required ?
                SUPPORT_PARTIAL : SUPPORT_FULL;
            break;
        default: ds.current_report.items[item].level = SUPPORT_UNKNOWN;
    }
    return 0;
}

/* 报告生成 */
int device_support_generate_report(device_support_report_t *report)
{
    if (!report) return -1;

    /* 统计 */
    int full = 0, partial = 0, broken = 0, not_present = 0;
    for (int i = 0; i < ds.current_report.item_count; i++) {
        switch (ds.current_report.items[i].status) {
            case HW_WORKING: full++; break;
            case HW_PARTIAL: partial++; break;
            case HW_BROKEN: broken++; break;
            case HW_NOT_PRESENT: not_present++; break;
            default: break;
        }
    }
    ds.current_report.full_count = full;
    ds.current_report.partial_count = partial;
    ds.current_report.broken_count = broken;
    ds.current_report.not_present_count = not_present;

    /* 计算整体等级 */
    ds.current_report.overall_level = device_support_calculate_level(&ds.current_report);

    /* 生成建议 */
    switch (ds.current_report.overall_level) {
        case SUPPORT_FULL:
            strncpy(ds.current_report.recommendation,
                    " device 完整 supports Hepu OS, all cores 功能正常, 推荐刷入 ", 255);
            ds.full_support_count++;
            break;
        case SUPPORT_PARTIAL:
            strncpy(ds.current_report.recommendation,
                    " device has 限 supports, 部分功能 not 可用, 适合作 WiFi 备用机或开发 test ", 255);
            ds.partial_support_count++;
            break;
        case SUPPORT_UNSUPPORTED:
            strncpy(ds.current_report.recommendation,
                    " device not supports Hepu OS, HW config 过 low 或关键 driver 缺失, 禁止刷入 ", 255);
            ds.unsupported_count++;
            break;
        default:
            strncpy(ds.current_report.recommendation, " device supports state unknown ", 255);
    }

    ds.current_report.valid = 1;
    ds.report_generated = 1;
    ds.current_report.detect_time = 1;

    memcpy(report, &ds.current_report, sizeof(device_support_report_t));
    return 0;
}

int device_support_get_report(device_support_report_t *report)
{
    if (!report || !ds.report_generated) return -1;
    memcpy(report, &ds.current_report, sizeof(device_support_report_t));
    return 0;
}

int device_support_get_level(support_level_t *level)
{
    if (!level) return -1;
    *level = ds.current_report.overall_level;
    return 0;
}

/* 等级判断 */
support_level_t device_support_calculate_level(const device_support_report_t *report)
{
    if (!report) return SUPPORT_UNKNOWN;

    /* 检查内存最低要求: 至少512MB */
    if (report->ram_size_mb < 512) return SUPPORT_UNSUPPORTED;

    /* 检查必需项 */
    int required_broken = 0;
    int required_partial = 0;
    for (int i = 0; i < report->item_count; i++) {
        if (report->items[i].required) {
            if (report->items[i].status == HW_BROKEN) required_broken++;
            else if (report->items[i].status == HW_PARTIAL) required_partial++;
        }
    }

    if (required_broken > 0) return SUPPORT_PARTIAL;
    if (required_partial > 0) return SUPPORT_PARTIAL;
    if (report->broken_count > 0) return SUPPORT_PARTIAL;

    return SUPPORT_FULL;
}

const char *device_support_level_name(support_level_t level)
{
    switch (level) {
        case SUPPORT_FULL: return " 完整 supports ";
        case SUPPORT_PARTIAL: return " has 限 supports ";
        case SUPPORT_UNSUPPORTED: return " not supports ";
        default: return " unknown ";
    }
}

const char *device_support_level_description(support_level_t level)
{
    switch (level) {
        case SUPPORT_FULL: return " 通话 / camera / fingerprint / move 通信全部正常 ";
        case SUPPORT_PARTIAL: return " 部分 HW 失效, 适合 WiFi 备用机 ";
        case SUPPORT_UNSUPPORTED: return " HW config 过 low, 禁止刷入 ";
        default: return " not detect ";
    }
}

/* 硬件项信息 */
int device_support_get_item(hw_item_t item, hw_support_item_t *out)
{
    if (item >= DS_MAX_HW_ITEMS || !out) return -1;
    memcpy(out, &ds.current_report.items[item], sizeof(hw_support_item_t));
    return 0;
}

int device_support_list_items(hw_support_item_t *items, int max)
{
    if (!items || max <= 0) return 0;
    int count = ds.current_report.item_count < max ? ds.current_report.item_count : max;
    for (int i = 0; i < count; i++)
        memcpy(&items[i], &ds.current_report.items[i], sizeof(hw_support_item_t));
    return count;
}

const char *device_support_item_name(hw_item_t item)
{
    if (item < HW_ITEM_BUTT) return hw_item_names[item];
    return " unknown ";
}

/* 刷机准入 */
int device_support_check_flash_allowed(void)
{
    if (!ds.report_generated) return -1;
    return ds.current_report.overall_level != SUPPORT_UNSUPPORTED;
}

int device_support_get_warning(char *buf, int size)
{
    if (!buf || size <= 0) return -1;
    if (!ds.report_generated) {
        strncpy(buf, " not gen device supports report, 请先 exec detect ", size - 1);
        return -1;
    }
    switch (ds.current_report.overall_level) {
        case SUPPORT_FULL:
            strncpy(buf, " device 完整 supports, 可 security 刷入 ", size - 1);
            break;
        case SUPPORT_PARTIAL:
            strncpy(buf, " warn: device has 限 supports, 部分功能 not 可用, 刷入 next 可能影响日常 used ", size - 1);
            break;
        case SUPPORT_UNSUPPORTED:
            strncpy(buf, " error: device not supports Hepu OS, 禁止刷入 ", size - 1);
            break;
        default:
            strncpy(buf, " device supports state unknown ", size - 1);
    }
    return 0;
}

/* 统计 */
int device_support_get_stats(device_support_t *stats)
{
    if (!stats) return -1;
    memcpy(stats, &ds, sizeof(device_support_t));
    return 0;
}
