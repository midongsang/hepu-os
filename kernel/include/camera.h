#ifndef CAMERA_H
#define CAMERA_H

#include <stdint.h>

#define CAMERA_MAX_DEVICES 2
#define CAMERA_MAX_BUFFERS 4

/* 像素格式 */
typedef enum {
    CAM_FMT_RGB565 = 0,
    CAM_FMT_RGB888,
    CAM_FMT_YUYV,
    CAM_FMT_YUV420,
    CAM_FMT_JPEG,
    CAM_FMT_BAYER,
} camera_format_t;

/* 摄像头状态 */
typedef enum {
    CAM_STATE_IDLE = 0,
    CAM_STATE_STREAMING,
    CAM_STATE_ERROR,
} camera_state_t;

/* 帧缓冲区 */
typedef struct {
    void *data;
    uint32_t size;
    uint32_t bytesused;
    uint32_t width;
    uint32_t height;
    uint64_t timestamp;
    int index;
    int ready;
} camera_buffer_t;

/* 摄像头格式描述 */
typedef struct {
    camera_format_t format;
    uint32_t width;
    uint32_t height;
    uint32_t fps;
} camera_fmt_desc_t;

/* 摄像头控制 */
typedef struct {
    const char *name;
    int min;
    int max;
    int default_val;
    int current;
} camera_control_t;

/* 摄像头设备 */
typedef struct {
    int used;
    const char *name;
    int type;  /* 0=USB UVC, 1=CSI, 2=并行接口 */
    uint32_t io_base;
    int irq;
    camera_state_t state;
    camera_format_t format;
    uint32_t width;
    uint32_t height;
    uint32_t fps;
    camera_buffer_t buffers[CAMERA_MAX_BUFFERS];
    int buffer_count;
    int current_buffer;
    camera_control_t controls[8];
    int control_count;
    uint64_t frame_count;
    int initialized;
} camera_device_t;

/* 初始化摄像头子系统 */
void camera_init(void);

/* 打开摄像头 */
int camera_open(int device);

/* 关闭摄像头 */
int camera_close(int device);

/* 设置格式 */
int camera_set_format(int device, camera_format_t fmt, uint32_t w, uint32_t h, uint32_t fps);

/* 获取格式 */
int camera_get_format(int device, camera_fmt_desc_t *desc);

/* 请求缓冲区 */
int camera_req_buffers(int device, int count);

/* 查询缓冲区 */
int camera_query_buffer(int device, int index, camera_buffer_t *buf);

/* 入队缓冲区 */
int camera_qbuf(int device, int index);

/* 出队缓冲区 (阻塞) */
int camera_dqbuf(int device, camera_buffer_t *buf);

/* 开始流 */
int camera_stream_on(int device);

/* 停止流 */
int camera_stream_off(int device);

/* 设置控制 */
int camera_set_control(int device, const char *name, int value);

/* 获取控制 */
int camera_get_control(int device, const char *name);

/* 读取一帧 (简化接口) */
int camera_grab_frame(int device, void *buf, uint32_t size);

/* 格式转换: YUYV -> RGB565 */
int camera_yuyv_to_rgb565(const void *yuyv, void *rgb, uint32_t width, uint32_t height);

/* 格式转换: RGB565 -> RGB888 */
int camera_rgb565_to_rgb888(const void *rgb565, void *rgb888, uint32_t width, uint32_t height);

/* 摄像头中断处理 */
void camera_handle_irq(void);

#endif /* CAMERA_H */
