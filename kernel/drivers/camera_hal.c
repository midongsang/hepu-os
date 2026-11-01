/* ============================================================================
 * 赫普OS - 相机 HAL
 * 预览/拍照/录像 + 3A(自动对焦/自动曝光/自动白平衡) + 场景模式
 * ========================================================================== */

#include <stdint.h>
#include "camera_hal.h"
#include "vga.h"
#include "string.h"
#include "kheap.h"

static hal_camera_device_t cameras[CAMERA_HAL_MAX_DEVICES];
static int camera_count = 0;
static int camhal_initialized = 0;

void camhal_init(void)
{
    for (int i = 0; i < CAMERA_HAL_MAX_DEVICES; i++) {
        cameras[i].used = 0;
    }
    camera_count = 0;
    camhal_initialized = 1;

    /* 注册后置摄像头 */
    hal_camera_device_t *cam = &cameras[0];
    memset(cam, 0, sizeof(hal_camera_device_t));
    cam->used = 1;
    cam->id = 0;
    cam->state = CAMERA_STATE_OFF;
    cam->info.name = "Rear Camera";
    cam->info.facing = CAMERA_FACING_BACK;
    cam->info.orientation = 90;
    cam->info.max_width = 4032;
    cam->info.max_height = 3024;
    cam->info.max_zoom = 10;
    cam->info.supports_af = 1;
    cam->info.supports_flash = 1;
    cam->info.supports_video = 1;
    cam->info.format_count = 3;
    cam->info.supported_formats[0] = 0;  /* YUV420 */
    cam->info.supported_formats[1] = 1;  /* JPEG */
    cam->info.supported_formats[2] = 2;  /* RAW */
    cam->a3.af_mode = AF_CONTINUOUS_PICTURE;
    cam->a3.ae_mode = AE_AUTO;
    cam->a3.awb_mode = AWB_AUTO;
    cam->a3.af_state = 2;  /* focused */
    cam->a3.iso = 100;
    cam->a3.shutter_speed = 10000;  /* 10ms */
    cam->flash_mode = FLASH_AUTO;
    cam->scene_mode = SCENE_MODE_AUTO;
    cam->zoom = 0;
    camera_count++;

    /* 注册前置摄像头 */
    cam = &cameras[1];
    memset(cam, 0, sizeof(hal_camera_device_t));
    cam->used = 1;
    cam->id = 1;
    cam->state = CAMERA_STATE_OFF;
    cam->info.name = "Front Camera";
    cam->info.facing = CAMERA_FACING_FRONT;
    cam->info.orientation = 270;
    cam->info.max_width = 1920;
    cam->info.max_height = 1080;
    cam->info.max_zoom = 4;
    cam->info.supports_af = 0;
    cam->info.supports_flash = 0;
    cam->info.supports_video = 1;
    cam->a3.af_mode = AF_OFF;
    cam->a3.ae_mode = AE_AUTO;
    cam->a3.awb_mode = AWB_AUTO;
    cam->flash_mode = FLASH_OFF;
    cam->scene_mode = SCENE_MODE_AUTO;
    camera_count++;

    vga_puts("[CAMERA HAL] camera HAL init done \n");
    vga_printf(" found %d camera \n", camera_count);
}

int camhal_open(int camera_id)
{
    if (camera_id < 0 || camera_id >= CAMERA_HAL_MAX_DEVICES || !cameras[camera_id].used) return -1;
    if (cameras[camera_id].state != CAMERA_STATE_OFF) return -1;
    cameras[camera_id].state = CAMERA_STATE_OPEN;
    vga_printf("[CAMERA] open %s\n", cameras[camera_id].info.name);
    return 0;
}

int camhal_close(int camera_id)
{
    if (camera_id < 0 || camera_id >= CAMERA_HAL_MAX_DEVICES || !cameras[camera_id].used) return -1;
    if (cameras[camera_id].state == CAMERA_STATE_PREVIEW ||
        cameras[camera_id].state == CAMERA_STATE_RECORDING) {
        camhal_stop_preview(camera_id);
    }
    cameras[camera_id].state = CAMERA_STATE_OFF;
    return 0;
}

