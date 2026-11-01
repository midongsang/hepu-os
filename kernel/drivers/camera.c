/* ============================================================================
 * 赫普OS - V4L2式摄像头驱动
 * 视频捕获 + 格式转换 + 缓冲区管理
 * ========================================================================== */

#include <stdint.h>
#include "camera.h"
#include "vga.h"
#include "string.h"
#include "kheap.h"

static camera_device_t devices[CAMERA_MAX_DEVICES];
static int camera_initialized = 0;

void camera_init(void)
{
    for (int i = 0; i < CAMERA_MAX_DEVICES; i++) {
        devices[i].used = 0;
    }
    camera_initialized = 1;

    /* 注册默认摄像头 (模拟USB UVC) */
    camera_device_t *cam = &devices[0];
    memset(cam, 0, sizeof(camera_device_t));
    cam->used = 1;
    cam->name = "USB UVC Camera";
    cam->type = 0;
    cam->io_base = 0xE000;
    cam->irq = 10;
    cam->state = CAM_STATE_IDLE;
    cam->format = CAM_FMT_YUYV;
    cam->width = 640;
    cam->height = 480;
    cam->fps = 30;
    cam->frame_count = 0;
    cam->initialized = 1;

    /* 控制项 */
    cam->controls[0].name = "Brightness"; cam->controls[0].min = 0; cam->controls[0].max = 100; cam->controls[0].default_val = 50; cam->controls[0].current = 50;
    cam->controls[1].name = "Contrast"; cam->controls[1].min = 0; cam->controls[1].max = 100; cam->controls[1].default_val = 50; cam->controls[1].current = 50;
    cam->controls[2].name = "Saturation"; cam->controls[2].min = 0; cam->controls[2].max = 100; cam->controls[2].default_val = 50; cam->controls[2].current = 50;
    cam->controls[3].name = "Exposure"; cam->controls[3].min = 1; cam->controls[3].max = 10000; cam->controls[3].default_val = 500; cam->controls[3].current = 500;
    cam->control_count = 4;

    vga_puts("[CAMERA] camera subsystem init done \n");
    vga_printf(" device: %s, default: %dx%d@%dfps\n", cam->name, cam->width, cam->height, cam->fps);
}

int camera_open(int device)
{
    if (device < 0 || device >= CAMERA_MAX_DEVICES || !devices[device].used) return -1;
    vga_printf("[CAMERA] open device %d: %s\n", device, devices[device].name);
    return 0;
}

int camera_close(int device)
{
    if (device < 0 || device >= CAMERA_MAX_DEVICES || !devices[device].used) return -1;
    if (devices[device].state == CAM_STATE_STREAMING) {
        camera_stream_off(device);
    }
    /* 释放缓冲区 */
    for (int i = 0; i < devices[device].buffer_count; i++) {
        if (devices[device].buffers[i].data) kfree(devices[device].buffers[i].data);
    }
    devices[device].buffer_count = 0;
    return 0;
}

int camera_set_format(int device, camera_format_t fmt, uint32_t w, uint32_t h, uint32_t fps)
{
    if (device < 0 || device >= CAMERA_MAX_DEVICES || !devices[device].used) return -1;
    if (devices[device].state == CAM_STATE_STREAMING) return -1;

    devices[device].format = fmt;
    devices[device].width = w;
    devices[device].height = h;
    devices[device].fps = fps;
    return 0;
}

int camera_get_format(int device, camera_fmt_desc_t *desc)
{
    if (device < 0 || device >= CAMERA_MAX_DEVICES || !devices[device].used || !desc) return -1;
    desc->format = devices[device].format;
    desc->width = devices[device].width;
    desc->height = devices[device].height;
    desc->fps = devices[device].fps;
    return 0;
}

int camera_req_buffers(int device, int count)
{
    if (device < 0 || device >= CAMERA_MAX_DEVICES || !devices[device].used) return -1;
    if (count > CAMERA_MAX_BUFFERS) count = CAMERA_MAX_BUFFERS;

    camera_device_t *cam = &devices[device];
    uint32_t buf_size = cam->width * cam->height * 2;  /* YUYV=2字节/像素 */

    for (int i = 0; i < count; i++) {
        cam->buffers[i].data = kmalloc(buf_size);
        cam->buffers[i].size = buf_size;
        cam->buffers[i].bytesused = 0;
        cam->buffers[i].width = cam->width;
        cam->buffers[i].height = cam->height;
        cam->buffers[i].index = i;
        cam->buffers[i].ready = 0;
    }
    cam->buffer_count = count;
    cam->current_buffer = 0;
    return count;
}

int camera_query_buffer(int device, int index, camera_buffer_t *buf)
{
    if (device < 0 || device >= CAMERA_MAX_DEVICES || !devices[device].used) return -1;
    if (index < 0 || index >= devices[device].buffer_count) return -1;
    if (buf) memcpy(buf, &devices[device].buffers[index], sizeof(camera_buffer_t));
    return 0;
}

int camera_qbuf(int device, int index)
{
    if (device < 0 || device >= CAMERA_MAX_DEVICES || !devices[device].used) return -1;
    if (index < 0 || index >= devices[device].buffer_count) return -1;
    devices[device].buffers[index].ready = 0;
    return 0;
}

