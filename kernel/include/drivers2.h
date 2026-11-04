#ifndef DRIVERS2_H
#define DRIVERS2_H

#include <stdint.h>

#define DRV2_MAX_PCI_DEVICES 32
#define DRV2_MAX_PCIE_DEVICES 64
#define DRV2_MAX_DMA_CHANNELS 16
#define DRV2_MAX_IRQS 256
#define DRV2_MAX_TIMERS 8

/* PCI配置空间 */
typedef struct {
    uint16_t vendor_id;
    uint16_t device_id;
    uint16_t command;
    uint16_t status;
    uint8_t revision_id;
    uint8_t prog_if;
    uint8_t subclass;
    uint8_t class_code;
    uint8_t cache_line_size;
    uint8_t latency_timer;
    uint8_t header_type;
    uint8_t bist;
    uint32_t bar[6];
    uint32_t cardbus_cis;
    uint16_t subsystem_vendor;
    uint16_t subsystem_id;
    uint32_t expansion_rom;
    uint8_t capabilities;
    uint8_t interrupt_line;
    uint8_t interrupt_pin;
    uint8_t min_grant;
    uint8_t max_latency;
} pci_config_t;

/* PCI设备 */
typedef struct {
    int used;
    int id;
    int bus;
    int device;
    int function;
    pci_config_t config;
    char name[64];
    int driver_loaded;
    int enabled;
    uint64_t bar0_phys;
    uint32_t bar0_size;
    void *bar0_virt;
    int irq;
} pci_device_t;

/* PCIe设备 */
typedef struct {
    int used;
    int id;
    int bus;
    int device;
    int function;
    pci_config_t config;
    char name[64];
    int driver_loaded;
    int enabled;
    int link_speed;  /* 0=2.5GT/s,1=5GT/s,2=8GT/s,3=16GT/s,4=32GT/s */
    int link_width;  /* x1/x2/x4/x8/x16 */
    int max_payload;
    int aspm_enabled;
    int aer_enabled;  /* Advanced Error Reporting */
} pcie_device_t;

/* DMA通道 */
typedef struct {
    int used;
    int id;
    int direction;  /* 0=mem-to-dev,1=dev-to-mem,2=mem-to-mem */
    uint64_t src_addr;
    uint64_t dst_addr;
    uint32_t length;
    int width;  /* 8/16/32/64位 */
    int burst;
    int completed;
    int error;
    uint64_t transferred;
} dma_channel_t;

/* 中断控制器类型 */
typedef enum {
    IRQ_CTRL_PIC = 0,  /* 8259 PIC */
    IRQ_CTRL_IOAPIC,  /* I/O APIC */
    IRQ_CTRL_LAPIC,  /* Local APIC */
    IRQ_CTRL_GIC,  /* ARM GIC */
    IRQ_CTRL_MSI,  /* MSI/MSI-X */
} irq_controller_type_t;

/* 中断控制器 */
typedef struct {
    irq_controller_type_t type;
    int initialized;
    int base_addr;
    int irq_count;
    int spurious_irq;
    uint64_t eoi_count;
} irq_controller_t;

/* 中断处理函数 */
typedef void (*drv2_irq_handler_t)(int irq, void *data);

/* 中断条目 */
typedef struct {
    int used;
    int irq;
    drv2_irq_handler_t handler;
    void *data;
    char name[64];
    uint64_t count;
    int enabled;
    int priority;
    int cpu_affinity;
} irq_entry_t;

/* 定时器类型 */
typedef enum {
    TIMER_PIT = 0,  /* 8253 PIT */
    TIMER_APIC,  /* Local APIC Timer */
    TIMER_HPET,  /* HPET */
    TIMER_TSC,  /* TSC Deadline */
    TIMER_ARM,  /* ARM Generic Timer */
} timer_type_t;

