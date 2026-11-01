/* ============================================================================
 * 赫普OS - PCI 总线驱动实现 (V2.23新增)
 * 通过配置空间端口 0xCF8/0xCFC 枚举 PCI/PCIe 设备.
 * 真实标准: PCI Local Bus Specification 3.0
 * ========================================================================== */
#include <stdint.h>
#include "pci.h"
#include "vga.h"

pci_device_t pci_devices[PCI_MAX_DEVICES];
int pci_device_count = 0;

static inline uint32_t inl(uint16_t port)
{
    uint32_t ret;
    __asm__ volatile ("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outl(uint16_t port, uint32_t val)
{
    __asm__ volatile ("outl %0, %1" : : "a"(val), "Nd"(port));
}

/* 读 PCI 配置空间: 构造配置地址(bit31=1 + bus:dev:func + 寄存器偏移) */
uint32_t pci_config_read(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset)
{
    uint32_t addr = PCI_ENABLE_BIT
                  | ((uint32_t)bus << 16)
                  | ((uint32_t)dev << 11)
                  | ((uint32_t)func << 8)
                  | (offset & 0xFC);
    outl(PCI_CONFIG_ADDR, addr);
    return inl(PCI_CONFIG_DATA);
}

/* 写 PCI 配置空间 */
void pci_config_write(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint32_t val)
{
    uint32_t addr = PCI_ENABLE_BIT
                  | ((uint32_t)bus << 16)
                  | ((uint32_t)dev << 11)
                  | ((uint32_t)func << 8)
                  | (offset & 0xFC);
    outl(PCI_CONFIG_ADDR, addr);
    outl(PCI_CONFIG_DATA, val);
}

static uint16_t pci_read_vendor(uint8_t bus, uint8_t dev, uint8_t func)
{
    return (uint16_t)(pci_config_read(bus, dev, func, PCI_VENDOR_ID) & 0xFFFF);
}

/* 探测某个 bus:dev:func 是否有效 (vendor != 0xFFFF) */
static int pci_probe_function(uint8_t bus, uint8_t dev, uint8_t func)
{
    uint32_t id = pci_config_read(bus, dev, func, PCI_VENDOR_ID);
    uint16_t vendor = (uint16_t)(id & 0xFFFF);
    if (vendor == 0xFFFF) return 0;  /* 无设备 */

    if (pci_device_count >= PCI_MAX_DEVICES) return 0;

    pci_device_t *p = &pci_devices[pci_device_count];
    p->vendor_id = vendor;
    p->device_id = (uint16_t)(id >> 16);
    p->bus = bus;
    p->dev = dev;
    p->func = func;

    uint32_t class_rev = pci_config_read(bus, dev, func, PCI_CLASS_CODE);
    p->revision = (uint8_t)(class_rev & 0xFF);
    p->prog_if  = (uint8_t)((class_rev >> 8) & 0xFF);
    p->sub_class = (uint8_t)((class_rev >> 16) & 0xFF);
    p->base_class = (uint8_t)((class_rev >> 24) & 0xFF);

    uint32_t hdr = pci_config_read(bus, dev, func, PCI_HEADER_TYPE);
    p->header_type = (uint8_t)((hdr >> 16) & 0xFF);

    /* BAR0-5 (仅 header_type bit7=0 的普通设备读 BAR) */
    for (int i = 0; i < 6; i++) {
        p->bar[i] = pci_config_read(bus, dev, func, PCI_BAR0 + i * 4);
    }

    uint32_t int_reg = pci_config_read(bus, dev, func, PCI_INTERRUPT_LINE);
    p->irq_line = (uint8_t)(int_reg & 0xFF);
    p->present = 1;

    pci_device_count++;
    return 1;
}

/* 枚举: bus 0-255, dev 0-31, func 0-7 */
int pci_scan_all(void)
{
    pci_device_count = 0;

    for (uint32_t bus = 0; bus < 256; bus++) {
        for (uint32_t dev = 0; dev < 32; dev++) {
            uint16_t vendor = pci_read_vendor((uint8_t)bus, (uint8_t)dev, 0);
            if (vendor == 0xFFFF) continue;  /* 该 dev 无设备 */

            uint32_t hdr = pci_config_read((uint8_t)bus, (uint8_t)dev, 0, PCI_HEADER_TYPE);
            uint8_t header_type = (uint8_t)((hdr >> 16) & 0xFF);

            pci_probe_function((uint8_t)bus, (uint8_t)dev, 0);
            /* 多功能设备 (header_type & 0x80): 探测 func 1-7 */
            if (header_type & 0x80) {
                for (uint32_t func = 1; func < 8; func++) {
                    uint16_t v = pci_read_vendor((uint8_t)bus, (uint8_t)dev, (uint8_t)func);
                    if (v != 0xFFFF) {
                        pci_probe_function((uint8_t)bus, (uint8_t)dev, (uint8_t)func);
                    }
                }
            }
        }
    }
    return pci_device_count;
}

int pci_find_device(uint16_t vendor, uint16_t device)
{
    for (int i = 0; i < pci_device_count; i++) {
        if (pci_devices[i].present &&
            pci_devices[i].vendor_id == vendor &&
            pci_devices[i].device_id == device) {
            return i;
        }
    }
    return -1;
}

int pci_find_by_class(uint8_t base_class, uint8_t sub_class)
{
    for (int i = 0; i < pci_device_count; i++) {
        if (pci_devices[i].present &&
            pci_devices[i].base_class == base_class &&
            (sub_class == 0xFF || pci_devices[i].sub_class == sub_class)) {
            return i;
        }
    }
    return -1;
}

/* 基类名称 */
static const char *class_name(uint8_t c)
{
    switch (c) {
        case PCI_CLASS_STORAGE:   return "Storage";
        case PCI_CLASS_NETWORK:   return "Network";
        case PCI_CLASS_DISPLAY:   return "Display";
        case PCI_CLASS_MULTIMEDIA:return "Audio/Video";
        case PCI_CLASS_MEMORY:    return "Memory";
        case PCI_CLASS_BRIDGE:    return "Bridge";
        case PCI_CLASS_COMMUNICATION: return "Comm";
        case PCI_CLASS_SERIAL:    return "Serial/USB";
        default:                  return "Other";
    }
}

void pci_dump_devices(void)
{
    vga_puts("=== PCI devices ===\n");
    for (int i = 0; i < pci_device_count; i++) {
        pci_device_t *p = &pci_devices[i];
        /* 注: vga_printf 不支持 %02x 宽度格式, 用 %x/%d */
        vga_printf("[PCI] bus=%x dev=%x func=%d vend=%x dev=%x class=%x sub=%x (%s) irq=%d\n",
                   p->bus, p->dev, p->func,
                   p->vendor_id, p->device_id,
                   p->base_class, p->sub_class,
                   class_name(p->base_class), p->irq_line);
    }
    vga_printf("[PCI] %d device(s) found\n", pci_device_count);
}
