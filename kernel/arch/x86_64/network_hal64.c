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
    NET_DEV_NONE = 0,
    NET_DEV_RTL8139,
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

/* 外部网卡驱动函数 */
extern void net64_poll(void);

/* RTL8139 原始驱动入口 */
extern int  rtl8139_available(void);
extern int  rtl8139_send(const uint8_t *data, uint32_t length);
extern int  rtl8139_recv(uint8_t *buffer, uint32_t max_length);
extern int  rtl8139_get_mac_i(uint8_t *mac);

/* E1000 原始驱动入口 */
extern int  e1000_available(void);
extern int  e1000_send(const uint8_t *data, uint32_t length);
extern int  e1000_recv(uint8_t *buffer, uint32_t max_length);
extern int  e1000_get_mac(uint8_t *mac);

/* PCI 配置空间读取（pci64.c 既有导出，本文件不改 pci64.c） */
extern uint32_t pci_config_read32(int bus, int dev, int func, int off);

/* RTL8139操作表 */
static net_dev_ops_t rtl8139_ops = {
    .init = NULL,
    .send = rtl8139_send,
    .recv = rtl8139_recv,
    .get_mac = rtl8139_get_mac_i,
    .set_ip = NULL,
    .get_state = NULL,
    .poll = net64_poll,
};

/* E1000操作表 */
static net_dev_ops_t e1000_ops = {
    .init = NULL,
    .send = e1000_send,
    .recv = e1000_recv,
    .get_mac = e1000_get_mac,
    .set_ip = NULL,
    .get_state = NULL,
    .poll = net64_poll,
};

/* 按 Vendor/Device ID 在 PCI 配置空间扫描网卡是否在位 */
static int hal_pci_match(uint16_t vendor, const uint16_t *devs, int ndev)
{
    for (int bus = 0; bus < 8; bus++) {
        for (int dev = 0; dev < 32; dev++) {
            for (int func = 0; func < 8; func++) {
                uint32_t w = pci_config_read32(bus, dev, func, 0);
                if (w == 0xFFFFFFFFu) continue;
                if ((w & 0xFFFFu) != vendor) continue;
                uint16_t did = (uint16_t)((w >> 16) & 0xFFFFu);
                for (int k = 0; k < ndev; k++) {
                    if (devs[k] == did) return 1;
                }
            }
        }
    }
    return 0;
}

/* 初始化网络HAL —— 按 PCI 探测结果绑定真实在位的网卡驱动；
 * 总线里既没有 E1000 也没有 RTL8139 时，明确上报 no NIC，不伪造 eth0。 */
int net_hal_init(void)
{
    vga_puts("[NET_HAL] Initializing network HAL...\n");

    net_mgr.device_count = 0;
    net_mgr.initialized = 1;
    net_mgr.default_device = 0;

    static const uint16_t e1000_ids[] = { 0x100E, 0x100F, 0x1000 };
    static const uint16_t rtl_ids[]    = { 0x8139 };

    int e1000_pci = hal_pci_match(0x8086, e1000_ids, 3);
    int rtl_pci   = hal_pci_match(0x10EC, rtl_ids, 1);

    net_device_t *dev = &net_mgr.devices[0];
    dev->tx_packets = 0;
    dev->rx_packets = 0;
    dev->tx_bytes = 0;
    dev->rx_bytes = 0;

    if (e1000_pci && e1000_available()) {
        dev->name = "eth0";
        dev->type = NET_DEV_E1000;
        dev->state = NET_DEV_INITIALIZED;
        dev->ops = &e1000_ops;
        net_mgr.device_count = 1;
        vga_puts("[NET_HAL] E1000 initialized (eth0)\n");
    } else if (rtl_pci && rtl8139_available()) {
        dev->name = "eth0";
        dev->type = NET_DEV_RTL8139;
        dev->state = NET_DEV_INITIALIZED;
        dev->ops = &rtl8139_ops;
        net_mgr.device_count = 1;
        vga_puts("[NET_HAL] RTL8139 initialized (eth0)\n");
    } else {
        dev->type = NET_DEV_NONE;
        dev->state = NET_DEV_ERROR;
        dev->ops = NULL;
        net_mgr.device_count = 0;
        vga_puts("[NET_HAL] no NIC found, network HAL idle\n");
    }

    if (net_mgr.device_count > 0) {
        vga_puts("[NET_HAL] Network HAL initialized: 1 device\n");
    }
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
