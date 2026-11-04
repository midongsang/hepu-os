/* ============================================================================
 * 赫普OS - HCI协议实现
 * V8.62: 初始实现
 * HCI命令构建、事件解析、状态管理
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "../include/hci.h"
#include "../include/vga.h"

/* 构建HCI命令包 */
int hci_build_command(uint16_t opcode, const void *params, uint8_t param_len,
                      uint8_t *out_buf, int out_buf_len)
{
    if (!out_buf || out_buf_len < (3 + param_len)) return -1;
    if (param_len > 0 && !params) return -1;

    int pos = 0;

    /* 操作码（小端序） */
    out_buf[pos++] = opcode & 0xFF;
    out_buf[pos++] = (opcode >> 8) & 0xFF;

    /* 参数长度 */
    out_buf[pos++] = param_len;

    /* 参数数据 */
    if (param_len > 0 && params) {
        memcpy(&out_buf[pos], params, param_len);
        pos += param_len;
    }

    return pos;
}

/* 解析HCI事件包 */
int hci_parse_event(const uint8_t *data, int len, hci_event_packet_t *event)
{
    if (!data || !event || len < 2) return -1;

    int pos = 0;

    /* 事件码 */
    event->event_code = data[pos++];

    /* 参数长度 */
    event->param_len = data[pos++];

    /* 检查长度 */
    if (event->param_len > 255 || (pos + event->param_len) > len) {
        return -1;
    }

    /* 参数数据 */
    if (event->param_len > 0) {
        memcpy(event->params, &data[pos], event->param_len);
    }

    return pos + event->param_len;
}

/* 获取命令状态事件中的状态码 */
uint8_t hci_get_command_status(const hci_event_packet_t *event)
{
    if (!event || event->event_code != HCI_EVT_COMMAND_STATUS) return 0xFF;
    if (event->param_len < 1) return 0xFF;
    return event->params[0];
}

/* 获取命令完成事件中的opcode */
uint16_t hci_get_command_complete_opcode(const hci_event_packet_t *event)
{
    if (!event || event->event_code != HCI_EVT_COMMAND_COMPLETE) return 0;
    if (event->param_len < 3) return 0;
    return event->params[1] | (event->params[2] << 8);
}

/* 解析Inquiry Result事件 */
int hci_parse_inquiry_result(const hci_event_packet_t *event,
                              hci_bdaddr_t *addrs, int max_addrs,
                              int *rssi_list)
{
    if (!event || event->event_code != HCI_EVT_INQUIRY_RESULT) return 0;
    if (event->param_len < 1) return 0;

    int num_responses = event->params[0];
    if (num_responses > max_addrs) num_responses = max_addrs;

    int pos = 1;
    for (int i = 0; i < num_responses; i++) {
        /* BD_ADDR（6字节，小端序需要反转） */
        if (pos + 6 > event->param_len) break;
        for (int j = 0; j < 6; j++) {
            addrs[i].addr[j] = event->params[pos + 5 - j];
        }
        pos += 6;

        /* Page Scan Repetition Mode (1字节) */
        pos += 1;

        /* 保留 (1字节) */
        pos += 1;

        /* Page Scan Period Mode (1字节) */
        pos += 1;

        /* Class of Device (3字节) */
        pos += 3;

        /* Clock Offset (2字节) */
        pos += 2;

        /* RSSI（仅Inquiry Result With RSSI事件有） */
        if (event->event_code == HCI_EVT_INQUIRY_RESULT_WITH_RSSI &&
            pos < event->param_len) {
            if (rssi_list) rssi_list[i] = (int8_t)event->params[pos];
            pos += 1;
        }
    }

    return num_responses;
}

/* 解析Connection Complete事件 */
int hci_parse_conn_complete(const hci_event_packet_t *event,
                             uint16_t *handle, hci_bdaddr_t *addr,
                             uint8_t *status, uint8_t *role, uint8_t *encryption)
{
    if (!event || event->event_code != HCI_EVT_CONN_COMPLETE) return -1;
    if (event->param_len < 11) return -1;

    int pos = 0;
    if (status) *status = event->params[pos++];
    else pos++;

    if (handle) *handle = event->params[pos] | (event->params[pos+1] << 8);
    pos += 2;

    if (addr) {
        for (int j = 0; j < 6; j++) {
            addr->addr[j] = event->params[pos + 5 - j];
        }
    }
    pos += 6;

    if (role) *role = event->params[pos++];
    else pos++;

    if (encryption) *encryption = event->params[pos++];

    return 0;
}

/* 解析Disconnection Complete事件 */
int hci_parse_disconn_complete(const hci_event_packet_t *event,
                                uint16_t *handle, uint8_t *status, uint8_t *reason)
{
    if (!event || event->event_code != HCI_EVT_DISCONN_COMPLETE) return -1;
    if (event->param_len < 4) return -1;

    int pos = 0;
    if (status) *status = event->params[pos++];
    else pos++;

    if (handle) *handle = event->params[pos] | (event->params[pos+1] << 8);
    pos += 2;

    if (reason) *reason = event->params[pos++];

    return 0;
}

