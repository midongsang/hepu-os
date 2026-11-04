/* ============================================================================
 * 赫普OS - 64位PCI配置空间访问
 * V0.10.6: 初始实现
 * ========================================================================== */

#include <stdint.h>

/* PCI配置端口 */
#define PCI_CONFIG_ADDRESS  0xCF8
#define PCI_CONFIG_DATA     0xCFC

/* I/O端口访问 */
static inline uint32_t pci_inl(uint16_t port)
{
    uint32_t val;
    __asm__ volatile("inl %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static inline void pci_outl(uint16_t port, uint32_t val)
{
    __asm__ volatile("outl %0, %1" : : "a"(val), "Nd"(port));
}

/* 读取PCI配置空间的32位字 */
uint32_t pci_config_read32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset)
{
    uint32_t address = (uint32_t)((bus << 16) | (dev << 11) | (func << 8) |
                                  (offset & 0xFC) | 0x80000000);
    pci_outl(PCI_CONFIG_ADDRESS, address);
    return pci_inl(PCI_CONFIG_DATA);
}

/* 读取PCI配置空间的16位字 */
uint16_t pci_config_read16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset)
{
    uint32_t val = pci_config_read32(bus, dev, func, offset);
    return (uint16_t)((val >> ((offset & 2) * 8)) & 0xFFFF);
}

/* 读取PCI配置空间的8位字 */
uint8_t pci_config_read8(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset)
{
    uint32_t val = pci_config_read32(bus, dev, func, offset);
    return (uint8_t)((val >> ((offset & 3) * 8)) & 0xFF);
}

/* 写入PCI配置空间的32位字 */
void pci_config_write32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint32_t val)
{
    uint32_t address = (uint32_t)((bus << 16) | (dev << 11) | (func << 8) |
                                  (offset & 0xFC) | 0x80000000);
    pci_outl(PCI_CONFIG_ADDRESS, address);
    pci_outl(PCI_CONFIG_DATA, val);
}

/* 按Vendor/Device ID查找设备，返回I/O端口基地址（BAR0） */
/* V0.13.64: 按 class 找设备, 返回 BAR0 值 (0=未找到)
 * base_class/sub_class/prog_if (prog_if=0xFF 不匹配) */
uint32_t pci_find_class_bar0(uint8_t base_class, uint8_t sub_class, uint8_t prog_if)
{
    for (int dev = 0; dev < 32; dev++) {
        for (int func = 0; func < 8; func++) {
            uint32_t vendor = pci_config_read32(0, dev, func, 0);
            if (vendor == 0xFFFFFFFF) { if (func == 0) break; continue; }
            uint32_t cc = pci_config_read32(0, dev, func, 8);
            uint8_t bc = (cc >> 24) & 0xFF;
            uint8_t sc = (cc >> 16) & 0xFF;
            uint8_t pi = (cc >> 8) & 0xFF;
            if (bc == base_class && sc == sub_class &&
                (prog_if == 0xFF || pi == prog_if)) {
                return pci_config_read32(0, dev, func, 0x10) & ~0x0F;
            }
        }
    }
    return 0;
}

uint16_t pci_find_device(uint16_t vendor_id, uint16_t device_id)
{
    for (int bus = 0; bus < 256; bus++) {
        for (int dev = 0; dev < 32; dev++) {
            for (int func = 0; func < 8; func++) {
                uint16_t vendor = pci_config_read16(bus, dev, func, 0x00);
                uint16_t device = pci_config_read16(bus, dev, func, 0x02);
                if (vendor == vendor_id && device == device_id) {
                    /* 扫描所有BAR (0x10-0x24)，取第一个I/O空间型(bit0=1)非零基址 */
                    uint16_t io_base = 0;
                    for (int bar_off = 0x10; bar_off <= 0x24; bar_off += 4) {
                        uint32_t bar = pci_config_read32(bus, dev, func, bar_off);
                        if ((bar & 0x01) && (bar & 0xFFFFFFFC)) {   /* I/O型且非零 */
                            io_base = (uint16_t)(bar & 0xFFFC);
                            break;
                        }
                    }

                    /* 启用总线主控和I/O访问 */
                    uint16_t command = pci_config_read16(bus, dev, func, 0x04);
                    command |= 0x07;  /* I/O空间 + 内存空间 + 总线主控 */
                    pci_config_write32(bus, dev, func, 0x04, command);

                    return io_base;
                }
            }
        }
    }
    return 0;
}
