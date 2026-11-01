/* ============================================================================
 * 赫普OS - 进程与线程单元测试
 * 覆盖：进程状态/PCB/线程/上下文切换/优先级/进程间通信
 * ========================================================================== */

#include <stdint.h>
#include "kunit.h"
#include "vga.h"
#include "string.h"

/* === 进程状态测试 === */
#define PROC_STATE_NEW       0
#define PROC_STATE_READY     1
#define PROC_STATE_RUNNING   2
#define PROC_STATE_BLOCKED   3
#define PROC_STATE_TERMINATED 4
#define PROC_STATE_SLEEPING  5
#define PROC_STATE_ZOMBIE    6

static kunit_result_t test_process_states(void)
{
    KUNIT_ASSERT_EQ(PROC_STATE_NEW, 0, " 新建 state ");
    KUNIT_ASSERT_EQ(PROC_STATE_READY, 1, " ready state ");
    KUNIT_ASSERT_EQ(PROC_STATE_RUNNING, 2, " run state ");
    KUNIT_ASSERT_EQ(PROC_STATE_BLOCKED, 3, " 阻塞 state ");
    KUNIT_ASSERT_EQ(PROC_STATE_TERMINATED, 4, " 终止 state ");
    KUNIT_ASSERT_EQ(PROC_STATE_SLEEPING, 5, " 睡眠 state ");
    KUNIT_ASSERT_EQ(PROC_STATE_ZOMBIE, 6, " 僵尸 state ");
    return KUNIT_PASS;
}

static const char *state_names[] = {
    "NEW", "READY", "RUNNING", "BLOCKED", "TERMINATED", "SLEEPING", "ZOMBIE"
};

static kunit_result_t test_process_state_names(void)
{
    KUNIT_ASSERT_EQ(strcmp(state_names[PROC_STATE_NEW], "NEW"), 0, "NEW name ");
    KUNIT_ASSERT_EQ(strcmp(state_names[PROC_STATE_RUNNING], "RUNNING"), 0, "RUNNING name ");
    KUNIT_ASSERT_EQ(strcmp(state_names[PROC_STATE_ZOMBIE], "ZOMBIE"), 0, "ZOMBIE name ");
    return KUNIT_PASS;
}

/* === PCB测试 === */
#define PROC_NAME_MAX 32
typedef struct {
    u32 pid;
    u32 ppid;
    u8  state;
    u8  priority;
    u32 ticks_remaining;
    u32 total_ticks;
    u32 stack_ptr;
    u32 page_dir;
    char name[PROC_NAME_MAX];
} pcb_t;

static kunit_result_t test_pcb_init(void)
{
    pcb_t pcb;
    memset(&pcb, 0, sizeof(pcb));
    pcb.pid = 1;
    pcb.ppid = 0;
    pcb.state = PROC_STATE_NEW;
    pcb.priority = 10;
    pcb.ticks_remaining = 100;
    strcpy(pcb.name, "init");

    KUNIT_ASSERT_EQ(pcb.pid, 1, "PID=1");
    KUNIT_ASSERT_EQ(pcb.ppid, 0, "PPID=0");
    KUNIT_ASSERT_EQ(pcb.state, PROC_STATE_NEW, " state =NEW");
    KUNIT_ASSERT_EQ(pcb.priority, 10, " 优先 level =10");
    KUNIT_ASSERT_EQ(strcmp(pcb.name, "init"), 0, " name =init");
    return KUNIT_PASS;
}

static kunit_result_t test_pcb_state_transition(void)
{
    pcb_t pcb;
    memset(&pcb, 0, sizeof(pcb));
    pcb.state = PROC_STATE_NEW;

    /* NEW -> READY */
    pcb.state = PROC_STATE_READY;
    KUNIT_ASSERT_EQ(pcb.state, PROC_STATE_READY, "NEW→READY");

    /* READY -> RUNNING */
    pcb.state = PROC_STATE_RUNNING;
    KUNIT_ASSERT_EQ(pcb.state, PROC_STATE_RUNNING, "READY→RUNNING");

    /* RUNNING -> BLOCKED */
    pcb.state = PROC_STATE_BLOCKED;
    KUNIT_ASSERT_EQ(pcb.state, PROC_STATE_BLOCKED, "RUNNING→BLOCKED");

    /* BLOCKED -> READY */
    pcb.state = PROC_STATE_READY;
    KUNIT_ASSERT_EQ(pcb.state, PROC_STATE_READY, "BLOCKED→READY");

    /* RUNNING -> TERMINATED */
    pcb.state = PROC_STATE_TERMINATED;
    KUNIT_ASSERT_EQ(pcb.state, PROC_STATE_TERMINATED, "RUNNING→TERMINATED");
    return KUNIT_PASS;
}

