/* ============================================================================
 * 赫普OS - 固件层机型/电池声明探测
 *
 * 只回答两个问题：
 *   1. 固件把机器声明成什么形状（SMBIOS Type 3，提示用）；
 *   2. ACPI 表里有没有电池设备声明（PNP0C0A，主判据）。
 *
 * 本模块不读 _BST/_BIF，也不执行 AML；电池数值由后续 acpi_battery 模块负责。
 * ========================================================================== */

#ifndef FIRMWARE_INFO_H
#define FIRMWARE_INFO_H

#include <stdint.h>

typedef enum {
    FI_FORM_UNKNOWN = 0,
    FI_FORM_DESKTOP,
    FI_FORM_LAPTOP,
    FI_FORM_TABLET,
    FI_FORM_OTHER,
} fi_form_factor_t;

typedef enum {
    FI_BAT_UNKNOWN = 0,   /* ACPI 表都没拿到，不能断言没有电池 */
    FI_BAT_ABSENT,        /* ACPI 可用，且未发现 PNP0C0A */
    FI_BAT_DECLARED,      /* ACPI 表里声明了 PNP0C0A（未验证 _STA） */
} fi_battery_presence_t;

typedef struct {
    int smbios_found;
    int smbios_type3;
    uint8_t chassis_type;
    fi_form_factor_t form_factor;
    int acpi_ok;
    int fadt_found;
    int dsdt_found;
    int ssdt_count;
    int battery_declared;   /* DSDT/SSDT 原始字节中出现 PNP0C0A */
    int ac_declared;        /* 出现 ACPI0003 */
    fi_battery_presence_t battery;
} fi_info_t;

void fi_init(void);
const fi_info_t *fi_get(void);

/* M2: 已校验的 ACPI 表（DSDT 在前，SSDT 随后），供 AML 命名空间使用。
 * 指针指向物理映射区，生命周期到下次复位；不拷贝表内容。 */
int fi_table_count(void);
int fi_table_at(int i, const uint8_t **data, uint32_t *len);

const char *fi_form_name(fi_form_factor_t f);
const char *fi_battery_name(fi_battery_presence_t b);

#endif
