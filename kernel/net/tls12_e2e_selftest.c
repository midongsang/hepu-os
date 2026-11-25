/* ============================================================================
 * TLS 1.2 端到端开机自检（编译期门控，默认关闭）
 *
 * 仅在 `make TLS_E2E_SELFTEST=1`（-DHEPU_TLS_E2E_SELFTEST）的联调测试构建中
 * 编入；默认构建（gate.sh / regress64.sh）完全不进本文件代码。
 *
 * 流程：等 DHCP 拿到非 0 IP → 打印本机 IP / 网关 → 连 10.0.2.2:4433 跑一次
 * 真实 TLS1.2 握手 + 加密 GET → 把握手结果、套件、证书链张数、失败阶段/原因、
 * 解密响应正文片段打到串口。TCP 未建立与 TLS 握手失败由 tls12_client_run 分阶段
 * 上报，日志上可区分。
 * ========================================================================== */

#include <stdint.h>
#include <string.h>
#include "tls12_client.h"
#include "vga.h"
#include "kstdio.h"

#if defined(HEPU_TLS_E2E_SELFTEST) && HEPU_TLS_E2E_SELFTEST

extern uint32_t dhcp64_get_ip(void);
extern int      dhcp64_poll(void);
extern uint32_t eth64_get_gateway(void);
extern void     net64_poll(void);
extern uint64_t rtc64_get_timestamp(void);

void tls12_e2e_selftest(void)
{
    vga_puts("[TLS-E2E] 开机自检：等待 DHCP 就绪...\n");

    uint64_t t0 = rtc64_get_timestamp();
    while (dhcp64_get_ip() == 0) {
        dhcp64_poll();
        net64_poll();
        if (rtc64_get_timestamp() - t0 > 20) break;
        for (volatile int dw = 0; dw < 10000; dw++) { }
    }
    uint32_t lip = dhcp64_get_ip();
    uint32_t gw  = eth64_get_gateway();
    {
        char ipbuf[40];
        snprintf(ipbuf, sizeof(ipbuf),
                 "[TLS-E2E] 本机 IP=%d.%d.%d.%d 网关=%d.%d.%d.%d\n",
                 (int)((lip >> 24) & 0xff), (int)((lip >> 16) & 0xff),
                 (int)((lip >> 8) & 0xff),  (int)(lip & 0xff),
                 (int)((gw >> 24) & 0xff),  (int)((gw >> 16) & 0xff),
                 (int)((gw >> 8) & 0xff),  (int)(gw & 0xff));
        vga_puts(ipbuf);
    }

    tls12_result_t res;
    uint8_t resp[4096];
    uint32_t rlen = 0;

    vga_puts("[TLS-E2E] 连接 10.0.2.2:4433，跑真实 TLS1.2 握手 + 加密 GET\n");
    int rc = tls12_client_run("10.0.2.2", 4433, "/",
                              resp, sizeof(resp), &rlen, &res);

    if (rc == 0 && res.complete && rlen == 0) {
        vga_puts("[TLS-E2E] 结果: 握手完成但加密响应为空\n");
        vga_puts("[TLS-E2E] TLS1.2 端到端未通过\n");
    } else if (rc == 0 && res.complete) {
        vga_printf("[TLS-E2E] 结果: 握手完成 cipher=0x%x 证书链=%d 张\n",
                   res.cipher_suite, res.cert_count);
        vga_printf("[TLS-E2E] 解密响应正文 %u 字节，片段: ", rlen);
        {
            char snippet[121];
            uint32_t n = rlen < 120 ? rlen : 120;
            for (uint32_t i = 0; i < n; i++) {
                uint8_t c = resp[i];
                snippet[i] = (c >= 0x20 && c < 0x7f) ? (char)c : '.';
            }
            snippet[n] = 0;
            vga_puts(snippet);
        }
        vga_puts("\n[TLS-E2E] TLS1.2 端到端通过\n");
    } else {
        vga_printf("[TLS-E2E] 结果: 握手未完成 阶段=%s 原因=%s\n",
                   res.fail_stage, res.fail_reason);
        vga_puts("[TLS-E2E] TLS1.2 端到端未通过\n");
    }
}

#endif /* HEPU_TLS_E2E_SELFTEST */
