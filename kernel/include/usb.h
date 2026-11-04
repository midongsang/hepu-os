#ifndef USB_H
#define USB_H

#include <stdint.h>

#define USB_MAX_DEVICES 16
#define USB_MAX_ENDPOINTS 16
#define USB_MAX_INTERFACES 8

/* USB描述符类型 */
#define USB_DESC_DEVICE    1
#define USB_DESC_CONFIG    2
#define USB_DESC_STRING    3
#define USB_DESC_INTERFACE 4
#define USB_DESC_ENDPOINT  5
#define USB_DESC_HID       0x21

/* USB请求类型 */
#define USB_REQ_GET_STATUS    0
#define USB_REQ_CLEAR_FEATURE 1
#define USB_REQ_SET_FEATURE   3
#define USB_REQ_SET_ADDRESS   5
#define USB_REQ_GET_DESCRIPTOR 6
#define USB_REQ_SET_DESCRIPTOR 7
#define USB_REQ_GET_CONFIG    8
#define USB_REQ_SET_CONFIG    9
#define USB_REQ_GET_INTERFACE 10
#define USB_REQ_SET_INTERFACE 11
#define USB_REQ_SYNCH_FRAME   12

/* USB设备类 */
#define USB_CLASS_HID    3
#define USB_CLASS_STORAGE 8
#define USB_CLASS_VENDOR 0xFF

/* USB端点类型 */
#define USB_EP_CONTROL 0
#define USB_EP_ISOCHRONOUS 1
#define USB_EP_BULK 2
#define USB_EP_INTERRUPT 3

/* USB方向 */
#define USB_DIR_OUT 0
#define USB_DIR_IN  1

/* USB设备描述符 */
typedef struct {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t bcdUSB;
    uint8_t  bDeviceClass;
    uint8_t  bDeviceSubClass;
    uint8_t  bDeviceProtocol;
    uint8_t  bMaxPacketSize0;
    uint16_t idVendor;
    uint16_t idProduct;
    uint16_t bcdDevice;
    uint8_t  iManufacturer;
    uint8_t  iProduct;
    uint8_t  iSerialNumber;
    uint8_t  bNumConfigurations;
} __attribute__((packed)) usb_device_desc_t;

/* USB配置描述符 */
typedef struct {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t wTotalLength;
    uint8_t  bNumInterfaces;
    uint8_t  bConfigurationValue;
    uint8_t  iConfiguration;
    uint8_t  bmAttributes;
    uint8_t  bMaxPower;
} __attribute__((packed)) usb_config_desc_t;

/* USB接口描述符 */
typedef struct {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bInterfaceNumber;
    uint8_t  bAlternateSetting;
    uint8_t  bNumEndpoints;
    uint8_t  bInterfaceClass;
    uint8_t  bInterfaceSubClass;
    uint8_t  bInterfaceProtocol;
    uint8_t  iInterface;
} __attribute__((packed)) usb_interface_desc_t;

/* USB端点描述符 */
typedef struct {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bEndpointAddress;
    uint8_t  bmAttributes;
    uint16_t wMaxPacketSize;
    uint8_t  bInterval;
} __attribute__((packed)) usb_endpoint_desc_t;

/* USB设置包 */
typedef struct {
    uint8_t  bmRequestType;
    uint8_t  bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
} __attribute__((packed)) usb_setup_pkt_t;

/* USB端点 */
typedef struct {
    uint8_t  address;
    uint8_t  type;
    uint16_t max_packet_size;
    uint8_t  interval;
    int      toggle;
} usb_endpoint_t;

/* USB接口 */
typedef struct {
    uint8_t  number;
    uint8_t  class;
    uint8_t  subclass;
    uint8_t  protocol;
    usb_endpoint_t endpoints[USB_MAX_ENDPOINTS];
    int num_endpoints;
} usb_interface_t;

/* USB设备 */
typedef struct {
    int used;
    uint8_t  address;
    uint8_t  hub_port;      /* 兼容旧字段 */
    int      port;          /* V4.48-E93: 根集线器端口号 (0-based) */
    int      speed;         /* V4.48-E93: 0=全速 1=低速 */
    uint16_t vendor_id;
    uint16_t product_id;
    uint8_t  device_class;
    uint8_t  max_packet_size0;
    usb_interface_t interfaces[USB_MAX_INTERFACES];
    int num_interfaces;
    int configured;
    void *driver_data;
    struct usb_driver *driver;  /* V8.66: 绑定的驱动 */
} usb_device_t;

/* USB主机控制器类型 */
typedef enum {
    USB_HC_UHCI = 0,
    USB_HC_OHCI,
    USB_HC_EHCI,
    USB_HC_XHCI,
} usb_hc_type_t;

/* USB主机控制器 */
typedef struct {
    usb_hc_type_t type;
    uint32_t io_base;
    uint32_t mem_base;
    int irq;
    int initialized;
} usb_hc_t;

/* USB驱动 */
typedef struct {
    const char *name;
    uint8_t device_class;
    uint8_t interface_class;
    int (*probe)(usb_device_t *dev);
    int (*disconnect)(usb_device_t *dev);
} usb_driver_t;

/* 初始化USB子系统 */
void usb_init(void);

/* 主机控制器初始化 */
int usb_hc_init(usb_hc_type_t type, uint32_t base);

/* 设备枚举 */
int usb_enumerate_device(usb_device_t *dev);

/* 控制传输 */
int usb_control_transfer(usb_device_t *dev, uint8_t request_type,
                          uint8_t request, uint16_t value, uint16_t index,
                          void *data, uint16_t length);

/* 批量传输 */
int usb_bulk_transfer(usb_device_t *dev, uint8_t ep, void *data, int length);

/* 中断传输 */
int usb_interrupt_transfer(usb_device_t *dev, uint8_t ep, void *data, int length);

/* 获取描述符 */
int usb_get_descriptor(usb_device_t *dev, uint8_t type, uint8_t index,
                        void *buf, uint16_t length);

/* 设置地址 */
int usb_set_address(usb_device_t *dev, uint8_t address);

/* 设置配置 */
int usb_set_configuration(usb_device_t *dev, uint8_t config);

/* 注册驱动 */
int usb_register_driver(usb_driver_t *driver);

/* HID类驱动 */
int usb_hid_init(void);

/* ---- V2.62: USB 驱动栈增强 ---- */

/* USB 主机控制器编程接口 (prog_if) */
#define USB_PI_UHCI 0x00
#define USB_PI_OHCI 0x10
#define USB_PI_EHCI 0x20
#define USB_PI_XHCI 0x30

/* PCI 检测 USB 主机控制器 (class 0x0C), 初始化并打印类型/BAR */
int usb_hc_detect_pci(void);

/* V4.48-E93: UHCI 根集线器端口扫描——复位端口 + 连接检测, 填设备槽 */
int usb_scan_ports(void);

/* 查询设备表: 返回设备数 / 取第 index 个设备 */
int usb_device_count(void);
usb_device_t *usb_get_device(int index);

/* 设备断开（V8.66新增） */
int usb_disconnect_device(usb_device_t *dev);
int usb_disconnect_by_port(uint8_t port);

/* ---- HID 键盘 (boot 协议) ---- */
/* 处理一个 8 字节 HID 键盘报告: [0]=modifier [1]=reserved [2..7]=keycode */
void usb_hid_kbd_process_report(const uint8_t *report, int len);
/* 演示: 模拟 boot 协议键盘报告流并解析 (真实 usage 映射) */
void usb_hid_kbd_demo(void);

#endif /* USB_H */
