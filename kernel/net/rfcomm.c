/* ============================================================================
 * 赫普OS - RFCOMM协议层实现（V8.69新增）
 * 串口仿真协议（基于L2CAP）
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "../include/rfcomm.h"
#include "../include/l2cap.h"
#include "../include/vga.h"
#include "../include/kheap.h"

/* RFCOMM最大会话数 */
#define RFCOMM_MAX_SESSIONS 8

/* RFCOMM会话表 */
static rfcomm_session_t sessions[RFCOMM_MAX_SESSIONS];
static int num_sessions = 0;
static uint8_t initialized = 0;

/* 回调表 */
typedef struct {
    uint8_t dlci;
    rfcomm_data_cb_t data_cb;
    rfcomm_conn_cb_t conn_cb;
    void *user_data;
} rfcomm_callback_t;

static rfcomm_callback_t callbacks[RFCOMM_MAX_SESSIONS];

/* 统计 */
static uint32_t stat_bytes_sent = 0;
static uint32_t stat_bytes_recv = 0;

/* 初始化RFCOMM */
int rfcomm_init(void)
{
    if (initialized) return 0;

    memset(sessions, 0, sizeof(sessions));
    memset(callbacks, 0, sizeof(callbacks));
    num_sessions = 0;
    stat_bytes_sent = 0;
    stat_bytes_recv = 0;

    initialized = 1;
    vga_puts("[RFCOMM] RFCOMM layer initialized\n");
    return 0;
}

/* 查找空闲会话槽位 */
static int find_free_session(void)
{
    for (int i = 0; i < RFCOMM_MAX_SESSIONS; i++) {
        if (sessions[i].state == RFCOMM_SESSION_CLOSED) {
            return i;
        }
    }
    return -1;
}

/* 按DLCI查找会话 */
static rfcomm_session_t *find_session_by_dlci(uint8_t dlci)
{
    for (int i = 0; i < RFCOMM_MAX_SESSIONS; i++) {
        if (sessions[i].dlci == dlci &&
            sessions[i].state != RFCOMM_SESSION_CLOSED) {
            return &sessions[i];
        }
    }
    return NULL;
}

/* 计算FCS（帧校验序列） */
static uint8_t rfcomm_calc_fcs(const uint8_t *data, int len)
{
    uint8_t fcs = 0xFF;
    for (int i = 0; i < len; i++) {
        fcs ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (fcs & 0x01) {
                fcs = (fcs >> 1) ^ 0xE1;
            } else {
                fcs >>= 1;
            }
        }
    }
    return ~fcs;
}

/* 发送RFCOMM帧 */
static int rfcomm_send_frame(uint16_t l2cap_cid, uint8_t address,
                              uint8_t control, const uint8_t *data,
                              uint16_t len)
{
    uint8_t buf[512];
    int pos = 0;

    /* 地址字段 */
    buf[pos++] = address | 0x01;  /* EA=1 */

    /* 控制字段 */
    buf[pos++] = control;

    /* 长度字段（简化：单字节） */
    uint8_t length = (len << 1) | 0x01;  /* EA=1 */
    buf[pos++] = length;

    /* 数据 */
    if (data && len > 0) {
        memcpy(buf + pos, data, len);
        pos += len;
    }

    /* FCS */
    buf[pos++] = rfcomm_calc_fcs(buf, pos);

    /* 通过L2CAP发送 */
    int ret = l2cap_send(l2cap_cid, buf, pos);
    if (ret > 0) {
        stat_bytes_sent += len;
    }
    return ret;
}

/* 发送SABME（设置异步平衡模式扩展） */
static int rfcomm_send_sabme(uint16_t l2cap_cid, uint8_t dlci)
{
    uint8_t address = (dlci << 2) | 0x03;  /* C/R=1, EA=1 */
    return rfcomm_send_frame(l2cap_cid, address, RFCOMM_SABME, NULL, 0);
}

/* 发送UA（无编号确认） */
static int rfcomm_send_ua(uint16_t l2cap_cid, uint8_t dlci)
{
    uint8_t address = (dlci << 2) | 0x03;
    return rfcomm_send_frame(l2cap_cid, address, RFCOMM_UA, NULL, 0);
}

