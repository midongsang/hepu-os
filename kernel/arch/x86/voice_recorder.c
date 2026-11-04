/* ============================================================================
 * 赫普OS - 录音机
 * ========================================================================== */

#include <stdint.h>
#include "voice_recorder.h"
#include "vga.h"
#include "string.h"

static voice_recorder_t vr;

void voice_recorder_init(void)
{
    memset(&vr, 0, sizeof(voice_recorder_t));
    vr.initialized = 1;
    vr.enabled = 1;
    vr.state = VR_STATE_IDLE;
    vr.format = VR_FORMAT_M4A;
    vr.quality = VR_QUALITY_HIGH;
    vr.scene = VR_SCENE_GENERAL;
    vr.sample_rate = 44100;
    vr.bitrate = 192;
    vr.channels = 1;
    vr.gain = 70;
    vr.noise_reduction = 1;
    vr.echo_cancellation = 1;
    vr.auto_gain = 1;
    vr.skip_silence = 0;
    vr.auto_save = 1;
    vr.max_duration = 0;
    strncpy(vr.save_path, "/home/user/Recordings/", 255);
    vr.current_recording = -1;

    /* 测试录音 */
    vr_start_recording(" test recording ");
    vr_stop_recording();

    vga_puts("[VOICE] voice recorder init done \n");
    vga_printf(" format: M4A, quality: high, recording: %d \n", vr.recording_count);
}

/* 录音控制 */
int vr_start_recording(const char *title)
{
    if (vr.state == VR_STATE_RECORDING) return -1;
    for (int i = 0; i < VR_MAX_RECORDINGS; i++) {
        if (!vr.recordings[i].used) {
            memset(&vr.recordings[i], 0, sizeof(vr_recording_t));
            vr.recordings[i].used = 1;
            vr.recordings[i].id = i;
            if (title) strncpy(vr.recordings[i].title, title, 127);
            else strncpy(vr.recordings[i].title, " not 命名 recording ", 127);
            vr.recordings[i].format = vr.format;
            vr.recordings[i].quality = vr.quality;
            vr.recordings[i].scene = vr.scene;
            vr.recordings[i].sample_rate = vr.sample_rate;
            vr.recordings[i].bitrate = vr.bitrate;
            vr.recordings[i].channels = vr.channels;
            vr.recordings[i].created = 1;
            vr.recordings[i].modified = 1;
            vr.current_recording = i;
            vr.record_position = 0;
            vr.state = VR_STATE_RECORDING;
            return i;
        }
    }
    return -1;
}

int vr_pause_recording(void)
{
    if (vr.state != VR_STATE_RECORDING) return -1;
    vr.state = VR_STATE_PAUSED;
    return 0;
}

int vr_resume_recording(void)
{
    if (vr.state != VR_STATE_PAUSED) return -1;
    vr.state = VR_STATE_RECORDING;
    return 0;
}

int vr_stop_recording(void)
{
    if (vr.state != VR_STATE_RECORDING && vr.state != VR_STATE_PAUSED) return -1;
    if (vr.current_recording >= 0) {
        vr.recordings[vr.current_recording].duration = vr.record_position;
        uint32_t secs = (uint32_t)vr.record_position / 1000;
        vr.recordings[vr.current_recording].size = (uint64_t)secs * (uint32_t)vr.bitrate * 128;
        vr.recording_count++;
        vr.total_recordings++;
        vr.total_record_time += vr.record_position;
    }
    vr.state = VR_STATE_IDLE;
    return 0;
}

int vr_cancel_recording(void)
{
    if (vr.state != VR_STATE_RECORDING && vr.state != VR_STATE_PAUSED) return -1;
    if (vr.current_recording >= 0) {
        vr.recordings[vr.current_recording].used = 0;
        vr.current_recording = -1;
    }
    vr.state = VR_STATE_IDLE;
    return 0;
}

int vr_add_marker(const char *text)
{
    if (vr.state != VR_STATE_RECORDING) return -1;
    for (int i = 0; i < 128; i++) {
        if (!vr.markers[i].used) {
            memset(&vr.markers[i], 0, sizeof(vr_marker_t));
            vr.markers[i].used = 1;
            vr.markers[i].id = i;
            vr.markers[i].recording_id = vr.current_recording;
            vr.markers[i].timestamp = vr.record_position;
            if (text) strncpy(vr.markers[i].text, text, 255);
            vr.marker_count++;
            return i;
        }
    }
    return -1;
}

/* 播放控制 */
int vr_play(int recording_id)
{
    if (recording_id < 0 || recording_id >= VR_MAX_RECORDINGS || !vr.recordings[recording_id].used) return -1;
    vr.current_recording = recording_id;
    vr.play_position = 0;
    vr.state = VR_STATE_PLAYING;
    vr.recordings[recording_id].play_count++;
    return 0;
}

int vr_pause_playback(void)
{
    if (vr.state != VR_STATE_PLAYING) return -1;
    vr.state = VR_STATE_PAUSED;
    return 0;
}

