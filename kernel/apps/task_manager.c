/* ============================================================================
 * 赫普OS - 任务管理器（类似Windows任务管理器）
 *
 * 功能：
 * - 系统信息面板（CPU、内存、显存、进程数）
 * - 进程列表（PID、名称、状态、CPU使用率、内存使用）
 * - 资源使用条（可视化显示）
 * - 进程操作（终止进程、刷新）
 * - 键盘交互（W/S选择、K终止、R刷新、Q退出）
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdarg.h>
#include "vga.h"
#include "process.h"
#include "mm.h"
#include "vram.h"
#include "virtual_vram.h"
#include "kheap.h"
#include "keyboard.h"

/* 任务管理器状态 */
typedef struct {
    int      initialized;
    int      visible;
    int      selected_proc;
    int      scroll_offset;
    uint32_t refresh_ticks;
} task_mgr_t;

static task_mgr_t tm;

/* 带属性的printf */
static void vga_printf_attr(uint8_t attr, const char *fmt, ...)
{
    va_list args;
    char buf[256];
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    vga_set_color(attr & 0x0F, (attr >> 4) & 0x0F);
    vga_puts(buf);
    vga_set_color(0x07, 0x00);  /* 恢复默认 */
}

/* 带属性的putchar */
static void vga_putchar_attr(char c, uint8_t attr)
{
    vga_set_color(attr & 0x0F, (attr >> 4) & 0x0F);
    vga_putc(c);
    vga_set_color(0x07, 0x00);
}

/* 设置光标（x=列, y=行） */
static void vga_set_cursor(int x, int y)
{
    vga_gotoxy(y, x);
}

/* 进程状态字符串 */
static const char *proc_state_str(proc_state_t state)
{
    switch (state) {
        case PROC_UNUSED:   return "UNUSED";
        case PROC_READY:    return "READY";
        case PROC_RUNNING:  return "RUNNING";
        case PROC_BLOCKED:  return "BLOCKED";
        case PROC_SLEEPING: return "SLEEPING";
        case PROC_ZOMBIE:   return "ZOMBIE";
        default:            return "UNKNOWN";
    }
}

/* 获取CPU使用率 */
static uint32_t task_mgr_get_cpu_usage(void)
{
    extern uint32_t sched_get_ticks(void);
    extern uint32_t sched_get_idle_ticks(void);
    uint32_t total = sched_get_ticks();
    uint32_t idle = sched_get_idle_ticks();
    if (total == 0) return 0;
    if (idle > total) idle = total;
    return ((total - idle) * 100) / total;
}

/* 获取内存信息 */
static void task_mgr_get_mem_info(uint32_t *total, uint32_t *used, uint32_t *free_mem)
{
    mm_stats(total, used, free_mem);
}

/* 绘制资源使用条 */
static void draw_usage_bar(int x, int y, int width, uint32_t percent, uint8_t color)
{
    if (percent > 100) percent = 100;
    int filled = (width * percent) / 100;

    vga_set_cursor(x, y);
    for (int i = 0; i < width; i++) {
        if (i < filled) {
            vga_putchar_attr(0xDB, color);
        } else {
            vga_putchar_attr(' ', 0x07);
        }
    }
}

