/* ============================================================================
 * 赫普OS - 统一输入抽象层实现 (V0.13.5 P2)
 * 归一化 USB/PS2 输入: 键盘事件 + 鼠标位置/按钮 + 滚轮 → 桌面回调
 * ========================================================================== */
#include "hsinput.h"
#include "hstouch.h"
#include "hpdesk.h"

/* 屏幕尺寸 */
static int g_sw = 1024;
static int g_sh = 768;
static int g_wheel_acc = 0;
static int last_left = 0;
static int last_right = 0;

/* ---- 外部依赖 (USB/PS2 驱动与桌面回调) ---- */
extern void usb64_poll(void);
extern int usb64_kbd_available(void);
extern int usb64_kbd_haschar(void);
extern int usb64_kbd_getchar(void);
extern int usb64_kbd_get_mod(void);
extern int usb64_mouse_available(void);
extern int usb64_tablet_available(void);
extern void usb64_tablet_get(int *x, int *y, int *left, int *right);
extern void usb64_mouse_get(int *x, int *y, int *left, int *right);
extern int keyboard64_haschar(void);
extern int keyboard64_getchar(void);
extern int keyboard64_get_mod(void);
extern void mouse64_update(int w, int h);
extern void mouse64_get_pos(int *x, int *y);
extern int mouse64_get_left(void);
extern int mouse64_get_right(void);
extern void shell64_run(void);
/* P3: 窗口/工作区交互钩子 */
extern void hpdesk_switch_workspace(int ws);
extern void hpdesk_cycle_window_focus(void);
extern void hpdesk_close_focused_window(void);

/* ============================================================================
 * P3 统一组合快捷键引擎 (参考 Windows Win+ / macOS Cmd+ 习惯)
 * 组合键优先于单键处理, 消费后不再下发 hpdesk_handle_key
 * mod: bit0=Ctrl bit1=Shift bit2=Alt
 * ========================================================================== */
