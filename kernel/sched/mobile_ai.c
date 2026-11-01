/* ============================================================================
 * 赫普OS - 手机模式 - AI 助手应用
 * 麦克风入口 + 语音识别演示 + 权限联动
 * ========================================================================== */

#include "mobile.h"
#include "vga_gfx.h"
#include "vga.h"
#include "keyboard.h"
#include "font_zh.h"
#include "permission_manager.h"
#include "string.h"
#include <stdint.h>

/* 共享变量 (定义在 mobile.c 中) */
extern uint8_t mob_accent;
extern int sys_lang_en;

/* 共享函数 (定义在 mobile.c 中) */
extern void ui_draw_app_bg(void);
extern uint8_t ui_border(void);

/* 共享函数 (定义在 mobile_perm_mgr.c 中) */
extern int mobile_perm_request(const char *app, const char *pkg, int perm);
extern void mobile_perm_denied_page(const char *app, const char *perm);

/* V3.79: AI 助手应用——麦克风入口 + 语音识别演示 + 权限联动
 * [A] 触发识别 (ai_speech_recognize 原型), ESC 返回 */
void mobile_run_ai(void)
{
    /* 权限门: 麦克风权限 (V3.79: 弹窗授权, 拒绝则提示页) */
    if (!mobile_perm_request("assistant", "com.hepu.assistant", PM_PERM_MICROPHONE)) {
        mobile_perm_denied_page("assistant", prm_perm_name(PM_PERM_MICROPHONE));
        return;
    }

    int running = 1;
    char last_text[128] = "";
    int heard = 0;   /* 是否识别过一次 */
    while (running) {
        ui_draw_app_bg();   /* V3.50: TRON 网格背景 */
        gfx_fillrect(0, 0, gfx_width, 14, mob_accent);
        gfx_puts(4, 3, "Back: ESC", 0x0F);
        if (sys_lang_en) gfx_puts((gfx_width - 76) / 2, 18, "AI Assistant", 0x0A);
        else gfx_puts_zh((gfx_width - 72) / 2, 18, "AI 助手", 0x0A);

        /* 对话区 */
        if (!sys_lang_en) gfx_puts_zh(8, 42, "你好，我是赫普助手", 0x0F);
        else gfx_puts(8, 42, "Hi, I'm Hepu AI.", 0x0F);
        if (heard) {
            gfx_puts(8, 62, "You: (voice)", 0x0B);
            if (sys_lang_en) gfx_puts(8, 78, last_text, 0x0A);
            else gfx_puts_zh(8, 78, last_text, 0x0A);
        }

        /* 麦克风按钮 */
        gfx_fillrect(8, gfx_height - 26, gfx_width - 16, 18, 0x0A);
        gfx_rect(8, gfx_height - 26, gfx_width - 16, 18, ui_border());
        if (!sys_lang_en) gfx_puts_zh(20, gfx_height - 22, "[A] 说话", 0x0F);
        else gfx_puts(20, gfx_height - 22, "[A] Speak", 0x0F);

        gfx_flip();
        char kc;
        while (keyboard_try_getchar(&kc)) {
            if (kc == 0x1B) { running = 0; break; }
            if (kc == 'a' || kc == 'A') {
                /* 语音识别 (原型: 返回模拟结果) */
                extern int ai_speech_recognize(int16_t *, int, int, void *);
                typedef struct { char text[1024]; float confidence; int is_final;
                                 float start_time; float end_time; } ai_res;
                ai_res res;
                int rc = ai_speech_recognize(NULL, 0, 16000, &res);
                if (rc == 0) {
                    strncpy(last_text, res.text, sizeof(last_text) - 1);
                    last_text[sizeof(last_text) - 1] = 0;
                    heard = 1;
                }
            }
        }
        for (volatile int d = 0; d < 200000; d++) ;
    }
}