int camhal_configure_streams(int camera_id, camera_stream_config_t *streams, int count)
{
    if (camera_id < 0 || camera_id >= CAMERA_HAL_MAX_DEVICES || !cameras[camera_id].used) return -1;
    if (!streams || count <= 0 || count > CAMERA_HAL_MAX_STREAMS) return -1;
    if (cameras[camera_id].state < CAMERA_STATE_OPEN) return -1;

    memcpy(cameras[camera_id].streams, streams, count * sizeof(camera_stream_config_t));
    cameras[camera_id].stream_count = count;
    cameras[camera_id].state = CAMERA_STATE_CONFIGURED;

    for (int i = 0; i < count; i++) {
        vga_printf("[CAMERA] streams %d: %dx%d@%dfps, type=%d\n",
                   i, streams[i].width, streams[i].height, streams[i].fps, streams[i].type);
    }
    return 0;
}

int camhal_start_preview(int camera_id)
{
    if (camera_id < 0 || camera_id >= CAMERA_HAL_MAX_DEVICES || !cameras[camera_id].used) return -1;
    if (cameras[camera_id].state < CAMERA_STATE_CONFIGURED) return -1;
    cameras[camera_id].state = CAMERA_STATE_PREVIEW;
    vga_printf("[CAMERA] %s start preview \n", cameras[camera_id].info.name);
    return 0;
}

int camhal_stop_preview(int camera_id)
{
    if (camera_id < 0 || camera_id >= CAMERA_HAL_MAX_DEVICES || !cameras[camera_id].used) return -1;
    if (cameras[camera_id].state == CAMERA_STATE_RECORDING) {
        camhal_stop_recording(camera_id);
    }
    cameras[camera_id].state = CAMERA_STATE_CONFIGURED;
    return 0;
}

int camhal_take_picture(int camera_id)
{
    if (camera_id < 0 || camera_id >= CAMERA_HAL_MAX_DEVICES || !cameras[camera_id].used) return -1;
    if (cameras[camera_id].state != CAMERA_STATE_PREVIEW) return -1;

    cameras[camera_id].state = CAMERA_STATE_CAPTURING;
    vga_printf("[CAMERA] %s capturing ...\n", cameras[camera_id].info.name);

    /* 原型阶段: 模拟拍照 */
    cameras[camera_id].pictures_taken++;
    cameras[camera_id].state = CAMERA_STATE_PREVIEW;

    if (cameras[camera_id].on_picture) {
        cameras[camera_id].on_picture(camera_id, NULL, 0);
    }
    vga_printf("[CAMERA] capture done(total %u)\n", (uint32_t)cameras[camera_id].pictures_taken);
    return 0;
}

int camhal_start_recording(int camera_id, const char *path)
{
    if (camera_id < 0 || camera_id >= CAMERA_HAL_MAX_DEVICES || !cameras[camera_id].used) return -1;
    if (cameras[camera_id].state != CAMERA_STATE_PREVIEW) return -1;
    if (!cameras[camera_id].info.supports_video) return -1;

    cameras[camera_id].state = CAMERA_STATE_RECORDING;
    vga_printf("[CAMERA] %s start record: %s\n", cameras[camera_id].info.name, path ? path : "default");
    return 0;
}

int camhal_stop_recording(int camera_id)
{
    if (camera_id < 0 || camera_id >= CAMERA_HAL_MAX_DEVICES || !cameras[camera_id].used) return -1;
    if (cameras[camera_id].state != CAMERA_STATE_RECORDING) return -1;
    cameras[camera_id].state = CAMERA_STATE_PREVIEW;
    vga_printf("[CAMERA] record stop, duration: %ums\n", (uint32_t)cameras[camera_id].recording_time);
    return 0;
}

int camhal_set_af_mode(int camera_id, af_mode_t mode)
{
    if (camera_id < 0 || camera_id >= CAMERA_HAL_MAX_DEVICES || !cameras[camera_id].used) return -1;
    cameras[camera_id].a3.af_mode = mode;
    return 0;
}

int camhal_set_ae_mode(int camera_id, ae_mode_t mode)
{
    if (camera_id < 0 || camera_id >= CAMERA_HAL_MAX_DEVICES || !cameras[camera_id].used) return -1;
    cameras[camera_id].a3.ae_mode = mode;
    return 0;
}

int camhal_set_awb_mode(int camera_id, awb_mode_t mode)
{
    if (camera_id < 0 || camera_id >= CAMERA_HAL_MAX_DEVICES || !cameras[camera_id].used) return -1;
    cameras[camera_id].a3.awb_mode = mode;
    return 0;
}

int camhal_trigger_af(int camera_id)
{
    if (camera_id < 0 || camera_id >= CAMERA_HAL_MAX_DEVICES || !cameras[camera_id].used) return -1;
    if (!cameras[camera_id].info.supports_af) return -1;
    cameras[camera_id].a3.af_state = 1;  /* searching */
    /* 原型阶段: 立即对焦成功 */
    cameras[camera_id].a3.af_state = 2;  /* focused */
    cameras[camera_id].a3.focus_distance = 30;
    return 0;
}

