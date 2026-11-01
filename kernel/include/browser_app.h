#ifndef BROWSER_APP_H
#define BROWSER_APP_H

#include <stdint.h>

/* ============================================================================
 * 赫普OS 浏览器应用
 * 标签页 / 书签 / 历史 / 下载 / 隐私模式
 * ========================================================================== */

#define BR_MAX_TABS 32
#define BR_MAX_BOOKMARKS 256
#define BR_MAX_HISTORY 1024
#define BR_MAX_DOWNLOADS 128
#define BR_URL_LEN 512
#define BR_TITLE_LEN 256

/* 标签页状态 */
typedef enum {
    TAB_STATE_EMPTY = 0,
    TAB_STATE_LOADING,
    TAB_STATE_LOADED,
    TAB_STATE_ERROR,
} br_tab_state_t;

/* 标签页 */
typedef struct {
    int used;
    int id;
    char url[BR_URL_LEN];
    char title[BR_TITLE_LEN];
    br_tab_state_t state;
    int progress;  /* 0-100 */
    int private_mode;
    uint64_t created;
    uint64_t last_visit;
    /* 导航历史 */
    char history[32][BR_URL_LEN];
    int history_count;
    int history_pos;
    /* 缩放 */
    int zoom;  /* 50-200% */
    /* 安全 */
    int https;
    char security_info[64];
} br_tab_t;

/* 书签 */
typedef struct {
    int used;
    int id;
    char title[BR_TITLE_LEN];
    char url[BR_URL_LEN];
    int folder_id;
    int favorite;
    uint64_t added;
    uint64_t last_visit;
    int visit_count;
} br_bookmark_t;

/* 书签文件夹 */
typedef struct {
    int used;
    int id;
    char name[64];
    int parent_id;
    int bookmark_count;
} br_bookmark_folder_t;

/* 历史记录 */
typedef struct {
    int used;
    int id;
    char title[BR_TITLE_LEN];
    char url[BR_URL_LEN];
    uint64_t timestamp;
    int visit_count;
} br_history_t;

/* 下载项 */
typedef struct {
    int used;
    int id;
    char url[BR_URL_LEN];
    char filename[128];
    char path[256];
    uint64_t total_size;
    uint64_t downloaded;
    int progress;
    int status;  /* 0=进行中,1=完成,2=暂停,3=失败,4=取消 */
    uint64_t speed;  /* bytes/s */
    uint64_t started;
    uint64_t completed;
} br_download_t;

/* 浏览器设置 */
typedef struct {
    char homepage[BR_URL_LEN];
    char search_engine[64];
    int block_popups;
    int block_ads;
    int block_trackers;
    int do_not_track;
    int javascript;
    int cookies;  /* 0=允许,1=仅当前,2=阻止 */
    int cache_enabled;
    int auto_fill;
    int save_passwords;
    int dark_mode;
    int text_size;  /* 50-200% */
} br_settings_t;

/* 浏览器应用状态 */
typedef struct {
    int initialized;
    int enabled;
    /* 标签页 */
    br_tab_t tabs[BR_MAX_TABS];
    int tab_count;
    int active_tab;
    /* 书签 */
    br_bookmark_t bookmarks[BR_MAX_BOOKMARKS];
    int bookmark_count;
    br_bookmark_folder_t folders[32];
    int folder_count;
    /* 历史 */
    br_history_t history[BR_MAX_HISTORY];
    int history_count;
    /* 下载 */
    br_download_t downloads[BR_MAX_DOWNLOADS];
    int download_count;
    /* 设置 */
    br_settings_t settings;
    /* 统计 */
    uint64_t total_pages_visited;
    uint64_t total_downloads;
    uint64_t trackers_blocked;
    uint64_t ads_blocked;
} browser_app_t;

/* 初始化 */
void browser_init(void);

/* 标签页 */
int browser_tab_open(const char *url);
int browser_tab_close(int tab_id);
int browser_tab_switch(int tab_id);
int browser_tab_get(int tab_id, br_tab_t *tab);
int browser_tab_list(br_tab_t *tabs, int max);
int browser_tab_get_active(br_tab_t *tab);
int browser_tab_count(void);

/* 导航 */
int browser_navigate(int tab_id, const char *url);
int browser_reload(int tab_id);
int browser_stop(int tab_id);
int browser_go_back(int tab_id);
int browser_go_forward(int tab_id);
int browser_get_url(int tab_id, char *buf, int size);
int browser_get_title(int tab_id, char *buf, int size);

/* 书签 */
int browser_bookmark_add(const char *title, const char *url, int folder);
int browser_bookmark_remove(int id);
int browser_bookmark_get(int id, br_bookmark_t *bm);
int browser_bookmark_list(br_bookmark_t *bms, int max);
int browser_bookmark_search(const char *query, br_bookmark_t *results, int max);
int browser_bookmark_folder_create(const char *name, int parent);

/* 历史 */
int browser_history_add(const char *title, const char *url);
int browser_history_remove(int id);
int browser_history_clear(void);
int browser_history_list(br_history_t *items, int max);
int browser_history_search(const char *query, br_history_t *results, int max);

/* 下载 */
int browser_download_start(const char *url, const char *filename);
int browser_download_pause(int id);
int browser_download_resume(int id);
int browser_download_cancel(int id);
int browser_download_get(int id, br_download_t *dl);
int browser_download_list(br_download_t *dls, int max);

/* 设置 */
int browser_set_homepage(const char *url);
int browser_set_search_engine(const char *engine);
int browser_toggle_adblock(void);
int browser_toggle_tracker_block(void);
int browser_toggle_private(int tab_id);
int browser_clear_cache(void);
int browser_clear_cookies(void);
int browser_get_settings(br_settings_t *settings);

/* 统计 */
int browser_get_stats(browser_app_t *stats);

#endif /* BROWSER_APP_H */
