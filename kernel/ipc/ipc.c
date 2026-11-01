/* ============================================================================
 * 赫普OS - IPC 进程间通信
 * 消息队列 + 管道 + 共享内存 + 信号
 * ========================================================================== */

#include <stdint.h>
#include "ipc.h"
#include "ipc_perm.h"
#include "vga.h"
#include "string.h"
#include "kheap.h"
#include "sched.h"

static ipc_queue_t queues[IPC_MAX_QUEUES];
static ipc_pipe_t pipes[IPC_MAX_PIPES];
static ipc_shm_t shm_segments[IPC_MAX_SHM];

typedef void (*sighandler_t)(int);
static sighandler_t signal_handlers[NSIG];
static uint32_t pending_signals[64];

void ipc_init(void)
{
    for (int i = 0; i < IPC_MAX_QUEUES; i++) queues[i].used = 0;
    for (int i = 0; i < IPC_MAX_PIPES; i++) pipes[i].used = 0;
    for (int i = 0; i < IPC_MAX_SHM; i++) shm_segments[i].used = 0;
    for (int i = 0; i < NSIG; i++) signal_handlers[i] = NULL;
    for (int i = 0; i < 64; i++) pending_signals[i] = 0;
    vga_puts("[IPC] IPC subsystem init done \n");
}

/* ---- 消息队列 ---- */

int ipc_queue_create(uint32_t pid)
{
    for (int i = 0; i < IPC_MAX_QUEUES; i++) {
        if (!queues[i].used) {
            queues[i].used = 1;
            queues[i].owner_pid = pid;
            queues[i].head = queues[i].tail = 0;
            queues[i].count = 0;
            /* V7.81: 注册IPC权限对象（漏洞X完全接线）
             * 队列所有者=pid，读权限=所有用户(0x01)，写权限=所有用户(0x01)
             * 注意：当前为宽松权限，后续可根据安全需求收紧 */
            extern int ipc_perm_register(uint32_t, uint32_t, uint8_t, uint8_t, uint8_t);
            ipc_perm_register(pid, pid, IPC_OBJ_QUEUE, 0x01, 0x01);
            return i;
        }
    }
    return -1;
}

int ipc_queue_destroy(uint32_t pid)
{
    for (int i = 0; i < IPC_MAX_QUEUES; i++) {
        if (queues[i].used && queues[i].owner_pid == pid) {
            queues[i].used = 0;
            return 0;
        }
    }
    return -1;
}

static ipc_queue_t *find_queue(uint32_t pid)
{
    for (int i = 0; i < IPC_MAX_QUEUES; i++) {
        if (queues[i].used && queues[i].owner_pid == pid) return &queues[i];
    }
    return NULL;
}

int ipc_send_prio(uint32_t target_pid, uint8_t priority, uint32_t type, const void *data, uint32_t length)
{
    if (priority > IPC_PRIO_URGENT) priority = IPC_PRIO_URGENT;
    if (length > IPC_MSG_SIZE) length = IPC_MSG_SIZE;

    /* V7.81: IPC权限检查（漏洞X完全接线）
     * 检查当前进程是否有权限向target_pid的队列发送消息 */
    extern int ipc_perm_check(uint32_t, uint32_t, uint8_t, uint8_t, uint8_t);
    uint32_t caller_pid = sched_current_pid();
    if (!ipc_perm_check(caller_pid, target_pid, IPC_OBJ_QUEUE, IPC_OP_SEND, 1)) {
        vga_printf("[IPC] permission denied: pid %d send to %d\n", caller_pid, target_pid);
        return -1;
    }

    ipc_queue_t *q = find_queue(target_pid);
    if (!q) {
        ipc_queue_create(target_pid);
        q = find_queue(target_pid);
        if (!q) return -1;
    }
    if (q->count >= IPC_MAX_MESSAGES) return -1;
    ipc_message_t *msg = &q->messages[q->tail];
    msg->sender_pid = sched_current_pid();
    msg->type = type;
    msg->priority = priority;
    msg->length = length;
    if (data && length > 0) memcpy(msg->data, data, length);
    q->tail = (q->tail + 1) % IPC_MAX_MESSAGES;
    q->count++;
    return 0;
}

