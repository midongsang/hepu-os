/* ============================================================================
 * 赫普OS - ACPI MADT解析（多处理器APIC表）
 * V0.10.142: 解析ACPI MADT获取APIC ID列表
 * ========================================================================== */

#ifndef ACPI_H
#define ACPI_H

#include <stdint.h>

/* ACPI RSDP结构 */
typedef struct {
    char signature[8];       /* "RSD PTR " */
    uint8_t checksum;
    char oem_id[6];
    uint8_t revision;
    uint32_t rsdt_address;
    uint32_t length;
    uint64_t xsdt_address;
    uint8_t extended_checksum;
    uint8_t reserved[3];
} __attribute__((packed)) acpi_rsdp_t;

/* ACPI表头 */
typedef struct {
    char signature[4];
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    char oem_id[6];
    char oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} __attribute__((packed)) acpi_header_t;

/* RSDT */
typedef struct {
    acpi_header_t header;
    uint32_t entry[];
} __attribute__((packed)) acpi_rsdt_t;

/* MADT */
typedef struct {
    acpi_header_t header;
    uint32_t local_apic_address;
    uint32_t flags;
    uint8_t entries[];
} __attribute__((packed)) acpi_madt_t;

/* MADT条目类型 */
#define MADT_TYPE_LAPIC      0  /* 本地APIC */
#define MADT_TYPE_IOAPIC     1  /* I/O APIC */
#define MADT_TYPE_INT_SRC    2  /* 中断源覆盖 */
#define MADT_TYPE_NMI        4  /* NMI */
#define MADT_TYPE_LAPIC_ADDR 5  /* 本地APIC地址覆盖 */

/* MADT本地APIC条目 */
typedef struct {
    uint8_t type;
    uint8_t length;
    uint8_t processor_id;
    uint8_t apic_id;
    uint32_t flags;
} __attribute__((packed)) madt_lapic_t;

/* MADT I/O APIC条目 */
typedef struct {
    uint8_t type;
    uint8_t length;
    uint8_t ioapic_id;
    uint8_t reserved;
    uint32_t address;
    uint32_t gsi_base;
} __attribute__((packed)) madt_ioapic_t;

/* 最大CPU数量 */
#define MAX_ACPI_CPUS 16

/* CPU信息 */
typedef struct {
    uint8_t apic_id;
    uint8_t processor_id;
    uint8_t enabled;
} acpi_cpu_t;

/* 全局变量 */
extern acpi_cpu_t acpi_cpus[MAX_ACPI_CPUS];
extern int acpi_cpu_count;
extern uint32_t acpi_ioapic_address;
extern uint32_t acpi_local_apic_address;

/* 函数 */
int acpi_init(void);
int acpi_parse_madt(void);
acpi_rsdp_t *acpi_find_rsdp(void);

#endif /* ACPI_H */
