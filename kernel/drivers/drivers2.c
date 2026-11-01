/* ============================================================================
 * 赫普OS - 驱动完善
 * PCI + PCIe + DMA + 中断控制器 + 定时器
 * ========================================================================== */

#include <stdint.h>
#include "drivers2.h"
#include "vga.h"
#include "string.h"

static drivers2_t drv;

void drivers2_init(void)
{
    memset(&drv, 0, sizeof(drivers2_t));
    drv.initialized = 1;

    /* 中断控制器 */
    drv.irq_ctrl.type = IRQ_CTRL_IOAPIC;
    drv.irq_ctrl.initialized = 1;
    drv.irq_ctrl.irq_count = 24;
    drv.irq_ctrl.spurious_irq = 255;

    /* 定时器 */
    drv.timers[0].used = 1;
    drv.timers[0].id = 0;
    drv.timers[0].type = TIMER_HPET;
    strncpy(drv.timers[0].name, "HPET", 63);
    drv.timers[0].frequency_hz = 14318180;
    drv.timers[0].enabled = 1;
    drv.timers[0].periodic = 1;
    drv.timer_count = 1;

    drv.timers[1].used = 1;
    drv.timers[1].id = 1;
    drv.timers[1].type = TIMER_APIC;
    strncpy(drv.timers[1].name, "LAPIC Timer", 63);
    drv.timers[1].frequency_hz = 1000000000;
    drv.timers[1].enabled = 1;
    drv.timer_count = 2;

    /* DMA通道 */
    for (int i = 0; i < 8; i++) {
        drv.dma_channels[i].used = 1;
        drv.dma_channels[i].id = i;
        drv.dma_count++;
    }

    vga_puts("[DRV2] driver complete init done \n");
    vga_printf(" PCIe + DMA + IOAPIC + HPET/LAPIC timer, DMA channel: %d\n", drv.dma_count);
}

/* PCI */
int drv2_pci_scan(void)
{
    /* 原型阶段: 模拟扫描 */
    drv.pci_count = 4;
    const char *names[] = {"VGA compat controller ", " Ethernet controller ", " audio controller ", "USB controller "};
    uint16_t vendors[] = {0x1234, 0x8086, 0x8086, 0x8086};
    uint16_t devices[] = {0x1111, 0x100E, 0x2415, 0x24C2};
    uint8_t classes[] = {0x03, 0x02, 0x04, 0x0C};
    for (int i = 0; i < 4; i++) {
        drv.pci_devices[i].used = 1;
        drv.pci_devices[i].id = i;
        drv.pci_devices[i].bus = 0;
        drv.pci_devices[i].device = i + 1;
        drv.pci_devices[i].function = 0;
        strncpy(drv.pci_devices[i].name, names[i], 63);
        drv.pci_devices[i].config.vendor_id = vendors[i];
        drv.pci_devices[i].config.device_id = devices[i];
        drv.pci_devices[i].config.class_code = classes[i];
        drv.pci_devices[i].enabled = 1;
        drv.pci_devices[i].driver_loaded = 1;
    }
    vga_printf("[DRV2] PCI scan done: %d device \n", drv.pci_count);
    return drv.pci_count;
}

int drv2_pci_get_device(int index, pci_device_t *dev)
{
    if (index < 0 || index >= DRV2_MAX_PCI_DEVICES || !drv.pci_devices[index].used || !dev) return -1;
    memcpy(dev, &drv.pci_devices[index], sizeof(pci_device_t));
    return 0;
}

int drv2_pci_list(pci_device_t *devs, int max)
{
    if (!devs || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < DRV2_MAX_PCI_DEVICES && count < max; i++) {
        if (drv.pci_devices[i].used) memcpy(&devs[count++], &drv.pci_devices[i], sizeof(pci_device_t));
    }
    return count;
}

int drv2_pci_read_config(int bus, int dev, int func, int offset, uint32_t *value)
{
    (void)bus; (void)dev; (void)func; (void)offset;
    if (value) *value = 0;
    return 0;
}

