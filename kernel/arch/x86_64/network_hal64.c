/* ============================================================================
 * 赫普OS - 64位网络硬件抽象层（Network HAL）
 * 统一网络驱动接口，支持多种网卡（RTL8139/E1000/virtio-net）
 * V0.10.101: P4-1 HAL完全统一，网络抽象层框架
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>

/* 外部函数 */
extern void vga_puts(const char *str);

/* 网络设备类型 */
typedef enum {
    NET_DEV_RTL8139 = 0,
    NET_DEV_E1000,
    NET_DEV_VIRTIO,
    NET_DEV_TYPE_MAX
} net_dev_type_t;

/* 网络设备状态 */
typedef enum {
    NET_DEV_UNINITIALIZED = 0,
    NET_DEV_INITIALIZED,
    NET_DEV_LINK_UP,
    NET_DEV_LINK_DOWN,
    NET_DEV_ERROR
} net_dev_state_t;

/* 网络设备操作表 */
typedef struct {
    int (*init)(void);
    int (*send)(const uint8_t *data, uint32_t length);
    int (*recv)(uint8_t *buffer, uint32_t max_length);
    int (*get_mac)(uint8_t *mac);
    int (*set_ip)(uint32_t ip, uint32_t netmask, uint32_t gateway);
    int (*get_state)(void);
    void (*poll)(void);
} net_dev_ops_t;

/* 网络设备描述符 */
typedef struct {
    const char *name;
    net_dev_type_t type;
    net_dev_state_t state;
    uint8_t mac[6];
    uint32_t ip;
    uint32_t netmask;
    uint32_t gateway;
    net_dev_ops_t *ops;
    uint64_t tx_packets;
    uint64_t rx_packets;
    uint64_t tx_bytes;
    uint64_t rx_bytes;
} net_device_t;

/* 最大网络设备数 */
#define NET_MAX_DEVICES 4

/* 网络设备管理器 */
typedef struct {
    net_device_t devices[NET_MAX_DEVICES];
    int device_count;
    int initialized;
    int default_device;
} net_manager_t;

static net_manager_t net_mgr;

/* 外部网卡驱动函数（RTL8139） */
extern int eth64_init(void);
extern int eth64_send(const uint8_t *data, uint32_t length);
extern int eth64_recv(uint8_t *buffer, uint32_t max_length);
extern int eth64_get_mac(uint8_t *mac);
extern int eth64_set_ip(uint32_t ip, uint32_t netmask, uint32_t gateway);
extern void net64_poll(void);

/* RTL8139操作表 */
static net_dev_ops_t rtl8139_ops = {
    .init = eth64_init,
    .send = eth64_send,
    .recv = eth64_recv,
    .get_mac = eth64_get_mac,
    .set_ip = eth64_set_ip,
    .get_state = NULL,  /* TODO: 实现状态查询 */
    .poll = net64_poll,
};

/* 初始化网络HAL */
int net_hal_init(void)
{
    vga_puts("[NET_HAL] Initializing network HAL...\n");

    net_mgr.device_count = 0;
    net_mgr.initialized = 1;
    net_mgr.default_device = 0;

    /* 注册RTL8139网卡 */
    net_device_t *dev = &net_mgr.devices[0];
    dev->name = "eth0";
    dev->type = NET_DEV_RTL8139;
    dev->state = NET_DEV_UNINITIALIZED;
    dev->ops = &rtl8139_ops;
    dev->tx_packets = 0;
    dev->rx_packets = 0;
    dev->tx_bytes = 0;
    dev->rx_bytes = 0;

    /* 初始化RTL8139 */
    if (dev->ops->init) {
        if (dev->ops->init() == 0) {
            dev->state = NET_DEV_INITIALIZED;
            vga_puts("[NET_HAL] RTL8139 initialized (eth0)\n");
        } else {
            dev->state = NET_DEV_ERROR;
            vga_puts("[NET_HAL] RTL8139 init failed\n");
        }
    }

    net_mgr.device_count = 1;
    vga_puts("[NET_HAL] Network HAL initialized: 1 device\n");
    return 0;
}

/* 通过HAL发送数据包 */
int net_hal_send(const uint8_t *data, uint32_t length)
{
    if (!net_mgr.initialized || net_mgr.device_count == 0) return -1;
    net_device_t *dev = &net_mgr.devices[net_mgr.default_device];
    if (!dev->ops || !dev->ops->send) return -1;
    int ret = dev->ops->send(data, length);
    if (ret > 0) {
        dev->tx_packets++;
        dev->tx_bytes += ret;
    }
    return ret;
}

/* 通过HAL接收数据包 */
int net_hal_recv(uint8_t *buffer, uint32_t max_length)
{
    if (!net_mgr.initialized || net_mgr.device_count == 0) return -1;
    net_device_t *dev = &net_mgr.devices[net_mgr.default_device];
    if (!dev->ops || !dev->ops->recv) return -1;
    int ret = dev->ops->recv(buffer, max_length);
    if (ret > 0) {
        dev->rx_packets++;
        dev->rx_bytes += ret;
    }
    return ret;
}

/* 通过HAL获取MAC地址 */
int net_hal_get_mac(uint8_t *mac)
{
    if (!net_mgr.initialized || net_mgr.device_count == 0) return -1;
    net_device_t *dev = &net_mgr.devices[net_mgr.default_device];
    if (!dev->ops || !dev->ops->get_mac) return -1;
    return dev->ops->get_mac(mac);
}

/* 通过HAL设置IP地址 */
int net_hal_set_ip(uint32_t ip, uint32_t netmask, uint32_t gateway)
{
    if (!net_mgr.initialized || net_mgr.device_count == 0) return -1;
    net_device_t *dev = &net_mgr.devices[net_mgr.default_device];
    dev->ip = ip;
    dev->netmask = netmask;
    dev->gateway = gateway;
    if (dev->ops && dev->ops->set_ip) {
        return dev->ops->set_ip(ip, netmask, gateway);
    }
    return 0;
}

/* 通过HAL轮询网络 */
void net_hal_poll(void)
{
    if (!net_mgr.initialized) return;
    for (int i = 0; i < net_mgr.device_count; i++) {
        net_device_t *dev = &net_mgr.devices[i];
        if (dev->ops && dev->ops->poll) {
            dev->ops->poll();
        }
    }
}

/* 获取网络设备统计 */
void net_hal_get_stats(int dev_idx, uint64_t *tx_packets, uint64_t *rx_packets,
                       uint64_t *tx_bytes, uint64_t *rx_bytes)
{
    if (dev_idx < 0 || dev_idx >= net_mgr.device_count) return;
    net_device_t *dev = &net_mgr.devices[dev_idx];
    if (tx_packets) *tx_packets = dev->tx_packets;
    if (rx_packets) *rx_packets = dev->rx_packets;
    if (tx_bytes) *tx_bytes = dev->tx_bytes;
    if (rx_bytes) *rx_bytes = dev->rx_bytes;
}

/* 获取网络设备数量 */
int net_hal_get_device_count(void)
{
    return net_mgr.device_count;
}

/* 获取默认设备索引 */
int net_hal_get_default_device(void)
{
    return net_mgr.default_device;
}
