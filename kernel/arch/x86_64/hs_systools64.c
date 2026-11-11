/* ============================================================================
 * 赫普OS - 系统工具窗口 (V0.55)
 * 设备管理器 (PCI/USB/驱动) / 磁盘管理 (容量/分区/挂载/fsck) / 日志查看器
 * 渲染由 hpdesk 窗口系统调用 (w->render), 键鼠经 hpdesk 转发
 * ========================================================================== */

#include "hpdesk.h"
#include "hs_systools.h"
#include "hsinput.h"
#include "klog64.h"
#include "../../shared/drivers/driver_framework.h"

/* 外部图形/系统接口 */
extern void gfx64_fill_rect(int x, int y, int w, int h, uint32_t color);
extern void gfx64_fill_rounded_rect(int x, int y, int w, int h, int radius, uint32_t color);
extern void gfx64_draw_rounded_rect(int x, int y, int w, int h, int radius, uint32_t color);
extern void font64_draw_string(void *fb, int fw, int fh,
                               int x, int y, const char *str,
                               uint32_t color, uint16_t size);
extern void *gfx64_get_framebuffer(void);
extern int strcmp(const char *a, const char *b);
extern char *strcpy(char *d, const char *s);
extern hs_window_t *hs_window_get_focused(void);
extern uint32_t pci_config_read32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset);

/* 存储/USB/文件系统 */
extern int storage_hal_get_info(uint32_t *total_sectors, uint32_t *sector_size);
extern int storage_hal_is_initialized(void);
extern int storage_hal_get_device(void);
extern int usb64_dev_count(void);
extern int usb64_storage_present(void);
extern int usb64_kbd_available(void);
extern int usb64_mouse_available(void);
extern int usb64_mount_usb(void);
extern int usb64_umount_usb(void);
extern int ext2_64_is_mounted(void);
extern int ext2_64_get_fs_info_ex(uint64_t *total_blocks, uint64_t *free_blocks,
                                  uint32_t *block_size, uint64_t *total_inodes,
                                  uint64_t *free_inodes, uint32_t *groups,
                                  char *volume_name);
extern int disk64_fsck(void);

/* ============================================================================
 * 通用工具
 * ========================================================================== */
static void st_text(int x, int y, const char *s, uint32_t c, int sz)
{
    font64_draw_string(gfx64_get_framebuffer(), g_shell.screen_width,
                       g_shell.screen_height, x, y, s, c, (uint16_t)sz);
}

static void st_itoa(int v, char *buf)
{
    int a = v < 0 ? -v : v;
    char tmp[12];
    int n = 0, i;
    do { tmp[n++] = '0' + (a % 10); a /= 10; } while (a);
    if (v < 0) tmp[n++] = '-';
    for (i = 0; i < n; i++) buf[i] = tmp[n - 1 - i];
    buf[n] = 0;
}

static void st_uitoa(uint64_t v, char *buf)
{
    char tmp[24];
    int n = 0, i;
    if (v == 0) { buf[0] = '0'; buf[1] = 0; return; }
    while (v > 0) { tmp[n++] = '0' + (int)(v % 10); v /= 10; }
    for (i = 0; i < n; i++) buf[i] = tmp[n - 1 - i];
    buf[n] = 0;
}

static void st_hex(uint32_t v, int digits, char *buf)
{
    int i;
    for (i = digits - 1; i >= 0; i--) {
        int d = (int)((v >> (i * 4)) & 0xF);
        buf[digits - 1 - i] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
    }
    buf[digits] = 0;
}

static int st_len(const char *s) { int n = 0; while (s[n]) n++; return n; }

static void st_append(char *dst, const char *s)
{
    int n = st_len(dst);
    while (*s) dst[n++] = *s++;
    dst[n] = 0;
}

static void st_pad(char *dst, int col)
{
    int n = st_len(dst);
    while (n < col) dst[n++] = ' ';
    dst[n] = 0;
}

static void st_copyn(char *d, const char *s, int max)
{
    int i = 0;
    while (s[i] && i < max) { d[i] = s[i]; i++; }
    d[i] = 0;
}

