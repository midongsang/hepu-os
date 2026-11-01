/* ============================================================================
 * 赫普OS - 相机应用
 * ========================================================================== */

#include <stdint.h>
#include "camera_app.h"
#include "vga.h"
#include "string.h"
#include "kstdio.h"   /* V3.79: snprintf 声明 */

static void tools_strcat(char *dst, const char *src, int max) {
    int dlen = 0; while (dst[dlen] && dlen < max-1) dlen++;
    int i = 0;
    while (src[i] && dlen < max-1) { dst[dlen++] = src[i++]; }
    dst[dlen] = 0;
}


static camera_app_t cam;

void camera_app_init(void)
{
    /* V0.10.5-BIOS: 简化版本，避免vga操作和磁盘写入在BIOS环境下卡住 */
    extern void serial_puts(uint16_t, const char *);
    serial_puts(0x3F8, "[CAMERA] init (minimal BIOS version)\n");

    memset(&cam, 0, sizeof(camera_app_t));
    cam.initialized = 1;
    cam.enabled = 1;
    cam.state = CAMAPP_STATE_IDLE;
    cam.current_camera = 0;

    /* 默认设置 */
    cam.settings.mode = CAM_MODE_PHOTO;
    cam.settings.flash = CAM_FLASH_AUTO;
    cam.settings.hdr = CAM_HDR_AUTO;
    cam.settings.photo_resolution = 0;
    cam.settings.video_resolution = 1;
    cam.settings.video_fps = 30;
    cam.settings.grid_lines = 0;
    cam.settings.timer = 0;
    cam.settings.burst = 0;
    cam.settings.face_detection = 1;
    cam.settings.location_tag = 0;
    cam.settings.shutter_sound = 1;
    cam.settings.zsl = 1;
    cam.settings.active_filter = -1;

    /* 注册滤镜（不调用磁盘写入） */
    camera_filter_add("Original", 0, 0, 0);
    camera_filter_add("Vivid", 0, 20, 30);
    camera_filter_add("Soft", 10, -10, -10);
    camera_filter_add("B&W", 0, 10, -100);
    camera_filter_add("Vintage", -5, 5, 10);
    camera_filter_add("Cool", 0, 0, -10);
    camera_filter_add("Warm", 0, 0, 10);
    camera_filter_add("High Contrast", 0, 30, 0);

    /* V0.10.5-BIOS: 跳过测试照片（避免ide_ext2_writefile卡住） */
    serial_puts(0x3F8, "[CAMERA] init done\n");
}

int camera_capture_photo(void)
{
    for (int i = 0; i < CAM_MAX_PHOTOS; i++) {
        if (!cam.photos[i].used) {
            memset(&cam.photos[i], 0, sizeof(cam_photo_t));
            cam.photos[i].used = 1;
            cam.photos[i].id = i;
            /* 文件名 */
            char name[64];
            int nid = cam.photo_count + 1;
            name[0] = 'I'; name[1] = 'M'; name[2] = 'G'; name[3] = '_';
            name[4] = '0' + (nid / 1000) % 10;
            name[5] = '0' + (nid / 100) % 10;
            name[6] = '0' + (nid / 10) % 10;
            name[7] = '0' + nid % 10;
            name[8] = '.'; name[9] = 'j'; name[10] = 'p'; name[11] = 'g'; name[12] = 0;
            strncpy(cam.photos[i].filename, name, 63);
            /* V3.79: 照片落盘到 /data/photos/ (手机数据盘), 相册/文件管理器可见 */
            strncpy(cam.photos[i].path, "/data/photos/", 255);
            tools_strcat(cam.photos[i].path, name, 256);
            cam.photos[i].width = 4032;
            cam.photos[i].height = 3024;
            cam.photos[i].size = 5 * 1024 * 1024;  /* 5MB */
            cam.photos[i].timestamp = 1;
            cam.photos[i].mode = cam.settings.mode;
            cam.photos[i].hdr = cam.settings.hdr;
            cam.photo_count++;
            cam.total_photos++;
            cam.state = CAMAPP_STATE_PREVIEW;
            /* V3.79: 真实存储——照片记录写入磁盘 /data/photos/ (记录文件) */
            extern int ide_ext2_writefile(const char *, const char *);
            extern int ide_ext2_mount(void);
            if (ide_ext2_mount() == 0) {
                char meta[160];
                int ml = snprintf(meta, sizeof(meta),
                                  "IMG_%04d|%dx%d|%dbytes|mode=%d|hdr=%d|ts=%d\n",
                                  nid, cam.photos[i].width, cam.photos[i].height,
                                  cam.photos[i].size, cam.photos[i].mode,
                                  cam.photos[i].hdr, cam.photos[i].timestamp);
                if (ml > 0) {
                    char disk_name[64];
                    int dn = snprintf(disk_name, sizeof(disk_name), "data/photos/%s.meta", name);
                    if (dn > 0) ide_ext2_writefile(disk_name, meta);
                }
            }
            return i;
        }
    }
    return -1;
}

