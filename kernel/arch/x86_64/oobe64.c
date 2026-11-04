/* ============================================================================
 * 赫普OS - 64位首启向导 OOBE (V0.55 P3, 文本版)
 * 流程: 语言 → 时区 → 用户类型(普通/开发者) → 账户密码(开发者需管理员密码)
 *       → 欢迎屏 → 写 /system.conf + /account.conf → 继续引导
 * 图形化失败时同样可用（文本模式本身是 OOBE 的兜底通道）
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>

extern void vga_puts(const char *str);

/* ===== 键盘输入 =====
 * OOBE 在 keyboard64_init 之后运行 (IRQ1 已开, 中断驱动的环形缓冲是
 * 唯一输入来源). 不要在这里直读 0x60 —— 会和 IRQ 处理器抢端口,
 * 造成同一按键被重复/丢失投递. */
extern int  keyboard64_haschar(void);
extern int  keyboard64_getchar(void);      /* 无键返回 -1 */
extern void keyboard64_flush(void);

static int oobe_getkey(void)
{
    int c = keyboard64_getchar();
    return c < 0 ? 0 : c;
}

static int oobe_haskey(void)
{
    return keyboard64_haschar();
}

/* auth64 API */
extern int  auth64_create_account(const char *, const char *, int, const char *);
extern int  auth64_write_config(const char *path, const char *text, int len);
extern int  auth64_current_role(void);

#define SYS_PATH "/system.conf"
#define LANG_PATH "/lang.conf"

/* ===== 双语字符串表 ===== */
static const char **S;
static const char *S_zh[] = {
    "HepuOS 首次设置\n============================\n\n",
    "选择系统语言 / Select language:\n  1. 中文\n  2. English\n> ",
    "选择时区 (数字键):\n  1. 北京 UTC+8   2. 东京 UTC+9   3. 柏林 UTC+1\n"
    "  4. 伦敦 UTC+0   5. 纽约 UTC-5   6. 洛杉矶 UTC-7\n> ",
    "选择账户类型:\n  1. 普通用户 (日常使用)\n  2. 开发者 (需要管理员密码)\n> ",
    "设置管理员密码: ",
    "输入用户名: ",
    "设置密码 (至少4位): ",
    "确认密码: ",
    "两次密码不一致，重新输入\n",
    "用户名不能为空\n",
    "创建账户失败 (磁盘错误)\n",
    "\n欢迎使用 HepuOS!\n按任意键进入系统...\n",
};
static const char *S_en[] = {
    "HepuOS First-time Setup\n=======================\n\n",
    "Select language:\n  1. 中文\n  2. English\n> ",
    "Select timezone (number):\n  1. Beijing UTC+8  2. Tokyo UTC+9  3. Berlin UTC+1\n"
    "  4. London UTC+0   5. New York UTC-5  6. Los Angeles UTC-7\n> ",
    "Account type:\n  1. Normal user\n  2. Developer (requires admin password)\n> ",
    "Set admin password: ",
    "Username: ",
    "Password (min 4 chars): ",
    "Confirm password: ",
    "Passwords do not match, try again\n",
    "Username cannot be empty\n",
    "Failed to create account (disk error)\n",
    "\nWelcome to HepuOS!\nPress any key to continue...\n",
};

enum { S_HEAD, S_LANG, S_TZ, S_TYPE, S_ADMIN, S_USER, S_PASS, S_PASS2,
       S_NOMATCH, S_NOUSER, S_FAIL, S_WELCOME };

/* ===== 输入辅助 ===== */
static void oobe_read_line(char *buf, int max, int echo_mask)
{
    int n = 0;
    while (n < max - 1) {
        int c = oobe_getkey();
        if (c == '\n' || c == '\r') {
            vga_puts("\n");
            break;
        }
        if (c == 0x08 || c == 0x7F) {           /* Backspace */
            if (n > 0) {
                n--;
                vga_puts("\b \b");
            }
            continue;
        }
        if (c >= 0x20 && c < 0x7F) {
            buf[n++] = (char)c;
            vga_puts(echo_mask ? "*" : (char[]){ (char)c, '\0' });
        }
    }
    buf[n] = '\0';
}

