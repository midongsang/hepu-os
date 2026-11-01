/* ============================================================================
 * 赫普OS - L2CAP协议层（V8.69新增）
 * 逻辑链路控制和适配协议
 * ========================================================================== */

#ifndef _L2CAP_H
#define _L2CAP_H

#include <stdint.h>

/* L2CAP信道ID */
#define L2CAP_CID_SIGNALING    0x0001
#define L2CAP_CID_CONNECTIONLESS 0x0002
#define L2CAP_CID_ATT          0x0004
#define L2CAP_CID_LE_SIGNALING 0x0005
#define L2CAP_CID_SMP          0x0006

/* L2CAP协议/服务多路复用器 (PSM) */
#define L2CAP_PSM_SDP          0x0001
#define L2CAP_PSM_RFCOMM       0x0003
#define L2CAP_PSM_TCS          0x0005
#define L2CAP_PSM_CTP          0x0007
#define L2CAP_PSM_BNEP         0x000F
#define L2CAP_PSM_HID_CONTROL  0x0011
#define L2CAP_PSM_HID_INTERRUPT 0x0013
#define L2CAP_PSM_AVCTP        0x0017
#define L2CAP_PSM_AVDTP        0x0019

/* L2CAP信令代码 */
#define L2CAP_CMD_REJECT         0x01
#define L2CAP_CMD_CONN_REQ       0x02
#define L2CAP_CMD_CONN_RSP       0x03
#define L2CAP_CMD_CONFIG_REQ     0x04
#define L2CAP_CMD_CONFIG_RSP     0x05
#define L2CAP_CMD_DISCONN_REQ    0x06
#define L2CAP_CMD_DISCONN_RSP    0x07
#define L2CAP_CMD_ECHO_REQ       0x08
#define L2CAP_CMD_ECHO_RSP       0x09
#define L2CAP_CMD_INFO_REQ       0x0A
#define L2CAP_CMD_INFO_RSP       0x0B

/* L2CAP连接结果 */
#define L2CAP_CONN_SUCCESS       0x0000
#define L2CAP_CONN_PENDING       0x0001
#define L2CAP_CONN_REFUSED_PSM   0x0002
#define L2CAP_CONN_REFUSED_SEC   0x0003
#define L2CAP_CONN_REFUSED_RESOURCES 0x0004

/* L2CAP配置结果 */
#define L2CAP_CFG_SUCCESS        0x0000
#define L2CAP_CFG_FAIL_UNACCEPT  0x0001
#define L2CAP_CFG_FAIL_REJECT    0x0002
#define L2CAP_CFG_FAIL_UNKNOWN   0x0003

/* L2CAP信道状态 */
typedef enum {
    L2CAP_CHAN_CLOSED = 0,
    L2CAP_CHAN_CONNECTING,
    L2CAP_CHAN_CONFIGING,
    L2CAP_CHAN_OPEN,
    L2CAP_CHAN_DISCONNECTING
} l2cap_chan_state_t;

/* L2CAP信道 */
typedef struct {
    uint16_t local_cid;
    uint16_t remote_cid;
    uint16_t psm;
    l2cap_chan_state_t state;
    uint16_t mtu;
    uint16_t flush_timeout;
    uint8_t  identifier;
    void    *user_data;
} l2cap_channel_t;

/* L2CAP回调函数 */
typedef void (*l2cap_data_cb_t)(uint16_t cid, const uint8_t *data,
                                 uint16_t len, void *user_data);
typedef void (*l2cap_conn_cb_t)(uint16_t cid, uint16_t result,
                                 void *user_data);

/* L2CAP初始化 */
int l2cap_init(void);

/* 创建L2CAP信道 */
int l2cap_connect(uint16_t psm, l2cap_conn_cb_t callback, void *user_data);

/* 断开L2CAP信道 */
int l2cap_disconnect(uint16_t cid);

/* 发送L2CAP数据 */
int l2cap_send(uint16_t cid, const uint8_t *data, uint16_t len);

/* 注册数据接收回调 */
int l2cap_register_data_callback(uint16_t cid, l2cap_data_cb_t callback,
                                  void *user_data);

/* 处理HCI ACL数据（从HCI层接收） */
int l2cap_process_acl_data(uint16_t handle, const uint8_t *data, uint16_t len);

/* 获取信道信息 */
int l2cap_get_channel_info(uint16_t cid, l2cap_channel_t *info);

/* L2CAP统计 */
void l2cap_stats(uint32_t *channels, uint32_t *packets_sent,
                 uint32_t *packets_recv, uint32_t *bytes_sent,
                 uint32_t *bytes_recv);

#endif /* _L2CAP_H */
