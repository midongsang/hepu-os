/* ============================================================================
 * 赫普OS - 64位内核环形日志
 * V0.55: 8KB 文本环形缓冲, 单写者多读者加锁保护
 * ========================================================================== */
#include "klog64.h"
#include "event_log.h"

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
    event_log_write(EVENT_INFO, "kernel64", 0, s);
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

/* 统一事件日志的 64 位平台适配。 */
static uint64_t event_log_clock64(void)
{
    extern uint64_t pit64_get_ticks(void);
    return pit64_get_ticks();
}

static uint32_t event_log_pid64(void)
{
    extern uint32_t process64_get_current_pid(void);
    return process64_get_current_pid();
}

static int event_log_sink64(const char *text, uint32_t len)
{
    extern int ext2_64_write_file(const char *path, const uint8_t *data, uint32_t size);
    return ext2_64_write_file("/hepu_events.log", (const uint8_t *)text, len);
}

void event_log_platform_init64(void)
{
    event_log_init(event_log_clock64);
    event_log_set_pid_source(event_log_pid64);
    event_log_set_sink(event_log_sink64);
    event_log_emit(EVENT_INFO, "boot64", 1, "64-bit kernel entry");
}
