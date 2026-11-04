/* ============================================================================
 * 赫普OS - L2CAP协议层实现（V8.69新增）
 * 逻辑链路控制和适配协议
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "../include/l2cap.h"
#include "../include/vga.h"
#include "../include/kheap.h"

/* L2CAP最大信道数 */
#define L2CAP_MAX_CHANNELS 16

/* L2CAP包头大小 */
#define L2CAP_HEADER_SIZE 4

/* 信道表 */
static l2cap_channel_t channels[L2CAP_MAX_CHANNELS];
static int num_channels = 0;
static uint16_t next_cid = 0x0040;  /* 动态CID从0x0040开始 */
static uint8_t next_identifier = 1;

/* 回调表 */
typedef struct {
    uint16_t cid;
    l2cap_data_cb_t data_cb;
    l2cap_conn_cb_t conn_cb;
    void *user_data;
} l2cap_callback_t;

static l2cap_callback_t callbacks[L2CAP_MAX_CHANNELS];

/* 统计 */
static uint32_t stat_packets_sent = 0;
static uint32_t stat_packets_recv = 0;
static uint32_t stat_bytes_sent = 0;
static uint32_t stat_bytes_recv = 0;

static uint8_t initialized = 0;

/* 外部HCI函数 */
extern int hci_send_acl_data(uint16_t handle, const uint8_t *data, uint16_t len);

/* 初始化L2CAP */
int l2cap_init(void)
{
    if (initialized) return 0;

    memset(channels, 0, sizeof(channels));
    memset(callbacks, 0, sizeof(callbacks));
    num_channels = 0;
    next_cid = 0x0040;
    next_identifier = 1;
    stat_packets_sent = 0;
    stat_packets_recv = 0;
    stat_bytes_sent = 0;
    stat_bytes_recv = 0;

    initialized = 1;
    vga_puts("[L2CAP] L2CAP layer initialized\n");
    return 0;
}

/* 查找空闲信道槽位 */
static int find_free_channel(void)
{
    for (int i = 0; i < L2CAP_MAX_CHANNELS; i++) {
        if (channels[i].state == L2CAP_CHAN_CLOSED) {
            return i;
        }
    }
    return -1;
}

/* 按CID查找信道 */
static l2cap_channel_t *find_channel_by_cid(uint16_t cid)
{
    for (int i = 0; i < L2CAP_MAX_CHANNELS; i++) {
        if (channels[i].local_cid == cid &&
            channels[i].state != L2CAP_CHAN_CLOSED) {
            return &channels[i];
        }
    }
    return NULL;
}

/* 构建L2CAP包头 */
static void build_l2cap_header(uint8_t *buf, uint16_t length, uint16_t cid)
{
    buf[0] = length & 0xFF;
    buf[1] = (length >> 8) & 0xFF;
    buf[2] = cid & 0xFF;
    buf[3] = (cid >> 8) & 0xFF;
}

/* 发送L2CAP信令命令 */
static int l2cap_send_command(uint16_t cid, uint8_t code,
                               uint8_t identifier, const uint8_t *data,
                               uint16_t data_len)
{
    uint8_t buf[256];
    uint16_t total_len = 4 + data_len;  /* 信令头4字节 + 数据 */

    if (total_len + L2CAP_HEADER_SIZE > sizeof(buf)) return -1;

    /* L2CAP头 */
    build_l2cap_header(buf, total_len, L2CAP_CID_SIGNALING);

    /* 信令头 */
    buf[4] = code;
    buf[5] = identifier;
    buf[6] = data_len & 0xFF;
    buf[7] = (data_len >> 8) & 0xFF;

    /* 信令数据 */
    if (data && data_len > 0) {
        memcpy(buf + 8, data, data_len);
    }

    stat_packets_sent++;
    stat_bytes_sent += total_len + L2CAP_HEADER_SIZE;

    /* 简化：直接返回，实际应通过HCI发送 */
    return total_len + L2CAP_HEADER_SIZE;
}

