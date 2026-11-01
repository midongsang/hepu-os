/* ============================================================================
 * 赫普OS - RFCOMM协议层（V8.69新增）
 * 串口仿真协议（基于L2CAP）
 * ========================================================================== */

#ifndef _RFCOMM_H
#define _RFCOMM_H

#include <stdint.h>

/* RFCOMM信道 */
#define RFCOMM_DLCI_CONTROL  0
#define RFCOMM_DLCI_MIN      2
#define RFCOMM_DLCI_MAX      61

/* RFCOMM帧类型 */
#define RFCOMM_SABME  0x2F
#define RFCOMM_UA     0x63
#define RFCOMM_DM     0x0F
#define RFCOMM_DISC   0x43
#define RFCOMM_UIH    0xEF
#define RFCOMM_UI     0x03

/* RFCOMM MCC（多路复用器控制命令） */
#define RFCOMM_MCC_PN       0x80  /* 端口协商 */
#define RFCOMM_MCC_MSC      0xE0  /* 调制解调器状态命令 */
#define RFCOMM_MCC_RPN      0x90  /* 远程端口参数 */
#define RFCOMM_MCC_RLS      0x50  /* 远程线路状态 */
#define RFCOMM_MCC_FCON     0xA0  /* 流量控制开启 */
#define RFCOMM_MCC_FCOFF    0x60  /* 流量控制关闭 */
#define RFCOMM_MCC_NSC      0x10  /* 不支持的命令 */
#define RFCOMM_MCC_TEST     0x20  /* 测试 */

/* RFCOMM端口参数 */
typedef struct {
    uint8_t  baud_rate;     /* 0=2400, 1=4800, 2=9600, 3=19200, 4=38400, 5=57600, 6=115200 */
    uint8_t  data_bits;     /* 0=5, 1=6, 2=7, 3=8 */
    uint8_t  stop_bits;     /* 0=1, 1=1.5, 2=2 */
    uint8_t  parity;        /* 0=none, 1=odd, 2=even, 3=mark, 4=space */
    uint8_t  flow_control;  /* bit0=XON/XOFF input, bit1=XON/XOFF output, bit2=RTS input, bit3=RTS output */
    uint8_t  xon_char;
    uint8_t  xoff_char;
    uint16_t mtu;
} rfcomm_port_params_t;

/* RFCOMM调制解调器状态 */
typedef struct {
    uint8_t dtr;  /* 数据终端就绪 */
    uint8_t rts;  /* 请求发送 */
    uint8_t dcd;  /* 数据载波检测 */
    uint8_t dsr;  /* 数据设备就绪 */
    uint8_t cts;  /* 清除发送 */
    uint8_t ri;   /* 振铃指示 */
} rfcomm_modem_status_t;

/* RFCOMM会话状态 */
typedef enum {
    RFCOMM_SESSION_CLOSED = 0,
    RFCOMM_SESSION_OPENING,
    RFCOMM_SESSION_OPEN,
    RFCOMM_SESSION_CLOSING
} rfcomm_session_state_t;

/* RFCOMM会话 */
typedef struct {
    uint16_t l2cap_cid;
    uint8_t  dlci;
    rfcomm_session_state_t state;
    rfcomm_port_params_t port_params;
    rfcomm_modem_status_t modem_status;
    uint32_t bytes_sent;
    uint32_t bytes_recv;
} rfcomm_session_t;

/* RFCOMM回调 */
typedef void (*rfcomm_data_cb_t)(uint8_t dlci, const uint8_t *data,
                                  uint16_t len, void *user_data);
typedef void (*rfcomm_conn_cb_t)(uint8_t dlci, int result, void *user_data);

/* 初始化RFCOMM */
int rfcomm_init(void);

/* 创建RFCOMM会话（串口连接） */
int rfcomm_connect(uint8_t dlci, rfcomm_conn_cb_t callback, void *user_data);

/* 断开RFCOMM会话 */
int rfcomm_disconnect(uint8_t dlci);

/* 发送数据（串口写入） */
int rfcomm_send(uint8_t dlci, const uint8_t *data, uint16_t len);

/* 注册数据接收回调 */
int rfcomm_register_data_callback(uint8_t dlci, rfcomm_data_cb_t callback,
                                   void *user_data);

/* 设置端口参数 */
int rfcomm_set_port_params(uint8_t dlci, const rfcomm_port_params_t *params);

/* 获取端口参数 */
int rfcomm_get_port_params(uint8_t dlci, rfcomm_port_params_t *params);

/* 设置调制解调器状态 */
int rfcomm_set_modem_status(uint8_t dlci, const rfcomm_modem_status_t *status);

/* 处理L2CAP数据（从L2CAP层接收） */
int rfcomm_process_l2cap_data(uint16_t cid, const uint8_t *data, uint16_t len);

/* 获取会话信息 */
int rfcomm_get_session_info(uint8_t dlci, rfcomm_session_t *info);

/* RFCOMM统计 */
void rfcomm_stats(uint32_t *sessions, uint32_t *bytes_sent,
                  uint32_t *bytes_recv);

#endif /* _RFCOMM_H */
