#ifndef ACPI_H
#define ACPI_H

#include <stdint.h>
#include <stddef.h>

/* SDT表头（所有ACPI表的标准开头，与ACPI规范一致） */
typedef struct {
    char     signature[4];
    uint32_t length;
    uint8_t  revision;
    uint8_t  checksum;
    char     oem_id[6];
    char     oem_table_id[8];
    uint32_t oem_revision;
    char     creator_id[4];
    uint32_t creator_revision;
} __attribute__((packed)) acpi_sdt_header_t;

/* RSDP (Root System Description Pointer) */
typedef struct {
    char     signature[8];     /* "RSD PTR " */
    uint8_t  checksum;
    char     oem_id[6];
    uint8_t  revision;
    uint32_t rsdt_address;
    uint32_t length;
    uint64_t xsdt_address;
    uint8_t  extended_checksum;
    uint8_t  reserved[3];
} __attribute__((packed)) acpi_rsdp_t;

/* FADT (Fixed ACPI Description Table) - 关键字段 */
typedef struct {
    acpi_sdt_header_t header;
    uint32_t firmware_ctrl;
    uint32_t dsdt;
    uint8_t  reserved;
    uint8_t  preferred_pm_profile;
    uint16_t sci_int;
    uint32_t smi_cmd;
    uint8_t  acpi_enable;
    uint8_t  acpi_disable;
    uint8_t  s4bios_req;
    uint8_t  pstate_cnt;
    uint32_t pm1a_evt;
    uint32_t pm1b_evt;
    uint32_t pm1a_cnt;
    uint32_t pm1b_cnt;
    uint32_t pm2_cnt;
    uint32_t pm_tmr;
    uint32_t gpe0_blk;
    uint32_t gpe1_blk;
    uint8_t  pm1_evt_len;
    uint8_t  pm1_cnt_len;
    uint8_t  pm2_cnt_len;
    uint8_t  pm_tmr_len;
    uint8_t  gpe0_blk_len;
    uint8_t  gpe1_blk_len;
    uint8_t  gpe1_base;
    uint8_t  cst_cnt;
    uint16_t p_lvl2_lat;
    uint16_t p_lvl3_lat;
    uint16_t flush_size;
    uint16_t flush_stride;
    uint8_t  duty_offset;
    uint8_t  duty_width;
    uint8_t  day_alrm;
    uint8_t  mon_alrm;
    uint8_t  century;
    uint16_t iapc_boot_arch;
    uint8_t  reserved2;
    uint32_t flags;
    uint8_t  reset_reg[12];
    uint8_t  reset_value;
    uint8_t  reserved3[3];
} __attribute__((packed)) acpi_fadt_t;

/* MADT (Multiple APIC Description Table) */
typedef struct {
    acpi_sdt_header_t header;
    uint32_t local_apic_addr;
    uint32_t flags;
} __attribute__((packed)) acpi_madt_t;

/* MADT条目类型 */
#define ACPI_MADT_LAPIC   0
#define ACPI_MADT_IO_APIC 1
#define ACPI_MADT_IRQ_SRC 2
#define ACPI_MADT_LAPIC_NMI 4

/* 解析后的ACPI表描述 */
typedef struct {
    char     signature[4];
    uint32_t phys_addr;
    uint32_t length;
    uint8_t  revision;
} acpi_sdt_t;

/* 表类型索引 */
#define ACPI_TABLE_RSDT 0
#define ACPI_TABLE_XSDT 1
#define ACPI_TABLE_FADT 2
#define ACPI_TABLE_MADT 3
#define ACPI_TABLE_DSDT 4

/* FADT字段索引 */
typedef enum {
    ACPI_FADT_SMI_CMD = 0,
    ACPI_FADT_PM1A_EVT,
    ACPI_FADT_PM1B_EVT,
    ACPI_FADT_PM1A_CNT,
    ACPI_FADT_PM1B_CNT,
    ACPI_FADT_PM2_CNT,
    ACPI_FADT_PM_TMR,
    ACPI_FADT_RESET_REG,
    ACPI_FADT_FLAGS
} acpi_fadt_field_t;

/* 探测并解析ACPI（返回0成功） */
int acpi_init(void);
uint32_t acpi_find_rsdp(void);
int acpi_parse_rsdp(uint32_t rsdp_phys);

/* 查询 */
int acpi_get_table(uint8_t table_type, acpi_sdt_t *out);
int acpi_has_table(uint8_t table_type);
int acpi_fadt_get(acpi_fadt_field_t field, uint32_t *value);
int acpi_madt_info(uint32_t *local_apic_addr, uint32_t *io_apic_count,
                   uint32_t *lapic_count);
void acpi_stats(uint32_t *tables, uint8_t *has_xsdt, uint8_t *initialized);
/* V2.21: 解析 DSDT _S5 包, 获取 PM1a/PM1b 的 SLP_TYP(找不到返回-1) */
int acpi_s5_slp_typ(uint8_t *slp_typ_a, uint8_t *slp_typ_b);

#endif
