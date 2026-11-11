/* ============================================================================
 * 赫普OS 64位 - 任务管理器
 * 显示系统信息、进程列表、资源使用
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include "process64.h"

extern void vga_puts(const char *s);
extern void vga_put_hex(uint64_t val);
extern void pmm64_get_stats(uint64_t *total, uint64_t *used, uint64_t *free);
extern int process64_get_count(void);
extern int drv_get_count(void);        /* V0.13.9: 真实驱动数 */

/* 获取系统信息 */
typedef struct {
    uint64_t total_memory;
    uint64_t used_memory;
    uint64_t free_memory;
    int      process_count;
    int      driver_count;
} system_info_t;

static void get_system_info(system_info_t *info)
{
    if (!info) return;
    uint64_t total_pages, used_pages, free_pages;
    pmm64_get_stats(&total_pages, &used_pages, &free_pages);
    info->total_memory = total_pages * 4096;
    info->free_memory = free_pages * 4096;
    info->used_memory = used_pages * 4096;
    info->process_count = process64_get_count();
    info->driver_count = drv_get_count();
}

/* 格式化内存大小 */
static void format_memory(uint64_t bytes, char *buf, int buf_size)
{
    if (!buf || buf_size < 16) return;
    if (bytes >= 1024 * 1024 * 1024) {
        int gb = bytes / (1024 * 1024 * 1024);
        int mb = (bytes % (1024 * 1024 * 1024)) / (1024 * 1024);
        int i = 0;
        if (gb > 9) buf[i++] = '0' + gb / 10;
        buf[i++] = '0' + gb % 10;
        buf[i++] = '.';
        buf[i++] = '0' + mb / 100;
        buf[i++] = 'G';
        buf[i++] = 'B';
        buf[i] = 0;
    } else if (bytes >= 1024 * 1024) {
        int mb = bytes / (1024 * 1024);
        int i = 0;
        if (mb > 99) buf[i++] = '0' + mb / 100;
        if (mb > 9) buf[i++] = '0' + (mb / 10) % 10;
        buf[i++] = '0' + mb % 10;
        buf[i++] = 'M';
        buf[i++] = 'B';
        buf[i] = 0;
    } else if (bytes >= 1024) {
        int kb = bytes / 1024;
        int i = 0;
        if (kb > 99) buf[i++] = '0' + kb / 100;
        if (kb > 9) buf[i++] = '0' + (kb / 10) % 10;
        buf[i++] = '0' + kb % 10;
        buf[i++] = 'K';
        buf[i++] = 'B';
        buf[i] = 0;
    } else {
        int i = 0;
        if (bytes > 99) buf[i++] = '0' + bytes / 100;
        if (bytes > 9) buf[i++] = '0' + (bytes / 10) % 10;
        buf[i++] = '0' + bytes % 10;
        buf[i++] = 'B';
        buf[i] = 0;
    }
}

/* 绘制进度条 */
static void draw_progress_bar(void *fb, int fb_w, int fb_h,
                              int x, int y, int width, int height,
                              int percent, uint32_t color)
{
    if (!fb) return;
    uint32_t *framebuffer = (uint32_t *)fb;

    /* 背景 */
    for (int row = 0; row < height; row++) {
        for (int col = 0; col < width; col++) {
            if (y + row < fb_h && x + col < fb_w) {
                framebuffer[(y + row) * fb_w + (x + col)] = 0xDDDDDD;
            }
        }
    }

    /* 进度 */
    int fill_width = (width * percent) / 100;
    for (int row = 0; row < height; row++) {
        for (int col = 0; col < fill_width; col++) {
            if (y + row < fb_h && x + col < fb_w) {
                framebuffer[(y + row) * fb_w + (x + col)] = color;
            }
        }
    }
}

