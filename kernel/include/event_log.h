/* ============================================================================
 * 赫普OS 统一事件日志（32/64 共享）
 *
 * 目标：把“启动日志 / 内核日志 / 崩溃日志 / 服务日志”收敛到同一条记录流。
 * 记录带序号、时间源、级别、来源、pid、事件码和消息；内存环形缓冲可查询，
 * 平台层可注册落盘 sink（ext2 就绪后写持久文件）。
 * ========================================================================== */
#ifndef HEPU_EVENT_LOG_H
#define HEPU_EVENT_LOG_H

#include <stdint.h>

#define EVENT_LOG_CAP       256
#define EVENT_LOG_MSG_MAX   144
#define EVENT_LOG_SRC_MAX   16

typedef enum {
    EVENT_DEBUG = 0,
    EVENT_INFO  = 1,
    EVENT_WARN  = 2,
    EVENT_ERROR = 3,
    EVENT_FATAL = 4
} event_level_t;

typedef struct {
    uint32_t seq;
    uint64_t tick;          /* 平台时间源；默认 TSC */
    uint8_t  level;
    uint8_t  source_len;
    uint16_t reserved;
    uint32_t pid;
    uint32_t code;
    char     source[EVENT_LOG_SRC_MAX];
    char     msg[EVENT_LOG_MSG_MAX];
} event_log_entry_t;

/* 平台时间源；返回单调 tick。不设置时用 rdtsc。 */
typedef uint64_t (*event_log_clock_fn)(void);
/* 当前 pid 来源；不设置时 pid=0。 */
typedef uint32_t (*event_log_pid_fn)(void);
/* 落盘 sink：接收已格式化的文本，返回写入字节数或负数。 */
typedef int (*event_log_sink_fn)(const char *text, uint32_t len);

void event_log_init(event_log_clock_fn clock_fn);
void event_log_set_clock(event_log_clock_fn clock_fn);
void event_log_set_pid_source(event_log_pid_fn pid_fn);
void event_log_set_sink(event_log_sink_fn sink_fn);
void event_log_set_serial(int enabled);

void event_log_emit(int level, const char *source, uint32_t code,
                    const char *fmt, ...);
void event_log_write(int level, const char *source, uint32_t code,
                     const char *msg);

uint32_t event_log_count(void);
uint32_t event_log_snapshot(event_log_entry_t *out, uint32_t max);
const char *event_log_level_name(int level);

/* 格式化最近事件；min_level 为最低级别。返回写入字节数（不含结尾 0）。 */
uint32_t event_log_format(char *out, uint32_t cap, int min_level);
void event_log_dump(int min_level);
void event_log_clear(void);

/* 通过平台 sink 落盘；未注册 sink 返回 -2。 */
int event_log_save(void);

#endif /* HEPU_EVENT_LOG_H */
