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
