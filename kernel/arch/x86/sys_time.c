/* ============================================================================
 * 赫普OS - 系统调用：TIME
 * ========================================================================== */

#include "types.h"
#include <stdint.h>
#include "process.h"
#include <stddef.h>
#include "syscall.h"
#include "vga.h"
#include "kheap.h"
#include "sched.h"
#include "hepu.h"
#include "fingerprint.h"
#include "vfs.h"
#include "ipc.h"
extern const char *hepu_kernel_version;

u32 sys_sleep(u32 ms, u32 arg2,
                          u32 arg3, u32 arg4)
{
    (void)arg2; (void)arg3; (void)arg4;
    /* V3.89-E27: 真睡眠——调度器管理的进程进入 SLEEPING 让出 CPU,
     * 到点由 sched_tick 唤醒扫描转 READY. 消除忙等(省电+释放CPU).
     * 内核主线(shell)等非调度进程回退忙等(不阻塞内核主线). */
    extern process_t *current;
    extern uint32_t sched_get_ticks(void);
    extern volatile int need_resched;
    if (current && current->state == PROC_RUNNING) {
        current->sleep_until = sched_get_ticks() + (ms / 10) + 1;
        current->state = PROC_SLEEPING;
        need_resched = 1;
        return 0;
    }
    for (volatile u32 i = 0; i < ms * 1000; i++);
    return 0;
}

u32 sys_time(u32 arg1, u32 arg2, u32 arg3, u32 arg4)
{
    (void)arg1; (void)arg2; (void)arg3; (void)arg4;
    /* V3.89-E27 修复: 此前返回 pit_get_ticks()/1000——PIT 为 100Hz,
     * tick/1000 = 0.1/秒, 导致 SYS_TIME 语义错误(调度演示 A 的
     * 8 秒判断永不满足). 正确秒数 = tick / PIT_HZ(100). */
    extern u32 pit_get_seconds(void);
    return pit_get_seconds();
}

u32 sys_gettimeofday(u32 tv_ptr, u32 arg2, u32 arg3, u32 arg4)
{
    (void)arg2; (void)arg3; (void)arg4;
    /* V7.79安全修复: 用户指针校验（漏洞C全覆盖） */
    extern int user_ptr_valid(const void *ptr, uint32_t size);
    if (tv_ptr && !user_ptr_valid((void *)tv_ptr, 2 * sizeof(u32))) {
        vga_printf("[TIME] sys_gettimeofday: invalid tv ptr 0x%x\n", tv_ptr);
        return (u32)-1;
    }
    extern u32 pit_get_ticks(void);
    if (tv_ptr) {
        u32 *tv = (u32 *)tv_ptr;
        tv[0] = pit_get_ticks() / 1000;   /* seconds */
        tv[1] = (pit_get_ticks() % 1000) * 1000;  /* microseconds */
    }
    return 0;
}

u32 sys_settimeofday(u32 tv_ptr, u32 arg2, u32 arg3, u32 arg4)
{
    (void)tv_ptr; (void)arg2; (void)arg3; (void)arg4;
    return 0;  /* 原型阶段不允许设置时间 */
}

u32 sys_nanosleep(u32 req_ptr, u32 rem_ptr, u32 arg3, u32 arg4)
{
    (void)rem_ptr; (void)arg3; (void)arg4;
    /* V7.79安全修复: 用户指针校验（漏洞C全覆盖） */
    extern int user_ptr_valid(const void *ptr, uint32_t size);
    if (req_ptr && !user_ptr_valid((const void *)req_ptr, 2 * sizeof(u32))) {
        vga_printf("[TIME] sys_nanosleep: invalid req ptr 0x%x\n", req_ptr);
        return (u32)-1;
    }
    if (req_ptr) {
        u32 *req = (u32 *)req_ptr;
        u32 ms = req[0] * 1000 + req[1] / 1000000;
        for (volatile u32 i = 0; i < ms * 1000; i++);
    }
    return 0;
}

