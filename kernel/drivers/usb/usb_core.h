/* ============================================================================
 * 赫普OS - USB核心驱动头文件
 * V8.29: 初始实现
 * ========================================================================== */

#ifndef _USB_CORE_H
#define _USB_CORE_H

#include <stdint.h>
#include <stddef.h>

/* USB常量 */
#define USB_MAX_DEVICES      128
#define USB_MAX_INTERFACES   8
#define USB_MAX_ENDPOINTS    16
#define USB_MAX_CONFIGS      4

/* USB描述符类型 */
#define USB_DT_DEVICE        0x01
#define USB_DT_CONFIG        0x02
#define USB_DT_STRING        0x03
#define USB_DT_INTERFACE     0x04
#define USB_DT_ENDPOINT      0x05
#define USB_DT_HID           0x21
#define USB_DT_REPORT        0x22
#define USB_DT_PHYSICAL      0x23
#define USB_DT_HUB           0x29

/* USB设备类 */
#define HEPU_USB_CLASS_AUDIO      0x01
#define HEPU_USB_CLASS_HID        0x03
#define HEPU_USB_CLASS_PHYSICAL   0x05
#define HEPU_USB_CLASS_IMAGE      0x06
#define HEPU_USB_CLASS_PRINTER    0x07
#define HEPU_USB_CLASS_MASS_STORAGE 0x08
#define HEPU_USB_CLASS_HUB        0x09
#define HEPU_USB_CLASS_CDC_DATA   0x0A
#define HEPU_USB_CLASS_VIDEO      0x0E
#define HEPU_USB_CLASS_WIRELESS   0xE0
#define HEPU_USB_CLASS_MISC       0xEF
#define HEPU_USB_CLASS_APP_SPEC   0xFE
#define HEPU_USB_CLASS_VENDOR     0xFF

/* USB端点方向 */
#define USB_DIR_OUT          0x00
#define USB_DIR_IN           0x80

/* USB端点类型 */
#define USB_EP_CONTROL       0
#define USB_EP_ISOCHRONOUS   1
#define USB_EP_BULK          2
#define USB_EP_INTERRUPT     3

/* USB标准请求 */
#define USB_REQ_GET_STATUS   0x00
#define USB_REQ_CLEAR_FEATURE 0x01
#define USB_REQ_SET_FEATURE  0x03
#define USB_REQ_SET_ADDRESS  0x05
#define USB_REQ_GET_DESCRIPTOR 0x06
#define USB_REQ_SET_DESCRIPTOR 0x07
#define USB_REQ_GET_CONFIG   0x08
#define USB_REQ_SET_CONFIG   0x09
#define USB_REQ_GET_INTERFACE 0x0A
#define USB_REQ_SET_INTERFACE 0x0B
#define USB_REQ_SYNCH_FRAME  0x0C

/* USB请求类型 */
#define USB_TYPE_STANDARD    (0x00 << 5)
#define USB_TYPE_CLASS       (0x01 << 5)
#define USB_TYPE_VENDOR      (0x02 << 5)
#define USB_TYPE_RESERVED    (0x03 << 5)

/* USB接收者 */
#define USB_RECIP_DEVICE     0x00
#define USB_RECIP_INTERFACE  0x01
#define USB_RECIP_ENDPOINT   0x02
#define USB_RECIP_OTHER      0x03

/* USB设备速度 */
#define HEPU_USB_SPEED_LOW        0
#define HEPU_USB_SPEED_FULL       1
#define HEPU_USB_SPEED_HIGH       2
#define HEPU_USB_SPEED_SUPER      3

/* USB状态 */
#define USB_STATE_ATTACHED   0
#define USB_STATE_POWERED    1
#define USB_STATE_DEFAULT    2
#define USB_STATE_ADDRESS    3
#define USB_STATE_CONFIGURED 4
#define USB_STATE_SUSPENDED  5

/* USB描述符结构 */
typedef struct {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
} __attribute__((packed)) usb_descriptor_header_t;

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
} __attribute__((packed)) usb_device_descriptor_t;

typedef struct {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t wTotalLength;
    uint8_t  bNumInterfaces;
    uint8_t  bConfigurationValue;
    uint8_t  iConfiguration;
    uint8_t  bmAttributes;
    uint8_t  bMaxPower;
} __attribute__((packed)) usb_config_descriptor_t;

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
} __attribute__((packed)) usb_interface_descriptor_t;

typedef struct {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bEndpointAddress;
    uint8_t  bmAttributes;
    uint16_t wMaxPacketSize;
    uint8_t  bInterval;
} __attribute__((packed)) usb_endpoint_descriptor_t;