/* 内容区原点 (标题栏 26px) */
static void st_content(hs_window_t *w, int *cx, int *cy, int *cw, int *ch)
{
    *cx = w->x + 8;
    *cy = w->y + 26;
    *cw = w->width - 16;
    *ch = w->height - 26 - 8;
}

/* ============================================================================
 * 设备管理器
 * ========================================================================== */
#define ST_PCI_MAX 40

typedef struct {
    uint8_t  bus, dev, func, cls;
    uint16_t vendor, device;
} st_pci_dev_t;

static st_pci_dev_t st_pci[ST_PCI_MAX];
static int st_pci_n;
static int st_drv_n;
static int st_drv_active;
static int st_usb_n;
static int st_usb_storage;
static int st_usb_kbd;
static int st_usb_mouse;

static hs_window_t *st_dev_win;
static int st_dev_tab;
static int st_dev_scroll[3];

static const char *st_class_name(uint8_t c)
{
    switch (c) {
        case 0x00: return "legacy";
        case 0x01: return "storage";
        case 0x02: return "network";
        case 0x03: return "display";
        case 0x04: return "multimedia";
        case 0x05: return "memory";
        case 0x06: return "bridge";
        case 0x07: return "comm";
        case 0x08: return "system";
        case 0x09: return "serial";
        case 0x0A: return "wireless";
        case 0x0C: return "bridge";
        case 0x0D: return "sensor";
        default:   return "other";
    }
}

static void st_pci_scan(void)
{
    int bus, dev, func;
    st_pci_n = 0;

    for (bus = 0; bus < 32 && st_pci_n < ST_PCI_MAX; bus++) {
        for (dev = 0; dev < 32 && st_pci_n < ST_PCI_MAX; dev++) {
            uint32_t hdr;
            uint16_t vendor;
            int nfunc = 1;

            vendor = (uint16_t)(pci_config_read32((uint8_t)bus, (uint8_t)dev, 0, 0) & 0xFFFF);
            if (vendor == 0xFFFF || vendor == 0) continue;

            hdr = pci_config_read32((uint8_t)bus, (uint8_t)dev, 0, 0x0C);
            if (hdr & 0x00800000) nfunc = 8;   /* multi-function bit */

            for (func = 0; func < nfunc && st_pci_n < ST_PCI_MAX; func++) {
                uint32_t vd = pci_config_read32((uint8_t)bus, (uint8_t)dev, (uint8_t)func, 0);
                uint32_t cc = pci_config_read32((uint8_t)bus, (uint8_t)dev, (uint8_t)func, 8);
                if ((vd & 0xFFFF) == 0xFFFF || (vd & 0xFFFF) == 0) continue;

                st_pci[st_pci_n].bus = (uint8_t)bus;
                st_pci[st_pci_n].dev = (uint8_t)dev;
                st_pci[st_pci_n].func = (uint8_t)func;
                st_pci[st_pci_n].cls = (uint8_t)(cc >> 24);
                st_pci[st_pci_n].vendor = (uint16_t)(vd & 0xFFFF);
                st_pci[st_pci_n].device = (uint16_t)(vd >> 16);
                st_pci_n++;
            }
        }
    }
}

static void st_dev_refresh(void)
{
    st_pci_scan();
    st_drv_n = drv_get_count();
    st_drv_active = drv_get_active_count();
    st_usb_n = usb64_dev_count();
    st_usb_storage = usb64_storage_present();
    st_usb_kbd = usb64_kbd_available();
    st_usb_mouse = usb64_mouse_available();
}

static int st_dev_rows(hs_window_t *w)
{
    int cx, cy, cw, ch;
    st_content(w, &cx, &cy, &cw, &ch);
    return (ch - 34 - 24) / 15;
}

