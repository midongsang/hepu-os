/* ============================================================================
 * 赫普OS - 网络QoS管理（从net3.c拆分）
 * ========================================================================== */

#include <stdint.h>
#include "net3.h"
#include "vga.h"
#include "string.h"

extern net3_t net;

int net3_qos_enable(int enable)
{
    net.qos_enabled = enable ? 1 : 0;
    return 0;
}

int net3_qos_add_class(qos_class_t *qos)
{
    if (!qos) return -1;
    for (int i = 0; i < NET3_MAX_QOS_CLASSES; i++) {
        if (!net.qos_classes[i].used) {
            memcpy(&net.qos_classes[i], qos, sizeof(qos_class_t));
            net.qos_classes[i].used = 1;
            net.qos_classes[i].id = i;
            net.qos_count++;
            return i;
        }
    }
    return -1;
}

int net3_qos_remove_class(int class_id)
{
    if (class_id < 0 || class_id >= NET3_MAX_QOS_CLASSES || !net.qos_classes[class_id].used) return -1;
    net.qos_classes[class_id].used = 0;
    net.qos_count--;
    return 0;
}

int net3_qos_list(qos_class_t *classes, int max)
{
    if (!classes || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < NET3_MAX_QOS_CLASSES && count < max; i++) {
        if (net.qos_classes[i].used) memcpy(&classes[count++], &net.qos_classes[i], sizeof(qos_class_t));
    }
    return count;
}

int net3_get_stats(uint64_t *rx, uint64_t *tx, uint64_t *packets, uint64_t *tcp_conns, uint64_t *dns)
{
    if (rx) *rx = net.total_rx_bytes;
    if (tx) *tx = net.total_tx_bytes;
    if (packets) *packets = net.total_packets;
    if (tcp_conns) *tcp_conns = net.tcp_connections_total;
    if (dns) *dns = net.dns_queries;
    return 0;
}
