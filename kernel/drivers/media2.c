/* ============================================================================
 * 赫普OS - 多媒体深化
 * MediaSession + 音频焦点 + 音效 + 音量控制 + 输出设备
 * ========================================================================== */

#include <stdint.h>
#include "media2.h"
#include "vga.h"
#include "string.h"

static media2_t m2;

/* 均衡器预设增益 (10段) */
static const float eq_presets[10][10] = {
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0},           /* NORMAL */
    {0, 0, 2, 2, 1, 1, 2, 2, 2, 2},           /* CLASSICAL */
    {1, 3, 4, 3, 1, -1, -1, 0, 2, 3},         /* DANCE */
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0},           /* FLAT */
    {0, 0, 1, 1, 2, 2, 1, 1, 1, 1},           /* FOLK */
    {4, 4, 2, 0, -2, -2, 0, 2, 3, 3},         /* HEAVY_METAL */
    {4, 3, 1, 0, -1, 0, 2, 2, 3, 3},          /* HIP_HOP */
    {1, 2, 3, 2, 0, 0, 0, 1, 2, 2},           /* JAZZ */
    {-1, 0, 1, 3, 4, 3, 1, 0, -1, -1},        /* POP */
    {4, 3, 2, 1, 0, 0, 1, 2, 3, 4},           /* ROCK */
};

void media2_init(void)
{
    memset(&m2, 0, sizeof(media2_t));
    m2.initialized = 1;
    m2.active_session = -1;
    m2.master_volume = 1.0f;
    m2.current_output = OUTPUT_SPEAKER;
    m2.mute = 0;
    m2.vibrate_on_silent = 1;

    for (int i = 0; i < 8; i++) m2.stream_volumes[i] = 0.7f;
    m2.stream_volumes[STREAM_MUSIC] = 0.8f;
    m2.stream_volumes[STREAM_RING] = 0.9f;
    m2.stream_volumes[STREAM_ALARM] = 0.9f;

    /* 创建默认音效 */
    audioeffect_create(EFFECT_EQUALIZER);
    audioeffect_create(EFFECT_BASS_BOOST);
    audioeffect_create(EFFECT_VIRTUALIZER);
    audioeffect_create(EFFECT_REVERB);

    vga_puts("[MEDIA2] media deep init done \n");
    vga_printf(" session: %d, fx: %d, master volume: %.0f%%\n",
               m2.session_count, m2.effect_count, m2.master_volume * 100);
}

int mediasession_create(const char *package, const char *tag)
{
    if (!package || !m2.initialized) return -1;
    for (int i = 0; i < MEDIA2_MAX_SESSIONS; i++) {
        if (!m2.sessions[i].used) {
            memset(&m2.sessions[i], 0, sizeof(media_session_t));
            m2.sessions[i].used = 1;
            m2.sessions[i].id = i;
            strncpy(m2.sessions[i].package_name, package, 127);
            if (tag) strncpy(m2.sessions[i].tag, tag, 63);
            m2.sessions[i].state = SESSION_STATE_NONE;
            m2.sessions[i].stream_type = STREAM_MUSIC;
            m2.sessions[i].usage = USAGE_MEDIA;
            m2.sessions[i].content_type = CONTENT_TYPE_MUSIC;
            m2.sessions[i].volume = 1.0f;
            m2.sessions[i].volume_max = 1.0f;
            m2.sessions[i].actions = 0xFFFF;  /* 支持所有操作 */
            m2.sessions[i].focus = AUDIO_FOCUS_NONE;
            m2.sessions[i].has_focus = 0;
            m2.session_count++;
            vga_printf("[MEDIA2] create MediaSession: %s/%s\n", package, tag ? tag : "");
            return i;
        }
    }
    return -1;
}

int mediasession_destroy(int session_id)
{
    if (session_id < 0 || session_id >= MEDIA2_MAX_SESSIONS || !m2.sessions[session_id].used) return -1;
    if (m2.sessions[session_id].has_focus) audiofocus_abandon(session_id);
    m2.sessions[session_id].used = 0;
    m2.session_count--;
    if (m2.active_session == session_id) m2.active_session = -1;
    return 0;
}

int mediasession_set_active(int session_id, int active)
{
    if (session_id < 0 || session_id >= MEDIA2_MAX_SESSIONS || !m2.sessions[session_id].used) return -1;
    m2.sessions[session_id].active = active ? 1 : 0;
    if (active) m2.active_session = session_id;
    return 0;
}

int mediasession_get_active(void) { return m2.active_session; }

int mediasession_get(int session_id, media_session_t *session)
{
    if (session_id < 0 || session_id >= MEDIA2_MAX_SESSIONS || !m2.sessions[session_id].used || !session) return -1;
    memcpy(session, &m2.sessions[session_id], sizeof(media_session_t));
    return 0;
}

int mediasession_list(media_session_t *sessions, int max)
{
    if (!sessions || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < MEDIA2_MAX_SESSIONS && count < max; i++) {
        if (m2.sessions[i].used) memcpy(&sessions[count++], &m2.sessions[i], sizeof(media_session_t));
    }
    return count;
}