int ipc_send(uint32_t target_pid, uint32_t type, const void *data, uint32_t length)
{
    return ipc_send_prio(target_pid, IPC_PRIO_NORMAL, type, data, length);
}

int ipc_recv(uint32_t pid, ipc_message_t *msg)
{
    /* V7.81: IPC权限检查（漏洞X完全接线）
     * 检查当前进程是否有权限从pid的队列接收消息
     * 注意：只有队列所有者可以接收消息 */
    extern int ipc_perm_check(uint32_t, uint32_t, uint8_t, uint8_t, uint8_t);
    uint32_t caller_pid = sched_current_pid();
    if (caller_pid != pid) {
        /* 非所有者接收需要权限检查 */
        if (!ipc_perm_check(caller_pid, pid, IPC_OBJ_QUEUE, IPC_OP_RECV, 0)) {
            vga_printf("[IPC] permission denied: pid %d recv from %d\n", caller_pid, pid);
            return -1;
        }
    }

    ipc_queue_t *q = find_queue(pid);
    if (!q || !msg) return -1;
    /* 阻塞等待：使用schedule()让其他进程运行，避免hlt死锁 */
    while (q->count == 0) {
        extern void schedule(void);
        schedule();
    }
    return ipc_try_recv(pid, msg);
}

/* 从队列取最高优先级消息（同优先级FIFO），返回0成功 */
static int dequeue_highest_prio(ipc_queue_t *q, ipc_message_t *out)
{
    if (q->count == 0) return -1;

    /* 扫描找最高优先级（严格大于，保证同优先级取最旧的FIFO） */
    int best_idx = q->head;
    uint8_t best_prio = q->messages[q->head].priority;
    for (int i = 1; i < q->count; i++) {
        int idx = (q->head + i) % IPC_MAX_MESSAGES;
        if (q->messages[idx].priority > best_prio) {
            best_prio = q->messages[idx].priority;
            best_idx = idx;
        }
    }

    if (out) memcpy(out, &q->messages[best_idx], sizeof(ipc_message_t));

    /* 从环形缓冲移除该消息（将后续元素前移一位） */
    int cur = best_idx;
    while (cur != q->tail) {
        int next = (cur + 1) % IPC_MAX_MESSAGES;
        if (next != q->tail) {
            q->messages[cur] = q->messages[next];
        }
        cur = next;
    }
    q->tail = (q->tail + IPC_MAX_MESSAGES - 1) % IPC_MAX_MESSAGES;
    q->count--;
    return 0;
}

int ipc_try_recv(uint32_t pid, ipc_message_t *msg)
{
    ipc_queue_t *q = find_queue(pid);
    if (!q || !msg) return -1;
    return dequeue_highest_prio(q, msg);
}

int ipc_pending(uint32_t pid)
{
    ipc_queue_t *q = find_queue(pid);
    return q ? q->count : 0;
}

/* ---- 管道 ---- */

int ipc_pipe(int *read_fd, int *write_fd)
{
    for (int i = 0; i < IPC_MAX_PIPES; i++) {
        if (!pipes[i].used) {
            pipes[i].used = 1;
            pipes[i].read_pos = pipes[i].write_pos = 0;
            pipes[i].count = 0;
            pipes[i].read_end_open = pipes[i].write_end_open = 1;
            pipes[i].reader_pid = pipes[i].writer_pid = sched_current_pid();
            if (read_fd) *read_fd = i * 2;
            if (write_fd) *write_fd = i * 2 + 1;
            /* V7.82: 注册IPC权限对象（漏洞X完全接线）
             * 管道对象ID = pipe索引 + 0x20000偏移 */
            extern int ipc_perm_register(uint32_t, uint32_t, uint8_t, uint8_t, uint8_t);
            ipc_perm_register(pipes[i].reader_pid,
                              i + 0x20000,
                              IPC_OBJ_PIPE,
                              0x01, 0x01);  /* 读写权限：所有用户（简化） */
            return 0;
        }
    }
    return -1;
}

