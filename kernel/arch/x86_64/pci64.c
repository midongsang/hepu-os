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
uint16_t pci_find_device(uint16_t vendor_id, uint16_t device_id)
{
    for (int bus = 0; bus < 256; bus++) {
        for (int dev = 0; dev < 32; dev++) {
            for (int func = 0; func < 8; func++) {
                uint16_t vendor = pci_config_read16(bus, dev, func, 0x00);
                uint16_t device = pci_config_read16(bus, dev, func, 0x02);
                if (vendor == vendor_id && device == device_id) {
                    /* 读取BAR0（I/O端口基地址） */
                    uint32_t bar0 = pci_config_read32(bus, dev, func, 0x10);
                    uint16_t io_base = (uint16_t)(bar0 & 0xFFFC);

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
