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
#define EVENT_LOG_TEXT_MAX  4096

/* 落盘路径与轮转阈值：32/64 共用同一字面量口径，只在本头文件收敛一次。
 * 主文件 hepu_events.log；超限后轮转成 hepu_events.log.1（只保留一级旧档）。 */
#define EVENT_LOG_FILE_MAIN   "hepu_events.log"
#define EVENT_LOG_FILE_ROT1   "hepu_events.log.1"
#define EVENT_LOG_PATH_MAIN   "/hepu_events.log"
#define EVENT_LOG_PATH_ROT1   "/hepu_events.log.1"
#define EVENT_LOG_ROTATE_BYTES 8192u

/* emergency 异常上下文缓冲条数（无锁定长环）。 */
#define EVENT_EMERG_CAP 16

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

/* 平台文件回调（追加/轮转落盘用）：
 *  read  : 读到的字节写入 buf（上限 max），*out_len 为读到字节数；文件不存在
 *          视为成功且 *out_len=0；失败返回 -1。
 *  write : 整文件写入 len 字节；返回写入字节数或负数。
 *  size  : *out_size 为文件字节数；文件不存在返回 -1。 */
typedef struct {
    int  (*read)(const char *path, uint8_t *buf, uint32_t max, uint32_t *out_len);
    int  (*write)(const char *path, const uint8_t *buf, uint32_t len);
    int  (*size)(const char *path, uint32_t *out_size);
    int  (*remove)(const char *path);
} event_log_file_ops_t;

void event_log_init(event_log_clock_fn clock_fn);
void event_log_set_clock(event_log_clock_fn clock_fn);
void event_log_set_pid_source(event_log_pid_fn pid_fn);
void event_log_set_file_ops(const event_log_file_ops_t *ops);
void event_log_set_serial(int enabled);

void event_log_emit(int level, const char *source, uint32_t code,
                    const char *fmt, ...);
void event_log_write(int level, const char *source, uint32_t code,
                     const char *msg);

/* emergency 变体：异常/崩溃上下文专用。
 * 无自旋锁、无变参格式化、定长缓冲；溢出条目计入 drop 计数并写一条统计条目。
 * 调用方必须先把消息拼好（手工拼接/定长缓冲），禁止在中断/崩溃上下文里 vsnprintf。 */
void event_log_emergency_write(int level, const char *source, uint32_t code,
                               const char *msg);
uint32_t event_log_emergency_count(void);
uint32_t event_log_emergency_drop_count(void);
uint32_t event_log_emergency_snapshot(event_log_entry_t *out, uint32_t max);

uint32_t event_log_count(void);
uint32_t event_log_snapshot(event_log_entry_t *out, uint32_t max);
const char *event_log_level_name(int level);

/* 格式化最近事件；min_level 为最低级别。返回写入字节数（不含结尾 0）。 */
uint32_t event_log_format(char *out, uint32_t cap, int min_level);
void event_log_dump(int min_level);
void event_log_clear(void);

/* 把 new_text 经平台文件回调追加落盘；超阈值则把旧主文件轮转进 .1。
 * 未注册文件回调返回 -2。返回写入主文件的字节数或负数。 */
int event_log_persist(const char *new_text, uint32_t new_len);
/* 通过平台回调落盘当前内存环；未注册返回 -2。 */
int event_log_save(void);

/* 测试钩子：临时下调轮转阈值（字节）；传 0 恢复默认 EVENT_LOG_ROTATE_BYTES。 */
void event_log_test_set_rotate_threshold(uint32_t bytes);

/* 经平台回调读文件/取大小/整文件写回，供 KUnit 与诊断命令核对落盘内容。 */
int event_log_file_read(const char *path, uint8_t *buf, uint32_t max,
                        uint32_t *out_len);
int event_log_file_size(const char *path, uint32_t *out_size);
int event_log_file_write(const char *path, const uint8_t *buf, uint32_t len);
int event_log_file_remove(const char *path);

#endif /* HEPU_EVENT_LOG_H */