int ipc_pipe_read(int pipe_id, void *buf, uint32_t size)
{
    int idx = pipe_id / 2;
    if (idx < 0 || idx >= IPC_MAX_PIPES || !pipes[idx].used) return -1;
    if (!pipes[idx].read_end_open) return -1;

    /* V7.82: IPC权限检查（漏洞X完全接线） */
    extern int ipc_perm_check(uint32_t, uint32_t, uint8_t, uint8_t, uint8_t);
    uint32_t caller_pid = sched_current_pid();
    if (!ipc_perm_check(caller_pid, idx + 0x20000,
                        IPC_OBJ_PIPE, IPC_OP_RECV, 0)) {
        vga_printf("[IPC] pipe %d: pid %d denied read\n", idx, caller_pid);
        return -1;
    }

    while (pipes[idx].count == 0) {
        if (!pipes[idx].write_end_open) return 0;
        __asm__ volatile ("hlt");
    }
    uint32_t avail = pipes[idx].count;
    if (size > avail) size = avail;
    uint8_t *dst = (uint8_t *)buf;
    for (uint32_t i = 0; i < size; i++) {
        dst[i] = pipes[idx].buffer[pipes[idx].read_pos];
        pipes[idx].read_pos = (pipes[idx].read_pos + 1) % IPC_PIPE_BUF;
    }
    pipes[idx].count -= size;
    return size;
}

int ipc_pipe_write(int pipe_id, const void *buf, uint32_t size)
{
    int idx = pipe_id / 2;
    if (idx < 0 || idx >= IPC_MAX_PIPES || !pipes[idx].used) return -1;
    if (!pipes[idx].write_end_open) return -1;

    /* V7.82: IPC权限检查（漏洞X完全接线） */
    extern int ipc_perm_check(uint32_t, uint32_t, uint8_t, uint8_t, uint8_t);
    uint32_t caller_pid = sched_current_pid();
    if (!ipc_perm_check(caller_pid, idx + 0x20000,
                        IPC_OBJ_PIPE, IPC_OP_SEND, 1)) {
        vga_printf("[IPC] pipe %d: pid %d denied write\n", idx, caller_pid);
        return -1;
    }

    uint32_t space = IPC_PIPE_BUF - pipes[idx].count;
    if (size > space) size = space;
    if (size == 0) return 0;
    const uint8_t *src = (const uint8_t *)buf;
    for (uint32_t i = 0; i < size; i++) {
        pipes[idx].buffer[pipes[idx].write_pos] = src[i];
        pipes[idx].write_pos = (pipes[idx].write_pos + 1) % IPC_PIPE_BUF;
    }
    pipes[idx].count += size;
    return size;
}

/* ---- 共享内存 ---- */

int ipc_shm_get(uint32_t key, uint32_t size)
{
    for (int i = 0; i < IPC_MAX_SHM; i++) {
        if (shm_segments[i].used && shm_segments[i].key == key) return i;
    }
    for (int i = 0; i < IPC_MAX_SHM; i++) {
        if (!shm_segments[i].used) {
            void *mem = kmalloc(size);
            if (!mem) return -1;
            memset(mem, 0, size);
            shm_segments[i].used = 1;
            shm_segments[i].key = key;
            shm_segments[i].phys_addr = mem;
            shm_segments[i].size = size;
            shm_segments[i].attach_count = 0;
            /* V7.70安全修复: 记录创建者PID和默认权限0644 */
            extern uint32_t sched_current_pid(void);
            shm_segments[i].owner_pid = sched_current_pid();
            shm_segments[i].mode = 0644;  /* owner读写, 其他只读 */
            /* V7.82: 注册IPC权限对象（漏洞X完全接线）
             * 共享内存对象ID = key + IPC_MAX_SHM偏移，避免与队列ID冲突 */
            extern int ipc_perm_register(uint32_t, uint32_t, uint8_t, uint8_t, uint8_t);
            ipc_perm_register(shm_segments[i].owner_pid,
                              key + 0x10000,  /* 对象ID：key + 偏移 */
                              IPC_OBJ_SHM,
                              0x01,  /* 读权限：所有用户 */
                              0x01); /* 写权限：所有用户（简化，后续可收紧） */
            return i;
        }
    }
    return -1;
}

