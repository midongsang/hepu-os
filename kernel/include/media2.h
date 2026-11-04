#ifndef MEDIA2_H
#define MEDIA2_H

#include <stdint.h>

#define MEDIA2_MAX_SESSIONS 16
#define MEDIA2_MAX_EFFECTS 8
#define MEDIA2_MAX_AUDIO_FOCUS_STACK 8

/* 音频焦点类型 */
typedef enum {
    AUDIO_FOCUS_NONE = 0,
    AUDIO_FOCUS_GAIN,
    AUDIO_FOCUS_GAIN_TRANSIENT,
    AUDIO_FOCUS_GAIN_TRANSIENT_MAY_DUCK,
    AUDIO_FOCUS_LOSS,
    AUDIO_FOCUS_LOSS_TRANSIENT,
    AUDIO_FOCUS_LOSS_TRANSIENT_CAN_DUCK,
} audio_focus_t;

/* 音频流类型 */
typedef enum {
    STREAM_VOICE_CALL = 0,
    STREAM_SYSTEM,
    STREAM_RING,
    STREAM_MUSIC,
    STREAM_ALARM,
    STREAM_NOTIFICATION,
    STREAM_DTMF,
    STREAM_ACCESSIBILITY,
} stream_type_t;

/* 音频使用场景 */
typedef enum {
    USAGE_UNKNOWN = 0,
    USAGE_MEDIA,
    USAGE_VOICE_COMMUNICATION,
    USAGE_VOICE_COMMUNICATION_SIGNALLING,
    USAGE_ALARM,
    USAGE_NOTIFICATION,
    USAGE_NOTIFICATION_RINGTONE,
    USAGE_NOTIFICATION_COMMUNICATION_REQUEST,
    USAGE_NOTIFICATION_COMMUNICATION_INSTANT,
    USAGE_NOTIFICATION_COMMUNICATION_DELAYED,
    USAGE_NOTIFICATION_EVENT,
    USAGE_ASSISTANCE_ACCESSIBILITY,
    USAGE_ASSISTANCE_NAVIGATION_GUIDANCE,
    USAGE_ASSISTANCE_SONIFICATION,
    USAGE_GAME,
    USAGE_ASSISTANT,
} audio_usage_t;

/* 音频内容类型 */
typedef enum {
    CONTENT_TYPE_UNKNOWN = 0,
    CONTENT_TYPE_SPEECH,
    CONTENT_TYPE_MUSIC,
    CONTENT_TYPE_MOVIE,
    CONTENT_TYPE_SONIFICATION,
} audio_content_type_t;

/* 音效类型 */
typedef enum {
    EFFECT_EQUALIZER = 0,
    EFFECT_BASS_BOOST,
    EFFECT_VIRTUALIZER,
    EFFECT_REVERB,
    EFFECT_VISUALIZER,
    EFFECT_NOISE_SUPPRESSOR,
    EFFECT_ECHO_CANCELLER,
    EFFECT_AUTOMATIC_GAIN_CONTROL,
    EFFECT_LOUDNESS_ENHANCER,
} effect_type_t;

/* 均衡器预设 */
typedef enum {
    EQ_NORMAL = 0,
    EQ_CLASSICAL,
    EQ_DANCE,
    EQ_FLAT,
    EQ_FOLK,
    EQ_HEAVY_METAL,
    EQ_HIP_HOP,
    EQ_JAZZ,
    EQ_POP,
    EQ_ROCK,
} eq_preset_t;

/* 音效 */
typedef struct {
    int used;
    int id;
    effect_type_t type;
    int enabled;
    int strength;  /* 0-1000 */
    /* 均衡器 */
    float bands[10];  /* 10段均衡器 */
    int band_count;
    eq_preset_t preset;
    /* 混响 */
    int reverb_type;  /* 0=无,1=大厅,2=房间,3=舞台 */
    float reverb_decay;
    /* 可视化 */
    int fft_size;
} audio_effect_t;

/* MediaSession状态 */
typedef enum {
    SESSION_STATE_NONE = 0,
    SESSION_STATE_PAUSED,
    SESSION_STATE_PLAYING,
    SESSION_STATE_BUFFERING,
    SESSION_STATE_ERROR,
    SESSION_STATE_STOPPED,
} media_session_state_t;

/* 播放控制命令 */
typedef enum {
    CMD_PLAY = 0,
    CMD_PAUSE,
    CMD_STOP,
    CMD_NEXT,
    CMD_PREVIOUS,
    CMD_SEEK_TO,
    CMD_FAST_FORWARD,
    CMD_REWIND,
    CMD_SET_RATE,
    CMD_SET_VOLUME,
} media_command_t;

