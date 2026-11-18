/* gfx64_* 兼容层的第二块: hpdesk.c 依赖的非绘制外部符号。
 *
 * hpdesk.c 除了 30 个 gfx64_* 图元, 还要这些东西:
 *   rtc64_get_h/m/s   —— 顶栏时钟
 *   pit64_get_ticks/ms —— 动画计时
 *   pmm64_get_stats    —— 内存条
 *   cursor64_*         —— 光标
 *
 * 32 位这边尽量接已有的实现, 接不上的退化成常量并在串口说明一次。
 * 退化项不会让桌面崩, 但会让对应 UI 静止 —— 以后要逐个补真实现。
 */

#include <stdint.h>

/* ---- 已有的 32 位实现 ---- */
extern uint32_t pit_get_ticks(void);
extern int rtc_get_time(int *h, int *m, int *s, int *d, int *mo, int *y);

extern void serial_puts(uint16_t port, const char *s);

static int g_ext_note = 0;
static void note_once(const char *what)
{
    if (g_ext_note) return;
    g_ext_note = 1;
    serial_puts(0x3F8, "[COMPAT32] ext degrade: ");
    serial_puts(0x3F8, what);
    serial_puts(0x3F8, "\n");
}

/* ---- RTC: 接 32 位 rtc_get_time ---- */
static void read_hms(int *h, int *m, int *s)
{
    int d, mo, y;
    *h = 0; *m = 0; *s = 0;
    rtc_get_time(h, m, s, &d, &mo, &y);
}

uint8_t rtc64_get_hours(void)   { int h, m, s; read_hms(&h, &m, &s); return (uint8_t)(h & 0xFF); }
uint8_t rtc64_get_minutes(void) { int h, m, s; read_hms(&h, &m, &s); return (uint8_t)(m & 0xFF); }
uint8_t rtc64_get_seconds(void) { int h, m, s; read_hms(&h, &m, &s); return (uint8_t)(s & 0xFF); }

/* 日期字段 —— 32 位 rtc_get_time 已经带 day/month/year, 直接转出来。
 * weekday 它不给, 用 Zeller 同余从年月日算(1=Sunday..7=Saturday, 与 CMOS 同口径)。 */
static void read_ymd(int *y, int *mo, int *d)
{
    int h, m, s;
    *y = 2000; *mo = 1; *d = 1;
    rtc_get_time(&h, &m, &s, d, mo, y);
}
uint8_t rtc64_get_day(void)   { int y, mo, d; read_ymd(&y, &mo, &d); return (uint8_t)d; }
uint8_t rtc64_get_month(void) { int y, mo, d; read_ymd(&y, &mo, &d); return (uint8_t)mo; }
uint16_t rtc64_get_year(void) { int y, mo, d; read_ymd(&y, &mo, &d); return (uint16_t)y; }
uint8_t rtc64_get_weekday(void)
{
    int y, mo, d;
    read_ymd(&y, &mo, &d);
    if (mo < 3) { mo += 12; y -= 1; }
    int k = y % 100, j = y / 100;
    int hh = (d + (13 * (mo + 1)) / 5 + k + k / 4 + j / 4 + 5 * j) % 7;  /* 0=Saturday */
    return (uint8_t)(((hh + 6) % 7) + 1);                                /* -> 1=Sunday */
}

/* Unix 时间戳：接 32 位 rtc_get_time 读 CMOS 折算（口径同 64 位 rtc64.c：
 * 从 2000-01-01 起算，加 946684800）。读失败返回 0。 */
uint64_t rtc64_get_timestamp(void)
{
    int h, m, s, d, mo, y;
    if (rtc_get_time(&h, &m, &s, &d, &mo, &y) != 0) return 0;

    uint64_t days = 0;
    for (int yy = 2000; yy < y; yy++) {
        if ((yy % 4 == 0 && yy % 100 != 0) || (yy % 400 == 0)) days += 366;
        else days += 365;
    }
    int mdays[12] = {31, 28, 31, 30, 31, 30, 31, 30, 31, 30, 31, 30};
    if ((y % 4 == 0 && y % 100 != 0) || (y % 400 == 0)) mdays[1] = 29;
    for (int mm = 0; mm < mo - 1; mm++) days += mdays[mm];
    days += d - 1;

    return days * 86400ULL + (uint64_t)h * 3600 + (uint64_t)m * 60 + s + 946684800ULL;
}

