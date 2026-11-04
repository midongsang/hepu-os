/* ============================================================================
 * 赫普OS - 手机模式 - 相机 + 相册应用
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

/* 共享函数 (定义在 mobile.c 中) */
extern void ui_draw_app_bg(void);
extern void ui_title(const char *zh, const char *en, int x);

/* 共享函数 (定义在 mobile_perm_mgr.c 中) */
extern int mobile_perm_request(const char *app, const char *pkg, int perm);
extern void mobile_perm_denied_page(const char *app, const char *perm);

/* V2.46: 相机应用 */
int cam_count = 0;
/* V2.80: BMP 抓帧缓冲 (14+40+1024+320*200=65578) */
uint8_t cam_bmp_buf[65578];

/* 前向声明 */
int album_cam_shots(void);

/* V2.80: 构造 photo_N.bmp 文件名 */
void camera_photo_name(int shot_no, char *out)
{
    int fi = 0;
    out[fi++] = 'p'; out[fi++] = 'h'; out[fi++] = 'o';
    out[fi++] = 't'; out[fi++] = 'o'; out[fi++] = '_';
    int nn = shot_no;
    char tmp[8]; int ti = 0;
    if (nn == 0) tmp[ti++] = '0';
    while (nn > 0) { tmp[ti++] = '0' + (nn % 10); nn /= 10; }
    while (ti > 0) out[fi++] = tmp[--ti];
    out[fi++] = '.'; out[fi++] = 'b'; out[fi++] = 'm'; out[fi++] = 'p';
    out[fi] = 0;
}

/* V2.80: 抓取显存帧 -> 生成 320x200 256色 BMP -> 落盘 /data/photos/photo_N.bmp */
int camera_save_bmp(int shot_no)
{
    extern int ide_ext2_write_raw(const char *, const char *, const char *, int);
    uint8_t *b = cam_bmp_buf;
    int w = 320, h = 200;
    uint32_t filesize = 14 + 40 + 1024 + (uint32_t)w * (uint32_t)h;
    /* BMP 文件头 (14B) */
    b[0] = 'B'; b[1] = 'M';
    b[2] = (uint8_t)filesize; b[3] = (uint8_t)(filesize >> 8);
    b[4] = (uint8_t)(filesize >> 16); b[5] = (uint8_t)(filesize >> 24);
    b[6] = b[7] = b[8] = b[9] = 0;
    b[10] = 54; b[11] = 0; b[12] = 0; b[13] = 0;
    /* DIB 头 (40B) */
    b[14] = 40; b[15] = b[16] = b[17] = 0;
    b[18] = (uint8_t)w; b[19] = (uint8_t)(w >> 8); b[20] = 0; b[21] = 0;
    b[22] = (uint8_t)h; b[23] = (uint8_t)(h >> 8); b[24] = 0; b[25] = 0;
    b[26] = 1; b[27] = 0;          /* planes */
    b[28] = 8; b[29] = 0;          /* 8 bpp */
    b[30] = b[31] = b[32] = b[33] = 0;   /* BI_RGB */
    uint32_t pix = (uint32_t)w * (uint32_t)h;
    b[34] = (uint8_t)pix; b[35] = (uint8_t)(pix >> 8);
    b[36] = (uint8_t)(pix >> 16); b[37] = (uint8_t)(pix >> 24);
    b[38] = b[39] = b[40] = b[41] = b[42] = b[43] = b[44] = b[45] = 0;
    b[46] = 0; b[47] = 0; b[48] = 1; b[49] = 0;   /* 256 色 */
    b[50] = b[51] = b[52] = b[53] = 0;
    /* 调色板 (1024B): VGA 标准 16 色循环 */
    static const uint8_t pal[48] = {
        0,0,0,  0,0,168,  0,168,0,  0,168,168,
        168,0,0,  168,0,168,  168,84,0,  168,168,168,
        84,84,84,  84,84,252,  84,252,84,  84,252,252,
        252,84,84,  252,84,252,  252,252,84,  252,252,252
    };
    int idx = 54;
    for (int i = 0; i < 256; i++) {
        int pi = (i % 16) * 3;
        b[idx++] = pal[pi];      /* B */
        b[idx++] = pal[pi + 1];  /* G */
        b[idx++] = pal[pi + 2];  /* R */
        b[idx++] = 0;
    }
    /* 像素 (64000B): 显存 0xA0000 读取, BMP bottom-up 行逆序 */
    volatile uint8_t *vga = (volatile uint8_t *)0xA0000;
    for (int y = 0; y < h; y++) {
        int src = (h - 1 - y) * w;
        for (int x = 0; x < w; x++)
            b[idx + y * w + x] = vga[src + x];
    }
    char fname[24];
    camera_photo_name(shot_no, fname);
    return ide_ext2_write_raw("/data/photos", fname, (const char *)b, (int)filesize);
}

