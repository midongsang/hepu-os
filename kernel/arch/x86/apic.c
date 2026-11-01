/* ============================================================================
 * 赫普OS - APIC驱动 (Local APIC + IO APIC)
 * V6.89新增，用于真机中断控制器支持
 * ========================================================================== */

#include <stdint.h>
#include "apic.h"
#include "acpi.h"
#include "vga.h"
#include "paging.h"

#define APIC_DEBUG 1

static volatile uint32_t *lapic_base = NULL;
static volatile uint32_t *ioapic_base = NULL;
static int apic_available_flag = 0;
static uint32_t ioapic_gsi_base = 0;
static int ioapic_max_redirect = 0;

/* 检测CPU是否支持APIC */
static int cpu_supports_apic(void)
{
    uint32_t eax, edx;
    __asm__ volatile(
        "mov $1, %%eax\n"
        "cpuid\n"
        : "=a"(eax), "=d"(edx)
        :
        : "ebx", "ecx"
    );
    return (edx & (1 << 9)) ? 1 : 0;  /* EDX bit 9 = APIC */
}

/* 读取MSR */
static uint64_t rdmsr(uint32_t msr)
{
    uint32_t lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

/* 写入MSR */
static void wrmsr(uint32_t msr, uint64_t val)
{
    uint32_t lo = (uint32_t)val;
    uint32_t hi = (uint32_t)(val >> 32);
    __asm__ volatile("wrmsr" : : "a"(lo), "d"(hi), "c"(msr));
}

/* Local APIC读写 */
uint32_t lapic_read(uint32_t reg)
{
    if (!lapic_base) return 0;
    return lapic_base[reg / 4];
}

void lapic_write(uint32_t reg, uint32_t val)
{
    if (!lapic_base) return;
    lapic_base[reg / 4] = val;
}

void lapic_send_eoi(void)
{
    lapic_write(LAPIC_EOI, 0);
}

uint32_t lapic_get_id(void)
{
    return (lapic_read(LAPIC_ID) >> 24) & 0xFF;
}

/* IO APIC读写 */
uint32_t ioapic_read(uint32_t reg)
{
    if (!ioapic_base) return 0;
    ioapic_base[IOAPIC_REGSEL / 4] = reg;
    return ioapic_base[IOAPIC_WINDOW / 4];
}

void ioapic_write(uint32_t reg, uint32_t val)
{
    if (!ioapic_base) return;
    ioapic_base[IOAPIC_REGSEL / 4] = reg;
    ioapic_base[IOAPIC_WINDOW / 4] = val;
}

/* 设置IO APIC重定向表项 */
void ioapic_set_irq(uint8_t irq, uint8_t vector, uint8_t dest_apic_id, int masked)
{
    uint32_t reg = IOAPIC_REDTBL_BASE + (irq * 2);
    uint32_t low = vector | IOAPIC_DELIVERY_FIXED | IOAPIC_DEST_PHYSICAL;
    if (masked) low |= IOAPIC_INT_MASKED;
    uint32_t high = ((uint32_t)dest_apic_id) << 24;

    ioapic_write(reg, low);
    ioapic_write(reg + 1, high);
}

void ioapic_mask_irq(uint8_t irq)
{
    uint32_t reg = IOAPIC_REDTBL_BASE + (irq * 2);
    uint32_t low = ioapic_read(reg);
    low |= IOAPIC_INT_MASKED;
    ioapic_write(reg, low);
}

void ioapic_unmask_irq(uint8_t irq)
{
    uint32_t reg = IOAPIC_REDTBL_BASE + (irq * 2);
    uint32_t low = ioapic_read(reg);
    low &= ~IOAPIC_INT_MASKED;
    ioapic_write(reg, low);
}

/* 从MADT解析IO APIC信息 */
static int parse_madt_ioapic(uint32_t *ioapic_addr, uint32_t *gsi_base)
{
    acpi_madt_t *madt = NULL;
    uint32_t madt_phys = 0;

    /* 获取MADT表 */
    acpi_sdt_t table;
    if (acpi_get_table(ACPI_TABLE_MADT, &table) != 0) return -1;
    madt_phys = table.phys_addr;
    if (!madt_phys) return -1;

    madt = (acpi_madt_t *)madt_phys;

    /* 遍历MADT条目 */
    uint8_t *p = (uint8_t *)(madt_phys + sizeof(acpi_madt_t));
    uint8_t *end = (uint8_t *)(madt_phys + madt->header.length);

    while (p < end) {
        uint8_t type = p[0];
        uint8_t length = p[1];
        if (length < 2) break;

        if (type == ACPI_MADT_IO_APIC) {
            /* IO APIC条目: type(1) length(1) ioapic_id(1) reserved(1)
             *              ioapic_addr(4) gsi_base(4) */
            *ioapic_addr = *(uint32_t *)(p + 4);
            *gsi_base = *(uint32_t *)(p + 8);
            return 0;
        }
        p += length;
    }
    return -1;
}

/* 初始化Local APIC */
static int init_local_apic(uint32_t lapic_addr)
{
    /* 映射Local APIC页面 */
    page_directory_t *kdir = paging_get_kernel_dir();
    paging_map(kdir, lapic_addr, lapic_addr, 0x3);  /* PRESENT|WRITE */
    __asm__ volatile("mov %%cr3, %%eax; mov %%eax, %%cr3" ::: "eax");

    lapic_base = (volatile uint32_t *)lapic_addr;

    /* 启用Local APIC（设置SVR） */
    uint32_t svr = lapic_read(LAPIC_SVR);
    svr |= 0x100;  /* APIC Enable */
    svr |= 0xFF;   /* 伪中断向量 */
    lapic_write(LAPIC_SVR, svr);

    /* 设置任务优先级为0（接受所有中断） */
    lapic_write(LAPIC_TPR, 0);

    /* 初始化LVT条目 */
    lapic_write(LAPIC_LVT_TIMER, 0x10000);  /* 屏蔽定时器 */
    lapic_write(LAPIC_LVT_LINT0, 0x10000);  /* 屏蔽LINT0 */
    lapic_write(LAPIC_LVT_LINT1, 0x10000);  /* 屏蔽LINT1 */
    lapic_write(LAPIC_LVT_ERROR, 0x10000);  /* 屏蔽错误 */

    /* 清除错误状态 */
    lapic_write(LAPIC_ESR, 0);
    lapic_write(LAPIC_ESR, 0);

    if (APIC_DEBUG) {
        vga_printf("[APIC] Local APIC @ 0x%x, ID=%d, ver=0x%x\n",
                   lapic_addr, lapic_get_id(), lapic_read(LAPIC_VERSION) & 0xFF);
    }
    return 0;
}

/* 初始化IO APIC */
static int init_io_apic(uint32_t ioapic_addr, uint32_t gsi_base)
{
    /* 映射IO APIC页面 */
    page_directory_t *kdir = paging_get_kernel_dir();
    paging_map(kdir, ioapic_addr, ioapic_addr, 0x3);
    __asm__ volatile("mov %%cr3, %%eax; mov %%eax, %%cr3" ::: "eax");

    ioapic_base = (volatile uint32_t *)ioapic_addr;
    ioapic_gsi_base = gsi_base;

    /* 获取IO APIC版本和最大重定向数 */
    uint32_t ver = ioapic_read(IOAPIC_VER);
    ioapic_max_redirect = (ver >> 16) & 0xFF;
    uint8_t ioapic_id = (ioapic_read(IOAPIC_ID) >> 24) & 0xFF;

    if (APIC_DEBUG) {
        vga_printf("[APIC] IO APIC @ 0x%x, ID=%d, ver=%d, max_redirect=%d, gsi_base=%d\n",
                   ioapic_addr, ioapic_id, ver & 0xFF, ioapic_max_redirect + 1, gsi_base);
    }

    /* 屏蔽所有中断 */
    for (int i = 0; i <= ioapic_max_redirect; i++) {
        ioapic_set_irq(i, 0x20 + i, 0, 1);  /* 全部屏蔽 */
    }

    return 0;
}

/* 路由IRQ到指定向量 */
int apic_route_irq(uint8_t irq, uint8_t vector)
{
    if (!apic_available_flag) return -1;
    if (irq > ioapic_max_redirect) return -1;

    uint32_t lapic_id = lapic_get_id();
    ioapic_set_irq(irq, vector, lapic_id, 0);  /* 取消屏蔽 */

    if (APIC_DEBUG) {
        vga_printf("[APIC] IRQ %d -> vector 0x%x (CPU %d)\n", irq, vector, lapic_id);
    }
    return 0;
}

int apic_available(void)
{
    return apic_available_flag;
}

/* APIC初始化 */
int apic_init(void)
{
    /* 检测CPU支持 */
    if (!cpu_supports_apic()) {
        vga_puts("[APIC] CPU does not support APIC\n");
        return -1;
    }

    /* 检测ACPI MADT */
    uint32_t lapic_addr = 0;
    uint32_t ioapic_count = 0;
    uint32_t lapic_count = 0;
    if (acpi_madt_info(&lapic_addr, &ioapic_count, &lapic_count) != 0) {
        vga_puts("[APIC] MADT not available\n");
        return -1;
    }

    if (APIC_DEBUG) {
        vga_printf("[APIC] MADT: LAPIC@0x%x, IOAPICs=%d, CPUs=%d\n",
                   lapic_addr, ioapic_count, lapic_count);
    }

    /* 启用xAPIC（通过IA32_APIC_BASE MSR） */
    uint64_t apic_base_msr = rdmsr(0x1B);
    if (!(apic_base_msr & 0x800)) {  /* bit 11 = APIC Global Enable */
        apic_base_msr |= 0x800;
        wrmsr(0x1B, apic_base_msr);
    }

    /* 初始化Local APIC */
    if (init_local_apic(lapic_addr) != 0) {
        vga_puts("[APIC] Local APIC init failed\n");
        return -1;
    }

    /* 解析并初始化IO APIC */
    uint32_t ioapic_addr = 0;
    uint32_t gsi_base = 0;
    if (parse_madt_ioapic(&ioapic_addr, &gsi_base) == 0) {
        init_io_apic(ioapic_addr, gsi_base);
    } else {
        vga_puts("[APIC] No IO APIC found in MADT\n");
    }

    apic_available_flag = 1;
    vga_puts("[APIC] Initialized successfully\n");
    return 0;
}