/* 解析Remote Name Request Complete事件 */
int hci_parse_remote_name(const hci_event_packet_t *event,
                           hci_bdaddr_t *addr, char *name, int name_len)
{
    if (!event || event->event_code != HCI_EVT_REMOTE_NAME_REQ_COMPLETE) return -1;
    if (event->param_len < 255) return -1;

    int pos = 0;
    uint8_t status = event->params[pos++];
    if (status != HCI_SUCCESS) return -1;

    if (addr) {
        for (int j = 0; j < 6; j++) {
            addr->addr[j] = event->params[pos + 5 - j];
        }
    }
    pos += 6;

    /* 设备名称（最多248字节，以null结尾） */
    if (name && name_len > 0) {
        int copy_len = event->param_len - pos;
        if (copy_len > name_len - 1) copy_len = name_len - 1;
        memcpy(name, &event->params[pos], copy_len);
        name[copy_len] = '\0';
    }

    return 0;
}

/* 解析Read BD_ADDR命令完成事件 */
int hci_parse_read_bd_addr(const hci_event_packet_t *event, hci_bdaddr_t *addr)
{
    if (!event || event->event_code != HCI_EVT_COMMAND_COMPLETE) return -1;
    if (event->param_len < 10) return -1;

    /* params[0] = num_hci_command_packets
     * params[1-2] = opcode
     * params[3] = status
     * params[4-9] = BD_ADDR
     */
    if (event->params[3] != HCI_SUCCESS) return -1;

    if (addr) {
        for (int j = 0; j < 6; j++) {
            addr->addr[j] = event->params[4 + 5 - j];
        }
    }

    return 0;
}

/* 解析LE Advertising Report事件 */
int hci_parse_le_advertising_report(const hci_event_packet_t *event,
                                     hci_bdaddr_t *addrs, int max_addrs,
                                     int *rssi_list, uint8_t *event_types)
{
    if (!event || event->event_code != HCI_EVT_LE_META_EVENT) return 0;
    if (event->param_len < 2) return 0;
    if (event->params[0] != HCI_LE_EVT_ADVERTISING_REPORT) return 0;

    int num_reports = event->params[1];
    if (num_reports > max_addrs) num_reports = max_addrs;

    int pos = 2;
    for (int i = 0; i < num_reports; i++) {
        if (pos >= event->param_len) break;

        if (event_types) event_types[i] = event->params[pos];
        pos++;

        /* 地址类型 (1字节) */
        uint8_t addr_type = event->params[pos++];

        /* BD_ADDR (6字节) */
        if (pos + 6 <= event->param_len) {
            for (int j = 0; j < 6; j++) {
                addrs[i].addr[j] = event->params[pos + 5 - j];
            }
            pos += 6;
        }

        /* 数据长度 (1字节) */
        uint8_t data_len = event->params[pos++];
        pos += data_len;  /* 跳过广告数据 */

        /* RSSI (1字节) */
        if (pos < event->param_len) {
            if (rssi_list) rssi_list[i] = (int8_t)event->params[pos];
            pos++;
        }

        (void)addr_type;
    }

    return num_reports;
}

/* 构建Reset命令 */
int hci_cmd_reset(uint8_t *out_buf, int out_buf_len)
{
    return hci_build_command(HCI_CMD_RESET, NULL, 0, out_buf, out_buf_len);
}

/* 构建Set Event Mask命令 */
int hci_cmd_set_event_mask(const hci_event_mask_t *mask,
                            uint8_t *out_buf, int out_buf_len)
{
    return hci_build_command(HCI_CMD_SET_EVENT_MASK, mask->mask, 8,
                             out_buf, out_buf_len);
}

/* 构建Inquiry命令 */
int hci_cmd_inquiry(uint32_t lap, uint8_t length, uint8_t num_responses,
                    uint8_t *out_buf, int out_buf_len)
{
    hci_inquiry_params_t params;
    params.lap = lap;
    params.inquiry_length = length;
    params.num_responses = num_responses;
    return hci_build_command(HCI_CMD_INQUIRY, &params, sizeof(params),
                             out_buf, out_buf_len);
}

/* 构建Inquiry Cancel命令 */
int hci_cmd_inquiry_cancel(uint8_t *out_buf, int out_buf_len)
{
    return hci_build_command(HCI_CMD_INQUIRY_CANCEL, NULL, 0, out_buf, out_buf_len);
}

/* 构建Create Connection命令 */
int hci_cmd_create_connection(const hci_bdaddr_t *addr, uint16_t packet_type,
                               uint8_t page_scan_rep_mode, uint8_t page_scan_mode,
                               uint16_t clock_offset, uint8_t allow_role_switch,
                               uint8_t *out_buf, int out_buf_len)
{
    hci_create_conn_params_t params;
    memset(&params, 0, sizeof(params));
    memcpy(params.bdaddr.addr, addr->addr, 6);
    params.packet_type = packet_type;
    params.page_scan_repetition_mode = page_scan_rep_mode;
    params.page_scan_mode = page_scan_mode;
    params.clock_offset = clock_offset;
    params.allow_role_switch = allow_role_switch;
    return hci_build_command(HCI_CMD_CREATE_CONN, &params, sizeof(params),
                             out_buf, out_buf_len);
}

