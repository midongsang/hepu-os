/* ============================================================================
 * 赫普OS - 音效引擎
 * ========================================================================== */

#include <stdint.h>
#include "audio_effects.h"
#include "vga.h"
#include "string.h"

static audio_effects_t ae;

/* 系统预设名称 */
static const char *system_presets[] = {
    " default ", " streams 行 ", " 摇滚 ", " 古典 ", " 爵士 ", " 电子 ", " 人声 ", " 重 low 音 ",
    " 影院 ", " game ", " 播客 ", " 会议 ", " 睡眠 ", " 运动 ", " 驾驶 ", " 户外 "
};

void audio_effects_init(void)
{
    memset(&ae, 0, sizeof(audio_effects_t));
    ae.initialized = 1;
    ae.enabled = 1;
    ae.master_volume = 80;
    ae.sample_rate = 48000;
    ae.channels = 2;
    ae.bit_depth = 16;
    ae.spatial_mode = AE_SPATIAL_STEREO;
    ae.current_preset = -1;

    /* 默认效果链: 均衡器 + 低音增强 + 空间音频 */
    ae_add_effect(AE_EFFECT_EQUALIZER);
    ae_add_effect(AE_EFFECT_BASS_BOOST);
    ae_add_effect(AE_EFFECT_SPATIAL_AUDIO);

    /* 系统预设 */
    for (int i = 0; i < 16; i++) {
        ae_preset_create(system_presets[i], " system preset ");
        ae.presets[i].is_system = 1;
    }

    vga_puts("[AEEFF] audio fx init done \n");
    vga_printf(" effects: %d, preset: %d, spatial audio: %s\n",
               ae.effect_count, ae.preset_count,
               ae.spatial_mode == AE_SPATIAL_OFF ? " close " : " on ");
}

/* 效果管理 */
int ae_add_effect(ae_effect_type_t type)
{
    for (int i = 0; i < AE_MAX_EFFECTS; i++) {
        if (ae.effects[i].type == AE_EFFECT_EQUALIZER && i == 0 && ae.effect_count > 0) continue;
        if (ae.effects[i].enabled == 0 && ae.effects[i].type == 0 && i > 0) {
            memset(&ae.effects[i], 0, sizeof(ae_effect_t));
            ae.effects[i].type = type;
            ae.effects[i].enabled = 1;
            ae.effects[i].intensity = 50;
            ae.effects[i].mix = 100;
            ae.effect_count++;
            return i;
        }
    }
    /* 简化: 直接用effect_count索引 */
    if (ae.effect_count < AE_MAX_EFFECTS) {
        int idx = ae.effect_count;
        memset(&ae.effects[idx], 0, sizeof(ae_effect_t));
        ae.effects[idx].type = type;
        ae.effects[idx].enabled = 1;
        ae.effects[idx].intensity = 50;
        ae.effects[idx].mix = 100;
        ae.effect_count++;
        return idx;
    }
    return -1;
}

int ae_remove_effect(int id)
{
    if (id < 0 || id >= AE_MAX_EFFECTS) return -1;
    ae.effects[id].enabled = 0;
    ae.effects[id].type = AE_EFFECT_EQUALIZER;
    for (int i = id; i < ae.effect_count - 1; i++)
        ae.effects[i] = ae.effects[i+1];
    ae.effect_count--;
    return 0;
}

int ae_get_effect(int id, ae_effect_t *effect)
{
    if (id < 0 || id >= AE_MAX_EFFECTS || !effect) return -1;
    memcpy(effect, &ae.effects[id], sizeof(ae_effect_t));
    return 0;
}

int ae_update_effect(int id, const ae_effect_t *effect)
{
    if (id < 0 || id >= AE_MAX_EFFECTS || !effect) return -1;
    memcpy(&ae.effects[id], effect, sizeof(ae_effect_t));
    return 0;
}

int ae_list_effects(ae_effect_t *effects, int max)
{
    if (!effects || max <= 0) return 0;
    int count = ae.effect_count < max ? ae.effect_count : max;
    for (int i = 0; i < count; i++)
        memcpy(&effects[i], &ae.effects[i], sizeof(ae_effect_t));
    return count;
}

