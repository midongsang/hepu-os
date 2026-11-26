/* ============================================================================
 * Hepu OS - logd（OPEN-05，形态 B：非驻留用户态 worker）
 *
 * 定位：内核周期拉起的短命进程。单次执行链路：
 *   1. 读 RTC 日期，拼按日期命名的落盘文件 /var/log/hepu/YYYY-MM-DD.log；
 *   2. 读游标文件 .cursor 里上次消费到的序号；
 *   3. 经 SYS_EVENT_LOG_PULL 拉取游标之后的普通环条目；
 *   4. 追加写回当日文件（ext2 无原地追加原语：读旧文件 -> 内存拼接 -> 整文件写回）；
 *   5. 游标落盘后 exit(0)，把 CPU 交还内核主循环。
 *
 * 明确边界：
 *   - 本程序**不是常驻 daemon**：不做 while(1)，不忙等；内核调研已证明 BSP
 *     无可抢占用户调度，常驻会冻结桌面主循环。
 *   - 游标是普通环 256 条容量内的尽力增量消费；落后过环容量时旧记录已被覆盖，
 *     缺口由内核侧 oldest_seq 判定，本程序只按返回结果落盘，不承诺不丢历史。
 *   - 远程 syslog 未做：用户态 socket/sendto 当前为桩。
 * ========================================================================== */
#include "../../ulib.h"

/* O_CREAT 字面量，与内核 fs_open_sys.c FS_O_CREAT=0x40 对齐。 */
#define LOGD_O_CREAT 0x40
#define LOGD_O_WRONLY 1
#define LOGD_O_RDONLY 0

#define LOGD_MAX_PULL   16
#define LOGD_OLD_BUF    4096
#define LOGD_NEW_BUF    4096

static hepu_event_entry_t g_entries[LOGD_MAX_PULL];
static unsigned char g_old[LOGD_OLD_BUF];
static unsigned char g_new[LOGD_NEW_BUF];
static unsigned char g_mix[LOGD_OLD_BUF + LOGD_NEW_BUF];
static char g_line[220];

static void out(const char *s)
{
    while (*s) write(STDOUT_FILENO, s++, 1);
}

static void out_dec(uint32_t v)
{
    char b[12]; int i = 0;
    if (v == 0) { write(STDOUT_FILENO, "0", 1); return; }
    while (v && i < 12) { b[i++] = (char)('0' + v % 10); v /= 10; }
    while (i > 0) write(STDOUT_FILENO, &b[--i], 1);
}

static const char *level_name(uint8_t lv)
{
    switch (lv) {
        case 0: return "DEBUG";
        case 1: return "INFO";
        case 2: return "WARN";
        case 3: return "ERROR";
        case 4: return "FATAL";
        default: return "?";
    }
}

/* ---- 行拼接（手工，不走 printf：freestanding 下变参在本加载器会 #PF） ---- */
static int l_puts(char *d, int cap, int pos, const char *s)
{
    while (*s && pos + 1 < cap) d[pos++] = *s++;
    return pos;
}

static int l_dec(char *d, int cap, int pos, uint32_t v, int width)
{
    char b[12]; int i = 0;
    if (v == 0) b[i++] = '0';
    while (v && i < 12) { b[i++] = (char)('0' + v % 10); v /= 10; }
    int digits = i;
    /* 补零只写输出缓冲，绝不递增 b 的索引；反序阶段只写真实 digits。 */
    while (digits < width && pos + 1 < cap) { d[pos++] = '0'; digits++; }
    while (i > 0 && pos + 1 < cap) d[pos++] = b[--i];
    return pos;
}

static int l_hex(char *d, int cap, int pos, uint32_t v)
{
    static const char h[] = "0123456789abcdef";
    char b[8]; int i = 0;
    if (v == 0) b[i++] = '0';
    while (v && i < 8) { b[i++] = h[v & 0xF]; v >>= 4; }
    int digits = i;
    /* 定宽 8 位十六进制零填充：补零只写输出，反序只写真实 digits。 */
    while (digits < 8 && pos + 1 < cap) { d[pos++] = '0'; digits++; }
    while (i > 0 && pos + 1 < cap) d[pos++] = b[--i];
    return pos;
}

static int l_str_pad(char *d, int cap, int pos, const char *s, int w)
{
    int n = 0;
    while (s[n] && n < w) { if (pos + 1 < cap) d[pos++] = s[n]; n++; }
    while (n < w) { if (pos + 1 < cap) d[pos++] = ' '; n++; }
    return pos;
}

static int mkdir(const char *p)
{
    return (int)syscall2(SYS_MKDIR, (uint64_t)p, 0);
}

/* 整文件读入；返回读到的字节数（不存在/失败视为 0）。 */
static int read_whole(const char *path, unsigned char *buf, int cap)
{
    int fd = open(path, LOGD_O_RDONLY);
    if (fd < 0) return 0;
    int total = 0;
    for (;;) {
        int r = read(fd, buf + total, cap - total);
        if (r <= 0) break;
        total += r;
        if (total >= cap) break;
    }
    close(fd);
    return total;
}

/* 整文件写回（内核 write 按块读改写；这里一次性把 mix 全量写出）。 */
static int write_whole(const char *path, const unsigned char *buf, int len)
{
    int fd = open(path, LOGD_O_WRONLY | LOGD_O_CREAT);
    if (fd < 0) return -1;
    int off = 0;
    while (off < len) {
        int w = write(fd, buf + off, len - off);
        if (w <= 0) { close(fd); return -1; }
        off += w;
    }
    close(fd);
    return len;
}