int camera_start_recording(void)
{
    if (cam.recording) return -1;
    cam.recording = 1;
    cam.record_start = 1;
    cam.state = CAMAPP_STATE_RECORDING;
    return 0;
}

int camera_stop_recording(void)
{
    if (!cam.recording) return -1;
    for (int i = 0; i < CAM_MAX_VIDEOS; i++) {
        if (!cam.videos[i].used) {
            memset(&cam.videos[i], 0, sizeof(cam_video_t));
            cam.videos[i].used = 1;
            cam.videos[i].id = i;
            char name[64];
            int nid = cam.video_count + 1;
            name[0] = 'V'; name[1] = 'I'; name[2] = 'D'; name[3] = '_';
            name[4] = '0' + (nid / 1000) % 10;
            name[5] = '0' + (nid / 100) % 10;
            name[6] = '0' + (nid / 10) % 10;
            name[7] = '0' + nid % 10;
            name[8] = '.'; name[9] = 'm'; name[10] = 'p'; name[11] = '4'; name[12] = 0;
            strncpy(cam.videos[i].filename, name, 63);
            strncpy(cam.videos[i].path, "/home/user/Videos/", 255);
            tools_strcat(cam.videos[i].path, name, 256);
            cam.videos[i].width = 1920;
            cam.videos[i].height = 1080;
            cam.videos[i].fps = cam.settings.video_fps;
            cam.videos[i].size = 50 * 1024 * 1024;
            cam.videos[i].duration = 10000;  /* 10秒 */
            cam.videos[i].timestamp = 1;
            cam.videos[i].stabilization = 1;
            cam.video_count++;
            cam.total_videos++;
            cam.total_recording_time += 10000;
            break;
        }
    }
    cam.recording = 0;
    cam.state = CAMAPP_STATE_PREVIEW;
    return 0;
}

int camera_switch_camera(void)
{
    cam.current_camera = cam.current_camera ? 0 : 1;
    return cam.current_camera;
}

/* 设置 */
int camera_set_mode(cam_mode_t mode)
{
    cam.settings.mode = mode;
    return 0;
}

int camera_set_flash(cam_flash_t flash)
{
    cam.settings.flash = flash;
    return 0;
}

int camera_set_hdr(cam_hdr_t hdr)
{
    cam.settings.hdr = hdr;
    return 0;
}

int camera_set_zoom(int level)
{
    if (level < 0) level = 0;
    if (level > 100) level = 100;
    cam.zoom_level = level;
    return 0;
}

int camera_set_exposure(int value)
{
    if (value < -100) value = -100;
    if (value > 100) value = 100;
    cam.exposure = value;
    return 0;
}

int camera_set_iso(int iso)
{
    cam.iso = iso;
    return 0;
}

int camera_set_white_balance(int wb)
{
    cam.white_balance = wb;
    return 0;
}

int camera_set_timer(int seconds)
{
    cam.settings.timer = seconds;
    return 0;
}

int camera_set_resolution(int photo, int video)
{
    cam.settings.photo_resolution = photo;
    cam.settings.video_resolution = video;
    return 0;
}

int camera_set_fps(int fps)
{
    cam.settings.video_fps = fps;
    return 0;
}

int camera_toggle_grid(void)
{
    cam.settings.grid_lines = !cam.settings.grid_lines;
    return cam.settings.grid_lines;
}

int camera_toggle_face_detection(void)
{
    cam.settings.face_detection = !cam.settings.face_detection;
    return cam.settings.face_detection;
}

int camera_toggle_location_tag(void)
{
    cam.settings.location_tag = !cam.settings.location_tag;
    return cam.settings.location_tag;
}

/* 滤镜 */
int camera_filter_add(const char *name, int brightness, int contrast, int saturation)
{
    for (int i = 0; i < CAM_MAX_FILTERS; i++) {
        if (!cam.filters[i].used) {
            memset(&cam.filters[i], 0, sizeof(cam_filter_t));
            cam.filters[i].used = 1;
            cam.filters[i].id = i;
            if (name) strncpy(cam.filters[i].name, name, 63);
            cam.filters[i].brightness = brightness;
            cam.filters[i].contrast = contrast;
            cam.filters[i].saturation = saturation;
            cam.filters[i].intensity = 100;
            cam.filter_count++;
            return i;
        }
    }
    return -1;
}