/* 创建L2CAP连接 */
int l2cap_connect(uint16_t psm, l2cap_conn_cb_t callback, void *user_data)
{
    if (!initialized) l2cap_init();

    int idx = find_free_channel();
    if (idx < 0) return -1;

    l2cap_channel_t *chan = &channels[idx];
    memset(chan, 0, sizeof(l2cap_channel_t));

    chan->local_cid = next_cid++;
    chan->remote_cid = 0;
    chan->psm = psm;
    chan->state = L2CAP_CHAN_CONNECTING;
    chan->mtu = 672;  /* 默认MTU */
    chan->flush_timeout = 0xFFFF;
    chan->identifier = next_identifier++;
    chan->user_data = user_data;

    /* 注册回调 */
    callbacks[idx].cid = chan->local_cid;
    callbacks[idx].conn_cb = callback;
    callbacks[idx].user_data = user_data;

    num_channels++;

    /* 构建连接请求 */
    uint8_t conn_req[4];
    conn_req[0] = psm & 0xFF;
    conn_req[1] = (psm >> 8) & 0xFF;
    conn_req[2] = chan->local_cid & 0xFF;
    conn_req[3] = (chan->local_cid >> 8) & 0xFF;

    l2cap_send_command(L2CAP_CID_SIGNALING, L2CAP_CMD_CONN_REQ,
                       chan->identifier, conn_req, 4);

    vga_printf("[L2CAP] Connect request: CID=0x%04X PSM=0x%04X\n",
               chan->local_cid, psm);
    return chan->local_cid;
}

/* 断开L2CAP连接 */
int l2cap_disconnect(uint16_t cid)
{
    l2cap_channel_t *chan = find_channel_by_cid(cid);
    if (!chan) return -1;

    chan->state = L2CAP_CHAN_DISCONNECTING;

    /* 构建断开请求 */
    uint8_t disconn_req[4];
    disconn_req[0] = cid & 0xFF;
    disconn_req[1] = (cid >> 8) & 0xFF;
    disconn_req[2] = chan->remote_cid & 0xFF;
    disconn_req[3] = (chan->remote_cid >> 8) & 0xFF;

    l2cap_send_command(L2CAP_CID_SIGNALING, L2CAP_CMD_DISCONN_REQ,
                       next_identifier++, disconn_req, 4);

    vga_printf("[L2CAP] Disconnect: CID=0x%04X\n", cid);
    return 0;
}

/* 发送L2CAP数据 */
int l2cap_send(uint16_t cid, const uint8_t *data, uint16_t len)
{
    if (!data || len == 0) return -1;

    l2cap_channel_t *chan = find_channel_by_cid(cid);
    if (!chan || chan->state != L2CAP_CHAN_OPEN) return -2;

    if (len > chan->mtu) return -3;

    uint8_t buf[1024];
    if (len + L2CAP_HEADER_SIZE > sizeof(buf)) return -4;

    /* L2CAP头 */
    build_l2cap_header(buf, len, chan->remote_cid);

    /* 数据 */
    memcpy(buf + L2CAP_HEADER_SIZE, data, len);

    stat_packets_sent++;
    stat_bytes_sent += len + L2CAP_HEADER_SIZE;

    /* 简化：直接返回，实际应通过HCI发送 */
    return len + L2CAP_HEADER_SIZE;
}

/* 注册数据接收回调 */
int l2cap_register_data_callback(uint16_t cid, l2cap_data_cb_t callback,
                                  void *user_data)
{
    for (int i = 0; i < L2CAP_MAX_CHANNELS; i++) {
        if (callbacks[i].cid == cid) {
            callbacks[i].data_cb = callback;
            callbacks[i].user_data = user_data;
            return 0;
        }
    }
    return -1;
}

/* 处理连接响应 */
static void handle_conn_rsp(const uint8_t *data, uint16_t len)
{
    if (len < 8) return;

    uint16_t remote_cid = data[0] | (data[1] << 8);
    uint16_t local_cid = data[2] | (data[3] << 8);
    uint16_t result = data[4] | (data[5] << 8);
    uint16_t status = data[6] | (data[7] << 8);

    l2cap_channel_t *chan = find_channel_by_cid(local_cid);
    if (!chan) return;

    if (result == L2CAP_CONN_SUCCESS) {
        chan->remote_cid = remote_cid;
        chan->state = L2CAP_CHAN_CONFIGING;
        vga_printf("[L2CAP] Connected: local=0x%04X remote=0x%04X\n",
                   local_cid, remote_cid);

        /* 发送配置请求 */
        uint8_t cfg_req[8];
        cfg_req[0] = remote_cid & 0xFF;
        cfg_req[1] = (remote_cid >> 8) & 0xFF;
        cfg_req[2] = 0;  /* flags */
        cfg_req[3] = 0;
        /* MTU配置选项 */
        cfg_req[4] = 0x01;  /* 类型：MTU */
        cfg_req[5] = 0x02;  /* 长度 */
        cfg_req[6] = chan->mtu & 0xFF;
        cfg_req[7] = (chan->mtu >> 8) & 0xFF;

        l2cap_send_command(L2CAP_CID_SIGNALING, L2CAP_CMD_CONFIG_REQ,
                           next_identifier++, cfg_req, 8);
    } else {
        chan->state = L2CAP_CHAN_CLOSED;
        vga_printf("[L2CAP] Connect failed: result=0x%04X\n", result);
    }

    /* 调用连接回调 */
    for (int i = 0; i < L2CAP_MAX_CHANNELS; i++) {
        if (callbacks[i].cid == local_cid && callbacks[i].conn_cb) {
            callbacks[i].conn_cb(local_cid, result, callbacks[i].user_data);
            break;
        }
    }
}

