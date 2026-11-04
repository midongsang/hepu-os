/* ============================================================================
 * 赫普OS - 相机深化
 * RAW + HDR + 夜景 + 人像 + 多摄融合 + 人脸检测 + 场景检测
 * ========================================================================== */

#include <stdint.h>
#include "camera2.h"
#include "vga.h"
#include "string.h"

static camera2_t cam2;

void camera2_init(void)
{
    memset(&cam2, 0, sizeof(camera2_t));
    cam2.initialized = 1;

    /* 后置主摄 */
    cam2.cameras[0].used = 1;
    cam2.cameras[0].id = 0;
    strncpy(cam2.cameras[0].name, " next 置 main cam ", 63);
    cam2.cameras[0].facing = CAM_FACING_BACK;
    cam2.cameras[0].lens_count = 3;
    /* 主摄 */
    cam2.cameras[0].lenses[0].type = CAM_SENSOR_RGB;
    cam2.cameras[0].lenses[0].focal_length = 24;
    cam2.cameras[0].lenses[0].aperture = 1.8f;
    cam2.cameras[0].lenses[0].fov = 78;
    cam2.cameras[0].lenses[0].megapixels = 50;
    cam2.cameras[0].lenses[0].ois_supported = 1;
    /* 超广角 */
    cam2.cameras[0].lenses[1].type = CAM_SENSOR_ULTRAWIDE;
    cam2.cameras[0].lenses[1].focal_length = 13;
    cam2.cameras[0].lenses[1].aperture = 2.2f;
    cam2.cameras[0].lenses[1].fov = 120;
    cam2.cameras[0].lenses[1].megapixels = 12;
    /* 长焦 */
    cam2.cameras[0].lenses[2].type = CAM_SENSOR_TELE;
    cam2.cameras[0].lenses[2].focal_length = 72;
    cam2.cameras[0].lenses[2].aperture = 2.8f;
    cam2.cameras[0].lenses[2].fov = 34;
    cam2.cameras[0].lenses[2].megapixels = 12;
    cam2.cameras[0].lenses[2].optical_zoom = 3;
    cam2.cameras[0].lenses[2].ois_supported = 1;

    /* 分辨率 */
    cam2.cameras[0].resolutions[0].width = 8192; cam2.cameras[0].resolutions[0].height = 6144;
    cam2.cameras[0].resolutions[1].width = 4032; cam2.cameras[0].resolutions[1].height = 3024;
    cam2.cameras[0].resolutions[2].width = 3840; cam2.cameras[0].resolutions[2].height = 2160;
    cam2.cameras[0].resolutions[3].width = 1920; cam2.cameras[0].resolutions[3].height = 1080;
    cam2.cameras[0].resolutions[4].width = 1280; cam2.cameras[0].resolutions[4].height = 720;
    cam2.cameras[0].resolution_count = 5;

    /* 格式 */
    cam2.cameras[0].formats[0] = CAM_FORMAT_JPEG;
    cam2.cameras[0].formats[1] = CAM_FORMAT_RAW10;
    cam2.cameras[0].formats[2] = CAM_FORMAT_DNG;
    cam2.cameras[0].formats[3] = CAM_FORMAT_HEIC;
    cam2.cameras[0].formats[4] = CAM_FORMAT_YUV_420_888;
    cam2.cameras[0].format_count = 5;

    /* FPS */
    cam2.cameras[0].fps_ranges[0].min = 30; cam2.cameras[0].fps_ranges[0].max = 30;
    cam2.cameras[0].fps_ranges[1].min = 60; cam2.cameras[0].fps_ranges[1].max = 60;
    cam2.cameras[0].fps_ranges[2].min = 120; cam2.cameras[0].fps_ranges[2].max = 120;
    cam2.cameras[0].fps_ranges[3].min = 240; cam2.cameras[0].fps_ranges[3].max = 240;
    cam2.cameras[0].fps_count = 4;

    /* 特性 */
    cam2.cameras[0].support_raw = 1;
    cam2.cameras[0].support_hdr = 1;
    cam2.cameras[0].support_night = 1;
    cam2.cameras[0].support_portrait = 1;
    cam2.cameras[0].support_burst = 1;
    cam2.cameras[0].support_slow_motion = 1;
    cam2.cameras[0].support_time_lapse = 1;
    cam2.cameras[0].support_face_detect = 1;
    cam2.cameras[0].support_flash = 1;
    cam2.cameras[0].support_4k_video = 1;
    cam2.cameras[0].support_8k_video = 1;
    cam2.cameras[0].support_stabilization = 1;

    /* 前置摄像头 */
    cam2.cameras[1].used = 1;
    cam2.cameras[1].id = 1;
    strncpy(cam2.cameras[1].name, " prev 置 camera ", 63);
    cam2.cameras[1].facing = CAM_FACING_FRONT;
    cam2.cameras[1].lens_count = 1;
    cam2.cameras[1].lenses[0].type = CAM_SENSOR_RGB;
    cam2.cameras[1].lenses[0].focal_length = 26;
    cam2.cameras[1].lenses[0].aperture = 2.0f;
    cam2.cameras[1].lenses[0].fov = 80;
    cam2.cameras[1].lenses[0].megapixels = 32;
    cam2.cameras[1].resolutions[0].width = 6528; cam2.cameras[1].resolutions[0].height = 4896;
    cam2.cameras[1].resolutions[1].width = 3264; cam2.cameras[1].resolutions[1].height = 2448;
    cam2.cameras[1].resolutions[2].width = 1920; cam2.cameras[1].resolutions[2].height = 1080;
    cam2.cameras[1].resolution_count = 3;
    cam2.cameras[1].formats[0] = CAM_FORMAT_JPEG;
    cam2.cameras[1].formats[1] = CAM_FORMAT_RAW10;
    cam2.cameras[1].format_count = 2;
    cam2.cameras[1].support_hdr = 1;
    cam2.cameras[1].support_portrait = 1;
    cam2.cameras[1].support_face_detect = 1;

    cam2.camera_count = 2;

    /* 默认HDR参数 */
    cam2.hdr.enabled = 1;
    cam2.hdr.mode = 0;
    cam2.hdr.ev_step = 2;
    cam2.hdr.frame_count = 3;
    cam2.hdr.strength = 50;

    /* 默认夜景参数 */
    cam2.night.enabled = 1;
    cam2.night.mode = 0;
    cam2.night.max_shutter = 3000;
    cam2.night.max_iso = 6400;
    cam2.night.frame_count = 8;
    cam2.night.noise_reduction = 70;
    cam2.night.ai_enhance = 1;

    /* 默认人像参数 */
    cam2.portrait.enabled = 1;
    cam2.portrait.blur_strength = 50;
    cam2.portrait.aperture = 4;
    cam2.portrait.beauty_level = 30;
    cam2.portrait.bokeh_style = 0;

    /* 默认多摄融合 */
    cam2.multi_lens.enabled = 1;
    cam2.multi_lens.mode = 0;
    cam2.multi_lens.ai_scene_detect = 1;

    vga_puts("[CAM2] camera deep init done \n");
    vga_printf(" camera: %d, main cam: %dMP+%dMP+%dMP, supports RAW/HDR/ night / portrait \n",
               cam2.camera_count,
               cam2.cameras[0].lenses[0].megapixels,
               cam2.cameras[0].lenses[1].megapixels,
               cam2.cameras[0].lenses[2].megapixels);
}

