/* ============================================================================
 * 赫普OS - 编解码器子系统
 * 视频: H.264/H.265/VP9/AV1/MPEG4
 * 音频: AAC/MP3/Opus/FLAC/PCM
 * 颜色空间转换 + 音频重采样
 * ========================================================================== */

#include <stdint.h>
#include "codec.h"
#include "vga.h"
#include "string.h"
#include "kheap.h"

static codec_context_t contexts[CODEC_MAX_STREAMS];
static codec_capability_t capabilities[CODEC_MAX_DEVICES * 2];
static int capability_count = 0;
static int codec_initialized = 0;

void codec_init(void)
{
    for (int i = 0; i < CODEC_MAX_STREAMS; i++) {
        contexts[i].used = 0;
    }
    capability_count = 0;
    codec_initialized = 1;

    /* 注册内置编解码器能力 */
    codec_capability_t cap;

    memset(&cap, 0, sizeof(cap));
    cap.type = CODEC_VIDEO_H264;
    cap.direction = CODEC_DECODER;
    cap.name = "H.264 Decoder";
    cap.max_width = 3840;
    cap.max_height = 2160;
    cap.max_fps = 60;
    cap.max_bitrate = 50000000;
    cap.hardware_accelerated = 1;
    codec_register(&cap);

    memset(&cap, 0, sizeof(cap));
    cap.type = CODEC_VIDEO_H264;
    cap.direction = CODEC_ENCODER;
    cap.name = "H.264 Encoder";
    cap.max_width = 1920;
    cap.max_height = 1080;
    cap.max_fps = 30;
    cap.max_bitrate = 20000000;
    cap.hardware_accelerated = 1;
    codec_register(&cap);

    memset(&cap, 0, sizeof(cap));
    cap.type = CODEC_VIDEO_H265;
    cap.direction = CODEC_DECODER;
    cap.name = "H.265/HEVC Decoder";
    cap.max_width = 4096;
    cap.max_height = 2160;
    cap.max_fps = 60;
    cap.max_bitrate = 80000000;
    cap.hardware_accelerated = 1;
    codec_register(&cap);

    memset(&cap, 0, sizeof(cap));
    cap.type = CODEC_VIDEO_VP9;
    cap.direction = CODEC_DECODER;
    cap.name = "VP9 Decoder";
    cap.max_width = 3840;
    cap.max_height = 2160;
    cap.max_fps = 60;
    cap.max_bitrate = 50000000;
    cap.hardware_accelerated = 0;
    codec_register(&cap);

    memset(&cap, 0, sizeof(cap));
    cap.type = CODEC_AUDIO_AAC;
    cap.direction = CODEC_DECODER;
    cap.name = "AAC Decoder";
    cap.max_bitrate = 320000;
    cap.hardware_accelerated = 0;
    codec_register(&cap);

    memset(&cap, 0, sizeof(cap));
    cap.type = CODEC_AUDIO_MP3;
    cap.direction = CODEC_DECODER;
    cap.name = "MP3 Decoder";
    cap.max_bitrate = 320000;
    cap.hardware_accelerated = 0;
    codec_register(&cap);

    memset(&cap, 0, sizeof(cap));
    cap.type = CODEC_AUDIO_OPUS;
    cap.direction = CODEC_DECODER;
    cap.name = "Opus Decoder";
    cap.max_bitrate = 510000;
    cap.hardware_accelerated = 0;
    codec_register(&cap);

    memset(&cap, 0, sizeof(cap));
    cap.type = CODEC_AUDIO_OPUS;
    cap.direction = CODEC_ENCODER;
    cap.name = "Opus Encoder";
    cap.max_bitrate = 510000;
    cap.hardware_accelerated = 0;
    codec_register(&cap);

    vga_puts("[CODEC] codec subsystem init done \n");
    vga_printf(" registered %d codec \n", capability_count);
}

int codec_register(codec_capability_t *cap)
{
    if (!cap || !codec_initialized) return -1;
    if (capability_count >= CODEC_MAX_DEVICES * 2) return -1;
    memcpy(&capabilities[capability_count++], cap, sizeof(codec_capability_t));
    return 0;
}