/* 定时器 */
typedef struct {
    int used;
    int id;
    timer_type_t type;
    char name[64];
    uint64_t frequency_hz;
    uint64_t counter;
    uint64_t compare;
    int periodic;
    int enabled;
    int irq;
    uint64_t interrupts;
    void (*callback)(void *data);
    void *callback_data;
} timer_device_t;

/* 驱动完善状态 */
typedef struct {
    int initialized;
    /* PCI */
    pci_device_t pci_devices[DRV2_MAX_PCI_DEVICES];
    int pci_count;
    /* PCIe */
    pcie_device_t pcie_devices[DRV2_MAX_PCIE_DEVICES];
    int pcie_count;
    /* DMA */
    dma_channel_t dma_channels[DRV2_MAX_DMA_CHANNELS];
    int dma_count;
    /* 中断 */
    irq_controller_t irq_ctrl;
    irq_entry_t irqs[DRV2_MAX_IRQS];
    int irq_count;
    /* 定时器 */
    timer_device_t timers[DRV2_MAX_TIMERS];
    int timer_count;
    /* 统计 */
    uint64_t total_dma_transfers;
    uint64_t total_interrupts;
    uint64_t total_timer_interrupts;
} drivers2_t;

/* 初始化驱动完善 */
void drivers2_init(void);

/* PCI */
int drv2_pci_scan(void);
int drv2_pci_get_device(int index, pci_device_t *dev);
int drv2_pci_list(pci_device_t *devs, int max);
int drv2_pci_read_config(int bus, int dev, int func, int offset, uint32_t *value);
int drv2_pci_write_config(int bus, int dev, int func, int offset, uint32_t value);
int drv2_pci_enable_device(int dev_id);
int drv2_pci_disable_device(int dev_id);
int drv2_pci_set_master(int dev_id, int enable);
int drv2_pci_get_bar(int dev_id, int bar_index, uint64_t *addr, uint32_t *size);

/* PCIe */
int drv2_pcie_scan(void);
int drv2_pcie_get_device(int index, pcie_device_t *dev);
int drv2_pcie_list(pcie_device_t *devs, int max);
int drv2_pcie_get_link_status(int dev_id, int *speed, int *width);
int drv2_pcie_set_aspm(int dev_id, int enable);
int drv2_pcie_enable_aer(int dev_id);

/* DMA */
int drv2_dma_alloc_channel(void);
int drv2_dma_free_channel(int channel_id);
int drv2_dma_setup(int channel_id, int direction, uint64_t src, uint64_t dst, uint32_t length, int width);
int drv2_dma_start(int channel_id);
int drv2_dma_stop(int channel_id);
int drv2_dma_get_status(int channel_id, int *completed, int *error, uint64_t *transferred);
int drv2_dma_list(dma_channel_t *channels, int max);

/* 中断控制器 */
int drv2_irq_init(irq_controller_type_t type);
int drv2_irq_register(int irq, drv2_irq_handler_t handler, void *data, const char *name);
int drv2_irq_unregister(int irq);
int drv2_irq_enable(int irq);
int drv2_irq_disable(int irq);
int drv2_irq_set_affinity(int irq, int cpu_mask);
int drv2_irq_get_count(int irq, uint64_t *count);
int drv2_irq_list(irq_entry_t *irqs, int max);
int drv2_irq_eoi(int irq);

/* 定时器 */
int drv2_timer_init(timer_type_t type, uint64_t freq);
int drv2_timer_create(timer_type_t type, const char *name);
int drv2_timer_destroy(int timer_id);
int drv2_timer_start(int timer_id, uint64_t interval_us, int periodic);
int drv2_timer_stop(int timer_id);
int drv2_timer_set_callback(int timer_id, void (*callback)(void *), void *data);
int drv2_timer_get_count(int timer_id, uint64_t *count);
int drv2_timer_list(timer_device_t *timers, int max);
uint64_t drv2_timer_get_ticks(void);
uint64_t drv2_timer_get_ns(void);

/* 统计 */
int drv2_get_stats(uint64_t *dma, uint64_t *irqs, uint64_t *timers);

#endif /* DRIVERS2_H */
