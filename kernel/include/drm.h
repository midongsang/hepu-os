/* ============================================================================
 * 赫普OS - DRM/KMS 接口框架
 * V8.94: 初始实现（方案B第一步）
 * 显示模式设置、帧缓冲管理、平面/CRTC/连接器管理
 * 参考: Linux DRM/KMS 架构，自研简化实现
 * ========================================================================== */

#ifndef _DRM_H
#define _DRM_H

#include <stdint.h>

/* DRM版本 */
#define DRM_VERSION_MAJOR  1
#define DRM_VERSION_MINOR  0
#define DRM_VERSION_PATCH  0

/* 最大限制 */
#define DRM_MAX_CRTC        4   /* 最多4个CRTC（显示控制器） */
#define DRM_MAX_CONNECTOR   4   /* 最多4个连接器（显示器接口） */
#define DRM_MAX_ENCODER     4   /* 最多4个编码器 */
#define DRM_MAX_PLANE       8   /* 最多8个平面 */
#define DRM_MAX_FB          16  /* 最多16个帧缓冲 */
#define DRM_MAX_MODE        32  /* 每个连接器最多32个显示模式 */

/* 像素格式 */
typedef enum {
    DRM_FORMAT_XRGB8888 = 0,
    DRM_FORMAT_ARGB8888,
    DRM_FORMAT_RGB565,
    DRM_FORMAT_XBGR8888,
    DRM_FORMAT_ABGR8888,
    DRM_FORMAT_MAX
} drm_format_t;

/* 连接器状态 */
typedef enum {
    DRM_CONNECTOR_DISCONNECTED = 0,
    DRM_CONNECTOR_CONNECTED,
    DRM_CONNECTOR_UNKNOWN
} drm_connector_status_t;

/* 连接器类型 */
typedef enum {
    DRM_CONNECTOR_TYPE_UNKNOWN = 0,
    DRM_CONNECTOR_VGA,
    DRM_CONNECTOR_DVI_I,
    DRM_CONNECTOR_DVI_D,
    DRM_CONNECTOR_DVI_A,
    DRM_CONNECTOR_COMPOSITE,
    DRM_CONNECTOR_SVIDEO,
    DRM_CONNECTOR_LVDS,
    DRM_CONNECTOR_COMPONENT,
    DRM_CONNECTOR_9DIN,
    DRM_CONNECTOR_DISPLAYPORT,
    DRM_CONNECTOR_HDMI_A,
    DRM_CONNECTOR_HDMI_B,
    DRM_CONNECTOR_TV,
    DRM_CONNECTOR_EDP,
    DRM_CONNECTOR_VIRTUAL,
    DRM_CONNECTOR_DSI,
    DRM_CONNECTOR_DPI
} drm_connector_type_t;

/* 显示模式 */
typedef struct {
    uint32_t clock;          /* 像素时钟（kHz） */
    uint16_t hdisplay;       /* 水平显示 */
    uint16_t hsync_start;
    uint16_t hsync_end;
    uint16_t htotal;
    uint16_t hskew;
    uint16_t vdisplay;       /* 垂直显示 */
    uint16_t vsync_start;
    uint16_t vsync_end;
    uint16_t vtotal;
    uint16_t vscan;
    uint32_t vrefresh;       /* 刷新率（Hz） */
    uint32_t flags;
    char     name[32];       /* 模式名称，如 "1024x768" */
} drm_mode_t;

/* 帧缓冲 */
typedef struct {
    uint32_t     id;
    uint32_t     width;
    uint32_t     height;
    uint32_t     pitch;       /* 行字节数 */
    uint32_t     bpp;         /* 每像素位数 */
    drm_format_t format;
    uint32_t     handle;      /* 内存句柄 */
    void        *vaddr;       /* 虚拟地址 */
    uint32_t     paddr;       /* 物理地址 */
    uint32_t     size;        /* 大小（字节） */
    int          used;
} drm_framebuffer_t;

/* 平面类型 */
typedef enum {
    DRM_PLANE_TYPE_PRIMARY = 0,  /* 主平面 */
    DRM_PLANE_TYPE_CURSOR,       /* 光标平面 */
    DRM_PLANE_TYPE_OVERLAY       /* 叠加平面 */
} drm_plane_type_t;

/* 平面 */
typedef struct {
    uint32_t         id;
    drm_plane_type_t type;
    uint32_t         possible_crtcs;  /* 位掩码：可绑定的CRTC */
    uint32_t         fb_id;          /* 当前绑定的帧缓冲 */
    int32_t          x;              /* 位置 */
    int32_t          y;
    uint32_t         w;
    uint32_t         h;
    int              enabled;
    int              used;
} drm_plane_t;

