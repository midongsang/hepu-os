/* ============================================================================
 * 赫普OS - 弹性自由桌面
 * ========================================================================== */

#include <stdint.h>
#include "elastic_desktop.h"
#include "vga.h"
#include "string.h"

static elastic_desktop_t ed;

void elastic_desktop_init(void)
{
    memset(&ed, 0, sizeof(elastic_desktop_t));
    ed.initialized = 1;
    ed.enabled = 1;
    ed.current_page = 0;
    ed.config.enabled = 1;
    ed.config.freeform_mode = 1;       /* 默认自由模式 */
    ed.config.snap_to_grid = 0;
    ed.config.grid_size = 64;
    ed.config.show_labels = 1;
    ed.config.icon_size = 48;
    ed.config.label_size = 12;
    ed.config.dock_enabled = 1;
    ed.config.dock_position = 0;       /* 底部 */
    ed.config.long_press_edit = 1;
    ed.config.auto_align = 0;

    /* 创建默认页面 */
    ed.pages[0].used = 1;
    ed.pages[0].id = 0;
    strncpy(ed.pages[0].name, " master pages ", ED_NAME_LEN - 1);
    ed.pages[0].is_default = 1;
    ed.pages[0].bounds.x = 0; ed.pages[0].bounds.y = 0;
    ed.pages[0].bounds.width = 1080; ed.pages[0].bounds.height = 1920;
    ed.page_count = 1;

    /* 添加默认桌面项（自由摆放示例） */
    const char *apps[] = {" 电话 ", " SMS ", " camera ", " gallery ", " 浏览器 ", " set ",
        " 音乐 ", " file ", " app store ", " Hepu assistant "};
    int positions[10][2] = {
        {100, 200}, {250, 200}, {400, 200}, {550, 200}, {700, 200},
        {100, 350}, {250, 350}, {400, 350}, {550, 350}, {700, 350}
    };
    for (int i = 0; i < 10; i++) {
        ed_item_add(ED_ITEM_APP, apps[i], apps[i], 0,
                    positions[i][0], positions[i][1], 96, 96);
    }

    /* 添加时钟小部件 */
    ed_item_add(ED_ITEM_CLOCK, " clock ", "clock", 0, 100, 50, 400, 120);

    /* V0.10.5: BIOS环境下vga输出可能卡住，改为串口输出 */
    extern void serial_puts(uint16_t port, const char *str);
    serial_puts(0x3F8, "[DESKTOP] elastic free desktop init done\n");
    serial_puts(0x3F8, " free mode: ");
    serial_puts(0x3F8, ed.config.freeform_mode ? "on" : "off");
    serial_puts(0x3F8, ", pages: ");
    extern void serial_puthex(uint16_t, uint32_t);
    serial_puthex(0x3F8, ed.page_count);
    serial_puts(0x3F8, ", items: ");
    serial_puthex(0x3F8, ed.item_count);
    serial_puts(0x3F8, "\n");
}

int ed_page_add(const char *name)
{
    for (int i = 0; i < ED_MAX_PAGES; i++) {
        if (!ed.pages[i].used) {
            memset(&ed.pages[i], 0, sizeof(ed_page_t));
            ed.pages[i].used = 1;
            ed.pages[i].id = i;
            if (name) strncpy(ed.pages[i].name, name, ED_NAME_LEN - 1);
            ed.pages[i].bounds.width = 1080;
            ed.pages[i].bounds.height = 1920;
            ed.page_count++;
            return i;
        }
    }
    return -1;
}

int ed_page_remove(int page_id)
{
    if (page_id <= 0 || page_id >= ED_MAX_PAGES || !ed.pages[page_id].used) return -1;
    if (ed.pages[page_id].is_default) return -1;  /* 不能删除默认页 */
    /* 移除该页所有项 */
    for (int i = 0; i < ED_MAX_ITEMS; i++) {
        if (ed.items[i].used && ed.items[i].page == page_id)
            ed.items[i].used = 0;
    }
    ed.pages[page_id].used = 0;
    ed.page_count--;
    if (ed.current_page == page_id) ed.current_page = 0;
    return 0;
}

int ed_page_switch(int page_id)
{
    if (page_id < 0 || page_id >= ED_MAX_PAGES || !ed.pages[page_id].used) return -1;
    ed.current_page = page_id;
    ed.total_page_switches++;
    return 0;
}

int ed_page_get_current(int *page_id)
{
    if (!page_id) return -1;
    *page_id = ed.current_page;
    return 0;
}

