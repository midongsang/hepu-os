/* ============================================================================
 * 赫普OS - UHCI USB控制器驱动实现
 * USB 1.1 Universal Host Controller Interface
 * V8.29: 初始实现
 * ========================================================================== */

#include "uhci.h"
#include "../../include/vga.h"
#include "../../include/pci.h"
#include "../../include/hotplug.h"

/* I/O端口访问函数（与旧usb.c保持一致） */
static inline uint8_t  uhci_inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
static inline void     uhci_outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint16_t uhci_inw(uint16_t port) {
    uint16_t ret;
    __asm__ volatile("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
static inline void     uhci_outw(uint16_t port, uint16_t val) {
    __asm__ volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint32_t uhci_inl(uint16_t port) {
    uint32_t ret;
    __asm__ volatile("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
static inline void     uhci_outl(uint16_t port, uint32_t val) {
    __asm__ volatile("outl %0, %1" : : "a"(val), "Nd"(port));
}

/* 内存分配 */
extern void *kmalloc(uint32_t size);
extern void *kfree(void *ptr);
extern void *memset(void *dst, int val, uint32_t n);
extern void *memcpy(void *dst, const void *src, uint32_t n);

/* 物理地址获取（简化：假设内核在低内存，虚拟地址=物理地址） */
static uint32_t virt_to_phys(void *addr)
{
    return (uint32_t)addr;
}

/* 读取UHCI寄存器 */
static uint16_t uhci_readw(uhci_controller_t *uhci, uint16_t reg)
{
    return uhci_inw(uhci->io_base + reg);
}

static void uhci_writew(uhci_controller_t *uhci, uint16_t reg, uint16_t val)
{
    uhci_outw(uhci->io_base + reg, val);
}

static uint32_t uhci_readl(uhci_controller_t *uhci, uint16_t reg)
{
    return uhci_inl(uhci->io_base + reg);
}

static void uhci_writel(uhci_controller_t *uhci, uint16_t reg, uint32_t val)
{
    uhci_outl(uhci->io_base + reg, val);
}

/* 等待UHCI操作完成 */
static int uhci_wait_td_complete(uhci_td_t *td, int timeout_ms)
{
    uint32_t start = 0;  /* 简化：使用固定循环 */
    int loops = timeout_ms * 1000;

    while (loops-- > 0) {
        if (!(td->status & UHCI_TD_STS_ACTIVE)) {
            return 0;  /* 完成 */
        }
    }

    return -1;  /* 超时 */
}

/* 执行控制传输（真实实现：TD加入帧列表+轮询等待） */
int uhci_control_transfer(usb_controller_t *ctrl,
                           usb_device_t *dev,
                           usb_setup_packet_t *setup,
                           void *data)
{
    uhci_controller_t *uhci = (uhci_controller_t *)ctrl;
    if (!uhci || !dev || !setup) return -1;

    uint8_t direction = setup->bmRequestType & USB_DIR_IN;
    uint16_t length = setup->wLength;

    /* 使用静态TD（避免动态分配） */
    static uhci_td_t tds[4];
    memset(tds, 0, sizeof(tds));

    int td_count = 0;

    /* 阶段1: SETUP TD */
    tds[0].token = (dev->address << 8) | (0 << 15) | UHCI_TD_TOKEN_PID_SETUP | (8 << 21);
    tds[0].buffer = virt_to_phys(setup);
    tds[0].status = UHCI_TD_STS_ACTIVE;
    tds[0].link = 0x7F;  /* 暂时终止 */
    td_count = 1;

    /* 阶段2: DATA TD（如果有） */
    if (length > 0 && data) {
        tds[1].token = (dev->address << 8) | (0 << 15) |
                       (direction ? UHCI_TD_TOKEN_PID_IN : UHCI_TD_TOKEN_PID_OUT) |
                       (dev->max_packet_size0 << 21);
        tds[1].buffer = virt_to_phys(data);
        tds[1].status = UHCI_TD_STS_ACTIVE;
        tds[1].link = 0x7F;
        tds[0].link = virt_to_phys(&tds[1]) | 0x04;  /* 指向data TD */
        td_count = 2;
    }

    /* 阶段3: STATUS TD */
    int status_idx = td_count;
    tds[status_idx].token = (dev->address << 8) | (0 << 15) |
                            (direction ? UHCI_TD_TOKEN_PID_OUT : UHCI_TD_TOKEN_PID_IN) |
                            (dev->max_packet_size0 << 21);
    tds[status_idx].status = UHCI_TD_STS_ACTIVE;
    tds[status_idx].link = 0x7F;  /* 终止 */

    if (td_count == 1) {
        tds[0].link = virt_to_phys(&tds[status_idx]) | 0x04;
    } else {
        tds[1].link = virt_to_phys(&tds[status_idx]) | 0x04;
    }
    td_count++;

    /* 将第一个TD加入帧列表的第0帧 */
    uint32_t old_frame0 = uhci->frame_list[0];
    uhci->frame_list[0] = virt_to_phys(&tds[0]) | 0x04;  /* TD标志 */

    /* 轮询等待最后一个TD完成 */
    int timeout = 1000000;  /* 约1秒 */
    int completed = 0;
    while (timeout-- > 0) {
        if (!(tds[status_idx].status & UHCI_TD_STS_ACTIVE)) {
            completed = 1;
            break;
        }
    }

    /* 恢复帧列表 */
    uhci->frame_list[0] = old_frame0;

    /* 检查状态 */
    uint32_t status = tds[status_idx].status;
    if (!completed) {
        vga_printf("[UHCI] Control transfer timeout (dev=%d req=0x%02x)\n",
                   dev->address, setup->bRequest);
        return -1;
    }

    if (status & (UHCI_TD_STS_STALLED | UHCI_TD_STS_DBUFERR |
                   UHCI_TD_STS_BABBLE | UHCI_TD_STS_CRCTO |
                   UHCI_TD_STS_BITSTUFF)) {
        vga_printf("[UHCI] Control transfer error: status=0x%08x (dev=%d req=0x%02x)\n",
                   status, dev->address, setup->bRequest);
        return -1;
    }

    /* 返回实际传输长度（从TD的token字段的长度位提取） */
    int actual_len = (tds[status_idx].token >> 21) & 0x7FF;
    if (actual_len == 0x7FF) actual_len = 0;  /* 0长度数据包 */

    vga_printf("[UHCI] Control transfer OK: dev=%d req=0x%02x len=%d/%d\n",
               dev->address, setup->bRequest, actual_len, length);

    return (direction && length > 0) ? actual_len : length;
}

/* 批量传输 - V8.57: 真实实现 */
int uhci_bulk_transfer(usb_controller_t *ctrl,
                        usb_device_t *dev,
                        int endpoint,
                        void *data, int length, int direction)
{
    uhci_controller_t *uhci = (uhci_controller_t *)ctrl;
    if (!uhci || !dev || !data || length <= 0) return -1;
    if (endpoint < 0 || endpoint > 15) return -1;

    /* 使用静态TD（避免动态分配），批量传输最多8个TD */
    static uhci_td_t bulk_tds[8];
    memset(bulk_tds, 0, sizeof(bulk_tds));

    int max_packet = dev->max_packet_size0;
    if (max_packet == 0) max_packet = 64;

    int td_count = 0;
    int remaining = length;
    uint8_t *data_ptr = (uint8_t *)data;

    /* 创建多个DATA TD（每个最大max_packet字节） */
    while (remaining > 0 && td_count < 8) {
        int this_len = (remaining > max_packet) ? max_packet : remaining;

        bulk_tds[td_count].token =
            (dev->address << 8) |
            (endpoint << 15) |
            (direction ? UHCI_TD_TOKEN_PID_IN : UHCI_TD_TOKEN_PID_OUT) |
            (this_len << 21);
        bulk_tds[td_count].buffer = virt_to_phys(data_ptr);
        bulk_tds[td_count].status = UHCI_TD_STS_ACTIVE;
        bulk_tds[td_count].link = 0x7F;

        if (td_count > 0) {
            bulk_tds[td_count - 1].link =
                virt_to_phys(&bulk_tds[td_count]) | 0x04;
        }

        data_ptr += this_len;
        remaining -= this_len;
        td_count++;
    }

    if (td_count == 0) return 0;

    /* 将第一个TD加入帧列表的第0帧 */
    uint32_t old_frame0 = uhci->frame_list[0];
    uhci->frame_list[0] = virt_to_phys(&bulk_tds[0]) | 0x04;

    /* 轮询等待最后一个TD完成 */
    int timeout = 5000000;
    int completed = 0;
    while (timeout-- > 0) {
        if (!(bulk_tds[td_count - 1].status & UHCI_TD_STS_ACTIVE)) {
            completed = 1;
            break;
        }
    }

    uhci->frame_list[0] = old_frame0;

    uint32_t status = bulk_tds[td_count - 1].status;
    if (!completed) {
        vga_printf("[UHCI] Bulk timeout (dev=%d ep=%d len=%d)\n",
                   dev->address, endpoint, length);
        return -1;
    }

    if (status & (UHCI_TD_STS_STALLED | UHCI_TD_STS_DBUFERR |
                   UHCI_TD_STS_BABBLE | UHCI_TD_STS_CRCTO |
                   UHCI_TD_STS_BITSTUFF)) {
        vga_printf("[UHCI] Bulk error: status=0x%08x (dev=%d ep=%d)\n",
                   status, dev->address, endpoint);
        return -1;
    }

    return length - remaining;
}

/* 中断传输 - V8.57: 真实实现 */
int uhci_interrupt_transfer(usb_controller_t *ctrl,
                             usb_device_t *dev,
                             int endpoint,
                             void *data, int length)
{
    uhci_controller_t *uhci = (uhci_controller_t *)ctrl;
    if (!uhci || !dev || !data || length <= 0) return -1;
    if (endpoint < 0 || endpoint > 15) return -1;

    static uhci_td_t int_td;
    memset(&int_td, 0, sizeof(int_td));

    int max_packet = dev->max_packet_size0;
    if (max_packet == 0) max_packet = 8;
    if (length > max_packet) length = max_packet;

    int_td.token =
        (dev->address << 8) |
        (endpoint << 15) |
        UHCI_TD_TOKEN_PID_IN |
        (length << 21);
    int_td.buffer = virt_to_phys(data);
    int_td.status = UHCI_TD_STS_ACTIVE;
    int_td.link = 0x7F;

    uint32_t old_frame0 = uhci->frame_list[0];
    uhci->frame_list[0] = virt_to_phys(&int_td) | 0x04;

    int timeout = 1000000;
    int completed = 0;
    while (timeout-- > 0) {
        if (!(int_td.status & UHCI_TD_STS_ACTIVE)) {
            completed = 1;
            break;
        }
    }

    uhci->frame_list[0] = old_frame0;

    if (!completed) return 0;  /* 中断超时正常（无数据） */

    uint32_t status = int_td.status;
    if (status & (UHCI_TD_STS_STALLED | UHCI_TD_STS_DBUFERR |
                   UHCI_TD_STS_BABBLE | UHCI_TD_STS_CRCTO |
                   UHCI_TD_STS_BITSTUFF)) {
        return -1;
    }

    int actual_len = (int_td.token >> 21) & 0x7FF;
    if (actual_len == 0x7FF) actual_len = 0;

    return actual_len;
}

/* 重置端口 */
int uhci_reset_port(usb_controller_t *ctrl, int port)
{
    uhci_controller_t *uhci = (uhci_controller_t *)ctrl;
    if (!uhci || port < 0 || port >= uhci->num_ports) return -1;

    uint16_t port_reg = (port == 0) ? UHCI_PORTSC1 : UHCI_PORTSC2;

    /* 发送端口重置信号 */
    uint16_t portsc = uhci_readw(uhci, port_reg);
    portsc |= UHCI_PORT_RESET;
    uhci_writew(uhci, port_reg, portsc);

    /* 等待至少50ms */
    for (volatile int i = 0; i < 1000000; i++) {
        /* 简单延时 */
    }

    /* 清除重置信号 */
    portsc &= ~UHCI_PORT_RESET;
    uhci_writew(uhci, port_reg, portsc);

    /* 等待端口使能 */
    for (int i = 0; i < 100; i++) {
        portsc = uhci_readw(uhci, port_reg);
        if (portsc & UHCI_PORT_PED) break;
        for (volatile int j = 0; j < 10000; j++);
    }

    vga_printf("[UHCI] Port %d reset: status=0x%04x\n", port, portsc);

    return 0;
}

/* 初始化UHCI控制器 */
int uhci_init(usb_controller_t *ctrl)
{
    uhci_controller_t *uhci = (uhci_controller_t *)ctrl;
    if (!uhci) return -1;

    vga_printf("[UHCI] Initializing controller at I/O 0x%x\n", uhci->io_base);

    /* 1. 重置控制器 */
    uhci_writew(uhci, UHCI_USBCMD, UHCI_CMD_HCRESET);
    for (volatile int i = 0; i < 100000; i++) {
        if (!(uhci_readw(uhci, UHCI_USBCMD) & UHCI_CMD_HCRESET)) break;
    }

    /* 2. 分配帧列表（1024个4字节指针，4KB对齐） */
    uhci->frame_list = (uint32_t *)kmalloc(4096);
    if (!uhci->frame_list) return -1;
    memset(uhci->frame_list, 0, 4096);
    uhci->frame_list_phys = virt_to_phys(uhci->frame_list);

    /* 3. 设置帧列表基地址 */
    uhci_writel(uhci, UHCI_FRBASEADD, uhci->frame_list_phys);

    /* 4. 分配队列头 */
    uhci->ctrl_qh = (uhci_qh_t *)kmalloc(sizeof(uhci_qh_t));
    uhci->bulk_qh = (uhci_qh_t *)kmalloc(sizeof(uhci_qh_t));
    uhci->int_qh = (uhci_qh_t *)kmalloc(sizeof(uhci_qh_t));

    if (uhci->ctrl_qh) {
        memset(uhci->ctrl_qh, 0, sizeof(uhci_qh_t));
        uhci->ctrl_qh->link = 0x7F;  /* 终止 */
        uhci->ctrl_qh->element = 0x01;  /* 无效 */
    }

    /* 5. 设置帧列表指向控制队列 */
    for (int i = 0; i < 1024; i++) {
        uhci->frame_list[i] = virt_to_phys(uhci->ctrl_qh) | 0x02;  /* QH标志 */
    }

    /* 6. 清除状态 */
    uhci_writew(uhci, UHCI_USBSTS, 0xFFFF);

    /* 7. 禁用中断（使用轮询） */
    uhci_writew(uhci, UHCI_USBINTR, 0);

    /* 8. 启动控制器 */
    uint16_t cmd = uhci_readw(uhci, UHCI_USBCMD);
    cmd |= UHCI_CMD_RS | UHCI_CMD_CF;  /* 运行 + 配置标志 */
    uhci_writew(uhci, UHCI_USBCMD, cmd);

    /* 9. 检测端口 */
    uhci->num_ports = 2;  /* UHCI通常有2个端口 */
    for (int i = 0; i < uhci->num_ports; i++) {
        uint16_t portsc = uhci_readw(uhci, (i == 0) ? UHCI_PORTSC1 : UHCI_PORTSC2);
        vga_printf("[UHCI] Port %d: status=0x%04x %s%s\n",
                   i, portsc,
                   (portsc & UHCI_PORT_CCS) ? "connected" : "",
                   (portsc & UHCI_PORT_LSDA) ? " low-speed" : "");

        /* 如果有设备连接，重置端口 */
        if (portsc & UHCI_PORT_CCS) {
            uhci_reset_port(ctrl, i);

            /* 枚举设备 */
            int speed = (portsc & UHCI_PORT_LSDA) ? HEPU_USB_SPEED_LOW : HEPU_USB_SPEED_FULL;
            hepu_usb_enumerate_device(ctrl, 0, i, speed);
        }
    }

    vga_puts("[UHCI] Controller initialized\n");
    return 0;
}

/* 检测UHCI控制器 */
int uhci_detect(void)
{
    /* UHCI: base_class=0x0C (Serial), sub_class=0x03 (USB), prog_if=0x00 (UHCI) */
    int idx = pci_find_by_class(PCI_CLASS_SERIAL, 0x03);
    if (idx < 0) {
        vga_puts("[UHCI] No USB controller found\n");
        return -1;
    }

    /* 检查是否是UHCI（prog_if=0x00） */
    pci_device_t *p = &pci_devices[idx];
    if (p->prog_if != 0x00) {
        vga_printf("[UHCI] Controller prog_if=0x%02x, not UHCI\n", p->prog_if);
        return -1;
    }

    vga_printf("[UHCI] Found UHCI: %04x:%04x at %02x:%02x.%x\n",
               p->vendor_id, p->device_id, p->bus, p->dev, p->func);

    /* 读取I/O基地址（BAR4，UHCI使用BAR4） */
    uint32_t bar4 = pci_config_read(p->bus, p->dev, p->func, 0x20);
    uint16_t io_base = bar4 & 0xFFFE;  /* 清除最低位 */

    vga_printf("[UHCI] I/O base: 0x%x, IRQ: %d\n", io_base, p->irq_line);

    /* 启用总线主控和I/O空间 */
    uint16_t cmd = pci_config_read(p->bus, p->dev, p->func, 0x04);
    cmd |= 0x01 | 0x04;  /* I/O空间 + 总线主控 */
    pci_config_write(p->bus, p->dev, p->func, 0x04, cmd);

    /* 创建控制器结构 */
    uhci_controller_t *uhci = (uhci_controller_t *)kmalloc(sizeof(uhci_controller_t));
    if (!uhci) return -1;
    memset(uhci, 0, sizeof(uhci_controller_t));

    uhci->base.type = USB_CTRL_UHCI;
    uhci->base.io_base = io_base;
    uhci->base.init = uhci_init;
    uhci->base.reset_port = uhci_reset_port;
    uhci->base.control_transfer = uhci_control_transfer;
    uhci->base.bulk_transfer = uhci_bulk_transfer;
    uhci->base.interrupt_transfer = uhci_interrupt_transfer;
    uhci->io_base = io_base;
    uhci->num_ports = 2;

    /* 注册控制器 */
    hepu_usb_register_controller((usb_controller_t *)uhci);

    return 0;
}

/* USB热插拔端口轮询 - V8.58
 * 检测端口连接状态变化（CSC位），自动枚举新设备或处理断开 */
int uhci_hotplug_poll(usb_controller_t *ctrl)
{
    uhci_controller_t *uhci = (uhci_controller_t *)ctrl;
    if (!uhci) return -1;

    int changes = 0;

    for (int port = 0; port < uhci->num_ports; port++) {
        uint16_t port_reg = (port == 0) ? UHCI_PORTSC1 : UHCI_PORTSC2;
        uint16_t portsc = uhci_readw(uhci, port_reg);

        /* 检查连接状态变化（CSC位） */
        if (portsc & UHCI_PORT_CSC) {
            changes++;

            /* 清除CSC位（写1清除） */
            uhci_writew(uhci, port_reg, UHCI_PORT_CSC);

            /* 重新读取当前连接状态 */
            portsc = uhci_readw(uhci, port_reg);
            int connected = (portsc & UHCI_PORT_CCS) ? 1 : 0;

            if (connected) {
                /* 新设备连接 */
                int speed = (portsc & UHCI_PORT_LSDA) ?
                            HEPU_USB_SPEED_LOW : HEPU_USB_SPEED_FULL;

                vga_printf("[UHCI-HOTPLUG] Device connected on port %d (%s-speed)\n",
                           port, speed == HEPU_USB_SPEED_LOW ? "low" : "full");

                /* 重置端口 */
                uhci_reset_port(ctrl, port);

                /* 枚举设备 */
                hepu_usb_enumerate_device(ctrl, 0, port, speed);

                /* 通知热插拔框架 */
                extern int hotplug_notify_insert(uint8_t bus, const char *name, uint32_t dev_id);
                char dev_name[32];
                snprintf(dev_name, sizeof(dev_name), "usb-port%d", port);
                hotplug_notify_insert(HOTPLUG_BUS_USB, dev_name, port);
            } else {
                /* 设备断开 */
                vga_printf("[UHCI-HOTPLUG] Device disconnected on port %d\n", port);

                /* 通知热插拔框架 */
                extern int hotplug_notify_remove(uint8_t bus, const char *name, uint32_t dev_id);
                char dev_name[32];
                snprintf(dev_name, sizeof(dev_name), "usb-port%d", port);
                hotplug_notify_remove(HOTPLUG_BUS_USB, dev_name, port);
            }
        }

        /* 检查端口使能变化（PEDC位）- 设备过流等情况 */
        if (portsc & UHCI_PORT_PEDC) {
            uhci_writew(uhci, port_reg, UHCI_PORT_PEDC);
            vga_printf("[UHCI-HOTPLUG] Port %d enable/disable change\n", port);
        }
    }

    return changes;
}
