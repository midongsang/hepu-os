/* ============================================================================
 * 赫普OS 64位 - 图形桌面运行器（HepuShell 专用）
 * V0.13.3: 移除经典桌面（desktop64_draw 等），统一由 HepuShell 承担全部桌面渲染
 * 参考现代系统（Windows DWM / Wayland compositor）：单一合成器 + 分层渲染
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>

extern void vga_puts(const char *s);
extern void gfx64_init_from_bootloader(void);
extern void gfx64_get_info(int *width, int *height, int *bpp);

/* HepuShell 接口 */
extern int hepushell_init(int width, int height);
extern void hepushell_render(void);
extern void hepushell_handle_key(int key);
extern void hepushell_update_mouse(int x, int y);
extern void hepushell_mouse_down(int x, int y);
extern void hepushell_mouse_up(int x, int y);
extern void hepushell_handle_click(int x, int y);
extern void hepushell_handle_right_click(int x, int y);

/* 输入 */
extern int keyboard64_haschar(void);
extern int keyboard64_getchar(void);
extern void mouse64_update(int screen_width, int screen_height);
extern void mouse64_get_pos(int *x, int *y);
extern int mouse64_get_left(void);
extern int mouse64_get_right(void);

extern void shell64_run(void);
extern int g_pending_app;

/* V0.13.5: 统一输入抽象层 (USB/PS2 归一化) */
#include "hsinput.h"

/* USB HID 鼠标 */
extern int usb64_init(void);
extern void usb64_poll(void);
extern int usb64_mouse_available(void);
extern void usb64_mouse_get(int *x, int *y, int *left, int *right);
/* USB HID 键盘 */
extern int usb64_kbd_available(void);
extern int usb64_kbd_haschar(void);
extern int usb64_kbd_getchar(void);

static int desktop_initialized = 0;
static int screen_width = 0;
static int screen_height = 0;
int hepu_nographics = 0;  /* hepu.nographics: 默认图形模式，要文本改1 */
/* P3: hepu_dev_mode 全局变量已移除，改为 per-process 会话级（hepu_is_dev） */

/* 图形模式初始化（读取bootloader传递的VBE信息） */
int desktop64_init(void)
{
    if (hepu_nographics) {
        vga_puts("[DESKTOP64] nographics=1, skipping GUI\n");
        return -1;
    }
    int bpp;
    gfx64_init_from_bootloader();
    gfx64_get_info(&screen_width, &screen_height, &bpp);

    if (screen_width == 0 || screen_height == 0) {
        vga_puts("[DESKTOP64] No VBE mode, using text mode\n");
        return -1;
    }

    vga_puts("[DESKTOP64] Desktop initialized: ");
    char buf[16];
    int n = screen_width, i = 0;
    char tmp[8];
    do { tmp[i++] = n % 10 + '0'; } while ((n /= 10) > 0);
    int j = 0;
    while (i > 0) buf[j++] = tmp[--i];
    buf[j++] = 'x';
    n = screen_height; i = 0;
    do { tmp[i++] = n % 10 + '0'; } while ((n /= 10) > 0);
    while (i > 0) buf[j++] = tmp[--i];
    buf[j] = 0;
    vga_puts(buf);
    vga_puts("\n");

    desktop_initialized = 1;
    return 0;
}

/* 桌面主循环：HepuShell 事件循环（鼠标/键盘/渲染/点击） */
void desktop64_run(void)
{
    if (!desktop_initialized) {
        if (desktop64_init() != 0) {
            vga_puts("[DESKTOP64] Failed to initialize desktop\n");
            return;
        }
    }

    /* HepuShell：启动即初始化 */
    static int hepushell_started = 0;
    if (!hepushell_started) {
        hepushell_init(screen_width, screen_height);
        hepushell_started = 1;
        vga_puts("[DESKTOP64] HepuShell started as default desktop\n");
    }









    /* USB 主机控制器初始化（UHCI + HID 鼠标/键盘） */
    if (usb64_init() == 0) {
        vga_puts("[DESKTOP64] USB HID subsystem ready\n");
    }

    vga_puts("[DESKTOP64] Desktop running with mouse support\n");

    /* V0.13.5: 统一输入层接管全部输入分发 (键盘/鼠标/滚轮, USB优先PS/2兜底) */
    hs_input_init(screen_width, screen_height);

    /* V0.13.8 P5.1: 丢弃 PS/2 键盘初始化噪声(误触 F2-F8 关面板), 主循环前清空 */
    {
        extern void keyboard64_flush(void);
        keyboard64_flush();
        /* USB 键盘队列同样清空(枚举报告噪声) */
        extern void usb64_kbd_flush(void);
        usb64_kbd_flush();
    }

    /* V0.55 P3-GUI: 首次启动 → 桌面内弹出图形化首启向导
     * (图形化不可用时由 kernel_main 走 oobe64_run 文本兜底) */
    {
        extern int auth64_is_configured(void);
        extern void hs_oobe_begin(void);
        if (!auth64_is_configured()) hs_oobe_begin();
    }

    /* 事件循环
     * V0.56.1 性能修复：渲染限 60fps 并与输入轮询解耦。
     * 旧实现每轮都整屏合成 + gfx64_present() 全屏拷贝(1080p≈8MB)且无帧率上限，
     * 实际跑到数百 fps，把内存带宽/CPU 吃满，输入轮询被饿死 → 实机卡顿。 */
    uint64_t last_render_ms = 0;
    while (1) {
        extern uint64_t pit64_get_ms(void);
        uint64_t now = pit64_get_ms();

        /* V0.13.9: 网络轮询 (网卡收发) */
        extern void net64_poll(void);
        net64_poll();

        /* V0.13.65: 自动 DHCP 联网 (启动后自动获取 IP, 不依赖 shell 命令) */
        extern int dhcp64_poll(void);
        dhcp64_poll();

        /* V0.13.9: 天气真实数据拉取 (60 帧≈8s 后一次性, 阻塞 ~1-3s) */
        static int wx_tick = 0;
        static int wx_done = 0;
        if (!wx_done) {
            if (++wx_tick >= 60) {
                wx_done = 1;
                extern void hepushell_fetch_weather(void);
                hepushell_fetch_weather();
            }
        }

        hs_input_poll();

        /* V0.13.51: 内存压力周期检查 (每~100帧: 低内存时回收块缓存) */
        static int mp_tick = 0;
        if (++mp_tick >= 100) {
            mp_tick = 0;
            extern void mem_pressure_tick(void);
            mem_pressure_tick();
        }

        /* 渲染：仅每 ~16ms(≈60fps) 一次，输入/网络仍每轮处理保证低延迟 */
        if (now - last_render_ms >= 16) {
            last_render_ms = now;
            hepushell_render();
        }

        /* V0.13.7 P4: HepuShell Dock/启动器图标点击的应用启动请求 */
        if (g_pending_app != 0) {
            hepushell_handle_pending_app();
            vga_puts("[DESKTOP64] Pending app request handled by HepuShell\n");
            g_pending_app = 0;
        }

        /* 让出 CPU 等下一次中断(PIT 1kHz 兜底), 避免忙等 100% 占用 */
        asm volatile("hlt");
    }
}
