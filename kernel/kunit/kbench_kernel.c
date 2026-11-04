/* ============================================================================
 * 赫普OS - 内核子系统性能基准测试扩展 (V1.02新增)
 * 覆盖：IPC消息队列、互斥锁/信号量、页分配/释放、内存拷贝、共享内存
 * ========================================================================== */

#include <stdint.h>
#include "types.h"
#include "kbench.h"
#include "ipc.h"
#include "ipc_sem.h"
#include "ipc_mutex.h"
#include "mm.h"
#include "string.h"
#include "vga.h"

/* === IPC消息队列基准 === */
static uint32_t bench_ipc_qpid = 2;   /* 假目标PID */
static ipc_message_t bench_ipc_msg;

static void bench_ipc_send_setup(void)
{
    ipc_queue_create(bench_ipc_qpid);
}

static void bench_ipc_send_func(void)
{
    ipc_send(bench_ipc_qpid, 1, "bench", 6);
}

static void bench_ipc_send_teardown(void)
{
    ipc_queue_destroy(bench_ipc_qpid);
}

static void bench_ipc_recv_setup(void)
{
    ipc_queue_create(bench_ipc_qpid);
    ipc_send(bench_ipc_qpid, 1, "bench", 6);
    ipc_send(bench_ipc_qpid, 1, "bench", 6);
    ipc_send(bench_ipc_qpid, 1, "bench", 6);
}

static void bench_ipc_recv_func(void)
{
    ipc_try_recv(bench_ipc_qpid, &bench_ipc_msg);
}

static void bench_ipc_recv_teardown(void)
{
    ipc_queue_destroy(bench_ipc_qpid);
}

/* === 互斥锁/信号量基准 === */
static int bench_mutex_id;
static int bench_sem_id;
static uint32_t bench_pid = 1;

static void bench_mutex_lock_setup(void)
{
    bench_mutex_id = ipc_mutex_create(0xBEEF);
}

static void bench_mutex_lock_func(void)
{
    ipc_mutex_lock(bench_mutex_id, bench_pid);
    ipc_mutex_unlock(bench_mutex_id, bench_pid);
}

static void bench_sem_ops_setup(void)
{
    bench_sem_id = ipc_semget(0xBEEF, 1);
}

static void bench_sem_ops_func(void)
{
    ipc_sem_wait(bench_sem_id);
    ipc_sem_post(bench_sem_id);
}

/* === 页分配/释放基准 === */
static uint32_t bench_pages[64];

static void bench_page_alloc_func(void)
{
    bench_pages[0] = mm_alloc_page();
    mm_free_page(bench_pages[0]);
}

static void bench_page4k_alloc_func(void)
{
    bench_pages[0] = mm_alloc_pages(4);
    mm_free_pages(bench_pages[0], 4);
}

/* === 大块内存拷贝基准 === */
static u8 bench_big_src[8192];
static u8 bench_big_dst[8192];

static void bench_big_copy_func(void)
{
    memcpy(bench_big_dst, bench_big_src, 8192);
}

/* === 整页内存操作基准 === */
static u8 bench_page_buf[4096];

static void bench_page_zero_func(void)
{
    memset(bench_page_buf, 0, 4096);
}

/* 基准测试组：内核子系统 */
const kbench_test_t kbench_kernel_tests[] = {
    {"ipc_send",      bench_ipc_send_setup,   bench_ipc_send_func,
     bench_ipc_send_teardown, 1000},
    {"ipc_recv",      bench_ipc_recv_setup,   bench_ipc_recv_func,
     bench_ipc_recv_teardown, 1000},
    {"mutex_lock+unlock", bench_mutex_lock_setup, bench_mutex_lock_func,
     NULL, 10000},
    {"sem_wait+signal",   bench_sem_ops_setup,    bench_sem_ops_func,
     NULL, 10000},
    {"page_alloc+free",   NULL, bench_page_alloc_func, NULL, 5000},
    {"page4k_alloc+free", NULL, bench_page4k_alloc_func, NULL, 1000},
    {"memcpy(8K)",        NULL, bench_big_copy_func, NULL, 2000},
    {"page_zero(4K)",     NULL, bench_page_zero_func, NULL, 5000},
};
const u32 kbench_kernel_count = 8;

/* 运行内核子系统基准 */
int kbench_run_kernel(void)
{
    static kbench_result_t results[16];
    return kbench_run_all(kbench_kernel_tests, kbench_kernel_count, results);
}