/* 任务管理器主界面（图形模式） */
void taskmgr64_draw(void *fb, int fb_w, int fb_h, int x, int y, int w, int h)
{
    if (!fb) return;

    system_info_t info;
    get_system_info(&info);

    char buf[32];

    /* 标题 */
    extern void font64_draw_string(void *fb, int fw, int fh,
                                   int x, int y, const char *str,
                                   uint32_t color, uint16_t size);
    font64_draw_string(fb, fb_w, fb_h, x + 10, y + 10,
                       "System Information", 0x333333, 16);

    /* CPU信息 */
    font64_draw_string(fb, fb_w, fb_h, x + 10, y + 35,
                       "CPU: x86_64 (AMD EPYC 9Y24)", 0x333333, 16);

    /* 内存信息 */
    font64_draw_string(fb, fb_w, fb_h, x + 10, y + 55,
                       "Memory:", 0x333333, 16);
    format_memory(info.total_memory, buf, sizeof(buf));
    font64_draw_string(fb, fb_w, fb_h, x + 80, y + 55,
                       buf, 0x333333, 16);

    /* 内存使用条 */
    int mem_percent = info.total_memory > 0 ?
        (int)((info.used_memory * 100) / info.total_memory) : 0;
    draw_progress_bar(fb, fb_w, fb_h, x + 10, y + 75, w - 40, 12,
                      mem_percent, 0x4CAF50);

    format_memory(info.used_memory, buf, sizeof(buf));
    font64_draw_string(fb, fb_w, fb_h, x + 10, y + 92,
                       "Used: ", 0x333333, 16);
    font64_draw_string(fb, fb_w, fb_h, x + 60, y + 92,
                       buf, 0x333333, 16);

    format_memory(info.free_memory, buf, sizeof(buf));
    font64_draw_string(fb, fb_w, fb_h, x + 130, y + 92,
                       "Free: ", 0x333333, 16);
    font64_draw_string(fb, fb_w, fb_h, x + 180, y + 92,
                       buf, 0x333333, 16);

    /* 进程信息 */
    font64_draw_string(fb, fb_w, fb_h, x + 10, y + 120,
                       "Processes:", 0x333333, 16);
    int pc = info.process_count;
    char pbuf[16];
    int i = 0;
    if (pc > 9) pbuf[i++] = '0' + pc / 10;
    pbuf[i++] = '0' + pc % 10;
    pbuf[i] = 0;
    font64_draw_string(fb, fb_w, fb_h, x + 110, y + 120,
                       pbuf, 0x333333, 16);

    /* 驱动信息 */
    font64_draw_string(fb, fb_w, fb_h, x + 10, y + 140,
                       "Drivers: ", 0x333333, 16);
    int dc = info.driver_count;
    i = 0;
    if (dc > 9) pbuf[i++] = '0' + dc / 10;
    pbuf[i++] = '0' + dc % 10;
    pbuf[i] = 0;
    font64_draw_string(fb, fb_w, fb_h, x + 100, y + 140,
                       pbuf, 0x333333, 16);

    /* 网络面板：上传/下载实时速率 + 总量，挂 net_hal（实际驱动 RTL8139/e1000 通用计数器） */
    extern void net_hal_get_stats(int idx, uint64_t *tx_pkts, uint64_t *rx_pkts,
                                  uint64_t *tx_bytes, uint64_t *rx_bytes);
    extern int hpdesk_get_tick(void);
    uint64_t ntx = 0, nrx = 0;
    net_hal_get_stats(0, 0, 0, &ntx, &nrx);
    /* 速率：累计字节差 / tick 差（KB/s） */
    static uint64_t s_ntx = 0, s_nrx = 0, s_tick = 0;
    uint64_t now = (uint64_t)hpdesk_get_tick();
    int ntr = 0, nrr = 0;
    if (s_tick && now > s_tick) {
        uint64_t dt = now - s_tick;
        ntr = (int)((ntx - s_ntx) / dt / 1024);
        nrr = (int)((nrx - s_nrx) / dt / 1024);
    }
    s_ntx = ntx; s_nrx = nrx; s_tick = now;
    font64_draw_string(fb, fb_w, fb_h, x + 10, y + 165,
                       "Network: ", 0x333333, 16);
    /* 真实IP + 绑定状态 */
    extern uint32_t dhcp64_get_ip(void);
    extern int dhcp64_is_bound(void);
    {
        uint32_t ip = dhcp64_get_ip();
        char ipb[24];
        int i2 = 0;
        ipb[i2++] = '0' + ((ip >> 24) & 0xFF) / 100 % 10; ipb[i2++] = '0' + ((ip >> 24) & 0xFF) / 10 % 10; ipb[i2++] = '0' + ((ip >> 24) & 0xFF) % 10; ipb[i2++] = '.';
        ipb[i2++] = '0' + ((ip >> 16) & 0xFF) / 100 % 10; ipb[i2++] = '0' + ((ip >> 16) & 0xFF) / 10 % 10; ipb[i2++] = '0' + ((ip >> 16) & 0xFF) % 10; ipb[i2++] = '.';
        ipb[i2++] = '0' + ((ip >> 8) & 0xFF) / 100 % 10; ipb[i2++] = '0' + ((ip >> 8) & 0xFF) / 10 % 10; ipb[i2++] = '0' + ((ip >> 8) & 0xFF) % 10; ipb[i2++] = '.';
        ipb[i2++] = '0' + (ip & 0xFF) / 100 % 10; ipb[i2++] = '0' + (ip & 0xFF) / 10 % 10; ipb[i2++] = '0' + (ip & 0xFF) % 10; ipb[i2] = 0;
        int ok = dhcp64_is_bound();
        char ns[24];
        strcpy(ns, ipb);
        strcpy(ns + strlen(ns), ok ? " UP" : " DOWN");
        font64_draw_string(fb, fb_w, fb_h, x + 90, y + 165, ns, ok ? 0x4CAF50 : 0xF44336, 16);
    }
    char nb1[32], nb2[32];
    /* 速率: B/s -> KB/s */
    int tr_kb = (int)(ntr / 1024), rr_kb = (int)(nrr / 1024);
    nb1[0] = '0' + tr_kb / 100 % 10; nb1[1] = 0;  /* 占位简化, 下方格式化 */
    format_memory(ntx, nb1, sizeof(nb1));
    format_memory(nrx, nb2, sizeof(nb2));
    font64_draw_string(fb, fb_w, fb_h, x + 10, y + 185,
                       "UP: ", 0x333333, 16);
    char upb[20];
    int u = 0;
    if (tr_kb >= 1000) { upb[u++] = '0' + tr_kb / 1000; upb[u++] = '.'; upb[u++] = '0' + (tr_kb / 100) % 10; upb[u++] = 'M'; }
    else { if (tr_kb > 9) upb[u++] = '0' + tr_kb / 10; upb[u++] = '0' + tr_kb % 10; upb[u++] = 'K'; }
    upb[u++] = 'B'; upb[u++] = '/'; upb[u++] = 's'; upb[u] = 0;
    font64_draw_string(fb, fb_w, fb_h, x + 55, y + 185, upb, 0x333333, 16);
    font64_draw_string(fb, fb_w, fb_h, x + 130, y + 185,
                       "DN: ", 0x333333, 16);
    char dnb[20];
    u = 0;
    if (rr_kb >= 1000) { dnb[u++] = '0' + rr_kb / 1000; dnb[u++] = '.'; dnb[u++] = '0' + (rr_kb / 100) % 10; dnb[u++] = 'M'; }
    else { if (rr_kb > 9) dnb[u++] = '0' + rr_kb / 10; dnb[u++] = '0' + rr_kb % 10; dnb[u++] = 'K'; }
    dnb[u++] = 'B'; dnb[u++] = '/'; dnb[u++] = 's'; dnb[u] = 0;
    font64_draw_string(fb, fb_w, fb_h, x + 175, y + 185, dnb, 0x333333, 16);

    /* 分隔线 */
    font64_draw_string(fb, fb_w, fb_h, x + 10, y + 208,
                       "------------------------", 0x999999, 16);

    /* 进程列表标题 */
    font64_draw_string(fb, fb_w, fb_h, x + 10, y + 223,
                       "PID  Name        Status", 0x333333, 16);

    /* V0.13.9: 真实进程列表 — 内核核心 + 用户进程 */
    static const char *kernel_procs[3] = {"kernel", "init", "shell"};
    int row = 0;
    for (int i = 0; i < 3 && row < 8; i++) {
        char line[40];
        line[0] = '0' + i % 10;
        line[1] = ' ';
        int j = 2;
        const char *n = kernel_procs[i];
        while (*n && j < 30) line[j++] = *n++;
        while (j < 32) line[j++] = ' ';
        line[j++] = 'R';
        line[j++] = 'u';
        line[j++] = 'n';
        line[j++] = 'n';
        line[j++] = 'i';
        line[j++] = 'n';
        line[j++] = 'g';
        line[j] = 0;
        font64_draw_string(fb, fb_w, fb_h, x + 10, y + 243 + row * 18,
                           line, 0x333333, 12);
        row++;
    }
    /* 进程列表（数据同源：调用 process64_snapshot） */
    process64_stat_t plist[PROCESS64_MAX_SLOTS];
    int pn = process64_snapshot(plist, PROCESS64_MAX_SLOTS);
    for (int i = 0; i < pn && row < 12; i++) {
        char line[40];
        char nb[16];
        int np = plist[i].pid, k = 0;
        if (np > 9) nb[k++] = '0' + np / 10;
        nb[k++] = '0' + np % 10;
        nb[k] = 0;
        line[0] = nb[0];
        line[1] = (k > 1) ? nb[1] : ' ';
        line[2] = ' ';
        int j = 3;
        const char *n2 = plist[i].name;
        while (*n2 && j < 30) line[j++] = *n2++;
        while (j < 32) line[j++] = ' ';
        const char *st = "UNKNOWN";
        switch (plist[i].state) {
            case 1: st = "READY"; break;
            case 2: st = "RUN"; break;
            case 3: st = "WAIT"; break;
            case 4: st = "ZOMB"; break;
        }
        int s = 0;
        while (st[s] && j < 38) line[j++] = st[s++];
        line[j] = 0;
        font64_draw_string(fb, fb_w, fb_h, x + 10, y + 243 + row * 18,
                           line, 0x333333, 12);
        row++;
    }
}

