/* ============================================================================
 * 赫普OS - 图形化首启向导 OOBE (V0.55 P3-GUI)
 * 桌面层模态向导: 语言 → 时区 → 账户类型(普通/开发者) → 创建账户 → 欢迎
 * 图形化可用时由 desktop64_run 激活; 图形化不可用时仍走 oobe64.c 文本兜底
 * 写入与文本版相同的 /account.conf + /system.conf + /lang.conf
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>

/* ===== 图形接口 ===== */
extern void gfx64_fill_rect(int x, int y, int w, int h, uint32_t color);
extern void gfx64_fill_rect_alpha(int x, int y, int w, int h, uint32_t color, int alpha);
extern void gfx64_fill_rounded_rect(int x, int y, int w, int h, int radius, uint32_t color);
extern void gfx64_draw_rounded_rect(int x, int y, int w, int h, int radius, uint32_t color);
extern void gfx64_draw_rect(int x, int y, int w, int h, uint32_t color);
extern void *gfx64_get_framebuffer(void);
extern void font64_draw_string(void *fb, int fw, int fh, int x, int y,
                               const char *str, uint32_t color, uint16_t size);

/* ===== 账户/配置接口 ===== */
extern int  auth64_is_configured(void);
extern int  auth64_create_account(const char *name, const char *pass, int role,
                                  const char *admin_pass);
extern void oobe64_save_system_conf(int lang, int tz_min);
extern void vga_puts(const char *s);

/* 配色 */
#define C_BACKDROP 0x000000
#define C_CARD     0xF7F5F0
#define C_HEADER   0x2C5F8D
#define C_TEXT     0x2A2520
#define C_SUBTLE   0x8A857C
#define C_FIELD    0xFFFFFF
#define C_FIELD_BD 0xD6D2CA
#define C_BTN      0xE6E2DA
#define C_ACCENT   0x2C5F8D
#define C_ERROR    0xC0392B
#define C_WHITE    0xFFFFFF

/* 时区: 0..5 (与文本向导一致) */
static const char *TZ_ZH[6] = {
    "北京 UTC+8", "东京 UTC+9", "柏林 UTC+1",
    "伦敦 UTC+0", "纽约 UTC-5", "洛杉矶 UTC-7"
};
static const char *TZ_EN[6] = {
    "Beijing UTC+8", "Tokyo UTC+9", "Berlin UTC+1",
    "London UTC+0", "New York UTC-5", "Los Angeles UTC-7"
};
static const int TZ_MIN[6] = { 480, 540, 60, 0, -300, -420 };

/* 向导状态 */
static int  g_active;
static int  g_step;      /* 0=语言 1=时区 2=账户类型 3=创建账户 4=欢迎 */
static int  g_lang;      /* 1=中文 2=English */
static int  g_tz;        /* 0..5 */
static int  g_role;      /* 1=普通 2=开发者 */
static int  g_focus;     /* 账户步骤当前输入框 */
static char g_user[32];
static char g_pass[64];
static char g_pass2[64];
static char g_admin[64];
static char g_error[96];

/* 立即模式命中区: 渲染时登记, 点击时反查 */
#define OOBE_MAX_HITS 32
static int g_hit_n;
static int g_hit_rect[OOBE_MAX_HITS][4];
static int g_hit_act[OOBE_MAX_HITS];
enum {
    ACT_NONE = 0,
    ACT_LANG_ZH = 1, ACT_LANG_EN = 2,
    ACT_TZ_0 = 10,
    ACT_TYPE_NORMAL = 20, ACT_TYPE_DEV = 21,
    ACT_FIELD_0 = 30,
    ACT_BACK = 40, ACT_NEXT = 41, ACT_SUBMIT = 42, ACT_FINISH = 43
};

#define ZE(zh, en) (g_lang == 1 ? (zh) : (en))

