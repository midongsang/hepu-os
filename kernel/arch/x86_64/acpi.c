/* ============================================================================
 * 赫普OS - ACPI MADT解析实现
 * V0.10.142: 解析ACPI MADT获取APIC ID列表
 * ========================================================================== */

#include "acpi.h"

/* 外部函数 */
extern void vga_puts(const char *str);
extern void vga_put_hex(uint64_t val);
extern void *memcpy(void *dst, const void *src, unsigned long n);

/* 全局变量 */
acpi_cpu_t acpi_cpus[MAX_ACPI_CPUS];
int acpi_cpu_count = 0;
uint32_t acpi_ioapic_address = 0;
uint32_t acpi_local_apic_address = 0xFEE00000;

/* ISA IRQ -> GSI 映射，默认恒等（IRQn -> GSI n）。
   标记 -1 表示尚未由 MADT 覆盖解析初始化。 */
int acpi_isa_gsi[16] = {
    -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1
};

/* 把映射表重置为恒等（解析前的安全默认值） */
static void acpi_reset_isa_gsi(void)
{
    for (int i = 0; i < 16; i++) {
        acpi_isa_gsi[i] = i;
    }
}

/* ISA IRQ -> 实际 GSI。未初始化或越界时回退恒等，保证调用方永远可用。 */
int acpi_irq_to_gsi(int irq)
{
    if (irq < 0 || irq >= 16) {
        return irq;
    }
    if (acpi_isa_gsi[irq] < 0) {
        return irq;  /* 尚未解析：按规范默认 ISA IRQ 与 GSI 一一对应 */
    }
    return acpi_isa_gsi[irq];
}

/* 校验和验证 */
static uint8_t acpi_checksum(const void *ptr, uint32_t length)
{
    const uint8_t *bytes = (const uint8_t *)ptr;
    uint8_t sum = 0;
    for (uint32_t i = 0; i < length; i++) {
        sum += bytes[i];
    }
    return sum;
}

/* 查找RSDP（在0xE0000-0xFFFFF范围内） */
acpi_rsdp_t *acpi_find_rsdp(void)
{
    /* RSDP通常在以下位置：
     * 1. BIOS EBDA（0x40:0x0E指向的1KB区域）
     * 2. 0xE0000-0xFFFFF主BIOS区域
     */
    uint8_t *ebda_ptr = (uint8_t *)0x40E;
    uint16_t ebda_seg = (uint16_t)(ebda_ptr[0] | (ebda_ptr[1] << 8));
    uint8_t *ebda = (uint8_t *)(uintptr_t)((uint32_t)ebda_seg << 4);

    /* 在EBDA中搜索 */
    for (uint32_t i = 0; i < 1024; i += 16) {
        acpi_rsdp_t *rsdp = (acpi_rsdp_t *)(ebda + i);
        if (rsdp->signature[0] == 'R' && rsdp->signature[1] == 'S' &&
            rsdp->signature[2] == 'D' && rsdp->signature[3] == ' ' &&
            rsdp->signature[4] == 'P' && rsdp->signature[5] == 'T' &&
            rsdp->signature[6] == 'R' && rsdp->signature[7] == ' ') {
            if (acpi_checksum(rsdp, 20) == 0) {
                return rsdp;
            }
        }
    }

    /* 在主BIOS区域搜索 */
    for (uint32_t addr = 0xE0000; addr < 0x100000; addr += 16) {
        acpi_rsdp_t *rsdp = (acpi_rsdp_t *)(uintptr_t)addr;
        if (rsdp->signature[0] == 'R' && rsdp->signature[1] == 'S' &&
            rsdp->signature[2] == 'D' && rsdp->signature[3] == ' ' &&
            rsdp->signature[4] == 'P' && rsdp->signature[5] == 'T' &&
            rsdp->signature[6] == 'R' && rsdp->signature[7] == ' ') {
            if (acpi_checksum(rsdp, 20) == 0) {
                return rsdp;
            }
        }
    }

    return 0;
}