int ae_enable_effect(int id, int enable)
{
    if (id < 0 || id >= AE_MAX_EFFECTS) return -1;
    ae.effects[id].enabled = enable ? 1 : 0;
    return 0;
}

int ae_reorder_effect(int id, int new_pos)
{
    if (id < 0 || id >= ae.effect_count || new_pos < 0 || new_pos >= ae.effect_count) return -1;
    ae_effect_t temp = ae.effects[id];
    if (id < new_pos) {
        for (int i = id; i < new_pos; i++)
            ae.effects[i] = ae.effects[i+1];
    } else {
        for (int i = id; i > new_pos; i--)
            ae.effects[i] = ae.effects[i-1];
    }
    ae.effects[new_pos] = temp;
    return 0;
}

/* 均衡器 */
int ae_eq_set_band(int effect_id, int band, int gain)
{
    if (effect_id < 0 || effect_id >= AE_MAX_EFFECTS) return -1;
    if (band < 0 || band >= AE_MAX_EQ_BANDS) return -1;
    if (gain < -12) gain = -12;
    if (gain > 12) gain = 12;
    ae.effects[effect_id].eq_bands[band].gain = gain;
    return 0;
}

int ae_eq_get_band(int effect_id, int band, ae_eq_band_t *eq)
{
    if (effect_id < 0 || effect_id >= AE_MAX_EFFECTS || !eq) return -1;
    if (band < 0 || band >= AE_MAX_EQ_BANDS) return -1;
    memcpy(eq, &ae.effects[effect_id].eq_bands[band], sizeof(ae_eq_band_t));
    return 0;
}

int ae_eq_set_gain(int effect_id, int band, int gain)
{
    return ae_eq_set_band(effect_id, band, gain);
}

int ae_eq_auto_tune(void)
{
    /* 模拟自动调音 */
    for (int i = 0; i < 10; i++) {
        ae.effects[0].eq_bands[i].gain = (i % 3 == 0) ? 2 : 0;
    }
    return 0;
}

/* 混响 */
int ae_reverb_set_type(int effect_id, ae_reverb_type_t type)
{
    if (effect_id < 0 || effect_id >= AE_MAX_EFFECTS) return -1;
    ae.effects[effect_id].reverb_type = type;
    return 0;
}

int ae_reverb_set_params(int effect_id, int decay, int pre_delay, int density)
{
    if (effect_id < 0 || effect_id >= AE_MAX_EFFECTS) return -1;
    ae.effects[effect_id].reverb_decay = decay;
    ae.effects[effect_id].reverb_pre_delay = pre_delay;
    ae.effects[effect_id].reverb_density = density;
    return 0;
}

/* 空间音频 */
int ae_spatial_set_mode(ae_spatial_mode_t mode)
{
    ae.spatial_mode = mode;
    return 0;
}

int ae_spatial_get_mode(ae_spatial_mode_t *mode)
{
    if (!mode) return -1;
    *mode = ae.spatial_mode;
    return 0;
}

int ae_spatial_set_width(int width)
{
    if (width < 0) width = 0;
    if (width > 100) width = 100;
    for (int i = 0; i < ae.effect_count; i++) {
        if (ae.effects[i].type == AE_EFFECT_SPATIAL_AUDIO) {
            ae.effects[i].spatial_width = width;
            return 0;
        }
    }
    return -1;
}

int ae_spatial_set_height(int height)
{
    if (height < 0) height = 0;
    if (height > 100) height = 100;
    for (int i = 0; i < ae.effect_count; i++) {
        if (ae.effects[i].type == AE_EFFECT_SPATIAL_AUDIO) {
            ae.effects[i].spatial_height = height;
            return 0;
        }
    }
    return -1;
}

int ae_head_tracking_enable(int enable)
{
    ae.head_tracking_enabled = enable ? 1 : 0;
    return 0;
}

