/* ============================================================================
 * 赫普OS - ACPI支持 (V1.02新增)
 * 实现：ACPI表定位与解析 (RSDP/RSDT/XSDT/FADT/MADT)
 * 基于ACPI规范：RSD PTR签名扫描、SDT表头校验、表遍历
 * ========================================================================== */
#include <stdint.h>
#include <stddef.h>
#include "acpi.h"
#include "vga.h"
#include "string.h"

#define ACPI_RSDP_SIG "RSD PTR "
#define ACPI_RSDT_SIG "RSDT"
#define ACPI_XSDT_SIG "XSDT"
#define ACPI_FADT_SIG "FACP"
#define ACPI_MADT_SIG "APIC"
#define ACPI_DSDT_SIG "DSDT"
#define ACPI_SSDT_SIG "SSDT"

/* 内存探测范围 */
#define ACPI_BIOS_AREA_START 0x000E0000
#define ACPI_BIOS_AREA_END   0x000FFFFF
#define ACPI_EBDA_ALIGN      0x0400

static acpi_sdt_t rsdt;
static acpi_sdt_t xsdt;
static acpi_sdt_t fadt;
static acpi_sdt_t madt;
static acpi_sdt_t dsdt;
static uint8_t acpi_initialized = 0;
static uint8_t acpi_has_xsdt = 0;
static uint32_t acpi_table_count = 0;
static char acpi_oem_id[7];

/* 校验和：所有字节和为0则正确 */
static uint8_t checksum_valid(const void *addr, size_t len)
{
    const uint8_t *p = (const uint8_t *)addr;
    uint8_t sum = 0;
    for (size_t i = 0; i < len; i++) sum += p[i];
    return sum == 0;
}

/* 比较签名 */
static int sig_match(const char *actual, const char *expect)
{
    for (int i = 0; i < 4; i++) {
        if (actual[i] != expect[i]) return 0;
    }
    return 1;
}

/* 从物理地址读取SDT表头并校验 */
static int read_sdt_header(uint32_t phys, acpi_sdt_t *out)
{
    if (!out) return -1;
    acpi_sdt_header_t *hdr = (acpi_sdt_header_t *)phys;
    if (!checksum_valid(hdr, hdr->length)) return -2;
    out->phys_addr = phys;
    out->length = hdr->length;
    out->revision = hdr->revision;
    memcpy(out->signature, hdr->signature, 4);
    return 0;
}

/* 在内存区域中搜索RSDP */
static uint32_t scan_for_rsdp(uint32_t start, uint32_t end)
{
    /* RSDP按16字节对齐 */
    uint32_t addr = (start + 15) & ~15u;
    while (addr + 36 <= end) {
        const char *sig = (const char *)addr;
        if (sig[0] == 'R' && sig[1] == 'S' && sig[2] == 'D' &&
            sig[3] == ' ' && sig[4] == 'P' && sig[5] == 'T' &&
            sig[6] == 'R') {
            if (checksum_valid((void *)addr, 20)) {
                return addr;  /* ACPI 1.0 RSDP校验通过 */
            }
            if (checksum_valid((void *)addr, 36)) {
                return addr;  /* ACPI 2.0+ RSDP校验通过 */
            }
        }
        addr += 16;
    }
    return 0;
}

/* 探测RSDP：先EBDA，再BIOS区域 */
uint32_t acpi_find_rsdp(void)
{
    /* 1. 从EBDA(扩展BIOS数据区)搜索 */
    uint16_t ebda_seg = *(volatile uint16_t *)ACPI_EBDA_ALIGN;
    if (ebda_seg) {
        uint32_t ebda_addr = (uint32_t)ebda_seg << 4;
        uint32_t rsdp = scan_for_rsdp(ebda_addr, ebda_addr + 1024);
        if (rsdp) return rsdp;
    }
    /* 2. 从BIOS ROM区域搜索 */
    return scan_for_rsdp(ACPI_BIOS_AREA_START, ACPI_BIOS_AREA_END);
}

