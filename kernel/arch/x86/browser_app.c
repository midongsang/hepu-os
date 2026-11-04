/* ============================================================================
 * 赫普OS - 浏览器应用
 * ========================================================================== */

#include <stdint.h>
#include "browser_app.h"
#include "vga.h"
#include "string.h"

static void tools_strcat(char *dst, const char *src, int max) {
    int dlen = 0; while (dst[dlen] && dlen < max-1) dlen++;
    int i = 0;
    while (src[i] && dlen < max-1) { dst[dlen++] = src[i++]; }
    dst[dlen] = 0;
}


static browser_app_t br;

void browser_init(void)
{
    memset(&br, 0, sizeof(browser_app_t));
    br.initialized = 1;
    br.enabled = 1;
    br.active_tab = -1;

    /* 默认设置 */
    strncpy(br.settings.homepage, "https://hepu.os/home", 511);
    strncpy(br.settings.search_engine, "HEPU Search", 63);
    br.settings.block_popups = 1;
    br.settings.block_ads = 1;
    br.settings.block_trackers = 1;
    br.settings.do_not_track = 1;
    br.settings.javascript = 1;
    br.settings.cookies = 1;
    br.settings.cache_enabled = 1;
    br.settings.auto_fill = 1;
    br.settings.save_passwords = 0;
    br.settings.dark_mode = 0;
    br.settings.text_size = 100;

    /* 默认书签 */
    browser_bookmark_add(" Hepu OS 官网 ", "https://hepu.os", 0);
    browser_bookmark_add(" Hepu app store ", "https://store.hepu.os", 0);
    browser_bookmark_add(" Hepu 文档 ", "https://docs.hepu.os", 0);
    browser_bookmark_folder_create(" bookmarks 栏 ", -1);

    /* 打开首页 */
    browser_tab_open(br.settings.homepage);

    vga_puts("[BROWSER] browser app init done \n");
    vga_printf(" tabs: %d, bookmarks: %d, adblock: %s\n",
               br.tab_count, br.bookmark_count,
               br.settings.block_ads ? " on " : " close ");
}

/* 标签页 */
int browser_tab_open(const char *url)
{
    for (int i = 0; i < BR_MAX_TABS; i++) {
        if (!br.tabs[i].used) {
            memset(&br.tabs[i], 0, sizeof(br_tab_t));
            br.tabs[i].used = 1;
            br.tabs[i].id = i;
            if (url) strncpy(br.tabs[i].url, url, 511);
            br.tabs[i].state = TAB_STATE_LOADED;
            br.tabs[i].progress = 100;
            br.tabs[i].zoom = 100;
            br.tabs[i].https = (url && url[0] == 'h' && url[1] == 't' && url[2] == 't' && url[3] == 'p' && url[4] == 's') ? 1 : 0;
            br.tabs[i].created = 1;
            br.tabs[i].last_visit = 1;
            br.tab_count++;
            br.active_tab = i;
            if (url) browser_history_add(url, url);
            return i;
        }
    }
    return -1;
}

int browser_tab_close(int tab_id)
{
    if (tab_id < 0 || tab_id >= BR_MAX_TABS || !br.tabs[tab_id].used) return -1;
    br.tabs[tab_id].used = 0;
    br.tab_count--;
    if (br.active_tab == tab_id) {
        br.active_tab = -1;
        for (int i = 0; i < BR_MAX_TABS; i++) {
            if (br.tabs[i].used) { br.active_tab = i; break; }
        }
    }
    return 0;
}

int browser_tab_switch(int tab_id)
{
    if (tab_id < 0 || tab_id >= BR_MAX_TABS || !br.tabs[tab_id].used) return -1;
    br.active_tab = tab_id;
    return 0;
}

int browser_tab_get(int tab_id, br_tab_t *tab)
{
    if (tab_id < 0 || tab_id >= BR_MAX_TABS || !br.tabs[tab_id].used || !tab) return -1;
    memcpy(tab, &br.tabs[tab_id], sizeof(br_tab_t));
    return 0;
}

