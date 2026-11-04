/* ============================================================================
 * 赫普OS - 手机模式 - 隐私看板
 * ========================================================================== */

#include "mobile.h"
#include "vga_gfx.h"
#include "vga.h"
#include "keyboard.h"
#include "font_zh.h"
#include "serial.h"
#include "permission_manager.h"
#include <stdint.h>

/* 共享变量 (定义在 mobile.c 中) */
extern uint8_t mob_accent;
extern int sys_lang_en;

/* 共享函数 (定义在 mobile.c 中) */
extern void ui_draw_app_bg(void);
extern const char *privacy_app_name(int app_id, prm_app_brief_t *apps, int na, char *buf);
extern const char *perm_en(int perm);

void mobile_run_privacy(void)
{
    int running = 1;
    prm_app_perm_stat_t stats[16];
    prm_app_brief_t apps[16];
    while (running) {
        int ns = prm_log_stats(stats, 16);
        int na = prm_app_list_brief(apps, 16);
        ui_draw_app_bg();   /* V3.50: TRON 网格背景 */
        gfx_fillrect(0, 0, gfx_width, 14, mob_accent);
        gfx_puts(4, 3, "ESC:Back", 0x0F);
        gfx_puts((gfx_width - 28) / 2, 3, "Privacy", 0x0B);
        if (ns == 0) {
            if (sys_lang_en) gfx_puts((gfx_width - 96) / 2, 60, "No usage records", 0x08);
            else gfx_puts_zh((gfx_width - 80) / 2, 60, "暂无权限使用记录", 0x08);
        } else {
            for (int i = 0; i < ns && i < 8; i++) {
                int y = 36 + i * 16;
                uint8_t bg = (i == 0) ? mob_accent : 0x08;   /* 最新条目高亮 */
                char namebuf[8];
                gfx_fillrect(4, y, gfx_width - 8, 14, bg);
                gfx_puts(8, y + 1, privacy_app_name(stats[i].app_id, apps, na, namebuf), 0x0F);
                gfx_puts(76, y + 1, perm_en(stats[i].permission), 0x0B);
                char cnt[8];
                cnt[0] = 'x';
                cnt[1] = (char)('0' + (stats[i].count / 10) % 10);
                cnt[2] = (char)('0' + stats[i].count % 10);
                cnt[3] = 0;
                gfx_puts(gfx_width - 40, y + 1, cnt, 0x0A);
            }
        }
        gfx_puts(8, gfx_height - 12, "Enter: Clear Logs", 0x08);
        gfx_flip();
        char kc;
        while (keyboard_try_getchar(&kc)) {
            if (kc == 0x1B) { running = 0; break; }
            if (kc == '\n' || kc == '\r') {
                prm_clear_logs();
                serial_puts(0x3F8, "[PERM] logs cleared\n");
            }
        }
        for (volatile int d = 0; d < 150000; d++) ;
    }
}
