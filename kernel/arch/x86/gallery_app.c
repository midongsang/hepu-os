/* ============================================================================
 * 赫普OS - 相册应用
 * ========================================================================== */

#include <stdint.h>
#include "gallery_app.h"
#include "vga.h"
#include "string.h"

static gallery_app_t gal;

void gallery_init(void)
{
    memset(&gal, 0, sizeof(gallery_app_t));
    gal.initialized = 1;
    gal.enabled = 1;
    gal.current_album = 0;
    gal.current_media = -1;
    gal.view_mode = 0;  /* 网格 */
    gal.sort_mode = 0;  /* 日期降序 */

    /* 系统相册 */
    gallery_album_create(" all photos ");
    gal.albums[0].is_system = 1;
    gallery_album_create(" 收藏 ");
    gal.albums[1].is_system = 1;
    gallery_album_create(" video ");
    gal.albums[2].is_system = 1;
    gallery_album_create(" reclaim 站 ");
    gal.albums[3].is_system = 1;
    gal.albums[3].hidden = 1;

    /* 测试媒体 */
    gallery_add_media("/home/user/Pictures/IMG_0001.jpg", GAL_TYPE_PHOTO, 4032, 3024, 5*1024*1024);
    gallery_add_media("/home/user/Pictures/IMG_0002.jpg", GAL_TYPE_PHOTO, 4032, 3024, 4*1024*1024);
    gallery_add_media("/home/user/Pictures/IMG_0003.jpg", GAL_TYPE_PHOTO, 3024, 4032, 6*1024*1024);
    gallery_add_media("/home/user/Videos/VID_0001.mp4", GAL_TYPE_VIDEO, 1920, 1080, 50*1024*1024);

    vga_puts("[GALLERY] gallery app init done \n");
    vga_printf(" media: %d, gallery: %d \n", gal.media_count, gal.album_count);
}

/* 媒体管理 */
int gallery_add_media(const char *path, gal_media_type_t type, uint32_t w, uint32_t h, uint64_t size)
{
    for (int i = 0; i < GAL_MAX_MEDIA; i++) {
        if (!gal.media[i].used) {
            memset(&gal.media[i], 0, sizeof(gal_media_t));
            gal.media[i].used = 1;
            gal.media[i].id = i;
            if (path) {
                strncpy(gal.media[i].path, path, 255);
                const char *name = path;
                for (int j = strlen(path) - 1; j >= 0; j--) {
                    if (path[j] == '/') { name = path + j + 1; break; }
                }
                strncpy(gal.media[i].filename, name, 127);
            }
            gal.media[i].type = type;
            gal.media[i].width = w;
            gal.media[i].height = h;
            gal.media[i].size = size;
            gal.media[i].timestamp = 1;
            gal.media_count++;
            gal.total_size += size;
            if (type == GAL_TYPE_PHOTO || type == GAL_TYPE_RAW) gal.total_photos++;
            else gal.total_videos++;
            /* 添加到" all photos "相册 */
            gallery_album_add_media(0, i);
            if (type == GAL_TYPE_VIDEO) gallery_album_add_media(2, i);
            return i;
        }
    }
    return -1;
}

int gallery_remove_media(int id)
{
    if (id < 0 || id >= GAL_MAX_MEDIA || !gal.media[id].used) return -1;
    gal.media[id].used = 0;
    gal.media_count--;
    return 0;
}

int gallery_get_media(int id, gal_media_t *media)
{
    if (id < 0 || id >= GAL_MAX_MEDIA || !gal.media[id].used || !media) return -1;
    memcpy(media, &gal.media[id], sizeof(gal_media_t));
    return 0;
}

int gallery_list_media(gal_media_t *media, int max)
{
    if (!media || max <= 0) return 0;
    int count = 0;
    for (int i = GAL_MAX_MEDIA - 1; i >= 0 && count < max; i--) {
        if (gal.media[i].used && !gal.media[i].trash && !gal.media[i].hidden)
            memcpy(&media[count++], &gal.media[i], sizeof(gal_media_t));
    }
    return count;
}

int gallery_list_by_album(int album_id, gal_media_t *media, int max)
{
    if (album_id < 0 || album_id >= GAL_MAX_ALBUMS || !gal.albums[album_id].used || !media || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < gal.albums[album_id].media_count && count < max; i++) {
        int mid = gal.albums[album_id].media_ids[i];
        if (mid >= 0 && mid < GAL_MAX_MEDIA && gal.media[mid].used)
            memcpy(&media[count++], &gal.media[mid], sizeof(gal_media_t));
    }
    return count;
}