int ed_page_list(ed_page_t *pages, int max)
{
    if (!pages || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < ED_MAX_PAGES && count < max; i++) {
        if (ed.pages[i].used) memcpy(&pages[count++], &ed.pages[i], sizeof(ed_page_t));
    }
    return count;
}

int ed_page_get(int page_id, ed_page_t *page)
{
    if (page_id < 0 || page_id >= ED_MAX_PAGES || !ed.pages[page_id].used || !page) return -1;
    memcpy(page, &ed.pages[page_id], sizeof(ed_page_t));
    return 0;
}

int ed_page_reorder(int *page_ids, int count)
{
    (void)page_ids; (void)count;
    return 0;
}

int ed_item_add(ed_item_type_t type, const char *name, const char *app_id,
                int page, int x, int y, int w, int h)
{
    for (int i = 0; i < ED_MAX_ITEMS; i++) {
        if (!ed.items[i].used) {
            memset(&ed.items[i], 0, sizeof(ed_item_t));
            ed.items[i].used = 1;
            ed.items[i].id = i;
            ed.items[i].type = type;
            ed.items[i].state = ED_STATE_NORMAL;
            if (name) strncpy(ed.items[i].name, name, ED_NAME_LEN - 1);
            if (app_id) strncpy(ed.items[i].app_id, app_id, ED_APP_ID_LEN - 1);
            ed.items[i].rect.x = x;
            ed.items[i].rect.y = y;
            ed.items[i].rect.width = w;
            ed.items[i].rect.height = h;
            ed.items[i].min_rect.width = 48;
            ed.items[i].min_rect.height = 48;
            ed.items[i].max_rect.width = 800;
            ed.items[i].max_rect.height = 800;
            ed.items[i].page = page;
            ed.items[i].z_order = ed.item_count;
            ed.items[i].visible = 1;
            ed.items[i].draggable = 1;
            ed.items[i].resizable = (type == ED_ITEM_WIDGET || type == ED_ITEM_FOLDER);
            ed.items[i].clickable = 1;
            ed.item_count++;
            if (page >= 0 && page < ED_MAX_PAGES && ed.pages[page].used)
                ed.pages[page].item_count++;
            return i;
        }
    }
    return -1;
}

int ed_item_remove(int item_id)
{
    if (item_id < 0 || item_id >= ED_MAX_ITEMS || !ed.items[item_id].used) return -1;
    int page = ed.items[item_id].page;
    ed.items[item_id].used = 0;
    ed.item_count--;
    if (page >= 0 && page < ED_MAX_PAGES && ed.pages[page].used)
        ed.pages[page].item_count--;
    return 0;
}

int ed_item_get(int item_id, ed_item_t *item)
{
    if (item_id < 0 || item_id >= ED_MAX_ITEMS || !ed.items[item_id].used || !item) return -1;
    memcpy(item, &ed.items[item_id], sizeof(ed_item_t));
    return 0;
}

int ed_item_list(int page, ed_item_t *items, int max)
{
    if (!items || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < ED_MAX_ITEMS && count < max; i++) {
        if (ed.items[i].used && ed.items[i].page == page && ed.items[i].visible)
            memcpy(&items[count++], &ed.items[i], sizeof(ed_item_t));
    }
    return count;
}

int ed_item_list_all(ed_item_t *items, int max)
{
    if (!items || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < ED_MAX_ITEMS && count < max; i++) {
        if (ed.items[i].used) memcpy(&items[count++], &ed.items[i], sizeof(ed_item_t));
    }
    return count;
}

int ed_item_move(int item_id, int x, int y)
{
    if (item_id < 0 || item_id >= ED_MAX_ITEMS || !ed.items[item_id].used) return -1;
    ed.items[item_id].rect.x = x;
    ed.items[item_id].rect.y = y;
    return 0;
}

int ed_item_resize(int item_id, int w, int h)
{
    if (item_id < 0 || item_id >= ED_MAX_ITEMS || !ed.items[item_id].used) return -1;
    if (w < ed.items[item_id].min_rect.width) w = ed.items[item_id].min_rect.width;
    if (h < ed.items[item_id].min_rect.height) h = ed.items[item_id].min_rect.height;
    if (w > ed.items[item_id].max_rect.width) w = ed.items[item_id].max_rect.width;
    if (h > ed.items[item_id].max_rect.height) h = ed.items[item_id].max_rect.height;
    ed.items[item_id].rect.width = w;
    ed.items[item_id].rect.height = h;
    return 0;
}