/* 发送DISC（断开） */
static int rfcomm_send_disc(uint16_t l2cap_cid, uint8_t dlci)
{
    uint8_t address = (dlci << 2) | 0x03;
    return rfcomm_send_frame(l2cap_cid, address, RFCOMM_DISC, NULL, 0);
}

/* 发送端口协商（PN） */
static int rfcomm_send_pn(uint16_t l2cap_cid, uint8_t dlci,
                           const rfcomm_port_params_t *params)
{
    uint8_t data[8];
    data[0] = dlci;
    data[1] = 0x00;  /* flow control */
    data[2] = 0x00;  /* priority */
    data[3] = 0x00;  /* ack timer */
    data[4] = params->mtu & 0xFF;
    data[5] = (params->mtu >> 8) & 0xFF;
    data[6] = 0x00;  /* max retrans */
    data[7] = 0x00;  /* error recovery */

    /* MCC帧：类型=PN, 长度=8 */
    uint8_t mcc[10];
    mcc[0] = RFCOMM_MCC_PN | 0x01;  /* EA=1 */
    mcc[1] = 8 | 0x01;  /* 长度，EA=1 */
    memcpy(mcc + 2, data, 8);

    uint8_t address = (RFCOMM_DLCI_CONTROL << 2) | 0x01;  /* C/R=0, EA=1 */
    return rfcomm_send_frame(l2cap_cid, address, RFCOMM_UIH, mcc, 10);
}

/* 发送调制解调器状态命令（MSC） */
static int rfcomm_send_msc(uint16_t l2cap_cid, uint8_t dlci,
                            const rfcomm_modem_status_t *status)
{
    uint8_t data[3];
    data[0] = dlci << 1 | 0x01;  /* EA=1 */
    data[1] = 0x00;  /* 信号位（简化） */
    data[2] = 0x00;  /* 中断信号 */

    if (status->dtr) data[1] |= 0x04;
    if (status->rts) data[1] |= 0x02;
    if (status->dcd) data[1] |= 0x80;
    if (status->dsr) data[1] |= 0x20;
    if (status->cts) data[1] |= 0x10;
    if (status->ri)  data[1] |= 0x40;

    /* MCC帧：类型=MSC, 长度=3 */
    uint8_t mcc[5];
    mcc[0] = RFCOMM_MCC_MSC | 0x01;
    mcc[1] = 3 | 0x01;
    memcpy(mcc + 2, data, 3);

    uint8_t address = (RFCOMM_DLCI_CONTROL << 2) | 0x01;
    return rfcomm_send_frame(l2cap_cid, address, RFCOMM_UIH, mcc, 5);
}

/* L2CAP连接回调 */
static void rfcomm_l2cap_conn_cb(uint16_t cid, uint16_t result, void *user_data)
{
    uint8_t dlci = (uint8_t)(uintptr_t)user_data;

    if (result == L2CAP_CONN_SUCCESS) {
        rfcomm_session_t *sess = find_session_by_dlci(dlci);
        if (sess) {
            sess->l2cap_cid = cid;
            sess->state = RFCOMM_SESSION_OPENING;

            /* 发送SABME建立数据链路 */
            rfcomm_send_sabme(cid, dlci);

            /* 发送端口协商 */
            rfcomm_send_pn(cid, dlci, &sess->port_params);

            vga_printf("[RFCOMM] L2CAP connected for DLCI=%d, CID=0x%04X\n",
                       dlci, cid);
        }
    } else {
        vga_printf("[RFCOMM] L2CAP connect failed for DLCI=%d\n", dlci);
    }

    /* 调用RFCOMM连接回调 */
    for (int i = 0; i < RFCOMM_MAX_SESSIONS; i++) {
        if (callbacks[i].dlci == dlci && callbacks[i].conn_cb) {
            callbacks[i].conn_cb(dlci, result, callbacks[i].user_data);
            break;
        }
    }
}

/* L2CAP数据回调 */
static void rfcomm_l2cap_data_cb(uint16_t cid, const uint8_t *data,
                                  uint16_t len, void *user_data)
{
    rfcomm_process_l2cap_data(cid, data, len);
}

