#ifndef MEDIA_H
#define MEDIA_H

#include <stdint.h>

#define MEDIA_MAX_PLAYERS 8
#define MEDIA_MAX_PLAYLIST 256
#define MEDIA_MAX_TRACKS 64
#define MEDIA_EQ_BANDS 10

/* 播放器状态 */
typedef enum {
    MEDIA_STATE_IDLE = 0,
    MEDIA_STATE_INITIALIZED,
    MEDIA_STATE_PREPARING,
    MEDIA_STATE_PREPARED,
    MEDIA_STATE_STARTED,
    MEDIA_STATE_PAUSED,
    MEDIA_STATE_STOPPED,
    MEDIA_STATE_COMPLETED,
    MEDIA_STATE_ERROR,
} media_state_t;

/* 媒体类型 */
typedef enum {
    MEDIA_TYPE_AUDIO = 0,
    MEDIA_TYPE_VIDEO,
    MEDIA_TYPE_STREAM,
} media_type_t;

/* 音频编码 */
typedef enum {
    MEDIA_CODEC_AAC = 0,
    MEDIA_CODEC_MP3,
    MEDIA_CODEC_OPUS,
    MEDIA_CODEC_FLAC,
    MEDIA_CODEC_WAV,
    MEDIA_CODEC_OGG,
} media_audio_codec_t;

/* 视频编码 */
typedef enum {
    MEDIA_VIDEO_H264 = 0,
    MEDIA_VIDEO_H265,
    MEDIA_VIDEO_VP9,
    MEDIA_VIDEO_AV1,
} media_video_codec_t;

/* 媒体元数据 */
typedef struct {
    char title[128];
    char artist[128];
    char album[128];
    char genre[64];
    int year;
    int track_number;
    int total_tracks;
    uint64_t duration;  /* ms */
    int bitrate;        /* kbps */
    int sample_rate;
    int channels;
    int width;
    int height;
    media_type_t type;
    media_audio_codec_t audio_codec;
    media_video_codec_t video_codec;
} media_metadata_t;

/* 播放列表项 */
typedef struct {
    int used;
    char path[256];
    media_metadata_t metadata;
} media_track_t;

/* 播放列表 */
typedef struct {
    int used;
    int id;
    char name[64];
    media_track_t tracks[MEDIA_MAX_TRACKS];
    int track_count;
    int current_index;
    int repeat_mode;  /* 0=none, 1=all, 2=one */
    int shuffle;
} media_playlist_t;

/* 均衡器 */
typedef struct {
    int enabled;
    float bands[MEDIA_EQ_BANDS];  /* dB, -12 to +12 */
    float preamp;
} media_equalizer_t;

/* 播放器 */
typedef struct {
    int used;
    int id;
    media_state_t state;
    media_type_t type;
    char source[256];
    media_metadata_t metadata;
    uint64_t current_position;  /* ms */
    float playback_speed;       /* 0.5x - 2.0x */
    float volume;               /* 0.0 - 1.0 */
    int muted;
    int looping;
    media_equalizer_t equalizer;
    /* 回调 */
    void (*on_completion)(int player_id);
    void (*on_error)(int player_id, int error);
    void (*on_progress)(int player_id, uint64_t position);
    /* 统计 */
    uint64_t total_played;
    uint64_t buffering_time;
} media_player_t;

/* 初始化媒体子系统 */
void media_init(void);

/* 创建播放器 */
int media_player_create(media_type_t type);

/* 销毁播放器 */
int media_player_destroy(int player_id);

/* 设置数据源 */
int media_player_set_source(int player_id, const char *path);

/* 准备 (异步) */
int media_player_prepare(int player_id);

/* 开始播放 */
int media_player_start(int player_id);

/* 暂停 */
int media_player_pause(int player_id);

/* 停止 */
int media_player_stop(int player_id);

/* 跳转 */
int media_player_seek(int player_id, uint64_t position_ms);

/* 设置音量 */
int media_player_set_volume(int player_id, float volume);

/* 设置静音 */
int media_player_set_mute(int player_id, int mute);

/* 设置播放速度 */
int media_player_set_speed(int player_id, float speed);

/* 设置循环 */
int media_player_set_looping(int player_id, int loop);

/* 获取当前位置 */
uint64_t media_player_get_position(int player_id);

/* 获取时长 */
uint64_t media_player_get_duration(int player_id);

/* 获取状态 */
media_state_t media_player_get_state(int player_id);

/* 获取元数据 */
int media_player_get_metadata(int player_id, media_metadata_t *metadata);

/* 均衡器控制 */
int media_eq_set_band(int player_id, int band, float gain_db);
float media_eq_get_band(int player_id, int band);
int media_eq_enable(int player_id, int enable);
int media_eq_set_preset(int player_id, const char *preset);

/* 播放列表管理 */
int media_playlist_create(const char *name);
int media_playlist_destroy(int playlist_id);
int media_playlist_add_track(int playlist_id, const char *path);
int media_playlist_remove_track(int playlist_id, int index);
int media_playlist_get_current(int playlist_id, media_track_t *track);
int media_playlist_next(int playlist_id);
int media_playlist_prev(int playlist_id);
int media_playlist_set_repeat(int playlist_id, int mode);
int media_playlist_set_shuffle(int playlist_id, int shuffle);
int media_playlist_get_info(int playlist_id, media_playlist_t *info);

/* 媒体扫描 */
int media_scan_directory(const char *path);

/* 获取活跃播放器数 */
int media_player_count(void);

/* 媒体tick (由定时器调用) */
void media_tick(void);

#endif /* MEDIA_H */