/* 解析RSDP，定位RSDT/XSDT */
int acpi_parse_rsdp(uint32_t rsdp_phys)
{
    if (!rsdp_phys) return -1;   /* 空指针保护 */
    acpi_rsdp_t *rsdp = (acpi_rsdp_t *)rsdp_phys;
    memset(&rsdt, 0, sizeof(rsdt));
    memset(&xsdt, 0, sizeof(xsdt));
    acpi_has_xsdt = 0;

    memcpy(acpi_oem_id, rsdp->oem_id, 6);
    acpi_oem_id[6] = 0;

    /* ACPI 2.0+ 有XSDT */
    if (rsdp->revision >= 2 && rsdp->xsdt_address) {
        uint32_t xsdt_phys = (uint32_t)(rsdp->xsdt_address & 0xFFFFFFFF);
        if (read_sdt_header(xsdt_phys, &xsdt) == 0) {
            acpi_has_xsdt = 1;
        }
    }
    /* RSDT总是存在（ACPI 1.0+） */
    if (rsdp->rsdt_address) {
        read_sdt_header(rsdp->rsdt_address, &rsdt);
    }
    return 0;
}

/* 从RSDT/XSDT中查找指定签名的表 */
static uint32_t find_table(const char *signature)
{
    if (!acpi_initialized) return 0;

    if (acpi_has_xsdt && xsdt.phys_addr) {
        /* XSDT：64位表指针 */
        uint64_t *tables = (uint64_t *)(xsdt.phys_addr + sizeof(acpi_sdt_header_t));
        uint32_t count = (xsdt.length - sizeof(acpi_sdt_header_t)) / 8;
        for (uint32_t i = 0; i < count; i++) {
            uint32_t phys = (uint32_t)(tables[i] & 0xFFFFFFFF);
            acpi_sdt_header_t *hdr = (acpi_sdt_header_t *)phys;
            if (sig_match(hdr->signature, signature)) return phys;
        }
    }
    if (rsdt.phys_addr) {
        /* RSDT：32位表指针 */
        uint32_t *tables = (uint32_t *)(rsdt.phys_addr + sizeof(acpi_sdt_header_t));
        uint32_t count = (rsdt.length - sizeof(acpi_sdt_header_t)) / 4;
        for (uint32_t i = 0; i < count; i++) {
            uint32_t phys = tables[i];
            acpi_sdt_header_t *hdr = (acpi_sdt_header_t *)phys;
            if (sig_match(hdr->signature, signature)) return phys;
        }
    }
    return 0;
}

/* 加载并缓存关键ACPI表 */
int acpi_init(void)
{
    uint32_t rsdp = acpi_find_rsdp();
    if (!rsdp) {
        vga_puts("[ACPI] not found RSDP,ACPI parse skipped \n");
        acpi_initialized = 1;
        return 0;
    }

    acpi_parse_rsdp(rsdp);
    acpi_initialized = 1;

    /* 统计表数量 */
    if (acpi_has_xsdt && xsdt.phys_addr) {
        acpi_table_count = (xsdt.length - sizeof(acpi_sdt_header_t)) / 8;
    } else if (rsdt.phys_addr) {
        acpi_table_count = (rsdt.length - sizeof(acpi_sdt_header_t)) / 4;
    }

    /* 定位FADT/MADT/DSDT */
    uint32_t fadt_phys = find_table(ACPI_FADT_SIG);
    if (fadt_phys) read_sdt_header(fadt_phys, &fadt);
    uint32_t madt_phys = find_table(ACPI_MADT_SIG);
    if (madt_phys) read_sdt_header(madt_phys, &madt);
    uint32_t dsdt_phys = find_table(ACPI_DSDT_SIG);
    if (dsdt_phys) read_sdt_header(dsdt_phys, &dsdt);

    vga_printf("[ACPI] init done: OEM=%s RSDT@0x%x%s table =%d FADT=%s MADT=%s DSDT=%s\n",
               acpi_oem_id,
               (unsigned)rsdt.phys_addr,
               acpi_has_xsdt ? "+XSDT" : "",
               acpi_table_count,
               fadt.phys_addr ? "yes" : "no",
               madt.phys_addr ? "yes" : "no",
               dsdt.phys_addr ? "yes" : "no");
    return 0;
}