static void st_dev_render(hs_window_t *w)
{
    static const char *tabs[3] = {"PCI", "USB", "Drivers"};
    int cx, cy, cw, ch, i, rows, tc;
    char line[112], tmp[24];
    void *fb = gfx64_get_framebuffer();
    int W = g_shell.screen_width, H = g_shell.screen_height;

    st_content(w, &cx, &cy, &cw, &ch);

    /* 标签栏 */
    for (i = 0; i < 3; i++) {
        int tx = cx + i * 88;
        int active = (i == st_dev_tab);
        gfx64_fill_rounded_rect(tx, cy + 4, 80, 22, 4,
                                active ? g_shell.colors.accent : g_shell.colors.panel_bg);
        st_text(tx + 24, cy + 8, tabs[i],
                active ? 0x2B2930 : g_shell.colors.panel_fg, 12);
    }

    rows = st_dev_rows(w);
    if (rows < 1) rows = 1;
    tc = g_shell.colors.text_secondary;

    if (st_dev_tab == 0) {
        /* PCI 设备 */
        int maxs = st_pci_n - rows;
        if (maxs < 0) maxs = 0;
        if (st_dev_scroll[0] > maxs) st_dev_scroll[0] = maxs;
        if (st_dev_scroll[0] < 0) st_dev_scroll[0] = 0;

        st_text(cx, cy + 32, "BUS:DEV.FN  VENDOR:DEVICE  CLASS", tc, 11);
        for (i = 0; i < rows; i++) {
            int idx = st_dev_scroll[0] + i;
            if (idx >= st_pci_n) break;
            st_hex(st_pci[idx].bus, 2, tmp);
            st_copyn(line, tmp, 111);
            st_append(line, ":");
            st_hex(st_pci[idx].dev, 2, tmp);
            st_append(line, tmp);
            st_append(line, ".");
            st_hex(st_pci[idx].func, 1, tmp);
            st_append(line, tmp);
            st_pad(line, 12);
            st_hex(st_pci[idx].vendor, 4, tmp);
            st_append(line, tmp);
            st_append(line, ":");
            st_hex(st_pci[idx].device, 4, tmp);
            st_append(line, tmp);
            st_pad(line, 26);
            st_append(line, st_class_name(st_pci[idx].cls));
            font64_draw_string(fb, W, H, cx, cy + 48 + i * 15, line,
                               g_shell.colors.window_fg, 11);
        }
        st_text(cx, cy + ch - 16, "R=Refresh  Tab=Next tab  Up/Down=Scroll", tc, 10);
    } else if (st_dev_tab == 1) {
        /* USB 子系统 */
        int y = cy + 40;
        line[0] = 0;
        st_append(line, "USB devices detected: ");
        st_itoa(st_usb_n, line + st_len(line));
        st_text(cx, y, line, g_shell.colors.window_fg, 12); y += 20;

        st_copyn(line, "Mass storage: ", 60);
        st_append(line, st_usb_storage ? "present" : "not present");
        st_text(cx, y, line, g_shell.colors.window_fg, 12); y += 20;

        st_copyn(line, "Storage backend: ", 60);
        st_append(line, storage_hal_get_device() == 1 ? "USB (mounted)" : "ATA/IDE");
        st_text(cx, y, line, g_shell.colors.window_fg, 12); y += 20;

        st_copyn(line, "USB keyboard: ", 60);
        st_append(line, st_usb_kbd ? "available" : "not detected");
        st_text(cx, y, line, g_shell.colors.window_fg, 12); y += 20;

        st_copyn(line, "USB mouse: ", 60);
        st_append(line, st_usb_mouse ? "available" : "not detected");
        st_text(cx, y, line, g_shell.colors.window_fg, 12); y += 20;

        st_copyn(line, "Mount state: ", 60);
        st_append(line, storage_hal_get_device() == 1 ? "USB active" : "internal disk active");
        st_text(cx, y, line, g_shell.colors.window_fg, 12); y += 20;

        st_text(cx, y + 10, "Disk Manager: mount/unmount USB storage", tc, 11);
        st_text(cx, cy + ch - 16, "R=Refresh  Tab=Next tab", tc, 10);
    } else {
        /* 驱动列表 */
        drv_desc_t *descs[DRV_MAX_COUNT];
        static const char *cat_names[] = {
            "storage", "network", "input", "display", "audio", "usb",
            "sensor", "camera", "timer", "rtc", "irq", "bus", "other"
        };
        static const char *state_names[] = {
            "UNLOAD", "LOADED", "PROBED", "ACTIVE", "SUSP", "ERROR"
        };
        int n = drv_get_all(descs, DRV_MAX_COUNT);
        int maxs;

        st_copyn(line, "Drivers: ", 60);
        st_itoa(n, line + st_len(line));
        st_append(line, "  active: ");
        st_itoa(st_drv_active, line + st_len(line));
        st_text(cx, cy + 32, line, tc, 11);

        maxs = n - rows;
        if (maxs < 0) maxs = 0;
        if (st_dev_scroll[2] > maxs) st_dev_scroll[2] = maxs;
        if (st_dev_scroll[2] < 0) st_dev_scroll[2] = 0;

        for (i = 0; i < rows; i++) {
            int idx = st_dev_scroll[2] + i;
            if (idx >= n) break;
            st_copyn(line, descs[idx]->name ? descs[idx]->name : "?", 17);
            st_pad(line, 18);
            if (descs[idx]->category >= 0 && descs[idx]->category < DRV_CATEGORY_MAX)
                st_append(line, cat_names[descs[idx]->category]);
            st_pad(line, 30);
            if (descs[idx]->state >= 0 && descs[idx]->state <= DRV_STATE_ERROR)
                st_append(line, state_names[descs[idx]->state]);
            font64_draw_string(fb, W, H, cx, cy + 50 + i * 15, line,
                               descs[idx]->state == DRV_STATE_ACTIVE ?
                               0x52C41A : g_shell.colors.window_fg, 11);
        }
        st_text(cx, cy + ch - 16, "R=Refresh  Tab=Next tab  Up/Down=Scroll", tc, 10);
    }
}

