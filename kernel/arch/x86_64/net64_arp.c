/* ============================================================================
 * 赫普OS 64位 - ARP协议（地址解析协议）
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>

#define ARP_TABLE_SIZE 16
#define ARP_TIMEOUT 300  /* 5分钟（以秒为单位，简化为计数器） */

/* ARP包结构 */
typedef struct {
    uint16_t htype;      /* 硬件类型 (1=以太网) */
    uint16_t ptype;      /* 协议类型 (0x0800=IP) */
    uint8_t  hlen;       /* 硬件地址长度 (6) */
    uint8_t  plen;       /* 协议地址长度 (4) */
    uint16_t oper;       /* 操作 (1=请求, 2=应答) */
    uint8_t  sha[6];     /* 发送者MAC */
    uint32_t spa;        /* 发送者IP */
    uint8_t  tha[6];     /* 目标MAC */
    uint32_t tpa;        /* 目标IP */
} __attribute__((packed)) arp_packet_t;

/* ARP表项 */
typedef struct {
    uint32_t ip;
    uint8_t  mac[6];
    int      valid;
    int      age;
} arp_entry_t;

static arp_entry_t arp_table[ARP_TABLE_SIZE];
static int arp_initialized = 0;

extern void vga_puts(const char *s);
extern int eth64_send(const uint8_t *data, uint32_t length);
extern int eth64_get_mac(uint8_t *mac);
extern uint32_t eth64_get_ip(void);

/* 内存操作 */
static void arp_memcpy(void *dst, const void *src, int n) {
    uint8_t *d = (uint8_t*)dst;
    const uint8_t *s = (const uint8_t*)src;
    for (int i = 0; i < n; i++) d[i] = s[i];
}

static void arp_memset(void *dst, uint8_t val, int n) {
    uint8_t *d = (uint8_t*)dst;
    for (int i = 0; i < n; i++) d[i] = val;
}

/* 初始化ARP */
void arp64_init(void)
{
    arp_memset(arp_table, 0, sizeof(arp_table));
    arp_initialized = 1;
    vga_puts("[ARP64] ARP protocol initialized\n");
}

/* 查找ARP表项 */
int arp64_lookup(uint32_t ip, uint8_t *mac)
{
    for (int i = 0; i < ARP_TABLE_SIZE; i++) {
        if (arp_table[i].valid && arp_table[i].ip == ip) {
            if (mac) arp_memcpy(mac, arp_table[i].mac, 6);
            return 0;
        }
    }
    return -1;
}

/* 添加ARP表项 */
void arp64_add(uint32_t ip, const uint8_t *mac)
{
    /* 查找已有表项 */
    for (int i = 0; i < ARP_TABLE_SIZE; i++) {
        if (arp_table[i].valid && arp_table[i].ip == ip) {
            arp_memcpy(arp_table[i].mac, mac, 6);
            arp_table[i].age = 0;
            return;
        }
    }
    /* 找空表项 */
    for (int i = 0; i < ARP_TABLE_SIZE; i++) {
        if (!arp_table[i].valid) {
            arp_table[i].ip = ip;
            arp_memcpy(arp_table[i].mac, mac, 6);
            arp_table[i].valid = 1;
            arp_table[i].age = 0;
            return;
        }
    }
    /* 替换最旧的 */
    int oldest = 0;
    for (int i = 1; i < ARP_TABLE_SIZE; i++) {
        if (arp_table[i].age > arp_table[oldest].age) oldest = i;
    }
    arp_table[oldest].ip = ip;
    arp_memcpy(arp_table[oldest].mac, mac, 6);
    arp_table[oldest].valid = 1;
    arp_table[oldest].age = 0;
}

/* 主机序 -> 网络序 (32位) */
static inline uint32_t arp_htonl(uint32_t v)
{
    return __builtin_bswap32(v);
}
/* 发送ARP请求 */
int arp64_request(uint32_t target_ip)
{
    uint8_t frame[42]; /* 14(以太网头) + 28(ARP包) */
    uint8_t my_mac[6];
    uint32_t my_ip = eth64_get_ip();

    eth64_get_mac(my_mac);

    /* 以太网头：目标=广播, 源=我的MAC, 类型=ARP(0x0806) */
    for (int i = 0; i < 6; i++) frame[i] = 0xFF; /* 广播 */
    arp_memcpy(frame + 6, my_mac, 6);
    frame[12] = 0x08; frame[13] = 0x06;

    /* ARP包 */
    arp_packet_t *arp = (arp_packet_t *)(frame + 14);
    arp->htype = 0x0100;  /* 以太网（大端） */
    arp->ptype = 0x0008;  /* IP（大端） */
    arp->hlen = 6;
    arp->plen = 4;
    arp->oper = 0x0100;   /* 请求（大端） */
    arp_memcpy(arp->sha, my_mac, 6);
    arp->spa = arp_htonl(my_ip);
    for (int i = 0; i < 6; i++) arp->tha[i] = 0;
    arp->tpa = arp_htonl(target_ip);

    int sret = eth64_send(frame, 42);
    return sret;
}

/* 处理收到的ARP包 */
void arp64_handle(const uint8_t *packet, int length)
{
    if (length < 28) return;

    arp_packet_t *arp = (arp_packet_t *)packet;

    /* 只处理以太网+IP */
    if (arp->htype != 0x0100 || arp->ptype != 0x0008) return;
    if (arp->hlen != 6 || arp->plen != 4) return;

    uint32_t my_ip = eth64_get_ip();
    vga_puts("[ARP64] rx oper="); vga_put_hex(arp->oper);
    vga_puts(" spa="); vga_put_hex(arp_htonl(arp->spa)); vga_puts("\n");

    /* 添加发送者到ARP表 (spa 是网络序, 转主机序) */
    arp64_add(arp_htonl(arp->spa), arp->sha);

    /* 如果是请求且目标是我，发送应答 */
    if (arp->oper == 0x0100 && arp->tpa == arp_htonl(my_ip)) {
        uint8_t frame[42];
        uint8_t my_mac[6];
        eth64_get_mac(my_mac);

        /* 以太网头 */
        arp_memcpy(frame, arp->sha, 6);
        arp_memcpy(frame + 6, my_mac, 6);
        frame[12] = 0x08; frame[13] = 0x06;

        /* ARP应答 */
        arp_packet_t *reply = (arp_packet_t *)(frame + 14);
        reply->htype = 0x0100;
        reply->ptype = 0x0008;
        reply->hlen = 6;
        reply->plen = 4;
        reply->oper = 0x0200;  /* 应答 */
        arp_memcpy(reply->sha, my_mac, 6);
        reply->spa = arp_htonl(my_ip);
        arp_memcpy(reply->tha, arp->sha, 6);
        reply->tpa = arp->spa;

        eth64_send(frame, 42);
    }
}

/* ARP表老化（每秒调用一次） */
void arp64_tick(void)
{
    for (int i = 0; i < ARP_TABLE_SIZE; i++) {
        if (arp_table[i].valid) {
            arp_table[i].age++;
            if (arp_table[i].age > ARP_TIMEOUT) {
                arp_table[i].valid = 0;
            }
        }
    }
}
