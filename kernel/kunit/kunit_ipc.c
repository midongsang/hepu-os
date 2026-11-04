/* ============================================================================
 * 赫普OS - IPC单元测试
 * 覆盖：消息队列/信号量/共享内存/管道
 * ========================================================================== */

#include <stdint.h>
#include "kunit.h"
#include "vga.h"
#include "string.h"

/* === 消息队列测试 === */
#define MSG_QUEUE_SIZE 16
#define MSG_MAX_SIZE 256

typedef struct {
    u8  data[MSG_MAX_SIZE];
    u32 size;
    u32 sender_pid;
    u32 msg_type;
} ipc_msg_t;

typedef struct {
    ipc_msg_t messages[MSG_QUEUE_SIZE];
    u32 head;
    u32 tail;
    u32 count;
} msg_queue_t;

static void msg_queue_init(msg_queue_t *q)
{
    memset(q, 0, sizeof(msg_queue_t));
}

static int msg_queue_send(msg_queue_t *q, const u8 *data, u32 size,
                           u32 sender, u32 type)
{
    if (q->count >= MSG_QUEUE_SIZE) return -1;
    if (size > MSG_MAX_SIZE) return -1;

    memcpy(q->messages[q->tail].data, data, size);
    q->messages[q->tail].size = size;
    q->messages[q->tail].sender_pid = sender;
    q->messages[q->tail].msg_type = type;
    q->tail = (q->tail + 1) % MSG_QUEUE_SIZE;
    q->count++;
    return 0;
}

static int msg_queue_receive(msg_queue_t *q, u8 *data, u32 *size,
                              u32 *sender, u32 *type)
{
    if (q->count == 0) return -1;

    u32 len = q->messages[q->head].size;
    memcpy(data, q->messages[q->head].data, len);
    if (size) *size = len;
    if (sender) *sender = q->messages[q->head].sender_pid;
    if (type) *type = q->messages[q->head].msg_type;

    q->head = (q->head + 1) % MSG_QUEUE_SIZE;
    q->count--;
    return 0;
}

static kunit_result_t test_msg_queue_basic(void)
{
    msg_queue_t q;
    msg_queue_init(&q);

    u8 send_data[] = "Hello IPC!";
    KUNIT_ASSERT_EQ(msg_queue_send(&q, send_data, 11, 1, 0), 0, " send msg ");
    KUNIT_ASSERT_EQ(q.count, 1, " queue count =1");

    u8 recv_data[64];
    u32 recv_size, sender, type;
    KUNIT_ASSERT_EQ(msg_queue_receive(&q, recv_data, &recv_size, &sender, &type), 0, " recv msg ");
    KUNIT_ASSERT_EQ(recv_size, 11, " recv size ");
    KUNIT_ASSERT_EQ(sender, 1, " send 者 PID");
    KUNIT_ASSERT_EQ(memcmp(recv_data, send_data, 11), 0, " data 一致 ");
    KUNIT_ASSERT_EQ(q.count, 0, " queue empty ");
    return KUNIT_PASS;
}

static kunit_result_t test_msg_queue_overflow(void)
{
    msg_queue_t q;
    msg_queue_init(&q);

    /* 填满队列 */
    for (int i = 0; i < MSG_QUEUE_SIZE; i++) {
        u8 data = (u8)i;
        KUNIT_ASSERT_EQ(msg_queue_send(&q, &data, 1, i, 0), 0, " 填充 queue ");
    }
    KUNIT_ASSERT_EQ(q.count, MSG_QUEUE_SIZE, " queue full ");

    /* 再发送应该失败 */
    u8 data = 0xFF;
    KUNIT_ASSERT_EQ(msg_queue_send(&q, &data, 1, 99, 0), -1, " queue full send failed ");
    return KUNIT_PASS;
}

static kunit_result_t test_msg_queue_empty(void)
{
    msg_queue_t q;
    msg_queue_init(&q);

    u8 data[64];
    u32 size;
    KUNIT_ASSERT_EQ(msg_queue_receive(&q, data, &size, NULL, NULL), -1, " empty queue recv failed ");
    return KUNIT_PASS;
}

/* === 信号量测试 === */
typedef struct {
    s32 count;
    u32 max_count;
} semaphore_t;

static void sem_init(semaphore_t *sem, s32 initial, u32 max)
{
    sem->count = initial;
    sem->max_count = max;
}

static int sem_wait(semaphore_t *sem)
{
    if (sem->count <= 0) return -1;  /* 非阻塞模式 */
    sem->count--;
    return 0;
}