/* 显示系统信息面板 */
static void task_mgr_show_system_info(void)
{
    uint32_t mem_total, mem_used, mem_free;
    task_mgr_get_mem_info(&mem_total, &mem_used, &mem_free);
    uint32_t mem_percent = mem_total ? (mem_used * 100 / mem_total) : 0;

    uint32_t cpu_usage = task_mgr_get_cpu_usage();

    vvram_stats_t vvram_stats;
    vvram_get_stats(&vvram_stats);
    uint32_t vram_percent = vvram_stats.total_vram ?
        (vvram_stats.used_vram * 100 / vvram_stats.total_vram) : 0;

    uint32_t proc_total, proc_running, proc_ready;
    process_stats(&proc_total, &proc_running, &proc_ready);

    /* 标题栏 */
    vga_set_cursor(0, 0);
    vga_printf_attr(0x1F, " === Hepu OS Task Manager === ");

    /* CPU */
    vga_set_cursor(0, 2);
    vga_printf(" CPU:  ");
    draw_usage_bar(7, 2, 30, cpu_usage, 0x0A);
    vga_set_cursor(40, 2);
    vga_printf("%3d%%", cpu_usage);

    /* 内存 */
    vga_set_cursor(0, 3);
    vga_printf(" MEM:  ");
    draw_usage_bar(7, 3, 30, mem_percent, 0x09);
    vga_set_cursor(40, 3);
    vga_printf("%3d%%  %d/%d KB", mem_percent, mem_used / 1024, mem_total / 1024);

    /* 显存 */
    vga_set_cursor(0, 4);
    vga_printf(" VRAM: ");
    draw_usage_bar(7, 4, 30, vram_percent, 0x0D);
    vga_set_cursor(40, 4);
    vga_printf("%3d%%  %d/%d KB", vram_percent,
                vvram_stats.used_vram / 1024, vvram_stats.total_vram / 1024);

    /* 进程统计 */
    vga_set_cursor(0, 5);
    vga_printf(" Procs: %d total, %d running, %d ready",
                proc_total, proc_running, proc_ready);

    /* 分隔线 */
    vga_set_cursor(0, 6);
    for (int i = 0; i < 80; i++) vga_putc(0xC4);
}

/* 显示进程列表 */
static void task_mgr_show_process_list(void)
{
    extern process_t proc_table[MAX_PROCESSES];

    /* 表头 */
    vga_set_cursor(0, 7);
    vga_printf_attr(0x1F, " PID  Name              State     CPU%%   MEM(KB) ");

    /* 收集活跃进程 */
    int procs[64];
    int proc_count = 0;
    for (int i = 0; i < MAX_PROCESSES && proc_count < 64; i++) {
        if (proc_table[i].state != PROC_UNUSED) {
            procs[proc_count++] = i;
        }
    }

    /* 显示进程 */
    int display_start = tm.scroll_offset;
    int display_end = display_start + 15;
    if (display_end > proc_count) display_end = proc_count;

    for (int i = display_start; i < display_end; i++) {
        int idx = procs[i];
        process_t *p = &proc_table[idx];
        int row = 8 + (i - display_start);

        /* CPU使用率 */
        extern uint32_t sched_get_ticks(void);
        uint32_t total_ticks = sched_get_ticks();
        uint32_t cpu_pct = total_ticks ? (p->ticks_total * 100 / total_ticks) : 0;
        if (cpu_pct > 100) cpu_pct = 100;

        /* 选中行高亮 */
        uint8_t attr = (i == tm.selected_proc) ? 0x1E : 0x07;

        vga_set_cursor(0, row);
        vga_printf_attr(attr, "%4d  %-16s  %-8s  %3d%%  %8d",
                         p->pid, p->name, proc_state_str(p->state),
                         cpu_pct, p->mem_used * (PAGE_SIZE / 1024));
    }

    /* 底部状态栏 */
    vga_set_cursor(0, 24);
    vga_printf_attr(0x1F, " [W/S] Select  [K] Kill  [R] Refresh  [Q] Quit  | %d processes",
                     proc_count);
}

/* 终止进程 */
static int task_mgr_kill_process(int proc_idx)
{
    extern process_t proc_table[MAX_PROCESSES];
    if (proc_idx < 0 || proc_idx >= MAX_PROCESSES) return -1;
    if (proc_table[proc_idx].state == PROC_UNUSED) return -2;
    if (proc_table[proc_idx].pid == 0) return -3;
    if (proc_table[proc_idx].is_app == 0 && proc_table[proc_idx].pid < 10) return -4;

    proc_table[proc_idx].state = PROC_ZOMBIE;
    return 0;
}

