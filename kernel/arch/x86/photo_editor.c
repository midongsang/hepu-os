/* ============================================================================
 * 赫普OS - 图片编辑器
 * ========================================================================== */

#include <stdint.h>
#include "photo_editor.h"
#include "vga.h"
#include "string.h"

static photo_editor_t pe;

/* 滤镜名称 */
static const char *filter_names[] = {
    " 原 map ", " 鲜艳 ", " 柔 & ", " 黑白 ", " 复古 ", " 怀旧 ", " 冷色调 ", " 暖色调 ",
    " high pair 比 ", " 戏剧 ", " 胶片 ", " noir", " 柔 & 色 ", " 美食 ", " portrait ", " 风景 "
};

void photo_editor_init(void)
{
    memset(&pe, 0, sizeof(photo_editor_t));
    pe.initialized = 1;
    pe.enabled = 1;
    pe.current_filter = PE_FILTER_ORIGINAL;
    pe.filter_intensity = 100;
    pe.filter_count = 16;

    /* 默认画笔 */
    pe.brush.color = 0xFF000000;
    pe.brush.size = 10;
    pe.brush.opacity = 100;
    pe.brush.brush_type = 0;
    pe.brush.smooth = 1;

    /* 默认裁剪 */
    pe.crop.aspect_ratio = 0;

    vga_puts("[PEDIT] photo editor init done \n");
    vga_printf(" filters: %d, text layer: %d, sticker: %d\n",
               pe.filter_count, PE_MAX_TEXT, PE_MAX_STICKERS);
}

/* 图片操作 */
int pe_open(const char *path)
{
    if (!path) return -1;
    strncpy(pe.source_path, path, 255);
    pe.width = 1920;
    pe.height = 1080;
    pe.edited = 0;
    pe.crop.x = 0;
    pe.crop.y = 0;
    pe.crop.width = pe.width;
    pe.crop.height = pe.height;
    return 0;
}

int pe_save(const char *path)
{
    if (!path) return -1;
    strncpy(pe.output_path, path, 255);
    pe.edited = 0;
    return 0;
}

int pe_export(const char *path, int quality)
{
    (void)quality;
    if (!path) return -1;
    pe.total_exports++;
    return 0;
}

int pe_new_image(uint32_t width, uint32_t height)
{
    pe.width = width;
    pe.height = height;
    pe.source_path[0] = 0;
    pe.edited = 0;
    return 0;
}

int pe_get_image_info(uint32_t *width, uint32_t *height, int *edited)
{
    if (width) *width = pe.width;
    if (height) *height = pe.height;
    if (edited) *edited = pe.edited;
    return 0;
}

/* 滤镜 */
int pe_apply_filter(pe_filter_type_t filter, int intensity)
{
    pe.current_filter = filter;
    pe.filter_intensity = intensity;
    pe.edited = 1;
    pe.total_edits++;
    return 0;
}

int pe_get_filter(pe_filter_type_t *filter, int *intensity)
{
    if (filter) *filter = pe.current_filter;
    if (intensity) *intensity = pe.filter_intensity;
    return 0;
}

int pe_list_filters(char names[][32], int max)
{
    if (!names || max <= 0) return 0;
    int count = pe.filter_count < max ? pe.filter_count : max;
    for (int i = 0; i < count; i++)
        strncpy(names[i], filter_names[i], 31);
    return count;
}

/* 调整 */
int pe_adjust(const pe_adjustments_t *adj)
{
    if (!adj) return -1;
    memcpy(&pe.adjustments, adj, sizeof(pe_adjustments_t));
    pe.edited = 1;
    pe.total_edits++;
    return 0;
}

int pe_get_adjustments(pe_adjustments_t *adj)
{
    if (!adj) return -1;
    memcpy(adj, &pe.adjustments, sizeof(pe_adjustments_t));
    return 0;
}

int pe_reset_adjustments(void)
{
    memset(&pe.adjustments, 0, sizeof(pe_adjustments_t));
    return 0;
}