int hs_handle_shortcut(int key, int mod)
{
    int ctrl = mod & 0x01, shift = mod & 0x02, alt = mod & 0x04;

    /* P17.2: 窗口切换器激活时 Esc 关闭 (拦截 shell 入口) */
    extern int hpdesk_switcher_active(void);
    extern void hpdesk_switcher_close(void);
    if (hpdesk_switcher_active() && key == 27) {
        hpdesk_switcher_close();
        return 1;
    }
    /* P17.2: Alt+Tab 调出/循环窗口切换器 (Windows 惯例) */
    if (alt && !ctrl && !shift && (key == '\t' || key == 0x0F)) {
        extern void hpdesk_switcher_next(void);
        hpdesk_switcher_next();
        return 1;
    }

    /* Ctrl+Alt+Delete: 锁屏 (参考 Windows 安全键) */
    if (ctrl && alt && key == 0x53) {  /* 0x53=Delete usage/扫描码 */
        extern void hpdesk_lock(void);
        hpdesk_lock();
        return 1;
    }
    /* Ctrl+Alt+方向: 工作区切换 (参考 Linux 桌面) */
    if (ctrl && alt) {
        if (key == 0xE3) { hpdesk_switch_workspace_delta(1); return 1; }   /* 上=下一个 */
        if (key == 0xE2) { hpdesk_switch_workspace_delta(-1); return 1; }  /* 下=上一个 */
    }
    /* Ctrl+Tab / Ctrl+Shift+Tab: 工作区循环 (参考浏览器标签) */
    if (ctrl && !alt && (key == '\t' || key == 0x0F)) {
        hpdesk_switch_workspace_delta(shift ? -1 : 1);
        return 1;
    }
    /* Alt+Tab / Alt+Shift+Tab: 窗口焦点循环 (参考 Windows) */
    if (alt && !ctrl) {
        if (key == '\t' || key == 0x0F) {
            hpdesk_cycle_window_focus();
            return 1;
        }
    }
    /* Ctrl+W: 关闭焦点窗口 */
    if (ctrl && !alt && (key == 'w' || key == 'W')) {
        hpdesk_close_focused_window();
        return 1;
    }
    /* Ctrl+S: 编辑器保存 (V0.54.7) */
    if (ctrl && !alt && (key == 's' || key == 'S')) {
        extern int hs_apps_editor_save(void);
        if (hs_apps_editor_save()) return 1;
    }
    /* Ctrl+L: 锁屏; Ctrl+T: 主题 (与单键一致但组合触发) */
    if (ctrl && !alt) {
        if (key == 'l' || key == 'L') { extern void hpdesk_lock(void); hpdesk_lock(); return 1; }
        if (key == 't' || key == 'T') { extern void hpdesk_toggle_theme(void); hpdesk_toggle_theme(); return 1; }
        /* Ctrl+E: 打开代码编辑器 (V0.54.7) */
        if (key == 'e' || key == 'E') { extern int g_pending_app; g_pending_app = 5; return 1; }
        /* Ctrl+1..9: 工作区直达 */
        if (key >= '1' && key <= '9') { hpdesk_switch_workspace(key - '1'); return 1; }
    }
    /* V0.13.9: Win/Super 键快捷键 (参考 Windows Super 键习惯; mod bit3=Win) */
    if (mod & 0x08) {
        /* Win+E: 打开文件管理器 */
        if (key == 'e' || key == 'E') {
            extern int g_pending_app;
            g_pending_app = 2;  /* HS_APP_FILES */
            return 1;
        }
        /* Win+T: 打开终端 (参考 Windows 终端习惯) */
        if (key == 't' || key == 'T') {
            extern int g_pending_app;
            g_pending_app = 1;  /* HS_APP_TERMINAL */
            return 1;
        }
        /* Win+C: 打开代码编辑器 (V0.54.7) */
        if (key == 'c' || key == 'C') {
            extern int g_pending_app;
            g_pending_app = 5;  /* HS_APP_EDITOR */
            return 1;
        }
        /* Win+M: 打开任务管理器 */
        if (key == 'm' || key == 'M') {
            extern int g_pending_app;
            g_pending_app = 9;  /* HS_APP_TASK */
            return 1;
        }
        /* Win+D: 显示桌面 (最小化所有窗口) */
        if (key == 'd' || key == 'D') {
            extern void hpdesk_show_desktop(void);
            hpdesk_show_desktop();
            return 1;
        }
        /* Win+L: 锁屏 */
        if (key == 'l' || key == 'L') {
            extern void hpdesk_lock(void);
            hpdesk_lock();
            return 1;
        }
        /* Win+R: 打开启动器/运行 */
        if (key == 'r' || key == 'R') {
            extern void hpdesk_toggle_launcher(void);
            hpdesk_toggle_launcher();
            return 1;
        }
        /* V0.13.16: Win+←/→ 单窗口平铺(参考Windows Snap) */
        if (key == KEY_EXT_LEFT) {
            extern void hpdesk_tile_focused(int dir);
            hpdesk_tile_focused(-1);
            return 1;
        }
        if (key == KEY_EXT_RIGHT) {
            extern void hpdesk_tile_focused(int dir);
            hpdesk_tile_focused(1);
            return 1;
        }
    }
    return 0;  /* 未消费, 走普通键处理 */
}

void hs_input_init(int screen_width, int screen_height)
{
    g_sw = screen_width;
    g_sh = screen_height;
}

void hs_input_wheel(int delta)
{
    g_wheel_acc += delta;
}

int hs_input_get_mod(void)
{
    if (usb64_kbd_available()) return usb64_kbd_get_mod();
    return 0;
}

/* V0.13.8 P5: 0xE0 扩展键前缀 → 内部码归一化 (USB HID 与 PS/2 统一) */
static int g_hs_pending_e0 = 0;
static int hs_normalize_ext(int key);
static int hs_normalize_ext(int key)
{
    if (key == 0xE0) { g_hs_pending_e0 = 1; return 0; }
    if (g_hs_pending_e0) {
        g_hs_pending_e0 = 0;
        switch (key) {
            case 0x48: return KEY_EXT_UP;
            case 0x50: return KEY_EXT_DOWN;
            case 0x4B: return KEY_EXT_LEFT;
            case 0x4D: return KEY_EXT_RIGHT;
            case 0x49: return KEY_EXT_PGUP;
            case 0x51: return KEY_EXT_PGDN;
            case 0x47: return KEY_EXT_HOME;
            case 0x4F: return KEY_EXT_END;
            case 0x53: return KEY_EXT_DEL;
            case 0x52: return KEY_EXT_INS;
            default:   return 0;
        }
    }
    return key;
}

