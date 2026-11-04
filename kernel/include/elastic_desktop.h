#ifndef ELASTIC_DESKTOP_H
#define ELASTIC_DESKTOP_H

#include <stdint.h>

/* ============================================================================
 * 赫普OS 弹性自由桌面
 * 摒弃固定网格，组件自由摆放、自由缩放、多页面
 * ========================================================================== */

#define ED_MAX_PAGES 10
#define ED_MAX_ITEMS 128
#define ED_NAME_LEN 64
#define ED_APP_ID_LEN 128

/* 桌面项类型 */
typedef enum {
    ED_ITEM_APP = 0,       /* 应用图标 */
    ED_ITEM_WIDGET,        /* 小部件 */
    ED_ITEM_FOLDER,        /* 文件夹 */
    ED_ITEM_SHORTCUT,      /* 快捷方式 */
    ED_ITEM_CLOCK,         /* 时钟小部件 */
    ED_ITEM_WEATHER,       /* 天气小部件 */
    ED_ITEM_CALENDAR,      /* 日历小部件 */
    ED_ITEM_MUSIC,         /* 音乐控制小部件 */
    ED_ITEM_SEARCH,        /* 搜索栏 */
    ED_ITEM_DOCK,          /*  Dock栏 */
} ed_item_type_t;

/* 桌面项状态 */
typedef enum {
    ED_STATE_NORMAL = 0,
    ED_STATE_DRAGGING,
    ED_STATE_RESIZING,
    ED_STATE_EDITING,
    ED_STATE_HIDDEN,
} ed_item_state_t;

/* 矩形区域 */
typedef struct {
    int x;
    int y;
    int width;
    int height;
} ed_rect_t;

/* 桌面项 */
typedef struct {
    int used;
    int id;
    ed_item_type_t type;
    ed_item_state_t state;
    char name[ED_NAME_LEN];
    char app_id[ED_APP_ID_LEN];
    ed_rect_t rect;         /* 自由位置和大小（像素） */
    ed_rect_t min_rect;     /* 最小尺寸 */
    ed_rect_t max_rect;     /* 最大尺寸 */
    int page;               /* 所属页面 */
    int z_order;            /* 层级 */
    int visible;
    int icon_resource;      /* 图标资源ID */
    int badge_count;        /* 角标数字 */
    int folder_items[16];   /* 文件夹内的项目ID */
    int folder_item_count;
    /* 交互 */
    int draggable;
    int resizable;
    int clickable;
    /* 小部件数据 */
    int widget_update_interval_s;
    uint64_t last_update;
    char widget_data[256];
} ed_item_t;

/* 桌面页面 */
typedef struct {
    int used;
    int id;
    char name[ED_NAME_LEN];
    ed_rect_t bounds;       /* 页面边界 */
    int item_count;
    int item_ids[ED_MAX_ITEMS];
    int is_default;
    int wallpaper_id;
} ed_page_t;

/* 桌面配置 */
typedef struct {
    int enabled;
    int freeform_mode;          /* 自由模式（非网格） */
    int snap_to_grid;           /* 吸附网格 */
    int grid_size;              /* 网格大小（像素） */
    int show_labels;            /* 显示图标文字 */
    int icon_size;              /* 图标大小 */
    int label_size;             /* 文字大小 */
    int dock_enabled;           /* Dock栏 */
    int dock_position;          /* 0=底部,1=左侧,2=右侧 */
    int page_transition;        /* 页面切换动画 */
    int long_press_edit;        /* 长按进入编辑模式 */
    int auto_align;             /* 自动对齐 */
    int wallpaper_blur;         /* 壁纸模糊 */
} ed_config_t;

/* 桌面管理器状态 */
typedef struct {
    int initialized;
    int enabled;
    int edit_mode;
    int current_page;
    ed_config_t config;
    /* 页面 */
    ed_page_t pages[ED_MAX_PAGES];
    int page_count;
    /* 桌面项 */
    ed_item_t items[ED_MAX_ITEMS];
    int item_count;
    /* 交互状态 */
    int dragging_item;
    int resizing_item;
    int drag_start_x, drag_start_y;
    int resize_start_w, resize_start_h;
    /* 统计 */
    uint64_t total_launches;
    uint64_t total_edits;
    uint64_t total_page_switches;
} elastic_desktop_t;

/* 初始化 */
void elastic_desktop_init(void);

/* 页面管理 */
int ed_page_add(const char *name);
int ed_page_remove(int page_id);
int ed_page_switch(int page_id);
int ed_page_get_current(int *page_id);
int ed_page_list(ed_page_t *pages, int max);
int ed_page_get(int page_id, ed_page_t *page);
int ed_page_reorder(int *page_ids, int count);

/* 桌面项管理 */
int ed_item_add(ed_item_type_t type, const char *name, const char *app_id,
                int page, int x, int y, int w, int h);
int ed_item_remove(int item_id);
int ed_item_get(int item_id, ed_item_t *item);
int ed_item_list(int page, ed_item_t *items, int max);
int ed_item_list_all(ed_item_t *items, int max);

/* 自由摆放/缩放 */
int ed_item_move(int item_id, int x, int y);
int ed_item_resize(int item_id, int w, int h);
int ed_item_set_position(int item_id, int x, int y, int w, int h);
int ed_item_bring_to_front(int item_id);
int ed_item_send_to_back(int item_id);
int ed_item_set_zorder(int item_id, int z);

/* 拖拽/缩放交互 */
int ed_drag_start(int item_id, int x, int y);
int ed_drag_move(int x, int y);
int ed_drag_end(int x, int y);
int ed_resize_start(int item_id, int x, int y);
int ed_resize_move(int x, int y);
int ed_resize_end(int x, int y);

/* 文件夹 */
int ed_folder_create(const char *name, int page, int x, int y);
int ed_folder_add_item(int folder_id, int item_id);
int ed_folder_remove_item(int folder_id, int item_id);
int ed_folder_get_items(int folder_id, int *item_ids, int max);

/* 配置 */
int ed_set_config(const ed_config_t *config);
int ed_get_config(ed_config_t *config);
int ed_set_freeform(int enable);
int ed_set_grid_size(int size);
int ed_toggle_edit_mode(void);

/* 布局 */
int ed_auto_arrange(int page);
int ed_reset_layout(int page);
int ed_get_item_at(int x, int y, int *item_id);
int ed_check_overlap(int item_id, ed_rect_t *rect, int *overlap_id);

/* 统计 */
int ed_get_stats(elastic_desktop_t *stats);
int ed_record_launch(int item_id);

/* V6.80: 桌面渲染器 (desktop_render.c) */
void elastic_desktop_render(void);
void desktop_render_test(void);
void elastic_desktop_main_loop(void);
void desktop_set_mouse(int x, int y);
void desktop_get_mouse(int *x, int *y);
void desktop_show_mouse(int show);

#endif /* ELASTIC_DESKTOP_H */
