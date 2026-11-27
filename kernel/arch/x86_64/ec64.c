/* 见 ec64.h 顶部说明。 */

#include "ec64.h"

#ifdef AML_HOST_TEST
/* 主机单测注入一个模拟 EC 状态机（tools/host-tests/aml_eval_test.c）。 */
extern uint8_t ec_host_inb(uint16_t port);
extern void ec_host_outb(uint16_t port, uint8_t val);

static uint8_t ec_inb(uint16_t port) { return ec_host_inb(port); }
static void ec_outb(uint16_t port, uint8_t val) { ec_host_outb(port, val); }
#else
static uint8_t ec_inb(uint16_t port)
{
    uint8_t v;
    __asm__ __volatile__("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static void ec_outb(uint16_t port, uint8_t val)
{
    __asm__ __volatile__("outb %0, %1" : : "a"(val), "Nd"(port));
}
#endif

#define EC_DATA 0x62
#define EC_SC   0x66
#define EC_OBF  0x01
#define EC_IBF  0x02
#define EC_SPIN 20000

static int g_ec_present;

void ec64_set_present(int present) { g_ec_present = present ? 1 : 0; }
int ec64_present(void) { return g_ec_present; }

static int ec_wait_ibf_clear(void)
{
    int i;
    for (i = 0; i < EC_SPIN; i++) {
        if ((ec_inb(EC_SC) & EC_IBF) == 0) return 0;
    }
    return -1;
}

static int ec_wait_obf_set(void)
{
    int i;
    for (i = 0; i < EC_SPIN; i++) {
        if (ec_inb(EC_SC) & EC_OBF) return 0;
    }
    return -1;
}

int ec64_read(uint8_t offset, uint8_t *out)
{
    if (!g_ec_present || !out) return -2;

    /* 丢掉上一次遗留的输出字节，避免把旧数据当成本次读数。 */
    if (ec_inb(EC_SC) & EC_OBF) (void)ec_inb(EC_DATA);

    /* 命令 0x80 = RD_EC；数据 = EC 地址空间偏移。 */
    if (ec_wait_ibf_clear() != 0) return -3;
    ec_outb(EC_SC, 0x80);
    if (ec_wait_ibf_clear() != 0) return -3;
    ec_outb(EC_DATA, offset);
    if (ec_wait_ibf_clear() != 0) return -3;
    if (ec_wait_obf_set() != 0) return -3;

    *out = ec_inb(EC_DATA);
    return 0;
}
