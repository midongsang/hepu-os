/* ============================================================================
 * 赫普OS - 蓝牙协议栈
 * HCI + L2CAP + RFCOMM + SDP
 * ========================================================================== */

#include <stdint.h>
#include "bluetooth.h"
#include "vga.h"
#include "string.h"
#include "kheap.h"

static bt_adapter_t adapter;
static int bt_initialized = 0;

void bt_init(void)
{
    memset(&adapter, 0, sizeof(bt_adapter_t));
    adapter.initialized = 1;
    adapter.powered = 0;
    adapter.discoverable = 0;
    adapter.connectable = 1;
    strcpy(adapter.local_name, "HEPU OS");

    /* 默认本地地址 */
    adapter.local_addr.addr[0] = 0x00;
    adapter.local_addr.addr[1] = 0x1A;
    adapter.local_addr.addr[2] = 0x7D;
    adapter.local_addr.addr[3] = 0x00;
    adapter.local_addr.addr[4] = 0x00;
    adapter.local_addr.addr[5] = 0x01;

    bt_initialized = 1;
    vga_puts("[BT] BT subsystem init done \n");
    vga_printf(" local name: %s\n", adapter.local_name);
}

int bt_power_on(void)
{
    if (!bt_initialized) return -1;
    adapter.powered = 1;
    vga_puts("[BT] adapter on \n");
    return 0;
}

int bt_power_off(void)
{
    if (!bt_initialized) return -1;
    adapter.powered = 0;
    /* 断开所有连接 */
    for (int i = 0; i < BT_MAX_DEVICES; i++) {
        if (adapter.devices[i].used && adapter.devices[i].state == BT_DEV_CONNECTED) {
            adapter.devices[i].state = BT_DEV_DISCONNECTED;
        }
    }
    vga_puts("[BT] adapter off \n");
    return 0;
}

int bt_set_local_name(const char *name)
{
    if (!name) return -1;
    strncpy(adapter.local_name, name, BT_NAME_LEN - 1);
    return 0;
}

int bt_set_discoverable(int enable)
{
    adapter.discoverable = enable ? 1 : 0;
    return 0;
}

int bt_set_connectable(int enable)
{
    adapter.connectable = enable ? 1 : 0;
    return 0;
}

int bt_start_inquiry(int duration)
{
    if (!adapter.powered) return -1;
    vga_printf("[BT] start device scan(%d s)...\n", duration);

    /* 原型阶段: 模拟发现设备 */
    adapter.device_count = 0;

    /* 模拟设备1: 蓝牙耳机 */
    bt_device_t *dev = &adapter.devices[0];
    memset(dev, 0, sizeof(bt_device_t));
    dev->used = 1;
    dev->addr.addr[0] = 0x00; dev->addr.addr[1] = 0x1B; dev->addr.addr[2] = 0xDC;
    dev->addr.addr[3] = 0x12; dev->addr.addr[4] = 0x34; dev->addr.addr[5] = 0x56;
    strcpy(dev->name, "BT Headphones");
    dev->class_of_device = 0x240404;  /* 音频/耳机 */
    dev->rssi = -65;
    dev->state = BT_DEV_NONE;
    adapter.device_count++;

    /* 模拟设备2: 蓝牙音箱 */
    dev = &adapter.devices[1];
    memset(dev, 0, sizeof(bt_device_t));
    dev->used = 1;
    dev->addr.addr[0] = 0x00; dev->addr.addr[1] = 0x1C; dev->addr.addr[2] = 0xBE;
    dev->addr.addr[3] = 0x78; dev->addr.addr[4] = 0x9A; dev->addr.addr[5] = 0xBC;
    strcpy(dev->name, "BT Speaker");
    dev->class_of_device = 0x240414;  /* 音频/音箱 */
    dev->rssi = -72;
    dev->state = BT_DEV_NONE;
    adapter.device_count++;

    vga_printf("[BT] found %d device \n", adapter.device_count);
    return adapter.device_count;
}

int bt_cancel_inquiry(void)
{
    vga_puts("[BT] cancel scan \n");
    return 0;
}

int bt_get_devices(bt_device_t *list, int max)
{
    if (!list) return 0;
    int count = 0;
    for (int i = 0; i < BT_MAX_DEVICES && count < max; i++) {
        if (adapter.devices[i].used) {
            memcpy(&list[count], &adapter.devices[i], sizeof(bt_device_t));
            count++;
        }
    }
    return count;
}

int bt_connect(bt_addr_t *addr)
{
    if (!adapter.powered || !addr) return -1;

    for (int i = 0; i < BT_MAX_DEVICES; i++) {
        if (adapter.devices[i].used &&
            memcmp(adapter.devices[i].addr.addr, addr->addr, BT_ADDR_LEN) == 0) {
            adapter.devices[i].state = BT_DEV_CONNECTING;
            /* 原型阶段: 直接连接成功 */
            adapter.devices[i].state = BT_DEV_CONNECTED;
            adapter.devices[i].handle = i + 1;
            adapter.devices[i].mtu = 1024;
            vga_printf("[BT] connected: %s(handle=%d)\n",
                       adapter.devices[i].name, adapter.devices[i].handle);
            return adapter.devices[i].handle;
        }
    }
    return -1;
}