/* 预设 */
int ae_preset_create(const char *name, const char *desc)
{
    for (int i = 0; i < AE_MAX_PRESETS; i++) {
        if (!ae.presets[i].used) {
            memset(&ae.presets[i], 0, sizeof(ae_preset_t));
            ae.presets[i].used = 1;
            ae.presets[i].id = i;
            if (name) strncpy(ae.presets[i].name, name, 63);
            if (desc) strncpy(ae.presets[i].description, desc, 255);
            ae.preset_count++;
            return i;
        }
    }
    return -1;
}

int ae_preset_delete(int id)
{
    if (id < 0 || id >= AE_MAX_PRESETS || !ae.presets[id].used) return -1;
    if (ae.presets[id].is_system) return -1;
    ae.presets[id].used = 0;
    ae.preset_count--;
    return 0;
}

int ae_preset_apply(int id)
{
    if (id < 0 || id >= AE_MAX_PRESETS || !ae.presets[id].used) return -1;
    ae.current_preset = id;
    /* 根据预设名称应用不同参数 */
    switch (id) {
        case 1: /* 流行 */
            ae.effects[0].eq_bands[2].gain = 4;
            ae.effects[0].eq_bands[5].gain = 2;
            break;
        case 2: /* 摇滚 */
            ae.effects[0].eq_bands[0].gain = 6;
            ae.effects[0].eq_bands[8].gain = 4;
            break;
        case 7: /* 重低音 */
            ae.effects[1].bass_boost = 80;
            ae.effects[0].eq_bands[0].gain = 8;
            break;
        case 8: /* 影院 */
            ae.spatial_mode = AE_SPATIAL_SURROUND;
            break;
        default:
            break;
    }
    return 0;
}

int ae_preset_get(int id, ae_preset_t *preset)
{
    if (id < 0 || id >= AE_MAX_PRESETS || !ae.presets[id].used || !preset) return -1;
    memcpy(preset, &ae.presets[id], sizeof(ae_preset_t));
    return 0;
}

int ae_preset_list(ae_preset_t *presets, int max)
{
    if (!presets || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < AE_MAX_PRESETS && count < max; i++) {
        if (ae.presets[i].used) memcpy(&presets[count++], &ae.presets[i], sizeof(ae_preset_t));
    }
    return count;
}

int ae_preset_save_current(const char *name)
{
    int id = ae_preset_create(name, " user 自定义 ");
    if (id < 0) return -1;
    for (int i = 0; i < ae.effect_count && i < AE_MAX_EFFECTS; i++)
        ae.presets[id].effect_ids[i] = i;
    ae.presets[id].effect_count = ae.effect_count;
    return id;
}

/* 全局控制 */
int ae_set_master_volume(int volume)
{
    if (volume < 0) volume = 0;
    if (volume > 100) volume = 100;
    ae.master_volume = volume;
    return 0;
}

int ae_get_master_volume(int *volume)
{
    if (!volume) return -1;
    *volume = ae.master_volume;
    return 0;
}

int ae_set_mute(int mute)
{
    ae.master_mute = mute ? 1 : 0;
    return 0;
}

int ae_enable(int enable)
{
    ae.enabled = enable ? 1 : 0;
    return 0;
}

int ae_process(int16_t *input, int16_t *output, int samples)
{
    if (!input || !output || samples <= 0) return -1;
    /* 简化: 直通处理 */
    for (int i = 0; i < samples; i++) {
        int32_t sample = input[i];
        /* 主音量 */
        sample = (sample * ae.master_volume) / 100;
        if (sample > 32767) sample = 32767;
        if (sample < -32768) sample = -32768;
        output[i] = (int16_t)sample;
    }
    ae.total_processed += samples;
    ae.total_effects_applied++;
    return samples;
}

/* 状态查询 */
int ae_get_state(audio_effects_t *state)
{
    if (!state) return -1;
    memcpy(state, &ae, sizeof(audio_effects_t));
    return 0;
}

int ae_get_stats(audio_effects_t *stats)
{
    return ae_get_state(stats);
}
