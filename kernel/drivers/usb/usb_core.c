/* ============================================================================
 * 赫普OS - USB核心驱动实现
 * V8.29: 初始实现
 * ========================================================================== */

#include "usb_core.h"
#include "../../include/vga.h"

/* 内存分配 */
extern void *kmalloc(uint32_t size);
extern void kfree(void *ptr);
extern void *memset(void *dst, int val, uint32_t n);
extern void *memcpy(void *dst, const void *src, uint32_t n);

/* 全局状态 */
static usb_controller_t *controllers[4];
static int num_controllers = 0;
static usb_device_t *devices[USB_MAX_DEVICES];
static int num_devices = 0;
static usb_driver_t *drivers[16];
static int num_drivers = 0;

/* 初始化USB核心 */
int hepu_usb_core_init(void)
{
    memset(controllers, 0, sizeof(controllers));
    memset(devices, 0, sizeof(devices));
    memset(drivers, 0, sizeof(drivers));
    num_controllers = 0;
    num_devices = 0;
    num_drivers = 0;

    vga_puts("[USB] Core initialized\n");
    return 0;
}

/* 注册USB控制器 */
int hepu_usb_register_controller(usb_controller_t *ctrl)
{
    if (!ctrl || num_controllers >= 4) return -1;

    controllers[num_controllers++] = ctrl;
    vga_printf("[USB] Controller #%d registered (type=%d)\n",
               num_controllers - 1, ctrl->type);

    /* 初始化控制器 */
    if (ctrl->init) {
        int ret = ctrl->init(ctrl);
        if (ret != 0) {
            vga_printf("[USB] Controller #%d init failed: %d\n",
                       num_controllers - 1, ret);
            return ret;
        }
    }

    return 0;
}

/* 注册USB驱动 */
int hepu_usb_register_driver(usb_driver_t *driver)
{
    if (!driver || num_drivers >= 16) return -1;

    drivers[num_drivers++] = driver;
    vga_printf("[USB] Driver '%s' registered\n", driver->name);
    return 0;
}

/* 匹配驱动 */
static usb_driver_t *usb_match_driver(usb_device_t *dev)
{
    for (int i = 0; i < num_drivers; i++) {
        usb_driver_t *drv = drivers[i];

        /* 按vendor/product匹配 */
        if (drv->match_vendor && drv->match_product) {
            if (drv->match_vendor == dev->vendor_id &&
                drv->match_product == dev->product_id) {
                return drv;
            }
        }

        /* 按类匹配 */
        if (drv->match_class) {
            if (drv->match_class == dev->device_class) {
                return drv;
            }
        }
    }

    return NULL;
}

/* 控制传输 */
int hepu_usb_control_request(usb_device_t *dev, uint8_t request_type,
                         uint8_t request, uint16_t value, uint16_t index,
                         void *data, uint16_t length)
{
    if (!dev || !dev->controller) return -1;

    usb_controller_t *ctrl = (usb_controller_t *)dev->controller;
    if (!ctrl->control_transfer) return -1;

    usb_setup_packet_t setup;
    setup.bmRequestType = request_type;
    setup.bRequest = request;
    setup.wValue = value;
    setup.wIndex = index;
    setup.wLength = length;

    return ctrl->control_transfer(ctrl, dev, &setup, data);
}

/* 获取描述符 */
int hepu_usb_get_descriptor(usb_device_t *dev, uint8_t type, uint8_t index,
                        uint16_t language, void *buffer, uint16_t length)
{
    return hepu_usb_control_request(dev,
        USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_DEVICE,
        USB_REQ_GET_DESCRIPTOR,
        (type << 8) | index,
        language,
        buffer, length);
}

/* 设置地址 */
int hepu_usb_set_address(usb_device_t *dev, uint8_t address)
{
    int ret = hepu_usb_control_request(dev,
        USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_DEVICE,
        USB_REQ_SET_ADDRESS,
        address, 0, NULL, 0);

    if (ret == 0) {
        dev->address = address;
        dev->state = USB_STATE_ADDRESS;
    }

    return ret;
}

/* 设置配置 */
int hepu_usb_set_configuration(usb_device_t *dev, uint8_t config)
{
    int ret = hepu_usb_control_request(dev,
        USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_DEVICE,
        USB_REQ_SET_CONFIG,
        config, 0, NULL, 0);

    if (ret == 0) {
        dev->active_config = config;
        dev->state = USB_STATE_CONFIGURED;
    }

    return ret;
}