void mobile_run_camera(void)
{
    /* V2.67: 相机权限门——拒绝则进入无权限提示 */
    if (!mobile_perm_request("camera", "com.hepu.camera", PM_PERM_CAMERA)) {
        mobile_perm_denied_page("camera", prm_perm_name(PM_PERM_CAMERA));
        return;
    }
    int running = 1;
    while (running) {
        ui_draw_app_bg();   /* V3.50: TRON 网格背景 */
        gfx_fillrect(0, 0, gfx_width, 14, mob_accent);
        gfx_puts(4, 3, "Back: ESC", 0x0F);
        /* V2.72: 相机使用中指示 (应用界面实时显示) */
        {
            int cuser = -1;
            prm_get_camera_in_use(&cuser);
            if (cuser >= 0) gfx_puts(gfx_width - 36, 3, "CAM", 0x0A);
        }
        ui_title("相机", "Camera", 18);
        /* 取景器: 渐变背景 + 网格线 */
        for (int y = 30; y < gfx_height - 50; y++) {
            int col = 0x01 + ((y / 8) % 3);  /* 模拟场景深浅交替 */
            gfx_hline(4, y, gfx_width - 8, col);
        }
        gfx_rect(4, 30, gfx_width - 8, gfx_height - 80, 0x0F);
        /* 九宫格线 */
        int fw = gfx_width - 8, fh = gfx_height - 80;
        gfx_vline(4 + fw / 3, 30, fh, 0x07);
        gfx_vline(4 + fw * 2 / 3, 30, fh, 0x07);
        gfx_hline(4, 30 + fh / 3, fw, 0x07);
        gfx_hline(4, 30 + fh * 2 / 3, fw, 0x07);
        /* 取景框中心 */
        gfx_rect(4 + fw / 2 - 10, 30 + fh / 2 - 10, 20, 20, 0x0E);
        /* 快门按钮 */
        gfx_fillrect(gfx_width / 2 - 14, gfx_height - 44, 28, 28, 0x0C);
        gfx_fillrect(gfx_width / 2 - 10, gfx_height - 40, 20, 20, 0x04);
        char cnt[16];
        int i = 0, n = cam_count;
        if (n == 0) cnt[i++] = '0';
        while (n > 0) { cnt[i++] = '0' + (n % 10); n /= 10; }
        cnt[i] = 0;
        /* 反转显示计数 */
        for (int j = 0; j < i / 2; j++) { char t = cnt[j]; cnt[j] = cnt[i-1-j]; cnt[i-1-j] = t; }
        gfx_puts(gfx_width / 2 + 20, gfx_height - 40, "Shots:", 0x0F);
        gfx_puts(gfx_width / 2 + 60, gfx_height - 40, cnt, 0x0E);
        gfx_flip();
        char kc;
        while (keyboard_try_getchar(&kc)) {
            if (kc == 0x1B) { running = 0; break; }
            else if (kc == '\n' || kc == '\r') {
                /* V2.80: 快门 = 抓帧 BMP 落盘 + 闪光 + 计数更新 */
                cam_count++;
                camera_save_bmp(cam_count);   /* 先抓当前取景器帧存盘 */
                gfx_fillrect(0, 0, gfx_width, gfx_height, 0x0F);  /* 闪光 */
                gfx_flip();
                for (volatile int f = 0; f < 800000; f++) ;
                extern int ide_ext2_writefile(const char *, const char *);
                extern int ide_ext2_sync(void);
                char nb[8];
                int ni = 0, n = cam_count;
                if (n == 0) nb[ni++] = '0';
                while (n > 0) { nb[ni++] = '0' + (n % 10); n /= 10; }
                for (int j = 0; j < ni / 2; j++) {
                    char t = nb[j]; nb[j] = nb[ni - 1 - j]; nb[ni - 1 - j] = t;
                }
                nb[ni] = '\n'; nb[ni + 1] = 0;
                ide_ext2_writefile("data/photos/photos.txt", nb);
                ide_ext2_sync();
            }
        }
        for (volatile int d = 0; d < 150000; d++) ;
    }
}

