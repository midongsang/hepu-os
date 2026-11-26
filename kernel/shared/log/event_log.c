/* ============================================================================
 * 赫普OS 统一事件日志（32/64 共享）
 * ========================================================================== */
#include <stdint.h>
#include <stdarg.h>
#include <stddef.h>
#include "kstdio.h"
#include <string.h>

#include "event_log.h"

static event_log_entry_t ring[EVENT_LOG_CAP];
static volatile uint32_t ring_head;   /* 下一个写入槽 */
static volatile uint32_t ring_count;  /* 有效条数 */
static volatile uint32_t ring_seq;    /* 下一条序号 */
static volatile int ring_lock_word;
static event_log_clock_fn clock_source;
static event_log_pid_fn pid_source;
static event_log_sink_fn sink_source;
static int serial_mirror;

static uint64_t event_log_now(void)
{
    if (clock_source) return clock_source();
#if defined(__x86_64__) || defined(__i386__)
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
#else
    return 0;
#endif
}

static void ring_lock_acquire(void)
{
#if defined(__x86_64__) || defined(__i386__)
    while (__sync_lock_test_and_set(&ring_lock_word, 1)) {
        __asm__ volatile("pause");
    }
#else
    while (__sync_lock_test_and_set(&ring_lock_word, 1)) { }
#endif
}

static void ring_unlock_release(void)
{
    __sync_lock_release(&ring_lock_word);
}

static void copy_text(char *dst, uint32_t cap, const char *src)
{
    uint32_t i = 0;
    if (!dst || cap == 0) return;
    if (src) {
        while (src[i] && i + 1 < cap) {
            dst[i] = src[i];
            i++;
        }
    }
    dst[i] = 0;
}

void event_log_init(event_log_clock_fn clock_fn)
{
    ring_head = 0;
    ring_count = 0;
    ring_seq = 1;
    ring_lock_word = 0;
    clock_source = clock_fn;
    pid_source = NULL;
    sink_source = NULL;
    serial_mirror = 0;
    memset(ring, 0, sizeof(ring));
}

void event_log_set_clock(event_log_clock_fn clock_fn) { clock_source = clock_fn; }
void event_log_set_pid_source(event_log_pid_fn pid_fn) { pid_source = pid_fn; }
void event_log_set_sink(event_log_sink_fn sink_fn) { sink_source = sink_fn; }
void event_log_set_serial(int enabled) { serial_mirror = enabled ? 1 : 0; }

void event_log_write(int level, const char *source, uint32_t code,
                     const char *msg)
{
    uint32_t slot;
    uint32_t pid = pid_source ? pid_source() : 0;
    event_log_entry_t *e;

    if (level < EVENT_DEBUG) level = EVENT_DEBUG;
    if (level > EVENT_FATAL) level = EVENT_FATAL;

    ring_lock_acquire();
    slot = ring_head;
    e = &ring[slot];
    memset(e, 0, sizeof(*e));
    e->seq = ring_seq++;
    e->tick = event_log_now();
    e->level = (uint8_t)level;
    e->pid = pid;
    e->code = code;
    copy_text(e->source, sizeof(e->source), source ? source : "kernel");
    copy_text(e->msg, sizeof(e->msg), msg ? msg : "");
    e->source_len = (uint8_t)strlen(e->source);
    ring_head = (slot + 1) % EVENT_LOG_CAP;
    if (ring_count < EVENT_LOG_CAP) ring_count++;
    ring_unlock_release();

    if (serial_mirror) {
        extern void serial_puts(uint16_t, const char *);
        extern void serial_puthex(uint16_t, uint32_t);
        serial_puts(0x3F8, "[EVENT] ");
        serial_puts(0x3F8, event_log_level_name(level));
        serial_puts(0x3F8, " ");
        serial_puts(0x3F8, e->source);
        serial_puts(0x3F8, " pid=");
        serial_puthex(0x3F8, pid);
        serial_puts(0x3F8, " ");
        serial_puts(0x3F8, e->msg);
        serial_puts(0x3F8, "\n");
    }
}

