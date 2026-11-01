#ifndef CAMERA2_H
#define CAMERA2_H

#include <stdint.h>

#define CAM2_MAX_CAMERAS 4
#define CAM2_MAX_RESOLUTIONS 16
#define CAM2_MAX_FORMATS 8
#define CAM2_MAX_FPS 8
#define CAM2_MAX_STREAMS 4
#define CAM2_MAX_EFFECTS 16
#define CAM2_MAX_SCENES 16
#define CAM2_MAX_FACE_DETECT 10
#define CAM2_MAX_LENS 4

/* 相机朝向 */
typedef enum {
    CAM_FACING_BACK = 0,
    CAM_FACING_FRONT,
    CAM_FACING_EXTERNAL,
} cam_facing_t;

/* 传感器类型 */
typedef enum {
    CAM_SENSOR_RGB = 0,
    CAM_SENSOR_MONO,
    CAM_SENSOR_WIDE,
    CAM_SENSOR_ULTRAWIDE,
    CAM_SENSOR_TELE,
    CAM_SENSOR_DEPTH,
    CAM_SENSOR_THERMAL,
} cam_sensor_type_t;

/* 像素格式 */
typedef enum {
    CAM_FORMAT_RAW10 = 0,
    CAM_FORMAT_RAW12,
    CAM_FORMAT_RAW16,
    CAM_FORMAT_RAW_OPAQUE,
    CAM_FORMAT_YUV_420_888,
    CAM_FORMAT_YUV_422_888,
    CAM_FORMAT_JPEG,
    CAM_FORMAT_HEIC,
    CAM_FORMAT_DNG,
    CAM_FORMAT_PRIVATE,
} cam_format_t;

/* 分辨率 */
typedef struct {
    int width;
    int height;
} cam_resolution_t;

/* FPS范围 */
typedef struct {
    int min;
    int max;
} cam_fps_range_t;

/* 镜头信息 */
typedef struct {
    int id;
    cam_sensor_type_t type;
    float focal_length;  /* mm */
    float aperture;  /* f数 */
    float fov;  /* 视场角(度) */
    int min_focus_distance;  /* cm */
    int optical_zoom;  /* 光学变焦倍数 */
    int sensor_width;  /* mm */
    int sensor_height;  /* mm */
    int pixel_size;  /* um */
    int megapixels;
    int ois_supported;  /* 光学防抖 */
    int eis_supported;  /* 电子防抖 */
} cam_lens_t;

/* 相机设备 */
typedef struct {
    int used;
    int id;
    char name[64];
    cam_facing_t facing;
    cam_lens_t lenses[CAM2_MAX_LENS];
    int lens_count;
    int active_lens;
    cam_resolution_t resolutions[CAM2_MAX_RESOLUTIONS];
    int resolution_count;
    cam_format_t formats[CAM2_MAX_FORMATS];
    int format_count;
    cam_fps_range_t fps_ranges[CAM2_MAX_FPS];
    int fps_count;
    /* 特性支持 */
    int support_raw;
    int support_hdr;
    int support_night;
    int support_portrait;
    int support_burst;
    int support_slow_motion;
    int support_time_lapse;
    int support_panorama;
    int support_face_detect;
    int support_af;
    int support_ae;
    int support_awb;
    int support_flash;
    int support_hdr_video;
    int support_4k_video;
    int support_8k_video;
    int support_stabilization;
    /* 当前状态 */
    int opened;
    int streaming;
    int current_resolution;
    int current_format;
    int current_fps;
} cam_device_t;

/* 捕获请求 */
typedef struct {
    int camera_id;
    int width, height;
    cam_format_t format;
    int fps;
    int burst_count;
    int hdr;
    int night_mode;
    int portrait_mode;
    int raw;
    int flash;
    int exposure_compensation;  /* EV */
    int iso;
    int shutter_speed;  /* us */
    int focus_mode;  /* 0=auto,1=macro,2=infinity,3=manual */
    int focus_distance;  /* cm */
    int white_balance;  /* 0=auto,1=incandescent,2=fluorescent,3=daylight,4=cloudy,5=manual */
    int color_temperature;  /* K */
    int scene_mode;
    int effect;
    int stabilization;
    int zoom;  /* 1-100 (百分比) */
    int jpeg_quality;
    int orientation;
} cam_capture_request_t;

/* 捕获结果 */
typedef struct {
    int camera_id;
    int width, height;
    cam_format_t format;
    uint64_t timestamp;
    void *data;
    uint32_t size;
    int iso;
    int shutter_speed;
    float aperture;
    int exposure_compensation;
    int white_balance;
    int focus_distance;
    int flash_fired;
    int hdr_applied;
    int night_applied;
    int portrait_applied;
    int face_count;
    int lens_id;
} cam_capture_result_t;

/* 人脸检测 */
typedef struct {
    int id;
    int x, y, width, height;
    int score;  /* 0-100 */
    int left_eye_x, left_eye_y;
    int right_eye_x, right_eye_y;
    int mouth_x, mouth_y;
    int smiling;  /* 0-100 */
    int blink;  /* 0-100 */
} cam_face_t;

/* HDR参数 */
typedef struct {
    int enabled;
    int mode;  /* 0=auto,1=on,2=off */
    int ev_step;  /* EV步长 */
    int frame_count;  /* 包围曝光帧数 */
    int strength;  /* 0-100 */
    int tone_mapping;  /* 0=natural,1=vivid */
} cam_hdr_params_t;

