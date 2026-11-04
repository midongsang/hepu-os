/* ============================================================================
 * 赫普OS - 图形模式任务管理器（V7.45）
 *
 * 功能：
 * - 系统信息面板（CPU、内存、显存、进程数）
 * - 进程列表（PID、名称、状态、CPU使用率、内存使用）
 * - 资源使用条（可视化显示）
 * - 进程操作（终止进程、刷新）
 * - 键盘交互（W/S选择、K终止、R刷新、Q/ESC退出）
 * - 中文界面
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdarg.h>
#include "graphics.h"
#include "process.h"
#include "mm.h"
#include "virtual_vram.h"
#include "keyboard.h"

/* 图形模式全局变量 */
extern int gfx_width;
extern int gfx_height;

/* 颜色定义 */
#define TM_BG_COLOR         0x1A202C  /* 深色背景 */
#define TM_PANEL_COLOR      0x2D3748  /* 面板背景 */
#define TM_TEXT_COLOR       0xFFFFFF  /* 白色文字 */
#define TM_TEXT_DIM         0xA0AEC0  /* 灰色文字 */
#define TM_ACCENT_COLOR     0x3182CE  /* 蓝色强调 */
#define TM_GREEN            0x48BB78  /* 绿色 */
#define TM_RED              0xF56565  /* 红色 */
#define TM_YELLOW           0xECC94B  /* 黄色 */
#define TM_BORDER_COLOR     0x4A5568  /* 边框色 */
#define TM_SELECTED_COLOR   0x2B6CB0  /* 选中行背景 */

/* 任务管理器状态 */
typedef struct {
    int      visible;
    int      selected_proc;
    int      scroll_offset;
    int      proc_count;
    uint32_t refresh_ticks;
} task_mgr_gui_t;

static task_mgr_gui_t tm_gui;

/* 进程状态字符串（中文） */
static const char *proc_state_str_gui(int state)
{
    switch (state) {
        case 0: return "未使用";
        case 1: return "运行中";
        case 2: return "就绪";
        case 3: return "阻塞";
        case 4: return "睡眠";
        case 5: return "僵尸";
        default: return "未知";
    }
}

/* 绘制使用条 */
static void draw_usage_bar_gui(int x, int y, int width, int height, int percent, uint32_t color)
{
    /* 背景 */
    gfx_fill_rounded_rect(x, y, width, height, 3, 0x4A5568);
    /* 填充 */
    int fill_w = (width - 4) * percent / 100;
    if (fill_w > 0) {
        gfx_fill_rounded_rect(x + 2, y + 2, fill_w, height - 4, 2, color);
    }
}