/* 裁剪/旋转/翻转 */
int pe_crop(const pe_crop_t *crop)
{
    if (!crop) return -1;
    memcpy(&pe.crop, crop, sizeof(pe_crop_t));
    pe.edited = 1;
    pe.total_edits++;
    return 0;
}

int pe_get_crop(pe_crop_t *crop)
{
    if (!crop) return -1;
    memcpy(crop, &pe.crop, sizeof(pe_crop_t));
    return 0;
}

int pe_rotate(int degrees)
{
    pe.rotation = (pe.rotation + degrees) % 360;
    pe.edited = 1;
    pe.total_edits++;
    return 0;
}

int pe_flip(int horizontal, int vertical)
{
    if (horizontal) pe.flip_h = !pe.flip_h;
    if (vertical) pe.flip_v = !pe.flip_v;
    pe.edited = 1;
    pe.total_edits++;
    return 0;
}

int pe_get_rotation(int *degrees, int *flip_h, int *flip_v)
{
    if (degrees) *degrees = pe.rotation;
    if (flip_h) *flip_h = pe.flip_h;
    if (flip_v) *flip_v = pe.flip_v;
    return 0;
}

/* 涂鸦 */
int pe_set_brush(const pe_brush_t *brush)
{
    if (!brush) return -1;
    memcpy(&pe.brush, brush, sizeof(pe_brush_t));
    return 0;
}

int pe_get_brush(pe_brush_t *brush)
{
    if (!brush) return -1;
    memcpy(brush, &pe.brush, sizeof(pe_brush_t));
    return 0;
}

int pe_start_drawing(int x, int y)
{
    (void)x; (void)y;
    pe.drawing = 1;
    return 0;
}

int pe_draw_to(int x, int y)
{
    (void)x; (void)y;
    if (!pe.drawing) return -1;
    return 0;
}

int pe_end_drawing(void)
{
    pe.drawing = 0;
    pe.edited = 1;
    pe.total_edits++;
    return 0;
}

int pe_clear_drawing(void)
{
    return 0;
}

/* 文字 */
int pe_add_text(const char *text, int x, int y)
{
    for (int i = 0; i < PE_MAX_TEXT; i++) {
        if (!pe.text_layers[i].used) {
            memset(&pe.text_layers[i], 0, sizeof(pe_text_layer_t));
            pe.text_layers[i].used = 1;
            pe.text_layers[i].id = i;
            if (text) strncpy(pe.text_layers[i].text, text, 255);
            pe.text_layers[i].x = x;
            pe.text_layers[i].y = y;
            pe.text_layers[i].font_size = 48;
            pe.text_layers[i].color = 0xFFFFFFFF;
            pe.text_layers[i].opacity = 100;
            strncpy(pe.text_layers[i].font, "default", 31);
            pe.text_count++;
            pe.current_text = i;
            pe.edited = 1;
            return i;
        }
    }
    return -1;
}

int pe_update_text(int id, const char *text)
{
    if (id < 0 || id >= PE_MAX_TEXT || !pe.text_layers[id].used || !text) return -1;
    strncpy(pe.text_layers[id].text, text, 255);
    pe.edited = 1;
    return 0;
}

int pe_remove_text(int id)
{
    if (id < 0 || id >= PE_MAX_TEXT || !pe.text_layers[id].used) return -1;
    pe.text_layers[id].used = 0;
    pe.text_count--;
    return 0;
}

int pe_get_text(int id, pe_text_layer_t *text)
{
    if (id < 0 || id >= PE_MAX_TEXT || !pe.text_layers[id].used || !text) return -1;
    memcpy(text, &pe.text_layers[id], sizeof(pe_text_layer_t));
    return 0;
}

int pe_list_text(pe_text_layer_t *texts, int max)
{
    if (!texts || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < PE_MAX_TEXT && count < max; i++) {
        if (pe.text_layers[i].used) memcpy(&texts[count++], &pe.text_layers[i], sizeof(pe_text_layer_t));
    }
    return count;
}

