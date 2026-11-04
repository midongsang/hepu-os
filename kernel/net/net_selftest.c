/*
 * HepuOS 网络自测试模块
 * 在系统启动后自动测试网络栈
 */
#include "types.h"
#include "string.h"

extern void serial_puts(uint16_t port, const char *s);
extern void net_poll(void);
extern int dhcp_start(const char *interface);
extern int icmp_send_echo_request(uint32_t dest_ip, uint16_t id, uint16_t seq,
                                   const uint8_t *data, uint16_t data_len);

static int selftest_done = 0;

void network_selftest(void)
{
    if (selftest_done) return;
    selftest_done = 1;

    serial_puts(0x3F8, "[NET-TEST] === Network Self-Test ===\n");

    /* 测试1: DHCP启动（非阻塞） */
    serial_puts(0x3F8, "[NET-TEST] Starting DHCP...\n");
    int ret = dhcp_start("eth0");
    if (ret == 0) {
        serial_puts(0x3F8, "[NET-TEST] DHCP started\n");
    } else {
        serial_puts(0x3F8, "[NET-TEST] DHCP start failed (may need more init)\n");
    }

    /* 测试2: 轮询网络包 */
    serial_puts(0x3F8, "[NET-TEST] Polling network packets...\n");
    for (int i = 0; i < 20; i++) {
        net_poll();
    }

    /* 测试3: ICMP ping 测试（ping 8.8.8.8 或网关） */
    serial_puts(0x3F8, "[NET-TEST] Sending ICMP echo to 8.8.8.8...\n");
    icmp_send_echo_request(0x08080808, 0x1234, 1, (const uint8_t *)"HepuOS", 6);
    
    /* 轮询响应 */
    for (int i = 0; i < 10; i++) {
        net_poll();
    }

    serial_puts(0x3F8, "[NET-TEST] === Self-Test Complete ===\n");
}
