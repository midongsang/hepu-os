#ifndef BLUETOOTH_H
#define BLUETOOTH_H

#include <stdint.h>

#define BT_MAX_DEVICES 8
#define BT_MAX_CONNECTIONS 4
#define BT_NAME_LEN 32
#define BT_ADDR_LEN 6

/* 蓝牙地址 */
typedef struct {
    uint8_t addr[BT_ADDR_LEN];
} bt_addr_t;

/* HCI包类型 */
#define HCI_CMD  0x01
#define HCI_ACL  0x02
#define HCI_SCO  0x03
#define HCI_EVT  0x04

/* HCI命令 */
#define HCI_INQUIRY           0x0401
#define HCI_INQUIRY_CANCEL    0x0402
#define HCI_CREATE_CONN       0x0405
#define HCI_DISCONNECT        0x0406
#define HCI_ACCEPT_CONN       0x0409
#define HCI_REJECT_CONN       0x040A
#define HCI_SET_LOCAL_NAME    0x0C13
#define HCI_READ_LOCAL_NAME   0x0C14
#define HCI_RESET             0x0C03
#define HCI_SET_SCAN_ENABLE   0x0C1A
#define HCI_READ_BD_ADDR      0x1009

/* L2CAP协议 */
#define L2CAP_SDP   0x0001
#define L2CAP_RFCOMM 0x0003
#define L2CAP_HID    0x0011
#define L2CAP_ATT    0x0004

/* RFCOMM信道 */
#define RFCOMM_SDP   1
#define RFCOMM_SPP   2

/* 设备状态 */
typedef enum {
    BT_DEV_NONE = 0,
    BT_DEV_INQUIRY,
    BT_DEV_CONNECTING,
    BT_DEV_CONNECTED,
    BT_DEV_DISCONNECTED,
} bt_dev_state_t;

/* 蓝牙设备 */
typedef struct {
    int used;
    bt_addr_t addr;
    char name[BT_NAME_LEN];
    uint32_t class_of_device;
    int rssi;
    bt_dev_state_t state;
    uint16_t handle;
    uint16_t mtu;
} bt_device_t;

/* L2CAP通道 */
typedef struct {
    int used;
    uint16_t cid;
    uint16_t psm;
    uint16_t mtu;
    int state;
    bt_device_t *device;
} bt_l2cap_chan_t;

/* RFCOMM会话 */
typedef struct {
    int used;
    uint8_t dlci;
    int channel;
    bt_l2cap_chan_t *l2cap;
    int connected;
} bt_rfcomm_t;

/* 蓝牙适配器 */
typedef struct {
    int initialized;
    bt_addr_t local_addr;
    char local_name[BT_NAME_LEN];
    int powered;
    int discoverable;
    int connectable;
    bt_device_t devices[BT_MAX_DEVICES];
    int device_count;
    bt_l2cap_chan_t l2cap_chans[BT_MAX_CONNECTIONS];
    bt_rfcomm_t rfcomm_sessions[BT_MAX_CONNECTIONS];
} bt_adapter_t;

/* 初始化蓝牙子系统 */
void bt_init(void);

/* 打开/关闭适配器 */
int bt_power_on(void);
int bt_power_off(void);

/* 设置本地名称 */
int bt_set_local_name(const char *name);

/* 设置可发现/可连接 */
int bt_set_discoverable(int enable);
int bt_set_connectable(int enable);

/* 设备扫描 */
int bt_start_inquiry(int duration);
int bt_cancel_inquiry(void);
int bt_get_devices(bt_device_t *list, int max);

/* 设备连接 */
int bt_connect(bt_addr_t *addr);
int bt_disconnect(uint16_t handle);
int bt_accept_connection(uint16_t handle);
int bt_reject_connection(uint16_t handle);

/* L2CAP */
int bt_l2cap_connect(bt_addr_t *addr, uint16_t psm);
int bt_l2cap_send(int chan_id, const void *data, uint16_t len);
int bt_l2cap_recv(int chan_id, void *data, uint16_t len);

/* RFCOMM (串口仿真) */
int bt_rfcomm_connect(bt_addr_t *addr, int channel);
int bt_rfcomm_send(int session_id, const void *data, uint16_t len);
int bt_rfcomm_recv(int session_id, void *data, uint16_t len);

/* SDP (服务发现) */
int bt_sdp_search(bt_addr_t *addr, uint16_t service_uuid);

/* 发送HCI命令 */
int bt_hci_send_cmd(uint16_t opcode, const void *params, uint8_t len);

/* 处理HCI事件 */
void bt_hci_handle_event(uint8_t *data, uint16_t len);

/* 获取本地地址 */
int bt_get_local_addr(bt_addr_t *addr);

/* 地址格式化 */
void bt_addr_to_str(bt_addr_t *addr, char *str);

#endif /* BLUETOOTH_H */
