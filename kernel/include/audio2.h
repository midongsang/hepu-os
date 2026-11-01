#ifndef AUDIO2_H
#define AUDIO2_H

#include <stdint.h>

#define A2_MAX_OUTPUTS 8
#define A2_MAX_INPUTS 4
#define A2_MAX_EFFECTS 16
#define A2_MAX_SOURCES 32
#define A2_MAX_MIDI_DEVICES 8
#define A2_MAX_MIDI_NOTES 128
#define A2_MAX_VOICE_COMMANDS 32

/* 音频设备类型 */
typedef enum {
    A2_DEVICE_SPEAKER = 0,
    A2_DEVICE_EARPIECE,
    A2_DEVICE_HEADPHONE,
    A2_DEVICE_BLUETOOTH,
    A2_DEVICE_USB,
    A2_DEVICE_HDMI,
    A2_DEVICE_AUX,
} a2_device_type_t;

/* 音频格式 */
typedef enum {
    A2_FORMAT_PCM_S16 = 0,
    A2_FORMAT_PCM_S24,
    A2_FORMAT_PCM_S32,
    A2_FORMAT_PCM_F32,
    A2_FORMAT_AAC,
    A2_FORMAT_OPUS,
    A2_FORMAT_FLAC,
    A2_FORMAT_MP3,
    A2_FORMAT_VORBIS,
} a2_format_t;

/* 采样率 */
typedef enum {
    A2_RATE_8000 = 8000,
    A2_RATE_16000 = 16000,
    A2_RATE_22050 = 22050,
    A2_RATE_32000 = 32000,
    A2_RATE_44100 = 44100,
    A2_RATE_48000 = 48000,
    A2_RATE_96000 = 96000,
    A2_RATE_192000 = 192000,
} a2_sample_rate_t;

/* 声道配置 */
typedef enum {
    A2_CHANNEL_MONO = 1,
    A2_CHANNEL_STEREO = 2,
    A2_CHANNEL_2_1 = 3,
    A2_CHANNEL_4_0 = 4,
    A2_CHANNEL_5_1 = 6,
    A2_CHANNEL_7_1 = 8,
    A2_CHANNEL_DOLBY_ATMOS = 16,
} a2_channel_t;

/* 音频输出设备 */
typedef struct {
    int used;
    int id;
    char name[64];
    a2_device_type_t type;
    a2_sample_rate_t sample_rate;
    a2_format_t format;
    a2_channel_t channels;
    int volume;  /* 0-100 */
    int muted;
    int active;
    int latency_ms;
    int max_volume;
    int min_volume;
    int support_hires;
    int support_spatial;
    int support_noise_cancel;
} a2_output_t;

/* 音频输入设备 */
typedef struct {
    int used;
    int id;
    char name[64];
    a2_device_type_t type;
    a2_sample_rate_t sample_rate;
    a2_format_t format;
    a2_channel_t channels;
    int gain;  /* 0-100 */
    int muted;
    int active;
    int latency_ms;
    int support_ec;  /* 回声消除 */
    int support_ns;  /* 噪声抑制 */
    int support_agc;  /* 自动增益 */
} a2_input_t;

/* 音频源 */
typedef struct {
    int used;
    int id;
    char name[64];
    int pid;
    a2_sample_rate_t sample_rate;
    a2_format_t format;
    a2_channel_t channels;
    int volume;
    int playing;
    int paused;
    uint64_t position;  /* 帧 */
    uint64_t duration;  /* 帧 */
    int loop;
    int priority;  /* 0=低,1=中,2=高,3=通话 */
    int ducking;  /* 闪避 */
} a2_source_t;

/* 音效类型 */
typedef enum {
    A2_EFFECT_EQUALIZER = 0,
    A2_EFFECT_BASS_BOOST,
    A2_EFFECT_VIRTUALIZER,
    A2_EFFECT_REVERB,
    A2_EFFECT_COMPRESSOR,
    A2_EFFECT_LIMITER,
    A2_EFFECT_NOISE_SUPPRESSION,
    A2_EFFECT_ECHO_CANCELLATION,
    A2_EFFECT_AUTO_GAIN,
    A2_EFFECT_SPATIAL_AUDIO,
    A2_EFFECT_DOLBY_ATMOS,
    A2_EFFECT_HIFI,
    A2_EFFECT_LOUDNESS,
} a2_effect_type_t;

/* 音效 */
typedef struct {
    int used;
    int id;
    char name[64];
    a2_effect_type_t type;
    int enabled;
    int strength;  /* 0-100 */
    int params[8];
} a2_effect_t;

/* 空间音频 */
typedef struct {
    int enabled;
    int mode;  /* 0=off,1=fixed,2=head_tracking */
    int hrtf;  /* 头相关传输函数 */
    float head_orientation[3];  /* yaw, pitch, roll */
    int room_correction;
    int personalized;
} a2_spatial_t;

/* 降噪 */
typedef struct {
    int enabled;
    int mode;  /* 0=off,1=transparency,2=noise_cancel,3=adaptive */
    int strength;  /* 0-100 */
    int anc_level;  /* 主动降噪等级 */
    int transparency_level;
    int wind_noise_reduction;
    int voice_isolation;
} a2_noise_cancel_t;

/* 语音唤醒 */
typedef struct {
    int enabled;
    char wake_word[64];
    int sensitivity;  /* 0-100 */
    int always_on;
    int low_power_mode;
    int model_version;
    int language;
    int voice_id;  /* 声纹识别 */
    int multi_user;
} a2_voice_wake_t;

/* 语音命令 */
typedef struct {
    int id;
    char command[128];
    char action[128];
    int confidence;
} a2_voice_command_t;

