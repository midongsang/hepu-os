/* ============================================================================
 * 赫普OS - PCI 设备 → 设备树 自动注册绑定 (V2.23新增)
 * 将 PCI 探测到的设备按类别注册到 hal_device 设备树,
 * 实现"换到任意 x86 设备自动识别硬件"的驱动匹配基础.
 * ========================================================================== */
#include <stdint.h>
#include "pci.h"
#include "hal_device.h"
#include "vga.h"

/* PCI 基类 → 设备树类型映射 */
static device_type_t pci_class_to_devtype(uint8_t base_class)
{
    switch (base_class) {
        case PCI_CLASS_STORAGE:   return DEVICE_STORAGE;
        case PCI_CLASS_NETWORK:   return DEVICE_NETWORK;
        case PCI_CLASS_DISPLAY:   return DEVICE_DISPLAY;
        case PCI_CLASS_MULTIMEDIA:return DEVICE_AUDIO;
        case PCI_CLASS_SERIAL:
            return DEVICE_USB;    /* USB 控制器/串行总线 */
        case PCI_CLASS_BRIDGE:    return DEVICE_BUS;
        case PCI_CLASS_MEMORY:    return DEVICE_MEMORY;
        case PCI_CLASS_COMMUNICATION: return DEVICE_OTHER;
        default:                  return DEVICE_OTHER;
    }
}

/* 设备类别显示名 */
static const char *devtype_name(device_type_t t)
{
    switch (t) {
        case DEVICE_STORAGE: return "storage";
        case DEVICE_NETWORK: return "network";
        case DEVICE_DISPLAY: return "display";
        case DEVICE_AUDIO:   return "audio";
        case DEVICE_USB:     return "usb";
        case DEVICE_BUS:     return "bus";
        case DEVICE_INPUT:   return "input";
        case DEVICE_TIMER:   return "timer";
        default:             return "other";
    }
}

/* 把 PCI 设备注册进设备树, 返回注册数量 */
int pci_bind_to_devtree(void)
{
    int bound = 0;

    for (int i = 0; i < pci_device_count; i++) {
        pci_device_t *p = &pci_devices[i];
        device_type_t dt = pci_class_to_devtype(p->base_class);

        /* 跳过桥接设备(它们不是功能设备, 但保留bus节点) */
        char name[32];
        int n = 0;
        name[n++] = 'p';
        name[n++] = 'c';
        name[n++] = 'i';
        name[n++] = '-';
        /* 附加总线/设备号到名称 */
        static const char hex[] = "0123456789abcdef";
        name[n++] = hex[(p->bus >> 4) & 0xF];
        name[n++] = hex[p->bus & 0xF];
        name[n++] = ':';
        name[n++] = hex[(p->dev >> 4) & 0xF];
        name[n++] = hex[p->dev & 0xF];
        name[n] = '\0';

        /* hardware_id = vendor | device */
        uint32_t hw_id = ((uint32_t)p->vendor_id << 16) | p->device_id;
        uint32_t bar0 = p->bar[0];

        device_node_t *node = hal_device_create(name, dt, hw_id, bar0, p->irq_line);
        if (!node) continue;

        hal_device_set_prop(node, "vendor", p->vendor_id);
        hal_device_set_prop(node, "device", p->device_id);
        hal_device_set_prop(node, "class", p->base_class);
        hal_device_set_prop(node, "subclass", p->sub_class);
        hal_device_set_prop(node, "func", p->func);

        /* 匹配到驱动则标记已绑定 */
        hal_device_set_status(node, 1);

        vga_printf("[DEVTREE] %s (%s) %x:%x bound, irq=%d\n",
                   name, devtype_name(dt),
                   p->vendor_id, p->device_id, p->irq_line);
        bound++;
    }

    vga_printf("[DEVTREE] %d device(s) registered to device tree\n", bound);
    return bound;
}
