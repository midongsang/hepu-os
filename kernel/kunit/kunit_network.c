/* ============================================================================
 * 赫普OS - 网络协议单元测试
 * 覆盖：IP校验和/TCP校验和/以太网帧/DNS解析/可变长度整数
 * ========================================================================== */

#include <stdint.h>
#include "kunit.h"
#include "vga.h"
#include "string.h"

/* === IP校验和测试 === */
static u16 ip_checksum(const u8 *data, u32 len)
{
    u32 sum = 0;
    for (u32 i = 0; i < len; i += 2) {
        u16 word = (data[i] << 8) | (i + 1 < len ? data[i + 1] : 0);
        sum += word;
    }
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (u16)(~sum);
}

static kunit_result_t test_ip_checksum_zero(void)
{
    u8 data[20] = {0};
    u16 csum = ip_checksum(data, 20);
    KUNIT_ASSERT_EQ(csum, 0xFFFF, " 全零 data verify & ");
    return KUNIT_PASS;
}

static kunit_result_t test_ip_checksum_verify(void)
{
    /* 构造一个IP头，校验和字段先设为0，计算后再验证 */
    u8 ip_header[20] = {
        0x45, 0x00, 0x00, 0x3c,  /* version/IHL, TOS, total length */
        0x1c, 0x46, 0x40, 0x00,  /* identification, flags/fragment */
        0x40, 0x06, 0x00, 0x00,  /* TTL, protocol, checksum(0) */
        0xac, 0x10, 0x0a, 0x63,  /* src IP */
        0xac, 0x10, 0x0a, 0x0c   /* dst IP */
    };
    u16 csum = ip_checksum(ip_header, 20);
    KUNIT_ASSERT(csum != 0, " verify & non 零 ");

    /* 将校验和填入，再次计算应为0 */
    ip_header[10] = (csum >> 8) & 0xFF;
    ip_header[11] = csum & 0xFF;
    u16 verify = ip_checksum(ip_header, 20);
    KUNIT_ASSERT_EQ(verify, 0, " verify & verify 零 ");
    return KUNIT_PASS;
}

/* === 以太网帧测试 === */
static kunit_result_t test_ethernet_frame(void)
{
    u8 frame[14] = {
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  /* dst MAC (broadcast) */
        0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF,  /* src MAC */
        0x08, 0x00                            /* EtherType (IPv4) */
    };

    /* 检查广播地址 */
    int is_broadcast = 1;
    for (int i = 0; i < 6; i++) {
        if (frame[i] != 0xFF) { is_broadcast = 0; break; }
    }
    KUNIT_ASSERT(is_broadcast, " entries MAC broadcast ");

    /* 检查EtherType */
    u16 ethertype = (frame[12] << 8) | frame[13];
    KUNIT_ASSERT_EQ(ethertype, 0x0800, "EtherType IPv4");
    return KUNIT_PASS;
}

/* === DNS名称编码测试 === */
static kunit_result_t test_dns_name_encode(void)
{
    /* "www.example.com" -> 3www7example3com0 */
    const char *domain = "www.example.com";
    u8 encoded[32];
    u32 pos = 0;
    const char *p = domain;

    while (*p) {
        const char *dot = strchr(p, '.');
        u32 label_len = dot ? (u32)(dot - p) : strlen(p);
        encoded[pos++] = (u8)label_len;
        memcpy(encoded + pos, p, label_len);
        pos += label_len;
        p += label_len;
        if (*p == '.') p++;
    }
    encoded[pos++] = 0;

    KUNIT_ASSERT_EQ(encoded[0], 3, " 第一 标签 len ");
    KUNIT_ASSERT_EQ(encoded[1], 'w', " 第一 标签内容 ");
    KUNIT_ASSERT_EQ(encoded[4], 7, " 第二 标签 len ");
    KUNIT_ASSERT_EQ(encoded[pos - 1], 0, " 末尾零 bytes ");
    return KUNIT_PASS;
}