static int sem_post(semaphore_t *sem)
{
    if ((u32)sem->count >= sem->max_count) return -1;
    sem->count++;
    return 0;
}

static kunit_result_t test_semaphore_basic(void)
{
    semaphore_t sem;
    sem_init(&sem, 3, 10);

    KUNIT_ASSERT_EQ(sem.count, 3, " initial count =3");
    KUNIT_ASSERT_EQ(sem_wait(&sem), 0, "wait ok ");
    KUNIT_ASSERT_EQ(sem.count, 2, " count =2");
    KUNIT_ASSERT_EQ(sem_post(&sem), 0, "post ok ");
    KUNIT_ASSERT_EQ(sem.count, 3, " count =3");
    return KUNIT_PASS;
}

static kunit_result_t test_semaphore_zero(void)
{
    semaphore_t sem;
    sem_init(&sem, 0, 10);

    KUNIT_ASSERT_EQ(sem_wait(&sem), -1, " 零 semaphore wait failed ");
    KUNIT_ASSERT_EQ(sem_post(&sem), 0, "post ok ");
    KUNIT_ASSERT_EQ(sem.count, 1, " count =1");
    KUNIT_ASSERT_EQ(sem_wait(&sem), 0, "wait ok ");
    return KUNIT_PASS;
}

static kunit_result_t test_semaphore_max(void)
{
    semaphore_t sem;
    sem_init(&sem, 5, 5);

    KUNIT_ASSERT_EQ(sem_post(&sem), -1, " 达 to max value post failed ");
    KUNIT_ASSERT_EQ(sem.count, 5, " count 保持 5");
    return KUNIT_PASS;
}

/* === 互斥锁测试 === */
typedef struct {
    u32 locked;
    u32 owner_pid;
} mutex_t;

static void mutex_init(mutex_t *mtx)
{
    mtx->locked = 0;
    mtx->owner_pid = 0;
}

static int mutex_lock(mutex_t *mtx, u32 pid)
{
    if (mtx->locked) return -1;  /* 非阻塞 */
    mtx->locked = 1;
    mtx->owner_pid = pid;
    return 0;
}

static int mutex_unlock(mutex_t *mtx, u32 pid)
{
    if (!mtx->locked) return -1;
    if (mtx->owner_pid != pid) return -1;
    mtx->locked = 0;
    mtx->owner_pid = 0;
    return 0;
}

static kunit_result_t test_mutex_basic(void)
{
    mutex_t mtx;
    mutex_init(&mtx);

    KUNIT_ASSERT_EQ(mutex_lock(&mtx, 1), 0, " 加锁 ok ");
    KUNIT_ASSERT_EQ(mtx.locked, 1, " lock ");
    KUNIT_ASSERT_EQ(mtx.owner_pid, 1, " all 者 =1");

    KUNIT_ASSERT_EQ(mutex_lock(&mtx, 2), -1, " 其他 process 加锁 failed ");
    KUNIT_ASSERT_EQ(mutex_unlock(&mtx, 1), 0, " unlock ok ");
    KUNIT_ASSERT_EQ(mtx.locked, 0, " not lock ");
    return KUNIT_PASS;
}

static kunit_result_t test_mutex_wrong_owner(void)
{
    mutex_t mtx;
    mutex_init(&mtx);

    mutex_lock(&mtx, 1);
    KUNIT_ASSERT_EQ(mutex_unlock(&mtx, 2), -1, " non all 者 unlock failed ");
    KUNIT_ASSERT_EQ(mtx.locked, 1, " 仍然 lock ");
    return KUNIT_PASS;
}

/* === 共享内存测试 === */
#define SHM_MAX_SEGMENTS 8
#define SHM_SEG_SIZE 4096

typedef struct {
    u8  data[SHM_SEG_SIZE];
    u32 key;
    u32 size;
    int attached;
    int in_use;
} shm_segment_t;

static shm_segment_t shm_segments[SHM_MAX_SEGMENTS];

static void shm_init(void)
{
    memset(shm_segments, 0, sizeof(shm_segments));
}

static int shm_create(u32 key, u32 size)
{
    if (size > SHM_SEG_SIZE) return -1;

    for (int i = 0; i < SHM_MAX_SEGMENTS; i++) {
        if (!shm_segments[i].in_use) {
            shm_segments[i].in_use = 1;
            shm_segments[i].key = key;
            shm_segments[i].size = size;
            shm_segments[i].attached = 1;
            return i;
        }
    }
    return -1;
}

static void *shm_attach(int seg_id)
{
    if (seg_id < 0 || seg_id >= SHM_MAX_SEGMENTS) return NULL;
    if (!shm_segments[seg_id].in_use) return NULL;
    shm_segments[seg_id].attached++;
    return shm_segments[seg_id].data;
}

