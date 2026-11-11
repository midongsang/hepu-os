/* ============================================================================
 * 赫普OS 64位 - ICMP协议（Internet控制消息协议）
 * 支持ping请求/应答
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>

#define ICMP_TYPE_ECHO_REPLY   0
#define ICMP_TYPE_ECHO_REQUEST 8

/* ICMP头 */
typedef struct {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;
    uint16_t id;
    uint16_t seq;
} __attribute__((packed)) icmp_header_t;

extern void vga_puts(const char *s);
extern int ip64_send(uint32_t dst_ip, uint8_t protocol, const uint8_t *data, int data_len);

static void icmp_memcpy(void *dst, const void *src, int n) {
    uint8_t *d = (uint8_t*)dst;
    const uint8_t *s = (const uint8_t*)src;
    for (int i = 0; i < n; i++) d[i] = s[i];
}

static uint16_t icmp_checksum(const uint8_t *data, int length)
{
    uint32_t sum = 0;
    for (int i = 0; i < length; i += 2) {
        uint16_t word = data[i] << 8;
        if (i + 1 < length) word |= data[i + 1];
        sum += word;
    }
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)(~sum);
}

static uint16_t htons16(uint16_t v) { return (v >> 8) | (v << 8); }

static int icmp_initialized = 0;
static uint16_t ping_id = 0x1234;
static uint16_t ping_seq = 0;

/* 初始化ICMP */
void icmp64_init(void)
{
    icmp_initialized = 1;
    vga_puts("[ICMP64] ICMP protocol initialized\n");
}

/* 发送ping请求 */
int icmp64_ping(uint32_t dst_ip)
{
    if (!icmp_initialized) return -1;

    uint8_t packet[64];
    icmp_header_t *icmp = (icmp_header_t *)packet;

    icmp->type = ICMP_TYPE_ECHO_REQUEST;
    icmp->code = 0;
    icmp->checksum = 0;
    icmp->id = htons16(ping_id);
    icmp->seq = htons16(ping_seq++);

    /* 填充数据 */
    for (int i = 8; i < 64; i++) packet[i] = (uint8_t)(i & 0xFF);

    icmp->checksum = htons16(icmp_checksum(packet, 64));   /* V0.57.0: 网络字节序 */

    return ip64_send(dst_ip, 1, packet, 64);
}

/* 处理收到的ICMP包 */
void icmp64_handle(const uint8_t *packet, int length, uint32_t src_ip)
{
    if (length < 8) return;

    icmp_header_t *icmp = (icmp_header_t *)packet;

    if (icmp->type == ICMP_TYPE_ECHO_REQUEST) {
        /* 收到ping请求，发送应答 */
        uint8_t reply[1500];
        if (length > 1500) length = 1500;

        icmp_memcpy(reply, packet, length);
        icmp_header_t *r = (icmp_header_t *)reply;
        r->type = ICMP_TYPE_ECHO_REPLY;
        r->checksum = 0;
        r->checksum = htons16(icmp_checksum(reply, length));   /* V0.57.0: 网络字节序 */

        ip64_send(src_ip, 1, reply, length);

        vga_puts("[ICMP64] Ping reply sent to ");
        /* 输出IP地址 */
        char ipstr[16];
        uint8_t *ip = (uint8_t*)&src_ip;
        int pos = 0;
        for (int i = 0; i < 4; i++) {
            if (ip[i] >= 100) { ipstr[pos++] = '0' + ip[i]/100; }
            if (ip[i] >= 10) { ipstr[pos++] = '0' + (ip[i]/10)%10; }
            ipstr[pos++] = '0' + ip[i]%10;
            if (i < 3) ipstr[pos++] = '.';
        }
        ipstr[pos] = 0;
        vga_puts(ipstr);
        vga_puts("\n");
    }
}
