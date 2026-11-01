/* ============================================================================
 * 赫普OS - 移动桌面计算器应用
 * 从 mobile.c 拆分
 * ========================================================================== */

#include "mobile.h"
#include "vga_gfx.h"
#include "font_zh.h"
#include <string.h>


/* ---- 应用启动 (全屏页) ---- */
/* V2.43: 计算器虚拟键盘 (鼠标点击 + 键盘输入) */
/* V2.88: 科学模式 (S 切换, q/e/t/f/n/z 单目函数) */
void mobile_run_calc(void)
{
    calc_len = 0;
    calc_buf[0] = 0;
    int calc_sci = 0;   /* V2.88: 0=基础 1=科学 */
    int running = 1;
    while (running) {
        ui_draw_app_bg();   /* V3.50: TRON 网格背景 */
        gfx_fillrect(0, 0, gfx_width, 14, mob_accent);
        gfx_puts(4, 3, "Back: ESC", 0x0F);
        if (calc_sci)
            ui_title("计算器SCI", "Calc SCI", 18);
        else
            ui_title("计算器", "Calc", 18);
        if (calc_sci)
            gfx_puts(150, 18, calc_rad ? "RAD" : "DEG", calc_rad ? 0x0C : 0x0A);
        if (calc_sci) {
            /* V2.92: 函数槽 3 行 (含三角/对数) */
            gfx_puts(8, 32, "q:sqrt e:sqr t:2^n f:!", 0x08);
            gfx_puts(8, 44, "y:sin  u:cos i:log o:ln", 0x08);
            gfx_puts(8, 56, "g:tan h:asin j:acos k:atan", 0x08);
            gfx_puts(8, 68, "n:neg z:abs p:Rad S:basic", 0x08);
        }
        /* 显示行 */
        int disp_y = calc_sci ? 76 : 34;
        gfx_fillrect(8, disp_y, gfx_width - 16, 20, 0x00);
        gfx_rect(8, disp_y, gfx_width - 16, 20, 0x0F);
        gfx_puts(12, disp_y + 6, calc_buf[0] ? calc_buf : "0", 0x0F);
        /* 键盘 4x4 */
        int kx0 = 8, ky0 = calc_sci ? 96 : 62;
        int kw = (gfx_width - 16) / 4 - 2;
        int kh = calc_sci ? 24 : 26;
        for (int i = 0; i < 16; i++) {
            int kx = kx0 + (i % 4) * (kw + 2);
            int ky = ky0 + (i / 4) * (kh + 2);
            uint8_t bg = (i / 4 == 3 && i % 4 == 1) ? 0x0C : 0x08;  /* C 红色 */
            gfx_fillrect(kx, ky, kw, kh, bg);
            gfx_rect(kx, ky, kw, kh, 0x0F);
            gfx_puts(kx + kw / 2 - 4, ky + kh / 2 - 4, calc_keys[i], 0x0F);
        }
        gfx_flip();
        /* 键盘输入 */
        char kc;
        while (keyboard_try_getchar(&kc)) {
            if (kc == 0x1B) { running = 0; break; }
            if (kc == 's' || kc == 'S') { calc_sci = !calc_sci; }
            else if (calc_sci && (kc == 'q' || kc == 'Q')) calc_apply_func(calc_isqrt);
            else if (calc_sci && (kc == 'e' || kc == 'E')) {
                int v = 0;
                for (int i = 0; calc_buf[i] >= '0' && calc_buf[i] <= '9'; i++)
                    v = v * 10 + (calc_buf[i] - '0');
                if (v > 46340) { calc_put_err(); }
                else {
                    int r = v * v;
                    calc_len = 0;
                    char tmp[16]; int ti = 0;
                    if (r == 0) tmp[ti++] = '0';
                    while (r > 0) { tmp[ti++] = '0' + (r % 10); r /= 10; }
                    for (int j = ti - 1; j >= 0 && calc_len < CALC_BUF_MAX - 1; j--)
                        calc_buf[calc_len++] = tmp[j];
                    calc_buf[calc_len] = 0;
                }
            }
            else if (calc_sci && (kc == 't' || kc == 'T')) {
                int v = 0;
                for (int i = 0; calc_buf[i] >= '0' && calc_buf[i] <= '9'; i++)
                    v = v * 10 + (calc_buf[i] - '0');
                if (v < 0 || v > 30) { calc_put_err(); }
                else {
                    int r = 1;
                    for (int i = 0; i < v; i++) r *= 2;
                    calc_len = 0;
                    char tmp[16]; int ti = 0;
                    if (r == 0) tmp[ti++] = '0';
                    while (r > 0) { tmp[ti++] = '0' + (r % 10); r /= 10; }
                    for (int j = ti - 1; j >= 0 && calc_len < CALC_BUF_MAX - 1; j--)
                        calc_buf[calc_len++] = tmp[j];
                    calc_buf[calc_len] = 0;
                }
            }
            else if (calc_sci && (kc == 'y' || kc == 'Y')) {
                /* V2.92: sin x100 定点; V3.03: RAD 输入换算 */
                int v = 0;
                for (int i = 0; calc_buf[i] >= '0' && calc_buf[i] <= '9'; i++)
                    v = v * 10 + (calc_buf[i] - '0');
                if (calc_rad) v = v * 573 / 10;   /* 1rad ≈ 57.3° */
                calc_put_fixed(calc_sin(v));
            }
            else if (calc_sci && (kc == 'u' || kc == 'U')) {
                int v = 0;
                for (int i = 0; calc_buf[i] >= '0' && calc_buf[i] <= '9'; i++)
                    v = v * 10 + (calc_buf[i] - '0');
                if (calc_rad) v = v * 573 / 10;
                calc_put_fixed(calc_cos(v));
            }
            /* V3.03: tan / 反三角 / 模式切换 */
            else if (calc_sci && (kc == 'g' || kc == 'G')) {
                int v = 0;
                for (int i = 0; calc_buf[i] >= '0' && calc_buf[i] <= '9'; i++)
                    v = v * 10 + (calc_buf[i] - '0');
                if (calc_rad) v = v * 573 / 10;
                calc_put_fixed(calc_tan(v));
            }
            else if (calc_sci && (kc == 'h' || kc == 'H')) calc_apply_func(calc_asin);
            else if (calc_sci && (kc == 'j' || kc == 'J')) calc_apply_func(calc_acos);
            else if (calc_sci && (kc == 'k' || kc == 'K')) calc_apply_func(calc_atan);
            else if (calc_sci && (kc == 'p' || kc == 'P')) calc_rad = !calc_rad;
            else if (calc_sci && (kc == 'i' || kc == 'I')) {
                calc_apply_func(calc_log10);
            }
            else if (calc_sci && (kc == 'o' || kc == 'O')) {
                int v = 0;
                for (int i = 0; calc_buf[i] >= '0' && calc_buf[i] <= '9'; i++)
                    v = v * 10 + (calc_buf[i] - '0');
                int l = calc_ln(v);
                if (l == CALC_ERR) { calc_put_err(); }
                else calc_put_fixed(l);
            }
            else if (calc_sci && (kc == 'f' || kc == 'F')) calc_apply_func(calc_fact);
            else if (calc_sci && (kc == 'n' || kc == 'N')) {
                if (calc_buf[0] == '-') {
                    for (int i = 0; calc_buf[i]; i++) calc_buf[i] = calc_buf[i + 1];
                } else if (calc_len < CALC_BUF_MAX - 1) {
                    for (int i = calc_len; i >= 0; i--) calc_buf[i + 1] = calc_buf[i];
                    calc_buf[0] = '-'; calc_len++;
                }
            }
            else if (calc_sci && (kc == 'z' || kc == 'Z')) {
                if (calc_buf[0] == '-') {
                    for (int i = 0; calc_buf[i]; i++) calc_buf[i] = calc_buf[i + 1];
                }
            }
            else if (kc >= '0' && kc <= '9' && calc_len < CALC_INT_MAX_DIGITS) {
                if (calc_buf[0] == 'E') { calc_len = 0; calc_buf[0] = 0; }
                calc_buf[calc_len++] = kc; calc_buf[calc_len] = 0;
            } else if (kc == '+' || kc == '-' || kc == '.' || kc == '/') {
                if (calc_len < CALC_BUF_MAX - 1) { calc_buf[calc_len++] = kc; calc_buf[calc_len] = 0; }
            } else if (kc == '=' || kc == '\n' || kc == '\r') {
                /* 简易计算: 只支持 a op b 单步 */
                int a = 0, b = 0; char op = 0; int has_op = 0; int num = 0;
                for (int i = 0; calc_buf[i]; i++) {
                    char c = calc_buf[i];
                    if (c >= '0' && c <= '9') num = num * 10 + (c - '0');
                    else { if (!has_op) { a = num; op = c; has_op = 1; num = 0; }
                           else b = num; }
                }
                if (has_op) {
                    b = num;
                    int r = 0;
                    int overflow = 0;   /* V3.25: 运算溢出标记 */
                    if (op == '+') { if (b > 0 && a > 2147483647 - b) overflow = 1; else r = a + b; }
                    else if (op == '-') { if (b < 0 && a > 2147483647 + b) overflow = 1; else r = a - b; }
                    else if (op == '/') r = (b != 0) ? a / b : 0;
                    else {
                        /* V3.25: 乘法溢出检测——9 位x9 位可超 INT_MAX */
                        if (a != 0 && b != 0 && (a > 2147483647 / (b > 0 ? b : -b))) overflow = 1;
                        else r = a * b;
                    }
                    if (overflow) { calc_put_err(); }
                    else {
                    calc_len = 0;
                    int ri = r;
                    char tmp[12]; int ti = 0;
                    if (ri == 0) tmp[ti++] = '0';
                    while (ri > 0) { tmp[ti++] = '0' + (ri % 10); ri /= 10; }
                    for (int j = ti - 1; j >= 0 && calc_len < CALC_BUF_MAX - 1; j--)
                        calc_buf[calc_len++] = tmp[j];
                    calc_buf[calc_len] = 0;
                    }
                }
            } else if (kc == 'c' || kc == 'C') {
                calc_len = 0; calc_buf[0] = 0;
            }
        }
        for (volatile int d = 0; d < 150000; d++) ;
    }
}

