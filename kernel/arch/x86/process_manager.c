/* ============================================================================
 * 赫普OS - 进程管理器实现（V8.73新增）
 * 进程列表、CPU/内存监控、进程管理
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "../include/process_manager.h"
#include "../include/vga.h"
#include "../include/kheap.h"
#include "../include/graphics.h"

/* 全局管理器状态 */
static process_manager_t pm;

/* 外部函数 */
extern uint32_t get_total_memory_kb(void);
extern uint32_t get_used_memory_kb(void);
extern uint32_t get_free_memory_kb(void);

/* 初始化进程管理器 */
int process_manager_init(void)
{
    if (pm.initialized) return 0;

    memset(&pm, 0, sizeof(process_manager_t));
    pm.max_processes = 256;
    pm.processes = (proc_info_t *)kmalloc(pm.max_processes * sizeof(proc_info_t));
    if (!pm.processes) return -1;

    pm.selected_index = 0;
    pm.scroll_offset = 0;
    pm.sort_column = 1;  /* 默认按CPU排序 */
    pm.sort_ascending = 0;
    pm.refresh_interval_ms = 1000;
    pm.initialized = 1;

    process_manager_refresh();
    vga_puts("[PROC-MGR] Process manager initialized\n");
    return 0;
}

/* 关闭进程管理器 */
void process_manager_shutdown(void)
{
    if (!pm.initialized) return;
    if (pm.processes) {
        kfree(pm.processes);
        pm.processes = NULL;
    }
    pm.initialized = 0;
}

/* 刷新进程列表 */
int process_manager_refresh(void)
{
    if (!pm.initialized) return -1;

    /* 简化：模拟进程列表
     * 实际应从内核进程表读取真实进程信息 */
    pm.process_count = 0;

    /* 模拟系统进程 */
    const char *proc_names[] = {
        "kernel", "init", "kthreadd", "ksoftirqd", "kworker",
        "desktop", "terminal", "browser", "filemgr", "settings",
        "appstore", "media", "network", "audio", "print",
        "usb", "wifi", "bluetooth", "fsck", "syslogd"
    };

    for (int i = 0; i < 20 && pm.process_count < pm.max_processes; i++) {
        proc_info_t *p = &pm.processes[pm.process_count++];
        memset(p, 0, sizeof(proc_info_t));
        p->pid = i + 1;
        p->ppid = (i == 0) ? 0 : 1;
        strncpy(p->name, proc_names[i], 63);
        p->state = (i < 5) ? PROC_STATE_RUNNING :
                 (i < 10) ? PROC_STATE_READY : PROC_STATE_SLEEPING;
        p->priority = (i == 0) ? 0 : (i < 5) ? 50 : 100;
        p->cpu_time_ms = i * 1000 + (i * 137) % 500;
        p->memory_usage_kb = 64 + (i * 128) % 2048;
        p->threads = 1 + (i % 4);
        p->open_files = 2 + (i % 8);
        p->user_id = (i < 5) ? 0 : 1000;
        p->group_id = (i < 5) ? 0 : 1000;
        p->start_time = i * 60000;
    }

    /* 更新系统资源 */
    pm.resources.total_memory_kb = 512 * 1024;  /* 512MB */
    pm.resources.used_memory_kb = 128 * 1024;    /* 128MB */
    pm.resources.free_memory_kb = 384 * 1024;    /* 384MB */
    pm.resources.cached_memory_kb = 32 * 1024;   /* 32MB */
    pm.resources.cpu_usage_percent = 12.5f + (pm.process_count * 0.5f);
    pm.resources.cpu_cores = 4;
    pm.resources.active_processes = 5;
    pm.resources.total_processes = pm.process_count;
    pm.resources.active_threads = 12;
    pm.resources.uptime_ms = 3600000;  /* 1小时 */
    pm.resources.load_average[0] = 125;  /* 1.25 */
    pm.resources.load_average[1] = 98;   /* 0.98 */
    pm.resources.load_average[2] = 75;   /* 0.75 */

    /* 排序 */
    process_manager_sort(pm.sort_column);

    return pm.process_count;
}