/* ============================================================================
 * 磁盘管理
 * ========================================================================== */
typedef struct {
    int      present;
    uint8_t  type;
    uint32_t lba_start;
    uint32_t sectors;
    int      fs_ext2;
} st_partition_t;

extern int partition64_scan(st_partition_t *parts);

static hs_window_t *st_disk_win;
static int st_disk_btn[3][4];      /* x,y,w,h: Mount / Unmount / fsck */
static char st_disk_msg[64];
static st_partition_t st_parts[4];
static int st_part_ok;

static void st_disk_refresh(void)
{
    st_part_ok = (partition64_scan(st_parts) == 0);
    st_copyn(st_disk_msg, "Ready", 63);
}

static void st_disk_rescan(void)
{
    st_part_ok = (partition64_scan(st_parts) == 0);
}

static void st_disk_button(int bx, int by, int bw, int bh, const char *label)
{
    gfx64_fill_rounded_rect(bx, by, bw, bh, 4, g_shell.colors.panel_bg);
    gfx64_draw_rounded_rect(bx, by, bw, bh, 4, g_shell.colors.border);
    st_text(bx + 10, by + 5, label, g_shell.colors.panel_fg, 12);
}

static void st_disk_render(hs_window_t *w)
{
    int cx, cy, cw, ch, y, i;
    char line[128], tmp[24];
    void *fb = gfx64_get_framebuffer();
    int W = g_shell.screen_width, H = g_shell.screen_height;
    uint32_t sectors = 0, ssize = 0;
    uint64_t total_b = 0, free_b = 0;
    uint32_t bsize = 0, groups = 0;
    uint64_t tinodes = 0, finodes = 0;
    char vol[24];
    int have_fs, i2;

    st_content(w, &cx, &cy, &cw, &ch);
    y = cy + 6;

    /* 存储设备总览 */
    storage_hal_get_info(&sectors, &ssize);
    if (ssize == 0) ssize = 512;
    if (sectors == 0 && st_part_ok) {
        /* HAL 未提供容量: 用 MBR 分区末端估算磁盘大小 */
        for (i2 = 0; i2 < 4; i2++) {
            uint32_t end;
            if (!st_parts[i2].present) continue;
            end = st_parts[i2].lba_start + st_parts[i2].sectors;
            if (end > sectors) sectors = end;
        }
    }
    st_copyn(line, "Storage: ", 64);
    st_append(line, storage_hal_is_initialized() ? "initialized" : "not initialized");
    st_append(line, "  backend=");
    st_append(line, storage_hal_get_device() == 1 ? "USB" : "ATA");
    st_text(cx, y, line, g_shell.colors.window_fg, 12); y += 19;

    st_copyn(line, "Disk capacity: ", 64);
    st_uitoa(sectors, tmp);
    st_append(line, tmp);
    st_append(line, " sectors x ");
    st_uitoa(ssize, tmp);
    st_append(line, tmp);
    st_append(line, " bytes = ");
    st_uitoa(((uint64_t)sectors * ssize) >> 20, tmp);
    st_append(line, tmp);
    st_append(line, " MB");
    st_text(cx, y, line, g_shell.colors.window_fg, 12); y += 19;

    /* ext2 根卷 */
    have_fs = ext2_64_is_mounted();
    vol[0] = 0;
    if (have_fs) {
        ext2_64_get_fs_info_ex(&total_b, &free_b, &bsize, &tinodes,
                               &finodes, &groups, vol);
    }
    st_copyn(line, "Root volume: ", 64);
    st_append(line, have_fs ? "ext2 mounted" : "not mounted");
    if (have_fs) {
        st_append(line, "  name=\"");
        st_append(line, vol);
        st_append(line, "\"");
    }
    st_text(cx, y, line, g_shell.colors.window_fg, 12); y += 19;

    if (have_fs) {
        uint64_t tot_mb = (total_b * bsize) >> 20;
        uint64_t free_mb = (free_b * bsize) >> 20;
        uint64_t used_mb = tot_mb - free_mb;
        int pct = (total_b > 0) ? (int)((used_mb * 100) / (tot_mb ? tot_mb : 1)) : 0;

        st_copyn(line, "FS: total ", 64);
        st_uitoa(tot_mb, tmp); st_append(line, tmp); st_append(line, " MB");
        st_append(line, "   free "); st_uitoa(free_mb, tmp); st_append(line, tmp);
        st_append(line, " MB   used "); st_uitoa(used_mb, tmp); st_append(line, tmp);
        st_append(line, " MB (");
        st_itoa(pct, tmp); st_append(line, tmp);
        st_append(line, "%)");
        st_text(cx, y, line, g_shell.colors.window_fg, 12); y += 19;

        st_copyn(line, "Inodes: total ", 64);
        st_uitoa(tinodes, tmp); st_append(line, tmp);
        st_append(line, "  free "); st_uitoa(finodes, tmp); st_append(line, tmp);
        st_append(line, "  groups "); st_uitoa(groups, tmp); st_append(line, tmp);
        st_text(cx, y, line, g_shell.colors.text_secondary, 11); y += 19;

        /* 使用率进度条 */
        gfx64_fill_rect(cx, y, cw, 8, g_shell.colors.panel_bg);
        if (pct > 100) pct = 100;
        gfx64_fill_rect(cx, y, (cw * pct) / 100, 8, g_shell.colors.accent);
        y += 16;
    } else {
        y += 19;
    }

    /* MBR 分区表 */
    st_text(cx, y, "MBR partition table:", g_shell.colors.text_secondary, 11); y += 17;
    if (st_part_ok) {
        int any = 0;
        for (i = 0; i < 4; i++) {
            if (!st_parts[i].present) continue;
            any = 1;
            st_copyn(line, "P", 64);
            st_itoa(i + 1, line + st_len(line));
            st_append(line, "  type=0x");
            st_hex(st_parts[i].type, 2, tmp);
            st_append(line, tmp);
            st_append(line, "  LBA=");
            st_uitoa(st_parts[i].lba_start, tmp);
            st_append(line, tmp);
            st_append(line, "  size=");
            st_uitoa(((uint64_t)st_parts[i].sectors * ssize) >> 20, tmp);
            st_append(line, tmp);
            st_append(line, " MB  ext2=");
            st_append(line, st_parts[i].fs_ext2 ? "yes" : "no");
            st_text(cx, y, line, g_shell.colors.window_fg, 11); y += 16;
        }
        if (!any) { st_text(cx, y, "(no partitions)", g_shell.colors.text_secondary, 11); y += 16; }
    } else {
        st_text(cx, y, "(MBR read failed)", g_shell.colors.text_secondary, 11); y += 16;
    }

    /* 操作按钮 */
    {
        int by = w->y + w->height - 40;
        int bw = 120, gap = 8;
        int bx = cx;
        st_disk_button(bx, by, bw, 26, "Mount USB");
        st_disk_btn[0][0] = bx; st_disk_btn[0][1] = by;
        st_disk_btn[0][2] = bw; st_disk_btn[0][3] = 26;
        bx += bw + gap;
        st_disk_button(bx, by, bw, 26, "Unmount USB");
        st_disk_btn[1][0] = bx; st_disk_btn[1][1] = by;
        st_disk_btn[1][2] = bw; st_disk_btn[1][3] = 26;
        bx += bw + gap;
        st_disk_button(bx, by, 100, 26, "Run fsck");
        st_disk_btn[2][0] = bx; st_disk_btn[2][1] = by;
        st_disk_btn[2][2] = 100; st_disk_btn[2][3] = 26;

        st_text(cx, by - 20, st_disk_msg, g_shell.colors.text_secondary, 11);
        st_text(bx + 110, by + 7, "R=Refresh", g_shell.colors.text_secondary, 10);
    }
    (void)fb; (void)W; (void)H;
}