/* 主轮询: 一次收集全部输入并分发 */
void hs_input_poll(void)
{
    usb64_poll();
    mouse64_update(g_sw, g_sh);

    /* V0.13.8 P5: 统一扩展键前缀处理 (USB/PS2 双路径, 0xE0 归一化) */
    if (usb64_kbd_available() && usb64_kbd_haschar()) {
        int key = usb64_kbd_getchar();
        int mod = usb64_kbd_get_mod();
        key = hs_normalize_ext(key);
        if (key != 0) {  /* V0.13.8 P5: 0=扩展键前缀已消费 */
            if (hs_handle_shortcut(key, mod)) return;
            if (key == 27) {
                shell64_run();
                return;
            }
            hpdesk_handle_key(key);
        }
    } else if (keyboard64_haschar()) {
        int key = keyboard64_getchar();
        int mod = keyboard64_get_mod();
        key = hs_normalize_ext(key);
        if (key != 0) {
            if (hs_handle_shortcut(key, mod)) return;
            if (key == 27) {
                shell64_run();
                return;
            }
            hpdesk_handle_key(key);
        }
    }

    /* 鼠标位置 (V0.13.8 P6: tablet 绝对定位优先, USB 相对次之, PS/2 兜底) */
    int mx = 0, my = 0;
    /* V0.13.66: XHCI (USB3.0) 鼠标兜底 (现代主板无 UHCI/EHCI 时) */
    extern int xhci64_present(void);
    extern int xhci64_mouse_available(void);
    extern void xhci64_mouse_get(int *x, int *y, int *left, int *right);
    if (usb64_tablet_available()) {
        usb64_tablet_get(&mx, &my, 0, 0);
    } else if (usb64_mouse_available()) {
        usb64_mouse_get(&mx, &my, 0, 0);
    } else if (xhci64_present() && xhci64_mouse_available()) {
        /* V0.54.12: xhci64_mouse_get 返回的是每帧**增量**(dx/dy), 不是绝对坐标;
         * 旧实现把它当绝对坐标直接赋给 g_shell.mouse_x/y, 导致多鼠标源并存时
         * 渲染坐标恒为 (0,0)、光标画到屏幕左上角外而"消失"。此处自行累加并钳制;
         * 初始位置取屏幕中心, 避免从 (0,0) 起算。 */
        static int xhci_cx = -1, xhci_cy = -1;
        if (xhci_cx < 0) { xhci_cx = g_sw / 2; xhci_cy = g_sh / 2; }
        int dx = 0, dy = 0;
        xhci64_mouse_get(&dx, &dy, 0, 0);
        if (dx || dy) {
            xhci_cx += dx; xhci_cy += dy;
        }
        /* V0.57.0: USB 鼠标在位时**不再**用 PS/2 坐标兜底。
         * 旧实现在 USB 本帧无增量时跟随 PS/2 的绝对坐标 —— 而 QEMU 的
         * mouse_move / input-send-event 会把事件同时广播给 PS/2 和 USB,
         * PS/2 侧坐标与 USB 累加坐标各自演进, 于是每按一次键/点一次鼠标,
         * 光标就被 PS/2 的旧坐标拽回去(实测点击时坐标从 (90,75) 跳到 (27,7)),
         * 点击自然全部落空 —— 这正是"鼠标看着能动但点不中"的根因。 */
        if (xhci_cx < 0) xhci_cx = 0;
        if (xhci_cy < 0) xhci_cy = 0;
        if (xhci_cx > g_sw - 1) xhci_cx = g_sw - 1;
        if (xhci_cy > g_sh - 1) xhci_cy = g_sh - 1;
        mx = xhci_cx; my = xhci_cy;
    } else {
        mouse64_get_pos(&mx, &my);
    }
    hpdesk_update_mouse(mx, my);

    /* 左键 (下降沿按下 / 上升沿释放+点击)
     * V0.13.41: 多源按钮合流 — tablet/usb/PS2 任一按下都生效,
     * 修复 tablet 在位时 PS/2 鼠标按钮被完全忽略(monitor/物理PS2点击无效) */
    int left = 0, right = 0;
    if (usb64_tablet_available()) {
        int tl = 0, tr = 0;
        usb64_tablet_get(0, 0, &tl, &tr);
        left |= tl; right |= tr;
    }
    if (usb64_mouse_available()) {
        int ml = 0, mr = 0;
        usb64_mouse_get(0, 0, &ml, &mr);
        left |= ml; right |= mr;
    } else if (xhci64_present() && xhci64_mouse_available()) {
        int ml = 0, mr = 0;
        xhci64_mouse_get(0, 0, &ml, &mr);
        left |= ml; right |= mr;
    }
    left |= mouse64_get_left();
    right |= mouse64_get_right();
    /* 快速点击兜底: 按下沿与释放可能落在同一次轮询批次内, 由驱动锁存 */
    extern int mouse64_take_click(void);
    extern int usb64_tablet_take_click(void);
    int fast_click = mouse64_take_click() | usb64_tablet_take_click();
    if (left && !last_left) hpdesk_mouse_down(mx, my);
    if (!left && last_left) {
        hpdesk_mouse_up(mx, my);
        hpdesk_handle_click(mx, my);
    } else if (!left && !last_left && fast_click) {
        hpdesk_mouse_down(mx, my);
        hpdesk_mouse_up(mx, my);
        hpdesk_handle_click(mx, my);
    }
    last_left = left;

    /* 右键 */
    if (right && !last_right) hpdesk_handle_right_click(mx, my);
    last_right = right;

    /* V0.13.9: 滚轮 → 桌面 (USB 鼠标优先, tablet 次之, PS/2 兜底) */
    {
        extern int usb64_mouse_get_wheel(void);
        extern int mouse64_get_wheel(void);
        extern int usb64_tablet_get_wheel(void);
        int wheel = 0;
        if (usb64_mouse_available()) wheel = usb64_mouse_get_wheel();
        else if (usb64_tablet_available()) wheel = usb64_tablet_get_wheel();
        else wheel = mouse64_get_wheel();
        wheel += g_wheel_acc;   /* 兼容外部 hs_input_wheel 注入 */
        g_wheel_acc = 0;
        if (wheel != 0) {
            hpdesk_update_wheel(wheel);
        }
    }
}

