/* ============================================================================
 * 赫普OS 桌面输入法适配层 (M1)
 *
 * 分工：
 * - kernel/shared/ime/ime.c：拼音前缀、候选、翻页、词频（纯逻辑）
 * - 本文件：EN/ZH 状态、按键路由、提交到当前文本控件、候选条绘制、持久化
 * - hs_apps.c：终端/编辑器接收 UTF-8 的文本目标
 *
 * 第一版不跟随光标，候选条固定在 Dock 上方；M3 再通过 text_caret 接口升级。
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
    ime_reset();
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
    extern const char *reg64_set(const char *key, const char *value);
    extern int reg64_save(void);
    reg64_set("sys.ime_mode", next == HS_IME_MODE_ZH ? "zh" : "en");
    reg64_save();
    hs_notification_send("Input", next == HS_IME_MODE_ZH ? "中文输入 (ZH)" : "English (EN)",
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
    int x = (sw - w) / 2;
    int y = sh - hs_ui_sy(112) - h;
    if (y < 0) y = 0;

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

    /* 拼音：最多 8 个字母 */
    const char *py = ime_pinyin();
    char pybuf[24];
    int pn = 0;
    while (py[pn] && pn < 20) { pybuf[pn] = py[pn]; pn++; }
    pybuf[pn] = 0;
    font64_draw_string(fb, sw, sh, bx + bw + hs_ui_sx(10), by + hs_ui_sy(3),
                       pybuf, fg, fs);

    /* 候选：数字 1-9，当前页最多 9 个 */
    int cy = y + hs_ui_sy(34);
    int cx = x + hs_ui_sx(12);
    int total = ime_candidate_count();
    int page = ime_page_index();
    int pages = ime_page_count();
    for (int i = 0; i < IME_PAGE_SIZE && i < 9; i++) {
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