/* 处理配置响应 */
static void handle_config_rsp(const uint8_t *data, uint16_t len)
{
    if (len < 6) return;

    uint16_t local_cid = data[0] | (data[1] << 8);
    uint16_t result = data[4] | (data[5] << 8);

    l2cap_channel_t *chan = find_channel_by_cid(local_cid);
    if (!chan) return;

    if (result == L2CAP_CFG_SUCCESS) {
        chan->state = L2CAP_CHAN_OPEN;
        vga_printf("[L2CAP] Channel open: CID=0x%04X MTU=%d\n",
                   local_cid, chan->mtu);
    }
}

/* 处理断开响应 */
static void handle_disconn_rsp(const uint8_t *data, uint16_t len)
{
    if (len < 4) return;

    uint16_t local_cid = data[0] | (data[1] << 8);

    l2cap_channel_t *chan = find_channel_by_cid(local_cid);
    if (!chan) return;

    chan->state = L2CAP_CHAN_CLOSED;
    vga_printf("[L2CAP] Channel closed: CID=0x%04X\n", local_cid);
}

/* 处理L2CAP信令 */
static void l2cap_process_signaling(const uint8_t *data, uint16_t len)
{
    if (len < 4) return;

    uint8_t code = data[0];
    uint8_t identifier = data[1];
    uint16_t cmd_len = data[2] | (data[3] << 8);

    if (cmd_len + 4 > len) return;

    const uint8_t *cmd_data = data + 4;

    switch (code) {
        case L2CAP_CMD_CONN_RSP:
            handle_conn_rsp(cmd_data, cmd_len);
            break;
        case L2CAP_CMD_CONFIG_RSP:
            handle_config_rsp(cmd_data, cmd_len);
            break;
        case L2CAP_CMD_DISCONN_RSP:
            handle_disconn_rsp(cmd_data, cmd_len);
            break;
        case L2CAP_CMD_ECHO_RSP:
            vga_puts("[L2CAP] Echo response received\n");
            break;
        default:
            vga_printf("[L2CAP] Unknown signaling code: 0x%02X\n", code);
            break;
    }
}

/* 处理HCI ACL数据 */
int l2cap_process_acl_data(uint16_t handle, const uint8_t *data, uint16_t len)
{
    if (!data || len < L2CAP_HEADER_SIZE) return -1;

    stat_packets_recv++;
    stat_bytes_recv += len;

    /* 解析L2CAP头 */
    uint16_t payload_len = data[0] | (data[1] << 8);
    uint16_t cid = data[2] | (data[3] << 8);

    if (payload_len + L2CAP_HEADER_SIZE > len) return -2;

    const uint8_t *payload = data + L2CAP_HEADER_SIZE;

    if (cid == L2CAP_CID_SIGNALING) {
        /* 信令信道 */
        l2cap_process_signaling(payload, payload_len);
    } else {
        /* 数据信道：调用回调 */
        l2cap_channel_t *chan = find_channel_by_cid(cid);
        if (chan && chan->state == L2CAP_CHAN_OPEN) {
            for (int i = 0; i < L2CAP_MAX_CHANNELS; i++) {
                if (callbacks[i].cid == cid && callbacks[i].data_cb) {
                    callbacks[i].data_cb(cid, payload, payload_len,
                                         callbacks[i].user_data);
                    break;
                }
            }
        }
    }

    return 0;
}

/* 获取信道信息 */
int l2cap_get_channel_info(uint16_t cid, l2cap_channel_t *info)
{
    l2cap_channel_t *chan = find_channel_by_cid(cid);
    if (!chan || !info) return -1;
    memcpy(info, chan, sizeof(l2cap_channel_t));
    return 0;
}

/* L2CAP统计 */
void l2cap_stats(uint32_t *num_channels, uint32_t *packets_sent,
                 uint32_t *packets_recv, uint32_t *bytes_sent,
                 uint32_t *bytes_recv)
{
    uint32_t active = 0;
    for (int i = 0; i < L2CAP_MAX_CHANNELS; i++) {
        if (channels[i].state != L2CAP_CHAN_CLOSED) active++;
    }
    if (num_channels) *num_channels = active;
    if (packets_sent) *packets_sent = stat_packets_sent;
    if (packets_recv) *packets_recv = stat_packets_recv;
    if (bytes_sent) *bytes_sent = stat_bytes_sent;
    if (bytes_recv) *bytes_recv = stat_bytes_recv;
}
