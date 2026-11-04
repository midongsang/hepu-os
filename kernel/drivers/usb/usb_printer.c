/* ============================================================================
 * 赫普OS - USB打印机驱动实现
 * V8.59: 初始实现
 * 支持USB打印机类（Printer Class, 0x07）
 * 使用批量传输（Bulk Only Transport）
 * ========================================================================== */

#include "usb_printer.h"
#include "../../include/vga.h"
#include <string.h>

/* 最大打印机数量 */
#define MAX_PRINTERS 4

/* 全局打印机列表 */
static usb_printer_t printers[MAX_PRINTERS];
static int printer_count = 0;
static int initialized = 0;

/* 简单字符串函数 */
static int p_strlen(const char *s) {
    int n = 0;
    while (s && s[n]) n++;
    return n;
}

static void p_strcpy(char *dst, const char *src) {
    int i = 0;
    while (src[i]) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

static int p_strncmp(const char *a, const char *b, int n) {
    for (int i = 0; i < n; i++) {
        if (a[i] != b[i]) return (unsigned char)a[i] - (unsigned char)b[i];
        if (a[i] == 0) return 0;
    }
    return 0;
}

/* 解析设备ID字符串（IEEE 1284格式） */
static void parse_device_id(usb_printer_t *printer, const char *dev_id)
{
    if (!dev_id || !printer) return;

    /* 设备ID格式：长度(2字节) + "MANUFACTURER:xxx;MODEL:xxx;CMD:xxx;..." */
    const char *p = dev_id;

    /* 跳过长度字段（2字节大端） */
    if (p[0] == 0 && p[1] > 0) p += 2;

    /* 解析各字段 */
    while (*p) {
        /* 查找字段名 */
        const char *field_start = p;
        while (*p && *p != ':' && *p != ';') p++;

        if (*p == ':') {
            char field_name[32];
            int name_len = p - field_start;
            if (name_len > 31) name_len = 31;
            for (int i = 0; i < name_len; i++) field_name[i] = field_start[i];
            field_name[name_len] = 0;

            p++;  /* 跳过冒号 */

            /* 查找字段值 */
            const char *value_start = p;
            while (*p && *p != ';') p++;
            int value_len = p - value_start;

            /* 匹配字段 */
            if (p_strncmp(field_name, "MANUFACTURER", 12) == 0 ||
                p_strncmp(field_name, "MFG", 3) == 0) {
                if (value_len > 63) value_len = 63;
                for (int i = 0; i < value_len; i++) printer->manufacturer[i] = value_start[i];
                printer->manufacturer[value_len] = 0;
            } else if (p_strncmp(field_name, "MODEL", 5) == 0) {
                if (value_len > 63) value_len = 63;
                for (int i = 0; i < value_len; i++) printer->model[i] = value_start[i];
                printer->model[value_len] = 0;
            } else if (p_strncmp(field_name, "CMD", 3) == 0 ||
                       p_strncmp(field_name, "COMMAND SET", 11) == 0) {
                if (value_len > 31) value_len = 31;
                for (int i = 0; i < value_len; i++) printer->command_set[i] = value_start[i];
                printer->command_set[value_len] = 0;
            }
        }

        if (*p == ';') p++;  /* 跳过分号 */
    }
}

/* 初始化打印机驱动 */
int usb_printer_init(void)
{
    if (initialized) return 0;

    memset(printers, 0, sizeof(printers));
    printer_count = 0;
    initialized = 1;

    vga_puts("[PRINTER] USB printer driver initialized\n");
    return 0;
}

/* 打印机探测 */
int usb_printer_probe(usb_device_t *dev)
{
    if (!dev || printer_count >= MAX_PRINTERS) return -1;

    vga_printf("[PRINTER] Probing printer device (addr=%d)\n", dev->address);

    /* 查找空槽位 */
    usb_printer_t *printer = NULL;
    for (int i = 0; i < MAX_PRINTERS; i++) {
        if (!printers[i].usb_dev) {
            printer = &printers[i];
            break;
        }
    }
    if (!printer) return -1;

    memset(printer, 0, sizeof(usb_printer_t));
    printer->usb_dev = dev;
    printer->state = PRINTER_STATE_IDLE;
    printer->interface = 0;  /* 默认接口0 */

    /* 查找批量端点 */
    /* 简化实现：使用默认端点地址 */
    printer->ep_out = 0x01;  /* 批量输出端点 */
    printer->ep_in = 0x82;   /* 批量输入端点 */
    printer->max_packet_out = 64;
    printer->max_packet_in = 64;

    /* 获取设备ID */
    char dev_id_buf[256];
    if (usb_printer_get_device_id(printer, dev_id_buf, sizeof(dev_id_buf)) == 0) {
        p_strcpy(printer->device_id, dev_id_buf);
        parse_device_id(printer, dev_id_buf);
    }

    /* 获取端口状态 */
    uint8_t status;
    if (usb_printer_get_port_status(printer, &status) == 0) {
        printer->port_status = status;
        if (status & PRINTER_STATUS_PAPER_EMPTY) {
            printer->state = PRINTER_STATE_PAPER_EMPTY;
        } else if (!(status & PRINTER_STATUS_SELECTED)) {
            printer->state = PRINTER_STATE_OFFLINE;
        }
    }

    printer_count++;

    vga_printf("[PRINTER] Printer found: %s %s (CMD: %s)\n",
               printer->manufacturer[0] ? printer->manufacturer : "Unknown",
               printer->model[0] ? printer->model : "Unknown",
               printer->command_set[0] ? printer->command_set : "Unknown");

    return 0;
}

/* 打印机断开 */
int usb_printer_disconnect(usb_device_t *dev)
{
    if (!dev) return -1;

    for (int i = 0; i < MAX_PRINTERS; i++) {
        if (printers[i].usb_dev == dev) {
            vga_printf("[PRINTER] Printer disconnected: %s %s\n",
                       printers[i].manufacturer, printers[i].model);
            memset(&printers[i], 0, sizeof(usb_printer_t));
            printer_count--;
            return 0;
        }
    }
    return -1;
}

/* 获取设备ID */
int usb_printer_get_device_id(usb_printer_t *printer, char *buf, int buf_len)
{
    if (!printer || !printer->usb_dev || !buf || buf_len <= 0) return -1;

    usb_setup_packet_t setup;
    setup.bmRequestType = 0xA1;  /* Device-to-Host | Class | Interface */
    setup.bRequest = PRINTER_REQ_GET_DEVICE_ID;
    setup.wValue = 0;
    setup.wIndex = printer->interface;
    setup.wLength = buf_len;

    int ret = hepu_usb_control_request(printer->usb_dev,
                                        setup.bmRequestType,
                                        setup.bRequest,
                                        setup.wValue,
                                        setup.wIndex,
                                        buf,
                                        setup.wLength);
    if (ret < 0) {
        vga_printf("[PRINTER] GET_DEVICE_ID failed: %d\n", ret);
        return -1;
    }

    return 0;
}

/* 获取端口状态 */
int usb_printer_get_port_status(usb_printer_t *printer, uint8_t *status)
{
    if (!printer || !printer->usb_dev || !status) return -1;

    usb_setup_packet_t setup;
    setup.bmRequestType = 0xA1;  /* Device-to-Host | Class | Interface */
    setup.bRequest = PRINTER_REQ_GET_PORT_STATUS;
    setup.wValue = 0;
    setup.wIndex = printer->interface;
    setup.wLength = 1;

    int ret = hepu_usb_control_request(printer->usb_dev,
                                        setup.bmRequestType,
                                        setup.bRequest,
                                        setup.wValue,
                                        setup.wIndex,
                                        status,
                                        1);
    if (ret < 0) {
        vga_printf("[PRINTER] GET_PORT_STATUS failed: %d\n", ret);
        return -1;
    }

    return 0;
}

/* 软重置 */
int usb_printer_soft_reset(usb_printer_t *printer)
{
    if (!printer || !printer->usb_dev) return -1;

    usb_setup_packet_t setup;
    setup.bmRequestType = 0x21;  /* Host-to-Device | Class | Interface */
    setup.bRequest = PRINTER_REQ_SOFT_RESET;
    setup.wValue = 0;
    setup.wIndex = printer->interface;
    setup.wLength = 0;

    int ret = hepu_usb_control_request(printer->usb_dev,
                                        setup.bmRequestType,
                                        setup.bRequest,
                                        setup.wValue,
                                        setup.wIndex,
                                        NULL,
                                        0);
    if (ret < 0) {
        vga_printf("[PRINTER] SOFT_RESET failed: %d\n", ret);
        return -1;
    }

    printer->state = PRINTER_STATE_IDLE;
    vga_puts("[PRINTER] Soft reset completed\n");
    return 0;
}

/* 发送打印数据 */
int usb_printer_write(usb_printer_t *printer, const void *data, int length)
{
    if (!printer || !printer->usb_dev || !data || length <= 0) return -1;
    if (printer->state == PRINTER_STATE_PAPER_EMPTY ||
        printer->state == PRINTER_STATE_OFFLINE) {
        vga_printf("[PRINTER] Printer not ready (state=%d)\n", printer->state);
        return -1;
    }

    printer->state = PRINTER_STATE_BUSY;

    /* 使用批量输出传输发送数据 */
    int sent = 0;
    const uint8_t *ptr = (const uint8_t *)data;
    usb_controller_t *ctrl = (usb_controller_t *)printer->usb_dev->controller;

    while (sent < length) {
        int chunk = length - sent;
        if (chunk > printer->max_packet_out) chunk = printer->max_packet_out;

        int ret = ctrl->bulk_transfer(
            ctrl,
            printer->usb_dev,
            printer->ep_out,
            (void *)ptr,
            chunk,
            0  /* OUT方向 */
        );

        if (ret < 0) {
            vga_printf("[PRINTER] Bulk write failed at offset %d: %d\n", sent, ret);
            printer->state = PRINTER_STATE_ERROR;
            return -1;
        }

        sent += ret;
        ptr += ret;
    }

    printer->bytes_sent += sent;
    printer->state = PRINTER_STATE_IDLE;
    return sent;
}

/* 读取打印机状态数据 */
int usb_printer_read(usb_printer_t *printer, void *data, int length)
{
    if (!printer || !printer->usb_dev || !data || length <= 0) return -1;

    usb_controller_t *ctrl = (usb_controller_t *)printer->usb_dev->controller;
    int ret = ctrl->bulk_transfer(
        ctrl,
        printer->usb_dev,
        printer->ep_in,
        data,
        length,
        1  /* IN方向 */
    );

    return ret;
}

/* 获取打印机 */
usb_printer_t *usb_printer_get(int index)
{
    if (index < 0 || index >= printer_count) return NULL;
    int count = 0;
    for (int i = 0; i < MAX_PRINTERS; i++) {
        if (printers[i].usb_dev) {
            if (count == index) return &printers[i];
            count++;
        }
    }
    return NULL;
}

/* 获取打印机数量 */
int usb_printer_get_count(void)
{
    return printer_count;
}

/* 打印测试页 */
int usb_printer_print_test_page(usb_printer_t *printer)
{
    if (!printer) return -1;

    vga_printf("[PRINTER] Printing test page on %s %s\n",
               printer->manufacturer, printer->model);

    /* 简单的文本测试页（使用ESC/P命令） */
    static const char test_page[] =
        "\x1B" "@"              /* 初始化打印机 */
        "\x1B" "t" "\x01"       /* 选择字符集（美国） */
        "HepuOS Printer Test Page\r\n"
        "========================\r\n"
        "\r\n"
        "Printer: ";

    /* 发送测试页头部 */
    int ret = usb_printer_write(printer, test_page, sizeof(test_page) - 1);
    if (ret < 0) return -1;

    /* 发送打印机信息 */
    char info[128];
    int info_len = 0;
    const char *mfg = printer->manufacturer[0] ? printer->manufacturer : "Unknown";
    const char *mdl = printer->model[0] ? printer->model : "Unknown";
    while (*mfg && info_len < 60) info[info_len++] = *mfg++;
    info[info_len++] = ' ';
    while (*mdl && info_len < 120) info[info_len++] = *mdl++;
    info[info_len++] = '\r';
    info[info_len++] = '\n';

    ret = usb_printer_write(printer, info, info_len);
    if (ret < 0) return -1;

    /* 发送测试页尾部 */
    static const char footer[] =
        "\r\n"
        "Command Set: ";

    ret = usb_printer_write(printer, footer, sizeof(footer) - 1);
    if (ret < 0) return -1;

    const char *cmd = printer->command_set[0] ? printer->command_set : "Unknown";
    int cmd_len = 0;
    while (cmd[cmd_len] && cmd_len < 31) cmd_len++;
    ret = usb_printer_write(printer, cmd, cmd_len);
    if (ret < 0) return -1;

    /* 走纸换页 */
    static const char form_feed[] = "\r\n\r\n\r\n\x0C";  /* 走纸到下一页 */
    ret = usb_printer_write(printer, form_feed, sizeof(form_feed) - 1);
    if (ret < 0) return -1;

    printer->pages_printed++;
    vga_printf("[PRINTER] Test page sent (%d bytes total)\n", printer->bytes_sent);
    return 0;
}
