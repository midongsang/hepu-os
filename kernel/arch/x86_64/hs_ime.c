/* ============================================================================
 * 赫普OS 桌面输入法适配层 (M1/M2)
 *
 * 分工：
 * - kernel/shared/ime/ime.c：拼音前缀、候选、翻页、词频（纯逻辑）
 * - 本文件：EN/ZH 状态、按键路由、提交到当前文本控件、候选条绘制、持久化
 * - hs_apps.c：终端/编辑器接收 UTF-8 的文本目标与光标屏幕坐标
 *
 * 候选条在 M2 跟随文本光标：优先光标上方，顶部空间不足时翻到下方，
 * 并在屏幕左右/上下边界内钳制；鼠标命中区与绘制使用同一套几何。
 * ========================================================================== */
#include "hpdesk.h"
#include "hs_apps.h"
#include "hs_ime.h"
#include "ime.h"

extern void gfx64_fill_rounded_rect(int x, int y, int w, int h, int radius, uint32_t color);
extern void gfx64_draw_rounded_rect(int x, int y, int w, int h, int radius, uint32_t color);
extern uint8_t *gfx64_get_framebuffer(void);
extern void font64_draw_string(void *fb, int fw, int fh,
                               int x, int y, const char *str,
                               uint32_t color, uint16_t size);
extern int gfx64_measure_text(const char *str, int size);
extern int hs_ui_sx(int v);
extern int hs_ui_sy(int v);
extern void hpdesk_mark_dirty(void);

static int g_ime_mode = HS_IME_MODE_EN;

/* 上一帧候选条几何。鼠标按下时直接复用绘制结果，避免两套命中公式漂移。 */
#define IME_HIT_MAX 9
static int g_ime_bar_x, g_ime_bar_y, g_ime_bar_w, g_ime_bar_h;
static int g_ime_hit_n;
static int g_ime_hit_x[IME_HIT_MAX], g_ime_hit_y[IME_HIT_MAX];
static int g_ime_hit_w[IME_HIT_MAX], g_ime_hit_h[IME_HIT_MAX];
static int g_ime_hit_idx[IME_HIT_MAX];
static int g_ime_mouse_consumed;

static void ime_num(char *out, int v)
{
    char tmp[12];
    int n = 0;
    if (v <= 0) { out[0] = '0'; out[1] = 0; return; }
    while (v > 0 && n < 11) { tmp[n++] = (char)('0' + (v % 10)); v /= 10; }
    int i = 0;
    while (n > 0) out[i++] = tmp[--n];
    out[i] = 0;
}

void hs_ime_init(void)
{
    extern const char *reg64_get(const char *key, const char *def);
    const char *v = reg64_get("sys.ime_mode", "en");
    g_ime_mode = (v && v[0] == 'z') ? HS_IME_MODE_ZH : HS_IME_MODE_EN;
    const char *c = reg64_get("sys.ime_candidates", "9");
    int n = (c && c[0] >= '5' && c[0] <= '9') ? (c[0] - '0') : 9;
    ime_page_size_set(n);
    const char *f = reg64_get("sys.ime_freq", "on");
    ime_freq_set_enabled(f && f[0] == 'o' && f[1] == 'n');
    ime_reset();
}

int hs_ime_page_size(void) { return ime_page_size(); }

int hs_ime_set_page_size(int n)
{
    extern int reg64_set(const char *key, const char *value);
    extern int reg64_save(void);
    n = ime_page_size_set(n);
    char buf[8];
    buf[0] = (char)('0' + n); buf[1] = 0;
    reg64_set("sys.ime_candidates", buf);
    reg64_save();
    hpdesk_mark_dirty();
    return n;
}

int hs_ime_freq_enabled(void) { return ime_freq_enabled(); }

int hs_ime_set_freq_enabled(int on)
{
    extern int reg64_set(const char *key, const char *value);
    extern int reg64_save(void);
    ime_freq_set_enabled(on ? 1 : 0);
    reg64_set("sys.ime_freq", on ? "on" : "off");
    reg64_save();
    hpdesk_mark_dirty();
    return ime_freq_enabled();
}

int hs_ime_mode(void) { return g_ime_mode; }
int hs_ime_composing(void) { return ime_pinyin_len() > 0; }

void hs_ime_cancel(void)
{
    if (ime_pinyin_len() > 0) {
        ime_reset();
        hpdesk_mark_dirty();
    }
}

void hs_ime_set_mode(int mode)
{
    int next = (mode == HS_IME_MODE_ZH) ? HS_IME_MODE_ZH : HS_IME_MODE_EN;
    if (next == g_ime_mode) return;
    ime_reset();
    g_ime_mode = next;
    extern int reg64_set(const char *key, const char *value);
    extern int reg64_save(void);
    reg64_set("sys.ime_mode", next == HS_IME_MODE_ZH ? "zh" : "en");
    reg64_save();
    hs_notification_send("Input", next == HS_IME_MODE_ZH ? "Chinese input (ZH)" : "English (EN)",
                         0, 2000);
    hpdesk_mark_dirty();
}

