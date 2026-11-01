/* ============================================================================
 * 赫普OS - ALSA式音频驱动
 * PCM播放/录制 + 混音器 + 缓冲区管理
 * ========================================================================== */

#include <stdint.h>
#include "audio.h"
#include "vga.h"
#include "string.h"
#include "kheap.h"
#include "pit.h"

static audio_device_t devices[AUDIO_MAX_DEVICES];
static int device_count = 0;
static int next_stream_id = 1;
static int audio_initialized = 0;

/* 正弦波表 (用于测试) */
#define SIN_TABLE_SIZE 256
static int16_t sin_table[SIN_TABLE_SIZE];

static void init_sin_table(void)
{
    for (int i = 0; i < SIN_TABLE_SIZE; i++) {
        /* 简化的正弦波近似 */
        int x = i * 2 - SIN_TABLE_SIZE;
        sin_table[i] = (int16_t)(x * (32767 - (x * x) / 256) / 256);
    }
}

void audio_init(void)
{
    for (int i = 0; i < AUDIO_MAX_DEVICES; i++) {
        devices[i].used = 0;
    }
    device_count = 0;
    next_stream_id = 1;
    audio_initialized = 1;

    init_sin_table();

    /* 注册默认设备 (模拟AC97) */
    audio_device_t *dev = &devices[0];
    memset(dev, 0, sizeof(audio_device_t));
    dev->used = 1;
    dev->name = "AC97 Audio";
    dev->type = 0;
    dev->io_base = 0xD000;
    dev->irq = 5;
    dev->initialized = 1;

    /* 混音器控制 */
    dev->mixer[0].name = "Master"; dev->mixer[0].min = 0; dev->mixer[0].max = 100; dev->mixer[0].current = 75;
    dev->mixer[1].name = "PCM"; dev->mixer[1].min = 0; dev->mixer[1].max = 100; dev->mixer[1].current = 80;
    dev->mixer[2].name = "Line"; dev->mixer[2].min = 0; dev->mixer[2].max = 100; dev->mixer[2].current = 50;
    dev->mixer[3].name = "Mic"; dev->mixer[3].min = 0; dev->mixer[3].max = 100; dev->mixer[3].current = 60;
    dev->mixer_count = 4;

    device_count = 1;
    vga_puts("[AUDIO] audio subsystem init done \n");
    vga_printf(" device: %s, mixer: %d ctrl \n", dev->name, dev->mixer_count);
}

int audio_pcm_open(int device, audio_dir_t dir, audio_format_t fmt,
                    uint32_t rate, uint8_t channels)
{
    if (device < 0 || device >= AUDIO_MAX_DEVICES || !devices[device].used) return -1;

    audio_pcm_t *pcm = (dir == AUDIO_DIR_PLAYBACK) ?
        &devices[device].pcm_playback : &devices[device].pcm_capture;

    if (pcm->used) return -1;

    memset(pcm, 0, sizeof(audio_pcm_t));
    pcm->used = 1;
    pcm->stream_id = next_stream_id++;
    pcm->direction = dir;
    pcm->format = fmt;
    pcm->sample_rate = rate;
    pcm->channels = channels;
    pcm->period_size = 1024;
    pcm->buffer_size = 4096;
    pcm->state = AUDIO_STATE_OPEN;
    pcm->volume = 80;

    /* 分配缓冲区 */
    pcm->buffer_count = 4;
    for (int i = 0; i < pcm->buffer_count; i++) {
        pcm->buffers[i].data = kmalloc(pcm->buffer_size);
        pcm->buffers[i].size = pcm->buffer_size;
        pcm->buffers[i].used = 0;
        pcm->buffers[i].in_use = 0;
    }

    const char *dir_name = (dir == AUDIO_DIR_PLAYBACK) ? " 播放 " : " record ";
    vga_printf("[AUDIO] PCM open: %s %dHz %dch streams ID=%d\n",
               dir_name, rate, channels, pcm->stream_id);
    return pcm->stream_id;
}

int audio_pcm_close(int stream_id)
{
    for (int d = 0; d < AUDIO_MAX_DEVICES; d++) {
        if (!devices[d].used) continue;
        audio_pcm_t *pcms[] = {&devices[d].pcm_playback, &devices[d].pcm_capture};
        for (int p = 0; p < 2; p++) {
            if (pcms[p]->used && pcms[p]->stream_id == stream_id) {
                for (int i = 0; i < pcms[p]->buffer_count; i++) {
                    if (pcms[p]->buffers[i].data) kfree(pcms[p]->buffers[i].data);
                }
                pcms[p]->used = 0;
                vga_printf("[AUDIO] PCM close: streams ID=%d\n", stream_id);
                return 0;
            }
        }
    }
    return -1;
}

static audio_pcm_t *find_pcm(int stream_id)
{
    for (int d = 0; d < AUDIO_MAX_DEVICES; d++) {
        if (!devices[d].used) continue;
        if (devices[d].pcm_playback.used && devices[d].pcm_playback.stream_id == stream_id)
            return &devices[d].pcm_playback;
        if (devices[d].pcm_capture.used && devices[d].pcm_capture.stream_id == stream_id)
            return &devices[d].pcm_capture;
    }
    return NULL;
}