int vr_resume_playback(void)
{
    if (vr.state != VR_STATE_PAUSED) return -1;
    vr.state = VR_STATE_PLAYING;
    return 0;
}

int vr_stop_playback(void)
{
    vr.state = VR_STATE_IDLE;
    vr.play_position = 0;
    return 0;
}

int vr_seek(uint64_t position)
{
    if (vr.current_recording < 0) return -1;
    if (position > vr.recordings[vr.current_recording].duration) return -1;
    vr.play_position = position;
    return 0;
}

int vr_set_volume(int volume)
{
    (void)volume;
    return 0;
}

/* 录音管理 */
int vr_get_recording(int id, vr_recording_t *rec)
{
    if (id < 0 || id >= VR_MAX_RECORDINGS || !vr.recordings[id].used || !rec) return -1;
    memcpy(rec, &vr.recordings[id], sizeof(vr_recording_t));
    return 0;
}

int vr_list_recordings(vr_recording_t *recs, int max)
{
    if (!recs || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < VR_MAX_RECORDINGS && count < max; i++) {
        if (vr.recordings[i].used) memcpy(&recs[count++], &vr.recordings[i], sizeof(vr_recording_t));
    }
    return count;
}

int vr_delete_recording(int id)
{
    if (id < 0 || id >= VR_MAX_RECORDINGS || !vr.recordings[id].used) return -1;
    vr.recordings[id].used = 0;
    vr.recording_count--;
    return 0;
}

int vr_rename_recording(int id, const char *title)
{
    if (id < 0 || id >= VR_MAX_RECORDINGS || !vr.recordings[id].used || !title) return -1;
    strncpy(vr.recordings[id].title, title, 127);
    vr.recordings[id].modified = 1;
    return 0;
}

int vr_set_favorite(int id, int favorite)
{
    if (id < 0 || id >= VR_MAX_RECORDINGS || !vr.recordings[id].used) return -1;
    vr.recordings[id].favorite = favorite ? 1 : 0;
    return 0;
}

int vr_search(const char *query, vr_recording_t *results, int max)
{
    if (!query || !results || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < VR_MAX_RECORDINGS && count < max; i++) {
        if (vr.recordings[i].used) {
            int match = 1;
            int qlen = 0; while (query[qlen]) qlen++;
            for (int j = 0; j < qlen; j++) {
                int found = 0;
                for (int k = 0; vr.recordings[i].title[k]; k++)
                    if (vr.recordings[i].title[k] == query[j]) { found = 1; break; }
                if (!found) { match = 0; break; }
            }
            if (match) memcpy(&results[count++], &vr.recordings[i], sizeof(vr_recording_t));
        }
    }
    return count;
}

int vr_get_waveform(int id, uint8_t *waveform, int max)
{
    if (id < 0 || id >= VR_MAX_RECORDINGS || !vr.recordings[id].used || !waveform) return -1;
    int count = 256 < max ? 256 : max;
    for (int i = 0; i < count; i++)
        waveform[i] = vr.recordings[id].waveform[i];
    return count;
}

/* 编辑 */
int vr_trim(int id, uint64_t start, uint64_t end)
{
    if (id < 0 || id >= VR_MAX_RECORDINGS || !vr.recordings[id].used) return -1;
    if (start >= end || end > vr.recordings[id].duration) return -1;
    vr.recordings[id].duration = end - start;
    vr.recordings[id].modified = 1;
    return 0;
}

int vr_split(int id, uint64_t position)
{
    if (id < 0 || id >= VR_MAX_RECORDINGS || !vr.recordings[id].used) return -1;
    if (position >= vr.recordings[id].duration) return -1;
    /* 创建第二个片段 */
    for (int i = 0; i < VR_MAX_RECORDINGS; i++) {
        if (!vr.recordings[i].used) {
            memcpy(&vr.recordings[i], &vr.recordings[id], sizeof(vr_recording_t));
            vr.recordings[i].id = i;
            vr.recordings[i].duration = vr.recordings[id].duration - position;
            vr.recording_count++;
            break;
        }
    }
    vr.recordings[id].duration = position;
    return 0;
}

int vr_merge(int *ids, int count, const char *title)
{
    if (!ids || count <= 0) return -1;
    for (int i = 0; i < VR_MAX_RECORDINGS; i++) {
        if (!vr.recordings[i].used) {
            memset(&vr.recordings[i], 0, sizeof(vr_recording_t));
            vr.recordings[i].used = 1;
            vr.recordings[i].id = i;
            if (title) strncpy(vr.recordings[i].title, title, 127);
            else strncpy(vr.recordings[i].title, " merge recording ", 127);
            vr.recordings[i].format = vr.format;
            vr.recordings[i].sample_rate = vr.sample_rate;
            vr.recordings[i].channels = vr.channels;
            for (int j = 0; j < count; j++) {
                if (ids[j] >= 0 && ids[j] < VR_MAX_RECORDINGS && vr.recordings[ids[j]].used)
                    vr.recordings[i].duration += vr.recordings[ids[j]].duration;
            }
            vr.recordings[i].created = 1;
            vr.recording_count++;
            return i;
        }
    }
    return -1;
}