/* 枚举设备 */
int hepu_usb_enumerate_device(usb_controller_t *ctrl, int hub_addr, int port, int speed)
{
    vga_printf("[USB] Enumerating device at hub=%d port=%d speed=%d\n",
               hub_addr, port, speed);

    /* 分配设备结构 */
    usb_device_t *dev = (usb_device_t *)kmalloc(sizeof(usb_device_t));
    if (!dev) return -1;

    memset(dev, 0, sizeof(usb_device_t));
    dev->controller = ctrl;
    dev->controller_type = ctrl->type;
    dev->hub_addr = hub_addr;
    dev->hub_port = port;
    dev->speed = speed;
    dev->state = USB_STATE_DEFAULT;
    dev->address = 0;  /* 初始地址0 */

    /* 1. 获取设备描述符（前8字节，获取max_packet_size0） */
    usb_device_descriptor_t dev_desc;
    int ret = hepu_usb_get_descriptor(dev, USB_DT_DEVICE, 0, 0, &dev_desc, 8);
    if (ret < 0) {
        vga_puts("[USB] Failed to get device descriptor (first 8 bytes)\n");
        kfree(dev);
        return -1;
    }

    dev->max_packet_size0 = dev_desc.bMaxPacketSize0;
    vga_printf("[USB] max_packet_size0=%d\n", dev->max_packet_size0);

    /* 2. 设置地址 */
    static uint8_t next_address = 1;
    uint8_t new_address = next_address++;
    if (new_address >= USB_MAX_DEVICES) new_address = 1;

    ret = hepu_usb_set_address(dev, new_address);
    if (ret < 0) {
        vga_puts("[USB] Failed to set address\n");
        kfree(dev);
        return -1;
    }
    vga_printf("[USB] Address set to %d\n", dev->address);

    /* 3. 获取完整设备描述符 */
    ret = hepu_usb_get_descriptor(dev, USB_DT_DEVICE, 0, 0, &dev_desc,
                              sizeof(usb_device_descriptor_t));
    if (ret < 0) {
        vga_puts("[USB] Failed to get full device descriptor\n");
        kfree(dev);
        return -1;
    }

    dev->vendor_id = dev_desc.idVendor;
    dev->product_id = dev_desc.idProduct;
    dev->device_bcd = dev_desc.bcdDevice;
    dev->device_class = dev_desc.bDeviceClass;
    dev->device_subclass = dev_desc.bDeviceSubClass;
    dev->device_protocol = dev_desc.bDeviceProtocol;
    dev->num_configs = dev_desc.bNumConfigurations;

    vga_printf("[USB] Device: VID=0x%04x PID=0x%04x class=0x%02x\n",
               dev->vendor_id, dev->product_id, dev->device_class);

    /* 4. 获取配置描述符 */
    if (dev->num_configs > 0) {
        usb_config_descriptor_t cfg_desc;
        ret = hepu_usb_get_descriptor(dev, USB_DT_CONFIG, 0, 0, &cfg_desc,
                                  sizeof(usb_config_descriptor_t));
        if (ret >= 0) {
            dev->configs[0].value = cfg_desc.bConfigurationValue;
            dev->configs[0].num_interfaces = cfg_desc.bNumInterfaces;
            vga_printf("[USB] Config: %d interfaces\n",
                       cfg_desc.bNumInterfaces);
        }
    }

    /* 5. 设置配置1 */
    if (dev->num_configs > 0) {
        ret = hepu_usb_set_configuration(dev, 1);
        if (ret < 0) {
            vga_puts("[USB] WARNING: Failed to set configuration\n");
        }
    }

    /* 6. 注册设备 */
    devices[num_devices++] = dev;

    /* 7. 匹配并调用驱动 */
    usb_driver_t *driver = usb_match_driver(dev);
    if (driver) {
        vga_printf("[USB] Matched driver: %s\n", driver->name);
        dev->driver = driver;  /* V8.66: 记录绑定的驱动 */
        if (driver->probe) {
            ret = driver->probe(dev);
            if (ret != 0) {
                vga_printf("[USB] Driver probe failed: %d\n", ret);
            }
        }
    } else {
        vga_puts("[USB] No driver matched (generic device)\n");
        dev->driver = NULL;
    }

    return 0;
}

