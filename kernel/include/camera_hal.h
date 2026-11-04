#ifndef CAMERA_HAL_H
#define CAMERA_HAL_H

#include <stdint.h>

#define CAMERA_HAL_MAX_DEVICES 4
#define CAMERA_HAL_MAX_STREAMS 4
#define CAMERA_HAL_MAX_BUFFERS 8

/* 相机状态 */
typedef enum {
    CAMERA_STATE_OFF = 0,
    CAMERA_STATE_OPEN,
    CAMERA_STATE_CONFIGURED,
    CAMERA_STATE_PREVIEW,
    CAMERA_STATE_CAPTURING,
    CAMERA_STATE_RECORDING,
    CAMERA_STATE_ERROR,
} hal_camera_state_t;

/* 相机朝向 */
typedef enum {
    CAMERA_FACING_BACK = 0,
    CAMERA_FACING_FRONT,
    CAMERA_FACING_EXTERNAL,
} camera_facing_t;

/* 闪光灯模式 */
typedef enum {
    FLASH_OFF = 0,
    FLASH_ON,
    FLASH_AUTO,
    FLASH_TORCH,
    FLASH_REDEYE,
} flash_mode_t;

/* 对焦模式 */
typedef enum {
    AF_OFF = 0,
    AF_AUTO,
    AF_MACRO,
    AF_CONTINUOUS_VIDEO,
    AF_CONTINUOUS_PICTURE,
    AF_EDOF,
} af_mode_t;

/* 曝光模式 */
typedef enum {
    AE_OFF = 0,
    AE_AUTO,
    AE_MANUAL,
    AE_SHUTTER_PRIORITY,
    AE_APERTURE_PRIORITY,
} ae_mode_t;

/* 白平衡模式 */
typedef enum {
    AWB_OFF = 0,
    AWB_AUTO,
    AWB_INCANDESCENT,
    AWB_FLUORESCENT,
    AWB_WARM_FLUORESCENT,
    AWB_DAYLIGHT,
    AWB_CLOUDY_DAYLIGHT,
    AWB_TWILIGHT,
    AWB_SHADE,
} awb_mode_t;

/* 场景模式 */
typedef enum {
    SCENE_MODE_AUTO = 0,
    SCENE_MODE_ACTION,
    SCENE_MODE_PORTRAIT,
    SCENE_MODE_LANDSCAPE,
    SCENE_MODE_NIGHT,
    SCENE_MODE_NIGHT_PORTRAIT,
    SCENE_MODE_THEATRE,
    SCENE_MODE_BEACH,
    SCENE_MODE_SNOW,
    SCENE_MODE_SUNSET,
    SCENE_MODE_STEADYPHOTO,
    SCENE_MODE_FIREWORKS,
    SCENE_MODE_SPORTS,
    SCENE_MODE_PARTY,
    SCENE_MODE_CANDLELIGHT,
    SCENE_MODE_BARCODE,
} scene_mode_t;

/* 流配置 */
typedef struct {
    int width;
    int height;
    int format;      /* 像素格式 */
    int fps;
    int type;        /* 0=preview, 1=still, 2=video */
} camera_stream_config_t;

/* 相机能力 */
typedef struct {
    const char *name;
    camera_facing_t facing;
    int orientation;
    int max_width;
    int max_height;
    int max_zoom;
    int supports_af;
    int supports_flash;
    int supports_video;
    int supported_formats[8];
    int format_count;
    int supported_fps_ranges[8][2];
    int fps_range_count;
} camera_info_t;

/* 3A 状态 */
typedef struct {
    af_mode_t af_mode;
    ae_mode_t ae_mode;
    awb_mode_t awb_mode;
    int af_locked;
    int ae_locked;
    int awb_locked;
    int af_state;    /* 0=inactive, 1=searching, 2=focused, 3=failed */
    int ae_state;
    int awb_state;
    int exposure_compensation;  /* -10 to +10 */
    int iso;
    int shutter_speed;  /* us */
    int focus_distance; /* cm */
} camera_3a_state_t;

/* 相机设备 */
typedef struct {
    int used;
    int id;
    hal_camera_state_t state;
    camera_info_t info;
    camera_3a_state_t a3;
    flash_mode_t flash_mode;
    scene_mode_t scene_mode;
    int zoom;
    int brightness;
    int contrast;
    int saturation;
    int sharpness;
    /* 流 */
    camera_stream_config_t streams[CAMERA_HAL_MAX_STREAMS];
    int stream_count;
    /* 缓冲区 */
    void *buffers[CAMERA_HAL_MAX_BUFFERS];
    int buffer_count;
    /* 回调 */
    void (*on_frame)(int camera_id, void *frame, int size);
    void (*on_picture)(int camera_id, void *data, int size);
    void (*on_error)(int camera_id, int error);
    /* 统计 */
    uint64_t frames_captured;
    uint64_t pictures_taken;
    uint64_t recording_time;
} hal_camera_device_t;

/* 初始化相机HAL */
void camhal_init(void);

/* 打开相机 */
int camhal_open(int camera_id);

/* 关闭相机 */
int camhal_close(int camera_id);

/* 配置流 */
int camhal_configure_streams(int camera_id, camera_stream_config_t *streams, int count);

/* 开始预览 */
int camhal_start_preview(int camera_id);

/* 停止预览 */
int camhal_stop_preview(int camera_id);

/* 拍照 */
int camhal_take_picture(int camera_id);

/* 开始录像 */
int camhal_start_recording(int camera_id, const char *path);

/* 停止录像 */
int camhal_stop_recording(int camera_id);

/* 设置3A模式 */
int camhal_set_af_mode(int camera_id, af_mode_t mode);
int camhal_set_ae_mode(int camera_id, ae_mode_t mode);
int camhal_set_awb_mode(int camera_id, awb_mode_t mode);

/* 触发对焦 */
int camhal_trigger_af(int camera_id);

/* 取消对焦 */
int camhal_cancel_af(int camera_id);

/* 设置闪光灯 */
int camhal_set_flash(int camera_id, flash_mode_t mode);

/* 设置场景模式 */
int camhal_set_scene_mode(int camera_id, scene_mode_t mode);

/* 设置变焦 */
int camhal_set_zoom(int camera_id, int zoom);

/* 设置曝光补偿 */
int camhal_set_exposure_compensation(int camera_id, int value);

/* 获取3A状态 */
int camhal_get_3a_state(int camera_id, camera_3a_state_t *state);

/* 获取相机信息 */
int camhal_get_info(int camera_id, camera_info_t *info);

/* 获取相机状态 */
hal_camera_state_t camhal_get_state(int camera_id);

/* 枚举相机 */
int camhal_enumerate(camera_info_t *infos, int max);

/* 设置帧回调 */
int camhal_set_frame_callback(int camera_id, void (*cb)(int, void *, int));

/* 请求缓冲区 */
int camhal_request_buffers(int camera_id, int count);

/* 释放缓冲区 */
int camhal_release_buffers(int camera_id);

/* 获取活跃相机数 */
int camhal_device_count(void);

#endif /* CAMERA_HAL_H */
