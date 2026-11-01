/* ============================================================================
 * 赫普OS - .hug 调试器与模拟器
 * ========================================================================== */

#include <stdint.h>
#include "hug_debugger.h"
#include "vga.h"
#include "string.h"

static hug_debugger_t hdbg;

void hug_debugger_init(void)
{
    memset(&hdbg, 0, sizeof(hug_debugger_t));
    hdbg.initialized = 1;
    hdbg.enabled = 1;
    hdbg.state = HDBG_STATE_IDLE;
    hdbg.attached_pid = -1;
    hdbg.current_thread = -1;

    /* 默认模拟器配置 */
    hdbg.emulator.screen_width = 1080;
    hdbg.emulator.screen_height = 2400;
    hdbg.emulator.screen_dpi = 440;
    hdbg.emulator.ram_size_mb = 4096;
    hdbg.emulator.storage_size_mb = 65536;
    hdbg.emulator.cpu_cores = 8;
    hdbg.emulator.cpu_speed_mhz = 2800;
    hdbg.emulator.network_speed = 100000;
    hdbg.emulator.battery_level = 100;
    hdbg.emulator.gps_enabled = 1;
    hdbg.emulator.camera_enabled = 1;
    hdbg.emulator.sensors_enabled = 1;
    strncpy(hdbg.emulator.device_model, "HEPU Emulator", 63);
    strncpy(hdbg.emulator.os_version, "1.0", 31);

    vga_puts("[HDBG] debugger init done \n");
    vga_printf(" breakpoints: %d, watchpoints: %d, simulator: %dx%d\n",
               HDBG_MAX_BREAKPOINTS, HDBG_MAX_WATCHPOINTS,
               hdbg.emulator.screen_width, hdbg.emulator.screen_height);
}

/* 调试控制 */
int hdbg_attach(int pid)
{
    hdbg.attached_pid = pid;
    hdbg.state = HDBG_STATE_PAUSED;
    hdbg.total_sessions++;
    return 0;
}

int hdbg_detach(void)
{
    hdbg.attached_pid = -1;
    hdbg.state = HDBG_STATE_IDLE;
    return 0;
}

int hdbg_run(void)
{
    if (hdbg.attached_pid < 0) return -1;
    hdbg.state = HDBG_STATE_RUNNING;
    return 0;
}

int hdbg_pause(void)
{
    if (hdbg.state != HDBG_STATE_RUNNING) return -1;
    hdbg.state = HDBG_STATE_PAUSED;
    return 0;
}

int hdbg_step(void)
{
    if (hdbg.state == HDBG_STATE_IDLE) return -1;
    hdbg.state = HDBG_STATE_STEPPING;
    hdbg.total_steps++;
    return 0;
}

int hdbg_step_over(void)
{
    return hdbg_step();
}

int hdbg_step_out(void)
{
    return hdbg_step();
}

int hdbg_continue(void)
{
    if (hdbg.state == HDBG_STATE_IDLE) return -1;
    hdbg.state = HDBG_STATE_RUNNING;
    return 0;
}

int hdbg_stop(void)
{
    hdbg.state = HDBG_STATE_TERMINATED;
    return 0;
}

/* 断点 */
int hdbg_breakpoint_add(uint32_t address, const char *condition)
{
    for (int i = 0; i < HDBG_MAX_BREAKPOINTS; i++) {
        if (!hdbg.breakpoints[i].used) {
            memset(&hdbg.breakpoints[i], 0, sizeof(hdbg_breakpoint_t));
            hdbg.breakpoints[i].used = 1;
            hdbg.breakpoints[i].id = i;
            hdbg.breakpoints[i].type = HDBG_BP_SOFTWARE;
            hdbg.breakpoints[i].address = address;
            hdbg.breakpoints[i].enabled = 1;
            if (condition) strncpy(hdbg.breakpoints[i].condition, condition, 255);
            hdbg.breakpoint_count++;
            return i;
        }
    }
    return -1;
}

int hdbg_breakpoint_add_symbol(const char *symbol, const char *condition)
{
    (void)symbol;
    return hdbg_breakpoint_add(0, condition);
}