int ed_item_set_position(int item_id, int x, int y, int w, int h)
{
    ed_item_move(item_id, x, y);
    ed_item_resize(item_id, w, h);
    return 0;
}

int ed_item_bring_to_front(int item_id)
{
    if (item_id < 0 || item_id >= ED_MAX_ITEMS || !ed.items[item_id].used) return -1;
    int max_z = 0;
    for (int i = 0; i < ED_MAX_ITEMS; i++) {
        if (ed.items[i].used && ed.items[i].z_order > max_z)
            max_z = ed.items[i].z_order;
    }
    ed.items[item_id].z_order = max_z + 1;
    return 0;
}

int ed_item_send_to_back(int item_id)
{
    if (item_id < 0 || item_id >= ED_MAX_ITEMS || !ed.items[item_id].used) return -1;
    ed.items[item_id].z_order = 0;
    return 0;
}

int ed_item_set_zorder(int item_id, int z)
{
    if (item_id < 0 || item_id >= ED_MAX_ITEMS || !ed.items[item_id].used) return -1;
    ed.items[item_id].z_order = z;
    return 0;
}

int ed_drag_start(int item_id, int x, int y)
{
    if (item_id < 0 || item_id >= ED_MAX_ITEMS || !ed.items[item_id].used) return -1;
    if (!ed.items[item_id].draggable) return -1;
    ed.dragging_item = item_id;
    ed.drag_start_x = x;
    ed.drag_start_y = y;
    ed.items[item_id].state = ED_STATE_DRAGGING;
    ed_item_bring_to_front(item_id);
    return 0;
}

int ed_drag_move(int x, int y)
{
    if (ed.dragging_item < 0) return -1;
    int dx = x - ed.drag_start_x;
    int dy = y - ed.drag_start_y;
    ed.items[ed.dragging_item].rect.x += dx;
    ed.items[ed.dragging_item].rect.y += dy;
    ed.drag_start_x = x;
    ed.drag_start_y = y;
    /* 网格吸附 */
    if (ed.config.snap_to_grid && ed.config.grid_size > 0) {
        ed.items[ed.dragging_item].rect.x =
            (ed.items[ed.dragging_item].rect.x / ed.config.grid_size) * ed.config.grid_size;
        ed.items[ed.dragging_item].rect.y =
            (ed.items[ed.dragging_item].rect.y / ed.config.grid_size) * ed.config.grid_size;
    }
    return 0;
}

int ed_drag_end(int x, int y)
{
    if (ed.dragging_item < 0) return -1;
    ed_drag_move(x, y);
    ed.items[ed.dragging_item].state = ED_STATE_NORMAL;
    ed.dragging_item = -1;
    ed.total_edits++;
    return 0;
}

int ed_resize_start(int item_id, int x, int y)
{
    if (item_id < 0 || item_id >= ED_MAX_ITEMS || !ed.items[item_id].used) return -1;
    if (!ed.items[item_id].resizable) return -1;
    ed.resizing_item = item_id;
    ed.resize_start_w = ed.items[item_id].rect.width;
    ed.resize_start_h = ed.items[item_id].rect.height;
    ed.drag_start_x = x;
    ed.drag_start_y = y;
    ed.items[item_id].state = ED_STATE_RESIZING;
    return 0;
}

int ed_resize_move(int x, int y)
{
    if (ed.resizing_item < 0) return -1;
    int dw = x - ed.drag_start_x;
    int dh = y - ed.drag_start_y;
    ed_item_resize(ed.resizing_item, ed.resize_start_w + dw, ed.resize_start_h + dh);
    return 0;
}

int ed_resize_end(int x, int y)
{
    if (ed.resizing_item < 0) return -1;
    ed_resize_move(x, y);
    ed.items[ed.resizing_item].state = ED_STATE_NORMAL;
    ed.resizing_item = -1;
    ed.total_edits++;
    return 0;
}

int ed_folder_create(const char *name, int page, int x, int y)
{
    return ed_item_add(ED_ITEM_FOLDER, name, "folder", page, x, y, 96, 96);
}

int ed_folder_add_item(int folder_id, int item_id)
{
    if (folder_id < 0 || folder_id >= ED_MAX_ITEMS || !ed.items[folder_id].used) return -1;
    if (ed.items[folder_id].type != ED_ITEM_FOLDER) return -1;
    if (ed.items[folder_id].folder_item_count >= 16) return -1;
    ed.items[folder_id].folder_items[ed.items[folder_id].folder_item_count++] = item_id;
    return 0;
}