int mediasession_set_state(int session_id, media_session_state_t state)
{
    if (session_id < 0 || session_id >= MEDIA2_MAX_SESSIONS || !m2.sessions[session_id].used) return -1;
    m2.sessions[session_id].state = state;
    if (state == SESSION_STATE_PLAYING) {
        m2.active_session = session_id;
    }
    return 0;
}

int mediasession_set_metadata(int session_id, const m2_metadata_t *metadata)
{
    if (session_id < 0 || session_id >= MEDIA2_MAX_SESSIONS || !m2.sessions[session_id].used) return -1;
    if (metadata) memcpy(&m2.sessions[session_id].metadata, metadata, sizeof(m2_metadata_t));
    return 0;
}

int mediasession_set_position(int session_id, uint64_t position, float rate)
{
    if (session_id < 0 || session_id >= MEDIA2_MAX_SESSIONS || !m2.sessions[session_id].used) return -1;
    m2.sessions[session_id].position.position = position;
    m2.sessions[session_id].position.playback_rate = rate;
    return 0;
}

int mediasession_set_actions(int session_id, uint32_t actions)
{
    if (session_id < 0 || session_id >= MEDIA2_MAX_SESSIONS || !m2.sessions[session_id].used) return -1;
    m2.sessions[session_id].actions = actions;
    return 0;
}

int mediasession_send_command(int session_id, media_command_t cmd, uint64_t arg)
{
    if (session_id < 0 || session_id >= MEDIA2_MAX_SESSIONS || !m2.sessions[session_id].used) return -1;
    media_session_t *s = &m2.sessions[session_id];
    switch (cmd) {
        case CMD_PLAY: s->state = SESSION_STATE_PLAYING; break;
        case CMD_PAUSE: s->state = SESSION_STATE_PAUSED; break;
        case CMD_STOP: s->state = SESSION_STATE_STOPPED; break;
        case CMD_NEXT: s->queue_index++; break;
        case CMD_PREVIOUS: if (s->queue_index > 0) s->queue_index--; break;
        case CMD_SEEK_TO: s->position.position = arg; break;
        case CMD_SET_RATE: s->position.playback_rate = (float)arg / 1000.0f; break;
        case CMD_SET_VOLUME: s->volume = (float)arg / 100.0f; break;
        default: break;
    }
    if (s->on_command) s->on_command(cmd, arg);
    return 0;
}

int mediasession_get_position(int session_id, playback_position_t *pos)
{
    if (session_id < 0 || session_id >= MEDIA2_MAX_SESSIONS || !m2.sessions[session_id].used || !pos) return -1;
    memcpy(pos, &m2.sessions[session_id].position, sizeof(playback_position_t));
    return 0;
}

int audiofocus_request(int session_id, audio_focus_t focus_type)
{
    if (session_id < 0 || session_id >= MEDIA2_MAX_SESSIONS || !m2.sessions[session_id].used) return -1;
    if (m2.focus_stack_size >= MEDIA2_MAX_AUDIO_FOCUS_STACK) return -1;

    /* 通知之前持有焦点的会话 */
    for (int i = 0; i < m2.focus_stack_size; i++) {
        int sid = m2.focus_stack[i].session_id;
        if (focus_type == AUDIO_FOCUS_GAIN_TRANSIENT_MAY_DUCK) {
            m2.sessions[sid].focus = AUDIO_FOCUS_LOSS_TRANSIENT_CAN_DUCK;
            m2.focus_stack[i].ducking = 1;
        } else {
            m2.sessions[sid].focus = AUDIO_FOCUS_LOSS_TRANSIENT;
        }
    }

    m2.focus_stack[m2.focus_stack_size].session_id = session_id;
    m2.focus_stack[m2.focus_stack_size].focus_type = focus_type;
    m2.focus_stack[m2.focus_stack_size].ducking = 0;
    m2.focus_stack_size++;
    m2.sessions[session_id].has_focus = 1;
    m2.sessions[session_id].focus = focus_type;

    vga_printf("[MEDIA2] audio focus req: session=%d type=%d\n", session_id, focus_type);
    return AUDIO_FOCUS_GAIN;
}

int audiofocus_abandon(int session_id)
{
    if (session_id < 0 || session_id >= MEDIA2_MAX_SESSIONS) return -1;
    for (int i = 0; i < m2.focus_stack_size; i++) {
        if (m2.focus_stack[i].session_id == session_id) {
            for (int j = i; j < m2.focus_stack_size - 1; j++) {
                m2.focus_stack[j] = m2.focus_stack[j + 1];
            }
            m2.focus_stack_size--;
            m2.sessions[session_id].has_focus = 0;
            m2.sessions[session_id].focus = AUDIO_FOCUS_NONE;
            /* 恢复之前的会话 */
            if (m2.focus_stack_size > 0) {
                int top = m2.focus_stack[m2.focus_stack_size - 1].session_id;
                m2.sessions[top].focus = AUDIO_FOCUS_GAIN;
            }
            return 0;
        }
    }
    return -1;
}