int pe_set_text_property(int id, int property, int value)
{
    if (id < 0 || id >= PE_MAX_TEXT || !pe.text_layers[id].used) return -1;
    switch (property) {
        case 0: pe.text_layers[id].font_size = value; break;
        case 1: pe.text_layers[id].color = value; break;
        case 2: pe.text_layers[id].rotation = value; break;
        case 3: pe.text_layers[id].opacity = value; break;
        case 4: pe.text_layers[id].bold = value; break;
        case 5: pe.text_layers[id].italic = value; break;
        case 6: pe.text_layers[id].underline = value; break;
        case 7: pe.text_layers[id].x = value; break;
        case 8: pe.text_layers[id].y = value; break;
        default: return -1;
    }
    pe.edited = 1;
    return 0;
}

/* 贴纸 */
int pe_add_sticker(const char *name, int x, int y)
{
    for (int i = 0; i < PE_MAX_STICKERS; i++) {
        if (!pe.stickers[i].used) {
            memset(&pe.stickers[i], 0, sizeof(pe_sticker_t));
            pe.stickers[i].used = 1;
            pe.stickers[i].id = i;
            if (name) strncpy(pe.stickers[i].name, name, 63);
            pe.stickers[i].x = x;
            pe.stickers[i].y = y;
            pe.stickers[i].width = 100;
            pe.stickers[i].height = 100;
            pe.stickers[i].opacity = 100;
            pe.sticker_count++;
            pe.edited = 1;
            return i;
        }
    }
    return -1;
}

int pe_remove_sticker(int id)
{
    if (id < 0 || id >= PE_MAX_STICKERS || !pe.stickers[id].used) return -1;
    pe.stickers[id].used = 0;
    pe.sticker_count--;
    return 0;
}

int pe_get_sticker(int id, pe_sticker_t *sticker)
{
    if (id < 0 || id >= PE_MAX_STICKERS || !pe.stickers[id].used || !sticker) return -1;
    memcpy(sticker, &pe.stickers[id], sizeof(pe_sticker_t));
    return 0;
}

int pe_list_stickers(pe_sticker_t *stickers, int max)
{
    if (!stickers || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < PE_MAX_STICKERS && count < max; i++) {
        if (pe.stickers[i].used) memcpy(&stickers[count++], &pe.stickers[i], sizeof(pe_sticker_t));
    }
    return count;
}

/* 历史 */
int pe_undo(void)
{
    if (pe.history_pos <= 0) return -1;
    pe.history_pos--;
    pe.edited = 1;
    return 0;
}

int pe_redo(void)
{
    if (pe.history_pos >= pe.history_count - 1) return -1;
    pe.history_pos++;
    pe.edited = 1;
    return 0;
}

int pe_can_undo(void)
{
    return pe.history_pos > 0;
}

int pe_can_redo(void)
{
    return pe.history_pos < pe.history_count - 1;
}

int pe_reset(void)
{
    pe.current_filter = PE_FILTER_ORIGINAL;
    pe.filter_intensity = 100;
    memset(&pe.adjustments, 0, sizeof(pe_adjustments_t));
    pe.rotation = 0;
    pe.flip_h = 0;
    pe.flip_v = 0;
    pe.crop.x = 0;
    pe.crop.y = 0;
    pe.crop.width = pe.width;
    pe.crop.height = pe.height;
    for (int i = 0; i < PE_MAX_TEXT; i++) pe.text_layers[i].used = 0;
    pe.text_count = 0;
    for (int i = 0; i < PE_MAX_STICKERS; i++) pe.stickers[i].used = 0;
    pe.sticker_count = 0;
    pe.edited = 0;
    return 0;
}

/* 统计 */
int pe_get_stats(photo_editor_t *stats)
{
    if (!stats) return -1;
    memcpy(stats, &pe, sizeof(photo_editor_t));
    return 0;
}