/* 创建RFCOMM会话 */
int rfcomm_connect(uint8_t dlci, rfcomm_conn_cb_t callback, void *user_data)
{
    if (!initialized) rfcomm_init();
    if (dlci < RFCOMM_DLCI_MIN || dlci > RFCOMM_DLCI_MAX) return -1;

    int idx = find_free_session();
    if (idx < 0) return -2;

    rfcomm_session_t *sess = &sessions[idx];
    memset(sess, 0, sizeof(rfcomm_session_t));

    sess->dlci = dlci;
    sess->state = RFCOMM_SESSION_OPENING;
    sess->port_params.baud_rate = 6;  /* 115200 */
    sess->port_params.data_bits = 3;  /* 8 bits */
    sess->port_params.stop_bits = 0;  /* 1 stop bit */
    sess->port_params.parity = 0;     /* no parity */
    sess->port_params.mtu = 127;

    /* 注册回调 */
    callbacks[idx].dlci = dlci;
    callbacks[idx].conn_cb = callback;
    callbacks[idx].user_data = user_data;

    num_sessions++;

    /* 创建L2CAP连接（PSM=RFCOMM） */
    int cid = l2cap_connect(L2CAP_PSM_RFCOMM, rfcomm_l2cap_conn_cb,
                            (void *)(uintptr_t)dlci);
    if (cid < 0) {
        sess->state = RFCOMM_SESSION_CLOSED;
        return -3;
    }

    /* 注册L2CAP数据回调 */
    l2cap_register_data_callback(cid, rfcomm_l2cap_data_cb, NULL);

    vga_printf("[RFCOMM] Connect request: DLCI=%d, L2CAP_CID=0x%04X\n",
               dlci, cid);
    return dlci;
}

/* 断开RFCOMM会话 */
int rfcomm_disconnect(uint8_t dlci)
{
    rfcomm_session_t *sess = find_session_by_dlci(dlci);
    if (!sess) return -1;

    sess->state = RFCOMM_SESSION_CLOSING;

    /* 发送DISC */
    rfcomm_send_disc(sess->l2cap_cid, dlci);

    /* 断开L2CAP */
    l2cap_disconnect(sess->l2cap_cid);

    vga_printf("[RFCOMM] Disconnect: DLCI=%d\n", dlci);
    return 0;
}

/* 发送数据（串口写入） */
int rfcomm_send(uint8_t dlci, const uint8_t *data, uint16_t len)
{
    if (!data || len == 0) return -1;

    rfcomm_session_t *sess = find_session_by_dlci(dlci);
    if (!sess || sess->state != RFCOMM_SESSION_OPEN) return -2;

    uint8_t address = (dlci << 2) | 0x03;  /* C/R=1, EA=1 */
    int ret = rfcomm_send_frame(sess->l2cap_cid, address, RFCOMM_UIH,
                                 data, len);
    if (ret > 0) {
        sess->bytes_sent += len;
    }
    return ret;
}

/* 注册数据接收回调 */
int rfcomm_register_data_callback(uint8_t dlci, rfcomm_data_cb_t callback,
                                   void *user_data)
{
    for (int i = 0; i < RFCOMM_MAX_SESSIONS; i++) {
        if (callbacks[i].dlci == dlci) {
            callbacks[i].data_cb = callback;
            callbacks[i].user_data = user_data;
            return 0;
        }
    }
    return -1;
}

/* 设置端口参数 */
int rfcomm_set_port_params(uint8_t dlci, const rfcomm_port_params_t *params)
{
    rfcomm_session_t *sess = find_session_by_dlci(dlci);
    if (!sess || !params) return -1;
    memcpy(&sess->port_params, params, sizeof(rfcomm_port_params_t));

    if (sess->state == RFCOMM_SESSION_OPEN) {
        rfcomm_send_pn(sess->l2cap_cid, dlci, params);
    }
    return 0;
}

/* 获取端口参数 */
int rfcomm_get_port_params(uint8_t dlci, rfcomm_port_params_t *params)
{
    rfcomm_session_t *sess = find_session_by_dlci(dlci);
    if (!sess || !params) return -1;
    memcpy(params, &sess->port_params, sizeof(rfcomm_port_params_t));
    return 0;
}

/* 设置调制解调器状态 */
int rfcomm_set_modem_status(uint8_t dlci, const rfcomm_modem_status_t *status)
{
    rfcomm_session_t *sess = find_session_by_dlci(dlci);
    if (!sess || !status) return -1;
    memcpy(&sess->modem_status, status, sizeof(rfcomm_modem_status_t));

    if (sess->state == RFCOMM_SESSION_OPEN) {
        rfcomm_send_msc(sess->l2cap_cid, dlci, status);
    }
    return 0;
}