/* V2.83: 删除第 n 张照片: 后续照片前移重编号 + 计数减一落盘 */
int photo_delete(int n)
{
    extern int ide_ext2_delete(const char *, const char *);
    extern int ide_ext2_read_file(const char *, const char *, char *, int);
    extern int ide_ext2_write_raw(const char *, const char *, const char *, int);
    extern int ide_ext2_writefile(const char *, const char *);
    extern int ide_ext2_sync(void);
    int total = album_cam_shots();
    if (n < 1 || n > total) return -1;
    char src[24], dst[24];
    /* 后续照片前移: photo_{n+1}..photo_total -> photo_n..photo_{total-1} */
    for (int i = n + 1; i <= total; i++) {
        camera_photo_name(i, src);
        camera_photo_name(i - 1, dst);
        int rd = ide_ext2_read_file("/data/photos", src,
                                    (char *)cam_bmp_buf,
                                    (int)sizeof(cam_bmp_buf) - 1);
        if (rd >= 54 + 1024 + 64000)
            ide_ext2_write_raw("/data/photos", dst,
                               (const char *)cam_bmp_buf, rd);
        ide_ext2_delete("/data/photos", src);
    }
    if (n == total) {
        /* 删最后一张本体 */
        camera_photo_name(n, src);
        ide_ext2_delete("/data/photos", src);
    }
    /* 计数减一落盘 */
    {
        char nb[8];
        int ni = 0, m = total - 1;
        if (m == 0) nb[ni++] = '0';
        while (m > 0) { nb[ni++] = '0' + (m % 10); m /= 10; }
        for (int j = 0; j < ni / 2; j++) {
            char t = nb[j]; nb[j] = nb[ni - 1 - j]; nb[ni - 1 - j] = t;
        }
        nb[ni] = '\n'; nb[ni + 1] = 0;
        ide_ext2_writefile("data/photos/photos.txt", nb);
    }
    ide_ext2_sync();
    return 0;
}

/* V2.79: 读取磁盘相机照片计数 (相册联动) */
int album_cam_shots(void)
{
    extern int ide_ext2_read_file(const char *, const char *, char *, int);
    char buf[16];
    int n = ide_ext2_read_file("/data/photos", "photos.txt", buf, 15);
    if (n <= 0) return 0;
    buf[n] = 0;
    int v = 0;
    for (int i = 0; i < n && buf[i] >= '0' && buf[i] <= '9'; i++)
        v = v * 10 + (buf[i] - '0');
    return v;
}

/* V2.81: 相册照片浏览器——W/S 翻页浏览 photo_1..photo_N (从 shot_no 进入) */
void mobile_view_photo(int shot_no)
{
    extern int ide_ext2_read_file(const char *, const char *, char *, int);
    int total = album_cam_shots();
    if (total < 1) return;
    int cur = shot_no;
    if (cur < 1) cur = 1;
    if (cur > total) cur = total;
    int del_arm = 0;   /* V2.83: D 键删除二次确认 */
    int w = 320, h = 200;
    int running = 1;
    while (running) {
        char fname[24];
        camera_photo_name(cur, fname);
        int rd = ide_ext2_read_file("/data/photos", fname,
                                    (char *)cam_bmp_buf, (int)sizeof(cam_bmp_buf) - 1);
        int valid = (rd >= 54 + 1024 + 64000 &&
                     cam_bmp_buf[0] == 'B' && cam_bmp_buf[1] == 'M' &&
                     cam_bmp_buf[18] == 0x40 && cam_bmp_buf[19] == 0x01 &&
                     cam_bmp_buf[22] == 0xC8 && cam_bmp_buf[23] == 0x00);
        if (valid) {
            /* 照片像素 -> back_buffer (BMP bottom-up -> 顶行先) */
            uint8_t *fb = gfx_get_framebuffer();
            for (int y = 0; y < h; y++) {
                int dst = y * w;
                int src = 54 + 1024 + (h - 1 - y) * w;
                for (int x = 0; x < w; x++) fb[dst + x] = cam_bmp_buf[src + x];
            }
        } else {
            ui_draw_app_bg();   /* V3.50: TRON 网格背景 */
        }
        /* 标题栏: Photo cur/total */
        gfx_fillrect(0, 0, gfx_width, 14, mob_accent);
        gfx_puts(4, 3, "Back: ESC", 0x0F);
        gfx_puts((gfx_width - 60) / 2, 3, "Photo", 0x0B);
        {
            char sb[8];
            int fi = 0, nn = cur;
            char tmp[6]; int ti = 0;
            if (nn == 0) tmp[ti++] = '0';
            while (nn > 0) { tmp[ti++] = '0' + (nn % 10); nn /= 10; }
            while (ti > 0) sb[fi++] = tmp[--ti];
            sb[fi++] = '/';
            nn = total; ti = 0;
            if (nn == 0) tmp[ti++] = '0';
            while (nn > 0) { tmp[ti++] = '0' + (nn % 10); nn /= 10; }
            while (ti > 0) sb[fi++] = tmp[--ti];
            sb[fi] = 0;
            gfx_puts(gfx_width / 2 + 14, 3, sb, 0x0F);
        }
        gfx_puts(gfx_width - 150, gfx_height - 14,
                 del_arm ? "D again: DELETE!" : "W/S: prev/next  D: del", 
                 del_arm ? 0x0C : 0x08);
        gfx_flip();
        char kc;
        while (keyboard_try_getchar(&kc)) {
            if (kc == 0x1B) { running = 0; break; }
            else if (kc == 'w' || kc == 'W') { if (cur > 1) cur--; }
            else if (kc == 's' || kc == 'S') { if (cur < total) cur++; }
            else if (kc == 'd' || kc == 'D') {
                /* V2.83: 删除当前照片 (二次确认) */
                if (del_arm) {
                    photo_delete(cur);
                    total = album_cam_shots();
                    if (cur > total) cur = total;
                    if (total < 1) { running = 0; break; }
                    del_arm = 0;
                } else {
                    del_arm = 1;
                }
            }
        }
        for (volatile int d = 0; d < 150000; d++) ;
    }
}