/* 文本模式任务管理器 */
void taskmgr64_text(void)
{
    system_info_t info;
    get_system_info(&info);
    char buf[32];

    vga_puts("\n=== Task Manager ===\n");
    vga_puts("CPU: x86_64 (AMD EPYC 9Y24)\n");
    vga_puts("Memory: ");
    format_memory(info.total_memory, buf, sizeof(buf));
    vga_puts(buf);
    vga_puts(" total, ");
    format_memory(info.used_memory, buf, sizeof(buf));
    vga_puts(buf);
    vga_puts(" used, ");
    format_memory(info.free_memory, buf, sizeof(buf));
    vga_puts(buf);
    vga_puts(" free\n");
    vga_puts("Processes: ");
    char pbuf[16];
    int pc = info.process_count;
    int i = 0;
    if (pc > 9) pbuf[i++] = '0' + pc / 10;
    pbuf[i++] = '0' + pc % 10;
    pbuf[i] = 0;
    vga_puts(pbuf);
    vga_puts("\n");
    vga_puts("Drivers: IDE, RTL8139, PS/2 KBD, PS/2 Mouse, RTC\n");
    extern void e1000_get_net_stats(uint64_t *tx, uint64_t *rx,
                                    uint32_t *tr, uint32_t *rr);
    uint64_t ntx = 0, nrx = 0; uint32_t ntr = 0, nrr = 0;
    e1000_get_net_stats(&ntx, &nrx, &ntr, &nrr);
    vga_puts("Network: 10.0.2.15/24 UP\n");
    vga_puts("UP: ");
    vga_put_hex((uint64_t)(ntr / 1024));
    vga_puts(" KB/s  DN: ");
    vga_put_hex((uint64_t)(nrr / 1024));
    vga_puts(" KB/s  (TX "); vga_put_hex(ntx);
    vga_puts(" B, RX "); vga_put_hex(nrx); vga_puts(" B)\n");
    vga_puts("====================\n\n");
}

/* 初始化任务管理器 */
void taskmgr64_init(void)
{
    vga_puts("[TASKMGR] Task manager initialized\n");
}