/* V2.44: 电话拨号应用 */
/* V2.77: 通话记录 (落盘 /data/phone/calls.txt) */

void calls_load_from_disk(void)
{
    extern int ide_ext2_read_file(const char *, const char *, char *, int);
    int n = ide_ext2_read_file("/data/phone", "calls.txt", calls_buf, 511);
    if (n <= 0) { calls_count = 0; return; }
    calls_buf[n] = 0;
    int cnt = 0;
    char *line = calls_buf;
    while (line && cnt < CALLS_MAX) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = 0;
        char *sep = strchr(line, '|');
        if (sep) {
            *sep = 0;
            /* V2.81: 行首 '!' = 未接 */
            calls_flags[cnt] = 0;
            char *num0 = line;
            if (num0[0] == '!') { calls_flags[cnt] = 1; num0++; }
            int j = 0;
            while (num0[j] && j < 15) { calls_nums[cnt][j] = num0[j]; j++; }
            calls_nums[cnt][j] = 0;
            j = 0;
            while (sep[1 + j] && j < 7) { calls_times[cnt][j] = sep[1 + j]; j++; }
            calls_times[cnt][j] = 0;
            cnt++;
        }
        if (!nl) break;
        line = nl + 1;
    }
    calls_count = cnt;
}

/* V2.81: 追加通话记录; missed=1 记未接 (行首 '!' 标记) */
void calls_append(const char *num, int missed)
{
    extern int ide_ext2_read_file(const char *, const char *, char *, int);
    extern int ide_ext2_writefile(const char *, const char *);
    extern int ide_ext2_sync(void);
    extern uint64_t hal_clock_uptime_ms(void);
    extern int snprintf(char *, int, const char *, ...);
    char buf[512];
    int n = ide_ext2_read_file("/data/phone", "calls.txt", buf, 511);
    if (n < 0) n = 0;
    buf[n] = 0;
    int p = strlen(buf);
    if (p < 0 || p > 500) p = 0;
    if (p > 400) p = 0;
    uint32_t t = (uint32_t)hal_clock_uptime_ms() / 1000;   /* 32 位秒 (49 天溢出, 可接受) */
    char line[32];
    snprintf(line, 31, "%s%s|%02d:%02d\n", missed ? "!" : "",
             num, (int)(t / 60), (int)(t % 60));
    int li = 0;
    while (p < 500 && line[li]) buf[p++] = line[li++];
    buf[p] = 0;
    ide_ext2_writefile("data/phone/calls.txt", buf);
    ide_ext2_sync();
}

