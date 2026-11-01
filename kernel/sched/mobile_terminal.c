/* ============================================================================
 * 赫普OS - 手机模式 - 终端
 * ========================================================================== */

#include "mobile.h"
#include "vga_gfx.h"
#include "vga.h"
#include "keyboard.h"
#include "font_zh.h"
#include <stdint.h>

/* 共享变量 (定义在 mobile.c 中) */
extern uint8_t mob_accent;
extern int sys_lang_en;

/* 共享函数 (定义在 mobile.c 中) */
extern void ui_draw_app_bg(void);

void mobile_run_terminal(void)
{
    char line[48];
    int len = 0;
    line[0] = 0;
    int running = 1;
    while (running) {
        ui_draw_app_bg();   /* V3.50: TRON 网格背景 */
        gfx_fillrect(0, 0, gfx_width, 14, mob_accent);
        gfx_puts(4, 3, "Back: ESC", 0x0F);
        if (sys_lang_en) gfx_puts((gfx_width - 64) / 2, 18, "Terminal", 0x0B);
        else gfx_puts_zh((gfx_width - 96) / 2, 18, "终端", 0x0B);
        gfx_fillrect(8, 38, gfx_width - 16, gfx_height - 46, 0x03);
        gfx_puts(12, 44, "Hepu terminal (mobile)", 0x0F);
        gfx_puts(12, 58, "commands: help version echo", 0x0F);
        gfx_puts(12, 72, line[0] ? line : "_", 0x0F);
        gfx_flip();
        char kc;
        while (keyboard_try_getchar(&kc)) {
            if (kc == 0x1B) { running = 0; break; }
            if (kc == '\n' || kc == '\r') {
                gfx_puts(12, 86, "executed (demo)", 0x0A);
                len = 0; line[0] = 0;
            } else if (kc == '\b') {
                if (len > 0) line[--len] = 0;
            } else if (len < 47) {
                line[len++] = kc; line[len] = 0;
            }
        }
        for (volatile int d = 0; d < 200000; d++) ;
    }
}