int camera2_get_count(void) { return cam2.camera_count; }

int camera2_get_info(int camera_id, cam_device_t *info)
{
    if (camera_id < 0 || camera_id >= CAM2_MAX_CAMERAS || !cam2.cameras[camera_id].used || !info) return -1;
    memcpy(info, &cam2.cameras[camera_id], sizeof(cam_device_t));
    return 0;
}

int camera2_open(int camera_id)
{
    if (camera_id < 0 || camera_id >= CAM2_MAX_CAMERAS || !cam2.cameras[camera_id].used) return -1;
    cam2.cameras[camera_id].opened = 1;
    return 0;
}

int camera2_close(int camera_id)
{
    if (camera_id < 0 || camera_id >= CAM2_MAX_CAMERAS || !cam2.cameras[camera_id].used) return -1;
    cam2.cameras[camera_id].opened = 0;
    cam2.cameras[camera_id].streaming = 0;
    return 0;
}

int camera2_list(cam_device_t *cameras, int max)
{
    if (!cameras || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < CAM2_MAX_CAMERAS && count < max; i++) {
        if (cam2.cameras[i].used) memcpy(&cameras[count++], &cam2.cameras[i], sizeof(cam_device_t));
    }
    return count;
}

int camera2_capture(cam_capture_request_t *request, cam_capture_result_t *result)
{
    if (!request || !result) return -1;
    if (request->camera_id < 0 || request->camera_id >= CAM2_MAX_CAMERAS ||
        !cam2.cameras[request->camera_id].used) return -1;

    memset(result, 0, sizeof(cam_capture_result_t));
    result->camera_id = request->camera_id;
    result->width = request->width;
    result->height = request->height;
    result->format = request->format;
    result->iso = request->iso ? request->iso : 100;
    result->shutter_speed = request->shutter_speed ? request->shutter_speed : 10000;
    result->aperture = cam2.cameras[request->camera_id].lenses[cam2.cameras[request->camera_id].active_lens].aperture;
    result->exposure_compensation = request->exposure_compensation;
    result->hdr_applied = request->hdr && cam2.hdr.enabled;
    result->night_applied = request->night_mode && cam2.night.enabled;
    result->portrait_applied = request->portrait_mode && cam2.portrait.enabled;
    result->lens_id = cam2.cameras[request->camera_id].active_lens;

    cam2.photos_taken++;
    cam2.last_capture_ms = 50;  /* 原型阶段 */

    return 0;
}