/* ===== 小工具 ===== */
static int oobe_slen(const char *s)
{
    int n = 0;
    while (s[n]) n++;
    return n;
}

static int oobe_streq(const char *a, const char *b)
{
    while (*a && *b) {
        if (*a != *b) return 0;
        a++; b++;
    }
    return *a == *b;
}

static int oobe_is_enter(int k)
{
    return k == '\n' || k == '\r' || k == 13 || k == 0x1C;
}

static int oobe_is_backspace(int k)
{
    return k == '\b' || k == 0x0E;
}

static int oobe_is_tab(int k)
{
    return k == '\t' || k == 0x0F;
}

/* 估算文本宽度(UTF-8 字符数): ASCII≈0.6em, CJK≈1em */
static int oobe_text_width(const char *s, int size)
{
    int w = 0;
    while (*s) {
        unsigned char c = (unsigned char)*s;
        if (c < 0x80) {
            w += size * 6 / 10;
            s++;
        } else {
            if (c >= 0xF0) s += 4;
            else if (c >= 0xE0) s += 3;
            else if (c >= 0xC0) s += 2;
            else s++;
            w += size;
        }
    }
    return w;
}

static void oobe_text_center(void *fb, int sw, int sh, int cx, int y,
                             const char *s, uint32_t color, int size)
{
    int w = oobe_text_width(s, size);
    font64_draw_string(fb, sw, sh, cx - w / 2, y, s, color, (uint16_t)size);
}

static void oobe_card(int x, int y, int w, int h, int r, uint32_t fill, uint32_t border)
{
    gfx64_fill_rounded_rect(x, y, w, h, r, fill);
    gfx64_draw_rounded_rect(x, y, w, h, r, border);
}

static void oobe_hit(int x, int y, int w, int h, int act)
{
    if (g_hit_n >= OOBE_MAX_HITS) return;
    g_hit_rect[g_hit_n][0] = x;
    g_hit_rect[g_hit_n][1] = y;
    g_hit_rect[g_hit_n][2] = w;
    g_hit_rect[g_hit_n][3] = h;
    g_hit_act[g_hit_n] = act;
    g_hit_n++;
}

/* 账户步骤字段: 开发者多一个管理员密码 */
static int oobe_fields(char **bufs, int *maxs)
{
    int n = 0;
    if (g_role == 2) { bufs[n] = g_admin; maxs[n] = 63; n++; }
    bufs[n] = g_user;  maxs[n] = 31; n++;
    bufs[n] = g_pass;  maxs[n] = 63; n++;
    bufs[n] = g_pass2; maxs[n] = 63; n++;
    return n;
}

/* ===== 状态迁移 ===== */
static void oobe_select_lang(int lang)
{
    g_lang = lang;
    g_error[0] = '\0';
    g_step = 1;
}

static void oobe_select_tz(int idx)
{
    g_tz = idx;
    g_error[0] = '\0';
    g_step = 2;
}

static void oobe_select_type(int role)
{
    g_role = role;
    g_focus = 0;
    g_error[0] = '\0';
    g_step = 3;
}

static void oobe_set_error(const char *msg, int focus)
{
    int i = 0;
    while (msg[i] && i < (int)sizeof(g_error) - 1) { g_error[i] = msg[i]; i++; }
    g_error[i] = '\0';
    if (focus >= 0) g_focus = focus;
}

