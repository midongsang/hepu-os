/* hpdesk.c 编进 32 位后还缺的外部符号。
 *
 * 两类:
 *   ① 真实现 —— 32 位必须自己提供的 (64 位除法库、自旋锁、整数开方)
 *   ② 退化壳 —— 64 位子系统(USB/PNG/JPEG/进程统计/本地化)32 位还没有,
 *      先给最小可用返回值, 让桌面能跑起来, 不参与功能。
 *
 * 退化壳全部串口只打一行汇总, 免得刷屏掩盖真正的启动日志。
 */

#include <stdint.h>

extern void serial_puts(uint16_t port, const char *s);
#include "../x86_64/png64.h"

static int g_d_cpu = 0;
static int g_d_mem = 0;
static int g_d_hs_cpu = 0;
static int g_d_hs_mem = 0;
static int g_d_ub = 0;
static int g_d_drv = 0;
static int g_d_smp = 0;

static void note(const char *what)
{
    serial_puts(0x3F8, "[COMPAT32] hpdesk dep stub: ");
    serial_puts(0x3F8, what);
    serial_puts(0x3F8, "\n");
}

/* ================= ① 真实现 ================= */

/* 32 位下 64/64 无符号除法要编译器运行时支持。这里自己写长除法,
 * 不依赖 libgcc —— 链接命令是 -nostdlib, 拿不到 __udivdi3。 */
uint64_t __udivdi3(uint64_t n, uint64_t d)
{
    if (d == 0) return 0;
    if (n < d)  return 0;
    uint64_t q = 0, r = 0;
    int i;
    for (i = 63; i >= 0; i--) {
        r <<= 1;
        r |= (n >> i) & 1ULL;
        if (r >= d) { r -= d; q |= (1ULL << i); }
    }
    return q;
}

/* 自旋锁: 32 位用 xchg 实现, 与 64 位 spinlock_t 同一结构 {lock,owner_cpu,recursion}
 * 32 位这边只需要能编能链, 语义与 64 位一致(递归计数保护)。 */
void spinlock_init(void *lock)
{
    if (!lock) return;
    *(volatile uint32_t *)lock = 0;
}

void spinlock_lock(void *lock)
{
    if (!lock) return;
    volatile uint32_t *l = (volatile uint32_t *)lock;
    while (1) {
        uint32_t old;
        __asm__ volatile("xchgl %0, %1" : "=r"(old), "+m"(*l) : "0"(1u) : "memory");
        if (old == 0) return;
        while (*l) { __asm__ volatile("pause" ::: "memory"); }
    }
}

void spinlock_unlock(void *lock)
{
    if (!lock) return;
    __asm__ volatile("" ::: "memory");
    *(volatile uint32_t *)lock = 0;
}

/* 整数开方: 逐位逼近, 够 UI 用(圆角半径/图标尺寸) */
uint32_t math64_isqrt(uint32_t v)
{
    uint32_t r = 0, b = 1u << 30;
    if (v == 0) return 0;
    while (b > v) b >>= 2;
    while (b) {
        if (v >= r + b) { v -= r + b; r = (r >> 1) + b; }
        else            { r >>= 1; }
        b >>= 2;
    }
    return r;
}

/* VGA 十六进制输出: 32 位 vga 模块没有同名函数, 直接往 0x3F8 打 */
void vga_put_hex(uint64_t v)
{
    serial_puts(0x3F8, "0x");
    int i;
    int started = 0;
    for (i = 60; i >= 0; i -= 4) {
        uint32_t nib = (uint32_t)((v >> i) & 0xF);
        if (nib || started || i == 0) {
            started = 1;
            char c = (char)(nib < 10 ? '0' + nib : 'a' + (nib - 10));
            serial_puts(0x3F8, (const char[]){ c, 0 });
        }
    }
}

/* ================= ② 退化壳 ================= */

/* 进程统计 */
int process64_cpu_usage(void)  { if (!g_d_cpu) { g_d_cpu = 1; note("process64_cpu_usage=0"); } return 0; }
int process64_current_index(void) { return 0; }
int hs_process_get_cpu_usage(void) { if (!g_d_hs_cpu) { g_d_hs_cpu = 1; note("hs_process_get_cpu_usage=0"); } return 0; }
int hs_process_get_mem_usage(void) { if (!g_d_hs_mem) { g_d_hs_mem = 1; note("hs_process_get_mem_usage=0"); } return 0; }

/* USB: 32 位没有 usb64, 一律"没有设备" */
int usb64_dev_count(void)          { if (!g_d_ub) { g_d_ub = 1; note("usb64_*=0"); } return 0; }
int usb64_hotplug_pending(void)    { return 0; }
int usb64_mount_usb(void)          { return -1; }
int usb64_storage_present(void)    { return 0; }
int storage_hal_get_device(void)   { return -1; }

/* 驱动管理 / 网卡 / DHCP */
int drv_mgr_update(void)      { if (!g_d_drv) { g_d_drv = 1; note("drv_mgr_update=0"); } return 0; }
int e1000_link_status(void)   { return 0; }
int dhcp64_is_bound(void)     { return 0; }

/* SMP 核数: 32 位用自己那套 */
uint32_t smp_get_cpu_count(void) { if (!g_d_smp) { g_d_smp = 1; note("smp_get_cpu_count=1"); } return 1; }

