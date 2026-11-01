/* ============================================================================
 * 赫普OS - 手机模式 - 关于页面
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

void mobile_run_about(void)
{
    int running = 1;
    while (running) {
        ui_draw_app_bg();   /* V3.50: TRON 网格背景 */
        gfx_fillrect(0, 0, gfx_width, 14, mob_accent);
        gfx_puts(4, 3, "Back: ESC", 0x0F);
        if (sys_lang_en) gfx_puts((gfx_width - 40) / 2, 24, "About", 0x05);
        else gfx_puts_zh((gfx_width - 96) / 2, 24, "关于赫普", 0x05);
        if (!sys_lang_en) gfx_puts_zh((gfx_width - 160) / 2, 48, "赫普超进化系统", 0x0F);
        gfx_puts((gfx_width - 140) / 2, 70, "Hepu Hyper Evolve OS", 0x0A);
        gfx_puts((gfx_width - 110) / 2, 90, "Version 1.0.2", 0x0B);
        gfx_puts((gfx_width - 120) / 2, 110, "License: GPLv3", 0x07);
        gfx_puts((gfx_width - 150) / 2, 130, "Kernel arch: x86 microkernel", 0x07);
        gfx_flip();
        char kc;
        while (keyboard_try_getchar(&kc)) {
            if (kc == 0x1B) { running = 0; break; }
        }
        for (volatile int d = 0; d < 200000; d++) ;
    }
}
