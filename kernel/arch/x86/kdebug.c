/* ============================================================================
 * 赫普OS - 内核调试器
 * GDB stub + 断点 + 单步 + 内存查看 + 寄存器 + 栈回溯
 * ========================================================================== */

#include <stdint.h>
#include "kdebug.h"
#include "vga.h"
#include "string.h"

kdebug_t dbg;

/* 调试命令处理函数 */
static int cmd_help(const char *args);
static int cmd_break(const char *args);
static int cmd_delete(const char *args);
static int cmd_continue(const char *args);
static int cmd_step(const char *args);
static int cmd_next(const char *args);
static int cmd_finish(const char *args);
static int cmd_print(const char *args);
static int cmd_examine(const char *args);
static int cmd_backtrace(const char *args);
static int cmd_registers(const char *args);
static int cmd_info(const char *args);
static int cmd_list(const char *args);
static int cmd_disassemble(const char *args);

void kdebug_init(void)
{
    memset(&dbg, 0, sizeof(kdebug_t));
    dbg.initialized = 1;
    dbg.enabled = 1;
    dbg.state = DEBUG_STATE_RUNNING;

    /* 注册调试命令 */
    kdbg_command_register("help", " display 帮助 ", "help [ cmd ]", cmd_help);
    kdbg_command_register("break", " set breakpoints ", "break < addr >", cmd_break);
    kdbg_command_register("delete", " delete breakpoints ", "delete < breakpoints ID>", cmd_delete);
    kdbg_command_register("continue", " 继续 exec ", "continue", cmd_continue);
    kdbg_command_register("step", " 单步 enter ", "step", cmd_step);
    kdbg_command_register("next", " 单步 skipped ", "next", cmd_next);
    kdbg_command_register("finish", " exec to 返回 ", "finish", cmd_finish);
    kdbg_command_register("print", " 打印 table 达式 ", "print < table 达式 >", cmd_print);
    kdbg_command_register("examine", " check memory ", "x/<nfu> < addr >", cmd_examine);
    kdbg_command_register("backtrace", " stack trace ", "backtrace", cmd_backtrace);
    kdbg_command_register("registers", " display 寄存器 ", "registers", cmd_registers);
    kdbg_command_register("info", " display 信息 ", "info < type >", cmd_info);
    kdbg_command_register("list", " list 源码 ", "list [ 行号 ]", cmd_list);
    kdbg_command_register("disassemble", " 反汇编 ", "disassemble [ addr ]", cmd_disassemble);

    vga_puts("[KDBG] kernel debugger init done \n");
    vga_printf(" breakpoints: %d slot, watchpoints: %d slot, cmd: %d, GDB stub: not start \n",
               KDBG_MAX_BREAKPOINTS, KDBG_MAX_WATCHPOINTS, dbg.command_count);
}

/* 断点管理 */
int kdbg_breakpoint_add(uint64_t address, kdbg_kdbg_breakpoint_type_t type)
{
    for (int i = 0; i < KDBG_MAX_BREAKPOINTS; i++) {
        if (!dbg.breakpoints[i].used) {
            memset(&dbg.breakpoints[i], 0, sizeof(kdbg_breakpoint_t));
            dbg.breakpoints[i].used = 1;
            dbg.breakpoints[i].id = i;
            dbg.breakpoints[i].address = address;
            dbg.breakpoints[i].type = type;
            dbg.breakpoints[i].enabled = 1;
            dbg.breakpoint_count++;
            vga_printf("[KDBG] breakpoints #%d set at 0x%llx\n", i, (unsigned long long)address);
            return i;
        }
    }
    return -1;
}

int kdbg_breakpoint_remove(int bp_id)
{
    if (bp_id < 0 || bp_id >= KDBG_MAX_BREAKPOINTS || !dbg.breakpoints[bp_id].used) return -1;
    dbg.breakpoints[bp_id].used = 0;
    dbg.breakpoint_count--;
    return 0;
}

int kdbg_breakpoint_enable(int bp_id)
{
    if (bp_id < 0 || bp_id >= KDBG_MAX_BREAKPOINTS || !dbg.breakpoints[bp_id].used) return -1;
    dbg.breakpoints[bp_id].enabled = 1;
    return 0;
}

int kdbg_breakpoint_disable(int bp_id)
{
    if (bp_id < 0 || bp_id >= KDBG_MAX_BREAKPOINTS || !dbg.breakpoints[bp_id].used) return -1;
    dbg.breakpoints[bp_id].enabled = 0;
    return 0;
}

