/* ============================================================================
 * 赫普OS - USB HID类驱动头文件
 * V8.32: 初始实现
 * ========================================================================== */

#ifndef _USB_HID_H
#define _USB_HID_H

#include "../usb/usb_core.h"

/* HID描述符类型 */
#define HID_DT_HID           0x21
#define HID_DT_REPORT        0x22
#define HID_DT_PHYSICAL      0x23

/* HID请求 */
#define HID_REQ_GET_REPORT   0x01
#define HID_REQ_GET_IDLE     0x02
#define HID_REQ_GET_PROTOCOL 0x03
#define HID_REQ_SET_REPORT   0x09
#define HID_REQ_SET_IDLE     0x0A
#define HID_REQ_SET_PROTOCOL 0x0B

/* HID协议 */
#define HID_PROTOCOL_BOOT    0x00
#define HID_PROTOCOL_REPORT  0x01

/* HID子类 */
#define HID_SUBCLASS_NONE    0x00
#define HID_SUBCLASS_BOOT    0x01

/* HID协议（接口描述符的bInterfaceProtocol） */
#define HID_ITF_PROTOCOL_NONE    0x00
#define HID_ITF_PROTOCOL_KEYBOARD 0x01
#define HID_ITF_PROTOCOL_MOUSE    0x02

/* HID报告类型 */
#define HID_REPORT_TYPE_INPUT   0x01
#define HID_REPORT_TYPE_OUTPUT  0x02
#define HID_REPORT_TYPE_FEATURE 0x03

/* 键盘引导协议报告（8字节） */
typedef struct {
    uint8_t modifiers;    /* 修饰键状态 */
    uint8_t reserved;
    uint8_t keycode[6];   /* 按键码（最多6个同时按下） */
} __attribute__((packed)) hid_keyboard_report_t;

/* 键盘修饰键位 */
#define HID_KBD_MOD_LCTRL   0x01
#define HID_KBD_MOD_LSHIFT  0x02
#define HID_KBD_MOD_LALT    0x04
#define HID_KBD_MOD_LGUI    0x08
#define HID_KBD_MOD_RCTRL   0x10
#define HID_KBD_MOD_RSHIFT  0x20
#define HID_KBD_MOD_RALT    0x40
#define HID_KBD_MOD_RGUI    0x80

/* 鼠标引导协议报告（4字节） */
typedef struct {
    uint8_t buttons;      /* 按钮状态 */
    int8_t  x_displacement; /* X位移 */
    int8_t  y_displacement; /* Y位移 */
    int8_t  wheel;        /* 滚轮 */
} __attribute__((packed)) hid_mouse_report_t;

/* 鼠标按钮位 */
#define HID_MOUSE_BTN_LEFT   0x01
#define HID_MOUSE_BTN_RIGHT  0x02
#define HID_MOUSE_BTN_MIDDLE 0x04

/* HID设备结构 */
typedef struct {
    usb_device_t *usb_dev;     /* 关联的USB设备 */
    int  type;                 /* 设备类型：0=键盘, 1=鼠标 */
    int  protocol;             /* 当前协议（引导/报告） */
    int  interrupt_ep;         /* 中断输入端点地址 */
    int  interrupt_ep_size;    /* 中断端点最大包大小 */
    int  interval;             /* 轮询间隔 */

    /* 键盘状态 */
    hid_keyboard_report_t last_kbd_report;
    int  kbd_leds;             /* LED状态 */

    /* 鼠标状态 */
    hid_mouse_report_t last_mouse_report;
    int  mouse_x;              /* 累积X坐标 */
    int  mouse_y;              /* 累积Y坐标 */

    /* 回调函数 */
    void (*kbd_callback)(hid_keyboard_report_t *report);
    void (*mouse_callback)(hid_mouse_report_t *report);
} hid_device_t;

/* 函数声明 */
int usb_hid_init(void);
int usb_hid_probe(usb_device_t *dev);
int usb_hid_set_protocol(usb_device_t *dev, int protocol);
int usb_hid_set_idle(usb_device_t *dev, int duration, int report_id);
int usb_hid_get_report(usb_device_t *dev, int report_type, int report_id,
                        void *buffer, int length);
int usb_hid_set_report(usb_device_t *dev, int report_type, int report_id,
                        const void *buffer, int length);
int usb_hid_poll_keyboard(hid_device_t *hid, hid_keyboard_report_t *report);
int usb_hid_poll_mouse(hid_device_t *hid, hid_mouse_report_t *report);
void usb_hid_set_kbd_leds(hid_device_t *hid, uint8_t leds);

/* HID键盘码到ASCII转换 */
char hid_keycode_to_ascii(uint8_t keycode, uint8_t modifiers);

#endif /* _USB_HID_H */