/* USB设置包（Setup Packet） */
typedef struct {
    uint8_t  bmRequestType;
    uint8_t  bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
} __attribute__((packed)) usb_setup_packet_t;

/* USB端点 */
typedef struct {
    uint8_t  address;       /* 端点地址（含方向位） */
    uint8_t  type;          /* 端点类型 */
    uint16_t max_packet_size;
    uint8_t  interval;      /* 轮询间隔 */
    int      toggle;        /* 数据切换位 */
} usb_endpoint_t;

/* USB接口 */
typedef struct {
    uint8_t  number;
    uint8_t  alt_setting;
    uint8_t  class_code;
    uint8_t  sub_class;
    uint8_t  protocol;
    uint8_t  num_endpoints;
    usb_endpoint_t endpoints[USB_MAX_ENDPOINTS];
} usb_interface_t;

/* USB配置 */
typedef struct {
    uint8_t  value;
    uint8_t  num_interfaces;
    usb_interface_t interfaces[USB_MAX_INTERFACES];
} usb_config_t;

/* USB设备 */
typedef struct usb_device {
    uint8_t  address;       /* USB地址 */
    uint8_t  speed;         /* 设备速度 */
    uint8_t  state;         /* 设备状态 */
    uint8_t  hub_addr;      /* 所在Hub地址 */
    uint8_t  hub_port;      /* Hub端口 */
    int      port;          /* V8.66: 根集线器端口号 */

    uint16_t vendor_id;
    uint16_t product_id;
    uint16_t device_bcd;

    uint8_t  device_class;
    uint8_t  device_subclass;
    uint8_t  device_protocol;

    uint8_t  max_packet_size0;
    uint8_t  num_configs;
    uint8_t  active_config;

    usb_config_t configs[USB_MAX_CONFIGS];

    /* 控制器私有数据 */
    void    *controller;
    int      controller_type;

    /* 驱动私有数据 */
    void    *driver_data;
    struct usb_driver *driver;  /* V8.66: 绑定的驱动 */
} usb_device_t;

/* USB控制器类型 */
#define USB_CTRL_UHCI  0
#define USB_CTRL_OHCI  1
#define USB_CTRL_EHCI  2
#define USB_CTRL_XHCI  3

/* USB控制器 */
typedef struct usb_controller {
    int      type;
    uint32_t io_base;       /* I/O基地址 */
    uint32_t mem_base;      /* 内存基地址 */
    uint8_t  irq;
    uint8_t  num_ports;

    /* 设备列表 */
    usb_device_t *devices[USB_MAX_DEVICES];
    int      num_devices;

    /* 操作函数 */
    int      (*init)(struct usb_controller *ctrl);
    int      (*reset_port)(struct usb_controller *ctrl, int port);
    int      (*control_transfer)(struct usb_controller *ctrl,
                                  usb_device_t *dev,
                                  usb_setup_packet_t *setup,
                                  void *data);
    int      (*bulk_transfer)(struct usb_controller *ctrl,
                               usb_device_t *dev,
                               int endpoint,
                               void *data, int length, int direction);
    int      (*interrupt_transfer)(struct usb_controller *ctrl,
                                    usb_device_t *dev,
                                    int endpoint,
                                    void *data, int length);
} usb_controller_t;

/* USB核心函数 */
int hepu_usb_core_init(void);
int hepu_usb_register_controller(usb_controller_t *ctrl);
int hepu_usb_enumerate_device(usb_controller_t *ctrl, int hub_addr, int port, int speed);
int hepu_usb_get_descriptor(usb_device_t *dev, uint8_t type, uint8_t index,
                        uint16_t language, void *buffer, uint16_t length);
int hepu_usb_set_address(usb_device_t *dev, uint8_t address);
int hepu_usb_set_configuration(usb_device_t *dev, uint8_t config);
int hepu_usb_control_request(usb_device_t *dev, uint8_t request_type,
                         uint8_t request, uint16_t value, uint16_t index,
                         void *data, uint16_t length);

/* USB热插拔 - V8.58 */
int hepu_usb_hotplug_poll(void);

/* USB驱动注册 */
typedef struct usb_driver {
    const char *name;
    int      (*probe)(usb_device_t *dev);
    int      (*disconnect)(usb_device_t *dev);
    uint8_t  match_class;
    uint16_t match_vendor;
    uint16_t match_product;
} usb_driver_t;

int hepu_usb_register_driver(usb_driver_t *driver);

#endif /* _USB_CORE_H */