void hs_ime_toggle(void)
{
    hs_ime_set_mode(g_ime_mode == HS_IME_MODE_ZH ? HS_IME_MODE_EN : HS_IME_MODE_ZH);
}

static void ime_commit_text(const char *s)
{
    if (!s || !*s) return;
    hs_apps_text_insert_utf8(s);
    hpdesk_mark_dirty();
}

static int ime_ui_commit_first(void)
{
    char buf[IME_CHAR_MAX];
    int n = ime_commit_first(buf, sizeof(buf));
    if (n <= 0) return 0;
    ime_commit_text(buf);
    ime_bump(buf);
    ime_reset();
    return 1;
}

static int ime_select(int idx)
{
    char buf[IME_CHAR_MAX];
    if (ime_candidate_get(idx, buf, sizeof(buf)) != 0) return 0;
    ime_commit_text(buf);
    ime_bump(buf);
    ime_reset();
    return 1;
}

int hs_ime_click(int x, int y)
{
    if (ime_pinyin_len() <= 0 || g_ime_hit_n <= 0) return 0;
    if (x < g_ime_bar_x || x >= g_ime_bar_x + g_ime_bar_w ||
        y < g_ime_bar_y || y >= g_ime_bar_y + g_ime_bar_h)
        return 0;
    for (int i = 0; i < g_ime_hit_n; i++) {
        if (x >= g_ime_hit_x[i] && x < g_ime_hit_x[i] + g_ime_hit_w[i] &&
            y >= g_ime_hit_y[i] && y < g_ime_hit_y[i] + g_ime_hit_h[i]) {
            ime_select(g_ime_hit_idx[i]);
            hpdesk_mark_dirty();
            break;
        }
    }
    /* 条内非候选区域也吞掉，避免点页码/空白时穿透到下层窗口。 */
    g_ime_mouse_consumed = 1;
    return 1;
}

int hs_ime_mouse_release(void)
{
    int c = g_ime_mouse_consumed;
    g_ime_mouse_consumed = 0;
    return c;
}

int hs_ime_handle_key(int key, int mod)
{
    /* Esc 只在组合中拦截；无组合时继续走原来的 Esc→shell。 */
    if (key == 27 && ime_pinyin_len() > 0) {
        ime_reset();
        hpdesk_mark_dirty();
        return 1;
    }

    /* Ctrl+Space 是跨平台最常见的中英文切换键；在快捷键之前消费。 */
    if (key == 32 && (mod & 0x01)) {
        hs_ime_toggle();
        return 1;
    }

    if (g_ime_mode != HS_IME_MODE_ZH) return 0;
    if (!hs_apps_text_ime_allowed()) return 0;

    if (ime_pinyin_len() > 0) {
        if (key == 8 || key == 0x0E) {
            ime_input(8);
            hpdesk_mark_dirty();
            return 1;
        }
        if (key >= 'a' && key <= 'z') {
            ime_input(key);
            hpdesk_mark_dirty();
            return 1;
        }
        if (key >= '1' && key <= '9') {
            if (ime_select(key - '1')) return 1;
            return 1;
        }
        if (key == '0') {
            ime_page_next();
            hpdesk_mark_dirty();
            return 1;
        }
        /* M3: 组合中左右键翻页；提交后左右键才用于编辑器光标。 */
        if (key == 0x92 || key == 0x93) {
            if (key == 0x92) ime_page_prev();
            else ime_page_next();
            hpdesk_mark_dirty();
            return 1;
        }
        if (key == 32 || key == 13 || key == '\n') {
            if (!ime_ui_commit_first()) {
                /* 没有候选时不要把空格/回车漏进正文，也不静默丢拼音。 */
                hpdesk_mark_dirty();
            }
            return 1;
        }

        /* 组合中遇到标点/符号：取消组合并让应用处理该键，避免候选条卡住。 */
        ime_reset();
        hpdesk_mark_dirty();
        return 0;
    }

    if (key >= 'a' && key <= 'z') {
        ime_reset();
        ime_input(key);
        hpdesk_mark_dirty();
        return 1;
    }
    return 0;
}

