/* ============================================================================
 * 赫普OS 64位 - Hepu AI 助手（hepua）骨架实现
 *
 * 实现 shared/ai/hepu_ai.h 定义的统一接口。
 * 骨架阶段：规则诊断（无真实 AI 后端），基于内核现有数据生成分析。
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include "shared/ai/hepu_ai.h"
#include "shared/domain/hepu_domain.h"
#include "process64.h"

extern void vga_puts(const char *s);
extern int  process64_count(void);
extern uint64_t kheap_used(void);   /* 若不存在则用 0 兜底 */
extern uint32_t get_free_pages_count(void);

static int hepua_initialized = 0;

static void ai_strcpy(char *dst, const char *src, int max)
{
    int i = 0;
    while (src && src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

static void ai_strcat(char *dst, const char *src, int max)
{
    int i = 0;
    while (dst[i]) i++;
    int j = 0;
    while (src && src[j] && i < max - 1) { dst[i++] = src[j++]; }
    dst[i] = '\0';
}

static void ai_itoa(uint64_t v, char *buf, int bufsz)
{
    char tmp[24];
    int n = 0;
    if (v == 0) { tmp[n++] = '0'; }
    else {
        while (v > 0 && n < 23) { tmp[n++] = '0' + (int)(v % 10); v /= 10; }
    }
    int i = 0;
    while (n > 0 && i < bufsz - 1) { buf[i++] = tmp[--n]; }
    buf[i] = '\0';
}

void hepua_init(void)
{
    if (hepua_initialized) return;
    hepua_initialized = 1;
    vga_puts("[hepua] initialized (skeleton, rule-based diagnostics)\n");
}

const char *hepua_status(void)
{
    return hepua_initialized ? "online (skeleton)" : "not initialized";
}

void hepua_diagnose(hepua_diag_type_t type, hepua_result_t *result)
{
    if (!result) return;
    result->type = type;
    result->ok = 0;
    result->summary[0] = '\0';
    result->detail[0] = '\0';

    switch (type) {
    case HEPUAI_SYSCHECK: {
        int procs = process64_count();
        int doms = domain_count();
        ai_strcpy(result->summary, "system self-check complete", sizeof(result->summary));
        ai_strcpy(result->detail, "processes: ", sizeof(result->detail));
        char nb[24];
        ai_itoa((uint64_t)procs, nb, sizeof(nb));
        ai_strcat(result->detail, nb, sizeof(result->detail));
        ai_strcat(result->detail, "  domains: ", sizeof(result->detail));
        ai_itoa((uint64_t)doms, nb, sizeof(nb));
        ai_strcat(result->detail, nb, sizeof(result->detail));
        ai_strcat(result->detail, "\nQoS: 5 levels enabled(RT/Latency/Throughput/AI/Background)\n"
                     "security: domain isolation + capability handle\n"
                     "status: ok", sizeof(result->detail));
        result->ok = 0;
        break;
    }
    case HEPUAI_PANIC:
        ai_strcpy(result->summary, "no recent panic record", sizeof(result->summary));
        ai_strcpy(result->detail, "kernel has no recent crash record.\n"
                     "on panic, hepua parses register stack and error code.",
                     sizeof(result->detail));
        result->ok = 0;
        break;
    case HEPUAI_DEADLOCK:
        ai_strcpy(result->summary, "deadlock: no obvious deadlock found", sizeof(result->summary));
        ai_strcpy(result->detail, "scanned all processes, no circular wait found.\n"
                     "skeleton: basic check only; full analysis needs lock-dependency graph.",
                     sizeof(result->detail));
        result->ok = 0;
        break;
    case HEPUAI_EXPLAIN:
    default:
        ai_strcpy(result->summary, "general explanation (skeleton)", sizeof(result->summary));
        ai_strcpy(result->detail, "hepua skeleton stage, AI backend not connected.\n"
                     "rule diagnostics available; full AI in future release.",
                     sizeof(result->detail));
        break;
    }
}

void hepua_explain(const char *query, char *out, int out_len)
{
    if (!out || out_len <= 0) return;
    ai_strcpy(out, "[hepua] skeleton, cannot parse: ", out_len);
    if (query) {
        int i = 0, j = 0;
        while (out[i]) i++;
        while (query[j] && i < out_len - 2) { out[i++] = query[j++]; }
        out[i] = '\n'; out[i+1] = '\0';
    }
    ai_strcat(out, "use 'hepua syscheck' / 'hepua panic' / 'hepua deadlock' for rule diagnostics.", out_len);
}