/* 查询ACPI表 */
int acpi_get_table(uint8_t table_type, acpi_sdt_t *out)
{
    if (!acpi_initialized || !out) return -1;
    switch (table_type) {
    case ACPI_TABLE_RSDT:  if (rsdt.phys_addr)  { *out = rsdt;  return 0; } break;
    case ACPI_TABLE_XSDT:  if (xsdt.phys_addr)  { *out = xsdt;  return 0; } break;
    case ACPI_TABLE_FADT:  if (fadt.phys_addr)  { *out = fadt;  return 0; } break;
    case ACPI_TABLE_MADT:  if (madt.phys_addr)  { *out = madt;  return 0; } break;
    case ACPI_TABLE_DSDT:  if (dsdt.phys_addr)  { *out = dsdt;  return 0; } break;
    default: break;
    }
    return -1;
}

/* 是否找到指定表 */
int acpi_has_table(uint8_t table_type)
{
    acpi_sdt_t t;
    return acpi_get_table(table_type, &t) == 0;
}

/* FADT字段读取 */
int acpi_fadt_get(acpi_fadt_field_t field, uint32_t *value)
{
    if (!acpi_initialized || !value || !fadt.phys_addr) return -1;
    acpi_fadt_t *f = (acpi_fadt_t *)fadt.phys_addr;
    switch (field) {
    case ACPI_FADT_SMI_CMD:      *value = f->smi_cmd;      return 0;
    case ACPI_FADT_PM1A_EVT:     *value = f->pm1a_evt;     return 0;
    case ACPI_FADT_PM1B_EVT:     *value = f->pm1b_evt;     return 0;
    case ACPI_FADT_PM1A_CNT:     *value = f->pm1a_cnt;     return 0;
    case ACPI_FADT_PM1B_CNT:     *value = f->pm1b_cnt;     return 0;
    case ACPI_FADT_PM2_CNT:      *value = f->pm2_cnt;      return 0;
    case ACPI_FADT_PM_TMR:       *value = f->pm_tmr;       return 0;
    case ACPI_FADT_RESET_REG: {
        /* reset_reg是GAS结构体(12字节)，address为64位(偏移4) */
        uint64_t gas_addr;
        memcpy(&gas_addr, &f->reset_reg[4], 8);
        *value = (uint32_t)(gas_addr & 0xFFFFFFFF);
        return 0;
    }
    case ACPI_FADT_FLAGS:        *value = f->flags;        return 0;
    default: break;
    }
    return -1;
}

/* MADT表信息：本地APIC地址与IO APIC数量 */
int acpi_madt_info(uint32_t *local_apic_addr, uint32_t *io_apic_count,
                   uint32_t *lapic_count)
{
    if (!acpi_initialized || !madt.phys_addr) return -1;
    acpi_madt_t *m = (acpi_madt_t *)madt.phys_addr;
    if (local_apic_addr) *local_apic_addr = m->local_apic_addr;

    uint32_t n_io = 0, n_lapic = 0;
    uint8_t *p = (uint8_t *)(madt.phys_addr + sizeof(acpi_madt_t));
    uint8_t *end = (uint8_t *)(madt.phys_addr + madt.length);
    while (p + 2 <= end) {
        uint8_t type = p[0];
        uint8_t len = p[1];
        if (len < 2) break;
        if (type == ACPI_MADT_IO_APIC) n_io++;
        else if (type == ACPI_MADT_LAPIC) n_lapic++;
        p += len;
    }
    if (io_apic_count) *io_apic_count = n_io;
    if (lapic_count) *lapic_count = n_lapic;
    return 0;
}

