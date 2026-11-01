#ifndef AUDIO_EFFECTS_H
#define AUDIO_EFFECTS_H

#include <stdint.h>

/* ============================================================================
 * 赫普OS 音效引擎
 * 均衡器 / 混响 / 空间音频 / 降噪 / 音效预设
 * ========================================================================== */

#define AE_MAX_PRESETS 32
#define AE_MAX_EQ_BANDS 32
#define AE_MAX_EFFECTS 16

/* 音效类型 */
typedef enum {
    AE_EFFECT_EQUALIZER = 0,
    AE_EFFECT_BASS_BOOST,
    AE_EFFECT_VIRTUALIZER,
    AE_EFFECT_REVERB,
    AE_EFFECT_COMPRESSOR,
    AE_EFFECT_LIMITER,
    AE_EFFECT_NOISE_GATE,
    AE_EFFECT_DEESSER,
    AE_EFFECT_PITCH_SHIFT,
    AE_EFFECT_TIME_STRETCH,
    AE_EFFECT_CHORUS,
    AE_EFFECT_FLANGER,
    AE_EFFECT_PHASER,
    AE_EFFECT_DELAY,
    AE_EFFECT_DISTORTION,
    AE_EFFECT_SPATIAL_AUDIO,
} ae_effect_type_t;

/* 混响类型 */
typedef enum {
    AE_REVERB_NONE = 0,
    AE_REVERB_SMALL_ROOM,
    AE_REVERB_MEDIUM_ROOM,
    AE_REVERB_LARGE_ROOM,
    AE_REVERB_HALL,
    AE_REVERB_CHURCH,
    AE_REVERB_CATHEDRAL,
    AE_REVERB_PLATE,
    AE_REVERB_SPRING,
    AE_REVERB_AMBIENT,
} ae_reverb_type_t;

/* 空间音频模式 */
typedef enum {
    AE_SPATIAL_OFF = 0,
    AE_SPATIAL_STEREO,
    AE_SPATIAL_SURROUND,
    AE_SPATIAL_DOLBY_ATMOS,
    AE_SPATIAL_HEAD_TRACKING,
    AE_SPATIAL_3D,
} ae_spatial_mode_t;

/* 均衡器频段 */
typedef struct {
    int band;
    int freq;  /* Hz */
    int gain;  /* -12 to +12 dB */
    int q;     /* 0.1 to 10.0, 乘以10 */
    int type;  /* 0=peak,1=low_shelf,2=high_shelf,3=low_pass,4=high_pass */
} ae_eq_band_t;

/* 音效参数 */
typedef struct {
    int enabled;
    ae_effect_type_t type;
    /* 通用参数 */
    int intensity;  /* 0-100 */
    int mix;        /* 0-100, 干/湿比 */
    /* 均衡器 */
    ae_eq_band_t eq_bands[AE_MAX_EQ_BANDS];
    int eq_band_count;
    /* 低音增强 */
    int bass_boost;  /* 0-100 */
    int bass_freq;   /* Hz */
    /* 虚拟器 */
    int virtualizer;  /* 0-100 */
    /* 混响 */
    ae_reverb_type_t reverb_type;
    int reverb_decay;   /* 0-100 */
    int reverb_pre_delay;  /* ms */
    int reverb_density;   /* 0-100 */
    /* 压缩器 */
    int compressor_threshold;  /* -60 to 0 dB */
    int compressor_ratio;      /* 1-20 */
    int compressor_attack;     /* ms */
    int compressor_release;    /* ms */
    int compressor_makeup;     /* dB */
    /* 限制器 */
    int limiter_threshold;  /* dB */
    int limiter_release;    /* ms */
    /* 降噪门 */
    int noise_gate_threshold;  /* dB */
    int noise_gate_attack;     /* ms */
    int noise_gate_release;    /* ms */
    /* 空间音频 */
    ae_spatial_mode_t spatial_mode;
    int spatial_width;  /* 0-100 */
    int spatial_height; /* 0-100 */
} ae_effect_t;

/* 音效预设 */
typedef struct {
    int used;
    int id;
    char name[64];
    char description[256];
    int effect_ids[AE_MAX_EFFECTS];
    int effect_count;
    int is_system;
} ae_preset_t;

/* 音效引擎状态 */
typedef struct {
    int initialized;
    int enabled;
    /* 全局 */
    int master_volume;  /* 0-100 */
    int master_mute;
    int sample_rate;
    int channels;
    int bit_depth;
    /* 效果链 */
    ae_effect_t effects[AE_MAX_EFFECTS];
    int effect_count;
    /* 预设 */
    ae_preset_t presets[AE_MAX_PRESETS];
    int preset_count;
    int current_preset;
    /* 空间音频 */
    ae_spatial_mode_t spatial_mode;
    int head_tracking_enabled;
    /* 统计 */
    uint64_t total_processed;
    uint64_t total_effects_applied;
} audio_effects_t;

/* 初始化 */
void audio_effects_init(void);

/* 效果管理 */
int ae_add_effect(ae_effect_type_t type);
int ae_remove_effect(int id);
int ae_get_effect(int id, ae_effect_t *effect);
int ae_update_effect(int id, const ae_effect_t *effect);
int ae_list_effects(ae_effect_t *effects, int max);
int ae_enable_effect(int id, int enable);
int ae_reorder_effect(int id, int new_pos);

/* 均衡器 */
int ae_eq_set_band(int effect_id, int band, int gain);
int ae_eq_get_band(int effect_id, int band, ae_eq_band_t *eq);
int ae_eq_set_gain(int effect_id, int band, int gain);
int ae_eq_auto_tune(void);

/* 混响 */
int ae_reverb_set_type(int effect_id, ae_reverb_type_t type);
int ae_reverb_set_params(int effect_id, int decay, int pre_delay, int density);

/* 空间音频 */
int ae_spatial_set_mode(ae_spatial_mode_t mode);
int ae_spatial_get_mode(ae_spatial_mode_t *mode);
int ae_spatial_set_width(int width);
int ae_spatial_set_height(int height);
int ae_head_tracking_enable(int enable);

/* 预设 */
int ae_preset_create(const char *name, const char *desc);
int ae_preset_delete(int id);
int ae_preset_apply(int id);
int ae_preset_get(int id, ae_preset_t *preset);
int ae_preset_list(ae_preset_t *presets, int max);
int ae_preset_save_current(const char *name);

/* 全局控制 */
int ae_set_master_volume(int volume);
int ae_get_master_volume(int *volume);
int ae_set_mute(int mute);
int ae_enable(int enable);
int ae_process(int16_t *input, int16_t *output, int samples);

/* 状态查询 */
int ae_get_state(audio_effects_t *state);
int ae_get_stats(audio_effects_t *stats);

#endif /* AUDIO_EFFECTS_H */
