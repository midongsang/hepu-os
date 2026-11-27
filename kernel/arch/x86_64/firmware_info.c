/* ============================================================================
 * 赫普OS - 固件层机型/电池声明探测
 *
 * 设计约束（见 docs/planning/BATTERY_STATUS_PLAN_2026-11-27.md）：
 *   - SMBIOS Type 3 只做外形提示，不决定电池图标；
 *   - 电池存在主判据是 ACPI 表里的 PNP0C0A 声明；
 *   - 本模块不执行 AML，不读 _STA/_BIF/_BST，因此最多到 DECLARED；
 *   - 找不到 ACPI 表时给 UNKNOWN，不伪造“无电池”。
 * ========================================================================== */

#include "firmware_info.h"
#include "acpi.h"

extern void vga_puts(const char *str);
extern void vga_put_hex(uint64_t val);
extern void *memset(void *dst, int c, unsigned long n);
extern uint64_t vmm64_phys_to_ptr(uint64_t phys);

#define SMBIOS_SCAN_START 0x000E0000u  /* 规范主区在 F0000-FFFFF；部分固件落在 E0000 */
#define SMBIOS_SCAN_END   0x00100000u
#define FI_MAX_SSDT       16

static fi_info_t fi;
static int fi_ready;

static uint8_t fi_checksum(const void *ptr, uint32_t len)
{
    const uint8_t *p = (const uint8_t *)ptr;
    uint8_t sum = 0;
    for (uint32_t i = 0; i < len; i++) sum += p[i];
    return sum;
}

static int fi_memcmp(const void *a, const void *b, uint32_t n)
{
    const uint8_t *x = (const uint8_t *)a;
    const uint8_t *y = (const uint8_t *)b;
    for (uint32_t i = 0; i < n; i++) {
        if (x[i] != y[i]) return (int)x[i] - (int)y[i];
    }
    return 0;
}

static int fi_has_eisa(const uint8_t *blob, uint32_t len, uint32_t code)
{
    /* AML 里 EisaId("PNP0C0A") 会被压成 DWORD 0x41D00C0A（小端 0A 0C D0 41），
     * 不一定以 ASCII 字符串形式出现。两种都认，避免真机 DSDT 漏判。 */
    if (!blob || len < 4) return 0;
    for (uint32_t i = 0; i + 4 <= len; i++) {
        uint32_t v = (uint32_t)blob[i] |
                     ((uint32_t)blob[i + 1] << 8) |
                     ((uint32_t)blob[i + 2] << 16) |
                     ((uint32_t)blob[i + 3] << 24);
        if (v == code) return 1;
    }
    return 0;
}

static int fi_has_id(const uint8_t *blob, uint32_t len, const char *id)
{
    if (!blob || len < 8) return 0;
    uint32_t idlen = 0;
    while (id[idlen]) idlen++;
    if (idlen == 0 || idlen > len) return 0;
    for (uint32_t i = 0; i + idlen <= len; i++) {
        if (blob[i] == (uint8_t)id[0] &&
            fi_memcmp(blob + i, id, idlen) == 0) {
            return 1;
        }
    }
    if (fi_memcmp(id, "PNP0C0A", 8) == 0 && fi_has_eisa(blob, len, 0x41D00C0Au))
        return 1;
    if (fi_memcmp(id, "ACPI0003", 9) == 0 && fi_has_eisa(blob, len, 0x04700003u))
        return 1;
    return 0;
}

static fi_form_factor_t fi_chassis_to_form(uint8_t chassis)
{
    uint8_t type = chassis & 0x7Fu;
    switch (type) {
        case 3:  /* Desktop */
        case 4:  /* Low Profile Desktop */
        case 5:  /* Pizza Box */
        case 6:  /* Mini Tower */
        case 7:  /* Tower */
        case 15: /* Space-saving */
        case 16: /* Lunch Box */
        case 35: /* Mini PC */
            return FI_FORM_DESKTOP;
        case 8:  /* Portable */
        case 9:  /* Laptop */
        case 10: /* Notebook */
        case 14: /* Sub Notebook */
        case 31: /* Convertible */
        case 32: /* Detachable */
            return FI_FORM_LAPTOP;
        case 30: /* Tablet */
            return FI_FORM_TABLET;
        default:
            return FI_FORM_OTHER;
    }
}

/* 走 SMBIOS structure table，只取 Type 3 的 Chassis Type。 */
static void fi_smbios_walk(uint8_t *table, uint32_t len)
{
    uint8_t *p = table;
    uint8_t *end = table + len;

    while (p + 4 <= end) {
        uint8_t type = p[0];
        uint8_t slen = p[1];
        if (slen < 4 || p + slen > end) break;

        if (type == 3 && slen >= 7) {
            /* SMBIOS Type 3：0x06 才是 Chassis Type；0x02..0x05 是
             * Manufacturer/Version/Serial/Asset 四个字符串索引。 */
            fi.smbios_type3 = 1;
            fi.chassis_type = p[6];
            fi.form_factor = fi_chassis_to_form(p[6]);
        }
        if (type == 127) break;  /* End-of-table */

        /* 跳过格式化区 + 字符串区（以双 NUL 结尾） */
        uint8_t *q = p + slen;
        while (q + 1 < end && !(q[0] == 0 && q[1] == 0)) q++;
        if (q + 1 >= end) break;
        p = q + 2;
    }
}