/* 绘制系统信息面板（V7.46: 接入真实数据） */
static void draw_system_info(int x, int y, int width)
{
    int panel_h = 168;   /* V0.13.65: 增加网络速度行 */
    gfx_fill_rounded_rect(x, y, width, panel_h, 8, TM_PANEL_COLOR);

    /* 标题 */
    gfx_draw_string_font(x + 16, y + 12, "系统信息", TM_TEXT_COLOR, 18);
    gfx_draw_hline(x + 16, y + 36, width - 32, TM_BORDER_COLOR);

    /* 获取进程信息 */
    uint32_t proc_total, proc_running, proc_ready;
    process_stats(&proc_total, &proc_running, &proc_ready);

    /* CPU使用率（真实计算） */
    extern uint32_t sched_get_ticks(void);
    extern uint32_t sched_get_idle_ticks(void);
    static uint32_t last_total_ticks = 0;
    static uint32_t last_idle_ticks = 0;
    static int cpu_usage_cached = 0;
    uint32_t total_ticks = sched_get_ticks();
    uint32_t idle_ticks = sched_get_idle_ticks();
    if (total_ticks > last_total_ticks) {
        uint32_t delta_total = total_ticks - last_total_ticks;
        uint32_t delta_idle = idle_ticks - last_idle_ticks;
        if (delta_total > 0) {
            cpu_usage_cached = 100 - (delta_idle * 100 / delta_total);
            if (cpu_usage_cached < 0) cpu_usage_cached = 0;
            if (cpu_usage_cached > 100) cpu_usage_cached = 100;
        }
    }
    last_total_ticks = total_ticks;
    last_idle_ticks = idle_ticks;
    int cpu_usage = cpu_usage_cached;

    /* 内存使用（真实数据） */
    extern void mm_stats(uint32_t *total, uint32_t *used, uint32_t *free);
    uint32_t mem_total_pages, mem_used_pages, mem_free_pages;
    mm_stats(&mem_total_pages, &mem_used_pages, &mem_free_pages);
    uint32_t mem_total_kb = mem_total_pages * 4;  /* 4KB per page */
    uint32_t mem_used_kb = mem_used_pages * 4;
    int mem_percent = mem_total_pages ? (mem_used_pages * 100 / mem_total_pages) : 0;

    /* 显存使用（真实数据） */
    extern void vvram_get_stats(vvram_stats_t *stats);
    vvram_stats_t vram_stats;
    vvram_get_stats(&vram_stats);
    int vram_percent = 0;
    uint32_t vram_used_kb = 0, vram_total_kb = 0;
    if (vram_stats.total_vram > 0) {
        vram_used_kb = vram_stats.used_vram / 1024;
        vram_total_kb = vram_stats.total_vram / 1024;
        vram_percent = vram_stats.used_vram * 100 / vram_stats.total_vram;
    }

    int row_y = y + 48;
    int label_x = x + 16;
    int bar_x = x + 100;
    int bar_w = width - 200;
    int value_x = x + width - 100;

    /* CPU */
    gfx_draw_string_font(label_x, row_y, "CPU", TM_TEXT_DIM, 14);
    draw_usage_bar_gui(bar_x, row_y, bar_w, 16, cpu_usage, TM_GREEN);
    char buf[32];
    snprintf(buf, sizeof(buf), "%3d%%", cpu_usage);
    gfx_draw_string_font(value_x, row_y, buf, TM_TEXT_COLOR, 14);

    /* 内存 */
    row_y += 28;
    gfx_draw_string_font(label_x, row_y, "内存", TM_TEXT_DIM, 14);
    draw_usage_bar_gui(bar_x, row_y, bar_w, 16, mem_percent, TM_ACCENT_COLOR);
    snprintf(buf, sizeof(buf), "%d/%dMB", mem_used_kb/1024, mem_total_kb/1024);
    gfx_draw_string_font(value_x, row_y, buf, TM_TEXT_COLOR, 14);

    /* 显存 */
    row_y += 28;
    gfx_draw_string_font(label_x, row_y, "显存", TM_TEXT_DIM, 14);
    draw_usage_bar_gui(bar_x, row_y, bar_w, 16, vram_percent, TM_YELLOW);
    snprintf(buf, sizeof(buf), "%d/%dKB", vram_used_kb, vram_total_kb);
    gfx_draw_string_font(value_x, row_y, buf, TM_TEXT_COLOR, 14);

    /* 进程统计 */
    row_y += 28;
    gfx_draw_string_font(label_x, row_y, "进程", TM_TEXT_DIM, 14);
    snprintf(buf, sizeof(buf), "共%d个 运行%d 就绪%d", proc_total, proc_running, proc_ready);
    gfx_draw_string_font(bar_x, row_y, buf, TM_TEXT_COLOR, 14);

    /* 网络速度 (V0.13.65: 每秒采样 rx/tx 字节差, tick 差换算秒) */
#ifdef HEPU_KERNEL_64
    extern void net64_get_bytes(uint64_t *r, uint64_t *t);
#else
    /* 32 位内核只有 net3 统计，且无 libgcc（不能出现 64 位除法） */
    extern int net3_get_stats(uint64_t *rx, uint64_t *tx, uint64_t *packets,
                              uint64_t *tcp_conns, uint64_t *dns);
#endif
    static uint32_t last_net_ticks = 0;
    static uint64_t last_net_rx = 0, last_net_tx = 0;
    static int net_down_kbs = 0, net_up_kbs = 0;
    uint64_t net_rx_b, net_tx_b;
    uint32_t now_ticks = sched_get_ticks();
#ifdef HEPU_KERNEL_64
    net64_get_bytes(&net_rx_b, &net_tx_b);
#else
    net3_get_stats(&net_rx_b, &net_tx_b, 0, 0, 0);
#endif
    uint32_t dt_ticks = now_ticks - last_net_ticks;
    if (dt_ticks >= 10 && net_rx_b >= last_net_rx && net_tx_b >= last_net_tx) {
        uint32_t dsec = dt_ticks / 10;   /* 100Hz -> 厘秒; 除10=百ms; 秒 = dsec/10 */
        if (dsec > 0) {
#ifdef HEPU_KERNEL_64
            net_down_kbs = (int)((net_rx_b - last_net_rx) * 10ULL / dsec / 1024);
            net_up_kbs   = (int)((net_tx_b - last_net_tx) * 10ULL / dsec / 1024);
#else
            /* 单次采样差值远小于 4GB，取低 32 位做纯 32 位运算 */
            uint32_t drx = (uint32_t)(net_rx_b - last_net_rx);
            uint32_t dtx = (uint32_t)(net_tx_b - last_net_tx);
            net_down_kbs = (int)(drx * 10u / dsec / 1024u);
            net_up_kbs   = (int)(dtx * 10u / dsec / 1024u);
#endif
        }
        last_net_ticks = now_ticks;
        last_net_rx = net_rx_b;
        last_net_tx = net_tx_b;
    }
    row_y += 28;
    gfx_draw_string_font(label_x, row_y, "网络", TM_TEXT_DIM, 14);
    snprintf(buf, sizeof(buf), "↓%dKB/s ↑%dKB/s", net_down_kbs, net_up_kbs);
    gfx_draw_string_font(bar_x, row_y, buf, TM_TEXT_COLOR, 14);
}