int gallery_list_favorites(gal_media_t *media, int max)
{
    if (!media || max <= 0) return 0;
    int count = 0;
    for (int i = GAL_MAX_MEDIA - 1; i >= 0 && count < max; i--) {
        if (gal.media[i].used && gal.media[i].favorite && !gal.media[i].trash)
            memcpy(&media[count++], &gal.media[i], sizeof(gal_media_t));
    }
    return count;
}

int gallery_list_trash(gal_media_t *media, int max)
{
    if (!media || max <= 0) return 0;
    int count = 0;
    for (int i = GAL_MAX_MEDIA - 1; i >= 0 && count < max; i--) {
        if (gal.media[i].used && gal.media[i].trash)
            memcpy(&media[count++], &gal.media[i], sizeof(gal_media_t));
    }
    return count;
}

int gallery_search(const char *query, gal_media_t *results, int max)
{
    if (!query || !results || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < GAL_MAX_MEDIA && count < max; i++) {
        if (gal.media[i].used && !gal.media[i].trash) {
            /* 简单文件名匹配 */
            int match = 1;
            int qlen = 0; while (query[qlen]) qlen++;
            for (int j = 0; j < qlen; j++) {
                int found = 0;
                for (int k = 0; gal.media[i].filename[k]; k++) {
                    if (gal.media[i].filename[k] == query[j]) { found = 1; break; }
                }
                if (!found) { match = 0; break; }
            }
            if (match) memcpy(&results[count++], &gal.media[i], sizeof(gal_media_t));
        }
    }
    return count;
}

/* 编辑 */
int gallery_rotate(int id, int degrees)
{
    if (id < 0 || id >= GAL_MAX_MEDIA || !gal.media[id].used) return -1;
    gal.media[id].rotation = (gal.media[id].rotation + degrees) % 360;
    gal.media[id].edited = 1;
    gal.total_edits++;
    return 0;
}

int gallery_crop(int id, int x, int y, int w, int h)
{
    if (id < 0 || id >= GAL_MAX_MEDIA || !gal.media[id].used) return -1;
    gal.media[id].crop_x = x;
    gal.media[id].crop_y = y;
    gal.media[id].crop_w = w;
    gal.media[id].crop_h = h;
    gal.media[id].edited = 1;
    gal.total_edits++;
    return 0;
}

int gallery_adjust(int id, int brightness, int contrast, int saturation)
{
    if (id < 0 || id >= GAL_MAX_MEDIA || !gal.media[id].used) return -1;
    gal.media[id].brightness = brightness;
    gal.media[id].contrast = contrast;
    gal.media[id].saturation = saturation;
    gal.media[id].edited = 1;
    gal.total_edits++;
    return 0;
}

int gallery_apply_filter(int id, int filter_id)
{
    (void)filter_id;
    if (id < 0 || id >= GAL_MAX_MEDIA || !gal.media[id].used) return -1;
    gal.media[id].edited = 1;
    gal.total_edits++;
    return 0;
}

int gallery_undo(int id)
{
    if (id < 0 || id >= GAL_MAX_MEDIA || !gal.media[id].used) return -1;
    return 0;
}

int gallery_save_edits(int id)
{
    if (id < 0 || id >= GAL_MAX_MEDIA || !gal.media[id].used) return -1;
    return 0;
}

int gallery_revert_edits(int id)
{
    if (id < 0 || id >= GAL_MAX_MEDIA || !gal.media[id].used) return -1;
    gal.media[id].edited = 0;
    gal.media[id].rotation = 0;
    gal.media[id].brightness = 0;
    gal.media[id].contrast = 0;
    gal.media[id].saturation = 0;
    return 0;
}

/* 相册管理 */
int gallery_album_create(const char *name)
{
    for (int i = 0; i < GAL_MAX_ALBUMS; i++) {
        if (!gal.albums[i].used) {
            memset(&gal.albums[i], 0, sizeof(gal_album_t));
            gal.albums[i].used = 1;
            gal.albums[i].id = i;
            if (name) strncpy(gal.albums[i].name, name, 127);
            gal.albums[i].created = 1;
            gal.albums[i].modified = 1;
            gal.album_count++;
            return i;
        }
    }
    return -1;
}

int gallery_album_delete(int id)
{
    if (id < 0 || id >= GAL_MAX_ALBUMS || !gal.albums[id].used) return -1;
    if (gal.albums[id].is_system) return -1;
    gal.albums[id].used = 0;
    gal.album_count--;
    return 0;
}

int gallery_album_rename(int id, const char *name)
{
    if (id < 0 || id >= GAL_MAX_ALBUMS || !gal.albums[id].used || !name) return -1;
    if (gal.albums[id].is_system) return -1;
    strncpy(gal.albums[id].name, name, 127);
    gal.albums[id].modified = 1;
    return 0;
}

