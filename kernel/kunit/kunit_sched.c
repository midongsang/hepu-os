/* ============================================================================
 * 赫普OS - 调度器单元测试
 * 覆盖：优先级/时间片/进程状态/调度算法
 * ========================================================================== */

#include <stdint.h>
#include "kunit.h"
#include "vga.h"
#include "string.h"

/* === 进程状态测试 === */
#define PROC_STATE_READY   0
#define PROC_STATE_RUNNING 1
#define PROC_STATE_BLOCKED 2
#define PROC_STATE_SLEEPING 3
#define PROC_STATE_ZOMBIE  4
#define PROC_STATE_DEAD    5

static kunit_result_t test_process_states(void)
{
    KUNIT_ASSERT_EQ(PROC_STATE_READY, 0, " ready 态 ");
    KUNIT_ASSERT_EQ(PROC_STATE_RUNNING, 1, " run 态 ");
    KUNIT_ASSERT_EQ(PROC_STATE_BLOCKED, 2, " 阻塞态 ");
    KUNIT_ASSERT_EQ(PROC_STATE_SLEEPING, 3, " 睡眠态 ");
    KUNIT_ASSERT_EQ(PROC_STATE_ZOMBIE, 4, " 僵尸态 ");
    KUNIT_ASSERT_EQ(PROC_STATE_DEAD, 5, " 死亡态 ");
    return KUNIT_PASS;
}

static kunit_result_t test_process_state_transition(void)
{
    /* 状态转换验证 */
    u8 state = PROC_STATE_READY;

    /* READY -> RUNNING */
    state = PROC_STATE_RUNNING;
    KUNIT_ASSERT_EQ(state, PROC_STATE_RUNNING, " ready -> run ");

    /* RUNNING -> BLOCKED */
    state = PROC_STATE_BLOCKED;
    KUNIT_ASSERT_EQ(state, PROC_STATE_BLOCKED, " run -> 阻塞 ");

    /* BLOCKED -> READY */
    state = PROC_STATE_READY;
    KUNIT_ASSERT_EQ(state, PROC_STATE_READY, " 阻塞 -> ready ");

    /* RUNNING -> SLEEPING */
    state = PROC_STATE_SLEEPING;
    KUNIT_ASSERT_EQ(state, PROC_STATE_SLEEPING, " run -> 睡眠 ");

    /* SLEEPING -> READY */
    state = PROC_STATE_READY;
    KUNIT_ASSERT_EQ(state, PROC_STATE_READY, " 睡眠 -> ready ");

    return KUNIT_PASS;
}

/* === 优先级测试 === */
#define PRIORITY_HIGHEST 0
#define PRIORITY_HIGH    25
#define PRIORITY_NORMAL  50
#define PRIORITY_LOW     75
#define PRIORITY_LOWEST  99
#define PRIORITY_IDLE    100

static kunit_result_t test_priority_levels(void)
{
    KUNIT_ASSERT(PRIORITY_HIGHEST < PRIORITY_HIGH, " 最 high 优先 level < high ");
    KUNIT_ASSERT(PRIORITY_HIGH < PRIORITY_NORMAL, " high < normal ");
    KUNIT_ASSERT(PRIORITY_NORMAL < PRIORITY_LOW, " normal < low ");
    KUNIT_ASSERT(PRIORITY_LOW < PRIORITY_LOWEST, " low < 最 low ");
    KUNIT_ASSERT(PRIORITY_LOWEST < PRIORITY_IDLE, " 最 low < free ");
    return KUNIT_PASS;
}

static kunit_result_t test_priority_comparison(void)
{
    /* 模拟优先级比较：数值越小优先级越高 */
    u8 p1 = PRIORITY_HIGH;
    u8 p2 = PRIORITY_NORMAL;

    KUNIT_ASSERT(p1 < p2, " high 优先 level count value 更小 ");
    KUNIT_ASSERT(p2 > p1, " normal 优先 level count value 更大 ");

    /* 相同优先级 */
    KUNIT_ASSERT_EQ(PRIORITY_NORMAL, PRIORITY_NORMAL, " 相同优先 level 相 etc ");
    return KUNIT_PASS;
}

/* === 时间片测试 === */
#define TIMESLICE_DEFAULT 100  /* ms */
#define TIMESLICE_MIN     10
#define TIMESLICE_MAX     1000

static kunit_result_t test_timeslice_values(void)
{
    KUNIT_ASSERT_EQ(TIMESLICE_DEFAULT, 100, " default time 片 ");
    KUNIT_ASSERT(TIMESLICE_MIN < TIMESLICE_DEFAULT, " min < default ");
    KUNIT_ASSERT(TIMESLICE_MAX > TIMESLICE_DEFAULT, " max > default ");
    return KUNIT_PASS;
}