int hdbg_breakpoint_remove(int id)
{
    if (id < 0 || id >= HDBG_MAX_BREAKPOINTS || !hdbg.breakpoints[id].used) return -1;
    hdbg.breakpoints[id].used = 0;
    hdbg.breakpoint_count--;
    return 0;
}

int hdbg_breakpoint_enable(int id, int enable)
{
    if (id < 0 || id >= HDBG_MAX_BREAKPOINTS || !hdbg.breakpoints[id].used) return -1;
    hdbg.breakpoints[id].enabled = enable ? 1 : 0;
    return 0;
}

int hdbg_breakpoint_get(int id, hdbg_breakpoint_t *bp)
{
    if (id < 0 || id >= HDBG_MAX_BREAKPOINTS || !hdbg.breakpoints[id].used || !bp) return -1;
    memcpy(bp, &hdbg.breakpoints[id], sizeof(hdbg_breakpoint_t));
    return 0;
}

int hdbg_breakpoint_list(hdbg_breakpoint_t *bps, int max)
{
    if (!bps || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < HDBG_MAX_BREAKPOINTS && count < max; i++) {
        if (hdbg.breakpoints[i].used) memcpy(&bps[count++], &hdbg.breakpoints[i], sizeof(hdbg_breakpoint_t));
    }
    return count;
}

/* 观察点 */
int hdbg_watchpoint_add(uint32_t address, int size, int type)
{
    for (int i = 0; i < HDBG_MAX_WATCHPOINTS; i++) {
        if (!hdbg.watchpoints[i].used) {
            memset(&hdbg.watchpoints[i], 0, sizeof(hdbg_watchpoint_t));
            hdbg.watchpoints[i].used = 1;
            hdbg.watchpoints[i].id = i;
            hdbg.watchpoints[i].address = address;
            hdbg.watchpoints[i].size = size;
            hdbg.watchpoints[i].type = type;
            hdbg.watchpoints[i].enabled = 1;
            hdbg.watchpoint_count++;
            return i;
        }
    }
    return -1;
}

int hdbg_watchpoint_remove(int id)
{
    if (id < 0 || id >= HDBG_MAX_WATCHPOINTS || !hdbg.watchpoints[id].used) return -1;
    hdbg.watchpoints[id].used = 0;
    hdbg.watchpoint_count--;
    return 0;
}

int hdbg_watchpoint_list(hdbg_watchpoint_t *wps, int max)
{
    if (!wps || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < HDBG_MAX_WATCHPOINTS && count < max; i++) {
        if (hdbg.watchpoints[i].used) memcpy(&wps[count++], &hdbg.watchpoints[i], sizeof(hdbg_watchpoint_t));
    }
    return count;
}

/* 内存/寄存器 */
int hdbg_read_memory(uint32_t address, void *buf, int size)
{
    (void)address;
    if (!buf || size <= 0) return -1;
    memset(buf, 0, size);
    return size;
}

int hdbg_write_memory(uint32_t address, const void *buf, int size)
{
    (void)address; (void)buf;
    return size;
}

int hdbg_read_registers(hdbg_registers_t *regs)
{
    if (!regs) return -1;
    memcpy(regs, &hdbg.registers, sizeof(hdbg_registers_t));
    return 0;
}

int hdbg_write_register(int reg, uint32_t value)
{
    switch (reg) {
        case 0: hdbg.registers.eax = value; break;
        case 1: hdbg.registers.ebx = value; break;
        case 2: hdbg.registers.ecx = value; break;
        case 3: hdbg.registers.edx = value; break;
        case 4: hdbg.registers.esi = value; break;
        case 5: hdbg.registers.edi = value; break;
        case 6: hdbg.registers.ebp = value; break;
        case 7: hdbg.registers.esp = value; break;
        case 8: hdbg.registers.eip = value; break;
        default: return -1;
    }
    return 0;
}

int hdbg_disassemble(uint32_t address, int count, char *output, int max)
{
    (void)address;
    if (!output || max <= 0) return -1;
    int len = 0;
    for (int i = 0; i < count && len < max - 32; i++) {
        uint32_t addr = address + i * 4;
        const char *hex = "0123456789ABCDEF";
        output[len++] = '0'; output[len++] = 'x';
        for (int b = 28; b >= 0; b -= 4) output[len++] = hex[(addr >> b) & 0xF];
        output[len++] = ':'; output[len++] = ' ';
        output[len++] = 'n'; output[len++] = 'o'; output[len++] = 'p';
        output[len++] = '\n';
    }
    output[len] = 0;
    return count;
}