int gallery_album_add_media(int album_id, int media_id)
{
    if (album_id < 0 || album_id >= GAL_MAX_ALBUMS || !gal.albums[album_id].used) return -1;
    if (media_id < 0 || media_id >= GAL_MAX_MEDIA || !gal.media[media_id].used) return -1;
    if (gal.albums[album_id].media_count < 256) {
        gal.albums[album_id].media_ids[gal.albums[album_id].media_count++] = media_id;
        gal.albums[album_id].modified = 1;
        return 0;
    }
    return -1;
}

int gallery_album_remove_media(int album_id, int media_id)
{
    if (album_id < 0 || album_id >= GAL_MAX_ALBUMS || !gal.albums[album_id].used) return -1;
    for (int i = 0; i < gal.albums[album_id].media_count; i++) {
        if (gal.albums[album_id].media_ids[i] == media_id) {
            for (int j = i; j < gal.albums[album_id].media_count - 1; j++)
                gal.albums[album_id].media_ids[j] = gal.albums[album_id].media_ids[j+1];
            gal.albums[album_id].media_count--;
            return 0;
        }
    }
    return -1;
}

int gallery_album_get(int id, gal_album_t *album)
{
    if (id < 0 || id >= GAL_MAX_ALBUMS || !gal.albums[id].used || !album) return -1;
    memcpy(album, &gal.albums[id], sizeof(gal_album_t));
    return 0;
}

int gallery_album_list(gal_album_t *albums, int max)
{
    if (!albums || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < GAL_MAX_ALBUMS && count < max; i++) {
        if (gal.albums[i].used && !gal.albums[i].hidden)
            memcpy(&albums[count++], &gal.albums[i], sizeof(gal_album_t));
    }
    return count;
}

/* 操作 */
int gallery_toggle_favorite(int id)
{
    if (id < 0 || id >= GAL_MAX_MEDIA || !gal.media[id].used) return -1;
    gal.media[id].favorite = !gal.media[id].favorite;
    if (gal.media[id].favorite) gallery_album_add_media(1, id);
    return gal.media[id].favorite;
}

int gallery_toggle_hidden(int id)
{
    if (id < 0 || id >= GAL_MAX_MEDIA || !gal.media[id].used) return -1;
    gal.media[id].hidden = !gal.media[id].hidden;
    return gal.media[id].hidden;
}

int gallery_move_to_trash(int id)
{
    if (id < 0 || id >= GAL_MAX_MEDIA || !gal.media[id].used) return -1;
    gal.media[id].trash = 1;
    gal.trash_count++;
    gallery_album_add_media(3, id);
    return 0;
}

int gallery_restore_from_trash(int id)
{
    if (id < 0 || id >= GAL_MAX_MEDIA || !gal.media[id].used) return -1;
    gal.media[id].trash = 0;
    if (gal.trash_count > 0) gal.trash_count--;
    return 0;
}

int gallery_empty_trash(void)
{
    int count = 0;
    for (int i = 0; i < GAL_MAX_MEDIA; i++) {
        if (gal.media[i].used && gal.media[i].trash) {
            gal.media[i].used = 0;
            gal.media_count--;
            count++;
        }
    }
    gal.trash_count = 0;
    return count;
}

int gallery_share(int id, const char *target)
{
    (void)target;
    if (id < 0 || id >= GAL_MAX_MEDIA || !gal.media[id].used) return -1;
    gal.total_shares++;
    return 0;
}

int gallery_set_wallpaper(int id)
{
    if (id < 0 || id >= GAL_MAX_MEDIA || !gal.media[id].used) return -1;
    return 0;
}

/* 视图 */
int gallery_set_view_mode(int mode)
{
    gal.view_mode = mode;
    return 0;
}

int gallery_set_sort_mode(int mode)
{
    gal.sort_mode = mode;
    return 0;
}

int gallery_navigate_album(int album_id)
{
    if (album_id < 0 || album_id >= GAL_MAX_ALBUMS || !gal.albums[album_id].used) return -1;
    gal.current_album = album_id;
    return 0;
}

int gallery_get_current_media(gal_media_t *media)
{
    if (gal.current_media < 0 || !media) return -1;
    return gallery_get_media(gal.current_media, media);
}

int gallery_next_media(void)
{
    if (gal.current_media < GAL_MAX_MEDIA - 1) {
        gal.current_media++;
        return gal.current_media;
    }
    return -1;
}

int gallery_prev_media(void)
{
    if (gal.current_media > 0) {
        gal.current_media--;
        return gal.current_media;
    }
    return -1;
}

/* 统计 */
int gallery_get_stats(gallery_app_t *stats)
{
    if (!stats) return -1;
    memcpy(stats, &gal, sizeof(gallery_app_t));
    return 0;
}