/* 绘制进程列表 */
static void draw_process_list(int x, int y, int width, int height)
{
    extern process_t proc_table[MAX_PROCESSES];

    gfx_fill_rounded_rect(x, y, width, height, 8, TM_PANEL_COLOR);

    /* 标题 */
    gfx_draw_string_font(x + 16, y + 12, "进程列表", TM_TEXT_COLOR, 18);
    gfx_draw_hline(x + 16, y + 36, width - 32, TM_BORDER_COLOR);

    /* 表头 */
    int header_y = y + 44;
    gfx_draw_string_font(x + 16, header_y, "PID", TM_TEXT_DIM, 12);
    gfx_draw_string_font(x + 70, header_y, "名称", TM_TEXT_DIM, 12);
    gfx_draw_string_font(x + 220, header_y, "状态", TM_TEXT_DIM, 12);
    gfx_draw_string_font(x + 300, header_y, "CPU", TM_TEXT_DIM, 12);
    gfx_draw_string_font(x + 360, header_y, "内存(KB)", TM_TEXT_DIM, 12);
    gfx_draw_hline(x + 16, header_y + 18, width - 32, TM_BORDER_COLOR);

    /* 收集活跃进程 */
    int procs[64];
    int proc_count = 0;
    for (int i = 0; i < MAX_PROCESSES && proc_count < 64; i++) {
        if (proc_table[i].state != 0) { /* PROC_UNUSED */
            procs[proc_count++] = i;
        }
    }
    tm_gui.proc_count = proc_count;

    /* 显示进程 */
    int list_y = header_y + 26;
    int row_h = 24;
    int max_rows = (height - 80) / row_h;
    int display_start = tm_gui.scroll_offset;
    int display_end = display_start + max_rows;
    if (display_end > proc_count) display_end = proc_count;

    extern uint32_t sched_get_ticks(void);
    uint32_t total_ticks = sched_get_ticks();

    for (int i = display_start; i < display_end; i++) {
        int idx = procs[i];
        process_t *p = &proc_table[idx];
        int row = list_y + (i - display_start) * row_h;

        /* 选中行高亮 */
        if (i == tm_gui.selected_proc) {
            gfx_fill_rect(x + 8, row - 2, width - 16, row_h - 2, TM_SELECTED_COLOR);
        }

        /* CPU使用率 */
        int cpu_pct = total_ticks ? (p->ticks_total * 100 / total_ticks) : 0;
        if (cpu_pct > 100) cpu_pct = 100;

        char buf[64];
        snprintf(buf, sizeof(buf), "%4d", p->pid);
        gfx_draw_string_font(x + 16, row, buf, TM_TEXT_COLOR, 12);

        /* 进程名称（截断） */
        char name_buf[32];
        strncpy(name_buf, p->name, 20);
        name_buf[20] = 0;
        gfx_draw_string_font(x + 70, row, name_buf, TM_TEXT_COLOR, 12);

        /* 状态 */
        const char *state_str = proc_state_str_gui(p->state);
        uint32_t state_color = TM_TEXT_DIM;
        if (p->state == 1) state_color = TM_GREEN;      /* 运行中 */
        else if (p->state == 2) state_color = TM_ACCENT_COLOR;  /* 就绪 */
        else if (p->state == 5) state_color = TM_RED;   /* 僵尸 */
        gfx_draw_string_font(x + 220, row, state_str, state_color, 12);

        /* CPU */
        snprintf(buf, sizeof(buf), "%3d%%", cpu_pct);
        gfx_draw_string_font(x + 300, row, buf, TM_TEXT_COLOR, 12);

        /* 内存 */
        snprintf(buf, sizeof(buf), "%8d", p->mem_used * (PAGE_SIZE / 1024));
        gfx_draw_string_font(x + 360, row, buf, TM_TEXT_COLOR, 12);
    }

    /* 滚动提示 */
    if (proc_count > max_rows) {
        char scroll_buf[32];
        snprintf(scroll_buf, sizeof(scroll_buf), "显示 %d-%d / 共%d个",
                 display_start + 1, display_end, proc_count);
        gfx_draw_string_font(x + width - 150, y + height - 24, scroll_buf, TM_TEXT_DIM, 12);
    }
}

