/* ============================================================================
 * 赫普OS - 移动桌面公共头文件
 * 从 mobile.c 拆分（大文件拆分目标<500行）
 * 统一声明所有公共变量和函数
 * ========================================================================== */

#ifndef HEPU_MOBILE_H
#define HEPU_MOBILE_H

#include <stdint.h>
#include "permission_manager.h"
#include "wm.h"
#include "mouse.h"
#include "keyboard.h"
#include "ide_ext2.h"
#include "pit.h"

/* ---- 宏定义 ---- */
#define MOBILE_APPS 14
#define MOBILE_PAGES 3
#define MOBILE_COLS 3
#define MOBILE_ROWS 2
#define MOBILE_APPS_PER_PAGE (MOBILE_COLS * MOBILE_ROWS)
#define CALC_BUF_MAX 24
#define CALC_INT_MAX_DIGITS 9
#define CALC_ERR (-999999999)
#define ALARM_MAX 6
#define WC_MAX 8
#define CALLS_MAX 32
#define SMS_COUNT 20
#define SMS_LIST_VIEW 7
#define SMS_IN_MAX 40
#define CONV_VIEW 4
#define CONTACTS_MAX 12
#define CONTACTS_TAG_MAX 5
#define CONTACTS_BACKUP_MAX 5
#define SEARCH_HIST_MAX 8
#define MUSIC_COUNT 4
#define MUSIC_RECENT_MAX 5
#define SET_ITEMS 14
#define PERM_DETAIL_ROWS 8
#define ALBUM_NUM 6
#define ALBUM_THUMB_W 98
#define ALBUM_THUMB_H 50
#define WEB_URL_MAX 32
#define WEB_SITES 7
#define MOB_TOGGLE_NUM 4
#define MOB_MENU_ITEMS 3
#define MOB_ESC_CONFIRM_TICKS 600

/* ---- 全局状态变量 ---- */
extern int mob_page;
extern int mob_running;
extern int mob_focus;
extern int mob_notify_open;
extern int sms_unread_count;
extern int mob_esc_arm;
extern int mob_esc_left;
extern uint8_t mob_accent;
extern int sys_lang_en;
extern int ui_tron;
extern int mob_time12;
extern int mob_invert;
extern int mob_datefmt;
extern int mob_wallpaper;
extern int calc_rad;
extern int calc_len;
extern char calc_buf[];
extern const char *calc_keys[];
extern int mob_toggles[];
extern const char *mob_toggle_names[];
extern int mob_menu;
extern int mob_menu_sel;
extern const char *mob_menu_names[];
extern int touch_down;
extern int touch_start_x, touch_start_y;
extern int touch_cur_x, touch_cur_y;
extern int touch_dragging;
extern int touch_moved;
extern int touch_hold_count;
extern int touch_hold_done;
extern int sb_h;
extern int nb_h;
extern int app_area_x, app_area_y, app_area_w, app_area_h;
extern int cell_w, cell_h;
extern int icon_w, icon_h;

/* ---- 应用元数据 ---- */
extern const char *mob_app_names[];
extern const char *mob_app_names_en[];
extern const char *mob_app_ids[];
extern const uint8_t mob_app_colors[];

/* ---- 闹钟/世界时钟 ---- */
extern int al_h[];
extern int al_m[];
extern int al_armed[];
extern int al_sel;
extern int al_full;
extern char wc_name[][9];
extern int wc_off[];
extern int wc_cnt;
extern int wc_sel;
extern const char *wc_pool_name[];
extern const int wc_pool_off[];

/* ---- 通话记录 ---- */
extern char calls_buf[];
extern int calls_count;
extern char calls_nums[][16];
extern char calls_times[][8];
extern int calls_flags[];
extern int call_sel;
extern char phone_prefill_to[];
extern int phone_prefill_active;

/* ---- 短信相关 ---- */
extern char sms_disk_buf[];
extern int sms_disk_count;
extern const char *sms_senders[];
extern const char *sms_preview[];
extern int sms_dir[];
extern char sms_times[][6];
extern int sms_open_idx;
extern int sms_unread_flags[];
extern int sms_view_map[];
extern int sms_view_count;
extern char sms_compose_to[];
extern int sms_compose_to_len;
extern char sms_compose_body[];
extern int sms_compose_body_len;
extern int sms_composing;
extern int sms_ime_on;
extern int sms_reply_idx;
extern int sms_fwd_idx;
extern char sms_prefill_to[];
extern int sms_prefill_active;
extern int sms_cursor;
extern int sms_scroll;
extern int sms_disk_sel;
extern int sms_conv_sel;
extern int sms_conv_count;
extern char sms_conv_peers[][32];
extern int sms_conv_unread[];

/* ---- 通讯录相关 ---- */
extern int contacts_count;
extern int contacts_ime_on;
extern char contacts_names[][24];
extern char contacts_nums[][16];
extern char contacts_tags[][8];
extern int contacts_sel;
extern int contacts_edit_idx;
extern char contacts_msg[];
extern int contacts_msg_left;
extern const char *contacts_tags_def[];
extern int search_hist_cnt;
extern char search_hist[][17];

/* ---- 音乐相关 ---- */
extern const char *music_titles[];
extern int music_fav[];
extern int music_fav_view;
extern int music_cur;
extern int music_mode;
extern int music_rand;
extern int music_sort_mode;
extern int music_order[];
extern int music_rec[];
extern int music_hist_view;
extern int music_last_cur;
extern int music_playing;
extern int music_vol;
extern void music_rand_save(void);
extern void music_mode_load(void);
extern void datefmt_load(void);
extern void datefmt_save(void);