int camera2_burst_capture(cam_capture_request_t *request, int count, cam_capture_result_t *results)
{
    if (!request || !results || count <= 0) return -1;
    for (int i = 0; i < count; i++) {
        camera2_capture(request, &results[i]);
    }
    return count;
}

int camera2_start_stream(int camera_id, int width, int height, cam_format_t format, int fps)
{
    if (camera_id < 0 || camera_id >= CAM2_MAX_CAMERAS || !cam2.cameras[camera_id].used) return -1;
    cam2.cameras[camera_id].streaming = 1;
    cam2.cameras[camera_id].current_resolution = width * 10000 + height;
    cam2.cameras[camera_id].current_format = format;
    cam2.cameras[camera_id].current_fps = fps;
    return 0;
}

int camera2_stop_stream(int camera_id)
{
    if (camera_id < 0 || camera_id >= CAM2_MAX_CAMERAS || !cam2.cameras[camera_id].used) return -1;
    cam2.cameras[camera_id].streaming = 0;
    return 0;
}

int camera2_get_frame(int camera_id, void *buffer, uint32_t *size)
{
    (void)camera_id; (void)buffer;
    if (size) *size = 0;
    return 0;
}

int camera2_start_video(cam_video_params_t *params)
{
    if (!params) return -1;
    cam2.videos_recorded++;
    vga_printf("[CAM2] start video: %dx%d@%dfps\n", params->width, params->height, params->fps);
    return 0;
}

int camera2_stop_video(void)
{
    vga_puts("[CAM2] stop video \n");
    return 0;
}

int camera2_is_recording(void) { return 0; }
int camera2_get_video_duration(void) { return 0; }

int camera2_set_hdr(cam_hdr_params_t *params)
{
    if (!params) return -1;
    memcpy(&cam2.hdr, params, sizeof(cam_hdr_params_t));
    return 0;
}

int camera2_get_hdr(cam_hdr_params_t *params)
{
    if (!params) return -1;
    memcpy(params, &cam2.hdr, sizeof(cam_hdr_params_t));
    return 0;
}

int camera2_set_night(cam_night_params_t *params)
{
    if (!params) return -1;
    memcpy(&cam2.night, params, sizeof(cam_night_params_t));
    return 0;
}

int camera2_get_night(cam_night_params_t *params)
{
    if (!params) return -1;
    memcpy(params, &cam2.night, sizeof(cam_night_params_t));
    return 0;
}