/* 绘制底部状态栏 */
static void draw_status_bar(int x, int y, int width)
{
    gfx_fill_rounded_rect(x, y, width, 36, 8, TM_PANEL_COLOR);

    gfx_draw_string_font(x + 16, y + 10, "W/S 选择  K 终止  R 刷新  Q/ESC 退出", TM_TEXT_DIM, 14);

    char buf[32];
    snprintf(buf, sizeof(buf), "共 %d 个进程", tm_gui.proc_count);
    gfx_draw_string_font(x + width - 120, y + 10, buf, TM_TEXT_COLOR, 14);
}

/* 主渲染函数 */
static void task_mgr_gui_render(void)
{
    /* 背景 */
    gfx_fill_rect(0, 0, gfx_width, gfx_height, TM_BG_COLOR);

    /* 标题栏 */
    gfx_fill_rect(0, 0, gfx_width, 48, TM_PANEL_COLOR);
    gfx_draw_string_font(20, 14, "赫普OS 任务管理器", TM_TEXT_COLOR, 20);
    gfx_draw_string_font(gfx_width - 100, 18, "V7.45", TM_TEXT_DIM, 14);

    /* 计算布局 */
    int margin = 20;
    int content_y = 68;
    int content_h = gfx_height - content_y - margin - 44;
    int info_h = 140;
    int list_y = content_y + info_h + 12;
    int list_h = content_h - info_h - 12 - 44;

    /* 系统信息面板 */
    draw_system_info(margin, content_y, gfx_width - 2 * margin);

    /* 进程列表 */
    draw_process_list(margin, list_y, gfx_width - 2 * margin, list_h);

    /* 底部状态栏 */
    draw_status_bar(margin, gfx_height - margin - 36, gfx_width - 2 * margin);
}

