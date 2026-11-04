/* ============================================================================
 * 赫普OS - 性能基准测试报告
 * 生成性能报告，支持CSV和文本格式导出
 * ========================================================================== */

#include <stdint.h>
#include "kbench.h"
#include "vga.h"
#include "string.h"

#define KBENCH_REPORT_BUF_SIZE 4096

static char report_buf[KBENCH_REPORT_BUF_SIZE];
static u32 report_pos = 0;
static int report_initialized = 0;

static void report_append(const char *str)
{
    if (!str) return;
    u32 len = strlen(str);
    if (report_pos + len >= KBENCH_REPORT_BUF_SIZE - 1) return;
    memcpy(report_buf + report_pos, str, len);
    report_pos += len;
    report_buf[report_pos] = '\0';
}

static void report_append_line(const char *str)
{
    report_append(str);
    report_append("\n");
}

static void report_append_u64(u64 value)
{
    char buf[32];
    u32 pos = 0;
    if (value == 0) {
        buf[pos++] = '0';
    } else {
        char tmp[32];
        u32 tpos = 0;
        while (value > 0) {
            tmp[tpos++] = '0' + (value % 10);
            value /= 10;
        }
        while (tpos > 0) buf[pos++] = tmp[--tpos];
    }
    buf[pos] = '\0';
    report_append(buf);
}

void kbench_report_init(void)
{
    memset(report_buf, 0, sizeof(report_buf));
    report_pos = 0;
    report_initialized = 1;
    vga_puts("[KBENCH-REPORT] perf report generator init \n");
}

int kbench_report_generate_text(const kbench_result_t *results, u32 count)
{
    if (!report_initialized || !results || count == 0) return -1;

    report_pos = 0;
    memset(report_buf, 0, sizeof(report_buf));

    report_append_line("+======================================================+");
    report_append_line("| Hepu OS perf benchmark report |");
    report_append_line("+======================================================+");
    report_append("");

    report_append_line("+----------------------+----------+----------+----------+----------+");
    report_append_line("│ name │ iteration count │ avg cycles │ min cycles │ max cycles │");
    report_append_line("+----------------------┼----------┼----------┼----------┼----------+");

    for (u32 i = 0; i < count; i++) {
        report_append("| ");
        report_append(results[i].name);
        /* 填充名称到20字符 */
        u32 name_len = strlen(results[i].name);
        for (u32 j = name_len; j < 20; j++) report_append(" ");
        report_append(" | ");

        report_append_u64(results[i].iterations);
        report_append(" | ");
        report_append_u64(results[i].avg_cycles);
        report_append(" | ");
        report_append_u64(results[i].min_cycles);
        report_append(" | ");
        report_append_u64(results[i].max_cycles);
        report_append_line(" |");
    }

    report_append_line("+----------------------+----------+----------+----------+----------+");
    report_append("");

    /* 计算汇总 */
    u64 total_avg = 0;
    for (u32 i = 0; i < count; i++) total_avg += results[i].avg_cycles;

    report_append(" test count: ");
    report_append_u64(count);
    report_append_line("");
    report_append(" avg cycles total &: ");
    report_append_u64(total_avg);
    report_append_line("");

    if (count > 0) {
        report_append(" overall avg: ");
        report_append_u64(total_avg / count);
        report_append_line(" cycles / ops ");
    }

    report_append("");
    report_append_line("=== report finish ===");

    vga_printf("[KBENCH-REPORT] text report generated: %u bytes \n", report_pos);
    return (int)report_pos;
}

int kbench_report_generate_csv(const kbench_result_t *results, u32 count)
{
    if (!report_initialized || !results || count == 0) return -1;

    report_pos = 0;
    memset(report_buf, 0, sizeof(report_buf));

    report_append_line("name,iterations,avg_cycles,min_cycles,max_cycles");

    for (u32 i = 0; i < count; i++) {
        report_append(results[i].name);
        report_append(",");
        report_append_u64(results[i].iterations);
        report_append(",");
        report_append_u64(results[i].avg_cycles);
        report_append(",");
        report_append_u64(results[i].min_cycles);
        report_append(",");
        report_append_u64(results[i].max_cycles);
        report_append_line("");
    }

    vga_printf("[KBENCH-REPORT] CSV report generated: %u bytes \n", report_pos);
    return (int)report_pos;
}

int kbench_report_get_buffer(char **buffer, u32 *size)
{
    if (!report_initialized) return -1;
    if (buffer) *buffer = report_buf;
    if (size) *size = report_pos;
    return 0;
}

int kbench_report_print(void)
{
    if (!report_initialized) return -1;
    vga_puts("\n=== bench report ===\n");
    vga_write(report_buf, report_pos);
    return 0;
}

int kbench_report_save(const char *path, int csv_format)
{
    if (!report_initialized || !path) return -1;

    vga_printf("[KBENCH-REPORT] save to: %s(format: %s)\n",
               path, csv_format ? "CSV" : " text ");

    /* 在实际系统中调用VFS写入文件 */
    return (int)report_pos;
}

int kbench_report_clear(void)
{
    if (!report_initialized) return -1;
    memset(report_buf, 0, sizeof(report_buf));
    report_pos = 0;
    return 0;
}