/* 获取进程列表 */
int process_manager_get_processes(proc_info_t *procs, int max_count)
{
    if (!procs || max_count <= 0) return 0;
    int count = pm.process_count;
    if (count > max_count) count = max_count;
    memcpy(procs, pm.processes, count * sizeof(proc_info_t));
    return count;
}

/* 获取进程信息 */
int process_manager_get_process_info(uint32_t pid, proc_info_t *info)
{
    if (!info) return -1;
    for (int i = 0; i < pm.process_count; i++) {
        if (pm.processes[i].pid == pid) {
            memcpy(info, &pm.processes[i], sizeof(proc_info_t));
            return 0;
        }
    }
    return -2;
}

/* 获取系统资源 */
int process_manager_get_resources(system_resources_t *res)
{
    if (!res) return -1;
    memcpy(res, &pm.resources, sizeof(system_resources_t));
    return 0;
}

/* 终止进程 */
int process_manager_kill(uint32_t pid, int signal)
{
    /* 简化：模拟终止进程
     * 实际应调用内核sys_kill */
    for (int i = 0; i < pm.process_count; i++) {
        if (pm.processes[i].pid == pid) {
            pm.processes[i].state = PROC_STATE_ZOMBIE;
            vga_printf("[PROC-MGR] Killed PID %d (%s), signal %d\n",
                       pid, pm.processes[i].name, signal);
            return 0;
        }
    }
    return -1;
}

/* 暂停进程 */
int process_manager_stop(uint32_t pid)
{
    for (int i = 0; i < pm.process_count; i++) {
        if (pm.processes[i].pid == pid) {
            pm.processes[i].state = PROC_STATE_STOPPED;
            return 0;
        }
    }
    return -1;
}

/* 恢复进程 */
int process_manager_continue(uint32_t pid)
{
    for (int i = 0; i < pm.process_count; i++) {
        if (pm.processes[i].pid == pid) {
            pm.processes[i].state = PROC_STATE_READY;
            return 0;
        }
    }
    return -1;
}

/* 设置优先级 */
int process_manager_set_priority(uint32_t pid, int priority)
{
    for (int i = 0; i < pm.process_count; i++) {
        if (pm.processes[i].pid == pid) {
            pm.processes[i].priority = priority;
            return 0;
        }
    }
    return -1;
}

/* 选择进程 */
void process_manager_select(int index)
{
    if (index < 0) index = 0;
    if (index >= pm.process_count) index = pm.process_count - 1;
    pm.selected_index = index;

    /* 自动滚动 */
    if (index < pm.scroll_offset)
        pm.scroll_offset = index;
    else if (index >= pm.scroll_offset + 15)
        pm.scroll_offset = index - 14;
}

/* 滚动 */
void process_manager_scroll(int lines)
{
    int new_scroll = pm.scroll_offset + lines;
    if (new_scroll < 0) new_scroll = 0;
    if (new_scroll >= pm.process_count) new_scroll = pm.process_count - 1;
    pm.scroll_offset = new_scroll;
}

/* 排序比较函数 */
static int compare_processes(const void *a, const void *b)
{
    const proc_info_t *pa = (const proc_info_t *)a;
    const proc_info_t *pb = (const proc_info_t *)b;
    int result = 0;

    switch (pm.sort_column) {
        case 0:  /* PID */
            result = (pa->pid > pb->pid) ? 1 : (pa->pid < pb->pid) ? -1 : 0;
            break;
        case 1:  /* CPU */
            result = (pa->cpu_time_ms > pb->cpu_time_ms) ? 1 :
                     (pa->cpu_time_ms < pb->cpu_time_ms) ? -1 : 0;
            break;
        case 2:  /* 内存 */
            result = (pa->memory_usage_kb > pb->memory_usage_kb) ? 1 :
                     (pa->memory_usage_kb < pb->memory_usage_kb) ? -1 : 0;
            break;
        case 3:  /* 名称 */
            result = strcmp(pa->name, pb->name);
            break;
    }

    return pm.sort_ascending ? result : -result;
}