/* V2.46: 相册应用 */
#define ALBUM_NUM 6
static const uint8_t album_colors[ALBUM_NUM] = {0x0A, 0x0B, 0x0C, 0x0E, 0x09, 0x0D};
const char *album_names[ALBUM_NUM] = {"P1", "P2", "P3", "P4", "P5", "P6"};
int album_open = -1;
/* V2.82: 真实照片缩略图缓存 (320x200 BMP -> 网格缩略图) */
#define ALBUM_THUMB_W 98
#define ALBUM_THUMB_H 50
uint8_t album_thumbs[ALBUM_NUM][ALBUM_THUMB_W * ALBUM_THUMB_H];
int album_thumb_valid[ALBUM_NUM];

/* V2.82: 从磁盘构建前 N 张照片的缩略图 (进入相册时调用一次) */
void album_build_thumbs(void)
{
    extern int ide_ext2_read_file(const char *, const char *, char *, int);
    int total = album_cam_shots();
    for (int i = 0; i < ALBUM_NUM; i++) album_thumb_valid[i] = 0;
    for (int i = 1; i <= total && i <= ALBUM_NUM; i++) {
        char fname[24];
        camera_photo_name(i, fname);
        int rd = ide_ext2_read_file("/data/photos", fname,
                                    (char *)cam_bmp_buf,
                                    (int)sizeof(cam_bmp_buf) - 1);
        if (rd < 54 + 1024 + 64000) continue;
        if (cam_bmp_buf[0] != 'B' || cam_bmp_buf[1] != 'M') continue;
        if (cam_bmp_buf[18] != 0x40 || cam_bmp_buf[19] != 0x01) continue;
        if (cam_bmp_buf[22] != 0xC8 || cam_bmp_buf[23] != 0x00) continue;
        int w = 320, h = 200;
        uint8_t *dst = album_thumbs[i - 1];
        int dw = ALBUM_THUMB_W, dh = ALBUM_THUMB_H;
        for (int y = 0; y < dh; y++) {
            int sy = y * h / dh;
            for (int x = 0; x < dw; x++) {
                int sx = x * w / dw;
                dst[y * dw + x] = cam_bmp_buf[54 + 1024 + (h - 1 - sy) * w + sx];
            }
        }
        album_thumb_valid[i - 1] = 1;
    }
}