/* 处理UA帧 */
static void handle_ua(rfcomm_session_t *sess, uint8_t dlci)
{
    if (sess->state == RFCOMM_SESSION_OPENING) {
        sess->state = RFCOMM_SESSION_OPEN;

        /* 发送调制解调器状态 */
        rfcomm_modem_status_t status = {1, 1, 0, 0, 0, 0};  /* DTR+RTS */
        rfcomm_send_msc(sess->l2cap_cid, dlci, &status);

        vga_printf("[RFCOMM] Session open: DLCI=%d\n", dlci);
    }
}

/* 处理UIH帧（用户数据） */
static void handle_uih(rfcomm_session_t *sess, uint8_t dlci,
                        const uint8_t *data, uint16_t len)
{
    if (len < 1) return;

    /* 检查是否是MCC命令（控制信道） */
    if (dlci == RFCOMM_DLCI_CONTROL) {
        uint8_t mcc_type = data[0] & 0xFE;
        /* 简化：忽略MCC命令 */
        return;
    }

    /* 数据帧：调用回调 */
    sess->bytes_recv += len;
    stat_bytes_recv += len;

    for (int i = 0; i < RFCOMM_MAX_SESSIONS; i++) {
        if (callbacks[i].dlci == dlci && callbacks[i].data_cb) {
            callbacks[i].data_cb(dlci, data, len, callbacks[i].user_data);
            break;
        }
    }
}

/* 处理L2CAP数据 */
int rfcomm_process_l2cap_data(uint16_t cid, const uint8_t *data, uint16_t len)
{
    if (!data || len < 4) return -1;

    /* 解析RFCOMM帧头 */
    uint8_t address = data[0];
    uint8_t control = data[1];
    uint8_t length = data[2] >> 1;  /* 去掉EA位 */

    uint8_t dlci = address >> 2;
    const uint8_t *payload = data + 3;

    /* 查找会话 */
    rfcomm_session_t *sess = NULL;
    for (int i = 0; i < RFCOMM_MAX_SESSIONS; i++) {
        if (sessions[i].l2cap_cid == cid &&
            (sessions[i].dlci == dlci || dlci == RFCOMM_DLCI_CONTROL)) {
            sess = &sessions[i];
            break;
        }
    }

    if (!sess && dlci != RFCOMM_DLCI_CONTROL) return -2;

    switch (control) {
        case RFCOMM_SABME:
            /* 响应UA */
            if (sess) {
                rfcomm_send_ua(cid, dlci);
            }
            break;
        case RFCOMM_UA:
            if (sess) handle_ua(sess, dlci);
            break;
        case RFCOMM_DM:
            if (sess) sess->state = RFCOMM_SESSION_CLOSED;
            break;
        case RFCOMM_DISC:
            if (sess) {
                rfcomm_send_ua(cid, dlci);
                sess->state = RFCOMM_SESSION_CLOSED;
            }
            break;
        case RFCOMM_UIH:
        case RFCOMM_UI:
            handle_uih(sess, dlci, payload, length);
            break;
        default:
            vga_printf("[RFCOMM] Unknown control: 0x%02X\n", control);
            break;
    }

    return 0;
}

/* 获取会话信息 */
int rfcomm_get_session_info(uint8_t dlci, rfcomm_session_t *info)
{
    rfcomm_session_t *sess = find_session_by_dlci(dlci);
    if (!sess || !info) return -1;
    memcpy(info, sess, sizeof(rfcomm_session_t));
    return 0;
}

/* RFCOMM统计 */
void rfcomm_stats(uint32_t *num_sessions, uint32_t *bytes_sent,
                  uint32_t *bytes_recv)
{
    uint32_t active = 0;
    for (int i = 0; i < RFCOMM_MAX_SESSIONS; i++) {
        if (sessions[i].state != RFCOMM_SESSION_CLOSED) active++;
    }
    if (num_sessions) *num_sessions = active;
    if (bytes_sent) *bytes_sent = stat_bytes_sent;
    if (bytes_recv) *bytes_recv = stat_bytes_recv;
}
