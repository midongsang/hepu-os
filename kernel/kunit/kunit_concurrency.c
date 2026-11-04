/* ============================================================================
 * 赫普OS - 并发与同步单元测试
 * 覆盖：自旋锁/互斥锁/信号量/读写锁/原子操作/屏障
 * ========================================================================== */

#include <stdint.h>
#include "kunit.h"
#include "vga.h"
#include "string.h"

/* === 自旋锁测试 === */
typedef struct {
    volatile u32 lock;
} spinlock_t;

static void spinlock_init(spinlock_t *s) { s->lock = 0; }
static int spinlock_trylock(spinlock_t *s)
{
    if (s->lock == 0) { s->lock = 1; return 0; }
    return -1;
}
static void spinlock_unlock(spinlock_t *s) { s->lock = 0; }

static kunit_result_t test_spinlock_init(void)
{
    spinlock_t lock;
    spinlock_init(&lock);
    KUNIT_ASSERT_EQ(lock.lock, 0, " 自旋锁 initial not lock ");
    return KUNIT_PASS;
}

static kunit_result_t test_spinlock_lock_unlock(void)
{
    spinlock_t lock;
    spinlock_init(&lock);

    KUNIT_ASSERT_EQ(spinlock_trylock(&lock), 0, " 加锁 ok ");
    KUNIT_ASSERT_EQ(lock.lock, 1, " 锁 state = lock ");
    KUNIT_ASSERT_EQ(spinlock_trylock(&lock), -1, " 重复加锁 failed ");

    spinlock_unlock(&lock);
    KUNIT_ASSERT_EQ(lock.lock, 0, " unlock next not lock ");
    KUNIT_ASSERT_EQ(spinlock_trylock(&lock), 0, " unlock next 可再加锁 ");
    return KUNIT_PASS;
}

/* === 互斥锁测试 === */
typedef struct {
    volatile u32 owner;
    volatile u32 count;
} mutex_t;

static void mutex_init(mutex_t *m) { m->owner = 0xFFFFFFFF; m->count = 0; }
static int mutex_lock(mutex_t *m, u32 tid)
{
    if (m->count == 0) { m->owner = tid; m->count = 1; return 0; }
    if (m->owner == tid) { m->count++; return 0; }  /* 可重入 */
    return -1;
}
static int mutex_unlock(mutex_t *m, u32 tid)
{
    if (m->owner != tid) return -1;
    if (m->count == 0) return -1;
    m->count--;
    if (m->count == 0) m->owner = 0xFFFFFFFF;
    return 0;
}

static kunit_result_t test_mutex_basic(void)
{
    mutex_t m;
    mutex_init(&m);

    KUNIT_ASSERT_EQ(mutex_lock(&m, 1), 0, " thread 1 加锁 ");
    KUNIT_ASSERT_EQ(m.owner, 1, " all 者 = thread 1");
    KUNIT_ASSERT_EQ(mutex_lock(&m, 2), -1, " thread 2 加锁 failed ");
    KUNIT_ASSERT_EQ(mutex_unlock(&m, 1), 0, " thread 1 unlock ");
    KUNIT_ASSERT_EQ(mutex_lock(&m, 2), 0, " thread 2 可加锁 ");
    return KUNIT_PASS;
}

static kunit_result_t test_mutex_reentrant(void)
{
    mutex_t m;
    mutex_init(&m);

    KUNIT_ASSERT_EQ(mutex_lock(&m, 1), 0, " 第一 加锁 ");
    KUNIT_ASSERT_EQ(mutex_lock(&m, 1), 0, " 可重入加锁 ");
    KUNIT_ASSERT_EQ(m.count, 2, " count =2");
    KUNIT_ASSERT_EQ(mutex_unlock(&m, 1), 0, " 第一 unlock ");
    KUNIT_ASSERT_EQ(m.count, 1, " count =1");
    KUNIT_ASSERT_EQ(mutex_unlock(&m, 1), 0, " 第二 unlock ");
    KUNIT_ASSERT_EQ(m.count, 0, " count =0");
    return KUNIT_PASS;
}

static kunit_result_t test_mutex_wrong_owner(void)
{
    mutex_t m;
    mutex_init(&m);
    mutex_lock(&m, 1);
    KUNIT_ASSERT_EQ(mutex_unlock(&m, 2), -1, " non all 者 unlock failed ");
    return KUNIT_PASS;
}