int ed_folder_remove_item(int folder_id, int item_id)
{
    if (folder_id < 0 || folder_id >= ED_MAX_ITEMS || !ed.items[folder_id].used) return -1;
    for (int i = 0; i < ed.items[folder_id].folder_item_count; i++) {
        if (ed.items[folder_id].folder_items[i] == item_id) {
            for (int j = i; j < ed.items[folder_id].folder_item_count - 1; j++)
                ed.items[folder_id].folder_items[j] = ed.items[folder_id].folder_items[j + 1];
            ed.items[folder_id].folder_item_count--;
            return 0;
        }
    }
    return -1;
}

int ed_folder_get_items(int folder_id, int *item_ids, int max)
{
    if (folder_id < 0 || folder_id >= ED_MAX_ITEMS || !ed.items[folder_id].used || !item_ids) return -1;
    int count = ed.items[folder_id].folder_item_count < max ?
                ed.items[folder_id].folder_item_count : max;
    for (int i = 0; i < count; i++) item_ids[i] = ed.items[folder_id].folder_items[i];
    return count;
}

int ed_set_config(const ed_config_t *config)
{
    if (!config) return -1;
    memcpy(&ed.config, config, sizeof(ed_config_t));
    return 0;
}

int ed_get_config(ed_config_t *config)
{
    if (!config) return -1;
    memcpy(config, &ed.config, sizeof(ed_config_t));
    return 0;
}

int ed_set_freeform(int enable)
{
    ed.config.freeform_mode = enable ? 1 : 0;
    return 0;
}

int ed_set_grid_size(int size)
{
    ed.config.grid_size = size > 0 ? size : 64;
    return 0;
}

int ed_toggle_edit_mode(void)
{
    ed.edit_mode = !ed.edit_mode;
    return ed.edit_mode;
}

int ed_auto_arrange(int page)
{
    if (page < 0 || page >= ED_MAX_PAGES || !ed.pages[page].used) return -1;
    int x = 100, y = 200, col = 0;
    for (int i = 0; i < ED_MAX_ITEMS; i++) {
        if (ed.items[i].used && ed.items[i].page == page && ed.items[i].type == ED_ITEM_APP) {
            ed.items[i].rect.x = x;
            ed.items[i].rect.y = y;
            col++;
            if (col >= 4) { col = 0; x = 100; y += 150; }
            else x += 150;
        }
    }
    return 0;
}

int ed_reset_layout(int page)
{
    return ed_auto_arrange(page);
}

int ed_get_item_at(int x, int y, int *item_id)
{
    if (!item_id) return -1;
    /* 从上层到下层检测 */
    for (int z = 1000; z >= 0; z--) {
        for (int i = 0; i < ED_MAX_ITEMS; i++) {
            if (ed.items[i].used && ed.items[i].visible &&
                ed.items[i].page == ed.current_page &&
                ed.items[i].z_order == z) {
                ed_rect_t *r = &ed.items[i].rect;
                if (x >= r->x && x <= r->x + r->width &&
                    y >= r->y && y <= r->y + r->height) {
                    *item_id = i;
                    return 0;
                }
            }
        }
    }
    return -1;
}

int ed_check_overlap(int item_id, ed_rect_t *rect, int *overlap_id)
{
    if (!rect || !overlap_id) return -1;
    for (int i = 0; i < ED_MAX_ITEMS; i++) {
        if (ed.items[i].used && i != item_id && ed.items[i].page == ed.items[item_id].page) {
            ed_rect_t *r = &ed.items[i].rect;
            if (rect->x < r->x + r->width && rect->x + rect->width > r->x &&
                rect->y < r->y + r->height && rect->y + rect->height > r->y) {
                *overlap_id = i;
                return 1;  /* 有重叠 */
            }
        }
    }
    return 0;  /* 无重叠 */
}

int ed_get_stats(elastic_desktop_t *stats)
{
    if (!stats) return -1;
    memcpy(stats, &ed, sizeof(elastic_desktop_t));
    return 0;
}

int ed_record_launch(int item_id)
{
    if (item_id < 0 || item_id >= ED_MAX_ITEMS || !ed.items[item_id].used) return -1;
    ed.total_launches++;
    return 0;
}
