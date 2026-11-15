/* Hepu OS - fbdesc（framebuffer 契约探针，KERNEL_SCOPE §5-4 第 1 步）
 *
 * 查一件事：用户态能不能从内核拿到"显存在哪、多大、什么格式"。
 * 这是把合成搬到 ring3 的前提 —— 拿不到物理基址和布局，用户态连自己映射
 * 都做不到，后面全是空谈。
 *
 * 契约见 kernel/arch/x86_64/display_backend.h 的 fb_desc_t；
 * syscall 号 SYS_FB_DESC(229)，参数 1 = 用户缓冲指针，返回 0 成功。
 *
 * 结构必须与内核一致：
 *   uint64_t phys; uint32_t width, height, pitch, bpp, format, flags;
 */
#include "../ulib.h"

struct fb_desc {
    uint64_t phys;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bpp;
    uint32_t format;
    uint32_t flags;
};

static void out(const char *s) { while (*s) write(STDOUT_FILENO, s++, 1); }
/* 十六进制打印：第一版这里用了十进制却加了 "0x" 前缀, 打出 "0x2147483648",
 * 看着像坏值, 实际是 2147483648 = 0x80000000(正确的 QEMU LFB 基址)。 */
static void out_hex(uint64_t v)
{
    static const char *h = "0123456789abcdef";
    char b[20]; int i = 0;
    write(STDOUT_FILENO, "0x", 2);
    if (!v) { write(STDOUT_FILENO, "0", 1); return; }
    while (v && i < 20) { b[i++] = h[v & 0xF]; v >>= 4; }
    while (i > 0) write(STDOUT_FILENO, &b[--i], 1);
}
static void out_u64(uint64_t v)
{
    char b[24]; int i = 0;
    if (!v) { write(STDOUT_FILENO, "0", 1); return; }
    while (v && i < 24) { b[i++] = (char)('0' + (v % 10)); v /= 10; }
    while (i > 0) write(STDOUT_FILENO, &b[--i], 1);
}

int main(void)
{
    struct fb_desc d;
    for (unsigned i = 0; i < sizeof(d); i++) ((char*)&d)[i] = 0;

    uint64_t r = syscall1(SYS_FB_DESC, (uint64_t)&d);
    if ((int64_t)r < 0) { out("[FBDESC] query failed\n"); return 1; }

    out("[FBDESC] phys=");  out_hex(d.phys);
    out(" w=");                out_u64(d.width);
    out(" h=");                out_u64(d.height);
    out(" pitch=");            out_u64(d.pitch);
    out(" bpp=");              out_u64(d.bpp);
    out(" fmt=");              out_u64(d.format);
    out(" flags=");            out_u64(d.flags);
    out("\n");

    /* 判据：一块能用的帧缓冲至少要有物理基址、非零尺寸、pitch 不小于宽*4 */
    if (d.phys && d.width && d.height && d.pitch >= d.width * 4)
        out("[FBDESC] contract PASS\n");
    else
        out("[FBDESC] contract BAD\n");
    return 0;
}

/* 入口点：user.ld 的 ENTRY(_start)，只写 main 会让 ld 退化到段起始地址。 */
void _start(void) { exit(main()); }
