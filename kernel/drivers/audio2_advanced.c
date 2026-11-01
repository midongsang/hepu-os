/* ============================================================================
 * 赫普OS - 高级音频功能（从audio2.c拆分）
 * 空间音频 / 降噪 / 语音唤醒 / 低延迟
 * ========================================================================== */

#include <stdint.h>
#include "audio2.h"
#include "vga.h"
#include "string.h"

extern audio2_t a2;

int audio2_spatial_enable(int enable)
{
    a2.spatial.enabled = enable ? 1 : 0;
    vga_printf("[AUDIO2] spatial audio %s\n", enable ? " enabled " : " disabled ");
    return 0;
}

int audio2_spatial_set_mode(int mode)
{
    a2.spatial.mode = mode;
    return 0;
}

int audio2_spatial_set_head_orientation(float yaw, float pitch, float roll)
{
    a2.spatial.head_orientation[0] = yaw;
    a2.spatial.head_orientation[1] = pitch;
    a2.spatial.head_orientation[2] = roll;
    return 0;
}

int audio2_spatial_get(a2_spatial_t *spatial)
{
    if (!spatial) return -1;
    memcpy(spatial, &a2.spatial, sizeof(a2_spatial_t));
    return 0;
}

int audio2_noise_cancel_enable(int enable)
{
    a2.noise_cancel.enabled = enable ? 1 : 0;
    vga_printf("[AUDIO2] noise cancel %s\n", enable ? " enabled " : " disabled ");
    return 0;
}

int audio2_noise_cancel_set_mode(int mode)
{
    a2.noise_cancel.mode = mode;
    return 0;
}

int audio2_noise_cancel_set_strength(int strength)
{
    if (strength < 0) strength = 0;
    if (strength > 100) strength = 100;
    a2.noise_cancel.strength = strength;
    return 0;
}

int audio2_noise_cancel_get(a2_noise_cancel_t *nc)
{
    if (!nc) return -1;
    memcpy(nc, &a2.noise_cancel, sizeof(a2_noise_cancel_t));
    return 0;
}

int audio2_voice_wake_enable(int enable)
{
    a2.voice_wake.enabled = enable ? 1 : 0;
    vga_printf("[AUDIO2] voice wake %s(wakeword: %s)\n", enable ? " enabled " : " disabled ", a2.voice_wake.wake_word);
    return 0;
}

int audio2_voice_wake_set_word(const char *word)
{
    if (!word) return -1;
    strncpy(a2.voice_wake.wake_word, word, 63);
    return 0;
}

int audio2_voice_wake_set_sensitivity(int sensitivity)
{
    if (sensitivity < 0) sensitivity = 0;
    if (sensitivity > 100) sensitivity = 100;
    a2.voice_wake.sensitivity = sensitivity;
    return 0;
}

int audio2_voice_wake_get(a2_voice_wake_t *vw)
{
    if (!vw) return -1;
    memcpy(vw, &a2.voice_wake, sizeof(a2_voice_wake_t));
    return 0;
}

int audio2_register_voice_command(const char *command, const char *action)
{
    if (!command || !action) return -1;
    for (int i = 0; i < A2_MAX_VOICE_COMMANDS; i++) {
        if (a2.voice_commands[i].id == 0 && a2.voice_commands[i].command[0] == 0) {
            a2.voice_commands[i].id = i + 1;
            strncpy(a2.voice_commands[i].command, command, 127);
            strncpy(a2.voice_commands[i].action, action, 127);
            a2.voice_command_count++;
            return i + 1;
        }
    }
    return -1;
}

int audio2_unregister_voice_command(int command_id)
{
    for (int i = 0; i < A2_MAX_VOICE_COMMANDS; i++) {
        if (a2.voice_commands[i].id == command_id) {
            memset(&a2.voice_commands[i], 0, sizeof(a2_voice_command_t));
            a2.voice_command_count--;
            return 0;
        }
    }
    return -1;
}

int audio2_list_voice_commands(a2_voice_command_t *commands, int max)
{
    if (!commands || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < A2_MAX_VOICE_COMMANDS && count < max; i++) {
        if (a2.voice_commands[i].id != 0) memcpy(&commands[count++], &a2.voice_commands[i], sizeof(a2_voice_command_t));
    }
    return count;
}

int audio2_low_latency_enable(int enable)
{
    a2.low_latency.enabled = enable ? 1 : 0;
    return 0;
}

int audio2_low_latency_set_target(int latency_ms)
{
    a2.low_latency.target_latency_ms = latency_ms;
    return 0;
}

int audio2_low_latency_get(a2_low_latency_t *ll)
{
    if (!ll) return -1;
    memcpy(ll, &a2.low_latency, sizeof(a2_low_latency_t));
    return 0;
}

