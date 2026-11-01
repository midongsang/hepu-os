#ifndef VOICE_RECORDER_H
#define VOICE_RECORDER_H

#include <stdint.h>

/* ============================================================================
 * 赫普OS 录音机
 * 录音 / 播放 / 编辑 / 格式转换
 * ========================================================================== */

#define VR_MAX_RECORDINGS 256
#define VR_TITLE_LEN 128
#define VR_PATH_LEN 256

/* 录音状态 */
typedef enum {
    VR_STATE_IDLE = 0,
    VR_STATE_RECORDING,
    VR_STATE_PAUSED,
    VR_STATE_PLAYING,
    VR_STATE_PROCESSING,
    VR_STATE_ERROR,
} vr_state_t;

/* 录音格式 */
typedef enum {
    VR_FORMAT_WAV = 0,
    VR_FORMAT_MP3,
    VR_FORMAT_AAC,
    VR_FORMAT_OGG,
    VR_FORMAT_FLAC,
    VR_FORMAT_M4A,
    VR_FORMAT_AMR,
} vr_format_t;

/* 录音质量 */
typedef enum {
    VR_QUALITY_LOW = 0,     /* 8kHz, 64kbps */
    VR_QUALITY_MEDIUM,      /* 22kHz, 128kbps */
    VR_QUALITY_HIGH,        /* 44.1kHz, 192kbps */
    VR_QUALITY_LOSSLESS,    /* 48kHz, FLAC */
} vr_quality_t;

/* 录音场景 */
typedef enum {
    VR_SCENE_GENERAL = 0,
    VR_SCENE_INTERVIEW,
    VR_SCENE_MEETING,
    VR_SCENE_VOICE_MEMO,
    VR_SCENE_MUSIC,
    VR_SCENE_PODCAST,
    VR_SCENE_NOISE_REDUCTION,
} vr_scene_t;

/* 录音文件 */
typedef struct {
    int used;
    int id;
    char title[VR_TITLE_LEN];
    char path[VR_PATH_LEN];
    vr_format_t format;
    vr_quality_t quality;
    vr_scene_t scene;
    uint64_t duration;  /* 毫秒 */
    uint64_t size;
    uint32_t sample_rate;
    int bitrate;
    int channels;
    uint64_t created;
    uint64_t modified;
    int favorite;
    int play_count;
    char tags[256];
    char description[512];
    uint8_t waveform[256];  /* 波形数据 */
} vr_recording_t;

/* 录音标记 */
typedef struct {
    int used;
    int id;
    int recording_id;
    uint64_t timestamp;
    char text[256];
    int color;
} vr_marker_t;

/* 录音机状态 */
typedef struct {
    int initialized;
    int enabled;
    /* 状态 */
    vr_state_t state;
    int current_recording;
    uint64_t record_position;
    uint64_t play_position;
    /* 设置 */
    vr_format_t format;
    vr_quality_t quality;
    vr_scene_t scene;
    int sample_rate;
    int bitrate;
    int channels;
    int gain;  /* 0-100 */
    int noise_reduction;
    int echo_cancellation;
    int auto_gain;
    int skip_silence;
    /* 存储 */
    char save_path[VR_PATH_LEN];
    int auto_save;
    int max_duration;  /* 秒, 0=无限 */
    /* 录音库 */
    vr_recording_t recordings[VR_MAX_RECORDINGS];
    int recording_count;
    /* 标记 */
    vr_marker_t markers[128];
    int marker_count;
    /* 统计 */
    uint64_t total_recordings;
    uint64_t total_record_time;
    uint64_t total_play_time;
} voice_recorder_t;

/* 初始化 */
void voice_recorder_init(void);

/* 录音控制 */
int vr_start_recording(const char *title);
int vr_pause_recording(void);
int vr_resume_recording(void);
int vr_stop_recording(void);
int vr_cancel_recording(void);
int vr_add_marker(const char *text);

/* 播放控制 */
int vr_play(int recording_id);
int vr_pause_playback(void);
int vr_resume_playback(void);
int vr_stop_playback(void);
int vr_seek(uint64_t position);
int vr_set_volume(int volume);

/* 录音管理 */
int vr_get_recording(int id, vr_recording_t *rec);
int vr_list_recordings(vr_recording_t *recs, int max);
int vr_delete_recording(int id);
int vr_rename_recording(int id, const char *title);
int vr_set_favorite(int id, int favorite);
int vr_search(const char *query, vr_recording_t *results, int max);
int vr_get_waveform(int id, uint8_t *waveform, int max);

/* 编辑 */
int vr_trim(int id, uint64_t start, uint64_t end);
int vr_split(int id, uint64_t position);
int vr_merge(int *ids, int count, const char *title);
int vr_convert_format(int id, vr_format_t format);
int vr_change_quality(int id, vr_quality_t quality);

/* 设置 */
int vr_set_format(vr_format_t format);
int vr_set_quality(vr_quality_t quality);
int vr_set_scene(vr_scene_t scene);
int vr_set_gain(int gain);
int vr_set_noise_reduction(int enable);
int vr_set_echo_cancellation(int enable);
int vr_set_auto_gain(int enable);
int vr_set_skip_silence(int enable);
int vr_set_save_path(const char *path);

/* 标记 */
int vr_marker_add(int recording_id, uint64_t timestamp, const char *text);
int vr_marker_remove(int id);
int vr_marker_list(int recording_id, vr_marker_t *markers, int max);

/* 状态查询 */
int vr_get_state(vr_state_t *state);
int vr_get_position(uint64_t *pos);
int vr_get_duration(uint64_t *dur);
int vr_get_stats(voice_recorder_t *stats);

#endif /* VOICE_RECORDER_H */