int camhal_cancel_af(int camera_id)
{
    if (camera_id < 0 || camera_id >= CAMERA_HAL_MAX_DEVICES || !cameras[camera_id].used) return -1;
    cameras[camera_id].a3.af_state = 0;  /* inactive */
    return 0;
}

int camhal_set_flash(int camera_id, flash_mode_t mode)
{
    if (camera_id < 0 || camera_id >= CAMERA_HAL_MAX_DEVICES || !cameras[camera_id].used) return -1;
    if (!cameras[camera_id].info.supports_flash && mode != FLASH_OFF) return -1;
    cameras[camera_id].flash_mode = mode;
    return 0;
}

int camhal_set_scene_mode(int camera_id, scene_mode_t mode)
{
    if (camera_id < 0 || camera_id >= CAMERA_HAL_MAX_DEVICES || !cameras[camera_id].used) return -1;
    cameras[camera_id].scene_mode = mode;
    return 0;
}

int camhal_set_zoom(int camera_id, int zoom)
{
    if (camera_id < 0 || camera_id >= CAMERA_HAL_MAX_DEVICES || !cameras[camera_id].used) return -1;
    if (zoom < 0 || zoom > cameras[camera_id].info.max_zoom) return -1;
    cameras[camera_id].zoom = zoom;
    return 0;
}

int camhal_set_exposure_compensation(int camera_id, int value)
{
    if (camera_id < 0 || camera_id >= CAMERA_HAL_MAX_DEVICES || !cameras[camera_id].used) return -1;
    if (value < -10 || value > 10) return -1;
    cameras[camera_id].a3.exposure_compensation = value;
    return 0;
}

int camhal_get_3a_state(int camera_id, camera_3a_state_t *state)
{
    if (camera_id < 0 || camera_id >= CAMERA_HAL_MAX_DEVICES || !cameras[camera_id].used || !state) return -1;
    memcpy(state, &cameras[camera_id].a3, sizeof(camera_3a_state_t));
    return 0;
}

int camhal_get_info(int camera_id, camera_info_t *info)
{
    if (camera_id < 0 || camera_id >= CAMERA_HAL_MAX_DEVICES || !cameras[camera_id].used || !info) return -1;
    memcpy(info, &cameras[camera_id].info, sizeof(camera_info_t));
    return 0;
}

hal_camera_state_t camhal_get_state(int camera_id)
{
    if (camera_id < 0 || camera_id >= CAMERA_HAL_MAX_DEVICES || !cameras[camera_id].used) return CAMERA_STATE_ERROR;
    return cameras[camera_id].state;
}

int camhal_enumerate(camera_info_t *infos, int max)
{
    if (!infos || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < CAMERA_HAL_MAX_DEVICES && count < max; i++) {
        if (cameras[i].used) {
            memcpy(&infos[count++], &cameras[i].info, sizeof(camera_info_t));
        }
    }
    return count;
}

int camhal_set_frame_callback(int camera_id, void (*cb)(int, void *, int))
{
    if (camera_id < 0 || camera_id >= CAMERA_HAL_MAX_DEVICES || !cameras[camera_id].used) return -1;
    cameras[camera_id].on_frame = cb;
    return 0;
}

int camhal_request_buffers(int camera_id, int count)
{
    if (camera_id < 0 || camera_id >= CAMERA_HAL_MAX_DEVICES || !cameras[camera_id].used) return -1;
    if (count <= 0 || count > CAMERA_HAL_MAX_BUFFERS) return -1;
    for (int i = 0; i < count; i++) {
        cameras[camera_id].buffers[i] = kmalloc(1024 * 1024);  /* 1MB each */
    }
    cameras[camera_id].buffer_count = count;
    return count;
}

int camhal_release_buffers(int camera_id)
{
    if (camera_id < 0 || camera_id >= CAMERA_HAL_MAX_DEVICES || !cameras[camera_id].used) return -1;
    for (int i = 0; i < cameras[camera_id].buffer_count; i++) {
        if (cameras[camera_id].buffers[i]) kfree(cameras[camera_id].buffers[i]);
        cameras[camera_id].buffers[i] = NULL;
    }
    cameras[camera_id].buffer_count = 0;
    return 0;
}

int camhal_device_count(void) { return camera_count; }