int browser_tab_list(br_tab_t *tabs, int max)
{
    if (!tabs || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < BR_MAX_TABS && count < max; i++) {
        if (br.tabs[i].used) memcpy(&tabs[count++], &br.tabs[i], sizeof(br_tab_t));
    }
    return count;
}

int browser_tab_get_active(br_tab_t *tab)
{
    if (br.active_tab < 0 || !tab) return -1;
    return browser_tab_get(br.active_tab, tab);
}

int browser_tab_count(void)
{
    return br.tab_count;
}

/* 导航 */
int browser_navigate(int tab_id, const char *url)
{
    if (tab_id < 0 || tab_id >= BR_MAX_TABS || !br.tabs[tab_id].used || !url) return -1;
    /* 保存历史 */
    if (br.tabs[tab_id].history_count < 32) {
        strncpy(br.tabs[tab_id].history[br.tabs[tab_id].history_count++], br.tabs[tab_id].url, 511);
    }
    strncpy(br.tabs[tab_id].url, url, 511);
    br.tabs[tab_id].state = TAB_STATE_LOADING;
    br.tabs[tab_id].progress = 0;
    br.tabs[tab_id].state = TAB_STATE_LOADED;
    br.tabs[tab_id].progress = 100;
    br.tabs[tab_id].last_visit = 1;
    br.total_pages_visited++;
    browser_history_add(url, url);
    return 0;
}

int browser_reload(int tab_id)
{
    if (tab_id < 0 || tab_id >= BR_MAX_TABS || !br.tabs[tab_id].used) return -1;
    br.tabs[tab_id].state = TAB_STATE_LOADING;
    br.tabs[tab_id].state = TAB_STATE_LOADED;
    return 0;
}

int browser_stop(int tab_id)
{
    if (tab_id < 0 || tab_id >= BR_MAX_TABS || !br.tabs[tab_id].used) return -1;
    if (br.tabs[tab_id].state == TAB_STATE_LOADING)
        br.tabs[tab_id].state = TAB_STATE_LOADED;
    return 0;
}

int browser_go_back(int tab_id)
{
    if (tab_id < 0 || tab_id >= BR_MAX_TABS || !br.tabs[tab_id].used) return -1;
    if (br.tabs[tab_id].history_pos > 0) {
        br.tabs[tab_id].history_pos--;
        return 0;
    }
    return -1;
}

int browser_go_forward(int tab_id)
{
    if (tab_id < 0 || tab_id >= BR_MAX_TABS || !br.tabs[tab_id].used) return -1;
    if (br.tabs[tab_id].history_pos < br.tabs[tab_id].history_count - 1) {
        br.tabs[tab_id].history_pos++;
        return 0;
    }
    return -1;
}

int browser_get_url(int tab_id, char *buf, int size)
{
    if (tab_id < 0 || tab_id >= BR_MAX_TABS || !br.tabs[tab_id].used || !buf || size <= 0) return -1;
    strncpy(buf, br.tabs[tab_id].url, size - 1);
    return 0;
}

int browser_get_title(int tab_id, char *buf, int size)
{
    if (tab_id < 0 || tab_id >= BR_MAX_TABS || !br.tabs[tab_id].used || !buf || size <= 0) return -1;
    strncpy(buf, br.tabs[tab_id].title, size - 1);
    return 0;
}

/* 书签 */
int browser_bookmark_add(const char *title, const char *url, int folder)
{
    for (int i = 0; i < BR_MAX_BOOKMARKS; i++) {
        if (!br.bookmarks[i].used) {
            memset(&br.bookmarks[i], 0, sizeof(br_bookmark_t));
            br.bookmarks[i].used = 1;
            br.bookmarks[i].id = i;
            if (title) strncpy(br.bookmarks[i].title, title, 255);
            if (url) strncpy(br.bookmarks[i].url, url, 511);
            br.bookmarks[i].folder_id = folder;
            br.bookmarks[i].added = 1;
            br.bookmark_count++;
            return i;
        }
    }
    return -1;
}