/* ============================================================================
 * 日志查看器
 * ========================================================================== */
#define ST_LOG_TEXT_MAX  4096
#define ST_LOG_MAX_LINES 256

static hs_window_t *st_log_win;
static char st_log_text[ST_LOG_TEXT_MAX];
static int  st_log_offs[ST_LOG_MAX_LINES];
static int  st_log_lines;
static int  st_log_scroll;   /* 向上滚动的行数, 0=最新 */

static void st_log_refresh(void)
{
    int i = 0;
    st_log_lines = 0;
    klog64_snapshot(st_log_text, ST_LOG_TEXT_MAX);
    while (st_log_text[i] && st_log_lines < ST_LOG_MAX_LINES) {
        int j = i;
        while (st_log_text[j] && st_log_text[j] != '\n') j++;
        st_log_offs[st_log_lines++] = i;
        i = (st_log_text[j] == '\n') ? j + 1 : j;
    }
}

static int st_log_rows(hs_window_t *w)
{
    int cx, cy, cw, ch;
    st_content(w, &cx, &cy, &cw, &ch);
    return (ch - 30) / 14;
}

static void st_log_render(hs_window_t *w)
{
    int cx, cy, cw, ch, rows, i, start;
    char line[96];
    void *fb = gfx64_get_framebuffer();
    int W = g_shell.screen_width, H = g_shell.screen_height;

    st_content(w, &cx, &cy, &cw, &ch);
    rows = st_log_rows(w);
    if (rows < 1) rows = 1;

    st_copyn(line, "Kernel Log  ", 80);
    st_itoa(st_log_lines, line + st_len(line));
    st_append(line, " lines  ");
    st_uitoa(klog64_used(), line + st_len(line));
    st_append(line, " bytes");
    st_text(cx, cy + 4, line, g_shell.colors.text_secondary, 11);

    if (st_log_scroll > st_log_lines - rows) st_log_scroll = st_log_lines - rows;
    if (st_log_scroll < 0) st_log_scroll = 0;
    start = st_log_lines - rows - st_log_scroll;
    if (start < 0) start = 0;

    for (i = 0; i < rows; i++) {
        int idx = start + i;
        if (idx < 0 || idx >= st_log_lines) break;
        st_copyn(line, st_log_text + st_log_offs[idx], 82);
        font64_draw_string(fb, W, H, cx, cy + 24 + i * 14,
                           line, g_shell.colors.window_fg, 10);
    }

    st_text(cx, cy + ch - 14, "R=Refresh  C=Clear  Up/Down/PgUp/PgDn=Scroll",
            g_shell.colors.text_secondary, 10);
    (void)cw;
}

