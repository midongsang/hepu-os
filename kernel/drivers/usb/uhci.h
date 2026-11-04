/* ============================================================================
 * 赫普OS - UHCI USB控制器驱动头文件
 * USB 1.1 Universal Host Controller Interface
 * V8.29: 初始实现
 * ========================================================================== */

#ifndef _UHCI_H
#define _UHCI_H

#include "usb_core.h"

/* UHCI寄存器偏移 */
#define UHCI_USBCMD      0x00  /* USB命令 */
#define UHCI_USBSTS      0x02  /* USB状态 */
#define UHCI_USBINTR     0x04  /* USB中断使能 */
#define UHCI_FRNUM       0x06  /* 帧号 */
#define UHCI_FRBASEADD   0x08  /* 帧列表基地址 */
#define UHCI_SOFMOD      0x0C  /* SOF调制 */
#define UHCI_PORTSC1     0x10  /* 端口1状态/控制 */
#define UHCI_PORTSC2     0x12  /* 端口2状态/控制 */

/* USBCMD位 */
#define UHCI_CMD_RS      0x0001  /* 运行/停止 */
#define UHCI_CMD_HCRESET 0x0002  /* HC重置 */
#define UHCI_CMD_GRESET  0x0004  /* 全局重置 */
#define UHCI_CMD_SUSPEND 0x0008  /* 进入挂起 */
#define UHCI_CMD_FGR     0x0010  /* 强制全局恢复 */
#define UHCI_CMD_SWDBG   0x0020  /* 软件调试 */
#define UHCI_CMD_CF      0x0040  /* 配置标志 */
#define UHCI_CMD_MAXP    0x0080  /* 最大包（64字节） */

/* USBSTS位 */
#define UHCI_STS_USBINT  0x0001  /* USB中断 */
#define UHCI_STS_USBERR  0x0002  /* USB错误中断 */
#define UHCI_STS_RD      0x0004  /* 恢复检测 */
#define UHCI_STS_HSE     0x0008  /* 主机系统错误 */
#define UHCI_STS_HCPE    0x0010  /* HC进程错误 */
#define UHCI_STS_HCH     0x0020  /* HC暂停 */

/* PORTSC位 */
#define UHCI_PORT_CCS    0x0001  /* 当前连接状态 */
#define UHCI_PORT_CSC    0x0002  /* 连接状态变化 */
#define UHCI_PORT_PED    0x0004  /* 端口使能/禁用 */
#define UHCI_PORT_PEDC   0x0008  /* 端口使能变化 */
#define UHCI_PORT_LS     0x0030  /* 线路状态 */
#define UHCI_PORT_RD     0x0040  /* 恢复检测 */
#define UHCI_PORT_LSDA   0x0100  /* 低速设备连接 */
#define UHCI_PORT_RESET  0x0200  /* 端口重置 */
#define UHCI_PORT_SUSP   0x1000  /* 端口挂起 */

/* UHCI传输描述符（TD） */
typedef struct uhci_td {
    uint32_t link;        /* 下一个TD的链接指针 */
    uint32_t status;      /* 状态和控制 */
    uint32_t token;       /* 令牌 */
    uint32_t buffer;      /* 缓冲区指针 */
} __attribute__((packed)) uhci_td_t;

/* UHCI队列头（QH） */
typedef struct uhci_qh {
    uint32_t link;        /* 下一个QH的链接指针 */
    uint32_t element;     /* 元素链接指针 */
} __attribute__((packed)) uhci_qh_t;

/* TD状态位 */
#define UHCI_TD_STS_ACTIVE   0x00000080  /* 活跃 */
#define UHCI_TD_STS_STALLED  0x00000040  /* 停滞 */
#define UHCI_TD_STS_DBUFERR  0x00000020  /* 数据缓冲区错误 */
#define UHCI_TD_STS_BABBLE   0x00000010  /* 错误 */
#define UHCI_TD_STS_NAK      0x00000008  /* NAK */
#define UHCI_TD_STS_CRCTO    0x00000004  /* CRC/超时错误 */
#define UHCI_TD_STS_BITSTUFF 0x00000002  /* 位填充错误 */

/* TD令牌 */
#define UHCI_TD_TOKEN_PID_OUT  0xE1  /* OUT令牌 */
#define UHCI_TD_TOKEN_PID_IN   0x69  /* IN令牌 */
#define UHCI_TD_TOKEN_PID_SETUP 0x2D /* SETUP令牌 */

/* UHCI控制器 */
typedef struct {
    usb_controller_t base;
    uint32_t io_base;
    uint8_t  irq;
    uint8_t  num_ports;

    /* 帧列表（1024个帧指针） */
    uint32_t *frame_list;
    uint32_t  frame_list_phys;

    /* 传输描述符池 */
    uhci_td_t *td_pool;
    int        td_count;
    int        td_next;

    /* 队列头 */
    uhci_qh_t *ctrl_qh;
    uhci_qh_t *bulk_qh;
    uhci_qh_t *int_qh;
} uhci_controller_t;

/* 函数声明 */
int uhci_init(usb_controller_t *ctrl);
int uhci_detect(void);
int uhci_reset_port(usb_controller_t *ctrl, int port);
int uhci_control_transfer(usb_controller_t *ctrl,
                           usb_device_t *dev,
                           usb_setup_packet_t *setup,
                           void *data);
int uhci_bulk_transfer(usb_controller_t *ctrl,
                        usb_device_t *dev,
                        int endpoint,
                        void *data, int length, int direction);
int uhci_interrupt_transfer(usb_controller_t *ctrl,
                             usb_device_t *dev,
                             int endpoint,
                             void *data, int length);

#endif /* _UHCI_H */