/* 低延迟音频 */
typedef struct {
    int enabled;
    int target_latency_ms;
    int actual_latency_ms;
    int buffer_size;
    int period_size;
    int dsp_offload;
    int a2dp_offload;
} a2_low_latency_t;

/* MIDI设备 */
typedef struct {
    int used;
    int id;
    char name[64];
    int connected;
    int input_port;
    int output_port;
    int channels;
    int notes_active[A2_MAX_MIDI_NOTES];
    int program;
    int bank;
} a2_midi_device_t;

/* MIDI事件 */
typedef struct {
    uint8_t status;
    uint8_t data1;
    uint8_t data2;
    uint32_t timestamp;
} a2_midi_event_t;

/* 音频深化状态 */
typedef struct {
    int initialized;
    /* 设备 */
    a2_output_t outputs[A2_MAX_OUTPUTS];
    int output_count;
    a2_input_t inputs[A2_MAX_INPUTS];
    int input_count;
    int active_output;
    int active_input;
    /* 源 */
    a2_source_t sources[A2_MAX_SOURCES];
    int source_count;
    /* 音效 */
    a2_effect_t effects[A2_MAX_EFFECTS];
    int effect_count;
    /* 空间音频 */
    a2_spatial_t spatial;
    /* 降噪 */
    a2_noise_cancel_t noise_cancel;
    /* 语音唤醒 */
    a2_voice_wake_t voice_wake;
    a2_voice_command_t voice_commands[A2_MAX_VOICE_COMMANDS];
    int voice_command_count;
    /* 低延迟 */
    a2_low_latency_t low_latency;
    /* MIDI */
    a2_midi_device_t midi_devices[A2_MAX_MIDI_DEVICES];
    int midi_device_count;
    /* 主音量 */
    int master_volume;
    int master_muted;
    /* 统计 */
    uint64_t total_play_time;
    uint64_t total_record_time;
    int underruns;
    int overruns;
} audio2_t;

/* 初始化音频深化 */
void audio2_init(void);

/* 设备管理 */
int audio2_get_output_count(void);
int audio2_get_output(int id, a2_output_t *output);
int audio2_list_outputs(a2_output_t *outputs, int max);
int audio2_set_active_output(int id);
int audio2_get_active_output(void);
int audio2_set_output_volume(int id, int volume);
int audio2_get_output_volume(int id);
int audio2_mute_output(int id, int mute);

int audio2_get_input_count(void);
int audio2_get_input(int id, a2_input_t *input);
int audio2_list_inputs(a2_input_t *inputs, int max);
int audio2_set_active_input(int id);
int audio2_get_active_input(void);
int audio2_set_input_gain(int id, int gain);

/* 音频源 */
int audio2_create_source(const char *name, int pid, a2_sample_rate_t rate, a2_format_t format, a2_channel_t channels);
int audio2_destroy_source(int source_id);
int audio2_play(int source_id);
int audio2_pause(int source_id);
int audio2_stop(int source_id);
int audio2_set_source_volume(int source_id, int volume);
int audio2_get_source_position(int source_id, uint64_t *position);
int audio2_set_source_position(int source_id, uint64_t position);
int audio2_write_data(int source_id, const void *data, uint32_t size);

/* 音效 */
int audio2_create_effect(const char *name, a2_effect_type_t type);
int audio2_destroy_effect(int effect_id);
int audio2_enable_effect(int effect_id, int enable);
int audio2_set_effect_strength(int effect_id, int strength);
int audio2_set_effect_param(int effect_id, int param_index, int value);
int audio2_list_effects(a2_effect_t *effects, int max);

/* 空间音频 */
int audio2_spatial_enable(int enable);
int audio2_spatial_set_mode(int mode);
int audio2_spatial_set_head_orientation(float yaw, float pitch, float roll);
int audio2_spatial_get(a2_spatial_t *spatial);

/* 降噪 */
int audio2_noise_cancel_enable(int enable);
int audio2_noise_cancel_set_mode(int mode);
int audio2_noise_cancel_set_strength(int strength);
int audio2_noise_cancel_get(a2_noise_cancel_t *nc);

/* 语音唤醒 */
int audio2_voice_wake_enable(int enable);
int audio2_voice_wake_set_word(const char *word);
int audio2_voice_wake_set_sensitivity(int sensitivity);
int audio2_voice_wake_get(a2_voice_wake_t *vw);
int audio2_register_voice_command(const char *command, const char *action);
int audio2_unregister_voice_command(int command_id);
int audio2_list_voice_commands(a2_voice_command_t *commands, int max);

/* 低延迟 */
int audio2_low_latency_enable(int enable);
int audio2_low_latency_set_target(int latency_ms);
int audio2_low_latency_get(a2_low_latency_t *ll);

/* MIDI */
int audio2_midi_connect(const char *name);
int audio2_midi_disconnect(int device_id);
int audio2_midi_send_event(int device_id, a2_midi_event_t *event);
int audio2_midi_note_on(int device_id, int channel, int note, int velocity);
int audio2_midi_note_off(int device_id, int channel, int note, int velocity);
int audio2_midi_program_change(int device_id, int channel, int program);
int audio2_midi_control_change(int device_id, int channel, int controller, int value);
int audio2_midi_list_devices(a2_midi_device_t *devices, int max);

/* 主音量 */
int audio2_set_master_volume(int volume);
int audio2_get_master_volume(void);
int audio2_mute_master(int mute);

/* 统计 */
int audio2_get_stats(uint64_t *play_time, uint64_t *record_time, int *underruns);

#endif /* AUDIO2_H */