/* 排序 */
void process_manager_sort(int column)
{
    if (column >= 0 && column <= 3) {
        if (pm.sort_column == column)
            pm.sort_ascending = !pm.sort_ascending;
        else {
            pm.sort_column = column;
            pm.sort_ascending = 0;  /* 默认降序 */
        }
    }

    /* 简单冒泡排序 */
    for (int i = 0; i < pm.process_count - 1; i++) {
        for (int j = 0; j < pm.process_count - i - 1; j++) {
            if (compare_processes(&pm.processes[j], &pm.processes[j+1]) > 0) {
                proc_info_t tmp = pm.processes[j];
                pm.processes[j] = pm.processes[j+1];
                pm.processes[j+1] = tmp;
            }
        }
    }
}

/* 获取状态字符串 */
static const char *state_to_string(proc_state_t state)
{
    switch (state) {
        case PROC_STATE_RUNNING: return "运行";
        case PROC_STATE_READY: return "就绪";
        case PROC_STATE_BLOCKED: return "阻塞";
        case PROC_STATE_SLEEPING: return "睡眠";
        case PROC_STATE_ZOMBIE: return "僵尸";
        case PROC_STATE_STOPPED: return "停止";
        case PROC_STATE_DEAD: return "死亡";
        default: return "未知";
    }
}

