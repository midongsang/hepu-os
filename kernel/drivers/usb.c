/* ============================================================================
 * 赫普OS - USB 驱动栈
 * 主机控制器框架 + 设备枚举 + 控制传输 + HID类驱动
 *
 * V4.48-E93: 真实 UHCI 主机控制器初始化 + 根集线器端口扫描——
 *   - 修 pci_find_by_class(0x0C,0x03) 子类号错误 (UHCI=0x00, 原 0x03 找不到控制器)
 *   - usb_hc_init 真实操作 UHCI 寄存器 (HCRESET -> 帧列表 -> RUN)
 *   - usb_scan_ports 端口复位 + 连接检测 (PORTSC CCS/CSS)
 *   - 枚举仍为原型模拟 (TD/QH 控制传输真实实现列入后续)
 * ========================================================================== */

#include <stdint.h>
#include "usb.h"
#include "pci.h"
#include "vga.h"
#include "serial.h"
#include "string.h"
#include "kheap.h"

static usb_device_t devices[USB_MAX_DEVICES];
static usb_hc_t host_controller;
static usb_driver_t *drivers[8];
static int driver_count = 0;
static int usb_initialized = 0;

/* ---- V4.48-E93: I/O 端口访问 (UHCI 寄存器为 I/O 空间) ---- */
static inline uint8_t usb_inb(uint16_t port)
{
    uint8_t ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
static inline void usb_outb(uint16_t port, uint8_t val)
{
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint16_t usb_inw(uint16_t port)
{
    uint16_t ret;
    __asm__ volatile("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
static inline void usb_outw(uint16_t port, uint16_t val)
{
    __asm__ volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint32_t usb_inl(uint16_t port)
{
    uint32_t ret;
    __asm__ volatile("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
static inline void usb_outl(uint16_t port, uint32_t val)
{
    __asm__ volatile("outl %0, %1" : : "a"(val), "Nd"(port));
}

/* V4.48-E93: PCI 配置空间写 (0xCF8/0xCFC) */
static void usb_pci_write(uint8_t bus, uint8_t dev, uint8_t func,
                          uint8_t offset, uint32_t val)
{
    usb_outl(PCI_CONFIG_ADDR,
             PCI_ENABLE_BIT | ((uint32_t)bus << 16) |
             ((uint32_t)dev << 11) | ((uint32_t)func << 8) |
             (offset & 0xFC));
    usb_outl(PCI_CONFIG_DATA, val);
}

/* ---- V4.48-E93: UHCI 寄存器定义 (Intel UHCI 规范, 真实偏移/位) ---- */
#define UHCI_USBCMD      0x00   /* 命令寄存器 16-bit */
#define UHCI_USBSTS      0x02   /* 状态寄存器 16-bit */
#define UHCI_USBINTR     0x04   /* 中断使能 16-bit */
#define UHCI_FRNUM       0x06   /* 帧号 16-bit */
#define UHCI_FRBASEADD   0x08   /* 帧列表基址 32-bit */
#define UHCI_SOFMOD      0x0C   /* SOF 调节 8-bit */
#define UHCI_PORTSC1     0x10   /* 端口1 状态/控制 16-bit */
#define UHCI_PORTSC2     0x12   /* 端口2 状态/控制 16-bit */
#define UHCI_NUM_PORTS   2

#define UHCI_CMD_RUN      0x0001  /* 运行/停止 */
#define UHCI_CMD_HCRESET  0x0002  /* 主机控制器复位 */
#define UHCI_CMD_CF       0x0040  /* 配置标志 (软件设置) */
#define UHCI_STS_HCHALTED 0x0001  /* 控制器已挂起 */
#define UHCI_STS_HSE      0x0002  /* 主机系统错误 */
#define UHCI_STS_USBINT   0x0020  /* USB 中断 */

#define UHCI_PORT_CCS    0x0001  /* 当前连接状态 */
#define UHCI_PORT_CSC    0x0002  /* 连接状态变化 (写1清除) */
#define UHCI_PORT_PED    0x0004  /* 端口使能 */
#define UHCI_PORT_PEC    0x0008  /* 端口使能变化 */
#define UHCI_PORT_LSDA   0x0100  /* 线状态 D+ */
#define UHCI_PORT_LSDB   0x0200  /* 线状态 D- */
#define UHCI_PORT_PR     0x2000  /* 端口复位 (写1) */

void usb_init(void)
{
    if (usb_initialized) return;   /* V2.62: 幂等保护 */
    for (int i = 0; i < USB_MAX_DEVICES; i++) {
        devices[i].used = 0;
    }
    driver_count = 0;
    usb_initialized = 1;

    vga_puts("[USB] USB subsystem init done \n");
    serial_puts(0x3F8, "[USB] subsystem init done\n");

    /* V2.62: PCI 检测 USB 主机控制器 */
    usb_hc_detect_pci();

    /* 初始化HID类驱动 */
    usb_hid_init();

    /* V2.62: HID 键盘解析自测 (boot 协议, 串口镜像) */
    extern void usb_hid_kbd_demo(void);
    usb_hid_kbd_demo();

    /* V8.29: 初始化新的USB核心栈（模块化重写版本） */
    extern void hepu_usb_init(void);
    hepu_usb_init();
}

/* V4.48-E93: PCI 检测 USB 主机控制器 (优先 UHCI 子类 0x00, 避免误中 SMBus) */
int usb_hc_detect_pci(void)
{
    int idx = pci_find_by_class(0x0C, PCI_SUBCLASS_UHCI);
    if (idx < 0) idx = pci_find_by_class(0x0C, 0xFF);
    if (idx < 0) {
        vga_puts("[USB] no PCI USB host controller found\n");
        return -1;
    }
    if (idx >= PCI_MAX_DEVICES) return -1;
    pci_device_t *pc = &pci_devices[idx];

    usb_hc_type_t hc_type = USB_HC_UHCI;
    const char *hc_name = "UHCI";
    switch (pc->prog_if) {
        case USB_PI_UHCI: hc_type = USB_HC_UHCI; hc_name = "UHCI"; break;
        case USB_PI_OHCI: hc_type = USB_HC_OHCI; hc_name = "OHCI"; break;
        case USB_PI_EHCI: hc_type = USB_HC_EHCI; hc_name = "EHCI"; break;
        case USB_PI_XHCI: hc_type = USB_HC_XHCI; hc_name = "XHCI"; break;
        default: hc_name = "Unknown"; break;
    }

    /* V4.48-E93: UHCI I/O 寄存器 BAR——Intel 规范为 BAR4, QEMU 实现可能挂 BAR0,
     * 无 BIOS 分配时两个 BAR 都写入自分配 I/O 地址 (0xC000), 再使能 PCI_COMMAND */
#define UHCI_IO_BAR_OFFSET 0x20   /* PCI_BAR4 */
    uint32_t b0 = pci_config_read(pc->bus, pc->dev, pc->func, PCI_BAR0);
    uint32_t b4 = pci_config_read(pc->bus, pc->dev, pc->func, UHCI_IO_BAR_OFFSET);
    uint32_t base;
    if ((b0 & 0xFFFFFFFCu) || (b4 & 0xFFFFFFFCu)) {
        base = (b0 & 0xFFFFFFFCu) ? (b0 & 0xFFFFFFFCu) : (b4 & 0xFFFFFFFCu);
    } else {
        /* 双 BAR 都 0: 自分配 I/O 0xC000 (两个 BAR 都写, 兼容 QEMU/真实硬件) */
        usb_pci_write(pc->bus, pc->dev, pc->func, PCI_BAR0, 0x0000C001u);
        usb_pci_write(pc->bus, pc->dev, pc->func, UHCI_IO_BAR_OFFSET, 0x0000C001u);
        b0 = b4 = 0x0000C001u;
        base = 0x0000C000u;
    }
    uint16_t cmd = (uint16_t)pci_config_read(pc->bus, pc->dev, pc->func, PCI_COMMAND);
    cmd |= 0x0001;                          /* IO Space Enable */
    usb_pci_write(pc->bus, pc->dev, pc->func, PCI_COMMAND, cmd);
    vga_printf("[USB] PCI: %s host controller @ 0x%08x (bus=%d dev=%d func=%d class=%x sub=%x)\n",
               hc_name, base, pc->bus, pc->dev, pc->func, pc->base_class, pc->sub_class);
    serial_puts(0x3F8, "[USB] PCI: ");
    serial_puts(0x3F8, hc_name);
    serial_puts(0x3F8, " host controller detected\n");

    /* V4.48-E93: 真实初始化 (UHCI 寄存器操作 + 端口扫描) */
    usb_hc_init(hc_type, base);
    return idx;
}

int usb_device_count(void)
{
    int n = 0;
    for (int i = 0; i < USB_MAX_DEVICES; i++) {
        if (devices[i].used) n++;
    }
    return n;
}

usb_device_t *usb_get_device(int index)
{
    int n = 0;
    for (int i = 0; i < USB_MAX_DEVICES; i++) {
        if (devices[i].used) {
            if (n == index) return &devices[i];
            n++;
        }
    }
    return NULL;
}

/* V4.48-E93: UHCI 端口扫描——端口复位 + 连接检测, 填设备槽 */
int usb_scan_ports(void)
{
    if (host_controller.type != USB_HC_UHCI || !host_controller.initialized) {
        vga_puts("[USB] scan: no running UHCI controller\n");
        return -1;
    }
    uint16_t io = (uint16_t)host_controller.io_base;
    int found = 0;
    /* V4.48-E93: 每次扫描重建设备表 (避免重复登记) */
    for (int i = 0; i < USB_MAX_DEVICES; i++) devices[i].used = 0;
    for (int p = 0; p < UHCI_NUM_PORTS; p++) {
        uint16_t psc = usb_inw(io + UHCI_PORTSC1 + p * 2);
        /* 端口复位: 写 PR=1, 等待控制器清除 (~50ms) */
        usb_outw(io + UHCI_PORTSC1 + p * 2, (uint16_t)(psc | UHCI_PORT_PR));
        for (int i = 0; i < 20000; i++) {
            psc = usb_inw(io + UHCI_PORTSC1 + p * 2);
            if (!(psc & UHCI_PORT_PR)) break;
        }
        psc = usb_inw(io + UHCI_PORTSC1 + p * 2);
        int connected = (psc & UHCI_PORT_CCS) ? 1 : 0;
        vga_printf("[USB] port %d: %s (PORTSC=0x%x)\n",
                   p + 1, connected ? "device connected" : "empty", (unsigned)psc);
        if (connected) {
            usb_device_t *dev = NULL;
            for (int i = 0; i < USB_MAX_DEVICES; i++) {
                if (!devices[i].used) { dev = &devices[i]; break; }
            }
            if (dev) {
                memset(dev, 0, sizeof(*dev));
                dev->used = 1;
                dev->port = p;
                dev->speed = (psc & (UHCI_PORT_LSDA | UHCI_PORT_LSDB)) ? 1 : 0;
                dev->address = 0;
                dev->configured = 0;
                vga_printf("[USB] device on port %d (enumeration pending TD/QH)\n", p + 1);
                found++;
            }
        }
        /* 清连接状态变化位 */
        usb_outw(io + UHCI_PORTSC1 + p * 2, (uint16_t)(psc | UHCI_PORT_CSC));
    }
    vga_printf("[USB] scan done: %d device(s)\n", found);
    return found;
}

int usb_hc_init(usb_hc_type_t type, uint32_t base)
{
    host_controller.type = type;
    host_controller.io_base = base;
    host_controller.mem_base = base;
    host_controller.irq = 11;  /* 默认IRQ */

    const char *type_name = "Unknown";
    switch (type) {
        case USB_HC_UHCI: type_name = "UHCI"; break;
        case USB_HC_OHCI: type_name = "OHCI"; break;
        case USB_HC_EHCI: type_name = "EHCI"; break;
        case USB_HC_XHCI: type_name = "XHCI"; break;
    }

    if (type != USB_HC_UHCI) {
        /* 非 UHCI: 框架占位 (真实初始化列入后续) */
        host_controller.initialized = 1;
        vga_printf("[USB] host controller %s @ 0x%x (framework only)\n", type_name, base);
        return 0;
    }

    /* ---- V4.48-E93: 真实 UHCI 初始化 ---- */
    uint16_t io = (uint16_t)base;
    vga_printf("[USB] UHCI init @ io=0x%x ...\n", io);
    serial_puts(0x3F8, "[USB] UHCI init io=");
    serial_puthex(0x3F8, io);
    serial_puts(0x3F8, "\n");

    /* 1. HCRESET: 先清 RUN 停止控制器, 再写复位, 等待 HCHalted 置位
     * (QEMU UHCI 上电 USBCMD=0xC1 已在运行, 必须先停止才能复位) */
    usb_outw(io + UHCI_USBCMD, 0x0000);      /* STOP: 清 RUN */
    usb_outw(io + UHCI_USBCMD, UHCI_CMD_HCRESET);
    int ok = 0;
    for (int i = 0; i < 20000; i++) {
        if (usb_inw(io + UHCI_USBSTS) & UHCI_STS_HCHALTED) { ok = 1; break; }
    }
    if (!ok) {
        /* 兼容: 某些实现复位后直接运行 (HCHalted=0) 或复位不可见,
         * 以 FRBASEADD 写入 + 端口扫描作为最终判定 */
        vga_puts("[USB] UHCI reset wait fallback (continue)\n");
        serial_puts(0x3F8, "[USB] UHCI reset wait fallback\n");
    } else {
        vga_puts("[USB] UHCI reset ok (HCHalted set)\n");
        serial_puts(0x3F8, "[USB] UHCI reset ok\n");
    }

    /* 2. 帧列表: 4K 页对齐, 清零, 写 FRBASEADD */
    void *frbase = kmalloc_aligned(4096, 4096);
    if (!frbase) { vga_puts("[USB] UHCI frame list OOM\n"); return -1; }
    memset(frbase, 0, 4096);
    usb_outl(io + UHCI_FRBASEADD, (uint32_t)frbase);

    /* 3. RUN + 配置标志 CF */
    usb_outw(io + UHCI_USBCMD, (uint16_t)(UHCI_CMD_RUN | UHCI_CMD_CF));

    /* 4. 等待退出 Halted */
    ok = 0;
    for (int i = 0; i < 20000; i++) {
        if (!(usb_inw(io + UHCI_USBSTS) & UHCI_STS_HCHALTED)) { ok = 1; break; }
    }
    if (!ok) {
        vga_puts("[USB] UHCI run timeout\n");
        serial_puts(0x3F8, "[USB] UHCI run timeout\n");
        return -1;
    }
    host_controller.initialized = 1;
    vga_printf("[USB] UHCI running: frame list=0x%x\n", (uint32_t)frbase);
    serial_puts(0x3F8, "[USB] UHCI running\n");

    /* 5. 端口扫描 (根集线器) */
    usb_scan_ports();
    return 0;
}

int usb_control_transfer(usb_device_t *dev, uint8_t request_type,
                          uint8_t request, uint16_t value, uint16_t index,
                          void *data, uint16_t length)
{
    if (!dev || !dev->used) return -1;

    /* 原型阶段: 模拟控制传输
     * 实际硬件: 通过主机控制器发送设置包 + 数据阶段 + 状态阶段
     * (真实 TD/QH 控制传输列入后续 E 系列)
     */
    (void)request_type; (void)request; (void)value;
    (void)index; (void)data; (void)length;

    return length;  /* 模拟成功 */
}

int usb_bulk_transfer(usb_device_t *dev, uint8_t ep, void *data, int length)
{
    if (!dev || !dev->used) return -1;
    (void)ep; (void)data;
    return length;  /* 模拟成功 */
}

int usb_interrupt_transfer(usb_device_t *dev, uint8_t ep, void *data, int length)
{
    if (!dev || !dev->used) return -1;
    (void)ep; (void)data;
    return length;  /* 模拟成功 */
}

int usb_get_descriptor(usb_device_t *dev, uint8_t type, uint8_t index,
                        void *buf, uint16_t length)
{
    return usb_control_transfer(dev,
        0x80,  /* IN | Standard | Device */
        USB_REQ_GET_DESCRIPTOR,
        (type << 8) | index,
        0,
        buf, length);
}

int usb_set_address(usb_device_t *dev, uint8_t address)
{
    int ret = usb_control_transfer(dev,
        0x00,  /* OUT | Standard | Device */
        USB_REQ_SET_ADDRESS,
        address, 0, NULL, 0);
    if (ret == 0) {
        dev->address = address;
    }
    return ret;
}

int usb_set_configuration(usb_device_t *dev, uint8_t config)
{
    int ret = usb_control_transfer(dev,
        0x00,
        USB_REQ_SET_CONFIG,
        config, 0, NULL, 0);
    if (ret == 0) {
        dev->configured = 1;
    }
    return ret;
}

int usb_enumerate_device(usb_device_t *dev)
{
    if (!dev) return -1;

    vga_puts("[USB] start device enumeration ...\n");

    /* 1. 读取设备描述符 (前8字节获取max_packet_size0) */
    usb_device_desc_t dev_desc; memset(&dev_desc, 0, sizeof(dev_desc));
    int ret = usb_get_descriptor(dev, USB_DESC_DEVICE, 0, &dev_desc, 8);
    if (ret < 0) {
        vga_puts("[USB] error: cannot read dev descriptor \n");
        return -1;
    }
    dev->max_packet_size0 = dev_desc.bMaxPacketSize0;

    /* 2. 设置地址 */
    static uint8_t next_address = 1;
    ret = usb_set_address(dev, next_address);
    if (ret < 0) {
        vga_puts("[USB] error: set addr failed \n");
        return -1;
    }
    next_address++;

    /* 3. 读取完整设备描述符 */
    ret = usb_get_descriptor(dev, USB_DESC_DEVICE, 0, &dev_desc, sizeof(dev_desc));
    if (ret < 0) return -1;

    dev->vendor_id = dev_desc.idVendor;
    dev->product_id = dev_desc.idProduct;
    dev->device_class = dev_desc.bDeviceClass;

    vga_printf("[USB] device: VID=0x%04x PID=0x%04x type =%d\n",
               dev->vendor_id, dev->product_id, dev->device_class);

    /* 4. 读取配置描述符 (前9字节) */
    usb_config_desc_t cfg_desc; memset(&cfg_desc, 0, sizeof(cfg_desc));
    ret = usb_get_descriptor(dev, USB_DESC_CONFIG, 0, &cfg_desc, 9);
    if (ret < 0) return -1;

    /* 5. 读取完整配置描述符 (包含接口和端点) */
    /* V7.69安全修复: 校验wTotalLength上限，防止恶意USB设备(BadUSB)触发大内存分配 */
    if (cfg_desc.wTotalLength == 0 || cfg_desc.wTotalLength > 8192) {
        vga_printf("[USB] invalid wTotalLength %d\n", cfg_desc.wTotalLength);
        return -1;
    }
    uint8_t *cfg_buf = kmalloc(cfg_desc.wTotalLength);
    if (!cfg_buf) return -1;
    ret = usb_get_descriptor(dev, USB_DESC_CONFIG, 0, cfg_buf, cfg_desc.wTotalLength);
    if (ret < 0) { kfree(cfg_buf); return -1; }

    /* 6. 解析接口和端点描述符 */
    uint32_t offset = 9;  /* 跳过配置描述符 */
    dev->num_interfaces = 0;
    while (offset + 1 < cfg_desc.wTotalLength) {
        uint8_t desc_len = cfg_buf[offset];
        /* V7.69安全修复: 校验desc_len>=2，防止cfg_buf[offset+1]越界读 */
        if (desc_len < 2) {
            vga_printf("[USB] invalid desc_len %d at offset %d\n", desc_len, offset);
            break;
        }
        /* V7.69安全修复: 校验offset+desc_len不越界 */
        if (offset + desc_len > cfg_desc.wTotalLength) {
            vga_printf("[USB] desc extends beyond config (off=%d, len=%d, total=%d)\n",
                       offset, desc_len, cfg_desc.wTotalLength);
            break;
        }
        uint8_t desc_type = cfg_buf[offset + 1];

        if (desc_type == USB_DESC_INTERFACE) {
            usb_interface_desc_t *if_desc = (usb_interface_desc_t *)(cfg_buf + offset);
            if (dev->num_interfaces < USB_MAX_INTERFACES) {
                usb_interface_t *iface = &dev->interfaces[dev->num_interfaces++];
                iface->number = if_desc->bInterfaceNumber;
                iface->class = if_desc->bInterfaceClass;
                iface->subclass = if_desc->bInterfaceSubClass;
                iface->protocol = if_desc->bInterfaceProtocol;
                iface->num_endpoints = 0;
                vga_printf("[USB] iface %d: type =%d endpoint =%d\n",
                           iface->number, iface->class, if_desc->bNumEndpoints);
            }
        }
        else if (desc_type == USB_DESC_ENDPOINT) {
            usb_endpoint_desc_t *ep_desc = (usb_endpoint_desc_t *)(cfg_buf + offset);
            if (dev->num_interfaces > 0) {
                usb_interface_t *iface = &dev->interfaces[dev->num_interfaces - 1];
                if (iface->num_endpoints < USB_MAX_ENDPOINTS) {
                    usb_endpoint_t *ep = &iface->endpoints[iface->num_endpoints++];
                    ep->address = ep_desc->bEndpointAddress;
                    ep->type = ep_desc->bmAttributes & 0x03;
                    ep->max_packet_size = ep_desc->wMaxPacketSize;
                    ep->interval = ep_desc->bInterval;
                    ep->toggle = 0;
                }
            }
        }
        offset += desc_len;
    }

    kfree(cfg_buf);

    /* 7. 设置配置 */
    ret = usb_set_configuration(dev, cfg_desc.bConfigurationValue);
    if (ret < 0) return -1;

    /* 8. 匹配驱动 */
    for (int i = 0; i < driver_count; i++) {
        if (drivers[i]->device_class == dev->device_class ||
            (dev->num_interfaces > 0 &&
             drivers[i]->interface_class == dev->interfaces[0].class)) {
            vga_printf("[USB] match driver: %s\n", drivers[i]->name);
            drivers[i]->probe(dev);
            break;
        }
    }

    vga_puts("[USB] device enumeration done \n");
    return 0;
}

int usb_register_driver(usb_driver_t *driver)
{
    if (!driver || driver_count >= 8) return -1;
    drivers[driver_count++] = driver;
    vga_printf("[USB] register driver: %s\n", driver->name);
    return 0;
}


/* HID类驱动已拆分到 usb_hid.c (V6.46 代码重构) */