static void fi_smbios_probe(void)
{
    for (uint32_t addr = SMBIOS_SCAN_START; addr < SMBIOS_SCAN_END; addr += 16) {
        uint8_t *p = (uint8_t *)(uintptr_t)addr;

        if (fi_memcmp(p, "_SM3_", 5) == 0 && p[0x06] >= 0x18 &&
            fi_checksum(p, p[0x06]) == 0) {
            uint64_t table = 0;
            for (int i = 0; i < 8; i++) table |= (uint64_t)p[0x10 + i] << (i * 8);
            uint32_t table_len = 0;
            for (int i = 0; i < 4; i++) table_len |= (uint32_t)p[0x0C + i] << (i * 8);
            if (table_len > 0x10000u) table_len = 0x10000u;  /* 表里没有真实长度，只做安全上界 */
            if (table && table_len) {
                fi.smbios_found = 1;
                fi_smbios_walk((uint8_t *)vmm64_phys_to_ptr(table), table_len);
                return;
            }
        }

        if (fi_memcmp(p, "_SM_", 4) == 0 && p[0x05] >= 0x1F &&
            fi_checksum(p, p[0x05]) == 0) {
            uint32_t table = 0;
            for (int i = 0; i < 4; i++) table |= (uint32_t)p[0x18 + i] << (i * 8);
            uint16_t table_len = (uint16_t)(p[0x16] | (p[0x17] << 8));
            if (table && table_len) {
                fi.smbios_found = 1;
                fi_smbios_walk((uint8_t *)vmm64_phys_to_ptr(table), table_len);
                return;
            }
        }
    }
}

/* 读 FADT 的 DSDT / X_DSDT。返回 0 表示成功。 */
static int fi_fadt_dsdt(const acpi_header_t *fadt, uint64_t *out)
{
    if (!fadt || fadt->length < 0x2C) return -1;
    uint32_t dsdt32 = 0;
    for (int i = 0; i < 4; i++) dsdt32 |= (uint32_t)((const uint8_t *)fadt)[0x28 + i] << (i * 8);
    uint64_t dsdt64 = 0;
    if (fadt->length >= 0x94) {
        for (int i = 0; i < 8; i++) dsdt64 |= (uint64_t)((const uint8_t *)fadt)[0x8C + i] << (i * 8);
    }
    *out = dsdt64 ? dsdt64 : (uint64_t)dsdt32;
    return *out ? 0 : -1;
}

static int fi_scan_dsdt(uint64_t phys)
{
    if (!phys) return 0;
    acpi_header_t *h = (acpi_header_t *)vmm64_phys_to_ptr(phys);
    if (!h || h->length < sizeof(acpi_header_t)) return 0;
    if (fi_checksum(h, h->length) != 0) return 0;
    if (fi_memcmp(h->signature, "DSDT", 4) != 0) return 0;
    fi.dsdt_found = 1;
    if (fi_has_id((const uint8_t *)h, h->length, "PNP0C0A")) fi.battery_declared = 1;
    if (fi_has_id((const uint8_t *)h, h->length, "ACPI0003")) fi.ac_declared = 1;
    return 1;
}

static void fi_scan_ssdt(uint64_t phys)
{
    if (!phys) return;
    acpi_header_t *h = (acpi_header_t *)vmm64_phys_to_ptr(phys);
    if (!h || h->length < sizeof(acpi_header_t)) return;
    if (fi_checksum(h, h->length) != 0) return;
    if (fi_memcmp(h->signature, "SSDT", 4) != 0) return;
    if (fi.ssdt_count >= FI_MAX_SSDT) return;
    fi.ssdt_count++;
    if (fi_has_id((const uint8_t *)h, h->length, "PNP0C0A")) fi.battery_declared = 1;
    if (fi_has_id((const uint8_t *)h, h->length, "ACPI0003")) fi.ac_declared = 1;
}