/* V2.81: 把最后一条记录标记为已接 (去掉行首 '!')——挂断时调用 */
void calls_mark_answered_last(void)
{
    extern int ide_ext2_read_file(const char *, const char *, char *, int);
    extern int ide_ext2_writefile(const char *, const char *);
    extern int ide_ext2_sync(void);
    char buf[512];
    int n = ide_ext2_read_file("/data/phone", "calls.txt", buf, 511);
    if (n <= 0) return;
    buf[n] = 0;
    /* 找最后一行起始 (上一行\n之后) */
    int last = (buf[0]) ? 0 : -1;
    for (int i = 0; buf[i]; i++)
        if (buf[i] == '\n' && buf[i + 1]) last = i + 1;
    if (last >= 0 && buf[last] == '!') buf[last] = ' ';
    ide_ext2_writefile("data/phone/calls.txt", buf);
    ide_ext2_sync();
}

void calls_delete(int idx)
{
    if (idx < 0 || idx >= calls_count) return;
    extern int ide_ext2_read_file(const char *, const char *, char *, int);
    extern int ide_ext2_writefile(const char *, const char *);
    extern int ide_ext2_sync(void);
    char buf[512], out[512];
    int n = ide_ext2_read_file("/data/phone", "calls.txt", buf, 511);
    if (n < 0) n = 0;
    buf[n] = 0;
    int op = 0, i = 0;
    char *line = buf;
    while (line && op < 500) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = 0;
        if (i != idx) {
            int li = 0;
            while (line[li] && op < 499) out[op++] = line[li++];
            out[op++] = '\n';
        }
        i++;
        if (!nl) break;
        line = nl + 1;
    }
    if (op > 0 && out[op - 1] == '\n') op--;
    out[op] = 0;
    ide_ext2_writefile("data/phone/calls.txt", out);
    ide_ext2_sync();
}

/* V2.79: 清空全部通话记录 */
void calls_clear_all(void)
{
    extern int ide_ext2_writefile(const char *, const char *);
    extern int ide_ext2_sync(void);
    ide_ext2_writefile("data/phone/calls.txt", "");
}