/* === 信号量测试 === */
typedef struct {
    volatile s32 count;
    volatile u32 max;
} semaphore_t;

static void sem_init(semaphore_t *s, u32 initial, u32 max)
{
    s->count = (s32)initial;
    s->max = max;
}
static int sem_wait(semaphore_t *s)
{
    if (s->count <= 0) return -1;
    s->count--;
    return 0;
}
static int sem_post(semaphore_t *s)
{
    if ((u32)s->count >= s->max) return -1;
    s->count++;
    return 0;
}

static kunit_result_t test_semaphore_basic(void)
{
    semaphore_t s;
    sem_init(&s, 3, 5);

    KUNIT_ASSERT_EQ(s.count, 3, " initial count =3");
    KUNIT_ASSERT_EQ(sem_wait(&s), 0, "wait ok ");
    KUNIT_ASSERT_EQ(s.count, 2, " count =2");
    KUNIT_ASSERT_EQ(sem_post(&s), 0, "post ok ");
    KUNIT_ASSERT_EQ(s.count, 3, " count =3");
    return KUNIT_PASS;
}

static kunit_result_t test_semaphore_zero(void)
{
    semaphore_t s;
    sem_init(&s, 0, 5);
    KUNIT_ASSERT_EQ(sem_wait(&s), -1, " 零 semaphore wait failed ");
    KUNIT_ASSERT_EQ(sem_post(&s), 0, "post ok ");
    KUNIT_ASSERT_EQ(sem_wait(&s), 0, "post next wait ok ");
    return KUNIT_PASS;
}

static kunit_result_t test_semaphore_max(void)
{
    semaphore_t s;
    sem_init(&s, 5, 5);
    KUNIT_ASSERT_EQ(sem_post(&s), -1, " 达 to max value post failed ");
    return KUNIT_PASS;
}

/* === 读写锁测试 === */
typedef struct {
    volatile s32 readers;
    volatile u32 writer;
    volatile u32 write_waiting;
} rwlock_t;

static void rwlock_init(rwlock_t *rw)
{
    rw->readers = 0;
    rw->writer = 0xFFFFFFFF;
    rw->write_waiting = 0;
}
static int rwlock_read_lock(rwlock_t *rw)
{
    if (rw->writer != 0xFFFFFFFF || rw->write_waiting > 0) return -1;
    rw->readers++;
    return 0;
}
static int rwlock_read_unlock(rwlock_t *rw)
{
    if (rw->readers <= 0) return -1;
    rw->readers--;
    return 0;
}
static int rwlock_write_lock(rwlock_t *rw, u32 tid)
{
    if (rw->writer != 0xFFFFFFFF || rw->readers > 0) return -1;
    rw->writer = tid;
    return 0;
}
static int rwlock_write_unlock(rwlock_t *rw, u32 tid)
{
    if (rw->writer != tid) return -1;
    rw->writer = 0xFFFFFFFF;
    return 0;
}

static kunit_result_t test_rwlock_multiple_readers(void)
{
    rwlock_t rw;
    rwlock_init(&rw);

    KUNIT_ASSERT_EQ(rwlock_read_lock(&rw), 0, " 读者 1 加锁 ");
    KUNIT_ASSERT_EQ(rwlock_read_lock(&rw), 0, " 读者 2 加锁 ");
    KUNIT_ASSERT_EQ(rwlock_read_lock(&rw), 0, " 读者 3 加锁 ");
    KUNIT_ASSERT_EQ(rw.readers, 3, "3 读者 ");

    KUNIT_ASSERT_EQ(rwlock_write_lock(&rw, 1), -1, " has 读者 runtime 写锁 failed ");

    rwlock_read_unlock(&rw);
    rwlock_read_unlock(&rw);
    rwlock_read_unlock(&rw);
    KUNIT_ASSERT_EQ(rw.readers, 0, " 读者全部 release ");
    KUNIT_ASSERT_EQ(rwlock_write_lock(&rw, 1), 0, " no 读者 runtime 写锁 ok ");
    return KUNIT_PASS;
}

static kunit_result_t test_rwlock_write_exclusive(void)
{
    rwlock_t rw;
    rwlock_init(&rw);

    rwlock_write_lock(&rw, 1);
    KUNIT_ASSERT_EQ(rwlock_read_lock(&rw), -1, " 写锁 runtime 读锁 failed ");
    KUNIT_ASSERT_EQ(rwlock_write_lock(&rw, 2), -1, " 写锁 runtime 写锁 failed ");
    rwlock_write_unlock(&rw, 1);
    KUNIT_ASSERT_EQ(rwlock_read_lock(&rw), 0, " 写 release next 读锁 ok ");
    return KUNIT_PASS;
}

