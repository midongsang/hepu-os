/* ============================================================================
 * 赫普OS - 开发者工具
 * 调试器 + 性能分析 + 日志系统 + 符号表 + 热重载 + SDK
 * ========================================================================== */

#include <stdint.h>
#include "devtools.h"
#include "vga.h"
#include "string.h"

devtools_t dt;

void devtools_init(void)
{
    memset(&dt, 0, sizeof(devtools_t));
    dt.initialized = 1;

    /* 调试器 */
    dt.debugger.initialized = 1;
    dt.debugger.state = DEBUGGER_STOPPED;

    /* 性能分析器 */
    dt.profiler.initialized = 1;
    dt.profiler.profiling = 0;
    dt.profiler.sample_interval = 1000;  /* 1ms */

    /* 日志系统 */
    dt.logger.initialized = 1;
    dt.logger.min_level = DT_LOG_DEBUG;
    dt.logger.log_to_vga = 1;

    /* 热重载 */
    dt.hot_reload.enabled = 0;
    dt.hot_reload.auto_reload = 1;

    /* SDK信息 */
    strncpy(dt.sdk.version, "0.17.0", 31);
    strncpy(dt.sdk.build_date, "2026-08-22", 31);
    strncpy(dt.sdk.build_hash, "abc123", 15);
    dt.sdk.api_level = 17;
    dt.sdk.min_api = 1;
    dt.sdk.target_api = 17;
    strncpy(dt.sdk.compiler, "GCC 12.2", 31);
    strncpy(dt.sdk.arch, "i386", 15);

    /* 添加默认符号 */
    symbol_add(0x100000, "kernel_start", 0, 'T');
    symbol_add(0x100100, "kernel_main", 0, 'T');
    symbol_add(0x101000, "vga_puts", 0, 'T');
    symbol_add(0x102000, "kmalloc", 0, 'T');
    symbol_add(0x103000, "kfree", 0, 'T');

    vga_puts("[DEV] dev tools init done \n");
    vga_printf(" SDK v%s(API %d), sym: %d \n",
               dt.sdk.version, dt.sdk.api_level, dt.symbol_count);
}

int debugger_attach(void)
{
    dt.debugger.state = DEBUGGER_PAUSED;
    vga_puts("[DEV] debugger attached \n");
    return 0;
}

int debugger_detach(void)
{
    dt.debugger.state = DEBUGGER_STOPPED;
    vga_puts("[DEV] debugger detached \n");
    return 0;
}

int debugger_pause(void)
{
    dt.debugger.state = DEBUGGER_PAUSED;
    return 0;
}

int debugger_resume(void)
{
    dt.debugger.state = DEBUGGER_RUNNING;
    return 0;
}

int debugger_step(void)
{
    dt.debugger.state = DEBUGGER_STEPPING;
    dt.debugger.step_count++;
    return 0;
}

int debugger_step_over(void)
{
    dt.debugger.step_over = 1;
    dt.debugger.state = DEBUGGER_STEPPING;
    return 0;
}

int debugger_step_out(void)
{
    dt.debugger.state = DEBUGGER_RUNNING;
    return 0;
}

int debugger_get_state(debugger_state_t *state)
{
    if (!state) return -1;
    *state = dt.debugger.state;
    return 0;
}

int debugger_get_registers(reg_state_t *regs)
{
    if (!regs) return -1;
    memcpy(regs, &dt.debugger.registers, sizeof(reg_state_t));
    return 0;
}

int debugger_set_register(int reg, uint32_t value)
{
    (void)reg; (void)value;
    return 0;
}

int debugger_read_memory(uint32_t address, void *buf, int len)
{
    if (!buf || len <= 0) return -1;
    memcpy(buf, (void *)address, len);
    return len;
}

int debugger_write_memory(uint32_t address, const void *buf, int len)
{
    if (!buf || len <= 0) return -1;
    memcpy((void *)address, buf, len);
    return len;
}

int breakpoint_add(uint32_t address, breakpoint_type_t type)
{
    for (int i = 0; i < DT_MAX_BREAKPOINTS; i++) {
        if (!dt.debugger.breakpoints[i].used) {
            memset(&dt.debugger.breakpoints[i], 0, sizeof(breakpoint_t));
            dt.debugger.breakpoints[i].used = 1;
            dt.debugger.breakpoints[i].id = i;
            dt.debugger.breakpoints[i].address = address;
            dt.debugger.breakpoints[i].type = type;
            dt.debugger.breakpoints[i].enabled = 1;
            dt.debugger.breakpoint_count++;
            vga_printf("[DEV] add breakpoint #%d @ 0x%x\n", i, address);
            return i;
        }
    }
    return -1;
}

int breakpoint_remove(int bp_id)
{
    if (bp_id < 0 || bp_id >= DT_MAX_BREAKPOINTS || !dt.debugger.breakpoints[bp_id].used) return -1;
    dt.debugger.breakpoints[bp_id].used = 0;
    dt.debugger.breakpoint_count--;
    return 0;
}