/* === 优先级测试 === */
#define PRIORITY_MIN 0
#define PRIORITY_MAX 31
#define PRIORITY_DEFAULT 15

static kunit_result_t test_priority_range(void)
{
    KUNIT_ASSERT_EQ(PRIORITY_MIN, 0, " 最 low 优先 level =0");
    KUNIT_ASSERT_EQ(PRIORITY_MAX, 31, " 最 high 优先 level =31");
    KUNIT_ASSERT_EQ(PRIORITY_DEFAULT, 15, " default 优先 level =15");

    /* 优先级验证 */
    KUNIT_ASSERT(PRIORITY_DEFAULT >= PRIORITY_MIN, " default >= 最 low ");
    KUNIT_ASSERT(PRIORITY_DEFAULT <= PRIORITY_MAX, " default <= 最 high ");
    return KUNIT_PASS;
}

static int priority_to_ticks(u8 priority)
{
    /* 高优先级 = 更多时间片 */
    if (priority > PRIORITY_MAX) priority = PRIORITY_MAX;
    return (priority + 1) * 10;
}

static kunit_result_t test_priority_ticks(void)
{
    KUNIT_ASSERT_EQ(priority_to_ticks(0), 10, " 优先 level 0=10ticks");
    KUNIT_ASSERT_EQ(priority_to_ticks(15), 160, " 优先 level 15=160ticks");
    KUNIT_ASSERT_EQ(priority_to_ticks(31), 320, " 优先 level 31=320ticks");
    KUNIT_ASSERT_EQ(priority_to_ticks(99), 320, " 超出 限钳 bit ");
    return KUNIT_PASS;
}

/* === 线程测试 === */
#define THREAD_STATE_READY   0
#define THREAD_STATE_RUNNING 1
#define THREAD_STATE_BLOCKED 2
#define THREAD_STATE_DONE    3

typedef struct {
    u32 tid;
    u32 pid;
    u8  state;
    u32 stack_ptr;
    u32 entry_point;
    u32 arg;
} thread_t;

static kunit_result_t test_thread_basic(void)
{
    thread_t thread;
    memset(&thread, 0, sizeof(thread));
    thread.tid = 100;
    thread.pid = 1;
    thread.state = THREAD_STATE_READY;
    thread.entry_point = 0xC0100000;
    thread.arg = 42;

    KUNIT_ASSERT_EQ(thread.tid, 100, "TID=100");
    KUNIT_ASSERT_EQ(thread.pid, 1, " 所属 PID=1");
    KUNIT_ASSERT_EQ(thread.state, THREAD_STATE_READY, " thread ready ");
    KUNIT_ASSERT_EQ(thread.entry_point, 0xC0100000, " entry addr ");
    KUNIT_ASSERT_EQ(thread.arg, 42, " thread param ");
    return KUNIT_PASS;
}

static kunit_result_t test_thread_states(void)
{
    KUNIT_ASSERT_EQ(THREAD_STATE_READY, 0, " thread ready ");
    KUNIT_ASSERT_EQ(THREAD_STATE_RUNNING, 1, " thread run ");
    KUNIT_ASSERT_EQ(THREAD_STATE_BLOCKED, 2, " thread 阻塞 ");
    KUNIT_ASSERT_EQ(THREAD_STATE_DONE, 3, " thread done ");
    return KUNIT_PASS;
}

/* === 上下文切换测试 === */
typedef struct {
    u32 eax, ebx, ecx, edx;
    u32 esi, edi, ebp, esp;
    u32 eip, eflags;
    u16 cs, ds, es, fs, gs, ss;
} cpu_context_t;

static kunit_result_t test_context_size(void)
{
    /* 验证上下文结构大小 */
    KUNIT_ASSERT(sizeof(cpu_context_t) >= 40, " ctx struct 足够大 ");
    return KUNIT_PASS;
}

static kunit_result_t test_context_save_restore(void)
{
    cpu_context_t ctx1, ctx2;
    memset(&ctx1, 0, sizeof(ctx1));
    memset(&ctx2, 0, sizeof(ctx2));

    /* 模拟保存上下文 */
    ctx1.eax = 0x12345678;
    ctx1.ebx = 0xABCDEF01;
    ctx1.esp = 0xC000FFF0;
    ctx1.eip = 0xC0100000;
    ctx1.eflags = 0x202;

    /* 模拟恢复 */
    memcpy(&ctx2, &ctx1, sizeof(ctx1));

    KUNIT_ASSERT_EQ(ctx2.eax, 0x12345678, "EAX resume ");
    KUNIT_ASSERT_EQ(ctx2.ebx, 0xABCDEF01, "EBX resume ");
    KUNIT_ASSERT_EQ(ctx2.esp, 0xC000FFF0, "ESP resume ");
    KUNIT_ASSERT_EQ(ctx2.eip, 0xC0100000, "EIP resume ");
    KUNIT_ASSERT_EQ(ctx2.eflags, 0x202, "EFLAGS resume ");
    return KUNIT_PASS;
}