static void oobe_submit(void)
{
    char *b[4]; int m[4];
    oobe_fields(b, m);
    int idx_user = (g_role == 2) ? 1 : 0;
    int idx_pass = idx_user + 1;
    int idx_pass2 = idx_pass + 1;

    if (!g_user[0]) {
        oobe_set_error(ZE("请输入用户名", "Username cannot be empty"), idx_user);
        return;
    }
    if (oobe_slen(g_pass) < 4) {
        oobe_set_error(ZE("密码至少 4 位", "Password needs at least 4 characters"), idx_pass);
        return;
    }
    if (!oobe_streq(g_pass, g_pass2)) {
        oobe_set_error(ZE("两次密码不一致", "Passwords do not match"), idx_pass2);
        return;
    }
    if (g_role == 2 && !g_admin[0]) {
        oobe_set_error(ZE("请输入管理员密码", "Admin password required"), 0);
        return;
    }
    if (auth64_create_account(g_user, g_pass, g_role,
                              g_role == 2 ? g_admin : "") != 0) {
        oobe_set_error(ZE("创建账户失败 (磁盘错误)", "Failed to create account (disk error)"), -1);
        return;
    }
    oobe64_save_system_conf(g_lang, TZ_MIN[g_tz]);
    g_error[0] = '\0';
    g_step = 4;
    vga_puts("[OOBE-GUI] account created\n");
}

static void oobe_finish(void)
{
    g_active = 0;
    g_error[0] = '\0';
    vga_puts("[OOBE-GUI] setup finished, entering desktop\n");
}

/* ===== 对外接口 ===== */
int hs_oobe_active(void)
{
    return g_active;
}

void hs_oobe_begin(void)
{
    if (auth64_is_configured()) { g_active = 0; return; }
    g_active = 1;
    g_step = 0;
    g_lang = 1;       /* 默认中文 */
    g_tz = 0;         /* 默认北京 */
    g_role = 1;       /* 默认普通用户 */
    g_focus = 0;
    g_user[0] = '\0';
    g_pass[0] = '\0';
    g_pass2[0] = '\0';
    g_admin[0] = '\0';
    g_error[0] = '\0';
    vga_puts("[OOBE-GUI] first boot: showing setup wizard on desktop\n");
}