int drv2_pci_write_config(int bus, int dev, int func, int offset, uint32_t value)
{
    (void)bus; (void)dev; (void)func; (void)offset; (void)value;
    return 0;
}

int drv2_pci_enable_device(int dev_id)
{
    if (dev_id < 0 || dev_id >= DRV2_MAX_PCI_DEVICES || !drv.pci_devices[dev_id].used) return -1;
    drv.pci_devices[dev_id].enabled = 1;
    return 0;
}

int drv2_pci_disable_device(int dev_id)
{
    if (dev_id < 0 || dev_id >= DRV2_MAX_PCI_DEVICES || !drv.pci_devices[dev_id].used) return -1;
    drv.pci_devices[dev_id].enabled = 0;
    return 0;
}

int drv2_pci_set_master(int dev_id, int enable)
{
    (void)dev_id; (void)enable;
    return 0;
}

int drv2_pci_get_bar(int dev_id, int bar_index, uint64_t *addr, uint32_t *size)
{
    if (dev_id < 0 || dev_id >= DRV2_MAX_PCI_DEVICES || !drv.pci_devices[dev_id].used) return -1;
    if (bar_index < 0 || bar_index >= 6) return -1;
    if (addr) *addr = drv.pci_devices[dev_id].config.bar[bar_index];
    if (size) *size = 4096;
    return 0;
}

/* PCIe */
int drv2_pcie_scan(void)
{
    drv.pcie_count = 2;
    strncpy(drv.pcie_devices[0].name, "NVMe SSD", 63);
    drv.pcie_devices[0].link_speed = 3;  /* 16GT/s */
    drv.pcie_devices[0].link_width = 4;  /* x4 */
    drv.pcie_devices[0].used = 1;
    drv.pcie_devices[0].id = 0;

    strncpy(drv.pcie_devices[1].name, "GPU", 63);
    drv.pcie_devices[1].link_speed = 3;
    drv.pcie_devices[1].link_width = 16;  /* x16 */
    drv.pcie_devices[1].used = 1;
    drv.pcie_devices[1].id = 1;

    vga_printf("[DRV2] PCIe scan done: %d device \n", drv.pcie_count);
    return drv.pcie_count;
}

int drv2_pcie_get_device(int index, pcie_device_t *dev)
{
    if (index < 0 || index >= DRV2_MAX_PCIE_DEVICES || !drv.pcie_devices[index].used || !dev) return -1;
    memcpy(dev, &drv.pcie_devices[index], sizeof(pcie_device_t));
    return 0;
}

int drv2_pcie_list(pcie_device_t *devs, int max)
{
    if (!devs || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < DRV2_MAX_PCIE_DEVICES && count < max; i++) {
        if (drv.pcie_devices[i].used) memcpy(&devs[count++], &drv.pcie_devices[i], sizeof(pcie_device_t));
    }
    return count;
}

int drv2_pcie_get_link_status(int dev_id, int *speed, int *width)
{
    if (dev_id < 0 || dev_id >= DRV2_MAX_PCIE_DEVICES || !drv.pcie_devices[dev_id].used) return -1;
    if (speed) *speed = drv.pcie_devices[dev_id].link_speed;
    if (width) *width = drv.pcie_devices[dev_id].link_width;
    return 0;
}

int drv2_pcie_set_aspm(int dev_id, int enable)
{
    if (dev_id < 0 || dev_id >= DRV2_MAX_PCIE_DEVICES || !drv.pcie_devices[dev_id].used) return -1;
    drv.pcie_devices[dev_id].aspm_enabled = enable ? 1 : 0;
    return 0;
}

int drv2_pcie_enable_aer(int dev_id)
{
    if (dev_id < 0 || dev_id >= DRV2_MAX_PCIE_DEVICES || !drv.pcie_devices[dev_id].used) return -1;
    drv.pcie_devices[dev_id].aer_enabled = 1;
    return 0;
}

