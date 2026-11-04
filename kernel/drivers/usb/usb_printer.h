/* ============================================================================
 * 赫普OS - USB打印机驱动头文件
 * V8.59: 初始实现
 * 支持USB打印机类（Printer Class, 0x07）
 * ========================================================================== */

#ifndef _USB_PRINTER_H
#define _USB_PRINTER_H

#include <stdint.h>
#include "usb_core.h"

/* 打印机特定请求 */
#define PRINTER_REQ_GET_DEVICE_ID    0x00
#define PRINTER_REQ_GET_PORT_STATUS  0x01
#define PRINTER_REQ_SOFT_RESET       0x02

/* 端口状态位 */
#define PRINTER_STATUS_NO_ERROR       0x00
#define PRINTER_STATUS_PAPER_EMPTY    0x20
#define PRINTER_STATUS_SELECTED       0x10
#define PRINTER_STATUS_NOT_ERROR      0x08

/* 打印机状态 */
typedef enum {
    PRINTER_STATE_IDLE = 0,
    PRINTER_STATE_BUSY,
    PRINTER_STATE_ERROR,
    PRINTER_STATE_PAPER_EMPTY,
    PRINTER_STATE_OFFLINE
} printer_state_t;

/* 打印机设备结构 */
typedef struct {
    usb_device_t *usb_dev;       /* USB设备 */
    uint8_t  interface;          /* 接口号 */
    uint8_t  ep_out;             /* 批量输出端点 */
    uint8_t  ep_in;              /* 批量输入端点 */
    uint16_t max_packet_out;     /* 输出最大包大小 */
    uint16_t max_packet_in;      /* 输入最大包大小 */

    printer_state_t state;       /* 打印机状态 */
    uint8_t  port_status;        /* 端口状态 */

    char     device_id[256];     /* 设备ID字符串（IEEE 1284） */
    char     manufacturer[64];   /* 制造商 */
    char     model[64];          /* 型号 */
    char     command_set[32];    /* 支持的命令集（PCL/PS/PDF等） */

    uint32_t pages_printed;      /* 已打印页数 */
    uint32_t bytes_sent;         /* 已发送字节数 */
} usb_printer_t;

/* 函数声明 */

/* 初始化打印机驱动 */
int usb_printer_init(void);

/* 打印机探测（由USB核心调用） */
int usb_printer_probe(usb_device_t *dev);

/* 打印机断开（由USB核心调用） */
int usb_printer_disconnect(usb_device_t *dev);

/* 获取打印机设备ID */
int usb_printer_get_device_id(usb_printer_t *printer, char *buf, int buf_len);

/* 获取打印机端口状态 */
int usb_printer_get_port_status(usb_printer_t *printer, uint8_t *status);

/* 打印机软重置 */
int usb_printer_soft_reset(usb_printer_t *printer);

/* 发送打印数据 */
int usb_printer_write(usb_printer_t *printer, const void *data, int length);

/* 读取打印机状态数据 */
int usb_printer_read(usb_printer_t *printer, void *data, int length);

/* 获取打印机列表中的打印机 */
usb_printer_t *usb_printer_get(int index);

/* 获取打印机数量 */
int usb_printer_get_count(void);

/* 打印测试页（简单文本） */
int usb_printer_print_test_page(usb_printer_t *printer);

#endif /* _USB_PRINTER_H */