int breakpoint_enable(int bp_id, int enable)
{
    if (bp_id < 0 || bp_id >= DT_MAX_BREAKPOINTS || !dt.debugger.breakpoints[bp_id].used) return -1;
    dt.debugger.breakpoints[bp_id].enabled = enable ? 1 : 0;
    return 0;
}

int breakpoint_list(breakpoint_t *bps, int max)
{
    if (!bps || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < DT_MAX_BREAKPOINTS && count < max; i++) {
        if (dt.debugger.breakpoints[i].used) memcpy(&bps[count++], &dt.debugger.breakpoints[i], sizeof(breakpoint_t));
    }
    return count;
}

int breakpoint_set_condition(int bp_id, const char *condition)
{
    if (bp_id < 0 || bp_id >= DT_MAX_BREAKPOINTS || !dt.debugger.breakpoints[bp_id].used) return -1;
    if (condition) strncpy(dt.debugger.breakpoints[bp_id].condition, condition, 127);
    return 0;
}

int watchpoint_add(uint32_t address, int size, int read, int write)
{
    for (int i = 0; i < DT_MAX_WATCHPOINTS; i++) {
        if (!dt.debugger.watchpoints[i].used) {
            memset(&dt.debugger.watchpoints[i], 0, sizeof(watchpoint_t));
            dt.debugger.watchpoints[i].used = 1;
            dt.debugger.watchpoints[i].id = i;
            dt.debugger.watchpoints[i].address = address;
            dt.debugger.watchpoints[i].size = size;
            dt.debugger.watchpoints[i].read = read;
            dt.debugger.watchpoints[i].write = write;
            dt.debugger.watchpoints[i].enabled = 1;
            dt.debugger.watchpoint_count++;
            return i;
        }
    }
    return -1;
}

int watchpoint_remove(int wp_id)
{
    if (wp_id < 0 || wp_id >= DT_MAX_WATCHPOINTS || !dt.debugger.watchpoints[wp_id].used) return -1;
    dt.debugger.watchpoints[wp_id].used = 0;
    dt.debugger.watchpoint_count--;
    return 0;
}

int watchpoint_list(watchpoint_t *wps, int max)
{
    if (!wps || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < DT_MAX_WATCHPOINTS && count < max; i++) {
        if (dt.debugger.watchpoints[i].used) memcpy(&wps[count++], &dt.debugger.watchpoints[i], sizeof(watchpoint_t));
    }
    return count;
}

int sdk_get_info(sdk_info_t *info)
{
    if (!info) return -1;
    memcpy(info, &dt.sdk, sizeof(sdk_info_t));
    return 0;
}

int sdk_get_api_level(void) { return dt.sdk.api_level; }

int devopt_set_usb_debugging(int enable)
{
    dt.usb_debugging = enable ? 1 : 0;
    return 0;
}

int devopt_set_show_fps(int enable)
{
    dt.show_fps = enable ? 1 : 0;
    return 0;
}

int devopt_set_show_touches(int enable)
{
    dt.show_touches = enable ? 1 : 0;
    return 0;
}

int devopt_set_disable_animations(int enable)
{
    dt.disable_animations = enable ? 1 : 0;
    return 0;
}

int devopt_set_strict_mode(int enable)
{
    dt.strict_mode = enable ? 1 : 0;
    return 0;
}

int devtools_hexdump(uint32_t address, int len)
{
    uint8_t *p = (uint8_t *)address;
    for (int i = 0; i < len; i += 16) {
        vga_printf("0x%08x: ", address + i);
        for (int j = 0; j < 16 && i + j < len; j++) {
            vga_printf("%02x ", p[i + j]);
        }
        vga_printf("\n");
    }
    return 0;
}

int devtools_backtrace(uint32_t *frames, int max)
{
    if (!frames || max <= 0) return 0;
    /* 原型阶段: 返回模拟调用栈 */
    frames[0] = 0x100100;  /* kernel_main */
    frames[1] = 0x100200;  /* some_function */
    return 2;
}

int devtools_dump_registers(void)
{
    vga_printf("EAX=0x%x EBX=0x%x ECX=0x%x EDX=0x%x\n",
               dt.debugger.registers.eax, dt.debugger.registers.ebx,
               dt.debugger.registers.ecx, dt.debugger.registers.edx);
    vga_printf("ESI=0x%x EDI=0x%x EBP=0x%x ESP=0x%x\n",
               dt.debugger.registers.esi, dt.debugger.registers.edi,
               dt.debugger.registers.ebp, dt.debugger.registers.esp);
    vga_printf("EIP=0x%x EFLAGS=0x%x\n",
               dt.debugger.registers.eip, dt.debugger.registers.eflags);
    return 0;
}