/* 媒体元数据 */
typedef struct {
    char title[256];
    char artist[128];
    char album[128];
    char album_artist[128];
    char genre[64];
    char author[128];
    char writer[128];
    char composer[128];
    int track_number;
    int total_tracks;
    int disc_number;
    int total_discs;
    uint64_t duration;  /* ms */
    int year;
    char art_uri[256];
    float rating;
} m2_metadata_t;

/* 播放位置 */
typedef struct {
    uint64_t position;  /* ms */
    float playback_rate;
    uint64_t last_update_time;
} playback_position_t;

/* MediaSession */
typedef struct {
    int used;
    int id;
    char package_name[128];
    char tag[64];
    int active;
    media_session_state_t state;
    m2_metadata_t metadata;
    playback_position_t position;
    /* 音频属性 */
    stream_type_t stream_type;
    audio_usage_t usage;
    audio_content_type_t content_type;
    /* 音量 */
    float volume;
    float volume_max;
    /* 控制标志 */
    uint32_t actions;  /* 支持的操作位掩码 */
    /* 队列 */
    int queue_index;
    int queue_size;
    /* 回调 */
    void (*on_command)(media_command_t cmd, uint64_t arg);
    /* 音频焦点 */
    audio_focus_t focus;
    int has_focus;
} media_session_t;

/* 音频焦点栈条目 */
typedef struct {
    int session_id;
    audio_focus_t focus_type;
    int ducking;
} focus_stack_entry_t;

/* 音频输出设备 */
typedef enum {
    OUTPUT_SPEAKER = 0,
    OUTPUT_EARPIECE,
    OUTPUT_WIRED_HEADSET,
    OUTPUT_BLUETOOTH_A2DP,
    OUTPUT_BLUETOOTH_SCO,
    OUTPUT_USB,
    OUTPUT_HDMI,
} audio_output_t;

/* 多媒体深化状态 */
typedef struct {
    int initialized;
    media_session_t sessions[MEDIA2_MAX_SESSIONS];
    int session_count;
    int active_session;
    /* 音频焦点 */
    focus_stack_entry_t focus_stack[MEDIA2_MAX_AUDIO_FOCUS_STACK];
    int focus_stack_size;
    /* 音效 */
    audio_effect_t effects[MEDIA2_MAX_EFFECTS];
    int effect_count;
    /* 全局音量 */
    float master_volume;
    float stream_volumes[8];
    /* 输出设备 */
    audio_output_t current_output;
    /* 音频策略 */
    int mute;
    int vibrate_on_silent;
} media2_t;

/* 初始化多媒体深化 */
void media2_init(void);

/* MediaSession管理 */
int mediasession_create(const char *package, const char *tag);
int mediasession_destroy(int session_id);
int mediasession_set_active(int session_id, int active);
int mediasession_get_active(void);
int mediasession_get(int session_id, media_session_t *session);
int mediasession_list(media_session_t *sessions, int max);

/* 播放控制 */
int mediasession_set_state(int session_id, media_session_state_t state);
int mediasession_set_metadata(int session_id, const m2_metadata_t *metadata);
int mediasession_set_position(int session_id, uint64_t position, float rate);
int mediasession_set_actions(int session_id, uint32_t actions);
int mediasession_send_command(int session_id, media_command_t cmd, uint64_t arg);
int mediasession_get_position(int session_id, playback_position_t *pos);

/* 音频焦点 */
int audiofocus_request(int session_id, audio_focus_t focus_type);
int audiofocus_abandon(int session_id);
int audiofocus_get_current(int *session_id, audio_focus_t *focus_type);

/* 音效 */
int audioeffect_create(effect_type_t type);
int audioeffect_destroy(int effect_id);
int audioeffect_set_enabled(int effect_id, int enabled);
int audioeffect_set_strength(int effect_id, int strength);
int audioeffect_eq_set_band(int effect_id, int band, float gain);
int audioeffect_eq_set_preset(int effect_id, eq_preset_t preset);
int audioeffect_get(int effect_id, audio_effect_t *effect);
int audioeffect_list(audio_effect_t *effects, int max);

/* 音量控制 */
int audio_set_stream_volume(stream_type_t stream, float volume);
float audio_get_stream_volume(stream_type_t stream);
int audio_set_master_volume(float volume);
float audio_get_master_volume(void);
int m2_audio_set_mute(int mute);
int m2_audio_get_mute(void);

/* 输出设备 */
int audio_set_output(audio_output_t output);
audio_output_t audio_get_output(void);

/* 音频策略 */
int audio_set_usage(audio_usage_t usage);
int audio_set_content_type(audio_content_type_t type);

/* 可视化 */
int audio_get_fft(int session_id, float *fft, int size);

/* 统计 */
int media2_session_count(void);
int media2_effect_count(void);

#endif /* MEDIA2_H */