int audiofocus_get_current(int *session_id, audio_focus_t *focus_type)
{
    if (m2.focus_stack_size == 0) return -1;
    if (session_id) *session_id = m2.focus_stack[m2.focus_stack_size - 1].session_id;
    if (focus_type) *focus_type = m2.focus_stack[m2.focus_stack_size - 1].focus_type;
    return 0;
}

int audioeffect_create(effect_type_t type)
{
    if (!m2.initialized) return -1;
    for (int i = 0; i < MEDIA2_MAX_EFFECTS; i++) {
        if (!m2.effects[i].used) {
            memset(&m2.effects[i], 0, sizeof(audio_effect_t));
            m2.effects[i].used = 1;
            m2.effects[i].id = i;
            m2.effects[i].type = type;
            m2.effects[i].enabled = 0;
            m2.effects[i].strength = 500;
            m2.effects[i].band_count = 10;
            m2.effects[i].preset = EQ_NORMAL;
            m2.effects[i].reverb_type = 0;
            m2.effects[i].reverb_decay = 0.5f;
            m2.effects[i].fft_size = 256;
            m2.effect_count++;
            return i;
        }
    }
    return -1;
}

int audioeffect_destroy(int effect_id)
{
    if (effect_id < 0 || effect_id >= MEDIA2_MAX_EFFECTS || !m2.effects[effect_id].used) return -1;
    m2.effects[effect_id].used = 0;
    m2.effect_count--;
    return 0;
}

int audioeffect_set_enabled(int effect_id, int enabled)
{
    if (effect_id < 0 || effect_id >= MEDIA2_MAX_EFFECTS || !m2.effects[effect_id].used) return -1;
    m2.effects[effect_id].enabled = enabled ? 1 : 0;
    return 0;
}

int audioeffect_set_strength(int effect_id, int strength)
{
    if (effect_id < 0 || effect_id >= MEDIA2_MAX_EFFECTS || !m2.effects[effect_id].used) return -1;
    if (strength < 0) strength = 0;
    if (strength > 1000) strength = 1000;
    m2.effects[effect_id].strength = strength;
    return 0;
}

int audioeffect_eq_set_band(int effect_id, int band, float gain)
{
    if (effect_id < 0 || effect_id >= MEDIA2_MAX_EFFECTS || !m2.effects[effect_id].used) return -1;
    if (band < 0 || band >= 10) return -1;
    m2.effects[effect_id].bands[band] = gain;
    return 0;
}

int audioeffect_eq_set_preset(int effect_id, eq_preset_t preset)
{
    if (effect_id < 0 || effect_id >= MEDIA2_MAX_EFFECTS || !m2.effects[effect_id].used) return -1;
    if (preset < 0 || preset > 9) return -1;
    m2.effects[effect_id].preset = preset;
    memcpy(m2.effects[effect_id].bands, eq_presets[preset], sizeof(float) * 10);
    return 0;
}

int audioeffect_get(int effect_id, audio_effect_t *effect)
{
    if (effect_id < 0 || effect_id >= MEDIA2_MAX_EFFECTS || !m2.effects[effect_id].used || !effect) return -1;
    memcpy(effect, &m2.effects[effect_id], sizeof(audio_effect_t));
    return 0;
}

int audioeffect_list(audio_effect_t *effects, int max)
{
    if (!effects || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < MEDIA2_MAX_EFFECTS && count < max; i++) {
        if (m2.effects[i].used) memcpy(&effects[count++], &m2.effects[i], sizeof(audio_effect_t));
    }
    return count;
}

int audio_set_stream_volume(stream_type_t stream, float volume)
{
    if (stream < 0 || stream >= 8) return -1;
    if (volume < 0) volume = 0;
    if (volume > 1) volume = 1;
    m2.stream_volumes[stream] = volume;
    return 0;
}

float audio_get_stream_volume(stream_type_t stream)
{
    if (stream < 0 || stream >= 8) return 0;
    return m2.stream_volumes[stream];
}

int audio_set_master_volume(float volume)
{
    if (volume < 0) volume = 0;
    if (volume > 1) volume = 1;
    m2.master_volume = volume;
    return 0;
}

float audio_get_master_volume(void) { return m2.master_volume; }

int m2_audio_set_mute(int mute) { m2.mute = mute ? 1 : 0; return 0; }
int m2_audio_get_mute(void) { return m2.mute; }

int audio_set_output(audio_output_t output)
{
    m2.current_output = output;
    vga_printf("[MEDIA2] audio out switch: %d\n", output);
    return 0;
}

audio_output_t audio_get_output(void) { return m2.current_output; }

int audio_set_usage(audio_usage_t usage) { (void)usage; return 0; }
int audio_set_content_type(audio_content_type_t type) { (void)type; return 0; }

int audio_get_fft(int session_id, float *fft, int size)
{
    (void)session_id;
    if (!fft || size <= 0) return -1;
    /* 原型阶段: 返回模拟FFT数据 */
    for (int i = 0; i < size; i++) {
        fft[i] = (float)((i * 13 + 7) % 100) / 100.0f;
    }
    return size;
}

int media2_session_count(void) { return m2.session_count; }
int media2_effect_count(void) { return m2.effect_count; }
