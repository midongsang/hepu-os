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

/* MADT 中断源覆盖条目（type=2）：把某个 ISA IRQ 重定向到指定 GSI。
   典型：QEMU 中 ISA IRQ0(PIT) 被覆盖到 GSI 2，而非默认的 GSI 0。 */
typedef struct {
    uint8_t bus;            /* 0 = ISA */
    uint8_t source_irq;     /* 原始 ISA 中断号 */
    uint32_t gsi;           /* 实际 GSI（= IOAPIC 引脚号） */
    uint16_t flags;         /* 极性/触发模式 */
} __attribute__((packed)) madt_intsrc_t;

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

/* ISA IRQ -> GSI 映射（16 个 ISA 中断）。
   默认一一对应；解析 MADT 中断源覆盖后，被覆盖的 IRQ 指向实际 GSI。 */
extern int acpi_isa_gsi[16];

/* 函数 */
int acpi_init(void);
int acpi_parse_madt(void);
acpi_rsdp_t *acpi_find_rsdp(void);

/* 仅解析 MADT 中断源覆盖、填充 acpi_isa_gsi。可在切换到纯 APIC
   模式之前独立调用（只依赖分页，不依赖 SMP/中断）。 */
int acpi_init_irq_overrides(void);

/* 把 ISA IRQ 翻译成实际 GSI；未初始化或越界时回退为恒等映射。 */
int acpi_irq_to_gsi(int irq);

#endif /* ACPI_H */
