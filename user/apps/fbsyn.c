/* Hepu OS - fbsyn（§5-4 用户态合成实测，2026-11-17）
 *
 * 这是 §5-4 四条硬要求的**唯一实测路径**。前三步（DESC 查询 / MAP_PHYS /
 * ACQUIRE+RELEASE+FLIP）都只是"接口在"，没人真的走过；这个程序走一遍：
 *
 *   1. SYS_FB_DESC 拿到显存布局与 flags（判断单/双缓冲）
 *   2. SYS_FB_ACQUIRE 拿写权 —— 双缓冲给的是**后台缓冲**，单缓冲给 LFB
 *   3. 画两半纯色（左绿右蓝），双缓冲下这时屏幕上**不应该**有变化
 *   4. SYS_FB_FLIP 提交 —— 这一刻屏幕上才出现色块
 *   5. 停 ~2 秒（留时间截图），然后 SYS_FB_RELEASE 交还
 *
 * 判据（串口）：
 *   [FBSYN] desc w=.. h=.. pitch=.. flags=..
 *   [FBSYN] acquire ok virt=0x..
 *   [FBSYN] drew, flipping
 *   [FBSYN] flip ok
 *   [FBSYN] released
 * 截图应看到左上/右上或整屏被绿蓝两半覆盖，且顶栏等桌面元素**不再重绘**
 * （内核 hs_present 在 owner 期间是空操作）。
 */
#include "../ulib.h"

struct fb_desc_local {
    uint64_t phys;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bpp;
    uint32_t format;
    uint32_t flags;
};

#define FB_DESC_FLAG_DOUBLE_BUFFERED 0x1u

static void out(const char *s) { while (*s) write(STDOUT_FILENO, s++, 1); }
static void out_hex(uint64_t v)
{
    static const char *h = "0123456789abcdef";
    char b[20]; int i = 0;
    write(STDOUT_FILENO, "0x", 2);
    if (!v) { write(STDOUT_FILENO, "0", 1); return; }
    while (v && i < 20) { b[i++] = h[v & 0xF]; v >>= 4; }
    while (i > 0) write(STDOUT_FILENO, &b[--i], 1);
}
static void out_u32(uint32_t v)
{
    char b[12]; int i = 0;
    if (!v) { write(STDOUT_FILENO, "0", 1); return; }
    while (v) { b[i++] = (char)('0' + v % 10); v /= 10; }
    while (i > 0) write(STDOUT_FILENO, &b[--i], 1);
}

int main(void)
{
    struct fb_desc_local d;
    d.phys = 0; d.width = d.height = d.pitch = d.bpp = d.format = d.flags = 0;

    uint64_t r = syscall2(SYS_FB_DESC, (uint64_t)(uintptr_t)&d, 0);
    if ((int64_t)r < 0 || d.width == 0 || d.height == 0) {
        out("[FBSYN] desc FAIL r="); out_hex(r); out("\n");
        return 1;
    }
    out("[FBSYN] desc w=");   out_u32(d.width);
    out(" h=");               out_u32(d.height);
    out(" pitch=");           out_u32(d.pitch);
    out(" flags=");           out_u32(d.flags);
    out((d.flags & FB_DESC_FLAG_DOUBLE_BUFFERED) ? " (double)\n" : " (single)\n");

    /* ACQUIRE: 长度传 0 = 要整帧。返回 -EBUSY(0xFFFFFFF0) 表示已有 owner。 */
    uint64_t v = syscall1(SYS_FB_ACQUIRE, 0);
    int64_t sv = (int64_t)v;
    if (sv <= 0) {
        out("[FBSYN] acquire FAIL v="); out_hex(v); out("\n");
        return 2;
    }
    out("[FBSYN] acquire ok virt="); out_hex(v); out("\n");

    /* 画两半: 左绿(0xFF00FF00 是 ABGR? 直接按 BGRX8888 存 32 位字,
     * 这里只求"颜色明显不同于桌面", 具体通道顺序不影响判据)。 */
    uint32_t *fb = (uint32_t *)(uintptr_t)v;
    uint32_t w = d.width, h = d.height;
    uint32_t stride_px = (d.flags & FB_DESC_FLAG_DOUBLE_BUFFERED)
                       ? w                      /* back 是紧密排列 width*4 */
                       : (d.pitch / 4);         /* LFB 按显存行宽 */
    if (stride_px == 0) stride_px = w;
    for (uint32_t y = 0; y < h; y++) {
        uint32_t *row = fb + (uint64_t)y * stride_px;
        for (uint32_t x = 0; x < w; x++)
            row[x] = (x < w / 2) ? 0x0000FF00u : 0x00FF0000u;
    }
    out("[FBSYN] drew, flipping\n");

    /* FLIP: 双缓冲下这一步才上屏; 单缓冲下内核 gfx64_present 直接 return。 */
    r = syscall0(SYS_FB_FLIP);
    if ((int64_t)r < 0) {
        out("[FBSYN] flip FAIL r="); out_hex(r); out("\n");
    } else {
        out("[FBSYN] flip ok\n");
    }

    /* 停一会儿: 没有 sleep syscall 的确定语义保证, 用忙等(约 2 秒量级)。
     * 目的只是给截图脚本时间, 不是精度要求。 */
    for (volatile uint64_t i = 0; i < 400000000ULL; i++) { }

    r = syscall0(SYS_FB_RELEASE);
    out("[FBSYN] released r="); out_hex(r); out("\n");
    return 0;
}

/* 入口点: user.ld 的 ENTRY(_start)。只写 main 会让 ld 退化到段起始地址,
 * 加载器跳过去执行的是别的函数 —— 实测表现就是 "[FBSYN] Running" 之后
 * 立刻 "[FBSYN] returned", 中间一行输出都没有。 */
void _start(void) { exit(main()); }