/* ============================================================================
 * V0.13.8 P5: 触摸事件 → 手势 → 桌面动作 对接
 * 触摸屏/触摸板驱动 (USB HID 触摸报告) 调用 hs_touch_report 填充事件;
 * 手势结果映射为鼠标等效动作 (参考 Android/Wayland 触摸交互)
 * ========================================================================== */
static hs_touch_state_t g_touch_st;
static uint64_t g_touch_ticks = 0;

void hs_touch_report(int type, int id, int x, int y)
{
    hs_touch_event_t ev;
    ev.type = type;
    ev.id = id;
    ev.x = x;
    ev.y = y;
    ev.ticks = ++g_touch_ticks;

    hs_gesture_t g = hs_touch_process(&g_touch_st, &ev);
    if (g.gesture == HS_GESTURE_NONE) return;

    /* 手势 → 桌面动作 */
    switch (g.gesture) {
    case HS_GESTURE_TAP:
        hpdesk_mouse_down(g.x, g.y);
        hpdesk_mouse_up(g.x, g.y);
        hpdesk_handle_click(g.x, g.y);
        break;
    case HS_GESTURE_DRAG:
        hpdesk_mouse_down(g.x, g.y);
        hpdesk_update_mouse(g.x, g.y);
        break;
    case HS_GESTURE_LONG_PRESS:
        hpdesk_handle_right_click(g.x, g.y);
        break;
    case HS_GESTURE_SWIPE:
        if (g.scroll_delta < 0) hpdesk_switch_workspace_delta(1);
        else if (g.scroll_delta > 0) hpdesk_switch_workspace_delta(-1);
        break;
    case HS_GESTURE_PINCH:
        /* 捏合缩放: 缩放焦点窗口 (参考 Wayland 窗口缩放) */
        if (g.pinch_scale > 256) hpdesk_pinch_resize(1);
        else if (g.pinch_scale < 256) hpdesk_pinch_resize(-1);
        break;
    default:
        break;
    }
}

void hs_touch_reset_all(void)
{
    hs_touch_reset(&g_touch_st);
}
