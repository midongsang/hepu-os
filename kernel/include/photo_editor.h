#ifndef PHOTO_EDITOR_H
#define PHOTO_EDITOR_H

#include <stdint.h>

/* ============================================================================
 * 赫普OS 图片编辑器
 * 滤镜 / 裁剪 / 调整 / 涂鸦 / 文字
 * ========================================================================== */

#define PE_MAX_HISTORY 64
#define PE_MAX_FILTERS 32
#define PE_MAX_STICKERS 64
#define PE_MAX_TEXT 32
#define PE_PATH_LEN 256
#define PE_NAME_LEN 128

/* 编辑操作类型 */
typedef enum {
    PE_OP_FILTER = 0,
    PE_OP_ADJUST,
    PE_OP_CROP,
    PE_OP_ROTATE,
    PE_OP_FLIP,
    PE_OP_DRAW,
    PE_OP_TEXT,
    PE_OP_STICKER,
    PE_OP_UNDO,
    PE_OP_REDO,
} pe_operation_t;

/* 滤镜类型 */
typedef enum {
    PE_FILTER_ORIGINAL = 0,
    PE_FILTER_VIVID,
    PE_FILTER_SOFT,
    PE_FILTER_BW,
    PE_FILTER_SEPIA,
    PE_FILTER_VINTAGE,
    PE_FILTER_COOL,
    PE_FILTER_WARM,
    PE_FILTER_HIGH_CONTRAST,
    PE_FILTER_DRAMA,
    PE_FILTER_FILM,
    PE_FILTER_NOIR,
    PE_FILTER_PASTEL,
    PE_FILTER_FOOD,
    PE_FILTER_PORTRAIT,
    PE_FILTER_LANDSCAPE,
} pe_filter_type_t;

/* 调整参数 */
typedef struct {
    int brightness;  /* -100 to 100 */
    int contrast;    /* -100 to 100 */
    int saturation;  /* -100 to 100 */
    int exposure;    /* -100 to 100 */
    int highlights;  /* -100 to 100 */
    int shadows;     /* -100 to 100 */
    int warmth;      /* -100 to 100 */
    int tint;        /* -100 to 100 */
    int sharpness;   /* -100 to 100 */
    int vignette;    /* 0 to 100 */
    int grain;       /* 0 to 100 */
    int hsl_hue;     /* -100 to 100 */
    int hsl_saturation;
    int hsl_lightness;
} pe_adjustments_t;

/* 裁剪区域 */
typedef struct {
    int x, y;
    int width, height;
    int aspect_ratio;  /* 0=自由,1=1:1,2=4:3,3=16:9,4=3:2,5=2:3 */
} pe_crop_t;

/* 涂鸦画笔 */
typedef struct {
    int color;
    int size;  /* 1-100 */
    int opacity;  /* 0-100 */
    int brush_type;  /* 0=铅笔,1=毛笔,2=马克笔,3=喷枪,4=橡皮擦 */
    int smooth;
} pe_brush_t;

/* 文字层 */
typedef struct {
    int used;
    int id;
    char text[256];
    int x, y;
    int font_size;
    int color;
    int rotation;
    int opacity;
    char font[32];
    int bold;
    int italic;
    int underline;
    int shadow;
    int bg_color;
    int bg_opacity;
} pe_text_layer_t;

/* 贴纸 */
typedef struct {
    int used;
    int id;
    char name[64];
    int x, y;
    int width, height;
    int rotation;
    int opacity;
} pe_sticker_t;

/* 历史记录 */
typedef struct {
    int used;
    pe_operation_t operation;
    pe_adjustments_t adjustments;
    pe_crop_t crop;
    int rotation;
    int flip_h;
    int flip_v;
    pe_filter_type_t filter;
    int filter_intensity;
} pe_history_entry_t;

/* 图片编辑器状态 */
typedef struct {
    int initialized;
    int enabled;
    /* 当前图片 */
    char source_path[PE_PATH_LEN];
    char output_path[PE_PATH_LEN];
    uint32_t width;
    uint32_t height;
    int edited;
    /* 编辑状态 */
    pe_filter_type_t current_filter;
    int filter_intensity;  /* 0-100 */
    pe_adjustments_t adjustments;
    pe_crop_t crop;
    int rotation;
    int flip_h;
    int flip_v;
    /* 画笔 */
    pe_brush_t brush;
    int drawing;
    /* 文字 */
    pe_text_layer_t text_layers[PE_MAX_TEXT];
    int text_count;
    int current_text;
    /* 贴纸 */
    pe_sticker_t stickers[PE_MAX_STICKERS];
    int sticker_count;
    /* 历史 */
    pe_history_entry_t history[PE_MAX_HISTORY];
    int history_count;
    int history_pos;
    /* 滤镜预设 */
    int filter_count;
    /* 统计 */
    uint64_t total_edits;
    uint64_t total_exports;
} photo_editor_t;

/* 初始化 */
void photo_editor_init(void);

/* 图片操作 */
int pe_open(const char *path);
int pe_save(const char *path);
int pe_export(const char *path, int quality);
int pe_new_image(uint32_t width, uint32_t height);
int pe_get_image_info(uint32_t *width, uint32_t *height, int *edited);

/* 滤镜 */
int pe_apply_filter(pe_filter_type_t filter, int intensity);
int pe_get_filter(pe_filter_type_t *filter, int *intensity);
int pe_list_filters(char names[][32], int max);

/* 调整 */
int pe_adjust(const pe_adjustments_t *adj);
int pe_get_adjustments(pe_adjustments_t *adj);
int pe_reset_adjustments(void);

/* 裁剪/旋转/翻转 */
int pe_crop(const pe_crop_t *crop);
int pe_get_crop(pe_crop_t *crop);
int pe_rotate(int degrees);
int pe_flip(int horizontal, int vertical);
int pe_get_rotation(int *degrees, int *flip_h, int *flip_v);

/* 涂鸦 */
int pe_set_brush(const pe_brush_t *brush);
int pe_get_brush(pe_brush_t *brush);
int pe_start_drawing(int x, int y);
int pe_draw_to(int x, int y);
int pe_end_drawing(void);
int pe_clear_drawing(void);

/* 文字 */
int pe_add_text(const char *text, int x, int y);
int pe_update_text(int id, const char *text);
int pe_remove_text(int id);
int pe_get_text(int id, pe_text_layer_t *text);
int pe_list_text(pe_text_layer_t *texts, int max);
int pe_set_text_property(int id, int property, int value);

/* 贴纸 */
int pe_add_sticker(const char *name, int x, int y);
int pe_remove_sticker(int id);
int pe_get_sticker(int id, pe_sticker_t *sticker);
int pe_list_stickers(pe_sticker_t *stickers, int max);

/* 历史 */
int pe_undo(void);
int pe_redo(void);
int pe_can_undo(void);
int pe_can_redo(void);
int pe_reset(void);

/* 统计 */
int pe_get_stats(photo_editor_t *stats);

#endif /* PHOTO_EDITOR_H */