/* ===== 渲染 ===== */
void hs_oobe_render(void *fb, int sw, int sh)
{
    if (!g_active) return;
    g_hit_n = 0;

    /* 桌面变暗，保持可见 */
    gfx64_fill_rect_alpha(0, 0, sw, sh, C_BACKDROP, 170);

#define SX(v) ((v) * sw / 1024)
#define SY(v) ((v) * sh / 768)
#define SF(v) (((v) * sh / 768) < 8 ? 8 : ((v) * sh / 768))

    int pw = SX(720), ph = SY(540);
    int px = (sw - pw) / 2, py = (sh - ph) / 2;
    int r  = SF(14);

    /* 卡片 + 顶部标题区 */
    oobe_card(px, py, pw, ph, r, C_CARD, C_ACCENT);
    int hx = px + SX(14), hy = py + SY(14), hw = pw - SX(28), hh = SY(62);
    gfx64_fill_rounded_rect(hx, hy, hw, hh, SF(10), C_HEADER);
    font64_draw_string(fb, sw, sh, hx + SX(20), hy + SY(18),
                       ZE("HepuOS 首次设置", "HepuOS First-time Setup"),
                       C_WHITE, (uint16_t)SF(22));

    /* 步骤指示 */
    {
        char sb[8];
        int p = 0;
        int shown = (g_step >= 3) ? 4 : g_step + 1;
        sb[p++] = (char)('0' + shown);
        sb[p++] = ' ';
        sb[p++] = '/';
        sb[p++] = ' ';
        sb[p++] = '4';
        sb[p] = '\0';
        font64_draw_string(fb, sw, sh, hx + hw - SX(64), hy + SY(22),
                           sb, C_WHITE, (uint16_t)SF(14));
        for (int i = 0; i < 4; i++) {
            uint32_t c = (i <= (shown - 1)) ? C_WHITE : 0x6F97B5;
            gfx64_fill_rounded_rect(hx + hw - SX(150) + i * SX(22),
                                    hy + SY(44), SX(10), SY(5), SF(2), c);
        }
    }

    int bx = px + SX(40);
    int by = py + SY(96);
    int bw = pw - SX(80);

    /* ---- 步骤 0: 语言 ---- */
    if (g_step == 0) {
        font64_draw_string(fb, sw, sh, bx, by,
                           ZE("选择系统语言 / Select language",
                              "Select system language"),
                           C_TEXT, (uint16_t)SF(19));
        int cw = (bw - SX(24)) / 2, ch = SY(112);
        int cy = by + SY(44);
        const char *names[2] = { "中文", "English" };
        const char *subs[2]  = { "简体中文", "English (US)" };
        for (int i = 0; i < 2; i++) {
            int cx = bx + i * (cw + SX(24));
            uint32_t bd = (i == 0) ? C_ACCENT : C_FIELD_BD;
            oobe_card(cx, cy, cw, ch, SF(10), C_FIELD, bd);
            oobe_text_center(fb, sw, sh, cx + cw / 2, cy + SY(30),
                             names[i], C_TEXT, SF(26));
            oobe_text_center(fb, sw, sh, cx + cw / 2, cy + SY(72),
                             subs[i], C_SUBTLE, SF(13));
            oobe_hit(cx, cy, cw, ch, i == 0 ? ACT_LANG_ZH : ACT_LANG_EN);
        }
    }

    /* ---- 步骤 1: 时区 ---- */
    else if (g_step == 1) {
        font64_draw_string(fb, sw, sh, bx, by,
                           ZE("选择时区", "Select timezone"),
                           C_TEXT, (uint16_t)SF(19));
        int gap = SX(16);
        int cw = (bw - gap * 2) / 3, ch = SY(84);
        int cy0 = by + SY(44);
        for (int i = 0; i < 6; i++) {
            int col = i % 3, row = i / 3;
            int cx = bx + col * (cw + gap);
            int cy = cy0 + row * (ch + SY(16));
            const char *nm = g_lang == 1 ? TZ_ZH[i] : TZ_EN[i];
            int selected = (i == g_tz);
            oobe_card(cx, cy, cw, ch, SF(10), C_FIELD,
                      selected ? C_ACCENT : C_FIELD_BD);
            oobe_text_center(fb, sw, sh, cx + cw / 2, cy + SY(28),
                             nm, C_TEXT, SF(15));
            oobe_hit(cx, cy, cw, ch, ACT_TZ_0 + i);
        }
    }

    /* ---- 步骤 2: 账户类型 ---- */
    else if (g_step == 2) {
        font64_draw_string(fb, sw, sh, bx, by,
                           ZE("选择账户类型", "Choose account type"),
                           C_TEXT, (uint16_t)SF(19));
        int cw = (bw - SX(24)) / 2, ch = SY(170);
        int cy = by + SY(44);
        const char *t0 = ZE("普通用户", "Normal user");
        const char *d0 = ZE("日常使用，无需管理员密码", "Everyday use, no admin password");
        const char *t1 = ZE("开发者", "Developer");
        const char *d1 = ZE("需要管理员密码，获得完整权限", "Requires admin password, full privileges");
        for (int i = 0; i < 2; i++) {
            int cx = bx + i * (cw + SX(24));
            uint32_t bd = ((i == 0 && g_role == 1) || (i == 1 && g_role == 2))
                          ? C_ACCENT : C_FIELD_BD;
            oobe_card(cx, cy, cw, ch, SF(10), C_FIELD, bd);
            oobe_text_center(fb, sw, sh, cx + cw / 2, cy + SY(30),
                             i == 0 ? t0 : t1, C_TEXT, SF(22));
            oobe_text_center(fb, sw, sh, cx + cw / 2, cy + SY(86),
                             i == 0 ? d0 : d1, C_SUBTLE, SF(12));
            oobe_hit(cx, cy, cw, ch, i == 0 ? ACT_TYPE_NORMAL : ACT_TYPE_DEV);
        }
    }

    /* ---- 步骤 3: 创建账户 ---- */
    else if (g_step == 3) {
        font64_draw_string(fb, sw, sh, bx, by,
                           ZE("创建账户", "Create account"),
                           C_TEXT, (uint16_t)SF(19));
        char *bufs[4]; int maxs[4];
        int n = oobe_fields(bufs, maxs);
        if (g_focus >= n) g_focus = n - 1;
        if (g_focus < 0) g_focus = 0;
        const char *labels[4] = {
            ZE("管理员密码", "Admin password"),
            ZE("用户名", "Username"),
            ZE("密码 (至少4位)", "Password (min 4)"),
            ZE("确认密码", "Confirm password")
        };
        /* 普通用户无管理员密码字段, 标签整体前移一位 */
        int loff = (g_role == 2) ? 0 : 1;
        int fy0 = by + SY(38);
        for (int i = 0; i < n; i++) {
            int ry = fy0 + i * SY(74);
            font64_draw_string(fb, sw, sh, bx, ry, labels[i + loff],
                               C_SUBTLE, (uint16_t)SF(12));
            int fx = bx, fy = ry + SY(18), fw = bw, fh = SY(40);
            oobe_card(fx, fy, fw, fh, SF(8), C_FIELD,
                      (i == g_focus) ? C_ACCENT : C_FIELD_BD);
            /* 密码类字段掩码显示; 用户名明文 */
            int is_pw = (i != ((g_role == 2) ? 1 : 0));
            char shown[66];
            int len = oobe_slen(bufs[i]);
            int p = 0;
            if (is_pw) {
                for (int k = 0; k < len && p < 64; k++) shown[p++] = '*';
            } else {
                for (int k = 0; k < len && p < 64; k++) shown[p++] = bufs[i][k];
            }
            shown[p] = '\0';
            font64_draw_string(fb, sw, sh, fx + SX(12), fy + SY(11), shown,
                               C_TEXT, (uint16_t)SF(14));
            oobe_hit(fx, fy, fw, fh, ACT_FIELD_0 + i);
        }
        if (g_error[0]) {
            font64_draw_string(fb, sw, sh, bx,
                               fy0 + n * SY(74) + SY(4), g_error,
                               C_ERROR, (uint16_t)SF(13));
        }
    }

    /* ---- 步骤 4: 欢迎 ---- */
    else if (g_step == 4) {
        oobe_text_center(fb, sw, sh, px + pw / 2, py + SY(180),
                         ZE("欢迎使用 HepuOS!", "Welcome to HepuOS!"),
                         C_ACCENT, SF(34));
        oobe_text_center(fb, sw, sh, px + pw / 2, py + SY(240),
                         ZE("首次设置已完成，账户已创建。",
                            "Setup complete. Your account is ready."),
                         C_SUBTLE, SF(15));
        int btn_w = SX(200), btn_h = SY(48);
        int btn_x = px + (pw - btn_w) / 2, btn_y = py + SY(330);
        oobe_card(btn_x, btn_y, btn_w, btn_h, SF(10), C_ACCENT, C_ACCENT);
        oobe_text_center(fb, sw, sh, px + pw / 2, btn_y + SY(14),
                         ZE("进入桌面", "Enter desktop"), C_WHITE, SF(17));
        oobe_hit(btn_x, btn_y, btn_w, btn_h, ACT_FINISH);
        return;
    }

    /* ---- 底部按钮 (步骤 0..3) ---- */
    int fy = py + ph - SY(56);
    int bh = SY(42);
    if (g_step > 0) {
        int bwid = SX(112);
        oobe_card(bx, fy, bwid, bh, SF(8), C_BTN, C_FIELD_BD);
        oobe_text_center(fb, sw, sh, bx + bwid / 2, fy + SY(11),
                         ZE("上一步", "Back"), C_TEXT, SF(15));
        oobe_hit(bx, fy, bwid, bh, ACT_BACK);
    }
    {
        const char *lab = (g_step == 3) ? ZE("创建账户", "Create account")
                                        : ZE("下一步", "Next");
        int bwid = (g_step == 3) ? SX(168) : SX(128);
        int bx2 = px + pw - SX(40) - bwid;
        oobe_card(bx2, fy, bwid, bh, SF(8), C_ACCENT, C_ACCENT);
        oobe_text_center(fb, sw, sh, bx2 + bwid / 2, fy + SY(11),
                         lab, C_WHITE, SF(15));
        oobe_hit(bx2, fy, bwid, bh, (g_step == 3) ? ACT_SUBMIT : ACT_NEXT);
    }

#undef SX
#undef SY
#undef SF
}

