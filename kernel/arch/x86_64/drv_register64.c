/* ============================================================================
 * 赫普OS 64位 - 驱动注册
 * 将所有64位驱动注册到统一驱动框架
 * V0.10.82: P2-1统一驱动框架，驱动注册
 * ========================================================================== */

#include <stdint.h>
#include "../../shared/drivers/driver_framework.h"

/* 驱动函数声明 */
extern void ide64_init(void);
extern int  ide64_read_sector(uint32_t lba, uint8_t *buffer);
extern int  ide64_write_sector(uint32_t lba, const uint8_t *buffer);
extern int  ide64_read_sectors(uint32_t lba, uint8_t count, uint8_t *buffer);
extern int  ide64_write_sectors(uint32_t lba, uint8_t count, const uint8_t *buffer);
extern void rtc64_init(void);
extern void pit64_init(void);
extern void keyboard64_init(void);
extern void ps2mouse64_init(void);
extern void pci64_init(void);
extern void rtl8139_64_init(void);
extern void ahci64_init(void);
extern void ac97_init(void);
extern int  usb64_init(void);   /* V0.13.9: USB UHCI 主机控制器 */

/* IDE驱动包装函数（匹配驱动框架签名） */
static int ide_drv_read(void *private, uint64_t lba, void *buffer, uint32_t size) {
    (void)private;
    uint8_t count = (size + 511) / 512;
    return ide64_read_sectors((uint32_t)lba, count, (uint8_t *)buffer);
}
static int ide_drv_write(void *private, uint64_t lba, const void *buffer, uint32_t size) {
    (void)private;
    uint8_t count = (size + 511) / 512;
    return ide64_write_sectors((uint32_t)lba, count, (const uint8_t *)buffer);
}

/* IDE驱动操作表 */
static drv_ops_t ide_drv_ops = {
    .init = (int (*)(void *))ide64_init,
    .probe = 0,
    .remove = 0,
    .suspend = 0,
    .resume = 0,
    .ioctl = 0,
    .read = ide_drv_read,
    .write = ide_drv_write,
    .irq_handler = 0
};

static drv_desc_t ide_drv_desc = {
    .name = "IDE",
    .category = DRV_CATEGORY_STORAGE,
    .chipset = "Generic IDE/ATA",
    .description = "IDE/ATA storage controller (LBA28/LBA48)",
    .ops = {0},
    .private_data = 0,
    .state = DRV_STATE_UNLOADED,
    .id = -1
};

/* RTC驱动描述符 */
static drv_desc_t rtc_drv_desc = {
    .name = "RTC",
    .category = DRV_CATEGORY_RTC,
    .chipset = "MC146818",
    .description = "Real-Time Clock",
    .ops = {0},
    .private_data = 0,
    .state = DRV_STATE_UNLOADED,
    .id = -1
};

/* PIT驱动描述符 */
static drv_desc_t pit_drv_desc = {
    .name = "PIT",
    .category = DRV_CATEGORY_TIMER,
    .chipset = "Intel 8253/8254",
    .description = "Programmable Interval Timer (1000Hz)",
    .ops = {0},
    .private_data = 0,
    .state = DRV_STATE_UNLOADED,
    .id = -1
};

/* 键盘驱动描述符 */
static drv_desc_t kbd_drv_desc = {
    .name = "PS/2 Keyboard",
    .category = DRV_CATEGORY_INPUT,
    .chipset = "PS/2",
    .description = "PS/2 Keyboard controller",
    .ops = {0},
    .private_data = 0,
    .state = DRV_STATE_UNLOADED,
    .id = -1
};

/* 鼠标驱动描述符 */
static drv_desc_t mouse_drv_desc = {
    .name = "PS/2 Mouse",
    .category = DRV_CATEGORY_INPUT,
    .chipset = "PS/2",
    .description = "PS/2 Mouse controller",
    .ops = {0},
    .private_data = 0,
    .state = DRV_STATE_UNLOADED,
    .id = -1
};

/* PCI驱动描述符 */
static drv_desc_t pci_drv_desc = {
    .name = "PCI",
    .category = DRV_CATEGORY_BUS,
    .chipset = "Generic PCI",
    .description = "PCI bus enumeration",
    .ops = {0},
    .private_data = 0,
    .state = DRV_STATE_UNLOADED,
    .id = -1
};

/* RTL8139驱动描述符 */
static drv_desc_t rtl8139_drv_desc = {
    .name = "RTL8139",
    .category = DRV_CATEGORY_NETWORK,
    .chipset = "Realtek RTL8139",
    .description = "Fast Ethernet controller",
    .ops = {0},
    .private_data = 0,
    .state = DRV_STATE_UNLOADED,
    .id = -1
};

/* V0.13.9: USB 驱动描述符 (UHCI 主机控制器) */
static drv_desc_t usb_drv_desc = {
    .name = "USB",
    .category = DRV_CATEGORY_USB,
    .chipset = "Intel UHCI (PIIX3/PIIX4)",
    .description = "USB 1.1 host controller (HID keyboard/mouse/tablet)",
    .ops = {0},
    .private_data = 0,
    .state = DRV_STATE_UNLOADED,
    .id = -1
};

/* AHCI驱动描述符 */
static drv_desc_t ahci_drv_desc = {
    .name = "AHCI",
    .category = DRV_CATEGORY_STORAGE,
    .chipset = "Generic AHCI",
    .description = "AHCI/SATA host controller",
    .ops = {0},
    .private_data = 0,
    .state = DRV_STATE_UNLOADED,
    .id = -1
};

/* 注册所有驱动 */
void drv_register_all64(void)
{
    /* 复制IDE操作表 */
    ide_drv_desc.ops = ide_drv_ops;

    /* 注册驱动 */
    ide_drv_desc.id = drv_register(&ide_drv_desc);
    rtc_drv_desc.id = drv_register(&rtc_drv_desc);
    pit_drv_desc.id = drv_register(&pit_drv_desc);
    kbd_drv_desc.id = drv_register(&kbd_drv_desc);
    mouse_drv_desc.id = drv_register(&mouse_drv_desc);
    pci_drv_desc.id = drv_register(&pci_drv_desc);
    rtl8139_drv_desc.id = drv_register(&rtl8139_drv_desc);
    ahci_drv_desc.id = drv_register(&ahci_drv_desc);
    usb_drv_desc.id = drv_register(&usb_drv_desc);
    ac97_init();   /* P7-3: AC'97 声卡 (QEMU -soundhw ac97) */

    /* 标记为活跃（已在初始化流程中初始化） */
    ide_drv_desc.state = DRV_STATE_ACTIVE;
    rtc_drv_desc.state = DRV_STATE_ACTIVE;
    pit_drv_desc.state = DRV_STATE_ACTIVE;
    kbd_drv_desc.state = DRV_STATE_ACTIVE;
    mouse_drv_desc.state = DRV_STATE_ACTIVE;
    pci_drv_desc.state = DRV_STATE_ACTIVE;
    rtl8139_drv_desc.state = DRV_STATE_ACTIVE;
    ahci_drv_desc.state = DRV_STATE_ACTIVE;
    usb_drv_desc.state = DRV_STATE_ACTIVE;
}
