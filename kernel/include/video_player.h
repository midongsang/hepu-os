#ifndef VIDEO_PLAYER_H
#define VIDEO_PLAYER_H

#include <stdint.h>

/* ============================================================================
 * 赫普OS 视频播放器
 * 播放控制 / 字幕 / 倍速 / 画质
 * ========================================================================== */

#define VP_MAX_VIDEOS 256
#define VP_MAX_PLAYLISTS 32
#define VP_MAX_SUBTITLES 16
#define VP_TITLE_LEN 128
#define VP_PATH_LEN 256

/* 播放状态 */
typedef enum {
    VP_STATE_STOPPED = 0,
    VP_STATE_PLAYING,
    VP_STATE_PAUSED,
    VP_STATE_BUFFERING,
    VP_STATE_ERROR,
} vp_state_t;

/* 视频格式 */
typedef enum {
    VP_FORMAT_MP4 = 0,
    VP_FORMAT_MKV,
    VP_FORMAT_AVI,
    VP_FORMAT_MOV,
    VP_FORMAT_WEBM,
    VP_FORMAT_FLV,
    VP_FORMAT_TS,
    VP_FORMAT_UNKNOWN,
} vp_format_t;

/* 视频编码 */
typedef enum {
    VP_CODEC_H264 = 0,
    VP_CODEC_H265,
    VP_CODEC_VP9,
    VP_CODEC_AV1,
    VP_CODEC_MPEG4,
    VP_CODEC_UNKNOWN,
} vp_codec_t;

/* 视频信息 */
typedef struct {
    int used;
    int id;
    char title[VP_TITLE_LEN];
    char path[VP_PATH_LEN];
    vp_format_t format;
    vp_codec_t video_codec;
    uint32_t width;
    uint32_t height;
    uint32_t fps;
    uint64_t duration;  /* 毫秒 */
    uint64_t size;
    int bitrate;  /* kbps */
    int audio_channels;
    int audio_sample_rate;
    int year;
    char genre[32];
    char director[64];
    int favorite;
    int play_count;
    uint64_t last_played;
    uint64_t resume_position;  /* 续播位置 */
    uint8_t thumbnail[128];
} vp_video_t;

/* 字幕 */
typedef struct {
    int used;
    int id;
    char language[16];
    char name[64];
    char path[VP_PATH_LEN];
    int format;  /* 0=SRT,1=ASS,2=SSA,3=VTT */
    int enabled;
    int delay;  /* 毫秒偏移 */
} vp_subtitle_t;

/* 字幕行 */
typedef struct {
    uint64_t start;
    uint64_t end;
    char text[512];
} vp_subtitle_line_t;

/* 播放列表 */
typedef struct {
    int used;
    int id;
    char name[64];
    int video_ids[128];
    int video_count;
} vp_playlist_t;

/* 视频播放器状态 */
typedef struct {
    int initialized;
    int enabled;
    /* 播放 */
    vp_state_t state;
    int current_video;
    int current_playlist;
    uint64_t current_position;
    int volume;
    int muted;
    float playback_speed;  /* 0.25 - 4.0 */
    int brightness;  /* 0-100 */
    int contrast;  /* 0-100 */
    int saturation;  /* 0-100 */
    /* 显示 */
    int fullscreen;
    int aspect_ratio;  /* 0=自动,1=16:9,2=4:3,3=填充,4=原始 */
    int rotation;  /* 0,90,180,270 */
    /* 媒体库 */
    vp_video_t videos[VP_MAX_VIDEOS];
    int video_count;
    vp_playlist_t playlists[VP_MAX_PLAYLISTS];
    int playlist_count;
    /* 字幕 */
    vp_subtitle_t subtitles[VP_MAX_SUBTITLES];
    int subtitle_count;
    int current_subtitle;
    /* 统计 */
    uint64_t total_videos;
    uint64_t total_play_time;
    uint64_t total_plays;
} video_player_t;

/* 初始化 */
void video_player_init(void);

/* 播放控制 */
int vp_play(int video_id);
int vp_pause(void);
int vp_resume(void);
int vp_stop(void);
int vp_next(void);
int vp_prev(void);
int vp_seek(uint64_t position);
int vp_seek_relative(int64_t offset);
int vp_set_volume(int volume);
int vp_set_mute(int mute);
int vp_set_speed(float speed);

/* 媒体库 */
int vp_add_video(const char *path, const char *title);
int vp_remove_video(int id);
int vp_get_video(int id, vp_video_t *video);
int vp_list_videos(vp_video_t *videos, int max);
int vp_search(const char *query, vp_video_t *results, int max);
int vp_scan_directory(const char *path);

/* 播放列表 */
int vp_playlist_create(const char *name);
int vp_playlist_delete(int id);
int vp_playlist_add(int playlist_id, int video_id);
int vp_playlist_remove(int playlist_id, int video_id);
int vp_playlist_get(int id, vp_playlist_t *pl);
int vp_playlist_list(vp_playlist_t *playlists, int max);
int vp_play_playlist(int playlist_id);

/* 字幕 */
int vp_subtitle_load(const char *path, const char *language);
int vp_subtitle_select(int id);
int vp_subtitle_toggle(void);
int vp_subtitle_set_delay(int delay_ms);
int vp_subtitle_get_current(uint64_t position, char *buf, int size);
int vp_subtitle_list(vp_subtitle_t *subs, int max);

/* 显示设置 */
int vp_set_fullscreen(int fullscreen);
int vp_set_aspect_ratio(int ratio);
int vp_set_rotation(int degrees);
int vp_set_brightness(int level);
int vp_set_contrast(int level);
int vp_set_saturation(int level);

/* 状态查询 */
int vp_get_state(vp_state_t *state);
int vp_get_current_video(vp_video_t *video);
int vp_get_position(uint64_t *pos);
int vp_get_duration(uint64_t *dur);
int vp_get_stats(video_player_t *stats);

#endif /* VIDEO_PLAYER_H */