/* ===== 键盘 ===== */
int hs_oobe_handle_key(int key)
{
    if (!g_active) return 0;

    if (g_step == 4) { oobe_finish(); return 1; }

    if (g_step == 0) {
        if (key == '1') oobe_select_lang(1);
        else if (key == '2') oobe_select_lang(2);
        return 1;
    }
    if (g_step == 1) {
        if (key >= '1' && key <= '6') oobe_select_tz(key - '1');
        return 1;
    }
    if (g_step == 2) {
        if (key == '1') oobe_select_type(1);
        else if (key == '2') oobe_select_type(2);
        return 1;
    }

    /* 步骤 3: 账户输入 */
    {
        char *bufs[4]; int maxs[4];
        int n = oobe_fields(bufs, maxs);
        if (g_focus >= n) g_focus = n - 1;
        if (g_focus < 0) g_focus = 0;

        if (oobe_is_tab(key)) {
            g_focus = (g_focus + 1) % n;
            return 1;
        }
        if (oobe_is_enter(key)) {
            if (g_focus < n - 1) g_focus++;
            else oobe_submit();
            return 1;
        }
        if (oobe_is_backspace(key)) {
            int len = oobe_slen(bufs[g_focus]);
            if (len > 0) bufs[g_focus][len - 1] = '\0';
            return 1;
        }
        if (key >= 32 && key < 127) {
            int len = oobe_slen(bufs[g_focus]);
            if (len < maxs[g_focus]) {
                bufs[g_focus][len] = (char)key;
                bufs[g_focus][len + 1] = '\0';
            }
            return 1;
        }
    }
    return 1;
}