int vr_convert_format(int id, vr_format_t format)
{
    if (id < 0 || id >= VR_MAX_RECORDINGS || !vr.recordings[id].used) return -1;
    vr.recordings[id].format = format;
    vr.recordings[id].modified = 1;
    return 0;
}

int vr_change_quality(int id, vr_quality_t quality)
{
    if (id < 0 || id >= VR_MAX_RECORDINGS || !vr.recordings[id].used) return -1;
    vr.recordings[id].quality = quality;
    vr.recordings[id].modified = 1;
    return 0;
}

/* 设置 */
int vr_set_format(vr_format_t format)
{
    vr.format = format;
    return 0;
}

int vr_set_quality(vr_quality_t quality)
{
    vr.quality = quality;
    switch (quality) {
        case VR_QUALITY_LOW: vr.sample_rate = 8000; vr.bitrate = 64; break;
        case VR_QUALITY_MEDIUM: vr.sample_rate = 22050; vr.bitrate = 128; break;
        case VR_QUALITY_HIGH: vr.sample_rate = 44100; vr.bitrate = 192; break;
        case VR_QUALITY_LOSSLESS: vr.sample_rate = 48000; vr.bitrate = 0; break;
    }
    return 0;
}

int vr_set_scene(vr_scene_t scene)
{
    vr.scene = scene;
    switch (scene) {
        case VR_SCENE_INTERVIEW: vr.noise_reduction = 1; vr.echo_cancellation = 1; break;
        case VR_SCENE_MEETING: vr.noise_reduction = 1; vr.skip_silence = 1; break;
        case VR_SCENE_MUSIC: vr.noise_reduction = 0; vr.echo_cancellation = 0; vr.channels = 2; break;
        case VR_SCENE_NOISE_REDUCTION: vr.noise_reduction = 1; vr.echo_cancellation = 1; vr.auto_gain = 1; break;
        default: break;
    }
    return 0;
}

int vr_set_gain(int gain)
{
    if (gain < 0) gain = 0;
    if (gain > 100) gain = 100;
    vr.gain = gain;
    return 0;
}

int vr_set_noise_reduction(int enable)
{
    vr.noise_reduction = enable ? 1 : 0;
    return 0;
}

int vr_set_echo_cancellation(int enable)
{
    vr.echo_cancellation = enable ? 1 : 0;
    return 0;
}

int vr_set_auto_gain(int enable)
{
    vr.auto_gain = enable ? 1 : 0;
    return 0;
}

int vr_set_skip_silence(int enable)
{
    vr.skip_silence = enable ? 1 : 0;
    return 0;
}

int vr_set_save_path(const char *path)
{
    if (!path) return -1;
    strncpy(vr.save_path, path, 255);
    return 0;
}

/* 标记 */
int vr_marker_add(int recording_id, uint64_t timestamp, const char *text)
{
    for (int i = 0; i < 128; i++) {
        if (!vr.markers[i].used) {
            memset(&vr.markers[i], 0, sizeof(vr_marker_t));
            vr.markers[i].used = 1;
            vr.markers[i].id = i;
            vr.markers[i].recording_id = recording_id;
            vr.markers[i].timestamp = timestamp;
            if (text) strncpy(vr.markers[i].text, text, 255);
            vr.marker_count++;
            return i;
        }
    }
    return -1;
}

int vr_marker_remove(int id)
{
    if (id < 0 || id >= 128 || !vr.markers[id].used) return -1;
    vr.markers[id].used = 0;
    vr.marker_count--;
    return 0;
}

int vr_marker_list(int recording_id, vr_marker_t *markers, int max)
{
    if (!markers || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < 128 && count < max; i++) {
        if (vr.markers[i].used && vr.markers[i].recording_id == recording_id)
            memcpy(&markers[count++], &vr.markers[i], sizeof(vr_marker_t));
    }
    return count;
}

/* 状态查询 */
int vr_get_state(vr_state_t *state)
{
    if (!state) return -1;
    *state = vr.state;
    return 0;
}

int vr_get_position(uint64_t *pos)
{
    if (!pos) return -1;
    if (vr.state == VR_STATE_RECORDING || vr.state == VR_STATE_PAUSED)
        *pos = vr.record_position;
    else
        *pos = vr.play_position;
    return 0;
}

int vr_get_duration(uint64_t *dur)
{
    if (!dur || vr.current_recording < 0) return -1;
    *dur = vr.recordings[vr.current_recording].duration;
    return 0;
}

int vr_get_stats(voice_recorder_t *stats)
{
    if (!stats) return -1;
    memcpy(stats, &vr, sizeof(voice_recorder_t));
    return 0;
}
