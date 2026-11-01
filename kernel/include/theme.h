#ifndef THEME_H
#define THEME_H

#include <stdint.h>

/*
 * 赫普OS 主题体系 (V2.35)
 * 全部 UI 颜色/尺寸收口到单一主题结构, 一改全改。
 * 默认主题: "Hepu Blue"。
 */

typedef struct {
    /* 颜色 (VGA 256 色调色板索引) */
    uint8_t desktop_bg;       /* 桌面背景 */
    uint8_t wallpaper_top;    /* 壁纸渐变顶部 */
    uint8_t wallpaper_bot;    /* 壁纸渐变底部 */
    uint8_t title_active;     /* 活动窗口标题栏 */
    uint8_t title_inactive;   /* 非活动窗口标题栏 */
    uint8_t title_text;       /* 标题文字 */
    uint8_t window_bg;        /* 窗口/页面背景 */
    uint8_t border;           /* 边框/分隔 */
    uint8_t button;           /* 按钮常态面 */
    uint8_t button_press;     /* 按钮按下 */
    uint8_t button_disabled;  /* 按钮禁用 */
    uint8_t button_text;      /* 按钮文字 */
    uint8_t label_text;       /* 标签文字 */
    uint8_t input_bg;         /* 输入框背景 */
    uint8_t input_text;       /* 输入框文字 */
    uint8_t focus;            /* 焦点高亮 */
    uint8_t select;           /* 列表选中 */
    uint8_t select_text;      /* 选中文字 */
    uint8_t taskbar;          /* 任务栏 */
    uint8_t taskbar_active;   /* 任务栏活动窗口按钮 */
    uint8_t warn;             /* 警告/错误 */
    uint8_t ok;               /* 成功 */
    uint8_t close_btn;        /* 关闭按钮底色 */
    uint8_t min_btn;          /* 最小化按钮底色 */

    /* 尺寸 (px) */
    int title_h;              /* 标题栏高 */
    int border_w;             /* 边框宽 */
    int btn_h;                /* 按钮高 */
    int input_h;              /* 输入框高 */
    int taskbar_h;            /* 任务栏高 */
    int icon_w;               /* 桌面图标宽 */
    int icon_h;               /* 桌面图标高 */
    int icon_gap;             /* 图标间距 */
} hepu_theme_t;

/* 全局唯一主题实例 */
extern hepu_theme_t hepu_theme;

/* 主题枚举 */
typedef enum {
    THEME_HEPU_BLUE = 0,
    THEME_DARK,
    THEME_OLED_BLACK,
    THEME_TRON,
    THEME_COUNT
} hepu_theme_id_t;

/* 初始化默认主题 "Hepu Blue" */
void theme_init(void);

/* 切换到指定主题 (返回主题名) */
const char *theme_apply(hepu_theme_id_t id);

/* 获取当前主题 ID */
hepu_theme_id_t theme_current(void);

#endif /* THEME_H */

/* WM 颜色宏 (从 wm.c 移来，供 wm_draw.c 等使用) */
#define WM_COLOR_DESKTOP   (hepu_theme.desktop_bg)
#define WM_COLOR_TITLEBAR  (hepu_theme.title_inactive)
#define WM_COLOR_TITLE_TX  (hepu_theme.title_text)
#define WM_COLOR_WINDOW_BG (hepu_theme.window_bg)
#define WM_COLOR_BORDER    (hepu_theme.border)
#define WM_COLOR_BUTTON    (hepu_theme.button)
#define WM_COLOR_BUTTON_P  (hepu_theme.button_press)
#define WM_COLOR_BUTTON_TX (hepu_theme.button_text)
#define WM_COLOR_LABEL     (hepu_theme.label_text)
#define WM_COLOR_INPUT_BG  (hepu_theme.input_bg)
#define WM_COLOR_INPUT_TX  (hepu_theme.input_text)
#define WM_COLOR_TASKBAR   (hepu_theme.taskbar)