int bt_disconnect(uint16_t handle)
{
    for (int i = 0; i < BT_MAX_DEVICES; i++) {
        if (adapter.devices[i].used && adapter.devices[i].handle == handle) {
            adapter.devices[i].state = BT_DEV_DISCONNECTED;
            vga_printf("[BT] disconnected: %s\n", adapter.devices[i].name);
            return 0;
        }
    }
    return -1;
}

int bt_accept_connection(uint16_t handle)
{
    (void)handle;
    return 0;
}

int bt_reject_connection(uint16_t handle)
{
    (void)handle;
    return 0;
}

int bt_l2cap_connect(bt_addr_t *addr, uint16_t psm)
{
    if (!adapter.powered || !addr) return -1;
    for (int i = 0; i < BT_MAX_CONNECTIONS; i++) {
        if (!adapter.l2cap_chans[i].used) {
            adapter.l2cap_chans[i].used = 1;
            adapter.l2cap_chans[i].cid = i + 1;
            adapter.l2cap_chans[i].psm = psm;
            adapter.l2cap_chans[i].mtu = 1024;
            adapter.l2cap_chans[i].state = 1;
            vga_printf("[BT-L2CAP] channel up: CID=%d PSM=0x%04x\n", i + 1, psm);
            return i + 1;
        }
    }
    return -1;
}

int bt_l2cap_send(int chan_id, const void *data, uint16_t len)
{
    if (chan_id <= 0 || chan_id > BT_MAX_CONNECTIONS) return -1;
    if (!adapter.l2cap_chans[chan_id - 1].used) return -1;
    (void)data;
    return len;  /* 原型阶段: 模拟发送 */
}

int bt_l2cap_recv(int chan_id, void *data, uint16_t len)
{
    if (chan_id <= 0 || chan_id > BT_MAX_CONNECTIONS) return -1;
    if (!adapter.l2cap_chans[chan_id - 1].used) return -1;
    memset(data, 0, len);
    return 0;  /* 原型阶段: 无数据 */
}

int bt_rfcomm_connect(bt_addr_t *addr, int channel)
{
    int l2cap_id = bt_l2cap_connect(addr, L2CAP_RFCOMM);
    if (l2cap_id < 0) return -1;

    for (int i = 0; i < BT_MAX_CONNECTIONS; i++) {
        if (!adapter.rfcomm_sessions[i].used) {
            adapter.rfcomm_sessions[i].used = 1;
            adapter.rfcomm_sessions[i].dlci = channel * 2;
            adapter.rfcomm_sessions[i].channel = channel;
            adapter.rfcomm_sessions[i].l2cap = &adapter.l2cap_chans[l2cap_id - 1];
            adapter.rfcomm_sessions[i].connected = 1;
            vga_printf("[BT-RFCOMM] session up: channel=%d\n", channel);
            return i + 1;
        }
    }
    return -1;
}

int bt_rfcomm_send(int session_id, const void *data, uint16_t len)
{
    if (session_id <= 0 || session_id > BT_MAX_CONNECTIONS) return -1;
    if (!adapter.rfcomm_sessions[session_id - 1].connected) return -1;
    return bt_l2cap_send(session_id, data, len);
}

int bt_rfcomm_recv(int session_id, void *data, uint16_t len)
{
    if (session_id <= 0 || session_id > BT_MAX_CONNECTIONS) return -1;
    if (!adapter.rfcomm_sessions[session_id - 1].connected) return -1;
    return bt_l2cap_recv(session_id, data, len);
}

int bt_sdp_search(bt_addr_t *addr, uint16_t service_uuid)
{
    (void)addr;
    vga_printf("[BT-SDP] search service: UUID=0x%04x\n", service_uuid);
    return 0;  /* 原型阶段 */
}

int bt_hci_send_cmd(uint16_t opcode, const void *params, uint8_t len)
{
    (void)opcode; (void)params; (void)len;
    return 0;
}

void bt_hci_handle_event(uint8_t *data, uint16_t len)
{
    (void)data; (void)len;
    /* 原型阶段: 处理HCI事件 */
}

int bt_get_local_addr(bt_addr_t *addr)
{
    if (!addr) return -1;
    memcpy(addr, &adapter.local_addr, sizeof(bt_addr_t));
    return 0;
}

void bt_addr_to_str(bt_addr_t *addr, char *str)
{
    if (!addr || !str) return;
    static const char hex[] = "0123456789abcdef";
    int pos = 0;
    for (int i = 0; i < 6; i++) {
        str[pos++] = hex[addr->addr[i] >> 4];
        str[pos++] = hex[addr->addr[i] & 0x0F];
        if (i < 5) str[pos++] = ':';
    }
    str[pos] = '\0';
}