/* 读游标文件（纯十进制序号文本）；不存在/损坏返回 0。 */
static uint32_t read_cursor(const char *path)
{
    unsigned char b[16];
    int n = read_whole(path, b, sizeof(b) - 1);
    if (n <= 0) return 0;
    b[n] = 0;
    uint32_t v = 0;
    for (int i = 0; i < n; i++) {
        if (b[i] < '0' || b[i] > '9') break;
        v = v * 10u + (uint32_t)(b[i] - '0');
    }
    return v;
}

static int write_cursor(const char *path, uint32_t seq)
{
    char b[12]; int i = 0;
    if (seq == 0) b[i++] = '0';
    while (seq && i < 12) { b[i++] = (char)('0' + seq % 10); seq /= 10; }
    /* 反序写回 */
    char rev[12]; int j = 0;
    while (i > 0) rev[j++] = b[--i];
    return write_whole(path, (const unsigned char *)rev, j);
}

int main(void)
{
    uint32_t tm[6] = { 0, 0, 0, 0, 0, 0 };
    int date_rc = event_log_date(tm);

    /* 目录逐级创建：ext2 建文件不会自动建父目录。存在与否的错误都忽略。 */
    mkdir("/var");
    mkdir("/var/log");
    mkdir("/var/log/hepu");

    char dir[32];
    const char *dirp = "/var/log/hepu/";
    int dp = 0;
    while (dirp[dp] && dp < (int)sizeof(dir) - 1) { dir[dp] = dirp[dp]; dp++; }
    dir[dp] = 0;

    char dated[40];
    dated[0] = 0;
    l_str_pad(dated, sizeof(dated), 0, dir, dp);
    if (date_rc == 0 && tm[0] > 1970) {
        int p = dp;
        p = l_dec(dated, sizeof(dated), p, tm[0], 4);
        dated[p++] = '-';
        p = l_dec(dated, sizeof(dated), p, tm[1], 2);
        dated[p++] = '-';
        p = l_dec(dated, sizeof(dated), p, tm[2], 2);
        dated[p++] = '.';
        dated[p++] = 'l'; dated[p++] = 'o'; dated[p++] = 'g'; dated[p] = 0;
    } else {
        l_puts(dated, sizeof(dated), dp, "unknown.log");
    }

    const char *cursor_path = "/var/log/hepu/.cursor";
    uint32_t after = read_cursor(cursor_path);

    uint32_t last = 0;
    int n = event_log_pull(after, g_entries, LOGD_MAX_PULL, &last, 0);

    int new_len = 0;
    for (int i = 0; i < n; i++) {
        int p = 0;
        p = l_dec(g_line, sizeof(g_line), p, g_entries[i].seq, 8);
        g_line[p++] = ' ';
        p = l_hex(g_line, sizeof(g_line), p,
                  (uint32_t)(g_entries[i].tick >> 32));
        p = l_hex(g_line, sizeof(g_line), p,
                  (uint32_t)(g_entries[i].tick & 0xFFFFFFFFu));
        g_line[p++] = ' ';
        p = l_str_pad(g_line, sizeof(g_line), p,
                      level_name(g_entries[i].level), 5);
        g_line[p++] = ' ';
        p = l_str_pad(g_line, sizeof(g_line), p, g_entries[i].source, 12);
        g_line[p++] = ' ';
        p = l_puts(g_line, sizeof(g_line), p, "pid=");
        p = l_dec(g_line, sizeof(g_line), p, g_entries[i].pid, 0);
        g_line[p++] = ' ';
        p = l_puts(g_line, sizeof(g_line), p, "code=");
        p = l_dec(g_line, sizeof(g_line), p, g_entries[i].code, 0);
        g_line[p++] = ' ';
        p = l_puts(g_line, sizeof(g_line), p, g_entries[i].msg);
        g_line[p++] = '\n';

        if (new_len + p > LOGD_NEW_BUF) break;
        for (int k = 0; k < p; k++) g_new[new_len++] = (unsigned char)g_line[k];
    }

    int old_len = read_whole(dated, g_old, LOGD_OLD_BUF);

    int wrote = 0;
    if (new_len > 0) {
        /* 旧内容让位：新条目整体优先进得去；旧文件超长部分截断（边界说明见头注释）。 */
        int old_keep = old_len;
        if (old_keep + new_len > (int)sizeof(g_mix))
            old_keep = (int)sizeof(g_mix) - new_len;
        if (old_keep < 0) old_keep = 0;
        for (int k = 0; k < old_keep; k++) g_mix[k] = g_old[k];
        for (int k = 0; k < new_len; k++) g_mix[old_keep + k] = g_new[k];
        int mix_len = old_keep + new_len;
        wrote = write_whole(dated, g_mix, mix_len);
        if (wrote > 0) write_cursor(cursor_path, last);
    }

    /* 串口取证行：consumed 条数 + 落盘路径 + 游标。vga/串口镜像由 stdout 承担。 */
    out("[LOGD] consumed=");
    out_dec((uint32_t)n);
    out(" wrote=");
    out(dated);
    out(" bytes=");
    out_dec((uint32_t)wrote);
    out(" last_seq=");
    out_dec(last);
    out("\n");

    return 0;
}

void _start(void) { exit(main()); }
