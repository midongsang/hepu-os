/* ============================================================================
 * 赫普OS - 蓝牙GATT和Mesh（从bt2.c拆分）
 * ========================================================================== */

#include <stdint.h>
#include "bt2.h"
#include "vga.h"
#include "string.h"

extern bt2_t bt2;

int gatt_add_service(uint8_t *uuid, int is_primary)
{
    if (!uuid) return -1;
    for (int i = 0; i < BT2_MAX_GATT_SERVICES; i++) {
        if (!bt2.gatt_services[i].used) {
            memset(&bt2.gatt_services[i], 0, sizeof(gatt_service_t));
            bt2.gatt_services[i].used = 1;
            bt2.gatt_services[i].id = i;
            bt2.gatt_services[i].handle = 0x1000 + i * 16;
            memcpy(bt2.gatt_services[i].uuid, uuid, 16);
            bt2.gatt_services[i].is_primary = is_primary;
            bt2.gatt_service_count++;
            return i;
        }
    }
    return -1;
}

int gatt_add_characteristic(int service_id, uint8_t *uuid, uint8_t properties, uint8_t permissions)
{
    if (service_id < 0 || service_id >= BT2_MAX_GATT_SERVICES || !bt2.gatt_services[service_id].used) return -1;
    if (!uuid) return -1;
    for (int i = 0; i < BT2_MAX_GATT_CHARS; i++) {
        if (!bt2.gatt_chars[i].used) {
            memset(&bt2.gatt_chars[i], 0, sizeof(gatt_characteristic_t));
            bt2.gatt_chars[i].used = 1;
            bt2.gatt_chars[i].id = i;
            bt2.gatt_chars[i].handle = 0x2000 + i * 4;
            bt2.gatt_chars[i].value_handle = bt2.gatt_chars[i].handle + 1;
            memcpy(bt2.gatt_chars[i].uuid, uuid, 16);
            bt2.gatt_chars[i].properties = properties;
            bt2.gatt_chars[i].permissions = permissions;
            bt2.gatt_chars[i].service_id = service_id;
            bt2.gatt_services[service_id].characteristic_count++;
            bt2.gatt_char_count++;
            return i;
        }
    }
    return -1;
}

int gatt_read_value(int char_id, uint8_t *value, int max_len)
{
    if (char_id < 0 || char_id >= BT2_MAX_GATT_CHARS || !bt2.gatt_chars[char_id].used) return -1;
    if (!value || max_len <= 0) return -1;
    int len = (bt2.gatt_chars[char_id].value_len < max_len) ? bt2.gatt_chars[char_id].value_len : max_len;
    if (bt2.gatt_chars[char_id].value) memcpy(value, bt2.gatt_chars[char_id].value, len);
    return len;
}

int gatt_write_value(int char_id, const uint8_t *value, int len)
{
    if (char_id < 0 || char_id >= BT2_MAX_GATT_CHARS || !bt2.gatt_chars[char_id].used) return -1;
    if (!value || len <= 0) return -1;
    bt2.gatt_chars[char_id].value = (uint8_t *)value;
    bt2.gatt_chars[char_id].value_len = len;
    return 0;
}

int gatt_notify(int char_id, const uint8_t *value, int len)
{
    if (char_id < 0 || char_id >= BT2_MAX_GATT_CHARS || !bt2.gatt_chars[char_id].used) return -1;
    if (!bt2.gatt_chars[char_id].notifying) return -1;
    (void)value; (void)len;
    return 0;
}

int gatt_set_notify(int char_id, int enable)
{
    if (char_id < 0 || char_id >= BT2_MAX_GATT_CHARS || !bt2.gatt_chars[char_id].used) return -1;
    bt2.gatt_chars[char_id].notifying = enable ? 1 : 0;
    return 0;
}

int gatt_discover_services(uint8_t *address) { (void)address; return 0; }
int gatt_read_characteristic(uint8_t *address, uint16_t handle) { (void)address; (void)handle; return 0; }
int gatt_write_characteristic(uint8_t *address, uint16_t handle, const uint8_t *value, int len)
{
    (void)address; (void)handle; (void)value; return len;
}

int mesh_provision(uint8_t *uuid, uint16_t address)
{
    if (!uuid) return -1;
    for (int i = 0; i < BT2_MAX_MESH_NODES; i++) {
        if (!bt2.mesh_nodes[i].used) {
            memset(&bt2.mesh_nodes[i], 0, sizeof(mesh_node_t));
            bt2.mesh_nodes[i].used = 1;
            bt2.mesh_nodes[i].id = i;
            bt2.mesh_nodes[i].address = address;
            memcpy(bt2.mesh_nodes[i].uuid, uuid, 16);
            bt2.mesh_nodes[i].online = 1;
            bt2.mesh_nodes[i].ttl = 7;
            bt2.mesh_node_count++;
            bt2.mesh_provisioned = 1;
            vga_printf("[BT2] Mesh node config: 0x%04x\n", address);
            return i;
        }
    }
    return -1;
}

int mesh_unprovision(uint16_t address)
{
    for (int i = 0; i < BT2_MAX_MESH_NODES; i++) {
        if (bt2.mesh_nodes[i].used && bt2.mesh_nodes[i].address == address) {
            bt2.mesh_nodes[i].used = 0;
            bt2.mesh_node_count--;
            return 0;
        }
    }
    return -1;
}

int mesh_send_message(mesh_message_t *msg)
{
    if (!msg) return -1;
    vga_printf("[BT2] Mesh msg: 0x%04x -> 0x%04x(op=0x%x)\n", msg->src, msg->dst, msg->opcode);
    return 0;
}

int mesh_get_node(uint16_t address, mesh_node_t *node)
{
    if (!node) return -1;
    for (int i = 0; i < BT2_MAX_MESH_NODES; i++) {
        if (bt2.mesh_nodes[i].used && bt2.mesh_nodes[i].address == address) {
            memcpy(node, &bt2.mesh_nodes[i], sizeof(mesh_node_t));
            return 0;
        }
    }
    return -1;
}

int mesh_list_nodes(mesh_node_t *nodes, int max)
{
    if (!nodes || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < BT2_MAX_MESH_NODES && count < max; i++) {
        if (bt2.mesh_nodes[i].used) memcpy(&nodes[count++], &bt2.mesh_nodes[i], sizeof(mesh_node_t));
    }
    return count;
}

int mesh_set_node_state(uint16_t address, int online)
{
    for (int i = 0; i < BT2_MAX_MESH_NODES; i++) {
        if (bt2.mesh_nodes[i].used && bt2.mesh_nodes[i].address == address) {
            bt2.mesh_nodes[i].online = online ? 1 : 0;
            return 0;
        }
    }
    return -1;
}

