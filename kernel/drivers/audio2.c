/* ============================================================================
 * 赫普OS - 音频深化
 * 空间音频 + 降噪 + 语音唤醒 + 低延迟 + MIDI
 * ========================================================================== */

#include <stdint.h>
#include "audio2.h"
#include "vga.h"
#include "string.h"

audio2_t a2;

void audio2_init(void)
{
    memset(&a2, 0, sizeof(audio2_t));
    a2.initialized = 1;
    a2.master_volume = 80;

    /* 扬声器 */
    a2.outputs[0].used = 1;
    a2.outputs[0].id = 0;
    strncpy(a2.outputs[0].name, " 扬声器 ", 63);
    a2.outputs[0].type = A2_DEVICE_SPEAKER;
    a2.outputs[0].sample_rate = A2_RATE_48000;
    a2.outputs[0].format = A2_FORMAT_PCM_S24;
    a2.outputs[0].channels = A2_CHANNEL_STEREO;
    a2.outputs[0].volume = 80;
    a2.outputs[0].latency_ms = 20;
    a2.outputs[0].support_hires = 1;
    a2.outputs[0].support_spatial = 1;
    a2.outputs[0].active = 1;
    a2.output_count = 1;

    /* 听筒 */
    a2.outputs[1].used = 1;
    a2.outputs[1].id = 1;
    strncpy(a2.outputs[1].name, " 听筒 ", 63);
    a2.outputs[1].type = A2_DEVICE_EARPIECE;
    a2.outputs[1].sample_rate = A2_RATE_48000;
    a2.outputs[1].format = A2_FORMAT_PCM_S16;
    a2.outputs[1].channels = A2_CHANNEL_MONO;
    a2.outputs[1].volume = 70;
    a2.outputs[1].latency_ms = 15;
    a2.output_count = 2;

    /* 主麦克风 */
    a2.inputs[0].used = 1;
    a2.inputs[0].id = 0;
    strncpy(a2.inputs[0].name, " master 麦克风 ", 63);
    a2.inputs[0].type = A2_DEVICE_SPEAKER;
    a2.inputs[0].sample_rate = A2_RATE_48000;
    a2.inputs[0].format = A2_FORMAT_PCM_S24;
    a2.inputs[0].channels = A2_CHANNEL_MONO;
    a2.inputs[0].gain = 60;
    a2.inputs[0].latency_ms = 10;
    a2.inputs[0].support_ec = 1;
    a2.inputs[0].support_ns = 1;
    a2.inputs[0].support_agc = 1;
    a2.inputs[0].active = 1;
    a2.input_count = 1;

    /* 降噪麦克风 */
    a2.inputs[1].used = 1;
    a2.inputs[1].id = 1;
    strncpy(a2.inputs[1].name, " noise cancel 麦克风 ", 63);
    a2.inputs[1].type = A2_DEVICE_SPEAKER;
    a2.inputs[1].sample_rate = A2_RATE_48000;
    a2.inputs[1].format = A2_FORMAT_PCM_S16;
    a2.inputs[1].channels = A2_CHANNEL_MONO;
    a2.inputs[1].gain = 50;
    a2.input_count = 2;

    a2.active_output = 0;
    a2.active_input = 0;

    /* 默认音效 */
    a2.effects[0].used = 1;
    a2.effects[0].id = 0;
    strncpy(a2.effects[0].name, " EQ ", 63);
    a2.effects[0].type = A2_EFFECT_EQUALIZER;
    a2.effects[0].enabled = 1;
    a2.effects[0].strength = 50;
    a2.effect_count = 1;

    /* 空间音频默认关闭 */
    a2.spatial.enabled = 0;
    a2.spatial.mode = 0;
    a2.spatial.hrtf = 1;

    /* 降噪默认关闭 */
    a2.noise_cancel.enabled = 0;
    a2.noise_cancel.mode = 0;

    /* 语音唤醒默认关闭 */
    a2.voice_wake.enabled = 0;
    strncpy(a2.voice_wake.wake_word, " 你好 Hepu ", 63);
    a2.voice_wake.sensitivity = 70;

    /* 低延迟默认关闭 */
    a2.low_latency.enabled = 0;
    a2.low_latency.target_latency_ms = 10;

    vga_puts("[AUDIO2] audio deep init done \n");
    vga_printf(" output: %d, input: %d, sample rate: %dHz, supports spatial audio / noise cancel / voice wake /MIDI\n",
               a2.output_count, a2.input_count, a2.outputs[0].sample_rate);
}