int codec_open(codec_type_t type, codec_direction_t dir, int width, int height,
               int fps, int bitrate)
{
    if (!codec_initialized) return -1;

    /* 查找匹配的编解码器能力 */
    int found = 0;
    for (int i = 0; i < capability_count; i++) {
        if (capabilities[i].type == type && capabilities[i].direction == dir) {
            found = 1;
            break;
        }
    }
    if (!found) {
        vga_printf("[CODEC] unsupported codec: type=%d dir=%d\n", type, dir);
        return -1;
    }

    for (int i = 0; i < CODEC_MAX_STREAMS; i++) {
        if (!contexts[i].used) {
            memset(&contexts[i], 0, sizeof(codec_context_t));
            contexts[i].used = 1;
            contexts[i].id = i;
            contexts[i].type = type;
            contexts[i].direction = dir;
            contexts[i].state = CODEC_STATE_READY;
            contexts[i].width = width;
            contexts[i].height = height;
            contexts[i].fps = fps;
            contexts[i].bitrate = bitrate;
            contexts[i].pix_fmt = CODEC_PIX_FMT_YUV420P;

            const char *name = "Unknown";
            switch (type) {
                case CODEC_VIDEO_H264: name = "H.264"; break;
                case CODEC_VIDEO_H265: name = "H.265"; break;
                case CODEC_VIDEO_VP9: name = "VP9"; break;
                case CODEC_VIDEO_AV1: name = "AV1"; break;
                case CODEC_VIDEO_MPEG4: name = "MPEG4"; break;
                case CODEC_AUDIO_AAC: name = "AAC"; break;
                case CODEC_AUDIO_MP3: name = "MP3"; break;
                case CODEC_AUDIO_OPUS: name = "Opus"; break;
                case CODEC_AUDIO_FLAC: name = "FLAC"; break;
                case CODEC_AUDIO_PCM: name = "PCM"; break;
            }
            contexts[i].name = name;

            vga_printf("[CODEC] open %s %s: %dx%d@%dfps, %dkbps\n",
                       name, dir == CODEC_DECODER ? "Decoder" : "Encoder",
                       width, height, fps, bitrate / 1000);
            return i;
        }
    }
    return -1;
}

int codec_close(int ctx_id)
{
    if (ctx_id < 0 || ctx_id >= CODEC_MAX_STREAMS || !contexts[ctx_id].used) return -1;
    contexts[ctx_id].used = 0;
    contexts[ctx_id].state = CODEC_STATE_UNINIT;
    return 0;
}

int codec_decode(int ctx_id, codec_packet_t *pkt, codec_video_frame_t *frame)
{
    if (ctx_id < 0 || ctx_id >= CODEC_MAX_STREAMS || !contexts[ctx_id].used) return -1;
    if (!pkt || !frame) return -1;
    if (contexts[ctx_id].direction != CODEC_DECODER) return -1;

    contexts[ctx_id].state = CODEC_STATE_RUNNING;

    /* 原型阶段: 模拟解码, 生成测试帧 */
    frame->width = contexts[ctx_id].width;
    frame->height = contexts[ctx_id].height;
    frame->format = contexts[ctx_id].pix_fmt;
    frame->pts = pkt->pts;
    frame->dts = pkt->dts;
    frame->key_frame = (pkt->flags & 1) ? 1 : 0;
    frame->used = 1;

    contexts[ctx_id].frames_decoded++;
    contexts[ctx_id].bytes_processed += pkt->size;
    return 0;
}

int codec_encode(int ctx_id, codec_video_frame_t *frame, codec_packet_t *pkt)
{
    if (ctx_id < 0 || ctx_id >= CODEC_MAX_STREAMS || !contexts[ctx_id].used) return -1;
    if (!frame || !pkt) return -1;
    if (contexts[ctx_id].direction != CODEC_ENCODER) return -1;

    contexts[ctx_id].state = CODEC_STATE_RUNNING;

    /* 原型阶段: 模拟编码 */
    pkt->size = frame->width * frame->height / 10;  /* 模拟压缩比 */
    pkt->pts = frame->pts;
    pkt->dts = frame->dts;
    pkt->flags = frame->key_frame ? 1 : 0;
    pkt->used = 1;

    contexts[ctx_id].frames_encoded++;
    return 0;
}

int codec_decode_audio(int ctx_id, codec_packet_t *pkt, codec_audio_frame_t *frame)
{
    if (ctx_id < 0 || ctx_id >= CODEC_MAX_STREAMS || !contexts[ctx_id].used) return -1;
    if (!pkt || !frame) return -1;

    /* 原型阶段: 模拟音频解码 */
    frame->sample_rate = contexts[ctx_id].sample_rate ? contexts[ctx_id].sample_rate : 44100;
    frame->channels = contexts[ctx_id].channels ? contexts[ctx_id].channels : 2;
    frame->format = CODEC_SAMPLE_FMT_S16;
    frame->size = 1024 * frame->channels * 2;
    frame->pts = pkt->pts;
    frame->used = 1;

    contexts[ctx_id].frames_decoded++;
    return 0;
}

int codec_encode_audio(int ctx_id, codec_audio_frame_t *frame, codec_packet_t *pkt)
{
    if (ctx_id < 0 || ctx_id >= CODEC_MAX_STREAMS || !contexts[ctx_id].used) return -1;
    if (!frame || !pkt) return -1;

    /* 原型阶段: 模拟音频编码 */
    pkt->size = frame->size / 4;  /* 模拟压缩比 4:1 */
    pkt->pts = frame->pts;
    pkt->used = 1;

    contexts[ctx_id].frames_encoded++;
    return 0;
}