void mobile_run_album(void)
{
    /* V2.67: 相册权限门 (STORAGE) */
    if (!mobile_perm_request("gallery", "com.hepu.gallery", PM_PERM_STORAGE)) {
        mobile_perm_denied_page("gallery", prm_perm_name(PM_PERM_STORAGE));
        return;
    }
    int running = 1;
    int sel = 0;
    album_build_thumbs();   /* V2.82: 进入相册时构建真实照片缩略图 */
    while (running) {
        ui_draw_app_bg();   /* V3.50: TRON 网格背景 */
        gfx_fillrect(0, 0, gfx_width, 14, mob_accent);
        gfx_puts(4, 3, "Back: ESC", 0x0F);
        if (album_open >= 0) {
            /* 放大查看单张 */
            gfx_puts_zh((gfx_width - 48) / 2, 18, album_names[album_open], 0x0B);
            gfx_fillrect(8, 30, gfx_width - 16, gfx_height - 80, album_colors[album_open]);
            gfx_rect(8, 30, gfx_width - 16, gfx_height - 80, 0x0F);
            gfx_puts(8, gfx_height - 30, "ESC: back to grid", 0x08);
        } else {
            ui_title("相册", "Album", 18);
            /* 3x2 缩略图网格 */
            int tw = (gfx_width - 24) / 3;
            int th = (gfx_height - 100) / 2;
            for (int i = 0; i < ALBUM_NUM; i++) {
                int gx = 8 + (i % 3) * (tw + 4);
                int gy = 32 + (i / 3) * (th + 4);
                if (album_thumb_valid[i]) {
                    /* V2.82: 真实照片缩略图 */
                    uint8_t *t = album_thumbs[i];
                    for (int y = 0; y < th && y < ALBUM_THUMB_H; y++)
                        for (int x = 0; x < tw && x < ALBUM_THUMB_W; x++)
                            gfx_putpixel(gx + x, gy + y,
                                         t[y * ALBUM_THUMB_W + x]);
                    gfx_rect(gx, gy, tw, th, (i == sel) ? 0x0F : 0x08);
                    gfx_fillrect(gx, gy + th - 12, tw, 12, 0x00);
                    gfx_puts(gx + 4, gy + th - 10, album_names[i], 0x0F);
                } else {
                    gfx_fillrect(gx, gy, tw, th, album_colors[i]);
                    gfx_rect(gx, gy, tw, th, (i == sel) ? 0x0F : 0x08);
                    gfx_puts(gx + 4, gy + th - 12, album_names[i], 0x0F);
                }
            }
            gfx_puts(8, 32 + 2 * (th + 4) + 2, "W/S sel  V: last  X: del  Enter view", 0x08);
            /* V2.79: 相机联动——磁盘照片计数 */
            int shots = album_cam_shots();
            if (shots > 0) {
                char sb[24];
                int si = 0;
                sb[si++] = 'C'; sb[si++] = ':'; sb[si++] = ' ';
                int sn = shots;
                char tmp[6]; int ti = 0;
                if (sn == 0) tmp[ti++] = '0';
                while (sn > 0) { tmp[ti++] = '0' + (sn % 10); sn /= 10; }
                while (ti > 0) sb[si++] = tmp[--ti];
                sb[si++] = ' '; sb[si++] = 's'; sb[si++] = 'h'; sb[si++] = 'o'; sb[si++] = 't'; sb[si++] = 's';
                sb[si] = 0;
                gfx_puts(gfx_width - 96, gfx_height - 14, sb, 0x0A);
            }
        }
        gfx_flip();
        char kc;
        while (keyboard_try_getchar(&kc)) {
            if (kc == 0x1B) {
                if (album_open >= 0) album_open = -1;
                else { running = 0; break; }
            }
            if (album_open >= 0) {
                /* V3.65: 查看态 X 删除当前照片 */
                if (kc == 'x' || kc == 'X') {
                    photo_delete(album_open + 1);
                    album_open = -1;
                    album_build_thumbs();
                }
                continue;
            }
            if (kc == 'w' || kc == 'W') { if (sel >= 3) sel -= 3; }
            else if (kc == 's' || kc == 'S') { if (sel < ALBUM_NUM - 3) sel += 3; }
            else if (kc == 'a' || kc == 'A') { if (sel > 0) sel--; }
            else if (kc == 'd' || kc == 'D') { if (sel < ALBUM_NUM - 1) sel++; }
            else if (kc == 'x' || kc == 'X') {
                /* V3.65: 网格态 X 删除选中照片 (照片前移重编号+落盘) */
                if (sel + 1 <= album_cam_shots()) {
                    photo_delete(sel + 1);
                    album_build_thumbs();
                }
            }
            else if (kc == '\n' || kc == '\r') {
                /* V2.82: 真实照片槽 Enter 直接查看 BMP; 空槽保持颜色块 */
                if (album_thumb_valid[sel]) {
                    mobile_view_photo(sel + 1);
                    album_build_thumbs();   /* V2.83: 删除后重建缩略图 */
                }
                else album_open = sel;
            }
            else if (kc == 'v' || kc == 'V') {
                /* V2.80: 查看最近拍摄的照片 */
                int shots = album_cam_shots();
                if (shots > 0) {
                    mobile_view_photo(shots);
                    album_build_thumbs();   /* V2.83: 删除后重建缩略图 */
                }
                else {
                    gfx_puts(4, gfx_height - 14, "no photos yet", 0x0C);
                    gfx_flip();
                    for (volatile int d = 0; d < 900000; d++) ;
                }
            }
        }
        for (volatile int d = 0; d < 150000; d++) ;
    }
}
