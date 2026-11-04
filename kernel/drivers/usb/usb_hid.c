/* ============================================================================
 * 赫普OS - USB HID类驱动实现
 * V8.32: 初始实现（键盘/鼠标引导协议）
 * ========================================================================== */

#include "usb_hid.h"
#include "../../include/vga.h"

/* 内存分配 */
extern void *kmalloc(uint32_t size);
extern void *kfree(void *ptr);
extern void *memset(void *dst, int val, uint32_t n);
extern void *memcpy(void *dst, const void *src, uint32_t n);

/* 全局HID设备 */
#define MAX_HID_DEVICES 8
static hid_device_t hid_devices[MAX_HID_DEVICES];
static int num_hid_devices = 0;

/* HID键盘码到ASCII转换表（无修饰键） */
static const char hid_kbd_us[] = {
    0, 0, 0, 0, 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l',
    'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
    '1', '2', '3', '4', '5', '6', '7', '8', '9', '0',
    '\n', 0, '\b', '\t', ' ', '-', '=', '[', ']', '\\', 0, ';', '\'', '`',
    ',', '.', '/', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

/* HID键盘码到ASCII转换表（Shift修饰键） */
static const char hid_kbd_us_shift[] = {
    0, 0, 0, 0, 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L',
    'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
    '!', '@', '#', '$', '%', '^', '&', '*', '(', ')',
    '\n', 0, '\b', '\t', ' ', '_', '+', '{', '}', '|', 0, ':', '"', '~',
    '<', '>', '?', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

/* HID键盘码转ASCII */
char hid_keycode_to_ascii(uint8_t keycode, uint8_t modifiers)
{
    if (keycode >= sizeof(hid_kbd_us)) return 0;

    if (modifiers & (HID_KBD_MOD_LSHIFT | HID_KBD_MOD_RSHIFT)) {
        return hid_kbd_us_shift[keycode];
    }
    return hid_kbd_us[keycode];
}

/* 设置HID协议 */
int usb_hid_set_protocol(usb_device_t *dev, int protocol)
{
    if (!dev) return -1;

    return hepu_usb_control_request(dev,
        USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
        HID_REQ_SET_PROTOCOL,
        protocol, 0, NULL, 0);
}

/* 设置HID空闲速率 */
int usb_hid_set_idle(usb_device_t *dev, int duration, int report_id)
{
    if (!dev) return -1;

    return hepu_usb_control_request(dev,
        USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
        HID_REQ_SET_IDLE,
        (duration << 8) | report_id, 0, NULL, 0);
}

/* 获取HID报告 */
int usb_hid_get_report(usb_device_t *dev, int report_type, int report_id,
                        void *buffer, int length)
{
    if (!dev || !buffer || length <= 0) return -1;

    return hepu_usb_control_request(dev,
        USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
        HID_REQ_GET_REPORT,
        (report_type << 8) | report_id, 0, buffer, length);
}

/* 设置HID报告 */
int usb_hid_set_report(usb_device_t *dev, int report_type, int report_id,
                        const void *buffer, int length)
{
    if (!dev || !buffer || length <= 0) return -1;

    return hepu_usb_control_request(dev,
        USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
        HID_REQ_SET_REPORT,
        (report_type << 8) | report_id, 0, (void *)buffer, length);
}

/* 设置键盘LED */
void usb_hid_set_kbd_leds(hid_device_t *hid, uint8_t leds)
{
    if (!hid || hid->type != 0) return;

    uint8_t report = leds;
    usb_hid_set_report(hid->usb_dev, HID_REPORT_TYPE_OUTPUT, 0, &report, 1);
    hid->kbd_leds = leds;
}

/* 轮询键盘 */
int usb_hid_poll_keyboard(hid_device_t *hid, hid_keyboard_report_t *report)
{
    if (!hid || hid->type != 0 || !report) return -1;

    int ret = usb_hid_get_report(hid->usb_dev, HID_REPORT_TYPE_INPUT, 0,
                                  report, sizeof(hid_keyboard_report_t));
    if (ret >= 0) {
        memcpy(&hid->last_kbd_report, report, sizeof(hid_keyboard_report_t));
    }
    return ret;
}

/* 轮询鼠标 */
int usb_hid_poll_mouse(hid_device_t *hid, hid_mouse_report_t *report)
{
    if (!hid || hid->type != 1 || !report) return -1;

    int ret = usb_hid_get_report(hid->usb_dev, HID_REPORT_TYPE_INPUT, 0,
                                  report, sizeof(hid_mouse_report_t));
    if (ret >= 0) {
        memcpy(&hid->last_mouse_report, report, sizeof(hid_mouse_report_t));
        hid->mouse_x += report->x_displacement;
        hid->mouse_y += report->y_displacement;
    }
    return ret;
}

/* HID设备探测 */
int usb_hid_probe(usb_device_t *dev)
{
    if (!dev) return -1;

    vga_printf("[USB-HID] Probing device: VID=0x%04x PID=0x%04x class=0x%02x\n",
               dev->vendor_id, dev->product_id, dev->device_class);

    /* 检查是否是HID设备 */
    int is_hid = 0;
    int hid_type = -1;  /* 0=键盘, 1=鼠标 */

    if (dev->device_class == HEPU_USB_CLASS_HID) {
        is_hid = 1;
    }

    /* 检查接口描述符中的HID类 */
    for (int cfg = 0; cfg < dev->num_configs && cfg < USB_MAX_CONFIGS; cfg++) {
        for (int iface = 0; iface < dev->configs[cfg].num_interfaces; iface++) {
            usb_interface_t *itf = &dev->configs[cfg].interfaces[iface];
            if (itf->class_code == HEPU_USB_CLASS_HID) {
                is_hid = 1;
                if (itf->protocol == HID_ITF_PROTOCOL_KEYBOARD) {
                    hid_type = 0;
                } else if (itf->protocol == HID_ITF_PROTOCOL_MOUSE) {
                    hid_type = 1;
                }
            }
        }
    }

    if (!is_hid) {
        return -1;  /* 不是HID设备 */
    }

    if (num_hid_devices >= MAX_HID_DEVICES) {
        vga_puts("[USB-HID] Too many HID devices\n");
        return -1;
    }

    /* 分配HID设备结构 */
    hid_device_t *hid = &hid_devices[num_hid_devices++];
    memset(hid, 0, sizeof(hid_device_t));
    hid->usb_dev = dev;
    hid->type = hid_type;
    hid->protocol = HID_PROTOCOL_BOOT;

    /* 设置引导协议 */
    usb_hid_set_protocol(dev, HID_PROTOCOL_BOOT);

    /* 设置空闲速率（无限） */
    usb_hid_set_idle(dev, 0, 0);

    if (hid_type == 0) {
        vga_puts("[USB-HID] Keyboard detected\n");
        /* 初始化键盘LED */
        usb_hid_set_kbd_leds(hid, 0);
    } else if (hid_type == 1) {
        vga_puts("[USB-HID] Mouse detected\n");
        hid->mouse_x = 0;
        hid->mouse_y = 0;
    } else {
        vga_printf("[USB-HID] Generic HID device (protocol=%d)\n", hid_type);
    }

    return 0;
}

/* 初始化HID驱动 */
int usb_hid_init(void)
{
    memset(hid_devices, 0, sizeof(hid_devices));
    num_hid_devices = 0;

    vga_puts("[USB-HID] HID class driver initialized\n");
    return 0;
}

/* 获取HID设备数量 */
int usb_hid_get_count(void)
{
    return num_hid_devices;
}

/* 获取HID设备 */
hid_device_t *usb_hid_get_device(int index)
{
    if (index < 0 || index >= num_hid_devices) return NULL;
    return &hid_devices[index];
}

/* 兼容旧接口：USB HID键盘演示 */
void usb_hid_kbd_demo(void)
{
    vga_puts("[USB-HID] Keyboard demo (new HID driver, V8.32)\n");

    /* 列出所有HID设备 */
    for (int i = 0; i < num_hid_devices; i++) {
        hid_device_t *hid = &hid_devices[i];
        if (hid->type == 0) {
            vga_printf("[USB-HID] Keyboard #%d: VID=0x%04x PID=0x%04x\n",
                       i, hid->usb_dev->vendor_id, hid->usb_dev->product_id);
        } else if (hid->type == 1) {
            vga_printf("[USB-HID] Mouse #%d: VID=0x%04x PID=0x%04x\n",
                       i, hid->usb_dev->vendor_id, hid->usb_dev->product_id);
        }
    }

    if (num_hid_devices == 0) {
        vga_puts("[USB-HID] No HID devices connected\n");
    }
}