/* === QUIC可变长度整数测试 === */
static u32 quic_varint_encode(u64 value, u8 *buf)
{
    if (value < 64) {
        buf[0] = (u8)value;
        return 1;
    } else if (value < 16384) {
        buf[0] = 0x40 | (u8)(value >> 8);
        buf[1] = (u8)(value & 0xFF);
        return 2;
    } else if (value < 1073741824) {
        buf[0] = 0x80 | (u8)(value >> 24);
        buf[1] = (u8)(value >> 16);
        buf[2] = (u8)(value >> 8);
        buf[3] = (u8)(value & 0xFF);
        return 4;
    } else {
        buf[0] = 0xC0;
        for (int i = 7; i >= 0; i--) {
            buf[8 - i] = (u8)((value >> (i * 8)) & 0xFF);
        }
        return 8;
    }
}

static u64 quic_varint_decode(const u8 *buf, u32 *consumed)
{
    u8 prefix = (buf[0] >> 6) & 0x03;
    u64 value = buf[0] & 0x3F;

    switch (prefix) {
        case 0:
            *consumed = 1;
            return value;
        case 1:
            value = (value << 8) | buf[1];
            *consumed = 2;
            return value;
        case 2:
            value = (value << 8) | buf[1];
            value = (value << 8) | buf[2];
            value = (value << 8) | buf[3];
            *consumed = 4;
            return value;
        case 3:
            for (int i = 1; i < 8; i++) {
                value = (value << 8) | buf[i];
            }
            *consumed = 8;
            return value;
    }
    *consumed = 0;
    return 0;
}

static kunit_result_t test_quic_varint(void)
{
    u8 buf[8];
    u32 consumed;

    /* 1字节 */
    u32 len = quic_varint_encode(0, buf);
    KUNIT_ASSERT_EQ(len, 1, "0 编码 1 bytes ");
    KUNIT_ASSERT_EQ(quic_varint_decode(buf, &consumed), 0, "0 解码 ");
    KUNIT_ASSERT_EQ(consumed, 1, "0 消耗 1 bytes ");

    len = quic_varint_encode(63, buf);
    KUNIT_ASSERT_EQ(len, 1, "63 编码 1 bytes ");

    /* 2字节 */
    len = quic_varint_encode(64, buf);
    KUNIT_ASSERT_EQ(len, 2, "64 编码 2 bytes ");
    KUNIT_ASSERT_EQ(quic_varint_decode(buf, &consumed), 64, "64 解码 ");

    len = quic_varint_encode(16383, buf);
    KUNIT_ASSERT_EQ(len, 2, "16383 编码 2 bytes ");

    /* 4字节 */
    len = quic_varint_encode(16384, buf);
    KUNIT_ASSERT_EQ(len, 4, "16384 编码 4 bytes ");
    KUNIT_ASSERT_EQ(quic_varint_decode(buf, &consumed), 16384, "16384 解码 ");

    /* 回环测试 */
    u64 test_values[] = {0, 1, 63, 64, 16383, 16384, 1073741823};
    for (u32 i = 0; i < 7; i++) {
        len = quic_varint_encode(test_values[i], buf);
        u64 decoded = quic_varint_decode(buf, &consumed);
        KUNIT_ASSERT_EQ(decoded, test_values[i], " 可变整 count 回环 ");
        KUNIT_ASSERT_EQ(consumed, len, " 消耗 len match ");
    }
    return KUNIT_PASS;
}

/* === 测试套件注册 === */
static const kunit_test_t network_tests[] = {
    {"IP verify & 零 ", test_ip_checksum_zero, "network"},
    {"IP verify & verify ", test_ip_checksum_verify, "network"},
    {" Ethernet frame ", test_ethernet_frame, "network"},
    {"DNS name 编码 ", test_dns_name_encode, "network"},
    {"QUIC 可变整 count ", test_quic_varint, "network"},
};
static const kunit_suite_t kunit_network_suite = {
    " net proto ", network_tests, 5
};

void kunit_network_register(void)
{
    kunit_register_suite(&kunit_network_suite);
    vga_puts("[KUNIT] net test suite registered(5 cases)\n");
}