/* 调用栈/线程 */
int hdbg_get_callstack(hdbg_stack_frame_t *frames, int max)
{
    if (!frames || max <= 0) return 0;
    int count = hdbg.callstack_depth < max ? hdbg.callstack_depth : max;
    for (int i = 0; i < count; i++)
        memcpy(&frames[i], &hdbg.callstack[i], sizeof(hdbg_stack_frame_t));
    return count;
}

int hdbg_list_threads(hdbg_thread_t *threads, int max)
{
    if (!threads || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < HDBG_MAX_THREADS && count < max; i++) {
        if (hdbg.threads[i].used) memcpy(&threads[count++], &hdbg.threads[i], sizeof(hdbg_thread_t));
    }
    return count;
}

int hdbg_switch_thread(int id)
{
    if (id < 0 || id >= HDBG_MAX_THREADS || !hdbg.threads[id].used) return -1;
    hdbg.current_thread = id;
    return 0;
}

/* 日志 */
int hdbg_log(int level, const char *tag, const char *message)
{
    int idx = hdbg.log_pos % HDBG_MAX_LOGS;
    hdbg.logs[idx].timestamp = 1;
    hdbg.logs[idx].level = level;
    if (tag) strncpy(hdbg.logs[idx].tag, tag, 63);
    if (message) strncpy(hdbg.logs[idx].message, message, 511);
    hdbg.log_pos++;
    if (hdbg.log_count < HDBG_MAX_LOGS) hdbg.log_count++;
    return 0;
}

int hdbg_get_logs(hdbg_log_entry_t *logs, int max)
{
    if (!logs || max <= 0) return 0;
    int count = hdbg.log_count < max ? hdbg.log_count : max;
    for (int i = 0; i < count; i++) {
        int idx = (hdbg.log_pos - count + i) % HDBG_MAX_LOGS;
        memcpy(&logs[i], &hdbg.logs[idx], sizeof(hdbg_log_entry_t));
    }
    return count;
}

int hdbg_clear_logs(void)
{
    hdbg.log_count = 0;
    hdbg.log_pos = 0;
    return 0;
}

/* 性能分析 */
int hdbg_perf_start(void)
{
    hdbg.start_time = 1;
    memset(&hdbg.perf, 0, sizeof(hdbg_perf_counters_t));
    return 0;
}

int hdbg_perf_stop(void)
{
    return 0;
}

int hdbg_perf_get(hdbg_perf_counters_t *counters)
{
    if (!counters) return -1;
    memcpy(counters, &hdbg.perf, sizeof(hdbg_perf_counters_t));
    return 0;
}

int hdbg_perf_reset(void)
{
    memset(&hdbg.perf, 0, sizeof(hdbg_perf_counters_t));
    return 0;
}

/* 模拟器 */
int hdbg_emulator_start(const hdbg_emulator_config_t *config)
{
    if (config) memcpy(&hdbg.emulator, config, sizeof(hdbg_emulator_config_t));
    hdbg.emulator.enabled = 1;
    hdbg_log(1, "Emulator", " simulator start ");
    return 0;
}

int hdbg_emulator_stop(void)
{
    hdbg.emulator.enabled = 0;
    return 0;
}

int hdbg_emulator_get_config(hdbg_emulator_config_t *config)
{
    if (!config) return -1;
    memcpy(config, &hdbg.emulator, sizeof(hdbg_emulator_config_t));
    return 0;
}

int hdbg_emulator_set_property(const char *key, const char *value)
{
    (void)key; (void)value;
    return 0;
}

int hdbg_emulator_send_key(int keycode)
{
    (void)keycode;
    return 0;
}

int hdbg_emulator_touch(int x, int y, int action)
{
    (void)x; (void)y; (void)action;
    return 0;
}

/* 状态查询 */
int hdbg_get_state(hdbg_state_t *state)
{
    if (!state) return -1;
    *state = hdbg.state;
    return 0;
}

int hdbg_get_stats(hug_debugger_t *stats)
{
    if (!stats) return -1;
    memcpy(stats, &hdbg, sizeof(hug_debugger_t));
    return 0;
}