/* === 进程表测试 === */
#define PROC_MAX 64
static pcb_t process_table[PROC_MAX];
static u32 next_pid;

static void proc_table_init(void)
{
    memset(process_table, 0, sizeof(process_table));
    next_pid = 1;
}

static int proc_create(const char *name, u8 priority)
{
    for (int i = 0; i < PROC_MAX; i++) {
        if (process_table[i].state == PROC_STATE_TERMINATED ||
            (process_table[i].pid == 0 && i > 0)) {
            memset(&process_table[i], 0, sizeof(pcb_t));
            process_table[i].pid = next_pid++;
            process_table[i].state = PROC_STATE_NEW;
            process_table[i].priority = priority;
            process_table[i].ticks_remaining = priority_to_ticks(priority);
            if (name) strncpy(process_table[i].name, name, PROC_NAME_MAX - 1);
            return process_table[i].pid;
        }
    }
    return -1;
}

static pcb_t *proc_find(u32 pid)
{
    if (pid == 0) return 0;   /* pid 0 无效 */
    for (int i = 0; i < PROC_MAX; i++) {
        if (process_table[i].pid == pid &&
            process_table[i].state != PROC_STATE_TERMINATED) {
            return &process_table[i];
        }
    }
    return 0;
}

static kunit_result_t test_proc_table_create(void)
{
    proc_table_init();

    int pid1 = proc_create("shell", 15);
    int pid2 = proc_create("editor", 10);
    int pid3 = proc_create("browser", 20);

    KUNIT_ASSERT_EQ(pid1, 1, " 第一 process PID=1");
    KUNIT_ASSERT_EQ(pid2, 2, " 第二 process PID=2");
    KUNIT_ASSERT_EQ(pid3, 3, " 第三 process PID=3");

    pcb_t *p = proc_find(2);
    KUNIT_ASSERT_NOT_NULL(p, " found PID=2");
    KUNIT_ASSERT_EQ(strcmp(p->name, "editor"), 0, " process 名 =editor");
    KUNIT_ASSERT_EQ(p->priority, 10, " 优先 level =10");
    return KUNIT_PASS;
}

static kunit_result_t test_proc_table_find_miss(void)
{
    proc_table_init();
    proc_create("test", 10);

    KUNIT_ASSERT_NULL(proc_find(999), " not found PID=999");
    KUNIT_ASSERT_NULL(proc_find(0), " not found PID=0");
    return KUNIT_PASS;
}

/* === 进程间通信测试 === */
#define IPC_MSG_MAX 64
typedef struct {
    u32 sender;
    u32 receiver;
    u8  data[64];
    u32 length;
} ipc_message_t;

static kunit_result_t test_ipc_message(void)
{
    ipc_message_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.sender = 1;
    msg.receiver = 2;
    msg.length = 5;
    memcpy(msg.data, "HELLO", 5);

    KUNIT_ASSERT_EQ(msg.sender, 1, " send 者 =1");
    KUNIT_ASSERT_EQ(msg.receiver, 2, " receivers =2");
    KUNIT_ASSERT_EQ(msg.length, 5, " msg len =5");
    KUNIT_ASSERT_EQ(memcmp(msg.data, "HELLO", 5), 0, " msg 内容 ");
    return KUNIT_PASS;
}

/* === 测试套件注册 === */
static const kunit_test_t process_tests[] = {
    {" process state ", test_process_states, "process"},
    {" process state 名 ", test_process_state_names, "process"},
    {"PCB init ", test_pcb_init, "process"},
    {"PCB state 转换 ", test_pcb_state_transition, "process"},
    {" 优先 level 范围 ", test_priority_range, "process"},
    {" 优先 level time 片 ", test_priority_ticks, "process"},
    {" thread basic ", test_thread_basic, "process"},
    {" thread state ", test_thread_states, "process"},
    {" ctx size ", test_context_size, "process"},
    {" ctx save resume ", test_context_save_restore, "process"},
    {" process table create ", test_proc_table_create, "process"},
    {" process table 查找 failed ", test_proc_table_find_miss, "process"},
    {"IPC msg ", test_ipc_message, "process"},
};
static const kunit_suite_t kunit_process_suite = {
    " process & thread ", process_tests, 13
};

void kunit_process_register(void)
{
    kunit_register_suite(&kunit_process_suite);
    vga_puts("[KUNIT] process & thread test suite registered(13 cases)\n");
}
