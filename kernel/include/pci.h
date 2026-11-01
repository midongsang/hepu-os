#ifndef HEPU_PCI_H
#define HEPU_PCI_H

#include <stdint.h>

/* ============================================================================
 * 赫普OS - PCI 总线驱动 (V2.23新增)
 * 通过配置空间端口 0xCF8/0xCFC 枚举 PCI 总线上的所有设备,
 * 建设备表供驱动自动匹配绑定. 兼容所有 x86 平台 PCI/PCIe.
 * 参考: PCI Local Bus Spec / PCIe Spec (真实寄存器)
 * ========================================================================== */

#define PCI_CONFIG_ADDR   0xCF8
#define PCI_CONFIG_DATA   0xCFC
#define PCI_ENABLE_BIT    0x80000000UL

/* PCI 配置空间寄存器偏移 */
#define PCI_VENDOR_ID     0x00
#define PCI_DEVICE_ID     0x02
#define PCI_COMMAND       0x04
#define PCI_STATUS        0x06
#define PCI_REVISION      0x08
#define PCI_CLASS_CODE    0x09   /* prog_if:subclass:class, 3字节 */
#define PCI_HEADER_TYPE   0x0E
#define PCI_BAR0          0x10
#define PCI_BAR5          0x24
#define PCI_SUBSYS_VENDOR 0x2C
#define PCI_SUBSYS_ID     0x2E
#define PCI_INTERRUPT_LINE 0x3C
#define PCI_INTERRUPT_PIN  0x3D

/* 常见 PCI 基类 (offset 0x0B) */
#define PCI_CLASS_STORAGE 0x01
#define PCI_CLASS_NETWORK 0x02
#define PCI_CLASS_DISPLAY 0x03
#define PCI_CLASS_MULTIMEDIA 0x04
#define PCI_CLASS_MEMORY  0x05
#define PCI_CLASS_BRIDGE  0x06
#define PCI_CLASS_COMMUNICATION 0x07
#define PCI_CLASS_SERIAL  0x0C   /* USB / SMBus 等 */
#define PCI_CLASS_OTHER   0xFF

/* 存储子类 (offset 0x0A) */
#define PCI_SUBCLASS_IDE   0x01
#define PCI_SUBCLASS_SATA  0x06
#define PCI_SUBCLASS_NVME  0x08
#define PCI_SUBCLASS_AHCI  0x06
/* 串行子类 */
#define PCI_SUBCLASS_UHCI  0x00
#define PCI_SUBCLASS_OHCI  0x10
#define PCI_SUBCLASS_EHCI  0x20
#define PCI_SUBCLASS_XHCI  0x30
#define PCI_SUBCLASS_SMBUS 0x05

/* 设备数量上限 */
#define PCI_MAX_DEVICES    64

/* PCI 设备条目 */
typedef struct {
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t  bus;
    uint8_t  dev;
    uint8_t  func;
    uint8_t  revision;      /* 修订号 */
    uint8_t  base_class;    /* 基类 */
    uint8_t  sub_class;     /* 子类 */
    uint8_t  prog_if;       /* 编程接口 */
    uint8_t  header_type;
    uint32_t bar[6];        /* BAR0-5 */
    uint8_t  irq_line;
    uint8_t  present;       /* 1=存在 */
} pci_device_t;

/* 读取 PCI 配置空间 (真实 0xCF8/0xCFC) */
uint32_t pci_config_read(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset);
/* 写入 PCI 配置空间 */
void pci_config_write(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint32_t val);

/* 扫描全部 PCI 设备, 填充 pci_devices 表, 返回找到的数量 */
int pci_scan_all(void);

/* 按厂商/设备ID查找 */
int pci_find_device(uint16_t vendor, uint16_t device);
/* 按基类查找第一个匹配设备 */
int pci_find_by_class(uint8_t base_class, uint8_t sub_class);

/* 全局设备表 (由 pci_scan_all 填充) */
extern pci_device_t pci_devices[PCI_MAX_DEVICES];
extern int pci_device_count;

/* 输出设备表到屏幕 */
void pci_dump_devices(void);

#endif /* HEPU_PCI_H */