/* === 原子操作测试 === */
static u32 atomic_add(volatile u32 *val, u32 delta)
{
    u32 old = *val;
    *val = old + delta;
    return old;
}
static u32 atomic_sub(volatile u32 *val, u32 delta)
{
    u32 old = *val;
    *val = old - delta;
    return old;
}
static int atomic_cas(volatile u32 *val, u32 expected, u32 new_val)
{
    if (*val == expected) { *val = new_val; return 1; }
    return 0;
}

static kunit_result_t test_atomic_add_sub(void)
{
    volatile u32 val = 100;
    KUNIT_ASSERT_EQ(atomic_add(&val, 50), 100, "add 返回旧 value 100");
    KUNIT_ASSERT_EQ(val, 150, "add next value =150");
    KUNIT_ASSERT_EQ(atomic_sub(&val, 30), 150, "sub 返回旧 value 150");
    KUNIT_ASSERT_EQ(val, 120, "sub next value =120");
    return KUNIT_PASS;
}

static kunit_result_t test_atomic_cas(void)
{
    volatile u32 val = 42;
    KUNIT_ASSERT_EQ(atomic_cas(&val, 42, 100), 1, "CAS ok ");
    KUNIT_ASSERT_EQ(val, 100, "CAS next value =100");
    KUNIT_ASSERT_EQ(atomic_cas(&val, 42, 200), 0, "CAS failed(expected value not pair)");
    KUNIT_ASSERT_EQ(val, 100, " value not 变 ");
    return KUNIT_PASS;
}

/* === 屏障测试 === */
typedef struct {
    volatile u32 count;
    volatile u32 arrived;
} barrier_t;

static void barrier_init(barrier_t *b, u32 count)
{
    b->count = count;
    b->arrived = 0;
}
static int barrier_wait(barrier_t *b)
{
    b->arrived++;
    if (b->arrived >= b->count) {
        b->arrived = 0;  /* 重置 */
        return 1;  /* 最后一个到达者 */
    }
    return 0;  /* 需要等待 */
}

static kunit_result_t test_barrier(void)
{
    barrier_t b;
    barrier_init(&b, 3);

    KUNIT_ASSERT_EQ(barrier_wait(&b), 0, " 第 1 to 达, 需 etc pending ");
    KUNIT_ASSERT_EQ(b.arrived, 1, " to 达 1 ");
    KUNIT_ASSERT_EQ(barrier_wait(&b), 0, " 第 2 to 达, 需 etc pending ");
    KUNIT_ASSERT_EQ(b.arrived, 2, " to 达 2 ");
    KUNIT_ASSERT_EQ(barrier_wait(&b), 1, " 第 3 to 达, all passed ");
    KUNIT_ASSERT_EQ(b.arrived, 0, " 屏障 reset ");
    return KUNIT_PASS;
}

/* === 测试套件注册 === */
static const kunit_test_t concurrency_tests[] = {
    {" 自旋锁 init ", test_spinlock_init, "concurrency"},
    {" 自旋锁加 unlock ", test_spinlock_lock_unlock, "concurrency"},
    {" mutex basic ", test_mutex_basic, "concurrency"},
    {" mutex 可重入 ", test_mutex_reentrant, "concurrency"},
    {" mutex error all 者 ", test_mutex_wrong_owner, "concurrency"},
    {" semaphore basic ", test_semaphore_basic, "concurrency"},
    {" semaphore 零 ", test_semaphore_zero, "concurrency"},
    {" semaphore max ", test_semaphore_max, "concurrency"},
    {" 读写锁多读者 ", test_rwlock_multiple_readers, "concurrency"},
    {" 读写锁写独占 ", test_rwlock_write_exclusive, "concurrency"},
    {" 原子加减 ", test_atomic_add_sub, "concurrency"},
    {" 原子 CAS", test_atomic_cas, "concurrency"},
    {" 屏障 ", test_barrier, "concurrency"},
};
static const kunit_suite_t kunit_concurrency_suite = {
    " concurrent & sync ", concurrency_tests, 13
};

void kunit_concurrency_register(void)
{
    kunit_register_suite(&kunit_concurrency_suite);
    vga_puts("[KUNIT] concurrent & sync test suite registered(13 cases)\n");
}