/* 绘制UI */
void process_manager_draw_ui(int x, int y, int width, int height)
{
    gfx_color_t bg = {25, 25, 35, 255};
    gfx_color_t fg = {220, 220, 220, 255};
    gfx_color_t accent = {100, 180, 255, 255};
    gfx_color_t green = {100, 220, 100, 255};
    gfx_color_t yellow = {220, 200, 100, 255};
    gfx_color_t red = {220, 100, 100, 255};
    gfx_color_t gray = {120, 120, 120, 255};
    gfx_color_t selected_bg = {50, 80, 150, 255};

    /* 背景 */
    gfx_fill_rect(x, y, width, height, bg);

    /* 标题 */
    gfx_draw_string_font(x + 10, y + 8, "任务管理器", accent, 16);

    /* 系统资源概览 */
    int res_y = y + 35;
    char res_str[200];
    snprintf(res_str, sizeof(res_str),
             "CPU: %.1f%% (%d核)  内存: %d/%d MB  进程: %d/%d  运行时间: %lld:%02lld",
             pm.resources.cpu_usage_percent,
             pm.resources.cpu_cores,
             pm.resources.used_memory_kb / 1024,
             pm.resources.total_memory_kb / 1024,
             pm.resources.active_processes,
             pm.resources.total_processes,
             (long long)(pm.resources.uptime_ms / 60000),
             (long long)((pm.resources.uptime_ms / 1000) % 60));
    gfx_draw_string_font(x + 10, res_y, res_str, fg, 12);

    /* CPU使用率条 */
    int bar_y = res_y + 20;
    gfx_fill_rect(x + 10, bar_y, width - 20, 8, gray);
    int cpu_width = (int)((width - 20) * pm.resources.cpu_usage_percent / 100);
    gfx_fill_rect(x + 10, bar_y, cpu_width, 8,
                  pm.resources.cpu_usage_percent > 80 ? red :
                  pm.resources.cpu_usage_percent > 50 ? yellow : green);

    /* 内存使用率条 */
    int mem_bar_y = bar_y + 12;
    float mem_percent = (float)pm.resources.used_memory_kb /
                        pm.resources.total_memory_kb * 100;
    gfx_fill_rect(x + 10, mem_bar_y, width - 20, 8, gray);
    int mem_width = (int)((width - 20) * mem_percent / 100);
    gfx_fill_rect(x + 10, mem_bar_y, mem_width, 8,
                  mem_percent > 80 ? red : mem_percent > 50 ? yellow : green);

    /* 列标题 */
    int header_y = mem_bar_y + 20;
    gfx_draw_string_font(x + 10, header_y, "PID", accent, 11);
    gfx_draw_string_font(x + 60, header_y, "进程名", accent, 11);
    gfx_draw_string_font(x + 180, header_y, "状态", accent, 11);
    gfx_draw_string_font(x + 240, header_y, "CPU", accent, 11);
    gfx_draw_string_font(x + 300, header_y, "内存", accent, 11);
    gfx_draw_string_font(x + 370, header_y, "线程", accent, 11);
    gfx_draw_string_font(x + 420, header_y, "用户", accent, 11);

    /* 分隔线 */
    gfx_draw_line(x + 5, header_y + 14, x + width - 5, header_y + 14, gray);

    /* 进程列表 */
    int list_y = header_y + 20;
    int line_height = 16;
    int visible_lines = (height - list_y - 30) / line_height;

    for (int i = 0; i < visible_lines; i++) {
        int idx = pm.scroll_offset + i;
        if (idx >= pm.process_count) break;

        proc_info_t *p = &pm.processes[idx];
        int cur_y = list_y + i * line_height;

        /* 选中高亮 */
        if (idx == pm.selected_index) {
            gfx_fill_rect(x + 5, cur_y - 2, width - 10, line_height, selected_bg);
        }

        /* PID */
        char pid_str[16];
        snprintf(pid_str, sizeof(pid_str), "%d", p->pid);
        gfx_draw_string_font(x + 10, cur_y, pid_str, fg, 11);

        /* 进程名 */
        gfx_draw_string_font(x + 60, cur_y, p->name, fg, 11);

        /* 状态 */
        gfx_color_t state_color = (p->state == PROC_STATE_RUNNING) ? green :
                                  (p->state == PROC_STATE_SLEEPING) ? gray : yellow;
        gfx_draw_string_font(x + 180, cur_y, state_to_string(p->state),
                             state_color, 11);

        /* CPU时间 */
        char cpu_str[16];
        snprintf(cpu_str, sizeof(cpu_str), "%lldms", (long long)p->cpu_time_ms);
        gfx_draw_string_font(x + 240, cur_y, cpu_str, fg, 11);

        /* 内存 */
        char mem_str[16];
        snprintf(mem_str, sizeof(mem_str), "%dKB", p->memory_usage_kb);
        gfx_draw_string_font(x + 300, cur_y, mem_str, fg, 11);

        /* 线程 */
        char thread_str[8];
        snprintf(thread_str, sizeof(thread_str), "%d", p->threads);
        gfx_draw_string_font(x + 370, cur_y, thread_str, fg, 11);

        /* 用户 */
        char user_str[8];
        snprintf(user_str, sizeof(user_str), "%d", p->user_id);
        gfx_draw_string_font(x + 420, cur_y, user_str, fg, 11);
    }

    /* 底部状态栏 */
    int status_y = y + height - 25;
    char status[200];
    snprintf(status, sizeof(status),
             "选中: PID %d (%s)  负载: %.2f %.2f %.2f  排序: %s",
             pm.processes[pm.selected_index].pid,
             pm.processes[pm.selected_index].name,
             pm.resources.load_average[0] / 100.0f,
             pm.resources.load_average[1] / 100.0f,
             pm.resources.load_average[2] / 100.0f,
             pm.sort_column == 0 ? "PID" :
             pm.sort_column == 1 ? "CPU" :
             pm.sort_column == 2 ? "内存" : "名称");
    gfx_draw_string_font(x + 10, status_y, status, gray, 11);

    /* 快捷键提示 */
    gfx_draw_string_font(x + width - 280, status_y,
                         "↑↓选择 K终止 P优先级 R刷新 1-4排序", gray, 10);
}

/* 处理键盘 */
void process_manager_handle_key(char key)
{
    if (!pm.initialized) return;

    switch (key) {
        case 0x1B:  /* ESC */
            break;
        case 'r':
        case 'R':
            process_manager_refresh();
            break;
        case 'k':
        case 'K':
            if (pm.selected_index < pm.process_count) {
                process_manager_kill(pm.processes[pm.selected_index].pid, 9);
                process_manager_refresh();
            }
            break;
        case 'p':
        case 'P':
            /* 简化：循环优先级 */
            if (pm.selected_index < pm.process_count) {
                uint32_t pid = pm.processes[pm.selected_index].pid;
                int new_prio = (pm.processes[pm.selected_index].priority + 10) % 140;
                process_manager_set_priority(pid, new_prio);
            }
            break;
        case '1':
            process_manager_sort(0);
            break;
        case '2':
            process_manager_sort(1);
            break;
        case '3':
            process_manager_sort(2);
            break;
        case '4':
            process_manager_sort(3);
            break;
    }
}

/* 获取状态 */
int process_manager_get_state(process_manager_t *state)
{
    if (!state) return -1;
    memcpy(state, &pm, sizeof(process_manager_t));
    return 0;
}
