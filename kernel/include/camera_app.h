#ifndef CAMERA_APP_H
#define CAMERA_APP_H

#include <stdint.h>

/* ============================================================================
 * 赫普OS 相机应用
 * 拍照 / 录像 / 设置 / 滤镜
 * ========================================================================== */

#define CAM_MAX_PHOTOS 256
#define CAM_MAX_VIDEOS 64
#define CAM_MAX_FILTERS 16
#define CAM_MAX_MODES 8
#define CAM_NAME_LEN 64
#define CAM_PATH_LEN 256

/* 相机模式 */
typedef enum {
    CAM_MODE_PHOTO = 0,
    CAM_MODE_VIDEO,
    CAM_MODE_PORTRAIT,
    CAM_MODE_NIGHT,
    CAM_MODE_PRO,
    CAM_MODE_PANORAMA,
    CAM_MODE_SLOW_MOTION,
    CAM_MODE_TIME_LAPSE,
} cam_mode_t;

/* 相机状态 */
typedef enum {
    CAMAPP_STATE_IDLE = 0,
    CAMAPP_STATE_PREVIEW,
    CAMAPP_STATE_CAPTURING,
    CAMAPP_STATE_RECORDING,
    CAMAPP_STATE_SAVING,
    CAMAPP_STATE_ERROR,
} camapp_state_t;

/* 闪光灯模式 */
typedef enum {
    CAM_FLASH_AUTO = 0,
    CAM_FLASH_ON,
    CAM_FLASH_OFF,
    CAM_FLASH_TORCH,
} cam_flash_t;

/* HDR模式 */
typedef enum {
    CAM_HDR_OFF = 0,
    CAM_HDR_ON,
    CAM_HDR_AUTO,
} cam_hdr_t;

/* 照片 */
typedef struct {
    int used;
    int id;
    char filename[CAM_NAME_LEN];
    char path[CAM_PATH_LEN];
    uint32_t width;
    uint32_t height;
    uint64_t size;
    uint64_t timestamp;
    cam_mode_t mode;
    int hdr;
    int flash_used;
    char location[64];
    int favorite;
    uint8_t thumbnail[64];  /* 缩略图 */
} cam_photo_t;

/* 视频 */
typedef struct {
    int used;
    int id;
    char filename[CAM_NAME_LEN];
    char path[CAM_PATH_LEN];
    uint32_t width;
    uint32_t height;
    uint32_t fps;
    uint64_t size;
    uint64_t duration;  /* 毫秒 */
    uint64_t timestamp;
    int hdr;
    int stabilization;
    int favorite;
} cam_video_t;

/* 滤镜 */
typedef struct {
    int used;
    int id;
    char name[CAM_NAME_LEN];
    int intensity;  /* 0-100 */
    int enabled;
    /* 参数 */
    int brightness;
    int contrast;
    int saturation;
    int warmth;
    int vignette;
} cam_filter_t;

/* 相机设置 */
typedef struct {
    cam_mode_t mode;
    cam_flash_t flash;
    cam_hdr_t hdr;
    uint32_t photo_resolution;  /* 0=4K,1=1080P,2=720P */
    uint32_t video_resolution;
    uint32_t video_fps;
    int grid_lines;
    int timer;  /* 0=关,3,5,10秒 */
    int burst;  /* 连拍 */
    int face_detection;
    int location_tag;
    int shutter_sound;
    int zsl;  /* 零延迟拍照 */
    int active_filter;
} cam_settings_t;

/* 相机应用状态 */
typedef struct {
    int initialized;
    int enabled;
    camapp_state_t state;
    cam_settings_t settings;
    /* 媒体库 */
    cam_photo_t photos[CAM_MAX_PHOTOS];
    int photo_count;
    cam_video_t videos[CAM_MAX_VIDEOS];
    int video_count;
    /* 滤镜 */
    cam_filter_t filters[CAM_MAX_FILTERS];
    int filter_count;
    /* 当前拍摄 */
    int current_camera;  /* 0=后置,1=前置 */
    int zoom_level;  /* 0-100 */
    int focus_x;
    int focus_y;
    int exposure;  /* -100 to 100 */
    int iso;
    int white_balance;  /* 0=自动,1=日光,2=阴天,3=白炽灯,4=荧光 */
    /* 录像 */
    int recording;
    uint64_t record_start;
    /* 统计 */
    uint64_t total_photos;
    uint64_t total_videos;
    uint64_t total_recording_time;
} camera_app_t;

/* 初始化 */
void camera_app_init(void);

/* 拍摄 */
int camera_capture_photo(void);
int camera_start_recording(void);
int camera_stop_recording(void);
int camera_switch_camera(void);

/* 设置 */
int camera_set_mode(cam_mode_t mode);
int camera_set_flash(cam_flash_t flash);
int camera_set_hdr(cam_hdr_t hdr);
int camera_set_zoom(int level);
int camera_set_exposure(int value);
int camera_set_iso(int iso);
int camera_set_white_balance(int wb);
int camera_set_timer(int seconds);
int camera_set_resolution(int photo, int video);
int camera_set_fps(int fps);
int camera_toggle_grid(void);
int camera_toggle_face_detection(void);
int camera_toggle_location_tag(void);

/* 滤镜 */
int camera_filter_add(const char *name, int brightness, int contrast, int saturation);
int camera_filter_apply(int filter_id);
int camera_filter_adjust(int filter_id, int intensity);
int camera_filter_list(cam_filter_t *filters, int max);

/* 媒体库 */
int camera_get_photo(int id, cam_photo_t *photo);
int camera_list_photos(cam_photo_t *photos, int max);
int camera_get_video(int id, cam_video_t *video);
int camera_list_videos(cam_video_t *videos, int max);
int camera_delete_photo(int id);
int camera_delete_video(int id);
int camera_toggle_favorite_photo(int id);
int camera_toggle_favorite_video(int id);
int camera_get_latest_photo(cam_photo_t *photo);

/* 状态 */
int camera_get_state(camapp_state_t *state);
int camera_get_settings(cam_settings_t *settings);
int camera_get_recording_time(uint64_t *ms);

/* 统计 */
int camera_get_stats(camera_app_t *stats);

#endif /* CAMERA_APP_H */