/* 构建Disconnect命令 */
int hci_cmd_disconnect(uint16_t handle, uint8_t reason,
                        uint8_t *out_buf, int out_buf_len)
{
    hci_disconnect_params_t params;
    params.handle = handle;
    params.reason = reason;
    return hci_build_command(HCI_CMD_DISCONNECT, &params, sizeof(params),
                             out_buf, out_buf_len);
}

/* 构建Change Local Name命令 */
int hci_cmd_change_local_name(const char *name,
                               uint8_t *out_buf, int out_buf_len)
{
    hci_set_local_name_params_t params;
    memset(&params, 0, sizeof(params));
    if (name) {
        int len = strlen(name);
        if (len > 247) len = 247;
        memcpy(params.name, name, len);
    }
    return hci_build_command(HCI_CMD_CHANGE_LOCAL_NAME, &params, sizeof(params),
                             out_buf, out_buf_len);
}

/* 构建Write Scan Enable命令 */
int hci_cmd_write_scan_enable(uint8_t scan_enable,
                               uint8_t *out_buf, int out_buf_len)
{
    hci_write_scan_enable_params_t params;
    params.scan_enable = scan_enable;
    return hci_build_command(HCI_CMD_WRITE_SCAN_ENABLE, &params, sizeof(params),
                             out_buf, out_buf_len);
}

/* 构建Read BD ADDR命令 */
int hci_cmd_read_bd_addr(uint8_t *out_buf, int out_buf_len)
{
    return hci_build_command(HCI_CMD_READ_BD_ADDR, NULL, 0, out_buf, out_buf_len);
}

/* 构建LE Set Scan Enable命令 */
int hci_cmd_le_set_scan_enable(uint8_t enable, uint8_t filter_duplicates,
                                uint8_t *out_buf, int out_buf_len)
{
    uint8_t params[2] = { enable, filter_duplicates };
    return hci_build_command(HCI_CMD_LE_SET_SCAN_ENABLE, params, 2,
                             out_buf, out_buf_len);
}

/* 构建LE Set Scan Parameters命令 */
int hci_cmd_le_set_scan_params(uint8_t scan_type, uint16_t scan_interval,
                                uint16_t scan_window, uint8_t own_addr_type,
                                uint8_t filter_policy,
                                uint8_t *out_buf, int out_buf_len)
{
    uint8_t params[7];
    params[0] = scan_type;
    params[1] = scan_interval & 0xFF;
    params[2] = (scan_interval >> 8) & 0xFF;
    params[3] = scan_window & 0xFF;
    params[4] = (scan_window >> 8) & 0xFF;
    params[5] = own_addr_type;
    params[6] = filter_policy;
    return hci_build_command(HCI_CMD_LE_SET_SCAN_PARAM, params, 7,
                             out_buf, out_buf_len);
}

/* 状态码转字符串 */
const char *hci_status_str(uint8_t status)
{
    switch (status) {
        case HCI_SUCCESS: return "Success";
        case HCI_UNKNOWN_CMD: return "Unknown Command";
        case HCI_UNKNOWN_CONN_ID: return "Unknown Connection ID";
        case HCI_HARDWARE_FAILURE: return "Hardware Failure";
        case HCI_PAGE_TIMEOUT: return "Page Timeout";
        case HCI_AUTH_FAILURE: return "Authentication Failure";
        case HCI_PIN_OR_KEY_MISSING: return "PIN or Key Missing";
        case HCI_MEMORY_CAPACITY_EXCEEDED: return "Memory Capacity Exceeded";
        case HCI_CONN_TIMEOUT: return "Connection Timeout";
        case HCI_CONN_LIMIT_EXCEEDED: return "Connection Limit Exceeded";
        case HCI_CONN_EXISTS: return "Connection Exists";
        case HCI_CMD_DISALLOWED: return "Command Disallowed";
        case HCI_REJECTED_LIMITED_RESOURCES: return "Rejected (Limited Resources)";
        case HCI_REJECTED_SECURITY: return "Rejected (Security)";
        case HCI_REJECTED_PERSONAL: return "Rejected (Personal)";
        case HCI_HOST_TIMEOUT: return "Host Timeout";
        case HCI_UNSUPPORTED_FEATURE: return "Unsupported Feature";
        case HCI_INVALID_PARAMS: return "Invalid Parameters";
        case HCI_REMOTE_USER_TERMINATED: return "Remote User Terminated";
        case HCI_REMOTE_LOW_RESOURCES: return "Remote Low Resources";
        case HCI_REMOTE_POWER_OFF: return "Remote Power Off";
        case HCI_CONN_TERMINATED_LOCAL: return "Connection Terminated (Local)";
        case HCI_PAIRING_NOT_ALLOWED: return "Pairing Not Allowed";
        case HCI_INSTANT_PASSED: return "Instant Passed";
        default: return "Unknown Error";
    }
}