void *ipc_shm_attach(int shm_id, uint32_t pid)
{
    if (shm_id < 0 || shm_id >= IPC_MAX_SHM || !shm_segments[shm_id].used) return NULL;

    /* V7.70安全修复: 共享内存权限检查
     * mode 0644: owner读写, 其他只读
     * 简化实现: owner可以attach, 其他进程需要mode有读权限 */
    if (pid != shm_segments[shm_id].owner_pid) {
        /* 非owner检查读权限 (mode & 0444) */
        if ((shm_segments[shm_id].mode & 0444) == 0) {
            vga_printf("[IPC] shm %d: pid %d denied (no read perm)\n", shm_id, pid);
            return NULL;
        }
    }

    /* V7.82: IPC权限系统检查（漏洞X完全接线）
     * 使用ipc_perm_check进行统一权限管理 */
    extern int ipc_perm_check(uint32_t, uint32_t, uint8_t, uint8_t, uint8_t);
    if (!ipc_perm_check(pid,
                        shm_segments[shm_id].key + 0x10000,
                        IPC_OBJ_SHM, IPC_OP_RECV, 0)) {
        vga_printf("[IPC] shm %d: pid %d denied by ipc_perm\n", shm_id, pid);
        return NULL;
    }

    shm_segments[shm_id].attach_count++;
    return shm_segments[shm_id].phys_addr;
}

int ipc_shm_detach(int shm_id, uint32_t pid)
{
    (void)pid;
    if (shm_id < 0 || shm_id >= IPC_MAX_SHM || !shm_segments[shm_id].used) return -1;
    if (shm_segments[shm_id].attach_count > 0) shm_segments[shm_id].attach_count--;
    return 0;
}

/* V3.77: 释放共享内存段——补上 shmget 缺的回收路径（此前只增不减, 反复
 * sys_shmget 新 key 会耗尽 kheap, 属真实内存泄漏 API 缺口）。 */
int ipc_shm_release(int shm_id, uint32_t pid)
{
    if (shm_id < 0 || shm_id >= IPC_MAX_SHM || !shm_segments[shm_id].used) return -1;

    /* V7.70安全修复: 只有owner可以释放共享内存 */
    if (pid != shm_segments[shm_id].owner_pid) {
        vga_printf("[IPC] shm %d: pid %d denied release (owner=%d)\n",
                   shm_id, pid, shm_segments[shm_id].owner_pid);
        return -1;
    }

    extern void kfree(void *);
    if (shm_segments[shm_id].phys_addr) kfree(shm_segments[shm_id].phys_addr);
    shm_segments[shm_id].phys_addr = NULL;
    shm_segments[shm_id].size = 0;
    shm_segments[shm_id].attach_count = 0;
    shm_segments[shm_id].key = 0;
    shm_segments[shm_id].used = 0;
    shm_segments[shm_id].owner_pid = 0;
    shm_segments[shm_id].mode = 0;
    vga_printf("[IPC] shm %d released by pid %d\n", shm_id, pid);
    return 0;
}

/* ---- 信号 ---- */

int ipc_kill(uint32_t pid, int sig)
{
    /* V7.66安全修复: 校验pid范围，防止数组越界写
     * pending_signals[64], pid/32作为索引, pid必须 < 64*32=2048 */
    if (pid == 0 || pid >= 2048) return -1;
    if (sig < 0 || sig >= NSIG) return -1;
    pending_signals[pid / 32] |= (1 << (sig % 32));
    vga_printf("[IPC] signal %d -> PID %d\n", sig, pid);
    return 0;
}

int ipc_signal(int sig, void (*handler)(int))
{
    if (sig < 0 || sig >= NSIG) return -1;
    /* V7.66安全修复: 校验handler地址范围，防止任意代码执行
     * 用户空间: 0x00000000-0xBFFFFFFF
     * 内核空间: 0xC0000000-0xFFFFFFFF (不允许注册)
     * NULL表示默认处理，允许 */
    if (handler != NULL && (uint32_t)handler >= 0xC0000000) {
        vga_printf("[IPC] signal %d: invalid handler 0x%x (kernel addr)\n",
                   sig, (uint32_t)handler);
        return -1;
    }
    sighandler_t old = signal_handlers[sig];
    signal_handlers[sig] = handler;
    return (int)old;
}

int ipc_sigpending(uint32_t pid)
{
    /* V7.66安全修复: 校验pid范围 */
    if (pid == 0 || pid >= 2048) return -1;
    uint32_t mask = pending_signals[pid / 32];
    for (int i = 0; i < 32; i++) {
        if (mask & (1 << i)) {
            pending_signals[pid / 32] &= ~(1 << i);
            return i;
        }
    }
    return -1;
}