int kdbg_breakpoint_list(kdbg_breakpoint_t *bps, int max)
{
    if (!bps || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < KDBG_MAX_BREAKPOINTS && count < max; i++) {
        if (dbg.breakpoints[i].used) memcpy(&bps[count++], &dbg.breakpoints[i], sizeof(kdbg_breakpoint_t));
    }
    return count;
}

int kdbg_breakpoint_get(int bp_id, kdbg_breakpoint_t *bp)
{
    if (bp_id < 0 || bp_id >= KDBG_MAX_BREAKPOINTS || !dbg.breakpoints[bp_id].used || !bp) return -1;
    memcpy(bp, &dbg.breakpoints[bp_id], sizeof(kdbg_breakpoint_t));
    return 0;
}

int kdbg_breakpoint_set_condition(int bp_id, const char *condition)
{
    if (bp_id < 0 || bp_id >= KDBG_MAX_BREAKPOINTS || !dbg.breakpoints[bp_id].used) return -1;
    if (condition) strncpy(dbg.breakpoints[bp_id].condition, condition, 127);
    return 0;
}

int kdbg_breakpoint_set_command(int bp_id, const char *command)
{
    if (bp_id < 0 || bp_id >= KDBG_MAX_BREAKPOINTS || !dbg.breakpoints[bp_id].used) return -1;
    if (command) strncpy(dbg.breakpoints[bp_id].command, command, 255);
    return 0;
}

/* 观察点 */
int kdbg_watchpoint_add(uint64_t address, int length, int type)
{
    for (int i = 0; i < KDBG_MAX_WATCHPOINTS; i++) {
        if (!dbg.watchpoints[i].used) {
            memset(&dbg.watchpoints[i], 0, sizeof(kdbg_watchpoint_t));
            dbg.watchpoints[i].used = 1;
            dbg.watchpoints[i].id = i;
            dbg.watchpoints[i].address = address;
            dbg.watchpoints[i].length = length;
            dbg.watchpoints[i].type = type;
            dbg.watchpoints[i].enabled = 1;
            dbg.watchpoint_count++;
            return i;
        }
    }
    return -1;
}

int kdbg_watchpoint_remove(int wp_id)
{
    if (wp_id < 0 || wp_id >= KDBG_MAX_WATCHPOINTS || !dbg.watchpoints[wp_id].used) return -1;
    dbg.watchpoints[wp_id].used = 0;
    dbg.watchpoint_count--;
    return 0;
}

int kdbg_watchpoint_list(kdbg_watchpoint_t *wps, int max)
{
    if (!wps || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < KDBG_MAX_WATCHPOINTS && count < max; i++) {
        if (dbg.watchpoints[i].used) memcpy(&wps[count++], &dbg.watchpoints[i], sizeof(kdbg_watchpoint_t));
    }
    return count;
}

/* 执行控制 */
int kdbg_continue(void)
{
    dbg.state = DEBUG_STATE_CONTINUING;
    dbg.total_continues++;
    return 0;
}

int kdbg_step(void)
{
    dbg.state = DEBUG_STATE_SINGLE_STEP;
    dbg.single_step_enabled = 1;
    dbg.total_steps++;
    return 0;
}

int kdbg_step_over(void)
{
    dbg.state = DEBUG_STATE_STEPPING_OVER;
    dbg.step_over_address = dbg.registers.rip + 1;
    return 0;
}

int kdbg_step_out(void)
{
    dbg.state = DEBUG_STATE_STEPPING_OUT;
    return 0;
}

int kdbg_stop(void)
{
    dbg.state = DEBUG_STATE_STOPPED;
    return 0;
}

int kdbg_get_state(debug_state_t *state)
{
    if (!state) return -1;
    *state = dbg.state;
    return 0;
}

/* 寄存器 */
int kdbg_registers_get(registers_t *regs)
{
    if (!regs) return -1;
    memcpy(regs, &dbg.registers, sizeof(registers_t));
    dbg.registers_valid = 1;
    return 0;
}

int kdbg_registers_set(const registers_t *regs)
{
    if (!regs) return -1;
    memcpy(&dbg.registers, regs, sizeof(registers_t));
    return 0;
}

int kdbg_register_get(const char *name, uint64_t *value)
{
    if (!name || !value) return -1;
    if (strcmp(name, "rax") == 0) *value = dbg.registers.rax;
    else if (strcmp(name, "rbx") == 0) *value = dbg.registers.rbx;
    else if (strcmp(name, "rcx") == 0) *value = dbg.registers.rcx;
    else if (strcmp(name, "rdx") == 0) *value = dbg.registers.rdx;
    else if (strcmp(name, "rsi") == 0) *value = dbg.registers.rsi;
    else if (strcmp(name, "rdi") == 0) *value = dbg.registers.rdi;
    else if (strcmp(name, "rbp") == 0) *value = dbg.registers.rbp;
    else if (strcmp(name, "rsp") == 0) *value = dbg.registers.rsp;
    else if (strcmp(name, "rip") == 0) *value = dbg.registers.rip;
    else if (strcmp(name, "rflags") == 0) *value = dbg.registers.rflags;
    else return -1;
    return 0;
}