/* ---- PIT: 32 位 pit_get_ticks 是 uint32_t, 64 位签名是 uint64_t ---- */
uint64_t pit64_get_ticks(void)
{
    return (uint64_t)pit_get_ticks();
}

/* 毫秒: 32 位 PIT 按 HZ 计, 这里按 100Hz 折算(与 kernel 默认一致) */
uint64_t pit64_get_ms(void)
{
    return (uint64_t)pit_get_ticks() * 10ULL;
}

/* ---- 内存统计: 32 位无对应, 给 0 免得 UI 显示垃圾 ---- */
void pmm64_get_stats(uint64_t *total, uint64_t *used, uint64_t *free)
{
    note_once("pmm64_get_stats=0");
    if (total) *total = 0;
    if (used)  *used  = 0;
    if (free)  *free  = 0;
}

/* ---- 光标: 32 位走 ps2mouse, 但 hpdesk 要的是"位图光标" ----
 * 这里先给能编译的最小实现: 不加载任何主题, ready=0 让 hpdesk 走自己的
 * 兜底绘制; draw 退化成画一个 7x7 小方块, 至少能看到指针在哪。*/
static int g_cursor_size = 16;

void cursor64_draw(int x, int y)
{
    extern void gfx64_fill_rect(int, int, int, int, uint32_t);
    gfx64_fill_rect(x, y, 6, 6, 0xFFFFFF);
    gfx64_fill_rect(x + 1, y + 1, 4, 4, 0x000000);
}

int cursor64_load_file(const char *path, int want)
{
    (void)path; (void)want;
    note_once("cursor64_load_file=stub");
    return -1;                       /* 没有主题文件 */
}

int cursor64_load_kind(int kind, const char *path, int want)
{
    (void)kind; (void)path; (void)want;
    return -1;
}

int cursor64_ready(void)    { return 0; }   /* 无主题 → hpdesk 用兜底 */
int cursor64_nominal(void)  { return g_cursor_size; }
int cursor64_hotspot_x(void) { return 0; }
int cursor64_hotspot_y(void) { return 0; }
int cursor64_pick_size(int w, int h) { (void)w; (void)h; return g_cursor_size; }
void cursor64_set_theme(int t) { (void)t; }

/* hs_window_drag_* 是 hpdesk.c 自己的函数, 不要在这里重复定义 */

/* ================= 字体宽度 ================= */

/* 32 位对应 font64.c 的 font64_get_string_width。
 * 32 位 font64_draw_string 走的是固定 8x16 点阵(见 gfx64_compat.c),
 * 每个 ASCII 字符前进 8px, 非 ASCII(UTF-8 多字节, 主要是 CJK)按 16px 算 ——
 * 与 draw 里的推进量保持一致, 否则居中/右对齐会偏。 */
int font64_get_string_width(const char *str, uint16_t size)
{
    /* 宽度必须和 font32.c 的绘制推进量一致, 否则居中/右对齐会偏。
     * 原来是固定 8px/字符(与写死的 16 号点阵配套), 现在按 size 缩放。 */
    extern int f32_string_width(const char *str, uint16_t size);
    return f32_string_width(str, size);
}

/* 32 位对应 gfx64.c 的 gfx64_get_info(64 位从自己的全局读, 这里从 32 位全局读)。
 * 放这一层是为了让 desk_surface.c 保持无架构分支 —— 它只需要一个"屏幕多大"的答案。 */
void gfx64_get_info(int *width, int *height, int *bpp)
{
    extern int gfx_width, gfx_height, gfx_bpp;
    if (width)  *width  = gfx_width;
    if (height) *height = gfx_height;
    if (bpp)    *bpp    = gfx_bpp;
}
