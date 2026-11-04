#ifndef CODEC_H
#define CODEC_H

#include <stdint.h>

#define CODEC_MAX_DEVICES 4
#define CODEC_MAX_STREAMS 16
#define CODEC_MAX_BUFFER 32

/* 编解码器类型 */
typedef enum {
    CODEC_VIDEO_H264 = 0,
    CODEC_VIDEO_H265,
    CODEC_VIDEO_VP9,
    CODEC_VIDEO_AV1,
    CODEC_VIDEO_MPEG4,
    CODEC_AUDIO_AAC,
    CODEC_AUDIO_MP3,
    CODEC_AUDIO_OPUS,
    CODEC_AUDIO_FLAC,
    CODEC_AUDIO_PCM,
} codec_type_t;

/* 编解码器方向 */
typedef enum {
    CODEC_DECODER = 0,
    CODEC_ENCODER,
} codec_direction_t;

/* 像素格式 */
typedef enum {
    CODEC_PIX_FMT_YUV420P = 0,
    CODEC_PIX_FMT_NV12,
    CODEC_PIX_FMT_RGB565,
    CODEC_PIX_FMT_RGB888,
    CODEC_PIX_FMT_RGBA8888,
} codec_pix_fmt_t;

/* 音频采样格式 */
typedef enum {
    CODEC_SAMPLE_FMT_U8 = 0,
    CODEC_SAMPLE_FMT_S16,
    CODEC_SAMPLE_FMT_S32,
    CODEC_SAMPLE_FMT_FLT,
} codec_sample_fmt_t;

/* 编解码器状态 */
typedef enum {
    CODEC_STATE_UNINIT = 0,
    CODEC_STATE_READY,
    CODEC_STATE_RUNNING,
    CODEC_STATE_FLUSHING,
    CODEC_STATE_EOS,
    CODEC_STATE_ERROR,
} codec_state_t;

/* 视频帧 */
typedef struct {
    uint8_t *data[3];      /* Y/U/V 或 RGB 平面 */
    int linesize[3];       /* 每行字节数 */
    int width;
    int height;
    codec_pix_fmt_t format;
    uint64_t pts;          /* 显示时间戳 */
    uint64_t dts;          /* 解码时间戳 */
    int key_frame;
    int used;
} codec_video_frame_t;

/* 音频帧 */
typedef struct {
    uint8_t *data;
    int size;
    int sample_rate;
    int channels;
    codec_sample_fmt_t format;
    uint64_t pts;
    int used;
} codec_audio_frame_t;

/* 编码包 */
typedef struct {
    uint8_t *data;
    int size;
    uint64_t pts;
    uint64_t dts;
    int flags;
    int used;
} codec_packet_t;

/* 编解码器能力 */
typedef struct {
    codec_type_t type;
    codec_direction_t direction;
    const char *name;
    int max_width;
    int max_height;
    int max_fps;
    int max_bitrate;
    int supported_profiles[8];
    int profile_count;
    int hardware_accelerated;
} codec_capability_t;

/* 编解码器上下文 */
typedef struct {
    int used;
    int id;
    codec_type_t type;
    codec_direction_t direction;
    codec_state_t state;
    const char *name;
    /* 视频参数 */
    int width;
    int height;
    codec_pix_fmt_t pix_fmt;
    int fps;
    int bitrate;
    int gop_size;
    /* 音频参数 */
    int sample_rate;
    int channels;
    codec_sample_fmt_t sample_fmt;
    int bitrate_audio;
    /* 缓冲区 */
    codec_video_frame_t video_frames[CODEC_MAX_BUFFER];
    codec_audio_frame_t audio_frames[CODEC_MAX_BUFFER];
    codec_packet_t packets[CODEC_MAX_BUFFER];
    int frame_count;
    int packet_count;
    /* 统计 */
    uint64_t frames_decoded;
    uint64_t frames_encoded;
    uint64_t bytes_processed;
    uint64_t errors;
} codec_context_t;

/* 初始化编解码子系统 */
void codec_init(void);

/* 注册编解码器 */
int codec_register(codec_capability_t *cap);

/* 打开编解码器 */
int codec_open(codec_type_t type, codec_direction_t dir, int width, int height,
               int fps, int bitrate);

/* 关闭编解码器 */
int codec_close(int ctx_id);

/* 解码一个包 */
int codec_decode(int ctx_id, codec_packet_t *pkt, codec_video_frame_t *frame);

/* 编码一帧 */
int codec_encode(int ctx_id, codec_video_frame_t *frame, codec_packet_t *pkt);

/* 音频解码 */
int codec_decode_audio(int ctx_id, codec_packet_t *pkt, codec_audio_frame_t *frame);

/* 音频编码 */
int codec_encode_audio(int ctx_id, codec_audio_frame_t *frame, codec_packet_t *pkt);

/* 刷新编解码器 */
int codec_flush(int ctx_id);

/* 获取编解码器能力列表 */
int codec_list_capabilities(codec_capability_t *caps, int max);

/* 获取编解码器信息 */
int codec_get_info(int ctx_id, codec_context_t *info);

/* 获取编解码器状态 */
codec_state_t codec_get_state(int ctx_id);

/* 颜色空间转换: YUV420 -> RGB565 */
int codec_yuv420_to_rgb565(uint8_t *y, uint8_t *u, uint8_t *v,
                           int width, int height, int y_stride, int uv_stride,
                           uint16_t *rgb);

/* 颜色空间转换: RGB565 -> YUV420 */
int codec_rgb565_to_yuv420(uint16_t *rgb, int width, int height,
                           uint8_t *y, uint8_t *u, uint8_t *v);

/* 音频重采样 */
int codec_resample(int16_t *input, int in_rate, int in_channels,
                   int16_t *output, int out_rate, int out_channels, int samples);

/* 获取活跃编解码器数量 */
int codec_active_count(void);

#endif /* CODEC_H */