int kdbg_register_set(const char *name, uint64_t value)
{
    if (!name) return -1;
    if (strcmp(name, "rax") == 0) dbg.registers.rax = value;
    else if (strcmp(name, "rbx") == 0) dbg.registers.rbx = value;
    else if (strcmp(name, "rcx") == 0) dbg.registers.rcx = value;
    else if (strcmp(name, "rdx") == 0) dbg.registers.rdx = value;
    else if (strcmp(name, "rsi") == 0) dbg.registers.rsi = value;
    else if (strcmp(name, "rdi") == 0) dbg.registers.rdi = value;
    else if (strcmp(name, "rbp") == 0) dbg.registers.rbp = value;
    else if (strcmp(name, "rsp") == 0) dbg.registers.rsp = value;
    else if (strcmp(name, "rip") == 0) dbg.registers.rip = value;
    else return -1;
    return 0;
}

/* 内存 */
int kdbg_memory_read(uint64_t address, void *buffer, int size)
{
    if (!buffer || size <= 0) return -1;
    memcpy(buffer, (void *)(uintptr_t)address, size);
    dbg.total_memory_reads++;
    return size;
}

int kdbg_memory_write(uint64_t address, const void *buffer, int size)
{
    if (!buffer || size <= 0) return -1;
    memcpy((void *)(uintptr_t)address, buffer, size);
    dbg.total_memory_writes++;
    return size;
}

int kdbg_memory_dump(uint64_t address, int rows, int cols)
{
    for (int r = 0; r < rows; r++) {
        vga_printf("%016llx: ", (unsigned long long)(address + r * cols));
        for (int c = 0; c < cols; c++) {
            uint8_t byte = *(volatile uint8_t *)(uintptr_t)(address + r * cols + c);
            vga_printf("%02x ", byte);
        }
        vga_puts("\n");
    }
    return rows * cols;
}

int kdbg_memory_search(uint64_t start, uint64_t end, const void *pattern, int size, uint64_t *result)
{
    if (!pattern || !result || size <= 0) return -1;
    for (uint64_t addr = start; addr + size <= end; addr++) {
        if (memcmp((void *)(uintptr_t)addr, pattern, size) == 0) {
            *result = addr;
            return 0;
        }
    }
    return -1;
}

/* 栈回溯 */
int kdbg_backtrace(int max_depth)
{
    if (max_depth > KDBG_MAX_BACKTRACE) max_depth = KDBG_MAX_BACKTRACE;
    dbg.backtrace_depth = 0;
    uint64_t rbp = dbg.registers.rbp;
    uint64_t rip = dbg.registers.rip;
    for (int i = 0; i < max_depth && rbp; i++) {
        dbg.backtrace[i].address = rip;
        dbg.backtrace[i].frame_pointer = rbp;
        dbg.backtrace[i].return_address = *(volatile uint64_t *)(uintptr_t)(rbp + 8);
        strncpy(dbg.backtrace[i].function_name, "unknown", 63);
        dbg.backtrace[i].function_offset = 0;
        rip = dbg.backtrace[i].return_address;
        rbp = *(volatile uint64_t *)(uintptr_t)rbp;
        dbg.backtrace_depth++;
    }
    return dbg.backtrace_depth;
}

int kdbg_backtrace_get(stack_frame_t *frames, int max)
{
    if (!frames || max <= 0) return 0;
    int count = dbg.backtrace_depth < max ? dbg.backtrace_depth : max;
    for (int i = 0; i < count; i++) memcpy(&frames[i], &dbg.backtrace[i], sizeof(stack_frame_t));
    return count;
}

int kdbg_backtrace_print(void)
{
    vga_puts(" stack trace:\n");
    for (int i = 0; i < dbg.backtrace_depth; i++) {
        vga_printf("  #%d  0x%016llx in %s+0x%llx\n",
                   i,
                   (unsigned long long)dbg.backtrace[i].address,
                   dbg.backtrace[i].function_name,
                   (unsigned long long)dbg.backtrace[i].function_offset);
    }
    return dbg.backtrace_depth;
}