int audio2_get_output_count(void) { return a2.output_count; }

int audio2_get_output(int id, a2_output_t *output)
{
    if (id < 0 || id >= A2_MAX_OUTPUTS || !a2.outputs[id].used || !output) return -1;
    memcpy(output, &a2.outputs[id], sizeof(a2_output_t));
    return 0;
}

int audio2_list_outputs(a2_output_t *outputs, int max)
{
    if (!outputs || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < A2_MAX_OUTPUTS && count < max; i++) {
        if (a2.outputs[i].used) memcpy(&outputs[count++], &a2.outputs[i], sizeof(a2_output_t));
    }
    return count;
}

int audio2_set_active_output(int id)
{
    if (id < 0 || id >= A2_MAX_OUTPUTS || !a2.outputs[id].used) return -1;
    for (int i = 0; i < A2_MAX_OUTPUTS; i++) a2.outputs[i].active = 0;
    a2.outputs[id].active = 1;
    a2.active_output = id;
    vga_printf("[AUDIO2] switch output: %s\n", a2.outputs[id].name);
    return 0;
}

int audio2_get_active_output(void) { return a2.active_output; }

int audio2_set_output_volume(int id, int volume)
{
    if (id < 0 || id >= A2_MAX_OUTPUTS || !a2.outputs[id].used) return -1;
    if (volume < 0) volume = 0;
    if (volume > 100) volume = 100;
    a2.outputs[id].volume = volume;
    return 0;
}

int audio2_get_output_volume(int id)
{
    if (id < 0 || id >= A2_MAX_OUTPUTS || !a2.outputs[id].used) return -1;
    return a2.outputs[id].volume;
}

int audio2_mute_output(int id, int mute)
{
    if (id < 0 || id >= A2_MAX_OUTPUTS || !a2.outputs[id].used) return -1;
    a2.outputs[id].muted = mute ? 1 : 0;
    return 0;
}

int audio2_get_input_count(void) { return a2.input_count; }

int audio2_get_input(int id, a2_input_t *input)
{
    if (id < 0 || id >= A2_MAX_INPUTS || !a2.inputs[id].used || !input) return -1;
    memcpy(input, &a2.inputs[id], sizeof(a2_input_t));
    return 0;
}

int audio2_list_inputs(a2_input_t *inputs, int max)
{
    if (!inputs || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < A2_MAX_INPUTS && count < max; i++) {
        if (a2.inputs[i].used) memcpy(&inputs[count++], &a2.inputs[i], sizeof(a2_input_t));
    }
    return count;
}

int audio2_set_active_input(int id)
{
    if (id < 0 || id >= A2_MAX_INPUTS || !a2.inputs[id].used) return -1;
    for (int i = 0; i < A2_MAX_INPUTS; i++) a2.inputs[i].active = 0;
    a2.inputs[id].active = 1;
    a2.active_input = id;
    return 0;
}

int audio2_get_active_input(void) { return a2.active_input; }

int audio2_set_input_gain(int id, int gain)
{
    if (id < 0 || id >= A2_MAX_INPUTS || !a2.inputs[id].used) return -1;
    if (gain < 0) gain = 0;
    if (gain > 100) gain = 100;
    a2.inputs[id].gain = gain;
    return 0;
}

int audio2_create_source(const char *name, int pid, a2_sample_rate_t rate, a2_format_t format, a2_channel_t channels)
{
    for (int i = 0; i < A2_MAX_SOURCES; i++) {
        if (!a2.sources[i].used) {
            memset(&a2.sources[i], 0, sizeof(a2_source_t));
            a2.sources[i].used = 1;
            a2.sources[i].id = i;
            if (name) strncpy(a2.sources[i].name, name, 63);
            a2.sources[i].pid = pid;
            a2.sources[i].sample_rate = rate;
            a2.sources[i].format = format;
            a2.sources[i].channels = channels;
            a2.sources[i].volume = 100;
            a2.source_count++;
            return i;
        }
    }
    return -1;
}

int audio2_destroy_source(int source_id)
{
    if (source_id < 0 || source_id >= A2_MAX_SOURCES || !a2.sources[source_id].used) return -1;
    a2.sources[source_id].used = 0;
    a2.source_count--;
    return 0;
}

