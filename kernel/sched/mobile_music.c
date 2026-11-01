/* ============================================================================
 * 赫普OS - 移动桌面音乐应用
 * 从 mobile.c 拆分
 * ========================================================================== */

#include "mobile.h"
#include "vga_gfx.h"
#include "font_zh.h"
#include "serial.h"
#include <string.h>

void mobile_run_music(void)
{
    /* V2.67: 音乐权限门 (STORAGE) */
    if (!mobile_perm_request("music", "com.hepu.music", PM_PERM_STORAGE)) {
        mobile_perm_denied_page("music", prm_perm_name(PM_PERM_STORAGE));
        return;
    }
    int running = 1;
    music_fav_load();
    music_recent_load();   /* V2.95: 续播恢复 */
    music_mode_load();     /* V3.01: 播放模式恢复 */
    music_rand_load();   /* V3.08: 随机种子恢复 */
    music_build_order();
    music_last_cur = music_cur;   /* V3.04: 首帧不触发历史 */
    while (running) {
        ui_draw_app_bg();   /* V3.50: TRON 网格背景 */
        gfx_fillrect(0, 0, gfx_width, 14, mob_accent);
        gfx_puts(4, 3, "Back: ESC", 0x0F);
        if (sys_lang_en)
            gfx_puts((gfx_width - 36) / 2, 18,
                     music_hist_view ? "Recent" : (music_fav_view ? "Favs" : "Music"), 0x0B);
        else
            gfx_puts_zh((gfx_width - 48) / 2, 18,
                        music_hist_view ? "最近" : (music_fav_view ? "收藏" : "音乐"), 0x0B);
        if (music_pos > 0)
            gfx_puts(6, 20, "resumed", 0x0A);
        if (music_hist_view) {
            /* V3.04: 最近播放列表 (最多 5 条) */
            int shown = 0;
            for (int k = 0; k < MUSIC_RECENT_MAX && music_rec[k] >= 0; k++) {
                int y = 36 + shown * 20;
                uint8_t bg = (music_rec[k] == music_cur) ? mob_accent : 0x08;
                gfx_fillrect(4, y, gfx_width - 8, 18, bg);
                gfx_rect(4, y, gfx_width - 8, 18, ui_border());   /* V3.49 */
                gfx_puts(8, y + 2, music_titles[music_rec[k]], 0x0F);
                gfx_puts(8, y + 11, "HEPU", 0x08);
                if (k == 0) gfx_puts(gfx_width - 42, y + 2, "NOW", 0x0A);
                shown++;
            }
            if (shown == 0)
                gfx_puts((gfx_width - 80) / 2, 80, "no history", 0x08);
        } else if (!music_fav_view) {
            /* 曲目列表 (V2.94: F 收藏标记; V3.08: 按排序 order) */
            for (int oi = 0; oi < MUSIC_COUNT; oi++) {
                int i = music_order[oi];
                int y = 36 + oi * 20;
                uint8_t bg = (i == music_cur) ? mob_accent : 0x08;
                gfx_fillrect(4, y, gfx_width - 8, 18, bg);
                gfx_rect(4, y, gfx_width - 8, 18, ui_border());   /* V3.49 */
                gfx_puts(8, y + 2, music_titles[i], 0x0F);
                gfx_puts(8, y + 11, "HEPU", 0x08);
                if (music_fav[i]) gfx_puts(gfx_width - 42, y + 2, "F", 0x0A);
                if (i == music_cur && music_playing)
                    gfx_puts(gfx_width - 28, y + 2, ">", 0x0A);
            }
        } else {
            /* V2.94: 收藏列表 */
            int shown = 0;
            for (int i = 0; i < MUSIC_COUNT; i++) {
                if (!music_fav[i]) continue;
                int y = 36 + shown * 20;
                uint8_t bg = (i == music_cur) ? mob_accent : 0x08;
                gfx_fillrect(4, y, gfx_width - 8, 18, bg);
                gfx_rect(4, y, gfx_width - 8, 18, ui_border());   /* V3.49 */
                gfx_puts(8, y + 2, music_titles[i], 0x0F);
                gfx_puts(gfx_width - 28, y + 2, "F", 0x0A);
                if (i == music_cur && music_playing)
                    gfx_puts(gfx_width - 42, y + 2, ">", 0x0A);
                shown++;
            }
            if (shown == 0) gfx_puts(8, 40, "No favorites (F to add)", 0x08);
        }
        /* 播放控制条 (V2.94: 强调色) */
        gfx_fillrect(4, 36 + MUSIC_COUNT * 20 + 2, gfx_width - 8, 18, mob_accent);
        gfx_puts(8, 36 + MUSIC_COUNT * 20 + 5,
                 music_playing ? "[Pause]" : "[Play]", 0x0F);
        gfx_puts(gfx_width - 64, 36 + MUSIC_COUNT * 20 + 5, "<Prev|Next>", 0x0F);
        /* V2.97: 播放模式 (移至 [Play] 右侧避免与 <Prev|Next> 重叠) */
        gfx_puts(64, 36 + MUSIC_COUNT * 20 + 5,
                 music_mode == 0 ? "Seq" : (music_mode == 1 ? "One" : "Shuf"),
                 0x0B);
        /* V2.85: 进度条 + 时间 */
        {
            int pb_y = 36 + MUSIC_COUNT * 20 + 24;
            gfx_fillrect(8, pb_y, gfx_width - 16, 6, 0x08);
            if (music_pos > 0)
                gfx_fillrect(8, pb_y, (gfx_width - 16) * music_pos / 100, 6,
                             music_playing ? 0x0A : 0x0B);
            int secs = music_pos * 180 / 100;
            int mm = secs / 60, ss = secs % 60;
            char tb[12];
            tb[0] = '0' + mm / 10; tb[1] = '0' + mm % 10; tb[2] = ':';
            tb[3] = '0' + ss / 10; tb[4] = '0' + ss % 10; tb[5] = 0;
            gfx_puts(8, pb_y + 8, tb, 0x08);
            gfx_puts(gfx_width - 44, pb_y + 8, "3:00", 0x08);
            /* 音量 */
            char vb[16];
            int vi = 0;
            vb[vi++] = 'V'; vb[vi++] = 'o'; vb[vi++] = 'l'; vb[vi++] = ':';
            int vn = music_vol;
            char tmp[4]; int tj = 0;
            if (vn == 0) tmp[tj++] = '0';
            while (vn > 0) { tmp[tj++] = '0' + (vn % 10); vn /= 10; }
            while (tj > 0) vb[vi++] = tmp[--tj];
            vb[vi] = 0;
            gfx_puts(8, pb_y + 20, vb, 0x0F);
            gfx_puts(gfx_width - 96, pb_y + 20, "+/-: volume", 0x08);
            gfx_puts(8, gfx_height - 8, "F: fav  V: favs  H: hist  L: sort", 0x08);
        }
        gfx_flip();
        char kc;
        while (keyboard_try_getchar(&kc)) {
            if (kc == 0x1B) {
                music_recent_save();   /* V2.95: 退出记录续播点 */
                running = 0; break;
            }
            if (kc == 'f' || kc == 'F') {
                /* V2.94: 收藏切换 + 落盘 */
                music_fav[music_cur] = !music_fav[music_cur];
                music_fav_save();
            }
            else if (kc == 'v' || kc == 'V') { music_fav_view = !music_fav_view; music_hist_view = 0; }
            else if (kc == 'h' || kc == 'H') { music_hist_view = !music_hist_view; music_fav_view = 0; }
            else if (kc == 'r' || kc == 'R') {   /* V2.97: 循环模式 */
                music_mode = (music_mode + 1) % 3;
                music_mode_save();   /* V3.01: 模式持久化 */
            }
            else if (kc == 'l' || kc == 'L') {   /* V3.08: 排序模式 */
                music_sort_mode = (music_sort_mode + 1) % 3;
                music_build_order();
            }
            else if ((kc == 'c' || kc == 'C') && music_hist_view) {   /* V3.08: 清空历史 */
                for (int k = 0; k < MUSIC_RECENT_MAX; k++) music_rec[k] = -1;
                music_recent_save();
            }
            else if (kc == 'w' || kc == 'W') {
                if (music_fav_view) {
                    for (int k = 1; k <= MUSIC_COUNT; k++) {
                        int ni = (music_cur - k + MUSIC_COUNT) % MUSIC_COUNT;
                        if (music_fav[ni]) { music_cur = ni; break; }
                    }
                } else { int p = music_order_pos(music_cur); if (p > 0) music_cur = music_order[p - 1]; }
            }
            else if (kc == 's' || kc == 'S') {
                if (music_fav_view) {
                    for (int k = 1; k <= MUSIC_COUNT; k++) {
                        int ni = (music_cur + k) % MUSIC_COUNT;
                        if (music_fav[ni]) { music_cur = ni; break; }
                    }
                } else { int p = music_order_pos(music_cur); if (p < MUSIC_COUNT - 1) music_cur = music_order[p + 1]; }
            }
            else if (kc == '\n' || kc == '\r') { music_playing = !music_playing; }
            else if (kc == 'a' || kc == 'A') { int p = music_order_pos(music_cur); if (p > 0) music_cur = music_order[p - 1]; }
            else if (kc == 'd' || kc == 'D') { int p = music_order_pos(music_cur); if (p < MUSIC_COUNT - 1) music_cur = music_order[p + 1]; }
            else if (kc == '<') {   /* V2.85: 上一首 (循环) */
                music_cur = (music_cur > 0) ? music_cur - 1 : MUSIC_COUNT - 1;
                music_pos = 0;
            }
            else if (kc == '>') {   /* V2.85: 下一首 (循环) */
                music_cur = (music_cur < MUSIC_COUNT - 1) ? music_cur + 1 : 0;
                music_pos = 0;
            }
            else if (kc == '+' || kc == '=') {   /* 音量 + */
                if (music_vol < 100) music_vol = music_vol + 5 > 100 ? 100 : music_vol + 5;
            }
            else if (kc == '-' || kc == '_') {   /* 音量 - */
                if (music_vol > 0) music_vol = music_vol - 5 < 0 ? 0 : music_vol - 5;
            }
        }
        /* V3.04: 曲目切换入历史 */
        if (music_cur != music_last_cur) {
            music_last_cur = music_cur;
            music_recent_push(music_cur);
        }
        /* V2.85: 播放时推进度 (循环播放) */
        /* V2.97: 曲目结束按模式切歌 */
        if (music_playing) {
            music_pos++;
            if (music_pos > 100) {
                if (music_mode == 1) {
                    music_pos = 0;                 /* 单曲循环 */
                } else if (music_mode == 2) {
                    music_rand = (music_rand * 7 + 3) % 31;   /* LCG */
                    music_rand_save();   /* V3.08: 种子持久化 */
                    music_cur = (music_cur + 1 +
                                 (music_rand % (MUSIC_COUNT - 1))) % MUSIC_COUNT;
                    music_pos = 0;
                } else {
                    music_cur = (music_cur < MUSIC_COUNT - 1) ? music_cur + 1 : 0;
                    music_pos = 0;
                }
            }
        }
        for (volatile int d = 0; d < 150000; d++) ;
    }
}

/* V2.55: 电源管理子页——电池/状态/省电/休眠唤醒 (联动 pm.c) */