int browser_bookmark_remove(int id)
{
    if (id < 0 || id >= BR_MAX_BOOKMARKS || !br.bookmarks[id].used) return -1;
    br.bookmarks[id].used = 0;
    br.bookmark_count--;
    return 0;
}

int browser_bookmark_get(int id, br_bookmark_t *bm)
{
    if (id < 0 || id >= BR_MAX_BOOKMARKS || !br.bookmarks[id].used || !bm) return -1;
    memcpy(bm, &br.bookmarks[id], sizeof(br_bookmark_t));
    return 0;
}

int browser_bookmark_list(br_bookmark_t *bms, int max)
{
    if (!bms || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < BR_MAX_BOOKMARKS && count < max; i++) {
        if (br.bookmarks[i].used) memcpy(&bms[count++], &br.bookmarks[i], sizeof(br_bookmark_t));
    }
    return count;
}

int browser_bookmark_search(const char *query, br_bookmark_t *results, int max)
{
    if (!query || !results || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < BR_MAX_BOOKMARKS && count < max; i++) {
        if (br.bookmarks[i].used) {
            int match = 1;
            int qlen = 0; while (query[qlen]) qlen++;
            for (int j = 0; j < qlen; j++) {
                int found = 0;
                for (int k = 0; br.bookmarks[i].title[k]; k++) {
                    if (br.bookmarks[i].title[k] == query[j]) { found = 1; break; }
                }
                if (!found) { match = 0; break; }
            }
            if (match) memcpy(&results[count++], &br.bookmarks[i], sizeof(br_bookmark_t));
        }
    }
    return count;
}

int browser_bookmark_folder_create(const char *name, int parent)
{
    for (int i = 0; i < 32; i++) {
        if (!br.folders[i].used) {
            memset(&br.folders[i], 0, sizeof(br_bookmark_folder_t));
            br.folders[i].used = 1;
            br.folders[i].id = i;
            if (name) strncpy(br.folders[i].name, name, 63);
            br.folders[i].parent_id = parent;
            br.folder_count++;
            return i;
        }
    }
    return -1;
}

/* 历史 */
int browser_history_add(const char *title, const char *url)
{
    for (int i = 0; i < BR_MAX_HISTORY; i++) {
        if (!br.history[i].used) {
            memset(&br.history[i], 0, sizeof(br_history_t));
            br.history[i].used = 1;
            br.history[i].id = i;
            if (title) strncpy(br.history[i].title, title, 255);
            if (url) strncpy(br.history[i].url, url, 511);
            br.history[i].timestamp = 1;
            br.history[i].visit_count = 1;
            br.history_count++;
            return i;
        }
    }
    return -1;
}

int browser_history_remove(int id)
{
    if (id < 0 || id >= BR_MAX_HISTORY || !br.history[id].used) return -1;
    br.history[id].used = 0;
    br.history_count--;
    return 0;
}

int browser_history_clear(void)
{
    for (int i = 0; i < BR_MAX_HISTORY; i++) br.history[i].used = 0;
    br.history_count = 0;
    return 0;
}

int browser_history_list(br_history_t *items, int max)
{
    if (!items || max <= 0) return 0;
    int count = 0;
    for (int i = BR_MAX_HISTORY - 1; i >= 0 && count < max; i--) {
        if (br.history[i].used) memcpy(&items[count++], &br.history[i], sizeof(br_history_t));
    }
    return count;
}

int browser_history_search(const char *query, br_history_t *results, int max)
{
    if (!query || !results || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < BR_MAX_HISTORY && count < max; i++) {
        if (br.history[i].used) {
            int match = 1;
            int qlen = 0; while (query[qlen]) qlen++;
            for (int j = 0; j < qlen; j++) {
                int found = 0;
                for (int k = 0; br.history[i].url[k]; k++) {
                    if (br.history[i].url[k] == query[j]) { found = 1; break; }
                }
                if (!found) { match = 0; break; }
            }
            if (match) memcpy(&results[count++], &br.history[i], sizeof(br_history_t));
        }
    }
    return count;
}