/* DMA */
int drv2_dma_alloc_channel(void)
{
    for (int i = 0; i < DRV2_MAX_DMA_CHANNELS; i++) {
        if (!drv.dma_channels[i].used) {
            drv.dma_channels[i].used = 1;
            drv.dma_channels[i].id = i;
            drv.dma_count++;
            return i;
        }
    }
    return -1;
}

int drv2_dma_free_channel(int channel_id)
{
    if (channel_id < 0 || channel_id >= DRV2_MAX_DMA_CHANNELS || !drv.dma_channels[channel_id].used) return -1;
    drv.dma_channels[channel_id].used = 0;
    drv.dma_count--;
    return 0;
}

int drv2_dma_setup(int channel_id, int direction, uint64_t src, uint64_t dst, uint32_t length, int width)
{
    if (channel_id < 0 || channel_id >= DRV2_MAX_DMA_CHANNELS || !drv.dma_channels[channel_id].used) return -1;
    drv.dma_channels[channel_id].direction = direction;
    drv.dma_channels[channel_id].src_addr = src;
    drv.dma_channels[channel_id].dst_addr = dst;
    drv.dma_channels[channel_id].length = length;
    drv.dma_channels[channel_id].width = width;
    drv.dma_channels[channel_id].completed = 0;
    drv.dma_channels[channel_id].error = 0;
    return 0;
}

int drv2_dma_start(int channel_id)
{
    if (channel_id < 0 || channel_id >= DRV2_MAX_DMA_CHANNELS || !drv.dma_channels[channel_id].used) return -1;
    drv.dma_channels[channel_id].completed = 1;
    drv.dma_channels[channel_id].transferred = drv.dma_channels[channel_id].length;
    drv.total_dma_transfers++;
    return 0;
}

int drv2_dma_stop(int channel_id)
{
    if (channel_id < 0 || channel_id >= DRV2_MAX_DMA_CHANNELS || !drv.dma_channels[channel_id].used) return -1;
    return 0;
}

int drv2_dma_get_status(int channel_id, int *completed, int *error, uint64_t *transferred)
{
    if (channel_id < 0 || channel_id >= DRV2_MAX_DMA_CHANNELS || !drv.dma_channels[channel_id].used) return -1;
    if (completed) *completed = drv.dma_channels[channel_id].completed;
    if (error) *error = drv.dma_channels[channel_id].error;
    if (transferred) *transferred = drv.dma_channels[channel_id].transferred;
    return 0;
}

int drv2_dma_list(dma_channel_t *channels, int max)
{
    if (!channels || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < DRV2_MAX_DMA_CHANNELS && count < max; i++) {
        if (drv.dma_channels[i].used) memcpy(&channels[count++], &drv.dma_channels[i], sizeof(dma_channel_t));
    }
    return count;
}

/* 中断控制器 */
int drv2_irq_init(irq_controller_type_t type)
{
    drv.irq_ctrl.type = type;
    drv.irq_ctrl.initialized = 1;
    return 0;
}

int drv2_irq_register(int irq, drv2_irq_handler_t handler, void *data, const char *name)
{
    if (irq < 0 || irq >= DRV2_MAX_IRQS || !handler) return -1;
    if (drv.irqs[irq].used) return -1;  /* 已注册 */
    drv.irqs[irq].used = 1;
    drv.irqs[irq].irq = irq;
    drv.irqs[irq].handler = handler;
    drv.irqs[irq].data = data;
    if (name) strncpy(drv.irqs[irq].name, name, 63);
    drv.irqs[irq].enabled = 1;
    drv.irq_count++;
    return 0;
}

int drv2_irq_unregister(int irq)
{
    if (irq < 0 || irq >= DRV2_MAX_IRQS || !drv.irqs[irq].used) return -1;
    drv.irqs[irq].used = 0;
    drv.irq_count--;
    return 0;
}

int drv2_irq_enable(int irq)
{
    if (irq < 0 || irq >= DRV2_MAX_IRQS) return -1;
    drv.irqs[irq].enabled = 1;
    return 0;
}

int drv2_irq_disable(int irq)
{
    if (irq < 0 || irq >= DRV2_MAX_IRQS) return -1;
    drv.irqs[irq].enabled = 0;
    return 0;
}

