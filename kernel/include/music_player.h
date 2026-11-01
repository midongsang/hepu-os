#ifndef MUSIC_PLAYER_H
#define MUSIC_PLAYER_H

#include <stdint.h>

/* ============================================================================
 * 赫普OS 音乐播放器
 * 播放列表 / 歌词 / 均衡器 / 播放控制
 * ========================================================================== */

#define MP_MAX_TRACKS 512
#define MP_MAX_PLAYLISTS 64
#define MP_MAX_LYRICS 256
#define MP_TITLE_LEN 128
#define MP_ARTIST_LEN 64
#define MP_ALBUM_LEN 64
#define MP_PATH_LEN 256

/* 播放状态 */
typedef enum {
    MP_STATE_STOPPED = 0,
    MP_STATE_PLAYING,
    MP_STATE_PAUSED,
    MP_STATE_BUFFERING,
    MP_STATE_ERROR,
} mp_state_t;

/* 播放模式 */
typedef enum {
    MP_MODE_SEQUENCE = 0,  /* 顺序播放 */
    MP_MODE_REPEAT_ALL,    /* 列表循环 */
    MP_MODE_REPEAT_ONE,    /* 单曲循环 */
    MP_MODE_SHUFFLE,       /* 随机播放 */
} mp_play_mode_t;

/* 音频格式 */
typedef enum {
    MP_FORMAT_MP3 = 0,
    MP_FORMAT_FLAC,
    MP_FORMAT_WAV,
    MP_FORMAT_AAC,
    MP_FORMAT_OGG,
    MP_FORMAT_OPUS,
    MP_FORMAT_M4A,
    MP_FORMAT_UNKNOWN,
} mp_format_t;

/* 歌曲 */
typedef struct {
    int used;
    int id;
    char title[MP_TITLE_LEN];
    char artist[MP_ARTIST_LEN];
    char album[MP_ALBUM_LEN];
    char path[MP_PATH_LEN];
    mp_format_t format;
    uint64_t duration;  /* 毫秒 */
    uint64_t size;
    uint32_t sample_rate;
    int bitrate;  /* kbps */
    int channels;
    int track_number;
    int year;
    char genre[32];
    int favorite;
    int play_count;
    uint64_t last_played;
    uint8_t cover_art[128];  /* 封面缩略图 */
} mp_track_t;

/* 播放列表 */
typedef struct {
    int used;
    int id;
    char name[64];
    int track_ids[256];
    int track_count;
    int is_system;  /* 系统播放列表不可删除 */
    uint64_t created;
    uint64_t modified;
} mp_playlist_t;

/* 歌词行 */
typedef struct {
    uint64_t timestamp;  /* 毫秒 */
    char text[256];
} mp_lyric_line_t;

/* 歌词 */
typedef struct {
    int used;
    int track_id;
    char title[MP_TITLE_LEN];
    char artist[MP_ARTIST_LEN];
    mp_lyric_line_t lines[MP_MAX_LYRICS];
    int line_count;
    int offset;  /* 毫秒偏移 */
} mp_lyrics_t;

/* 均衡器频段 */
typedef struct {
    int band;  /* 频段索引 */
    int freq;  /* Hz */
    int gain;  /* -12 to +12 dB */
} mp_eq_band_t;

/* 音乐播放器状态 */
typedef struct {
    int initialized;
    int enabled;
    /* 播放 */
    mp_state_t state;
    mp_play_mode_t mode;
    int current_track;
    int current_playlist;
    uint64_t current_position;  /* 毫秒 */
    int volume;  /* 0-100 */
    int muted;
    /* 媒体库 */
    mp_track_t tracks[MP_MAX_TRACKS];
    int track_count;
    mp_playlist_t playlists[MP_MAX_PLAYLISTS];
    int playlist_count;
    /* 歌词 */
    mp_lyrics_t current_lyrics;
    int lyrics_enabled;
    /* 均衡器 */
    mp_eq_band_t eq_bands[10];
    int eq_enabled;
    int eq_preset;  /* 0=自定义,1=流行,2=摇滚,3=古典,4=爵士,5=人声,6=重低音 */
    /* 音效 */
    int bass_boost;  /* 0-100 */
    int virtualizer;  /* 0-100 */
    int reverb;  /* 0-100 */
    /* 统计 */
    uint64_t total_tracks;
    uint64_t total_play_time;
    uint64_t total_plays;
} music_player_t;

/* 初始化 */
void music_player_init(void);

/* 播放控制 */
int mp_play(int track_id);
int mp_pause(void);
int mp_resume(void);
int mp_stop(void);
int mp_next(void);
int mp_prev(void);
int mp_seek(uint64_t position);
int mp_set_volume(int volume);
int mp_set_mute(int mute);
int mp_set_play_mode(mp_play_mode_t mode);

/* 媒体库 */
int mp_add_track(const char *path, const char *title, const char *artist, const char *album);
int mp_remove_track(int id);
int mp_get_track(int id, mp_track_t *track);
int mp_list_tracks(mp_track_t *tracks, int max);
int mp_list_by_artist(const char *artist, mp_track_t *tracks, int max);
int mp_list_by_album(const char *album, mp_track_t *tracks, int max);
int mp_search(const char *query, mp_track_t *results, int max);
int mp_scan_directory(const char *path);

/* 播放列表 */
int mp_playlist_create(const char *name);
int mp_playlist_delete(int id);
int mp_playlist_add_track(int playlist_id, int track_id);
int mp_playlist_remove_track(int playlist_id, int track_id);
int mp_playlist_get(int id, mp_playlist_t *pl);
int mp_playlist_list(mp_playlist_t *playlists, int max);
int mp_play_playlist(int playlist_id);

/* 歌词 */
int mp_lyrics_load(int track_id, const char *lyrics_text);
int mp_lyrics_get_current(uint64_t position, char *buf, int size);
int mp_lyrics_toggle(void);
int mp_lyrics_set_offset(int offset);

/* 均衡器 */
int mp_eq_set_band(int band, int gain);
int mp_eq_get_band(int band, mp_eq_band_t *eq);
int mp_eq_set_preset(int preset);
int mp_eq_toggle(void);
int mp_eq_list_presets(char names[][32], int max);

/* 音效 */
int mp_set_bass_boost(int level);
int mp_set_virtualizer(int level);
int mp_set_reverb(int level);

/* 状态查询 */
int mp_get_state(mp_state_t *state);
int mp_get_current_track(mp_track_t *track);
int mp_get_position(uint64_t *pos);
int mp_get_duration(uint64_t *dur);
int mp_get_stats(music_player_t *stats);

#endif /* MUSIC_PLAYER_H */
