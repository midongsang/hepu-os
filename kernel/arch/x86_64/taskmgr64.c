/* ============================================================================
 * 赫普OS 64位 - 任务管理器
 * 显示系统信息、进程列表、资源使用
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>

extern void vga_puts(const char *s);
extern void vga_put_hex(uint64_t val);
extern void pmm64_get_stats(uint64_t *total, uint64_t *used, uint64_t *free);
extern int process64_get_count(void);

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
    info->driver_count = 5;
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

    /* 分隔线 */
    font64_draw_string(fb, fb_w, fb_h, x + 10, y + 165,
                       "------------------------", 0x999999, 16);

    /* 进程列表标题 */
    font64_draw_string(fb, fb_w, fb_h, x + 10, y + 180,
                       "PID  Name        Status", 0x333333, 16);

    /* 内核进程 */
    font64_draw_string(fb, fb_w, fb_h, x + 10, y + 200,
                       "0    kernel      Running", 0x333333, 16);
    font64_draw_string(fb, fb_w, fb_h, x + 10, y + 218,
                       "1    init        Running", 0x333333, 16);
    font64_draw_string(fb, fb_w, fb_h, x + 10, y + 236,
                       "2    shell       Running", 0x333333, 16);
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
    vga_puts("====================\n\n");
}

/* 初始化任务管理器 */
void taskmgr64_init(void)
{
    vga_puts("[TASKMGR] Task manager initialized\n");
}