int camera_filter_apply(int filter_id)
{
    if (filter_id < 0 || filter_id >= CAM_MAX_FILTERS || !cam.filters[filter_id].used) return -1;
    cam.settings.active_filter = filter_id;
    return 0;
}

int camera_filter_adjust(int filter_id, int intensity)
{
    if (filter_id < 0 || filter_id >= CAM_MAX_FILTERS || !cam.filters[filter_id].used) return -1;
    if (intensity < 0) intensity = 0;
    if (intensity > 100) intensity = 100;
    cam.filters[filter_id].intensity = intensity;
    return 0;
}

int camera_filter_list(cam_filter_t *filters, int max)
{
    if (!filters || max <= 0) return 0;
    int count = cam.filter_count < max ? cam.filter_count : max;
    for (int i = 0; i < count; i++)
        memcpy(&filters[i], &cam.filters[i], sizeof(cam_filter_t));
    return count;
}

/* 媒体库 */
int camera_get_photo(int id, cam_photo_t *photo)
{
    if (id < 0 || id >= CAM_MAX_PHOTOS || !cam.photos[id].used || !photo) return -1;
    memcpy(photo, &cam.photos[id], sizeof(cam_photo_t));
    return 0;
}

int camera_list_photos(cam_photo_t *photos, int max)
{
    if (!photos || max <= 0) return 0;
    int count = 0;
    for (int i = CAM_MAX_PHOTOS - 1; i >= 0 && count < max; i--) {
        if (cam.photos[i].used) memcpy(&photos[count++], &cam.photos[i], sizeof(cam_photo_t));
    }
    return count;
}

int camera_get_video(int id, cam_video_t *video)
{
    if (id < 0 || id >= CAM_MAX_VIDEOS || !cam.videos[id].used || !video) return -1;
    memcpy(video, &cam.videos[id], sizeof(cam_video_t));
    return 0;
}

int camera_list_videos(cam_video_t *videos, int max)
{
    if (!videos || max <= 0) return 0;
    int count = 0;
    for (int i = CAM_MAX_VIDEOS - 1; i >= 0 && count < max; i--) {
        if (cam.videos[i].used) memcpy(&videos[count++], &cam.videos[i], sizeof(cam_video_t));
    }
    return count;
}

int camera_delete_photo(int id)
{
    if (id < 0 || id >= CAM_MAX_PHOTOS || !cam.photos[id].used) return -1;
    cam.photos[id].used = 0;
    cam.photo_count--;
    return 0;
}

int camera_delete_video(int id)
{
    if (id < 0 || id >= CAM_MAX_VIDEOS || !cam.videos[id].used) return -1;
    cam.videos[id].used = 0;
    cam.video_count--;
    return 0;
}

int camera_toggle_favorite_photo(int id)
{
    if (id < 0 || id >= CAM_MAX_PHOTOS || !cam.photos[id].used) return -1;
    cam.photos[id].favorite = !cam.photos[id].favorite;
    return cam.photos[id].favorite;
}

int camera_toggle_favorite_video(int id)
{
    if (id < 0 || id >= CAM_MAX_VIDEOS || !cam.videos[id].used) return -1;
    cam.videos[id].favorite = !cam.videos[id].favorite;
    return cam.videos[id].favorite;
}

int camera_get_latest_photo(cam_photo_t *photo)
{
    if (!photo) return -1;
    for (int i = CAM_MAX_PHOTOS - 1; i >= 0; i--) {
        if (cam.photos[i].used) {
            memcpy(photo, &cam.photos[i], sizeof(cam_photo_t));
            return i;
        }
    }
    return -1;
}

/* 状态 */
int camera_get_state(camapp_state_t *state)
{
    if (!state) return -1;
    *state = cam.state;
    return 0;
}

int camera_get_settings(cam_settings_t *settings)
{
    if (!settings) return -1;
    memcpy(settings, &cam.settings, sizeof(cam_settings_t));
    return 0;
}

int camera_get_recording_time(uint64_t *ms)
{
    if (!ms) return -1;
    *ms = cam.recording ? 1000 : 0;
    return 0;
}

/* 统计 */
int camera_get_stats(camera_app_t *stats)
{
    if (!stats) return -1;
    memcpy(stats, &cam, sizeof(camera_app_t));
    return 0;
}