/* ============================================================================
 * 窗口创建
 * ========================================================================== */
hs_window_t *hs_devmgr_create(void)
{
    if (st_dev_win && st_dev_win->visible) { st_dev_win->focus = 1; return st_dev_win; }
    st_dev_win = hs_window_create(HS_WINDOW_FLOATING, "Device Manager", -1, -1, 560, 400);
    if (st_dev_win) {
        st_dev_win->render = st_dev_render;
        st_dev_tab = 0;
        st_dev_scroll[0] = st_dev_scroll[1] = st_dev_scroll[2] = 0;
        st_dev_refresh();
        klog64_log("[SYSTOOLS] Device Manager opened");
    }
    return st_dev_win;
}

hs_window_t *hs_diskmgr_create(void)
{
    if (st_disk_win && st_disk_win->visible) { st_disk_win->focus = 1; return st_disk_win; }
    st_disk_win = hs_window_create(HS_WINDOW_FLOATING, "Disk Manager", -1, -1, 560, 420);
    if (st_disk_win) {
        st_disk_win->render = st_disk_render;
        st_disk_refresh();
        klog64_log("[SYSTOOLS] Disk Manager opened");
    }
    return st_disk_win;
}

hs_window_t *hs_logview_create(void)
{
    if (st_log_win && st_log_win->visible) { st_log_win->focus = 1; return st_log_win; }
    st_log_win = hs_window_create(HS_WINDOW_FLOATING, "Log Viewer", -1, -1, 620, 420);
    if (st_log_win) {
        st_log_win->render = st_log_render;
        st_log_scroll = 0;
        st_log_refresh();
        klog64_log("[SYSTOOLS] Log Viewer opened");
    }
    return st_log_win;
}

