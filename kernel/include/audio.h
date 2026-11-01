#ifndef AUDIO_H
#define AUDIO_H

#include <stdint.h>

#define AUDIO_MAX_DEVICES 4
#define AUDIO_MAX_BUFFERS 8

/* 音频格式 */
typedef enum {
    AUDIO_FMT_U8 = 0,
    AUDIO_FMT_S16_LE,
    AUDIO_FMT_S24_LE,
    AUDIO_FMT_S32_LE,
    AUDIO_FMT_FLOAT,
} audio_format_t;

/* 音频流方向 */
typedef enum {
    AUDIO_DIR_PLAYBACK = 0,
    AUDIO_DIR_CAPTURE,
} audio_dir_t;

/* PCM流状态 */
typedef enum {
    AUDIO_STATE_OPEN = 0,
    AUDIO_STATE_SETUP,
    AUDIO_STATE_PREPARED,
    AUDIO_STATE_RUNNING,
    AUDIO_STATE_PAUSED,
    AUDIO_STATE_SUSPENDED,
    AUDIO_STATE_DISCONNECTED,
} audio_state_t;

/* 音频缓冲区 */
typedef struct {
    uint8_t *data;
    uint32_t size;
    uint32_t used;
    int in_use;
} audio_buffer_t;

/* PCM流 */
typedef struct {
    int used;
    int stream_id;
    audio_dir_t direction;
    audio_format_t format;
    uint32_t sample_rate;
    uint8_t channels;
    uint32_t period_size;
    uint32_t buffer_size;
    audio_state_t state;
    audio_buffer_t buffers[AUDIO_MAX_BUFFERS];
    int buffer_count;
    int current_buffer;
    uint64_t total_samples;
    uint32_t volume;  /* 0-100 */
    void (*callback)(void *data, uint32_t size);
} audio_pcm_t;

/* 混音器控制 */
typedef struct {
    const char *name;
    int min;
    int max;
    int current;
    int muted;
} audio_mixer_ctl_t;

/* 音频设备 */
typedef struct {
    int used;
    const char *name;
    int type;  /* 0=AC97, 1=HDA, 2=I2S, 3=USB */
    uint32_t io_base;
    int irq;
    audio_pcm_t pcm_playback;
    audio_pcm_t pcm_capture;
    audio_mixer_ctl_t mixer[8];
    int mixer_count;
    int initialized;
} audio_device_t;

/* 初始化音频子系统 */
void audio_init(void);

/* 打开PCM流 */
int audio_pcm_open(int device, audio_dir_t dir, audio_format_t fmt,
                    uint32_t rate, uint8_t channels);

/* 关闭PCM流 */
int audio_pcm_close(int stream_id);

/* 写入播放数据 */
int audio_pcm_write(int stream_id, const void *data, uint32_t size);

/* 读取录制数据 */
int audio_pcm_read(int stream_id, void *data, uint32_t size);

/* 开始流 */
int audio_pcm_start(int stream_id);

/* 停止流 */
int audio_pcm_stop(int stream_id);

/* 暂停流 */
int audio_pcm_pause(int stream_id);

/* 恢复流 */
int audio_pcm_resume(int stream_id);

/* 设置音量 */
int audio_set_volume(int stream_id, int volume);

/* 获取音量 */
int audio_get_volume(int stream_id);

/* 设置静音 */
int audio_set_mute(int stream_id, int mute);

/* 获取延迟 */
uint32_t audio_get_delay(int stream_id);

/* 混音器控制 */
int audio_mixer_set(const char *name, int value);
int audio_mixer_get(const char *name);

/* 生成正弦波 (测试用) */
int audio_generate_tone(int stream_id, int freq, int duration_ms);

/* 音频中断处理 */
void audio_handle_irq(void);

#endif /* AUDIO_H */