/* 解析MADT */
int acpi_parse_madt(void)
{
    acpi_rsdp_t *rsdp = acpi_find_rsdp();
    if (!rsdp) {
        vga_puts("[ACPI] RSDP not found\n");
        return -1;
    }

    vga_puts("[ACPI] RSDP found at ");
    vga_put_hex((uint64_t)rsdp);
    vga_puts("\n");

    /* 获取RSDT */
    uint32_t rsdt_phys = rsdp->rsdt_address;
    vga_puts("[ACPI] RSDT physical address: ");
    vga_put_hex(rsdt_phys);
    vga_puts("\n");

    /* RSDT 可能在 >1GB 物理地址（高内存机型），用高半区直接映射访问。
     * phys<1GB 仍走低身份映射（phys_to_virt 原样返回）。 */
    extern uint64_t vmm64_phys_to_virt(uint64_t phys);
    acpi_rsdt_t *rsdt = (acpi_rsdt_t *)vmm64_phys_to_virt((uint64_t)rsdt_phys);
    if (!rsdt || rsdt_phys == 0) {
        vga_puts("[ACPI] RSDT not found\n");
        return -2;
    }

    /* 验证RSDT校验和 */
    if (acpi_checksum(rsdt, rsdt->header.length) != 0) {
        vga_puts("[ACPI] RSDT checksum failed\n");
        return -3;
    }

    vga_puts("[ACPI] RSDT found, length=");
    vga_put_hex(rsdt->header.length);
    vga_puts("\n");

    /* 遍历RSDT条目，查找MADT */
    uint32_t entry_count = (rsdt->header.length - sizeof(acpi_header_t)) / 4;
    acpi_madt_t *madt = 0;

    for (uint32_t i = 0; i < entry_count; i++) {
        acpi_header_t *header = (acpi_header_t *)vmm64_phys_to_virt((uint64_t)rsdt->entry[i]);
        if (header->signature[0] == 'A' && header->signature[1] == 'P' &&
            header->signature[2] == 'I' && header->signature[3] == 'C') {
            madt = (acpi_madt_t *)header;
            break;
        }
    }

    if (!madt) {
        vga_puts("[ACPI] MADT not found\n");
        return -4;
    }

    /* 验证MADT校验和 */
    if (acpi_checksum(madt, madt->header.length) != 0) {
        vga_puts("[ACPI] MADT checksum failed\n");
        return -5;
    }

    vga_puts("[ACPI] MADT found, length=");
    vga_put_hex(madt->header.length);
    vga_puts("\n");

    /* 保存本地APIC地址 */
    acpi_local_apic_address = madt->local_apic_address;
    vga_puts("[ACPI] Local APIC address: ");
    vga_put_hex(acpi_local_apic_address);
    vga_puts("\n");

    /* 解析MADT条目 */
    uint32_t offset = 0;
    uint32_t madt_length = madt->header.length - sizeof(acpi_madt_t) + 8;

    /* 先把 ISA->GSI 映射重置为恒等，再由 type=2 覆盖条目改写 */
    acpi_reset_isa_gsi();

    while (offset < madt_length && acpi_cpu_count < MAX_ACPI_CPUS) {
        uint8_t *entry_ptr = (uint8_t *)madt->entries + offset;
        uint8_t type = entry_ptr[0];
        uint8_t length = entry_ptr[1];

        if (length == 0) break;

        switch (type) {
            case MADT_TYPE_LAPIC: {
                madt_lapic_t *lapic = (madt_lapic_t *)entry_ptr;
                if (lapic->flags & 1) {  /* enabled */
                    acpi_cpus[acpi_cpu_count].apic_id = lapic->apic_id;
                    acpi_cpus[acpi_cpu_count].processor_id = lapic->processor_id;
                    acpi_cpus[acpi_cpu_count].enabled = 1;
                    acpi_cpu_count++;
                    vga_puts("[ACPI] CPU: apic_id=");
                    vga_put_hex(lapic->apic_id);
                    vga_puts(", proc_id=");
                    vga_put_hex(lapic->processor_id);
                    vga_puts("\n");
                }
                break;
            }
            case MADT_TYPE_IOAPIC: {
                madt_ioapic_t *ioapic = (madt_ioapic_t *)entry_ptr;
                acpi_ioapic_address = ioapic->address;
                vga_puts("[ACPI] I/O APIC: id=");
                vga_put_hex(ioapic->ioapic_id);
                vga_puts(", addr=");
                vga_put_hex(ioapic->address);
                vga_puts("\n");
                break;
            }
            case MADT_TYPE_INT_SRC: {
                /* 中断源覆盖：记录 ISA IRQ -> GSI 的实际路由 */
                madt_intsrc_t *ov = (madt_intsrc_t *)(entry_ptr + 2);
                if (ov->source_irq < 16) {
                    acpi_isa_gsi[ov->source_irq] = (int)ov->gsi;
                    vga_puts("[ACPI] IRQ override: ISA IRQ");
                    vga_put_hex(ov->source_irq);
                    vga_puts(" -> GSI");
                    vga_put_hex(ov->gsi);
                    vga_puts("\n");
                }
                break;
            }
            default:
                break;
        }

        offset += length;
    }

    vga_puts("[ACPI] Total CPUs found: ");
    vga_put_hex(acpi_cpu_count);
    vga_puts("\n");

    return 0;
}