/* 夜景参数 */
typedef struct {
    int enabled;
    int mode;  /* 0=auto,1=on,2=off */
    int max_shutter;  /* 最大快门(ms) */
    int max_iso;
    int frame_count;
    int noise_reduction;  /* 0-100 */
    int ai_enhance;
} cam_night_params_t;

/* 人像参数 */
typedef struct {
    int enabled;
    int mode;  /* 0=auto,1=on,2=off */
    int blur_strength;  /* 0-100 (光圈效果) */
    int aperture;  /* 模拟f数 */
    int beauty_level;  /* 0-100 */
    int skin_smooth;  /* 0-100 */
    int face_slim;  /* 0-100 */
    int eye_enlarge;  /* 0-100 */
    int bokeh_style;  /* 0=natural,1=circle,2=heart,3=star */
} cam_portrait_params_t;

/* 多摄融合 */
typedef struct {
    int enabled;
    int mode;  /* 0=auto,1=wide,2=ultrawide,3=tele,4=fusion */
    int active_lenses[CAM2_MAX_LENS];
    int active_lens_count;
    int fusion_strength;  /* 0-100 */
    int ai_scene_detect;
    int low_light_fusion;
    int high_resolution_fusion;
} cam_multi_lens_t;

/* 视频录制 */
typedef struct {
    int camera_id;
    int width, height;
    int fps;
    int bitrate;  /* kbps */
    int codec;  /* 0=H.264,1=H.265,2=VP9,3=AV1 */
    int hdr;
    int stabilization;
    int slow_motion;  /* 0=off,1=120fps,2=240fps,3=480fps */
    int time_lapse;  /* 0=off,1=2x,2=4x,3=8x,4=16x */
    int audio_enabled;
    int audio_source;  /* 0=mic,1=camcorder,2=voice_recognition */
    int duration_limit;  /* 秒, 0=无限制 */
    int file_size_limit;  /* MB, 0=无限制 */
    char output_path[256];
} cam_video_params_t;

/* 相机深化状态 */
typedef struct {
    int initialized;
    cam_device_t cameras[CAM2_MAX_CAMERAS];
    int camera_count;
    /* 当前捕获 */
    int capturing;
    cam_capture_request_t current_request;
    /* 统计 */
    uint64_t photos_taken;
    uint64_t videos_recorded;
    uint64_t total_capture_time;
    int last_capture_ms;
    /* 人脸检测 */
    cam_face_t detected_faces[CAM2_MAX_FACE_DETECT];
    int face_count;
    /* 参数 */
    cam_hdr_params_t hdr;
    cam_night_params_t night;
    cam_portrait_params_t portrait;
    cam_multi_lens_t multi_lens;
    /* 场景检测 */
    int current_scene;
    char scene_name[32];
    int scene_confidence;
} camera2_t;

/* 初始化相机深化 */
void camera2_init(void);

/* 相机管理 */
int camera2_get_count(void);
int camera2_get_info(int camera_id, cam_device_t *info);
int camera2_open(int camera_id);
int camera2_close(int camera_id);
int camera2_list(cam_device_t *cameras, int max);

/* 捕获 */
int camera2_capture(cam_capture_request_t *request, cam_capture_result_t *result);
int camera2_burst_capture(cam_capture_request_t *request, int count, cam_capture_result_t *results);
int camera2_start_stream(int camera_id, int width, int height, cam_format_t format, int fps);
int camera2_stop_stream(int camera_id);
int camera2_get_frame(int camera_id, void *buffer, uint32_t *size);

/* 视频录制 */
int camera2_start_video(cam_video_params_t *params);
int camera2_stop_video(void);
int camera2_is_recording(void);
int camera2_get_video_duration(void);

/* HDR */
int camera2_set_hdr(cam_hdr_params_t *params);
int camera2_get_hdr(cam_hdr_params_t *params);

/* 夜景 */
int camera2_set_night(cam_night_params_t *params);
int camera2_get_night(cam_night_params_t *params);

/* 人像 */
int camera2_set_portrait(cam_portrait_params_t *params);
int camera2_get_portrait(cam_portrait_params_t *params);

/* 多摄融合 */
int camera2_set_multi_lens(cam_multi_lens_t *params);
int camera2_get_multi_lens(cam_multi_lens_t *params);
int camera2_switch_lens(int camera_id, int lens_id);
int camera2_get_active_lens(int camera_id);

/* 人脸检测 */
int camera2_start_face_detect(int camera_id);
int camera2_stop_face_detect(int camera_id);
int camera2_get_faces(cam_face_t *faces, int max);

/* 场景检测 */
int camera2_detect_scene(int camera_id);
int camera2_get_scene(int *scene, char *name, int *confidence);

/* 控制 */
int camera2_set_exposure(int camera_id, int ev);
int camera2_set_iso(int camera_id, int iso);
int camera2_set_shutter(int camera_id, int us);
int camera2_set_focus(int camera_id, int mode, int distance);
int camera2_set_white_balance(int camera_id, int mode, int temperature);
int camera2_set_zoom(int camera_id, int percent);
int camera2_set_flash(int camera_id, int mode);  /* 0=off,1=on,2=auto,3=torch */
int camera2_set_stabilization(int camera_id, int enable);

/* 统计 */
int camera2_get_stats(uint64_t *photos, uint64_t *videos, int *last_capture_ms);

#endif /* CAMERA2_H */
