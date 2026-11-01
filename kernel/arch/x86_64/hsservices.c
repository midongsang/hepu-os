/* ============================================================================
 * 赫普OS - HepuShell 服务集成实现
 * V0.10.169
 * ========================================================================== */

#include "hsservices.h"
#include "hepushell.h"

extern void vga_puts(const char *s);
extern void gfx64_fill_rect(int x, int y, int w, int h, uint32_t color);
extern void gfx64_draw_rect(int x, int y, int w, int h, uint32_t color);
extern void *gfx64_get_framebuffer(void);
extern int gfx64_get_width(void);
extern int gfx64_get_height(void);
extern void font64_draw_string(void *fb, int fw, int fh,
                               int x, int y, const char *str,
                               uint32_t color, uint16_t size);

/* 简单字符串操作 */
static int str_cmp(const char *s1, const char *s2)
{
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

static void str_cpy(char *dst, const char *src)
{
    while (*src) *dst++ = *src++;
    *dst = 0;
}

static void str_ncpy(char *dst, const char *src, int n)
{
    int i = 0;
    while (i < n - 1 && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

/* 全局服务状态 */
static hs_audio_service_t g_audio;
static hs_process_monitor_t g_proc;
static hs_file_monitor_t g_filemon;
static int services_initialized = 0;

/* ============================================================================
 * 音频服务
 * ========================================================================== */

int hs_audio_init(void)
{
    g_audio.initialized = 1;
    g_audio.master_volume = 75;
    g_audio.muted = 0;
    g_audio.stream_count = 0;

    for (int i = 0; i < HS_MAX_AUDIO_STREAMS; i++) {
        g_audio.streams[i].active = 0;
    }

    vga_puts("[AUDIO] Service initialized (AC97)\n");
    return 0;
}

void hs_audio_set_volume(int vol)
{
    if (vol < 0) vol = 0;
    if (vol > 100) vol = 100;
    g_audio.master_volume = vol;

    /* TODO: 写入AC97主音量寄存器 */
    extern void ac97_set_master_volume(int left, int right);
    ac97_set_master_volume(vol, vol);
}

int hs_audio_get_volume(void)
{
    return g_audio.master_volume;
}

void hs_audio_mute(void)
{
    g_audio.muted = 1;
}

void hs_audio_unmute(void)
{
    g_audio.muted = 0;
}

int hs_audio_is_muted(void)
{
    return g_audio.muted;
}

int hs_audio_play_tone(int freq, int duration)
{
    if (!g_audio.initialized || g_audio.muted) return -1;

    /* TODO: 使用AC97播放音调 */
    vga_puts("[AUDIO] Playing tone: ");
    char buf[16];
    int n = freq, i = 0;
    if (n == 0) buf[i++] = '0';
    while (n > 0) { buf[i++] = '0' + n % 10; n /= 10; }
    buf[i] = 0;
    for (int j = 0; j < i / 2; j++) {
        char t = buf[j]; buf[j] = buf[i-1-j]; buf[i-1-j] = t;
    }
    vga_puts(buf);
    vga_puts(" Hz\n");

    return 0;
}

void hs_audio_render_indicator(int x, int y)
{
    void *fb = gfx64_get_framebuffer();
    char buf[16];

    /* 音量图标 */
    if (g_audio.muted) {
        font64_draw_string(fb, gfx64_get_width(), gfx64_get_height(),
                           x, y, "[MUTE]", 0xF44336, 10);
    } else {
        font64_draw_string(fb, gfx64_get_width(), gfx64_get_height(),
                           x, y, "[VOL]", g_shell.colors.panel_fg, 10);
    }

    /* 音量条 */
    int bar_x = x + 40;
    int bar_w = 50;
    int bar_h = 6;

    gfx64_fill_rect(bar_x, y + 2, bar_w, bar_h, g_shell.colors.border);
    int fill_w = (bar_w * g_audio.master_volume) / 100;
    gfx64_fill_rect(bar_x, y + 2, fill_w, bar_h, g_shell.colors.accent);
}

/* ============================================================================
 * 进程监控
 * ========================================================================== */

int hs_process_monitor_init(void)
{
    g_proc.process_count = 0;
    g_proc.total_cpu = 0;
    g_proc.total_mem = 0;
    g_proc.mem_total = 2048;  /* 2GB */
    g_proc.mem_used = 512;
    g_proc.mem_free = 1536;

    /* 模拟一些进程 */
    const char *names[] = {"kernel", "shell", "desktop", "hepushell",
                           "terminal", "files", "network", "syslog"};
    for (int i = 0; i < 8; i++) {
        hs_process_info_t *p = &g_proc.processes[i];
        p->pid = i + 1;
        str_cpy(p->name, names[i]);
        p->cpu_usage = (i * 7) % 30;
        p->mem_usage = 1024 + i * 256;
        p->state = 0;
        g_proc.process_count++;
    }

    vga_puts("[PROC] Monitor initialized\n");
    return 0;
}

void hs_process_monitor_update(void)
{
    static int tick = 0;
    tick++;

    /* 更新CPU使用率（模拟波动） */
    g_proc.total_cpu = 15 + (tick * 3) % 40;
    g_proc.mem_used = 480 + (tick % 5) * 32;
    g_proc.mem_free = g_proc.mem_total - g_proc.mem_used;

    /* 更新各进程 */
    for (int i = 0; i < g_proc.process_count; i++) {
        g_proc.processes[i].cpu_usage = (i * 7 + tick) % 25;
    }
}

hs_process_info_t *hs_process_get_list(int *count)
{
    if (count) *count = g_proc.process_count;
    return g_proc.processes;
}

int hs_process_get_cpu_usage(void)
{
    return g_proc.total_cpu;
}

int hs_process_get_mem_usage(void)
{
    return (g_proc.mem_used * 100) / g_proc.mem_total;
}

void hs_process_render_tasklist(int x, int y, int w, int h)
{
    void *fb = gfx64_get_framebuffer();

    /* 背景 */
    gfx64_fill_rect(x, y, w, h, g_shell.colors.window_bg);
    gfx64_draw_rect(x, y, w, h, g_shell.colors.border);

    /* 标题 */
    gfx64_fill_rect(x, y, w, 24, g_shell.colors.titlebar_bg);
    font64_draw_string(fb, gfx64_get_width(), gfx64_get_height(),
                       x + 8, y + 5, "Task Manager",
                       g_shell.colors.window_fg, 12);

    /* 表头 */
    int ty = y + 30;
    font64_draw_string(fb, gfx64_get_width(), gfx64_get_height(),
                       x + 8, ty, "PID", g_shell.colors.panel_fg, 10);
    font64_draw_string(fb, gfx64_get_width(), gfx64_get_height(),
                       x + 50, ty, "NAME", g_shell.colors.panel_fg, 10);
    font64_draw_string(fb, gfx64_get_width(), gfx64_get_height(),
                       x + 150, ty, "CPU%", g_shell.colors.panel_fg, 10);
    font64_draw_string(fb, gfx64_get_width(), gfx64_get_height(),
                       x + 200, ty, "MEM", g_shell.colors.panel_fg, 10);

    /* 进程列表 */
    ty += 16;
    for (int i = 0; i < g_proc.process_count && ty < y + h - 16; i++) {
        hs_process_info_t *p = &g_proc.processes[i];

        char buf[16];
        int n = p->pid, k = 0;
        if (n == 0) buf[k++] = '0';
        while (n > 0) { buf[k++] = '0' + n % 10; n /= 10; }
        buf[k] = 0;
        for (int j = 0; j < k / 2; j++) {
            char t = buf[j]; buf[j] = buf[k-1-j]; buf[k-1-j] = t;
        }
        font64_draw_string(fb, gfx64_get_width(), gfx64_get_height(),
                           x + 8, ty, buf, g_shell.colors.window_fg, 10);

        font64_draw_string(fb, gfx64_get_width(), gfx64_get_height(),
                           x + 50, ty, p->name, g_shell.colors.window_fg, 10);

        n = p->cpu_usage; k = 0;
        if (n == 0) buf[k++] = '0';
        while (n > 0) { buf[k++] = '0' + n % 10; n /= 10; }
        buf[k] = 0;
        for (int j = 0; j < k / 2; j++) {
            char t = buf[j]; buf[j] = buf[k-1-j]; buf[k-1-j] = t;
        }
        font64_draw_string(fb, gfx64_get_width(), gfx64_get_height(),
                           x + 150, ty, buf, g_shell.colors.window_fg, 10);

        n = p->mem_usage; k = 0;
        if (n == 0) buf[k++] = '0';
        while (n > 0) { buf[k++] = '0' + n % 10; n /= 10; }
        buf[k] = 0;
        for (int j = 0; j < k / 2; j++) {
            char t = buf[j]; buf[j] = buf[k-1-j]; buf[k-1-j] = t;
        }
        font64_draw_string(fb, gfx64_get_width(), gfx64_get_height(),
                           x + 200, ty, buf, g_shell.colors.window_fg, 10);

        ty += 14;
    }
}

/* ============================================================================
 * 文件监控
 * ========================================================================== */

int hs_file_monitor_init(void)
{
    g_filemon.watch_count = 0;
    for (int i = 0; i < HS_MAX_WATCH_FILES; i++) {
        g_filemon.watches[i].active = 0;
    }

    vga_puts("[FILEMON] Monitor initialized\n");
    return 0;
}

int hs_file_watch_add(const char *path, void (*callback)(const char *))
{
    if (g_filemon.watch_count >= HS_MAX_WATCH_FILES) return -1;

    hs_file_watch_t *w = &g_filemon.watches[g_filemon.watch_count];
    str_ncpy(w->path, path, 128);
    w->active = 1;
    w->last_mtime = 0;
    w->callback = callback;

    vga_puts("[FILEMON] Watching: ");
    vga_puts(path);
    vga_puts("\n");

    return g_filemon.watch_count++;
}

void hs_file_watch_remove(const char *path)
{
    for (int i = 0; i < g_filemon.watch_count; i++) {
        if (str_cmp(g_filemon.watches[i].path, path) == 0) {
            g_filemon.watches[i].active = 0;
            return;
        }
    }
}

void hs_file_monitor_poll(void)
{
    /* TODO: 检查文件mtime变化 */
    /* 目前只是框架，实际需要ext2文件系统支持 */
}

/* ============================================================================
 * 服务总控
 * ========================================================================== */

int hs_services_init(void)
{
    if (services_initialized) return 0;

    hs_audio_init();
    hs_process_monitor_init();
    hs_file_monitor_init();

    services_initialized = 1;
    vga_puts("[SERVICES] All services initialized\n");
    return 0;
}

void hs_services_update(void)
{
    hs_process_monitor_update();
    hs_file_monitor_poll();
}
