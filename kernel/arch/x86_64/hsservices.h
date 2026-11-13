/* ============================================================================
 * 赫普OS - Hepu Desktop 服务集成
 * 音频服务、进程监控、文件监控
 * V0.10.169
 * ========================================================================== */

#ifndef HSSERVICES_H
#define HSSERVICES_H

#include <stdint.h>
#include <stddef.h>

/* 最大配置 */
#define HS_MAX_AUDIO_STREAMS  8
#define HS_MAX_WATCH_FILES    16
#define HS_MAX_PROC_INFO      32

/* 音频流 */
typedef struct {
    int id;
    int active;
    int volume;       /* 0-100 */
    int sample_rate;
    int channels;
    char name[32];
} hs_audio_stream_t;

/* 音频服务 */
typedef struct {
    int initialized;
    int master_volume;  /* 0-100 */
    int muted;
    int stream_count;
    hs_audio_stream_t streams[HS_MAX_AUDIO_STREAMS];
} hs_audio_service_t;

/* 进程信息 */
typedef struct {
    int pid;
    char name[32];
    int cpu_usage;    /* 百分比 */
    int mem_usage;    /* KB */
    int state;        /* 0=running, 1=sleeping, 2=zombie */
    /* V0.59.5: 任务管理器要显示优先级/所在核/累计运行时间, 原来这三个字段在
     * process64_stat_t 里有、到这一层被丢掉了, 界面上就只能显示 PID/NAME/CPU/MEM/STATE 五列。 */
    int priority;
    int core;
    uint64_t runtime; /* 累计运行 tick */
    int env_type;
} hs_process_info_t;

/* 进程监控 */
typedef struct {
    int process_count;
    hs_process_info_t processes[HS_MAX_PROC_INFO];
    int total_cpu;
    int total_mem;
    int mem_total;
    int mem_used;
    int mem_free;
} hs_process_monitor_t;

/* 文件监控 */
typedef struct {
    char path[128];
    int active;
    int last_mtime;
    void (*callback)(const char *path);
} hs_file_watch_t;

typedef struct {
    int watch_count;
    hs_file_watch_t watches[HS_MAX_WATCH_FILES];
} hs_file_monitor_t;

/* API */
int hs_audio_init(void);
void hs_audio_set_volume(int vol);
int hs_audio_get_volume(void);
void hs_audio_mute(void);
void hs_audio_unmute(void);
int hs_audio_is_muted(void);
int hs_audio_play_tone(int freq, int duration);
void hs_audio_render_indicator(int x, int y);

/* 进程监控 */
int hs_process_monitor_init(void);
void hs_process_monitor_update(void);
hs_process_info_t *hs_process_get_list(int *count);
int hs_process_get_cpu_usage(void);
int hs_process_get_mem_usage(void);
void hs_process_render_tasklist(int x, int y, int w, int h);

/* 文件监控 */
int hs_file_monitor_init(void);
int hs_file_watch_add(const char *path, void (*callback)(const char *));
void hs_file_watch_remove(const char *path);
void hs_file_monitor_poll(void);

/* 服务总控 */
int hs_services_init(void);
void hs_services_update(void);

#endif /* HSSERVICES_H */