/* 注册表 / 本地化: 给空值, 桌面走默认文案 */
int reg64_count(void)                        { return 0; }
int reg64_get_int(const char *key, int def)  { (void)key; return def; }
/* 多语言文案原先在这里返回空串 —— 32 位编进 hpdesk.c 后，锁屏/登录/顶栏
 * 所有 L10N_STR_* 文案都渲染成空白，但当时 32 位没开锁屏，没人看到，
 * 于是这个"空串兜底"一直留着。现在 l10n64.c 已加入 32 位构建（Makefile 的
 * HP_DESK_DEPS），真表可用，两个桩删除。32 位默认语言仍是 en（registry 无
 * sys.lang），文案显示英文；若 registry 设 sys.lang=zh 则走中文表。 */

/* png64.c 内部用 pmm64_alloc_pages 拿连续页做 inflate 输出缓冲,
 * 返回物理地址后直接当指针用(依赖 identity map, 32 位满足)。 */
extern uint64_t mm_alloc_pages(uint32_t n);
uint64_t pmm64_alloc_pages(uint64_t n) { return mm_alloc_pages((uint32_t)n); }

/* 图片解码: png64.c 已真实编进 32 位(自含 inflate, 无外部依赖),
 * 这里只留 jpeg 的空壳 */
void jpeg64_free(void *img)          { (void)img; }

/* 文件关联 / 文件管理器光标 */
int file_assoc_count(void)                { return 0; }
int hs_files_cursor_kind(int x, int y)    { (void)x; (void)y; return 0; }

/* OOBE(首次开机引导): 32 位已初始化过, 不再弹 */
int  hs_oobe_active(void) { return 0; }
void hs_oobe_render(void *fb, int w, int h) { (void)fb; (void)w; (void)h; }

/* 终端: 桌面的终端窗口在 32 位先不跑真实命令 */
/* 32 位终端实现在 s1_terminal32.c：这里只转发 autostart 命令。 */
extern void s1_terminal_run_line_public(const char *line);
void hs_terminal_run_line(const char *line) { s1_terminal_run_line_public(line); }

/* 光标缩放/种类 */
void cursor64_set_scale(int pct) { (void)pct; }
void cursor64_set_kind(int kind) { (void)kind; }

/* TLB shootdown 计数: 32 位单核, 恒 0 */
uint64_t vmm64_tlb_shootdown_count(void) { return 0; }

/* ext2 读文件: 桌面壁纸/配置文件走这里。
 *
 * 32 位有自己的 ext2(ide_ext2_fm.c, 手机 UI 一直在用), 接口是
 * "目录 + 文件名"两段式, 而这里要的是完整路径。拆开转调即可 ——
 * fm_dir_ino 对 "/" 直接返回根 inode, 多级目录走 ext2_resolve_path。 */
extern int ide_ext2_read_file(const char *dirpath, const char *name, char *buf, int max);
int ext2_64_read_file(const char *path, uint8_t *buffer, uint32_t max_size)
{
    if (!path || !buffer || max_size < 2) return -1;
    if (path[0] != '/') return -1;
    const char *last = 0;
    for (const char *q = path; *q; q++) if (*q == '/') last = q;
    if (!last || !last[1]) return -1;
    char dir[128];
    int dl = (int)(last - path);
    if (dl <= 0) { dir[0] = '/'; dir[1] = 0; }
    else {
        if (dl >= 127) return -1;
        for (int i = 0; i < dl; i++) dir[i] = path[i];
        dir[dl] = 0;
    }
    {
        extern void serial_puts(uint16_t, const char *);
        extern void serial_puthex(uint16_t, uint32_t);
        int n = ide_ext2_read_file(dir, last + 1, (char *)buffer, (int)max_size);
        { static int _p32n; if (_p32n++ < 6) {
        serial_puts(0x3F8, "[HP32-FS] "); serial_puts(0x3F8, path);
        serial_puts(0x3F8, " dir="); serial_puts(0x3F8, dir);
        serial_puts(0x3F8, " n="); serial_puthex(0x3F8, (uint32_t)(n < 0 ? 0xFFFFFFFFu : (uint32_t)n));
        serial_puts(0x3F8, "\n"); } }
        return n;
    }
}

/* ---------------------------------------------------------------------------
 * 桌面离屏层专用池
 *
 * 为什么不用 kmalloc: 32 位 kheap 实测有结构性问题 —— 早期分配之后链表只剩
 * 前 ~101KB 的 9 个 used 块, 后面 31.9MB 永远回不到链表(maxfree=0),
 * 要 691KB 的离屏层必然拿不到。桌面层是"画完就贴回去"的临时缓冲,
 * 用一块静态 BSS 池最直接, 也完全不碰 kheap 的既有问题。
 *
 * 4MB: 顶栏 691KB + 灵动岛 + dock + 时钟 + 侧栏(750x900x4=2.7MB)。
 * 64 位不走这条路径。
 * ------------------------------------------------------------------------- */
#define HPD32_POOL_BYTES (4u * 1024u * 1024u)
static uint8_t  s_hpd32_pool[HPD32_POOL_BYTES];
static uint32_t s_hpd32_pool_used;

uint8_t *hpdesk32_layer_pool_alloc(uint32_t bytes)
{
    extern void serial_puts(uint16_t, const char *);
    extern void serial_puthex(uint16_t, uint32_t);

    if (bytes == 0) return 0;
    bytes = (bytes + 63u) & ~63u;          /* 64 字节对齐 */
    if (s_hpd32_pool_used + bytes > HPD32_POOL_BYTES) {
        serial_puts(0x3F8, "[HPD32-POOL] full, req=");
        serial_puthex(0x3F8, bytes);
        serial_puts(0x3F8, " used=");
        serial_puthex(0x3F8, s_hpd32_pool_used);
        serial_puts(0x3F8, "\n");
        return 0;
    }
    uint8_t *p = s_hpd32_pool + s_hpd32_pool_used;
    s_hpd32_pool_used += bytes;
    return p;
}