int audio_pcm_write(int stream_id, const void *data, uint32_t size)
{
    audio_pcm_t *pcm = find_pcm(stream_id);
    if (!pcm || pcm->direction != AUDIO_DIR_PLAYBACK) return -1;

    /* 原型阶段: 复制到缓冲区, 模拟播放 */
    audio_buffer_t *buf = &pcm->buffers[pcm->current_buffer];
    uint32_t to_copy = size;
    if (to_copy > buf->size) to_copy = buf->size;
    memcpy(buf->data, data, to_copy);
    buf->used = to_copy;
    pcm->current_buffer = (pcm->current_buffer + 1) % pcm->buffer_count;
    pcm->total_samples += to_copy / 2;

    return to_copy;
}

int audio_pcm_read(int stream_id, void *data, uint32_t size)
{
    audio_pcm_t *pcm = find_pcm(stream_id);
    if (!pcm || pcm->direction != AUDIO_DIR_CAPTURE) return -1;

    /* 原型阶段: 返回静音 */
    memset(data, 0, size);
    return size;
}

int audio_pcm_start(int stream_id)
{
    audio_pcm_t *pcm = find_pcm(stream_id);
    if (!pcm) return -1;
    pcm->state = AUDIO_STATE_RUNNING;
    vga_printf("[AUDIO] PCM start: streams ID=%d\n", stream_id);
    return 0;
}

int audio_pcm_stop(int stream_id)
{
    audio_pcm_t *pcm = find_pcm(stream_id);
    if (!pcm) return -1;
    pcm->state = AUDIO_STATE_SETUP;
    return 0;
}

int audio_pcm_pause(int stream_id)
{
    audio_pcm_t *pcm = find_pcm(stream_id);
    if (!pcm) return -1;
    pcm->state = AUDIO_STATE_PAUSED;
    return 0;
}

int audio_pcm_resume(int stream_id)
{
    audio_pcm_t *pcm = find_pcm(stream_id);
    if (!pcm) return -1;
    pcm->state = AUDIO_STATE_RUNNING;
    return 0;
}

int audio_set_volume(int stream_id, int volume)
{
    audio_pcm_t *pcm = find_pcm(stream_id);
    if (!pcm) return -1;
    if (volume < 0) volume = 0;
    if (volume > 100) volume = 100;
    pcm->volume = volume;
    return 0;
}

int audio_get_volume(int stream_id)
{
    audio_pcm_t *pcm = find_pcm(stream_id);
    if (!pcm) return -1;
    return pcm->volume;
}

int audio_set_mute(int stream_id, int mute)
{
    audio_pcm_t *pcm = find_pcm(stream_id);
    if (!pcm) return -1;
    (void)mute;
    return 0;
}

uint32_t audio_get_delay(int stream_id)
{
    audio_pcm_t *pcm = find_pcm(stream_id);
    if (!pcm) return 0;
    return pcm->buffer_size;
}

int audio_mixer_set(const char *name, int value)
{
    for (int d = 0; d < AUDIO_MAX_DEVICES; d++) {
        if (!devices[d].used) continue;
        for (int i = 0; i < devices[d].mixer_count; i++) {
            if (strcmp(devices[d].mixer[i].name, name) == 0) {
                if (value < devices[d].mixer[i].min) value = devices[d].mixer[i].min;
                if (value > devices[d].mixer[i].max) value = devices[d].mixer[i].max;
                devices[d].mixer[i].current = value;
                return 0;
            }
        }
    }
    return -1;
}

int audio_mixer_get(const char *name)
{
    for (int d = 0; d < AUDIO_MAX_DEVICES; d++) {
        if (!devices[d].used) continue;
        for (int i = 0; i < devices[d].mixer_count; i++) {
            if (strcmp(devices[d].mixer[i].name, name) == 0) {
                return devices[d].mixer[i].current;
            }
        }
    }
    return -1;
}

int audio_generate_tone(int stream_id, int freq, int duration_ms)
{
    audio_pcm_t *pcm = find_pcm(stream_id);
    if (!pcm || pcm->direction != AUDIO_DIR_PLAYBACK) return -1;

    uint32_t sample_count = (pcm->sample_rate * duration_ms) / 1000;
    int16_t *buffer = kmalloc(sample_count * 2);
    if (!buffer) return -1;

    /* 生成正弦波 */
    uint32_t phase = 0;
    uint32_t phase_inc = (freq * SIN_TABLE_SIZE) / pcm->sample_rate;
    int vol = pcm->volume * 32767 / 100;

    for (uint32_t i = 0; i < sample_count; i++) {
        int16_t sample = (int16_t)((sin_table[phase % SIN_TABLE_SIZE] * vol) / 32767);
        for (int ch = 0; ch < pcm->channels; ch++) {
            buffer[i * pcm->channels + ch] = sample;
        }
        phase += phase_inc;
    }

    int written = audio_pcm_write(stream_id, buffer, sample_count * 2 * pcm->channels);
    kfree(buffer);
    return written;
}

void audio_handle_irq(void)
{
    /* 原型阶段: 模拟音频中断 */
    for (int d = 0; d < AUDIO_MAX_DEVICES; d++) {
        if (!devices[d].used) continue;
        if (devices[d].pcm_playback.state == AUDIO_STATE_RUNNING) {
            /* 推进缓冲区 */
        }
    }
}