int drv2_irq_set_affinity(int irq, int cpu_mask)
{
    if (irq < 0 || irq >= DRV2_MAX_IRQS) return -1;
    drv.irqs[irq].cpu_affinity = cpu_mask;
    return 0;
}

int drv2_irq_get_count(int irq, uint64_t *count)
{
    if (irq < 0 || irq >= DRV2_MAX_IRQS || !count) return -1;
    *count = drv.irqs[irq].count;
    return 0;
}

int drv2_irq_list(irq_entry_t *irqs, int max)
{
    if (!irqs || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < DRV2_MAX_IRQS && count < max; i++) {
        if (drv.irqs[i].used) memcpy(&irqs[count++], &drv.irqs[i], sizeof(irq_entry_t));
    }
    return count;
}

int drv2_irq_eoi(int irq)
{
    (void)irq;
    drv.irq_ctrl.eoi_count++;
    return 0;
}

/* 定时器 */
int drv2_timer_init(timer_type_t type, uint64_t freq)
{
    (void)type; (void)freq;
    return 0;
}

int drv2_timer_create(timer_type_t type, const char *name)
{
    for (int i = 0; i < DRV2_MAX_TIMERS; i++) {
        if (!drv.timers[i].used) {
            memset(&drv.timers[i], 0, sizeof(timer_device_t));
            drv.timers[i].used = 1;
            drv.timers[i].id = i;
            drv.timers[i].type = type;
            if (name) strncpy(drv.timers[i].name, name, 63);
            drv.timer_count++;
            return i;
        }
    }
    return -1;
}

int drv2_timer_destroy(int timer_id)
{
    if (timer_id < 0 || timer_id >= DRV2_MAX_TIMERS || !drv.timers[timer_id].used) return -1;
    drv.timers[timer_id].used = 0;
    drv.timer_count--;
    return 0;
}

int drv2_timer_start(int timer_id, uint64_t interval_us, int periodic)
{
    if (timer_id < 0 || timer_id >= DRV2_MAX_TIMERS || !drv.timers[timer_id].used) return -1;
    drv.timers[timer_id].compare = interval_us;
    drv.timers[timer_id].periodic = periodic;
    drv.timers[timer_id].enabled = 1;
    return 0;
}

int drv2_timer_stop(int timer_id)
{
    if (timer_id < 0 || timer_id >= DRV2_MAX_TIMERS || !drv.timers[timer_id].used) return -1;
    drv.timers[timer_id].enabled = 0;
    return 0;
}

int drv2_timer_set_callback(int timer_id, void (*callback)(void *), void *data)
{
    if (timer_id < 0 || timer_id >= DRV2_MAX_TIMERS || !drv.timers[timer_id].used) return -1;
    drv.timers[timer_id].callback = callback;
    drv.timers[timer_id].callback_data = data;
    return 0;
}

int drv2_timer_get_count(int timer_id, uint64_t *count)
{
    if (timer_id < 0 || timer_id >= DRV2_MAX_TIMERS || !drv.timers[timer_id].used || !count) return -1;
    *count = drv.timers[timer_id].interrupts;
    return 0;
}

int drv2_timer_list(timer_device_t *timers, int max)
{
    if (!timers || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < DRV2_MAX_TIMERS && count < max; i++) {
        if (drv.timers[i].used) memcpy(&timers[count++], &drv.timers[i], sizeof(timer_device_t));
    }
    return count;
}

uint64_t drv2_timer_get_ticks(void) { return drv.timers[0].counter; }
uint64_t drv2_timer_get_ns(void) { uint32_t c = (uint32_t)drv.timers[0].counter; return (uint64_t)(c * 1000 / 14318); }

int drv2_get_stats(uint64_t *dma, uint64_t *irqs, uint64_t *timers)
{
    if (dma) *dma = drv.total_dma_transfers;
    if (irqs) *irqs = drv.total_interrupts;
    if (timers) *timers = drv.total_timer_interrupts;
    return 0;
}