int camera_dqbuf(int device, camera_buffer_t *buf)
{
    if (device < 0 || device >= CAMERA_MAX_DEVICES || !devices[device].used) return -1;
    if (devices[device].state != CAM_STATE_STREAMING) return -1;

    /* 原型阶段: 模拟等待帧 */
    camera_device_t *cam = &devices[device];
    int idx = cam->current_buffer;
    cam->buffers[idx].bytesused = cam->width * cam->height * 2;
    cam->buffers[idx].timestamp = cam->frame_count * (1000 / cam->fps);
    cam->buffers[idx].ready = 1;
    cam->frame_count++;
    cam->current_buffer = (cam->current_buffer + 1) % cam->buffer_count;

    if (buf) memcpy(buf, &cam->buffers[idx], sizeof(camera_buffer_t));
    return idx;
}

int camera_stream_on(int device)
{
    if (device < 0 || device >= CAMERA_MAX_DEVICES || !devices[device].used) return -1;
    if (devices[device].buffer_count == 0) return -1;
    devices[device].state = CAM_STATE_STREAMING;
    vga_printf("[CAMERA] start stream: %dx%d@%dfps\n",
               devices[device].width, devices[device].height, devices[device].fps);
    return 0;
}

int camera_stream_off(int device)
{
    if (device < 0 || device >= CAMERA_MAX_DEVICES || !devices[device].used) return -1;
    devices[device].state = CAM_STATE_IDLE;
    return 0;
}

int camera_set_control(int device, const char *name, int value)
{
    if (device < 0 || device >= CAMERA_MAX_DEVICES || !devices[device].used) return -1;
    for (int i = 0; i < devices[device].control_count; i++) {
        if (strcmp(devices[device].controls[i].name, name) == 0) {
            if (value < devices[device].controls[i].min) value = devices[device].controls[i].min;
            if (value > devices[device].controls[i].max) value = devices[device].controls[i].max;
            devices[device].controls[i].current = value;
            return 0;
        }
    }
    return -1;
}

int camera_get_control(int device, const char *name)
{
    if (device < 0 || device >= CAMERA_MAX_DEVICES || !devices[device].used) return -1;
    for (int i = 0; i < devices[device].control_count; i++) {
        if (strcmp(devices[device].controls[i].name, name) == 0) {
            return devices[device].controls[i].current;
        }
    }
    return -1;
}

int camera_grab_frame(int device, void *buf, uint32_t size)
{
    if (device < 0 || device >= CAMERA_MAX_DEVICES || !devices[device].used) return -1;
    camera_device_t *cam = &devices[device];
    uint32_t needed = cam->width * cam->height * 2;
    if (size < needed) return -1;

    /* 原型阶段: 生成测试图案 */
    uint16_t *out = (uint16_t *)buf;
    for (uint32_t y = 0; y < cam->height; y++) {
        for (uint32_t x = 0; x < cam->width; x++) {
            uint8_t r = (x * 255) / cam->width;
            uint8_t g = (y * 255) / cam->height;
            uint8_t b = 128;
            out[y * cam->width + x] = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
        }
    }
    cam->frame_count++;
    return needed;
}

/* YUYV -> RGB565 转换 */
int camera_yuyv_to_rgb565(const void *yuyv, void *rgb, uint32_t width, uint32_t height)
{
    if (!yuyv || !rgb) return -1;
    const uint8_t *src = (const uint8_t *)yuyv;
    uint16_t *dst = (uint16_t *)rgb;

    for (uint32_t i = 0; i < width * height; i += 2) {
        int y0 = src[i * 2];
        int u = src[i * 2 + 1] - 128;
        int y1 = src[i * 2 + 2];
        int v = src[i * 2 + 3] - 128;

        for (int p = 0; p < 2; p++) {
            int y = (p == 0) ? y0 : y1;
            int r = y + (int)(1.402 * v);
            int g = y - (int)(0.344 * u) - (int)(0.714 * v);
            int b = y + (int)(1.772 * u);
            if (r < 0) r = 0;
            if (r > 255) r = 255;
            if (g < 0) g = 0;
            if (g > 255) g = 255;
            if (b < 0) b = 0;
            if (b > 255) b = 255;
            dst[i + p] = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
        }
    }
    return width * height * 2;
}

/* RGB565 -> RGB888 转换 */
int camera_rgb565_to_rgb888(const void *rgb565, void *rgb888, uint32_t width, uint32_t height)
{
    if (!rgb565 || !rgb888) return -1;
    const uint16_t *src = (const uint16_t *)rgb565;
    uint8_t *dst = (uint8_t *)rgb888;

    for (uint32_t i = 0; i < width * height; i++) {
        uint16_t px = src[i];
        dst[i * 3] = ((px >> 11) & 0x1F) << 3;
        dst[i * 3 + 1] = ((px >> 5) & 0x3F) << 2;
        dst[i * 3 + 2] = (px & 0x1F) << 3;
    }
    return width * height * 3;
}

void camera_handle_irq(void)
{
    /* 原型阶段: 帧就绪中断 */
    for (int i = 0; i < CAMERA_MAX_DEVICES; i++) {
        if (devices[i].used && devices[i].state == CAM_STATE_STREAMING) {
            devices[i].buffers[devices[i].current_buffer].ready = 1;
        }
    }
}
