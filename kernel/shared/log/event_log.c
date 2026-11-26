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
static const event_log_file_ops_t *file_ops;
static int serial_mirror;
static uint32_t rotate_threshold = EVENT_LOG_ROTATE_BYTES;

/* emergency 独立定长环：无锁，只服务异常/崩溃上下文。 */
static event_log_entry_t emerg_ring[EVENT_EMERG_CAP];
static volatile uint32_t emerg_head;
static volatile uint32_t emerg_count;
static volatile uint32_t emerg_seq;
static volatile uint32_t emerg_drops;
static event_log_entry_t emerg_stat;  /* 溢出统计条目（原地更新） */

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
    file_ops = NULL;
    serial_mirror = 0;
    rotate_threshold = EVENT_LOG_ROTATE_BYTES;
    emerg_head = 0;
    emerg_count = 0;
    emerg_seq = 1;
    emerg_drops = 0;
    memset(ring, 0, sizeof(ring));
    memset(emerg_ring, 0, sizeof(emerg_ring));
    memset(&emerg_stat, 0, sizeof(emerg_stat));
}

void event_log_set_clock(event_log_clock_fn clock_fn) { clock_source = clock_fn; }
void event_log_set_pid_source(event_log_pid_fn pid_fn) { pid_source = pid_fn; }
void event_log_set_file_ops(const event_log_file_ops_t *ops) { file_ops = ops; }
void event_log_set_serial(int enabled) { serial_mirror = enabled ? 1 : 0; }

void event_log_test_set_rotate_threshold(uint32_t bytes)
{
    rotate_threshold = (bytes == 0) ? EVENT_LOG_ROTATE_BYTES : bytes;
}

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

/* 异常/崩溃上下文专用：不拿自旋锁、不走变参格式化。
 * 消息由调用方手工拼好后传入；pid 固定记 0（此时 current 可能不可靠）。 */
static void emerg_update_stat(void)
{
    char msg[EVENT_LOG_MSG_MAX];
    char tmp[11];
    uint32_t v = emerg_drops;
    int n = 0, d = 0;
    const char *p = "[EMERG-STAT] emergency overflow drops=";

    while (*p && n < (int)sizeof(msg) - 12) msg[n++] = *p++;
    if (v == 0) {
        msg[n++] = '0';
    } else {
        while (v > 0) { tmp[d++] = (char)('0' + (v % 10)); v /= 10; }
        while (d > 0) msg[n++] = tmp[--d];
    }
    msg[n] = 0;

    emerg_stat.seq = emerg_seq++;
    emerg_stat.tick = event_log_now();
    emerg_stat.level = EVENT_WARN;
    emerg_stat.pid = 0;
    emerg_stat.code = 78;
    copy_text(emerg_stat.source, sizeof(emerg_stat.source), "emergency");
    copy_text(emerg_stat.msg, sizeof(emerg_stat.msg), msg);
    emerg_stat.source_len = (uint8_t)strlen(emerg_stat.source);
}

void event_log_emergency_write(int level, const char *source, uint32_t code,
                               const char *msg)
{
    uint32_t slot;
    event_log_entry_t *e;

    if (level < EVENT_DEBUG) level = EVENT_DEBUG;
    if (level > EVENT_FATAL) level = EVENT_FATAL;

    slot = emerg_head;
    e = &emerg_ring[slot];
    memset(e, 0, sizeof(*e));
    e->seq = emerg_seq++;
    e->tick = event_log_now();
    e->level = (uint8_t)level;
    e->pid = 0;
    e->code = code;
    copy_text(e->source, sizeof(e->source), source ? source : "kernel");
    copy_text(e->msg, sizeof(e->msg), msg ? msg : "");
    e->source_len = (uint8_t)strlen(e->source);

    if (emerg_count < EVENT_EMERG_CAP) {
        emerg_count++;
    } else {
        emerg_drops++;
        emerg_update_stat();
    }
    emerg_head = (slot + 1) % EVENT_EMERG_CAP;
}