/* ---- 设置相关 ---- */
extern const char *accent_names[];
extern const uint8_t accent_colors[];
extern int accent_idx;
extern const char *set_names[];
extern int set_vals[];

/* ---- 相机/相册相关 ---- */
extern int cam_count;
extern uint8_t cam_bmp_buf[];
extern const char *album_names[];
extern int album_open;
extern uint8_t album_thumbs[][ALBUM_THUMB_W * ALBUM_THUMB_H];
extern int album_thumb_valid[];

/* ---- 浏览器相关 ---- */
extern char web_url[];
extern int web_url_len;
extern int web_page;
extern const char *web_site_keys[];
extern const char *web_site_titles[];
extern const char *web_site_line2[];
extern const char *qkbd_rows[];

extern char web_bm_names[][16];
extern int web_bm_count;

/* ---- 权限相关 ---- */
extern int mobile_perm_asked[];

/* ---- 模式切换 ---- */
extern int hepu_mode_switch_request;
extern int hepu_ui_mode;
extern int hepu_mobile_launch_slot(int slot);

/* ---- 公共函数 ---- */
extern const char *ui_tr(const char *zh, const char *en);
extern void ui_title(const char *zh, const char *en, int y);
extern uint8_t ui_border(void);
extern void ui_draw_app_bg(void);
extern void mob_time_str(char *out, int h, int m);
extern void lang_save(void);
extern void lang_load(void);
extern void mobile_layout(void);
extern void mobile_draw_statusbar(void);
extern void mobile_draw_grid(void);
extern void mobile_draw_dots(void);
extern void mobile_draw_navbar(void);
extern void mobile_draw_notify(void);
extern void mobile_draw_menu(void);
extern void mobile_draw_all(void);
extern void mobile_draw_qkbd(void);
extern void mobile_app_info(int idx);
extern void mobile_launch_app(int idx);
extern void mobile_handle_touch(void);
extern void mobile_run(void);

/* ---- 应用模块函数 ---- */
extern void mobile_run_terminal(void);
extern void mobile_run_files(void);
extern void mobile_run_clock(void);
extern void mobile_run_about(void);
extern void mobile_run_calc(void);
extern void mobile_run_phone(void);
extern void mobile_run_compose(void);
extern void mobile_run_conversation(int disk_sel);
extern void mobile_run_contacts(void);
extern void mobile_run_sms(void);
extern void mobile_run_music(void);
extern void mobile_run_power(void);
extern void mobile_run_settings(void);
extern void mobile_run_perm_app(int app_id);
extern void mobile_run_perm_mgr(void);
extern void mobile_run_privacy(void);
extern void mobile_run_ai(void);
extern void mobile_run_camera(void);
extern void mobile_view_photo(int shot_no);
extern void mobile_run_album(void);
extern void mobile_run_bookmarks(void);
extern void mobile_draw_webpage(void);
extern void mobile_run_browser(void);
extern int mobile_perm_request(const char *app, const char *pkg, int perm);
extern void mobile_perm_denied_page(const char *app, const char *perm);

/* ---- 计算器函数 ---- */
extern int calc_log10(int x);
extern int calc_ln(int x);
extern int calc_fact(int x);
extern int calc_sin(int deg);
extern int calc_cos(int deg);
extern int calc_tan(int deg);
extern int calc_sci(int op, int x);
extern int calc_isqrt(int x);
extern int calc_asin(int x);
extern int calc_acos(int x);
extern int calc_atan(int x);
extern int calc_wday(int y, int m, int d);
extern const char *calc_wday_name(int w);

/* ---- 闹钟/世界时钟函数 ---- */
extern void alarm_load(void);
extern void alarm_save(void);
extern void wc_load(void);
extern void wc_save(void);

/* ---- 通话记录函数 ---- */
extern void calls_append(const char *num, int missed);
extern void calls_delete(int idx);
extern void calls_load_from_disk(void);
extern void calls_clear_all(void);
extern void calls_mark_answered_last(void);

/* ---- 短信函数 ---- */
extern void sms_save_to_disk(void);
extern void sms_mark_read_peer(const char *peer);
extern void sms_load_from_disk(void);

/* ---- 通讯录函数 ---- */
extern const char *contacts_name_of(const char *num);
extern void search_hist_load(void);
extern void search_hist_save(void);

/* ---- 权限函数 ---- */
extern const char *prm_perm_name(int perm);

#endif /* HEPU_MOBILE_H */
extern void sms_edit_disk(int unmark_idx, int del_idx);
extern void sms_delete(int idx);
extern void sms_append(const char *to, const char *body);
extern void sms_seed_welcome(void);
extern void sms_wrap2(const char *text, char *l1, int c1, char *l2, int c2);
extern void music_recent_push(int idx);
extern void music_rand_next(void);
extern void music_sort_toggle(void);
extern void music_fav_toggle(void);
extern int music_order_pos(int idx);
extern void music_build_order(void);
extern void music_rand_load(void);
extern void music_rand_next(void);
extern void music_fav_save(void);
extern void music_fav_load(void);
extern void music_mode_save(void);
extern void music_recent_save(void);
extern void music_recent_load(void);
extern int music_pos;
extern void wallpaper_load(void);
extern void wallpaper_save(void);
extern void accent_save(void);
extern void accent_load(void);
extern void timefmt_save(void);
extern void timefmt_load(void);
extern void lang_save(void);
extern void lang_load(void);
extern void tron_load(void);
extern void tron_save(void);
extern void invert_load(void);
extern void invert_save(void);
extern void theme_load(void);
extern void theme_save(void);
extern void time12_load(void);
extern void time12_save(void);
extern void contacts_load(void);
extern void contacts_save(void);
extern void web_bookmarks_load(void);
extern void web_bookmarks_save(void);