void hs_ime_render(void)
{
    g_ime_hit_n = 0;
    if (ime_pinyin_len() <= 0) return;
    if (!hs_apps_text_focus()) {
        ime_reset();
        return;
    }

    int sw = g_shell.screen_width;
    int sh = g_shell.screen_height;
    int fs = hs_ui_sx(12);
    if (fs < 10) fs = 10;
    int w = hs_ui_sx(560);
    int h = hs_ui_sy(62);

    int caret_x = 0, caret_y = 0, caret_h = 0;
    int x, y;
    int has_caret = hs_apps_text_caret(&caret_x, &caret_y, &caret_h);
    int margin = hs_ui_sx(8);
    hs_window_t *foc = hs_window_get_focused();
    int win_top = foc ? (foc->y + hs_ui_sy(26)) : margin;
    if (has_caret) {
        x = caret_x - w / 2;
        y = caret_y - h - hs_ui_sy(8);
        /* 靠近窗口首行时光标上方只剩标题栏；宁可翻到光标下方，也不要糊住标题。 */
        if (y < win_top)
            y = caret_y + caret_h + hs_ui_sy(8);
    } else {
        x = (sw - w) / 2;
        y = sh - hs_ui_sy(112) - h;
    }
    if (x < margin) x = margin;
    if (x + w > sw - margin) x = sw - w - margin;
    if (y < margin) y = margin;
    if (y + h > sh - margin) y = sh - h - margin;
    g_ime_bar_x = x; g_ime_bar_y = y; g_ime_bar_w = w; g_ime_bar_h = h;

    uint32_t bg = 0x12161D;
    uint32_t border = g_shell.colors.accent;
    uint32_t fg = 0xF2F5FA;
    uint32_t dim = 0x9AA7B8;
    uint8_t *fb = gfx64_get_framebuffer();

    gfx64_fill_rounded_rect(x + hs_ui_sx(2), y + hs_ui_sy(3), w, h, hs_ui_sx(8), 0x080A0E);
    gfx64_fill_rounded_rect(x, y, w, h, hs_ui_sx(8), bg);
    gfx64_draw_rounded_rect(x, y, w, h, hs_ui_sx(6), border);

    /* 模式徽标：中 / EN */
    const char *mode = (g_ime_mode == HS_IME_MODE_ZH) ? "中" : "EN";
    int bw = hs_ui_sx(26);
    int bh = hs_ui_sy(20);
    int bx = x + hs_ui_sx(10);
    int by = y + hs_ui_sy(9);
    gfx64_fill_rounded_rect(bx, by, bw, bh, hs_ui_sx(4), border);
    font64_draw_string(fb, sw, sh, bx + hs_ui_sx(5), by + hs_ui_sy(3),
                       mode, 0xFFFFFF, fs);

    /* M3: 候选条显示分段拼音（ni hao / zhong guo），而不是一串字母。 */
    char pybuf[24];
    ime_pinyin_display(pybuf, sizeof(pybuf));
    font64_draw_string(fb, sw, sh, bx + bw + hs_ui_sx(10), by + hs_ui_sy(3),
                       pybuf, fg, fs);

    /* 候选：数字 1-9，当前页最多 9 个 */
    int cy = y + hs_ui_sy(34);
    int cx = x + hs_ui_sx(12);
    int total = ime_candidate_count();
    int page = ime_page_index();
    int pages = ime_page_count();
    int psz = ime_page_size();
    for (int i = 0; i < psz; i++) {
        char cand[IME_CHAR_MAX];
        if (ime_candidate_get(i, cand, sizeof(cand)) != 0) break;
        char item[24];
        item[0] = (char)('1' + i);
        item[1] = '.';
        int k = 2;
        int ci = 0;
        while (cand[ci] && k < 22) item[k++] = cand[ci++];
        item[k] = 0;
        int tw = gfx64_measure_text(item, fs);
        if (cx + tw >= x + w - hs_ui_sx(50)) break;
        font64_draw_string(fb, sw, sh, cx, cy, item, fg, fs);
        if (g_ime_hit_n < IME_HIT_MAX) {
            int hi = g_ime_hit_n++;
            g_ime_hit_x[hi] = cx - hs_ui_sx(2);
            g_ime_hit_y[hi] = cy - hs_ui_sy(4);
            g_ime_hit_w[hi] = tw + hs_ui_sx(4);
            g_ime_hit_h[hi] = hs_ui_sy(24);
            g_ime_hit_idx[hi] = i;
        }
        cx += tw + hs_ui_sx(12);
    }

    /* 页码：1/2 */
    char pagebuf[24];
    char nbuf[12];
    ime_num(nbuf, page + 1);
    pagebuf[0] = 0;
    int p = 0;
    while (nbuf[p]) pagebuf[p] = nbuf[p], p++;
    pagebuf[p++] = '/';
    ime_num(nbuf, pages > 0 ? pages : 1);
    int q = 0;
    while (nbuf[q]) pagebuf[p++] = nbuf[q++];
    pagebuf[p] = 0;
    font64_draw_string(fb, sw, sh, x + w - hs_ui_sx(42), cy, pagebuf, dim, fs);

    (void)total;
}