void event_log_emit(int level, const char *source, uint32_t code,
                    const char *fmt, ...)
{
    char msg[EVENT_LOG_MSG_MAX];
    va_list args;
    int n;

    if (!fmt) return;
    va_start(args, fmt);
    n = vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);
    if (n < 0) msg[0] = 0;
    event_log_write(level, source, code, msg);
}

uint32_t event_log_count(void)
{
    return ring_count;
}

void event_log_clear(void)
{
    ring_lock_acquire();
    ring_head = 0;
    ring_count = 0;
    ring_unlock_release();
}

uint32_t event_log_snapshot(event_log_entry_t *out, uint32_t max)
{
    uint32_t i, start, n;
    if (!out || max == 0) return 0;

    ring_lock_acquire();
    n = ring_count < max ? ring_count : max;
    start = (ring_count == EVENT_LOG_CAP) ?
            ring_head : (ring_head + EVENT_LOG_CAP - ring_count) % EVENT_LOG_CAP;
    /* 取最近 n 条，保持时间顺序 */
    if (ring_count > n) start = (start + (ring_count - n)) % EVENT_LOG_CAP;
    for (i = 0; i < n; i++) out[i] = ring[(start + i) % EVENT_LOG_CAP];
    ring_unlock_release();
    return n;
}

const char *event_log_level_name(int level)
{
    switch (level) {
        case EVENT_DEBUG: return "DEBUG";
        case EVENT_INFO:  return "INFO";
        case EVENT_WARN:  return "WARN";
        case EVENT_ERROR: return "ERROR";
        case EVENT_FATAL: return "FATAL";
        default:          return "?";
    }
}

static int event_log_format_entry(char *dst, uint32_t cap,
                                  const event_log_entry_t *e)
{
    /* 32 位 kstdio 只支持单个 l，不能传 %llu；用两个 32 位十六进制
     * 表示 64 位 tick，保证 32/64 的 varargs 对齐一致。 */
    return snprintf(dst, cap,
                    "%08u %08x%08x %-5s %-12s pid=%u code=%u %s\n",
                    e->seq, (unsigned int)(e->tick >> 32),
                    (unsigned int)(e->tick & 0xFFFFFFFFu),
                    event_log_level_name(e->level), e->source,
                    e->pid, e->code, e->msg);
}

uint32_t event_log_format(char *out, uint32_t cap, int min_level)
{
    event_log_entry_t entries[EVENT_LOG_CAP];
    uint32_t n, i, used = 0, start;
    char line[EVENT_LOG_MSG_MAX + 96];

    if (!out || cap == 0) return 0;
    out[0] = 0;
    n = event_log_snapshot(entries, EVENT_LOG_CAP);

    /* 先算“最近多少条”能装进 cap，再从 start 开始按时间顺序输出。
     * 这样 4KB 的 ext2 sink 不会只留下最旧的启动行。 */
    start = n;
    for (i = n; i > 0; i--) {
        int len;
        if ((int)entries[i - 1].level < min_level) { start = i - 1; continue; }
        len = event_log_format_entry(line, sizeof(line), &entries[i - 1]);
        if (len < 0 || (uint32_t)len >= cap - used) break;
        used += (uint32_t)len;
        start = i - 1;
    }

    used = 0;
    for (i = start; i < n; i++) {
        int len;
        if ((int)entries[i].level < min_level) continue;
        len = event_log_format_entry(out + used, cap - used, &entries[i]);
        if (len < 0 || (uint32_t)len >= cap - used) break;
        used += (uint32_t)len;
    }
    out[used] = 0;
    return used;
}

void event_log_dump(int min_level)
{
    extern void vga_puts(const char *);
    static char text[8192];
    event_log_format(text, sizeof(text), min_level);
    vga_puts("\n========== event log (journal) ==========\n");
    vga_puts(text);
    vga_puts("========== end of event log ==========\n");
}

int event_log_save(void)
{
    static char text[4096];
    uint32_t n;
    if (!sink_source) return -2;
    n = event_log_format(text, sizeof(text), EVENT_DEBUG);
    return sink_source(text, n);
}
