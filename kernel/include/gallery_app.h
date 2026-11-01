#ifndef GALLERY_APP_H
#define GALLERY_APP_H

#include <stdint.h>

/* ============================================================================
 * 赫普OS 相册应用
 * 图片浏览 / 编辑 / 分享 / 相册管理
 * ========================================================================== */

#define GAL_MAX_MEDIA 1024
#define GAL_MAX_ALBUMS 64
#define GAL_MAX_TAGS 32
#define GAL_NAME_LEN 128
#define GAL_PATH_LEN 256

/* 媒体类型 */
typedef enum {
    GAL_TYPE_PHOTO = 0,
    GAL_TYPE_VIDEO,
    GAL_TYPE_GIF,
    GAL_TYPE_RAW,
} gal_media_type_t;

/* 媒体项 */
typedef struct {
    int used;
    int id;
    char filename[GAL_NAME_LEN];
    char path[GAL_PATH_LEN];
    gal_media_type_t type;
    uint32_t width;
    uint32_t height;
    uint64_t size;
    uint64_t timestamp;
    uint64_t duration;  /* 视频时长 */
    int favorite;
    int hidden;
    int trash;  /* 回收站 */
    char location[64];
    char tags[GAL_MAX_TAGS][32];
    int tag_count;
    uint8_t thumbnail[128];
    /* 编辑 */
    int edited;
    int rotation;  /* 0,90,180,270 */
    int crop_x, crop_y, crop_w, crop_h;
    int brightness;
    int contrast;
    int saturation;
} gal_media_t;

/* 相册 */
typedef struct {
    int used;
    int id;
    char name[GAL_NAME_LEN];
    char cover_path[GAL_PATH_LEN];
    int media_count;
    int media_ids[256];
    int is_system;  /* 系统相册不可删除 */
    int hidden;
    uint64_t created;
    uint64_t modified;
} gal_album_t;

/* 编辑操作历史 */
typedef struct {
    int media_id;
    int operation;  /* 0=旋转,1=裁剪,2=滤镜,3=亮度,4=对比度,5=饱和度 */
    int value;
    int value2;
} gal_edit_history_t;

/* 相册应用状态 */
typedef struct {
    int initialized;
    int enabled;
    /* 媒体库 */
    gal_media_t media[GAL_MAX_MEDIA];
    int media_count;
    /* 相册 */
    gal_album_t albums[GAL_MAX_ALBUMS];
    int album_count;
    /* 当前视图 */
    int current_album;
    int current_media;
    int view_mode;  /* 0=网格,1=列表,2=时间线 */
    int sort_mode;  /* 0=日期降序,1=日期升序,2=名称,3=大小 */
    /* 回收站 */
    int trash_count;
    /* 统计 */
    uint64_t total_photos;
    uint64_t total_videos;
    uint64_t total_size;
    uint64_t total_edits;
    uint64_t total_shares;
} gallery_app_t;

/* 初始化 */
void gallery_init(void);

/* 媒体管理 */
int gallery_add_media(const char *path, gal_media_type_t type, uint32_t w, uint32_t h, uint64_t size);
int gallery_remove_media(int id);
int gallery_get_media(int id, gal_media_t *media);
int gallery_list_media(gal_media_t *media, int max);
int gallery_list_by_album(int album_id, gal_media_t *media, int max);
int gallery_list_favorites(gal_media_t *media, int max);
int gallery_list_trash(gal_media_t *media, int max);
int gallery_search(const char *query, gal_media_t *results, int max);

/* 编辑 */
int gallery_rotate(int id, int degrees);
int gallery_crop(int id, int x, int y, int w, int h);
int gallery_adjust(int id, int brightness, int contrast, int saturation);
int gallery_apply_filter(int id, int filter_id);
int gallery_undo(int id);
int gallery_save_edits(int id);
int gallery_revert_edits(int id);

/* 相册管理 */
int gallery_album_create(const char *name);
int gallery_album_delete(int id);
int gallery_album_rename(int id, const char *name);
int gallery_album_add_media(int album_id, int media_id);
int gallery_album_remove_media(int album_id, int media_id);
int gallery_album_get(int id, gal_album_t *album);
int gallery_album_list(gal_album_t *albums, int max);

/* 操作 */
int gallery_toggle_favorite(int id);
int gallery_toggle_hidden(int id);
int gallery_move_to_trash(int id);
int gallery_restore_from_trash(int id);
int gallery_empty_trash(void);
int gallery_share(int id, const char *target);
int gallery_set_wallpaper(int id);

/* 视图 */
int gallery_set_view_mode(int mode);
int gallery_set_sort_mode(int mode);
int gallery_navigate_album(int album_id);
int gallery_get_current_media(gal_media_t *media);
int gallery_next_media(void);
int gallery_prev_media(void);

/* 统计 */
int gallery_get_stats(gallery_app_t *stats);

#endif /* GALLERY_APP_H */