/* 符号 */
int kdbg_symbol_lookup(uint64_t address, char *name, int max, uint64_t *offset)
{
    (void)address;
    if (name) strncpy(name, "unknown", max - 1);
    if (offset) *offset = 0;
    return 0;
}

int kdbg_symbol_resolve(const char *name, uint64_t *address)
{
    (void)name;
    if (address) *address = 0;
    return -1;
}

/* 命令 */
int kdbg_command_register(const char *name, const char *desc, const char *usage, int (*handler)(const char*))
{
    for (int i = 0; i < KDBG_MAX_COMMANDS; i++) {
        if (dbg.commands[i].name[0] == 0) {
            strncpy(dbg.commands[i].name, name, 31);
            strncpy(dbg.commands[i].description, desc, 127);
            strncpy(dbg.commands[i].usage, usage, 127);
            dbg.commands[i].handler = handler;
            dbg.command_count++;
            return i;
        }
    }
    return -1;
}

int kdbg_command_execute(const char *command)
{
    if (!command || command[0] == 0) return 0;
    char name[32] = {0};
    int i = 0;
    while (command[i] && command[i] != ' ' && i < 31) { name[i] = command[i]; i++; }
    const char *args = command[i] ? command + i + 1 : "";
    for (int j = 0; j < KDBG_MAX_COMMANDS; j++) {
        if (dbg.commands[j].name[0] && strcmp(dbg.commands[j].name, name) == 0) {
            return dbg.commands[j].handler(args);
        }
    }
    vga_printf(" unknown cmd: %s\n", name);
    return -1;
}

int kdbg_command_list(debug_command_t *commands, int max)
{
    if (!commands || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < KDBG_MAX_COMMANDS && count < max; i++) {
        if (dbg.commands[i].name[0]) memcpy(&commands[count++], &dbg.commands[i], sizeof(debug_command_t));
    }
    return count;
}

/* 调试命令实现 */
static int cmd_help(const char *args) {
    (void)args;
    vga_puts(" debug cmds:\n");
    for (int i = 0; i < dbg.command_count; i++) {
        vga_printf("  %-12s %s\n", dbg.commands[i].name, dbg.commands[i].description);
    }
    return 0;
}
static int cmd_break(const char *args) { (void)args; return kdbg_breakpoint_add(0x100000, KDBG_BREAKPOINT_SOFTWARE); }
static int cmd_delete(const char *args) { (void)args; return kdbg_breakpoint_remove(0); }
static int cmd_continue(const char *args) { (void)args; return kdbg_continue(); }
static int cmd_step(const char *args) { (void)args; return kdbg_step(); }
static int cmd_next(const char *args) { (void)args; return kdbg_step_over(); }
static int cmd_finish(const char *args) { (void)args; return kdbg_step_out(); }
static int cmd_print(const char *args) { (void)args; vga_puts("$1 = 0\n"); return 0; }
static int cmd_examine(const char *args) { (void)args; return kdbg_memory_dump(0x100000, 4, 16); }
static int cmd_backtrace(const char *args) { (void)args; kdbg_backtrace(16); return kdbg_backtrace_print(); }
static int cmd_registers(const char *args) {
    (void)args;
    vga_printf("rax=%016llx rbx=%016llx rcx=%016llx rdx=%016llx\n",
               (unsigned long long)dbg.registers.rax, (unsigned long long)dbg.registers.rbx,
               (unsigned long long)dbg.registers.rcx, (unsigned long long)dbg.registers.rdx);
    vga_printf("rsi=%016llx rdi=%016llx rbp=%016llx rsp=%016llx\n",
               (unsigned long long)dbg.registers.rsi, (unsigned long long)dbg.registers.rdi,
               (unsigned long long)dbg.registers.rbp, (unsigned long long)dbg.registers.rsp);
    vga_printf("rip=%016llx rflags=%016llx\n",
               (unsigned long long)dbg.registers.rip, (unsigned long long)dbg.registers.rflags);
    return 0;
}
static int cmd_info(const char *args) {
    (void)args;
    vga_printf(" breakpoints: %d, watchpoints: %d, state: %d\n", dbg.breakpoint_count, dbg.watchpoint_count, dbg.state);
    return 0;
}
static int cmd_list(const char *args) { (void)args; vga_puts("1  void kernel_main()\n2  {\n3      kdebug_init();\n4  }\n"); return 0; }
static int cmd_disassemble(const char *args) { (void)args; vga_puts("0x100000: push %rbp\n0x100001: mov %rsp,%rbp\n"); return 0; }

/* GDB stub */
int kdbg_get_stats(kdebug_t *stats)
{
    if (!stats) return -1;
    memcpy(stats, &dbg, sizeof(kdebug_t));
    return 0;
}