/* ===== 鼠标 ===== */
int hs_oobe_handle_click(int x, int y)
{
    if (!g_active) return 0;

    for (int i = g_hit_n - 1; i >= 0; i--) {
        int rx = g_hit_rect[i][0], ry = g_hit_rect[i][1];
        int rw = g_hit_rect[i][2], rh = g_hit_rect[i][3];
        if (x < rx || x >= rx + rw || y < ry || y >= ry + rh) continue;

        int act = g_hit_act[i];
        if (act == ACT_LANG_ZH)       oobe_select_lang(1);
        else if (act == ACT_LANG_EN)  oobe_select_lang(2);
        else if (act >= ACT_TZ_0 && act < ACT_TZ_0 + 6)
                                      oobe_select_tz(act - ACT_TZ_0);
        else if (act == ACT_TYPE_NORMAL) oobe_select_type(1);
        else if (act == ACT_TYPE_DEV)    oobe_select_type(2);
        else if (act >= ACT_FIELD_0 && act < ACT_FIELD_0 + 4)
                                      g_focus = act - ACT_FIELD_0;
        else if (act == ACT_BACK) {
            if (g_step > 0) g_step--;
            g_error[0] = '\0';
        }
        else if (act == ACT_NEXT) {
            if (g_step == 0) oobe_select_lang(g_lang);
            else if (g_step == 1) oobe_select_tz(g_tz);
            else if (g_step == 2) oobe_select_type(g_role);
        }
        else if (act == ACT_SUBMIT)   oobe_submit();
        else if (act == ACT_FINISH)   oobe_finish();
        return 1;
    }
    return 1;   /* 模态: 吞掉所有点击 */
}