static kunit_result_t test_shm_basic(void)
{
    shm_init();

    int seg = shm_create(0x1234, 1024);
    KUNIT_ASSERT(seg >= 0, " create share memory ");

    void *p1 = shm_attach(seg);
    void *p2 = shm_attach(seg);
    KUNIT_ASSERT_NOT_NULL(p1, " process 1 附加 ");
    KUNIT_ASSERT_NOT_NULL(p2, " process 2 附加 ");
    KUNIT_ASSERT_EQ(p1, p2, " 同一 physical addr ");
    KUNIT_ASSERT_EQ(shm_segments[seg].attached, 3, " 附加 count =3");
    return KUNIT_PASS;
}

/* === 管道测试 === */
#define PIPE_SIZE 4096

typedef struct {
    u8  buffer[PIPE_SIZE];
    u32 read_pos;
    u32 write_pos;
    u32 count;
    int read_end_open;
    int write_end_open;
} pipe_t;

static void pipe_init(pipe_t *p)
{
    memset(p, 0, sizeof(pipe_t));
    p->read_end_open = 1;
    p->write_end_open = 1;
}

static int pipe_write(pipe_t *p, const u8 *data, u32 len)
{
    if (!p->write_end_open) return -1;
    if (p->count + len > PIPE_SIZE) return -1;

    for (u32 i = 0; i < len; i++) {
        p->buffer[p->write_pos] = data[i];
        p->write_pos = (p->write_pos + 1) % PIPE_SIZE;
    }
    p->count += len;
    return (int)len;
}

static int pipe_read(pipe_t *p, u8 *data, u32 len)
{
    if (!p->read_end_open) return -1;
    if (p->count == 0) return 0;

    u32 to_read = len < p->count ? len : p->count;
    for (u32 i = 0; i < to_read; i++) {
        data[i] = p->buffer[p->read_pos];
        p->read_pos = (p->read_pos + 1) % PIPE_SIZE;
    }
    p->count -= to_read;
    return (int)to_read;
}

static kunit_result_t test_pipe_basic(void)
{
    pipe_t p;
    pipe_init(&p);

    u8 write_data[] = "pipe test";
    int written = pipe_write(&p, write_data, 9);
    KUNIT_ASSERT_EQ(written, 9, " write 9 bytes ");

    u8 read_data[32];
    int read = pipe_read(&p, read_data, 9);
    KUNIT_ASSERT_EQ(read, 9, " read 9 bytes ");
    KUNIT_ASSERT_EQ(memcmp(read_data, write_data, 9), 0, " data 一致 ");
    return KUNIT_PASS;
}

static kunit_result_t test_pipe_wrap(void)
{
    pipe_t p;
    pipe_init(&p);

    /* 写入接近末尾，测试回绕 */
    p.write_pos = PIPE_SIZE - 5;
    p.read_pos = PIPE_SIZE - 5;

    u8 data[10] = {0,1,2,3,4,5,6,7,8,9};
    pipe_write(&p, data, 10);
    KUNIT_ASSERT_EQ(p.count, 10, " 回绕 write count ");

    u8 recv[10];
    pipe_read(&p, recv, 10);
    KUNIT_ASSERT_EQ(memcmp(recv, data, 10), 0, " 回绕 read data 一致 ");
    return KUNIT_PASS;
}

/* === 测试套件注册 === */
static const kunit_test_t ipc_tests[] = {
    {" msg queue basic ", test_msg_queue_basic, "ipc"},
    {" msg queue 溢出 ", test_msg_queue_overflow, "ipc"},
    {" msg queue empty ", test_msg_queue_empty, "ipc"},
    {" semaphore basic ", test_semaphore_basic, "ipc"},
    {" semaphore 零 ", test_semaphore_zero, "ipc"},
    {" semaphore max ", test_semaphore_max, "ipc"},
    {" mutex basic ", test_mutex_basic, "ipc"},
    {" mutex all 者 ", test_mutex_wrong_owner, "ipc"},
    {" share memory ", test_shm_basic, "ipc"},
    {" 管道 basic ", test_pipe_basic, "ipc"},
    {" 管道回绕 ", test_pipe_wrap, "ipc"},
};
static const kunit_suite_t kunit_ipc_suite = {
    "IPC 通信 ", ipc_tests, 11
};

void kunit_ipc_register(void)
{
    kunit_register_suite(&kunit_ipc_suite);
    vga_puts("[KUNIT] IPC test suite registered(11 cases)\n");
}