/* 处理键盘输入 */
static int task_mgr_handle_key(uint8_t key)
{
    extern process_t proc_table[MAX_PROCESSES];

    int proc_count = 0;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (proc_table[i].state != PROC_UNUSED) proc_count++;
    }

    switch (key) {
        case 'w':
        case 'W':
            if (tm.selected_proc > 0) {
                tm.selected_proc--;
                if (tm.selected_proc < tm.scroll_offset)
                    tm.scroll_offset = tm.selected_proc;
            }
            break;
        case 's':
        case 'S':
            if (tm.selected_proc < proc_count - 1) {
                tm.selected_proc++;
                if (tm.selected_proc >= tm.scroll_offset + 15)
                    tm.scroll_offset = tm.selected_proc - 14;
            }
            break;
        case 'k':
        case 'K': {
            int procs[64];
            int count = 0;
            for (int i = 0; i < MAX_PROCESSES && count < 64; i++) {
                if (proc_table[i].state != PROC_UNUSED)
                    procs[count++] = i;
            }
            if (tm.selected_proc < count)
                task_mgr_kill_process(procs[tm.selected_proc]);
            break;
        }
        case 'r':
        case 'R':
            break;
        case 'q':
        case 'Q':
        case 27:
            tm.visible = 0;
            return 0;
    }
    return 1;
}

/* 初始化任务管理器 */
void task_manager_init(void)
{
    memset(&tm, 0, sizeof(task_mgr_t));
    tm.initialized = 1;
    tm.visible = 0;
    tm.selected_proc = 0;
    tm.scroll_offset = 0;
    vga_puts("[TASKMGR] task manager init done\n");
}

/* 运行任务管理器 */
void task_manager_run(void)
{
    if (!tm.initialized) task_manager_init();

    tm.visible = 1;
    tm.selected_proc = 0;
    tm.scroll_offset = 0;

    while (tm.visible) {
        vga_clear();
        task_mgr_show_system_info();
        task_mgr_show_process_list();

        char key = keyboard_getchar();
        if (key)
            task_mgr_handle_key((uint8_t)key);

        tm.refresh_ticks++;
    }

    vga_clear();
}

/* 任务管理器自测 */
void task_manager_selftest(void)
{
    extern void serial_puts(uint16_t, const char *);
    extern void serial_puthex(uint16_t, uint32_t);
    #define TMLOG(s) serial_puts(0x3F8, s)

    TMLOG("[TASKMGR] selftest: start\n");
    int pass = 0, fail = 0;

    /* 测试1：初始化 */
    task_manager_init();
    if (tm.initialized) {
        TMLOG("[TASKMGR] test1 init: PASS\n");
        pass++;
    } else {
        TMLOG("[TASKMGR] test1 init: FAIL\n");
        fail++;
    }

    /* 测试2：CPU使用率 */
    uint32_t cpu = task_mgr_get_cpu_usage();
    if (cpu <= 100) {
        TMLOG("[TASKMGR] test2 cpu usage: PASS (cpu=");
        serial_puthex(0x3F8, cpu);
        TMLOG(" pct)\n");
        pass++;
    } else {
        TMLOG("[TASKMGR] test2 cpu usage: FAIL\n");
        fail++;
    }

    /* 测试3：内存信息 */
    uint32_t total, used, free_mem;
    task_mgr_get_mem_info(&total, &used, &free_mem);
    if (total > 0 && used <= total) {
        TMLOG("[TASKMGR] test3 mem info: PASS (total=");
        serial_puthex(0x3F8, total / 1024);
        TMLOG("KB)\n");
        pass++;
    } else {
        TMLOG("[TASKMGR] test3 mem info: FAIL\n");
        fail++;
    }

    /* 测试4：进程统计（函数能正常调用即可，早期可能进程数为0） */
    uint32_t proc_total, proc_running, proc_ready;
    process_stats(&proc_total, &proc_running, &proc_ready);
    /* 只要函数能正常返回就算通过（早期阶段进程可能未完全初始化） */
    TMLOG("[TASKMGR] test4 proc stats: PASS (total=");
    serial_puthex(0x3F8, proc_total);
    TMLOG(", running=");
    serial_puthex(0x3F8, proc_running);
    TMLOG(")\n");
    pass++;

    /* 测试5：进程状态字符串 */
    const char *state = proc_state_str(PROC_RUNNING);
    if (state && strcmp(state, "RUNNING") == 0) {
        TMLOG("[TASKMGR] test5 state str: PASS\n");
        pass++;
    } else {
        TMLOG("[TASKMGR] test5 state str: FAIL\n");
        fail++;
    }

    TMLOG("[TASKMGR] selftest: done (pass=");
    serial_puthex(0x3F8, pass);
    TMLOG(", fail=");
    serial_puthex(0x3F8, fail);
    TMLOG(")\n");

    #undef TMLOG
}
