/* ============================================================================
 * 赫普OS 64位 - 网络协议栈主入口
 * 负责数据包接收分发和协议栈初始化
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>

/* 以太网类型 */
#define ETH_TYPE_IP   0x0800
#define ETH_TYPE_ARP  0x0806

/* 以太网头 */
typedef struct {
    uint8_t  dst_mac[6];
    uint8_t  src_mac[6];
    uint16_t type;
} __attribute__((packed)) eth_header_t;

extern void vga_puts(const char *s);
extern void vga_put_hex(uint64_t v);
extern int eth64_recv(uint8_t *buffer, uint32_t max_length);
extern void arp64_handle(const uint8_t *packet, int length);
extern void ip64_handle(const uint8_t *frame, int length);
/* poll 在文件内先被连接流程调用、后定义, 前置原型消除隐式声明 */
extern void net64_poll(void);

/* 协议栈初始化 */
extern void eth64_init(void);
extern void arp64_init(void);
extern void ip64_init(void);
extern void icmp64_init(void);
extern void udp64_init(void);
extern void tcp64_init(void);

static uint16_t ntohs16(uint16_t v) { return (v >> 8) | (v << 8); }

static int net_initialized = 0;
static uint64_t net_rx_packets = 0;
static uint64_t net_tx_packets = 0;

/* 主机序 IP → "a.b.c.d" */
static void net_format_ip(uint32_t ip, char *out)
{
    uint32_t ip_net = ((ip & 0xFF) << 24) | ((ip & 0xFF00) << 8) |
                      ((ip & 0xFF0000) >> 8) | ((ip & 0xFF000000) >> 24);
    uint8_t *b = (uint8_t *)&ip_net;
    int pos = 0;
    for (int j = 0; j < 4; j++) {
        if (b[j] >= 100) out[pos++] = '0' + b[j] / 100;
        if (b[j] >= 10)  out[pos++] = '0' + (b[j] / 10) % 10;
        out[pos++] = '0' + b[j] % 10;
        if (j < 3) out[pos++] = '.';
    }
    out[pos] = 0;
}

/* 入网后一次性端到端探测：ICMP echo 网关 + 一次 TCP 握手尝试。
 * 仅在 DHCP 拿到租约后跑一轮；结果走串口分段取证。 */
void net64_postup_probe(void)
{
    static int ran = 0;
    if (ran) return;

    extern int dhcp64_is_bound(void);
    if (!dhcp64_is_bound()) return;
    ran = 1;

    extern uint32_t eth64_get_gateway(void);
    extern int icmp64_ping(uint32_t dst_ip);
    extern int tcp64_connect(uint32_t remote_ip, uint16_t remote_port, uint16_t local_port);
    extern int tcp64_get_state(int sock);
    extern uint64_t rtc64_get_timestamp(void);

    char buf[16];
    uint32_t gw = eth64_get_gateway();
    if (gw != 0) {
        net_format_ip(gw, buf);
        vga_puts("[NET64] Probe: ICMP echo to gateway ");
        vga_puts(buf);
        vga_puts("\n");
        icmp64_ping(gw);
    }

    /* TCP 握手探测：连 slirp 宿主 10.0.2.2:8081（宿主侧需有监听端口） */
    vga_puts("[NET64] Probe: TCP connect to 10.0.2.2:8081\n");
    int sock = tcp64_connect(0x0A000202u, 8081, 20000);
    vga_puts("[NET64] Probe: TCP connect sock=");
    vga_put_hex((uint64_t)sock);
    vga_puts("\n");

    if (sock >= 0) {
        uint64_t t0 = rtc64_get_timestamp();
        while (rtc64_get_timestamp() - t0 < 3) {
            net64_poll();
            if (tcp64_get_state(sock) == 4 /* TCP_ESTABLISHED */) break;
            for (volatile int d = 0; d < 100000; d++);
        }
        int st = tcp64_get_state(sock);
        vga_puts("[NET64] Probe: TCP state=");
        vga_put_hex((uint64_t)st);
        vga_puts("\n");
        /* 取证结束即关闭，避免占用有限的 TCP 连接槽位 */
        extern int tcp64_close(int sock);
        tcp64_close(sock);
    }
}

/* 初始化整个网络协议栈 */
void net64_init(void)
{
    vga_puts("[NET64] Initializing network stack...\n");

    eth64_init();
    arp64_init();
    ip64_init();
    icmp64_init();
    udp64_init();
    tcp64_init();

    net_initialized = 1;
    vga_puts("[OK] Network stack initialized (ETH/ARP/IP/ICMP/UDP/TCP)\n");
}

/* 网络轮询（处理接收数据包） */
void net64_poll(void)
{
    if (!net_initialized) return;

    uint8_t buffer[1514];
    int len = eth64_recv(buffer, sizeof(buffer));

    if (len <= 0) return;

    net_rx_packets++;

    if (len < 14) return;

    eth_header_t *eth = (eth_header_t *)buffer;
    uint16_t type = ntohs16(eth->type);

    switch (type) {
        case ETH_TYPE_ARP:
            arp64_handle(buffer + 14, len - 14);
            break;
        case ETH_TYPE_IP:
            ip64_handle(buffer, len);
            break;
    }
}

/* 获取网络字节统计 (供任务管理器网速显示) */
void net64_get_bytes(uint64_t *rx, uint64_t *tx)
{
    extern int eth64_get_bytes(uint64_t *r, uint64_t *t);
    if (eth64_get_bytes(rx, tx) != 0) {
        if (rx) *rx = 0;
        if (tx) *tx = 0;
    }
}

/* 获取网络统计 */
void net64_get_stats(uint64_t *rx, uint64_t *tx)
{
    if (rx) *rx = net_rx_packets;
    if (tx) *tx = net_tx_packets;
}