/* ACPI统计 */
void acpi_stats(uint32_t *tables, uint8_t *has_xsdt, uint8_t *initialized)
{
    if (tables) *tables = acpi_table_count;
    if (has_xsdt) *has_xsdt = acpi_has_xsdt;
    if (initialized) *initialized = acpi_initialized;
}

/* ============================================================================
 * V2.21: 解析 DSDT 中 _S5 包, 获取精确 SLP_TYP(PM1a/PM1b), 用于精确关机
 * 原理: AML 中 "Name (_S5, Package (...) { SLPTYP_A, SLPTYP_B, ... })"
 *       扫描 "_S5_" 名称(0x5F 0x53 0x35 0x5F)后的 Package 对象(0x12),
 *       解析前两个数值元素(Byte/Word/DWord).
 * 注意: 若 _S5 是 Method 形式(需执行AML), 本函数不支持, 返回-1,
 *       由调用方回退到默认 SLP_TYP.
 * ========================================================================== */
int acpi_s5_slp_typ(uint8_t *slp_typ_a, uint8_t *slp_typ_b)
{
    if (!acpi_initialized || !dsdt.phys_addr || !slp_typ_a || !slp_typ_b)
        return -1;
    uint8_t *d = (uint8_t *)dsdt.phys_addr;
    uint32_t len = dsdt.length;
    for (uint32_t i = 4; i + 16 < len; i++) {
        /* "_S5_" NameString */
        if (d[i] == 0x5F && d[i+1] == 0x53 && d[i+2] == 0x35 && d[i+3] == 0x5F) {
            uint32_t j = i + 4;
            if (j >= len || d[j] != 0x12) continue;   /* 非 Package, 跳过 */
            uint32_t k = j + 1;
            /* PkgLength (ACPI 可变长编码) */
            uint8_t b0 = d[k];
            uint32_t pkglen;
            if ((b0 & 0xC0) == 0x40) {                /* 2字节 */
                pkglen = (b0 & 0x0F) | (d[k+1] << 4); k += 2;
            } else if ((b0 & 0xC0) == 0x80) {         /* 3字节 */
                pkglen = (b0 & 0x0F) | (d[k+1] << 4) | (d[k+2] << 12); k += 3;
            } else if ((b0 & 0xC0) == 0xC0) {         /* 4字节 */
                pkglen = (b0 & 0x0F) | (d[k+1] << 4) | (d[k+2] << 12)
                         | (d[k+3] << 20); k += 4;
            } else {                                  /* 1字节 */
                pkglen = b0; k += 1;
            }
            (void)pkglen;
            if (k >= len) return -1;
            uint8_t num = d[k]; k++;                  /* NumElements */
            int got = 0;
            for (int e = 0; e < num && e < 4 && k + 2 < len && got < 2; e++) {
                uint8_t op = d[k];
                if (op == 0x0A) {                     /* BytePrefix */
                    if (got == 0) *slp_typ_a = d[k+1];
                    else          *slp_typ_b = d[k+1];
                    got++; k += 2;
                } else if (op == 0x0B) {              /* WordPrefix */
                    uint8_t v = d[k+1];
                    if (got == 0) *slp_typ_a = v;
                    else          *slp_typ_b = v;
                    got++; k += 3;
                } else if (op == 0x0C) {              /* DWordPrefix */
                    uint8_t v = d[k+1];
                    if (got == 0) *slp_typ_a = v;
                    else          *slp_typ_b = v;
                    got++; k += 5;
                } else if (op == 0x0D) {              /* StringPrefix: 跳过 */
                    uint8_t sl = d[k+1];
                    k += 2 + sl;
                } else {
                    return -1;                        /* 不支持元素类型 */
                }
            }
            if (got >= 1) return 0;
            return -1;
        }
    }
    return -1;
}