/* 终止进程 */
static int task_mgr_gui_kill(int proc_idx)
{
    extern process_t proc_table[MAX_PROCESSES];
    if (proc_idx < 0 || proc_idx >= MAX_PROCESSES) return -1;
    if (proc_table[proc_idx].state == 0) return -2;
    if (proc_table[proc_idx].pid == 0) return -3;
    if (proc_table[proc_idx].pid < 10 && proc_table[proc_idx].is_app == 0) return -4;

    proc_table[proc_idx].state = 5; /* PROC_ZOMBIE */
    return 0;
}

/* 图形模式任务管理器主循环 */
void task_manager_gui_run(void)
{
    extern void vga_gfx_init(void);
    extern void vga_gfx_text_mode(void);
    extern char keyboard_getchar(void);
    extern void gfx_flip(void);
    extern void pit_sleep(uint32_t);

    /* 初始化图形模式 */
    vga_gfx_init();

    tm_gui.visible = 1;
    tm_gui.selected_proc = 0;
    tm_gui.scroll_offset = 0;
    tm_gui.proc_count = 0;

    int running = 1;
    int auto_quit_counter = 0;  /* V7.47: 自动演示模式，100次后退出 */
    while (running) {
        /* 渲染 */
        task_mgr_gui_render();
        gfx_flip();

        /* 自动演示模式：100次渲染后自动退出 */
        auto_quit_counter++;
        if (auto_quit_counter >= 100) {
            running = 0;
            break;
        }

        /* 处理键盘输入 */
        char key = keyboard_getchar();
        if (key != 0) {
            switch (key) {
                case 27:  /* ESC - 退出 */
                case 'q':
                case 'Q':
                    running = 0;
                    break;

                case 'w':
                case 'W':
                case 72:  /* 上方向键 */
                    if (tm_gui.selected_proc > 0) {
                        tm_gui.selected_proc--;
                        if (tm_gui.selected_proc < tm_gui.scroll_offset) {
                            tm_gui.scroll_offset = tm_gui.selected_proc;
                        }
                    }
                    break;

                case 's':
                case 'S':
                case 80:  /* 下方向键 */
                    if (tm_gui.selected_proc < tm_gui.proc_count - 1) {
                        tm_gui.selected_proc++;
                        /* 自动滚动 */
                        int max_rows = (gfx_height - 300) / 24;
                        if (tm_gui.selected_proc >= tm_gui.scroll_offset + max_rows) {
                            tm_gui.scroll_offset = tm_gui.selected_proc - max_rows + 1;
                        }
                    }
                    break;

                case 'k':
                case 'K': {
                    /* 终止选中进程 */
                    extern process_t proc_table[MAX_PROCESSES];
                    int procs[64];
                    int count = 0;
                    for (int i = 0; i < MAX_PROCESSES && count < 64; i++) {
                        if (proc_table[i].state != 0) {
                            procs[count++] = i;
                        }
                    }
                    if (tm_gui.selected_proc >= 0 && tm_gui.selected_proc < count) {
                        task_mgr_gui_kill(procs[tm_gui.selected_proc]);
                    }
                    break;
                }

                case 'r':
                case 'R':
                    /* 刷新 - 重新渲染 */
                    break;
            }
        }

        /* 短暂延时，避免CPU占用过高 */
        pit_sleep(50);
    }

    /* 返回文本模式 */
    vga_gfx_text_mode();
}