/* 下载 */
int browser_download_start(const char *url, const char *filename)
{
    for (int i = 0; i < BR_MAX_DOWNLOADS; i++) {
        if (!br.downloads[i].used) {
            memset(&br.downloads[i], 0, sizeof(br_download_t));
            br.downloads[i].used = 1;
            br.downloads[i].id = i;
            if (url) strncpy(br.downloads[i].url, url, 511);
            if (filename) strncpy(br.downloads[i].filename, filename, 127);
            strncpy(br.downloads[i].path, "/home/user/Downloads/", 255);
            if (filename) tools_strcat(br.downloads[i].path, filename, 256);
            br.downloads[i].total_size = 10 * 1024 * 1024;
            br.downloads[i].downloaded = 10 * 1024 * 1024;
            br.downloads[i].progress = 100;
            br.downloads[i].status = 1;  /* 完成 */
            br.downloads[i].started = 1;
            br.downloads[i].completed = 1;
            br.download_count++;
            br.total_downloads++;
            return i;
        }
    }
    return -1;
}

int browser_download_pause(int id)
{
    if (id < 0 || id >= BR_MAX_DOWNLOADS || !br.downloads[id].used) return -1;
    br.downloads[id].status = 2;
    return 0;
}

int browser_download_resume(int id)
{
    if (id < 0 || id >= BR_MAX_DOWNLOADS || !br.downloads[id].used) return -1;
    br.downloads[id].status = 0;
    return 0;
}

int browser_download_cancel(int id)
{
    if (id < 0 || id >= BR_MAX_DOWNLOADS || !br.downloads[id].used) return -1;
    br.downloads[id].status = 4;
    return 0;
}

int browser_download_get(int id, br_download_t *dl)
{
    if (id < 0 || id >= BR_MAX_DOWNLOADS || !br.downloads[id].used || !dl) return -1;
    memcpy(dl, &br.downloads[id], sizeof(br_download_t));
    return 0;
}

int browser_download_list(br_download_t *dls, int max)
{
    if (!dls || max <= 0) return 0;
    int count = 0;
    for (int i = BR_MAX_DOWNLOADS - 1; i >= 0 && count < max; i--) {
        if (br.downloads[i].used) memcpy(&dls[count++], &br.downloads[i], sizeof(br_download_t));
    }
    return count;
}

/* 设置 */
int browser_set_homepage(const char *url)
{
    if (!url) return -1;
    strncpy(br.settings.homepage, url, 511);
    return 0;
}

int browser_set_search_engine(const char *engine)
{
    if (!engine) return -1;
    strncpy(br.settings.search_engine, engine, 63);
    return 0;
}

int browser_toggle_adblock(void)
{
    br.settings.block_ads = !br.settings.block_ads;
    return br.settings.block_ads;
}

int browser_toggle_tracker_block(void)
{
    br.settings.block_trackers = !br.settings.block_trackers;
    return br.settings.block_trackers;
}

int browser_toggle_private(int tab_id)
{
    if (tab_id < 0 || tab_id >= BR_MAX_TABS || !br.tabs[tab_id].used) return -1;
    br.tabs[tab_id].private_mode = !br.tabs[tab_id].private_mode;
    return br.tabs[tab_id].private_mode;
}

int browser_clear_cache(void)
{
    return 0;
}

int browser_clear_cookies(void)
{
    return 0;
}

int browser_get_settings(br_settings_t *settings)
{
    if (!settings) return -1;
    memcpy(settings, &br.settings, sizeof(br_settings_t));
    return 0;
}

/* 统计 */
int browser_get_stats(browser_app_t *stats)
{
    if (!stats) return -1;
    memcpy(stats, &br, sizeof(browser_app_t));
    return 0;
}
