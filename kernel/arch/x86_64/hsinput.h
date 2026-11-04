/* ============================================================================
 * 赫普OS - 统一输入抽象层 (V0.13.5 P2)
 * 参考 Linux input 子系统思路: 键盘/鼠标/滚轮/触摸 归一化为统一事件源
 * 桌面层只依赖本层, 不关心底层是 USB 还是 PS/2
 * ========================================================================== */
#ifndef HSINPUT_H
#define HSINPUT_H

/* 统一事件类型 */
#define HS_INPUT_EVT_KEY        1   /* key=键值(ASCII/扫描码) */
#define HS_INPUT_EVT_MOUSE_MOVE 2   /* x,y=坐标 */
#define HS_INPUT_EVT_MOUSE_DOWN 3   /* button=1左 2右 4中 */
#define HS_INPUT_EVT_MOUSE_UP   4
#define HS_INPUT_EVT_WHEEL      5   /* wheel=增量 */
#define HS_INPUT_EVT_TOUCH      6   /* 触摸/多点 (预留) */

typedef struct {
    int type;
    int x, y;      /* 鼠标/触摸坐标 */
    int key;       /* 键值 */
    int button;    /* 鼠标按钮位 */
    int wheel;     /* 滚轮增量 */
    int touch_id;  /* 触摸点ID */
} hs_input_event_t;

/* 初始化 (屏幕尺寸用于坐标钳位) */
void hs_input_init(int screen_width, int screen_height);

/* 轮询所有输入源并分发到桌面 (USB 优先, PS/2 兜底) */
void hs_input_poll(void);

/* 滚轮事件注入 (驱动/未来触摸手势使用) */
void hs_input_wheel(int delta);

/* 获取当前修饰键状态 (bit0=LCtrl bit1=LShift bit2=LAlt...) */
int hs_input_get_mod(void);

/* V0.13.8 P5: PS/2 扩展键内部码 (0xE0 前缀键, 与 ASCII 不冲突) */
#define KEY_EXT_UP      0x90
#define KEY_EXT_DOWN    0x91
#define KEY_EXT_LEFT    0x92
#define KEY_EXT_RIGHT   0x93
#define KEY_EXT_PGUP    0x94
#define KEY_EXT_PGDN    0x95
#define KEY_EXT_HOME    0x96
#define KEY_EXT_END     0x97
#define KEY_EXT_DEL     0x98
#define KEY_EXT_INS     0x99

void hs_touch_report(int type, int id, int x, int y);  /* V0.13.8 P5 */
void hs_touch_reset_all(void);

#endif /* HSINPUT_H */