int codec_flush(int ctx_id)
{
    if (ctx_id < 0 || ctx_id >= CODEC_MAX_STREAMS || !contexts[ctx_id].used) return -1;
    contexts[ctx_id].state = CODEC_STATE_FLUSHING;
    /* 清空缓冲区 */
    for (int i = 0; i < CODEC_MAX_BUFFER; i++) {
        contexts[ctx_id].video_frames[i].used = 0;
        contexts[ctx_id].audio_frames[i].used = 0;
        contexts[ctx_id].packets[i].used = 0;
    }
    contexts[ctx_id].frame_count = 0;
    contexts[ctx_id].packet_count = 0;
    contexts[ctx_id].state = CODEC_STATE_READY;
    return 0;
}

int codec_list_capabilities(codec_capability_t *caps, int max)
{
    if (!caps || max <= 0) return 0;
    int count = (capability_count < max) ? capability_count : max;
    memcpy(caps, capabilities, count * sizeof(codec_capability_t));
    return count;
}

int codec_get_info(int ctx_id, codec_context_t *info)
{
    if (ctx_id < 0 || ctx_id >= CODEC_MAX_STREAMS || !contexts[ctx_id].used) return -1;
    if (info) memcpy(info, &contexts[ctx_id], sizeof(codec_context_t));
    return 0;
}

codec_state_t codec_get_state(int ctx_id)
{
    if (ctx_id < 0 || ctx_id >= CODEC_MAX_STREAMS || !contexts[ctx_id].used) return CODEC_STATE_UNINIT;
    return contexts[ctx_id].state;
}

/* YUV420 -> RGB565 颜色空间转换 */
int codec_yuv420_to_rgb565(uint8_t *y, uint8_t *u, uint8_t *v,
                           int width, int height, int y_stride, int uv_stride,
                           uint16_t *rgb)
{
    if (!y || !u || !v || !rgb) return -1;
    for (int j = 0; j < height; j++) {
        for (int i = 0; i < width; i++) {
            int Y = y[j * y_stride + i] - 16;
            int U = u[(j / 2) * uv_stride + (i / 2)] - 128;
            int V = v[(j / 2) * uv_stride + (i / 2)] - 128;

            int r = (298 * Y + 409 * V + 128) >> 8;
            int g = (298 * Y - 100 * U - 208 * V + 128) >> 8;
            int b = (298 * Y + 516 * U + 128) >> 8;

            if (r < 0) r = 0;
            if (r > 255) r = 255;
            if (g < 0) g = 0;
            if (g > 255) g = 255;
            if (b < 0) b = 0;
            if (b > 255) b = 255;

            rgb[j * width + i] = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
        }
    }
    return width * height * 2;
}

/* RGB565 -> YUV420 颜色空间转换 */
int codec_rgb565_to_yuv420(uint16_t *rgb, int width, int height,
                           uint8_t *y, uint8_t *u, uint8_t *v)
{
    if (!rgb || !y || !u || !v) return -1;
    for (int j = 0; j < height; j++) {
        for (int i = 0; i < width; i++) {
            uint16_t px = rgb[j * width + i];
            int r = ((px >> 11) & 0x1F) << 3;
            int g = ((px >> 5) & 0x3F) << 2;
            int b = (px & 0x1F) << 3;

            int Y = ((66 * r + 129 * g + 25 * b + 128) >> 8) + 16;
            int U = ((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128;
            int V = ((112 * r - 94 * g - 18 * b + 128) >> 8) + 128;

            if (Y < 0) Y = 0;
            if (Y > 255) Y = 255;
            if (U < 0) U = 0;
            if (U > 255) U = 255;
            if (V < 0) V = 0;
            if (V > 255) V = 255;

            y[j * width + i] = (uint8_t)Y;
            if (j % 2 == 0 && i % 2 == 0) {
                u[(j / 2) * (width / 2) + (i / 2)] = (uint8_t)U;
                v[(j / 2) * (width / 2) + (i / 2)] = (uint8_t)V;
            }
        }
    }
    return width * height + (width / 2) * (height / 2) * 2;
}

/* 音频重采样 (线性插值) */
int codec_resample(int16_t *input, int in_rate, int in_channels,
                   int16_t *output, int out_rate, int out_channels, int samples)
{
    if (!input || !output) return -1;
    if (in_rate == out_rate && in_channels == out_channels) {
        memcpy(output, input, samples * in_channels * 2);
        return samples;
    }

    double ratio = (double)out_rate / in_rate;
    int out_samples = (int)(samples * ratio);

    for (int i = 0; i < out_samples; i++) {
        double src_pos = i / ratio;
        int src_idx = (int)src_pos;
        double frac = src_pos - src_idx;

        for (int ch = 0; ch < out_channels; ch++) {
            int in_ch = (ch < in_channels) ? ch : (in_channels - 1);
            int16_t s1 = (src_idx < samples) ? input[src_idx * in_channels + in_ch] : 0;
            int16_t s2 = (src_idx + 1 < samples) ? input[(src_idx + 1) * in_channels + in_ch] : s1;
            output[i * out_channels + ch] = (int16_t)(s1 + (s2 - s1) * frac);
        }
    }
    return out_samples;
}

int codec_active_count(void)
{
    int count = 0;
    for (int i = 0; i < CODEC_MAX_STREAMS; i++) {
        if (contexts[i].used) count++;
    }
    return count;
}
