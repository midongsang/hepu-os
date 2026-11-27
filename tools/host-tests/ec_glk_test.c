/* _GLK=1 锁死策略端到端测试：EC 存在但未实现 FACS Global Lock 时，
 * init 必须跳过 _REG，poll 必须 fail-closed，且一个 EC 端口字节都不碰。 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "aml64.h"
#include "acpi_battery.h"
#include "ec64.h"
#include "battery_ssdt_blobs.h"

static int fails, checks;

static void check(int cond, const char *what)
{
    checks++;
    if (!cond) {
        printf("  FAIL: %s\n", what);
        fails++;
    }
}

static char logbuf[8192];
static int loglen;
void vga_puts(const char *s)
{
    while (s && *s && loglen < (int)sizeof(logbuf) - 1) logbuf[loglen++] = *s++;
    logbuf[loglen] = 0;
}
void vga_put_hex(uint64_t v) { (void)v; }
void *kmalloc(uint32_t size) { return calloc(1, size ? size : 1); }
uint64_t vmm64_phys_to_ptr(uint64_t phys) { return phys; }

static int outb_count;

uint8_t ec_host_inb(uint16_t port)
{
    (void)port;
    return 0x02;   /* IBF 恒置位；只要碰端口就会超时，便于暴露违规访问 */
}
void ec_host_outb(uint16_t port, uint8_t val)
{
    (void)port; (void)val;
    outb_count++;
}

int fi_table_count(void) { return 1; }
int fi_table_at(int i, const uint8_t **data, uint32_t *len)
{
    if (i != 0 || !data || !len) return -1;
    *data = g_battery_ec_glk_ssdt;
    *len = (uint32_t)sizeof(g_battery_ec_glk_ssdt);
    return 0;
}

static int has_log(const char *needle) { return strstr(logbuf, needle) != 0; }

int main(void)
{
    acpi_batt_sample_t s;
    uint8_t byte = 0;

    check(acpi_battery_init() == 0, "init 成功");
    check(has_log("[BAT] ec=0x1 dev=EC00"), "EC 设备已声明");
    check(has_log("[BAT] ec glk=1"), "_GLK=1 被识别");
    check(has_log("[BAT] ec lockout=1"), "进入 GLK 锁死分支");
    check(has_log("[BAT] ec reg=skip-glk"), "锁死时不调用 _REG");
    check(ec64_present() == 1 && ec64_glk() == 1, "ec64 状态为 present+glk");

    check(ec64_read(0, &byte) == -4, "直接读 EC 返回 -4（锁死）");
    check(outb_count == 0, "锁死路径不写 0x62/0x66");

    memset(&s, 0, sizeof(s));
    check(acpi_battery_poll(&s) == 2, "poll 返回 UNKNOWN(2)");
    check(s.percent == 0, "不产生假百分比");
    check(s.ac_present == 0, "无 AC 设备不伪造交流状态");
    check(outb_count == 0, "poll 结束仍未碰 EC 端口");

    printf("RESULT: %s (%d checks, %d failed)\n",
           fails ? "FAIL" : "PASS", checks, fails);
    return fails ? 1 : 0;
}