int audio2_play(int source_id)
{
    if (source_id < 0 || source_id >= A2_MAX_SOURCES || !a2.sources[source_id].used) return -1;
    a2.sources[source_id].playing = 1;
    a2.sources[source_id].paused = 0;
    return 0;
}

int audio2_pause(int source_id)
{
    if (source_id < 0 || source_id >= A2_MAX_SOURCES || !a2.sources[source_id].used) return -1;
    a2.sources[source_id].paused = 1;
    return 0;
}

int audio2_stop(int source_id)
{
    if (source_id < 0 || source_id >= A2_MAX_SOURCES || !a2.sources[source_id].used) return -1;
    a2.sources[source_id].playing = 0;
    a2.sources[source_id].paused = 0;
    a2.sources[source_id].position = 0;
    return 0;
}

int audio2_set_source_volume(int source_id, int volume)
{
    if (source_id < 0 || source_id >= A2_MAX_SOURCES || !a2.sources[source_id].used) return -1;
    if (volume < 0) volume = 0;
    if (volume > 100) volume = 100;
    a2.sources[source_id].volume = volume;
    return 0;
}

int audio2_get_source_position(int source_id, uint64_t *position)
{
    if (source_id < 0 || source_id >= A2_MAX_SOURCES || !a2.sources[source_id].used || !position) return -1;
    *position = a2.sources[source_id].position;
    return 0;
}

int audio2_set_source_position(int source_id, uint64_t position)
{
    if (source_id < 0 || source_id >= A2_MAX_SOURCES || !a2.sources[source_id].used) return -1;
    a2.sources[source_id].position = position;
    return 0;
}

int audio2_write_data(int source_id, const void *data, uint32_t size)
{
    (void)data; (void)size;
    if (source_id < 0 || source_id >= A2_MAX_SOURCES || !a2.sources[source_id].used) return -1;
    return 0;
}

int audio2_create_effect(const char *name, a2_effect_type_t type)
{
    for (int i = 0; i < A2_MAX_EFFECTS; i++) {
        if (!a2.effects[i].used) {
            memset(&a2.effects[i], 0, sizeof(a2_effect_t));
            a2.effects[i].used = 1;
            a2.effects[i].id = i;
            if (name) strncpy(a2.effects[i].name, name, 63);
            a2.effects[i].type = type;
            a2.effect_count++;
            return i;
        }
    }
    return -1;
}

int audio2_destroy_effect(int effect_id)
{
    if (effect_id < 0 || effect_id >= A2_MAX_EFFECTS || !a2.effects[effect_id].used) return -1;
    a2.effects[effect_id].used = 0;
    a2.effect_count--;
    return 0;
}

int audio2_enable_effect(int effect_id, int enable)
{
    if (effect_id < 0 || effect_id >= A2_MAX_EFFECTS || !a2.effects[effect_id].used) return -1;
    a2.effects[effect_id].enabled = enable ? 1 : 0;
    return 0;
}

int audio2_set_effect_strength(int effect_id, int strength)
{
    if (effect_id < 0 || effect_id >= A2_MAX_EFFECTS || !a2.effects[effect_id].used) return -1;
    if (strength < 0) strength = 0;
    if (strength > 100) strength = 100;
    a2.effects[effect_id].strength = strength;
    return 0;
}

int audio2_set_effect_param(int effect_id, int param_index, int value)
{
    if (effect_id < 0 || effect_id >= A2_MAX_EFFECTS || !a2.effects[effect_id].used) return -1;
    if (param_index < 0 || param_index >= 8) return -1;
    a2.effects[effect_id].params[param_index] = value;
    return 0;
}

int audio2_list_effects(a2_effect_t *effects, int max)
{
    if (!effects || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < A2_MAX_EFFECTS && count < max; i++) {
        if (a2.effects[i].used) memcpy(&effects[count++], &a2.effects[i], sizeof(a2_effect_t));
    }
    return count;
}

int audio2_set_master_volume(int volume)
{
    if (volume < 0) volume = 0;
    if (volume > 100) volume = 100;
    a2.master_volume = volume;
    return 0;
}

int audio2_get_master_volume(void) { return a2.master_volume; }

int audio2_mute_master(int mute)
{
    a2.master_muted = mute ? 1 : 0;
    return 0;
}

int audio2_get_stats(uint64_t *play_time, uint64_t *record_time, int *underruns)
{
    if (play_time) *play_time = a2.total_play_time;
    if (record_time) *record_time = a2.total_record_time;
    if (underruns) *underruns = a2.underruns;
    return 0;
}