/* ============================================================================
 * 输入分发
 * ========================================================================== */
int hs_systools_handle_key(int key)
{
    hs_window_t *foc = hs_window_get_focused();
    if (!foc) return 0;

    if (st_dev_win && st_dev_win->visible && foc == st_dev_win) {
        if (key == '\t' || key == 0x0F) {
            st_dev_tab = (st_dev_tab + 1) % 3;
            return 1;
        }
        if (key == 'r' || key == 'R') { st_dev_refresh(); return 1; }
        if (key == KEY_EXT_UP) {
            if (st_dev_scroll[st_dev_tab] > 0) st_dev_scroll[st_dev_tab]--;
            return 1;
        }
        if (key == KEY_EXT_DOWN) { st_dev_scroll[st_dev_tab]++; return 1; }
        if (key == KEY_EXT_PGUP) {
            int r = st_dev_rows(st_dev_win);
            st_dev_scroll[st_dev_tab] -= r;
            if (st_dev_scroll[st_dev_tab] < 0) st_dev_scroll[st_dev_tab] = 0;
            return 1;
        }
        if (key == KEY_EXT_PGDN) {
            st_dev_scroll[st_dev_tab] += st_dev_rows(st_dev_win);
            return 1;
        }
        if (key >= 0x20 && key < 0x7F) return 1;   /* 吞掉其它按键 */
        return 1;
    }

    if (st_disk_win && st_disk_win->visible && foc == st_disk_win) {
        if (key == 'r' || key == 'R') { st_disk_refresh(); return 1; }
        if (key == 'm' || key == 'M') {
            int rc = usb64_mount_usb();
            st_copyn(st_disk_msg, rc == 0 ? "USB mounted" : "Mount failed (no USB storage)", 63);
            st_disk_rescan();
            return 1;
        }
        if (key == 'u' || key == 'U') {
            int rc = usb64_umount_usb();
            st_copyn(st_disk_msg, rc == 0 ? "USB unmounted" : "Unmount failed", 63);
            st_disk_rescan();
            return 1;
        }
        if (key == 'f' || key == 'F') {
            int rc = disk64_fsck();
            st_copyn(st_disk_msg, rc == 0 ? "fsck: PASS" : "fsck: issues/failed", 63);
            return 1;
        }
        if (key >= 0x20 && key < 0x7F) return 1;
        return 1;
    }

    if (st_log_win && st_log_win->visible && foc == st_log_win) {
        if (key == 'r' || key == 'R') { st_log_refresh(); return 1; }
        if (key == 'c' || key == 'C') {
            klog64_clear();
            st_log_scroll = 0;
            st_log_refresh();
            klog64_log("[SYSTOOLS] Log cleared by user");
            st_log_refresh();
            return 1;
        }
        if (key == KEY_EXT_UP) { st_log_scroll++; return 1; }
        if (key == KEY_EXT_DOWN) { if (st_log_scroll > 0) st_log_scroll--; return 1; }
        if (key == KEY_EXT_PGUP) { st_log_scroll += st_log_rows(st_log_win); return 1; }
        if (key == KEY_EXT_PGDN) {
            st_log_scroll -= st_log_rows(st_log_win);
            if (st_log_scroll < 0) st_log_scroll = 0;
            return 1;
        }
        if (key >= 0x20 && key < 0x7F) return 1;
        return 1;
    }

    return 0;
}