/* ACPI初始化 */
int acpi_init(void)
{
    vga_puts("[ACPI] Initializing ACPI...\n");
    return acpi_parse_madt();
}

/* 仅解析 MADT 中断源覆盖，填充 acpi_isa_gsi。
   在切换到纯 APIC 模式之前调用：此时分页已就绪（本函数只做内存读取，
   不依赖 SMP 或中断），让随后的 IOAPIC 重定向能拿到正确的 GSI。 */
int acpi_init_irq_overrides(void)
{
    acpi_rsdp_t *rsdp = acpi_find_rsdp();
    if (!rsdp) {
        acpi_reset_isa_gsi();  /* 找不到 RSDP：保持恒等映射，调用方仍可用 */
        return -1;
    }

    extern uint64_t vmm64_phys_to_virt(uint64_t phys);
    acpi_rsdt_t *rsdt = (acpi_rsdt_t *)vmm64_phys_to_virt((uint64_t)rsdp->rsdt_address);
    if (!rsdt || rsdp->rsdt_address == 0) {
        acpi_reset_isa_gsi();
        return -2;
    }

    uint32_t entry_count = (rsdt->header.length - sizeof(acpi_header_t)) / 4;
    acpi_madt_t *madt = 0;
    for (uint32_t i = 0; i < entry_count; i++) {
        acpi_header_t *header =
            (acpi_header_t *)vmm64_phys_to_virt((uint64_t)rsdt->entry[i]);
        if (header->signature[0] == 'A' && header->signature[1] == 'P' &&
            header->signature[2] == 'I' && header->signature[3] == 'C') {
            madt = (acpi_madt_t *)header;
            break;
        }
    }

    if (!madt) {
        acpi_reset_isa_gsi();
        return -3;
    }

    /* 重置为恒等，再应用 type=2 覆盖 */
    acpi_reset_isa_gsi();
    uint32_t length = madt->header.length - sizeof(acpi_madt_t) + 8;
    uint32_t off = 0;
    while (off < length) {
        uint8_t *entry = (uint8_t *)madt->entries + off;
        uint8_t type = entry[0];
        uint8_t entry_len = entry[1];
        if (entry_len == 0) break;
        if (type == MADT_TYPE_INT_SRC) {
            madt_intsrc_t *ov = (madt_intsrc_t *)(entry + 2);
            if (ov->source_irq < 16) {
                acpi_isa_gsi[ov->source_irq] = (int)ov->gsi;
            }
        }
        off += entry_len;
    }
    return 0;
}