static void fi_acpi_probe(void)
{
    acpi_rsdp_t *rsdp = acpi_find_rsdp();
    if (!rsdp) {
        fi.battery = FI_BAT_UNKNOWN;
        return;
    }
    fi.acpi_ok = 1;

    uint64_t fadt_phys = 0;
    int have_dsdt_scan = 0;

    if (rsdp->rsdt_address) {
        acpi_rsdt_t *rsdt = (acpi_rsdt_t *)vmm64_phys_to_ptr(rsdp->rsdt_address);
        if (rsdt && rsdt->header.length >= sizeof(acpi_header_t) &&
            fi_checksum(&rsdt->header, rsdt->header.length) == 0) {
            uint32_t n = (rsdt->header.length - sizeof(acpi_header_t)) / 4;
            for (uint32_t i = 0; i < n; i++) {
                acpi_header_t *h = (acpi_header_t *)vmm64_phys_to_ptr(rsdt->entry[i]);
                if (!h) continue;
                if (fi_memcmp(h->signature, "FACP", 4) == 0 && !fadt_phys) {
                    fadt_phys = rsdt->entry[i];
                }
                if (fi_memcmp(h->signature, "SSDT", 4) == 0) fi_scan_ssdt(rsdt->entry[i]);
            }
        }
    }

    if (rsdp->revision >= 2 && rsdp->xsdt_address) {
        acpi_header_t *xh = (acpi_header_t *)vmm64_phys_to_ptr(rsdp->xsdt_address);
        if (xh && xh->length >= sizeof(acpi_header_t) &&
            fi_checksum(xh, xh->length) == 0) {
            uint32_t n = (xh->length - sizeof(acpi_header_t)) / 8;
            uint64_t *entries = (uint64_t *)((uint8_t *)xh + sizeof(acpi_header_t));
            for (uint32_t i = 0; i < n; i++) {
                acpi_header_t *h = (acpi_header_t *)vmm64_phys_to_ptr(entries[i]);
                if (!h) continue;
                if (fi_memcmp(h->signature, "FACP", 4) == 0 && !fadt_phys) {
                    fadt_phys = entries[i];
                }
                if (fi_memcmp(h->signature, "SSDT", 4) == 0) fi_scan_ssdt(entries[i]);
            }
        }
    }

    if (fadt_phys) {
        acpi_header_t *fadt = (acpi_header_t *)vmm64_phys_to_ptr(fadt_phys);
        if (fadt && fadt->length >= 0x2C && fi_checksum(fadt, fadt->length) == 0 &&
            fi_memcmp(fadt->signature, "FACP", 4) == 0) {
            fi.fadt_found = 1;
            uint64_t dsdt = 0;
            if (fi_fadt_dsdt(fadt, &dsdt) == 0 && fi_scan_dsdt(dsdt)) {
                have_dsdt_scan = 1;
            }
        }
    }

    if (fi.battery_declared) fi.battery = FI_BAT_DECLARED;
    else if (fi.acpi_ok && have_dsdt_scan) fi.battery = FI_BAT_ABSENT;
    else fi.battery = FI_BAT_UNKNOWN;
}

void fi_init(void)
{
    if (fi_ready) return;
    memset(&fi, 0, sizeof(fi));
    fi.form_factor = FI_FORM_UNKNOWN;
    fi.battery = FI_BAT_UNKNOWN;

    fi_smbios_probe();
    fi_acpi_probe();

    fi_ready = 1;
    vga_puts("[BAT] smbios="); vga_put_hex((uint64_t)fi.smbios_found);
    vga_puts(" chassis="); vga_put_hex((uint64_t)fi.chassis_type);
    vga_puts(" form="); vga_puts(fi_form_name(fi.form_factor));
    vga_puts("\n[BAT] acpi="); vga_put_hex((uint64_t)fi.acpi_ok);
    vga_puts(" fadt="); vga_put_hex((uint64_t)fi.fadt_found);
    vga_puts(" dsdt="); vga_put_hex((uint64_t)fi.dsdt_found);
    vga_puts(" ssdt="); vga_put_hex((uint64_t)fi.ssdt_count);
    vga_puts(" battery="); vga_puts(fi_battery_name(fi.battery));
    vga_puts("\n[BAT] state="); vga_puts(fi_battery_name(fi.battery));
    vga_puts(" form_factor="); vga_puts(fi_form_name(fi.form_factor));
    vga_puts(" acpi_battery="); vga_puts(fi_battery_name(fi.battery));
    vga_puts(" reason=");
    if (fi.battery == FI_BAT_DECLARED) vga_puts("acpi-declared");
    else if (fi.battery == FI_BAT_ABSENT) vga_puts("no-acpi-battery");
    else vga_puts("acpi-unavailable");
    vga_puts("\n");
}

const fi_info_t *fi_get(void)
{
    return &fi;
}

const char *fi_form_name(fi_form_factor_t f)
{
    switch (f) {
        case FI_FORM_DESKTOP: return "desktop";
        case FI_FORM_LAPTOP:  return "laptop";
        case FI_FORM_TABLET:  return "tablet";
        case FI_FORM_OTHER:   return "other";
        default:              return "unknown";
    }
}

const char *fi_battery_name(fi_battery_presence_t b)
{
    switch (b) {
        case FI_BAT_ABSENT:   return "absent";
        case FI_BAT_DECLARED: return "declared";
        default:              return "unknown";
    }
}