int hs_systools_handle_wheel(int delta)
{
    hs_window_t *foc = hs_window_get_focused();
    if (!foc) return 0;

    if (st_dev_win && st_dev_win->visible && foc == st_dev_win) {
        if (delta > 0) { if (st_dev_scroll[st_dev_tab] > 0) st_dev_scroll[st_dev_tab]--; }
        else if (delta < 0) st_dev_scroll[st_dev_tab]++;
        return 1;
    }
    if (st_log_win && st_log_win->visible && foc == st_log_win) {
        if (delta > 0) st_log_scroll += 2;
        else if (delta < 0) { st_log_scroll -= 2; if (st_log_scroll < 0) st_log_scroll = 0; }
        return 1;
    }
    return 0;
}

int hs_systools_click(int x, int y)
{
    /* 设备管理器 */
    if (st_dev_win && st_dev_win->visible) {
        int cx = st_dev_win->x + 8, cy = st_dev_win->y + 26;
        int cw = st_dev_win->width - 16, ch = st_dev_win->height - 34;
        if (x >= st_dev_win->x && x <= st_dev_win->x + st_dev_win->width &&
            y >= st_dev_win->y + 26 && y <= st_dev_win->y + st_dev_win->height) {
            int i;
            for (i = 0; i < 3; i++) {
                int tx = cx + i * 88;
                if (x >= tx && x <= tx + 80 && y >= cy + 4 && y <= cy + 26) {
                    st_dev_tab = i;
                    return 1;
                }
            }
            (void)cw; (void)ch;
            return 1;
        }
    }

    /* 磁盘管理 */
    if (st_disk_win && st_disk_win->visible) {
        if (x >= st_disk_win->x && x <= st_disk_win->x + st_disk_win->width &&
            y >= st_disk_win->y + 26 && y <= st_disk_win->y + st_disk_win->height) {
            int i;
            for (i = 0; i < 3; i++) {
                if (x >= st_disk_btn[i][0] && x <= st_disk_btn[i][0] + st_disk_btn[i][2] &&
                    y >= st_disk_btn[i][1] && y <= st_disk_btn[i][1] + st_disk_btn[i][3]) {
                    if (i == 0) {
                        int rc = usb64_mount_usb();
                        st_copyn(st_disk_msg, rc == 0 ? "USB mounted" :
                                 "Mount failed (no USB storage)", 63);
                    } else if (i == 1) {
                        int rc = usb64_umount_usb();
                        st_copyn(st_disk_msg, rc == 0 ? "USB unmounted" :
                                 "Unmount failed", 63);
                    } else {
                        int rc = disk64_fsck();
                        st_copyn(st_disk_msg, rc == 0 ? "fsck: PASS" :
                                 "fsck: issues/failed", 63);
                    }
                    st_disk_rescan();
                    return 1;
                }
            }
            return 1;
        }
    }

    /* 日志查看器 */
    if (st_log_win && st_log_win->visible) {
        if (x >= st_log_win->x && x <= st_log_win->x + st_log_win->width &&
            y >= st_log_win->y + 26 && y <= st_log_win->y + st_log_win->height) {
            return 1;
        }
    }

    return 0;
}