static int oobe_choice(int min, int max)
{
    for (;;) {
        int c = oobe_getkey();
        if (c >= '0' + min && c <= '0' + max) {
            vga_puts("\n");
            return c - '0';
        }
    }
}

/* ===== 系统配置持久化 ===== */
static void save_system_conf(int lang, int tz_min)
{
    char buf[128];
    const char *lang_s = (lang == 2) ? "en" : "zh";   /* lang: 1=中文 2=English */
    int p = 0;
    const char *h = "oobe_done=1\nlang=";
    for (int i = 0; h[i]; i++) buf[p++] = h[i];
    buf[p++] = lang_s[0];
    buf[p++] = lang_s[1];
    buf[p++] = '\n';
    const char *t = "tz_min=";
    for (int i = 0; t[i]; i++) buf[p++] = t[i];
    /* 整数转十进制 */
    int v = tz_min, dv = 100000, started = 0;
    if (v < 0) { buf[p++] = '-'; v = -v; }
    while (dv > 0) {
        int d = v / dv;
        if (d || started) { buf[p++] = (char)('0' + d); started = 1; }
        v -= d * dv;
        dv /= 10;
    }
    if (!started) buf[p++] = '0';
    buf[p++] = '\n';
    buf[p] = '\0';
    auth64_write_config(SYS_PATH, buf, p);
    /* 语言独立文件，方便 l10n 模块读取 */
    char lb[4] = { lang_s[0], lang_s[1], '\n', '\0' };
    auth64_write_config(LANG_PATH, lb, 3);
}

/* ===== 向导主流程 ===== */
void oobe64_run(void)
{
    S = S_zh;   /* 初始中文 */
    keyboard64_flush();   /* 丢弃引导期间的残留按键噪声 */
    vga_puts(S[S_HEAD]);

    /* 1. 语言 */
    vga_puts(S[S_LANG]);
    int lang = oobe_choice(1, 2);
    S = lang == 1 ? S_zh : S_en;

    /* 2. 时区 */
    vga_puts(S[S_TZ]);
    int tz = oobe_choice(1, 6);
    static const int tz_tab[6] = { 480, 540, 60, 0, -300, -420 };
    int tz_min = tz_tab[tz - 1];

    /* 3. 用户类型 */
    vga_puts(S[S_TYPE]);
    int type = oobe_choice(1, 2);
    int role = type;   /* 1=普通 2=开发者 */

    /* 4. 开发者 → 管理员密码 */
    char admin_pass[64];
    admin_pass[0] = '\0';
    if (role == 2) {
        char p1[64], p2[64];
        for (;;) {
            vga_puts(S[S_ADMIN]);
            oobe_read_line(p1, sizeof(p1), 1);
            if (p1[0] != '\0') break;
        }
        (void)p2;
        for (int i = 0; p1[i]; i++) admin_pass[i] = p1[i];
        admin_pass[64 - 1] = '\0';
    }

    /* 5. 用户名 + 密码 */
    char user[32], pass[64], pass2[64];
    for (;;) {
        vga_puts(S[S_USER]);
        oobe_read_line(user, sizeof(user), 0);
        if (user[0] != '\0') break;
        vga_puts(S[S_NOUSER]);
    }
    for (;;) {
        vga_puts(S[S_PASS]);
        oobe_read_line(pass, sizeof(pass), 1);
        if (pass[0] != '\0' && pass[1] != '\0' &&
            pass[2] != '\0' && pass[3] != '\0') break;
    }
    for (;;) {
        vga_puts(S[S_PASS2]);
        oobe_read_line(pass2, sizeof(pass2), 1);
        int same = 1;
        for (int i = 0; i < 64; i++) {
            if (pass[i] != pass2[i]) { same = 0; break; }
            if (!pass[i]) break;
        }
        if (same) break;
        vga_puts(S[S_NOMATCH]);
    }

    /* 6. 创建账户 */
    if (auth64_create_account(user, pass, role,
                              role == 2 ? admin_pass : "") != 0) {
        vga_puts(S[S_FAIL]);
        return;
    }

    /* 7. 系统配置 + 欢迎屏 */
    save_system_conf(lang, tz_min);
    vga_puts(S[S_WELCOME]);
    while (!oobe_haskey()) { }
    oobe_getkey();
}