/* CRTC（显示控制器） */
typedef struct {
    uint32_t     id;
    uint32_t     possible_encoders;  /* 位掩码：可驱动的编码器 */
    drm_mode_t   mode;               /* 当前模式 */
    uint32_t     fb_id;              /* 当前帧缓冲 */
    int          active;
    int          used;
} drm_crtc_t;

/* 编码器 */
typedef struct {
    uint32_t    id;
    uint32_t    possible_crtcs;     /* 位掩码：可连接的CRTC */
    uint32_t    possible_clones;    /* 位掩码：可克隆的编码器 */
    int         used;
} drm_encoder_t;

/* 连接器 */
typedef struct {
    uint32_t               id;
    drm_connector_type_t   connector_type;
    uint32_t               connector_type_id;
    drm_connector_status_t status;
    uint32_t               encoder_id;     /* 当前连接的编码器 */
    drm_mode_t             modes[DRM_MAX_MODE];  /* 支持的模式 */
    int                    mode_count;
    drm_mode_t             current_mode;  /* 当前模式 */
    int                    used;
} drm_connector_t;

/* DRM设备 */
typedef struct {
    int              initialized;
    char             name[32];
    drm_crtc_t       crtcs[DRM_MAX_CRTC];
    drm_connector_t  connectors[DRM_MAX_CONNECTOR];
    drm_encoder_t    encoders[DRM_MAX_ENCODER];
    drm_plane_t      planes[DRM_MAX_PLANE];
    drm_framebuffer_t framebuffers[DRM_MAX_FB];
    uint32_t         next_fb_id;
    uint32_t         crtc_count;
    uint32_t         connector_count;
    uint32_t         encoder_count;
    uint32_t         plane_count;
    /* 后端操作 */
    int (*set_mode)(uint32_t crtc_id, drm_mode_t *mode, uint32_t fb_id);
    int (*page_flip)(uint32_t crtc_id, uint32_t fb_id);
    int (*create_dumb)(uint32_t width, uint32_t height, uint32_t bpp,
                       uint32_t *handle, uint32_t *pitch, uint64_t *offset);
    int (*map_dumb)(uint32_t handle, void **vaddr);
    int (*destroy_dumb)(uint32_t handle);
} drm_device_t;

/* 函数声明 */

/* 初始化/关闭 */
int drm_init(void);
void drm_shutdown(void);
drm_device_t *drm_get_device(void);

/* 模式设置 */
int drm_set_mode(uint32_t crtc_id, drm_mode_t *mode, uint32_t fb_id);
int drm_page_flip(uint32_t crtc_id, uint32_t fb_id);
int drm_get_connector_modes(uint32_t connector_id, drm_mode_t *modes, int max);
drm_connector_status_t drm_get_connector_status(uint32_t connector_id);

/* 帧缓冲管理 */
int drm_framebuffer_create(uint32_t width, uint32_t height,
                           drm_format_t format, uint32_t *fb_id);
int drm_framebuffer_destroy(uint32_t fb_id);
drm_framebuffer_t *drm_framebuffer_get(uint32_t fb_id);
void *drm_framebuffer_map(uint32_t fb_id);

/* 平面管理 */
int drm_plane_set(uint32_t plane_id, uint32_t crtc_id, uint32_t fb_id,
                  int32_t x, int32_t y, uint32_t w, uint32_t h);
int drm_plane_enable(uint32_t plane_id, int enable);

/* 资源枚举 */
int drm_get_crtcs(drm_crtc_t *crtcs, int max);
int drm_get_connectors(drm_connector_t *connectors, int max);
int drm_get_encoders(drm_encoder_t *encoders, int max);
int drm_get_planes(drm_plane_t *planes, int max);

/* 辅助函数 */
int drm_mode_match(drm_mode_t *mode, uint32_t width, uint32_t height);
void drm_mode_set_name(drm_mode_t *mode);
uint32_t drm_mode_vrefresh(drm_mode_t *mode);

/* 后端注册 */
int drm_register_backend(const char *name,
                         int (*set_mode)(uint32_t, drm_mode_t*, uint32_t),
                         int (*page_flip)(uint32_t, uint32_t),
                         int (*create_dumb)(uint32_t, uint32_t, uint32_t,
                                           uint32_t*, uint32_t*, uint64_t*),
                         int (*map_dumb)(uint32_t, void**),
                         int (*destroy_dumb)(uint32_t));

#endif /* _DRM_H */