uint32_t event_log_emergency_count(void) { return emerg_count; }
uint32_t event_log_emergency_drop_count(void) { return emerg_drops; }

uint32_t event_log_emergency_snapshot(event_log_entry_t *out, uint32_t max)
{
    uint32_t i, start, n;
    if (!out || max == 0) return 0;

    n = emerg_count < max ? emerg_count : max;
    start = (emerg_count == EVENT_EMERG_CAP) ? emerg_head :
            (emerg_head + EVENT_EMERG_CAP - emerg_count) % EVENT_EMERG_CAP;
    for (i = 0; i < n; i++)
        out[i] = emerg_ring[(start + i) % EVENT_EMERG_CAP];

    if (emerg_drops > 0 && n < max) out[n++] = emerg_stat;
    return n;
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

int event_log_file_read(const char *path, uint8_t *buf, uint32_t max,
                        uint32_t *out_len)
{
    if (!file_ops || !file_ops->read) return -2;
    return file_ops->read(path, buf, max, out_len);
}

int event_log_file_size(const char *path, uint32_t *out_size)
{
    if (!file_ops || !file_ops->size) return -2;
    return file_ops->size(path, out_size);
}

int event_log_file_write(const char *path, const uint8_t *buf, uint32_t len)
{
    if (!file_ops || !file_ops->write) return -2;
    return file_ops->write(path, buf, len);
}

int event_log_file_remove(const char *path)
{
    if (!file_ops || !file_ops->remove) return -2;
    return file_ops->remove(path);
}

int event_log_persist(const char *new_text, uint32_t new_len)
{
    /* 注意：现有 ext2 驱动没有“原地追加写”原语，write_file 一律整文件重写。
     * 因此这里走「读旧文件 → 内存拼接 → 整文件写回」；代价是每次落盘都要
     * 把旧文件整条读回并整块重写，旧内容受阈值缓冲上限约束（见轮转阈值）。 */
    static uint8_t old_buf[EVENT_LOG_ROTATE_BYTES];
    static uint8_t mix_buf[EVENT_LOG_ROTATE_BYTES + EVENT_LOG_TEXT_MAX];
    uint32_t th = rotate_threshold;
    uint32_t old_len = 0, old_read = 0, mix_len;
    int rotate;

    if (!file_ops || !new_text) return -2;
    if (new_len > EVENT_LOG_TEXT_MAX) new_len = EVENT_LOG_TEXT_MAX;

    if (file_ops->size && file_ops->size(EVENT_LOG_PATH_MAIN, &old_len) != 0) {
        old_len = 0;  /* 文件不存在视为空 */
    }
    if (old_len > th) old_len = th;

    rotate = (old_len >= th);
    if (old_len > 0 && file_ops->read) {
        if (file_ops->read(EVENT_LOG_PATH_MAIN, old_buf, th, &old_read) != 0) {
            old_read = 0;
        }
    }
    if (!rotate && old_read + new_len > th) rotate = 1;

    if (rotate) {
        /* 旧主文件降级为 .1（只保留一级旧档），新内容写主文件。 */
        if (old_read > 0 && file_ops->write) {
            file_ops->write(EVENT_LOG_PATH_ROT1, old_buf, old_read);
        }
        if (!file_ops->write) return -2;
        return file_ops->write(EVENT_LOG_PATH_MAIN,
                               (const uint8_t *)new_text, new_len);
    }

    if (old_read > 0) memcpy(mix_buf, old_buf, old_read);
    memcpy(mix_buf + old_read, new_text, new_len);
    mix_len = old_read + new_len;
    if (!file_ops->write) return -2;
    return file_ops->write(EVENT_LOG_PATH_MAIN, mix_buf, mix_len);
}

int event_log_save(void)
{
    static char text[EVENT_LOG_TEXT_MAX];
    uint32_t n;
    n = event_log_format(text, sizeof(text), EVENT_DEBUG);
    return event_log_persist(text, n);
}