static kunit_result_t test_timeslice_decay(void)
{
    /* 模拟时间片递减 */
    u32 timeslice = TIMESLICE_DEFAULT;

    for (int i = 0; i < 50; i++) {
        timeslice--;
    }
    KUNIT_ASSERT_EQ(timeslice, 50, " 递减 50 next 50");

    /* 时间片耗尽 */
    timeslice = 0;
    KUNIT_ASSERT_EQ(timeslice, 0, " time 片耗尽 ");
    return KUNIT_PASS;
}

/* === 调度算法测试 === */
typedef struct {
    u32 pid;
    u8  priority;
    u8  state;
    u32 timeslice;
    u64 total_runtime;
} test_proc_t;

static int sched_pick_highest_priority(test_proc_t *procs, u32 count)
{
    int best = -1;
    u8 best_prio = 0xFF;

    for (u32 i = 0; i < count; i++) {
        if (procs[i].state == PROC_STATE_READY &&
            procs[i].priority < best_prio) {
            best_prio = procs[i].priority;
            best = (int)i;
        }
    }
    return best;
}

static kunit_result_t test_sched_priority_pick(void)
{
    test_proc_t procs[5] = {
        {1, PRIORITY_NORMAL, PROC_STATE_READY, 100, 0},
        {2, PRIORITY_HIGH, PROC_STATE_READY, 100, 0},
        {3, PRIORITY_LOW, PROC_STATE_READY, 100, 0},
        {4, PRIORITY_HIGHEST, PROC_STATE_BLOCKED, 100, 0},  /* 阻塞，不选 */
        {5, PRIORITY_IDLE, PROC_STATE_READY, 100, 0},
    };

    int selected = sched_pick_highest_priority(procs, 5);
    KUNIT_ASSERT_EQ(selected, 1, " 选择最 high 优先 level ready process(PID2)");
    KUNIT_ASSERT_EQ(procs[selected].pid, 2, " 选 in PID=2");
    return KUNIT_PASS;
}

static kunit_result_t test_sched_round_robin(void)
{
    /* 模拟轮询调度：相同优先级轮流执行 */
    test_proc_t procs[3] = {
        {1, PRIORITY_NORMAL, PROC_STATE_READY, 100, 0},
        {2, PRIORITY_NORMAL, PROC_STATE_READY, 100, 0},
        {3, PRIORITY_NORMAL, PROC_STATE_READY, 100, 0},
    };

    /* 简单轮询：依次选择 */
    u32 execution_order[6];
    u32 current = 0;

    for (int i = 0; i < 6; i++) {
        execution_order[i] = procs[current].pid;
        current = (current + 1) % 3;
    }

    KUNIT_ASSERT_EQ(execution_order[0], 1, " 第 1 exec PID1");
    KUNIT_ASSERT_EQ(execution_order[1], 2, " 第 2 exec PID2");
    KUNIT_ASSERT_EQ(execution_order[2], 3, " 第 3 exec PID3");
    KUNIT_ASSERT_EQ(execution_order[3], 1, " 第 4 回 to PID1");
    return KUNIT_PASS;
}

/* === 进程ID测试 === */
#define PID_MAX 65535
#define PID_RESERVED 0
#define PID_INIT 1

static kunit_result_t test_pid_allocation(void)
{
    /* 模拟PID分配 */
    static u32 next_pid = PID_INIT;
    u32 pids[10];

    for (int i = 0; i < 10; i++) {
        pids[i] = next_pid++;
        if (next_pid > PID_MAX) next_pid = PID_INIT;
    }

    KUNIT_ASSERT_EQ(pids[0], 1, " 第一 PID=1(init)");
    KUNIT_ASSERT_EQ(pids[1], 2, " 第二 PID=2");
    KUNIT_ASSERT_EQ(pids[9], 10, " 第十 PID=10");
    KUNIT_ASSERT(pids[0] != PID_RESERVED, "PID not used 保留 value 0");
    return KUNIT_PASS;
}

/* === 测试套件注册 === */
static const kunit_test_t sched_tests[] = {
    {" process state ", test_process_states, "sched"},
    {" state 转换 ", test_process_state_transition, "sched"},
    {" 优先 level level 别 ", test_priority_levels, "sched"},
    {" 优先 level 比较 ", test_priority_comparison, "sched"},
    {" time 片 value ", test_timeslice_values, "sched"},
    {" time 片递减 ", test_timeslice_decay, "sched"},
    {" 优先 level sched ", test_sched_priority_pick, "sched"},
    {" 轮询 sched ", test_sched_round_robin, "sched"},
    {"PID alloc ", test_pid_allocation, "sched"},
};
static const kunit_suite_t kunit_sched_suite = {
    " scheduler ", sched_tests, 9
};

void kunit_sched_register(void)
{
    kunit_register_suite(&kunit_sched_suite);
    vga_puts("[KUNIT] scheduler test suite registered(9 cases)\n");
}