int camera2_set_portrait(cam_portrait_params_t *params)
{
    if (!params) return -1;
    memcpy(&cam2.portrait, params, sizeof(cam_portrait_params_t));
    return 0;
}

int camera2_get_portrait(cam_portrait_params_t *params)
{
    if (!params) return -1;
    memcpy(params, &cam2.portrait, sizeof(cam_portrait_params_t));
    return 0;
}

int camera2_set_multi_lens(cam_multi_lens_t *params)
{
    if (!params) return -1;
    memcpy(&cam2.multi_lens, params, sizeof(cam_multi_lens_t));
    return 0;
}

int camera2_get_multi_lens(cam_multi_lens_t *params)
{
    if (!params) return -1;
    memcpy(params, &cam2.multi_lens, sizeof(cam_multi_lens_t));
    return 0;
}

int camera2_switch_lens(int camera_id, int lens_id)
{
    if (camera_id < 0 || camera_id >= CAM2_MAX_CAMERAS || !cam2.cameras[camera_id].used) return -1;
    if (lens_id < 0 || lens_id >= cam2.cameras[camera_id].lens_count) return -1;
    cam2.cameras[camera_id].active_lens = lens_id;
    vga_printf("[CAM2] switch lens: %d(%.0fmm, f%.1f)\n",
               lens_id,
               cam2.cameras[camera_id].lenses[lens_id].focal_length,
               cam2.cameras[camera_id].lenses[lens_id].aperture);
    return 0;
}

int camera2_get_active_lens(int camera_id)
{
    if (camera_id < 0 || camera_id >= CAM2_MAX_CAMERAS || !cam2.cameras[camera_id].used) return -1;
    return cam2.cameras[camera_id].active_lens;
}

int camera2_start_face_detect(int camera_id)
{
    (void)camera_id;
    return 0;
}

int camera2_stop_face_detect(int camera_id)
{
    (void)camera_id;
    return 0;
}

int camera2_get_faces(cam_face_t *faces, int max)
{
    if (!faces || max <= 0) return 0;
    int count = cam2.face_count < max ? cam2.face_count : max;
    for (int i = 0; i < count; i++) memcpy(&faces[i], &cam2.detected_faces[i], sizeof(cam_face_t));
    return count;
}

int camera2_detect_scene(int camera_id)
{
    (void)camera_id;
    /* 原型阶段: 模拟场景检测 */
    cam2.current_scene = 0;
    strncpy(cam2.scene_name, " auto ", 31);
    cam2.scene_confidence = 80;
    return 0;
}

int camera2_get_scene(int *scene, char *name, int *confidence)
{
    if (scene) *scene = cam2.current_scene;
    if (name) strncpy(name, cam2.scene_name, 31);
    if (confidence) *confidence = cam2.scene_confidence;
    return 0;
}

int camera2_set_exposure(int camera_id, int ev)
{
    (void)camera_id; (void)ev;
    return 0;
}

int camera2_set_iso(int camera_id, int iso)
{
    (void)camera_id; (void)iso;
    return 0;
}

int camera2_set_shutter(int camera_id, int us)
{
    (void)camera_id; (void)us;
    return 0;
}

int camera2_set_focus(int camera_id, int mode, int distance)
{
    (void)camera_id; (void)mode; (void)distance;
    return 0;
}

int camera2_set_white_balance(int camera_id, int mode, int temperature)
{
    (void)camera_id; (void)mode; (void)temperature;
    return 0;
}

int camera2_set_zoom(int camera_id, int percent)
{
    (void)camera_id; (void)percent;
    return 0;
}

int camera2_set_flash(int camera_id, int mode)
{
    (void)camera_id; (void)mode;
    return 0;
}

int camera2_set_stabilization(int camera_id, int enable)
{
    (void)camera_id; (void)enable;
    return 0;
}

int camera2_get_stats(uint64_t *photos, uint64_t *videos, int *last_capture_ms)
{
    if (photos) *photos = cam2.photos_taken;
    if (videos) *videos = cam2.videos_recorded;
    if (last_capture_ms) *last_capture_ms = cam2.last_capture_ms;
    return 0;
}
