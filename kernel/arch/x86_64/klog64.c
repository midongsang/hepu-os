/* ============================================================================
 * 赫普OS - 64位内核环形日志
 * V0.55: 8KB 文本环形缓冲, 单写者多读者加锁保护
 * ========================================================================== */
#include "klog64.h"

#define KLOG_CAP 8192U

static char klog_buf[KLOG_CAP];
static uint32_t klog_head;   /* 下一个写入位置 */
static uint32_t klog_len;    /* 已用字节数 (<= KLOG_CAP) */
static volatile int klog_lock;

static void kl_lock(void)
{
    while (__sync_lock_test_and_set(&klog_lock, 1)) {
        __asm__ volatile("pause");
    }
}

static void kl_unlock(void)
{
    __sync_lock_release(&klog_lock);
}

static void kl_putc(char c)
{
    klog_buf[klog_head] = c;
    klog_head = (klog_head + 1) % KLOG_CAP;
    if (klog_len < KLOG_CAP) klog_len++;
}

static char kl_last(void)
{
    if (klog_len == 0) return 0;
    return klog_buf[(klog_head + KLOG_CAP - 1) % KLOG_CAP];
}

void klog64_log(const char *s)
{
    if (!s) return;
    kl_lock();
    while (*s) {
        char c = *s++;
        if (c == '\r') continue;
        kl_putc(c);
    }
    if (kl_last() != '\n') kl_putc('\n');
    kl_unlock();
}

uint32_t klog64_snapshot(char *out, uint32_t max)
{
    uint32_t n, start, i;

    if (!out || max == 0) return 0;

    kl_lock();
    n = (klog_len < max - 1) ? klog_len : (max - 1);
    /* 缓冲区满时从最旧字节开始; 截断时丢弃最早部分 */
    start = (klog_len == KLOG_CAP) ? klog_head : 0;
    if (klog_len > n) start = (start + (klog_len - n)) % KLOG_CAP;
    for (i = 0; i < n; i++) out[i] = klog_buf[(start + i) % KLOG_CAP];
    out[n] = 0;
    kl_unlock();

    return n;
}

void klog64_clear(void)
{
    kl_lock();
    klog_head = 0;
    klog_len = 0;
    kl_unlock();
}

uint32_t klog64_used(void)
{
    return klog_len;
}