/* 获取设备数量 */
int hepu_hepu_usb_get_device_count(void)
{
    return num_devices;
}

/* 获取设备 */
usb_device_t *hepu_usb_get_device(int index)
{
    if (index < 0 || index >= num_devices) return NULL;
    return devices[index];
}

/* 设备断开处理（V8.66新增）
 * 调用驱动的disconnect回调，从设备列表移除，释放资源 */
int hepu_usb_disconnect_device(usb_device_t *dev)
{
    if (!dev) return -1;

    /* 查找设备在列表中的位置 */
    int idx = -1;
    for (int i = 0; i < num_devices; i++) {
        if (devices[i] == dev) { idx = i; break; }
    }
    if (idx < 0) return -2;

    /* 调用驱动的disconnect回调 */
    if (dev->driver && dev->driver->disconnect) {
        vga_printf("[USB] Disconnecting driver '%s' for device %d\n",
                   dev->driver->name, idx);
        dev->driver->disconnect(dev);
    }

    /* 从设备列表移除 */
    for (int i = idx; i < num_devices - 1; i++) {
        devices[i] = devices[i + 1];
    }
    devices[num_devices - 1] = NULL;
    num_devices--;

    /* 释放设备内存 */
    kfree(dev);

    vga_printf("[USB] Device %d disconnected, %d devices remaining\n",
               idx, num_devices);
    return 0;
}

/* 按端口号断开设备（V8.66新增） */
int hepu_usb_disconnect_by_port(uint8_t port)
{
    for (int i = 0; i < num_devices; i++) {
        if (devices[i] && devices[i]->port == port) {
            return hepu_usb_disconnect_device(devices[i]);
        }
    }
    return -1;
}

/* 兼容旧接口：usb_init */
void hepu_usb_init(void)
{
    static int initialized = 0;
    if (initialized) return;
    initialized = 1;

    vga_puts("[USB] Initializing USB subsystem (V8.32 new stack)\n");

    /* 初始化USB核心 */
    hepu_usb_core_init();

    /* 注册HID驱动 */
    extern int usb_hid_init(void);
    extern int usb_hid_probe(usb_device_t *dev);
    usb_hid_init();

    static usb_driver_t hid_driver = {
        .name = "HID",
        .probe = usb_hid_probe,
        .disconnect = NULL,
        .match_class = HEPU_USB_CLASS_HID,
        .match_vendor = 0,
        .match_product = 0,
    };
    hepu_usb_register_driver(&hid_driver);

    /* 注册MSC驱动 */
    extern int usb_msc_init(void);
    extern int usb_msc_probe(usb_device_t *dev);
    usb_msc_init();

    static usb_driver_t msc_driver = {
        .name = "MSC",
        .probe = usb_msc_probe,
        .disconnect = NULL,
        .match_class = 0x08,  /* Mass Storage */
        .match_vendor = 0,
        .match_product = 0,
    };
    hepu_usb_register_driver(&msc_driver);

    /* 注册打印机驱动 - V8.59 */
    extern int usb_printer_init(void);
    extern int usb_printer_probe(usb_device_t *dev);
    extern int usb_printer_disconnect(usb_device_t *dev);
    usb_printer_init();

    static usb_driver_t printer_driver = {
        .name = "Printer",
        .probe = usb_printer_probe,
        .disconnect = usb_printer_disconnect,
        .match_class = HEPU_USB_CLASS_PRINTER,
        .match_vendor = 0,
        .match_product = 0,
    };
    hepu_usb_register_driver(&printer_driver);

    /* 检测UHCI控制器 */
    extern int uhci_detect(void);
    uhci_detect();

    vga_puts("[USB] Subsystem init done\n");
}

/* USB热插拔轮询 - V8.58
 * 遍历所有注册的USB控制器，检测端口连接状态变化
 * 返回检测到的变化总数 */
int hepu_usb_hotplug_poll(void)
{
    int total_changes = 0;

    for (int i = 0; i < num_controllers; i++) {
        if (!controllers[i]) continue;

        if (controllers[i]->type == USB_CTRL_UHCI) {
            extern int uhci_hotplug_poll(usb_controller_t *ctrl);
            total_changes += uhci_hotplug_poll(controllers[i]);
        }
        /* 未来支持EHCI/xHCI时添加对应轮询 */
    }

    return total_changes;
}
