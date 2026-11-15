/* ============================================================================
 * 赫普OS - bochs dispi VBE 引导（32 位专属）
 *
 * 为什么需要这个文件：32 位内核的 multiboot 头里没有 framebuffer 字段，
 * 所以 gfx_vbe.c 的 vbe_detect_from_boot_params() 永远返回 "no VBE info"，
 * 32 位只能退到 VGA 0x13（8bpp 索引色）。而 desktop_render.c 的整个桌面
 * 是按 24 位真彩色画的（0x2D3748 之类的 0xRRGGBB），在 8bpp 下会被当成
 * 调色板索引去查 —— 查不到就是黑屏。
 *
 * 本文件用 QEMU 的 bochs dispi 接口主动把显示切成 32bpp 真彩色，再读 PCI
 * BAR0 拿到线性帧缓冲的物理地址，回填 gfx_width/gfx_height/gfx_bpp/
 * gfx_fb_addr —— 之后 gfx_vbe.c 那条既有路径就能正常工作了。
 *
 * 边界：本文件只属于 32 位图形实现（见 docs/architecture/GRAPHICS_BOUNDARY.md
 * 规则 3：32 位的试错不外溢）。64 位不编它。
 * ========================================================================== */

typedef unsigned char  uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int   uint32_t;

static inline void bd_outb(uint16_t port, uint8_t val)
{ __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port)); }
static inline uint8_t bd_inb(uint16_t port)
{ uint8_t v; __asm__ volatile ("inb %1, %0" : "=a"(v) : "Nd"(port)); return v; }

/* ---- 端口 IO ---- */
static inline void bd_outw(uint16_t port, uint16_t val)
{
    __asm__ volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint16_t bd_inw(uint16_t port)
{
    uint16_t v;
    __asm__ volatile ("inw %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}
static inline void bd_outl(uint32_t port, uint32_t val)
{
    __asm__ volatile ("outl %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint32_t bd_inl(uint32_t port)
{
    uint32_t v;
    __asm__ volatile ("inl %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

#define DISPI_INDEX 0x01CE
#define DISPI_DATA  0x01CF

#define DISPI_ID        0x00
#define DISPI_XRES      0x01
#define DISPI_YRES      0x02
#define DISPI_BPP       0x03
#define DISPI_ENABLE    0x04
#define DISPI_BANK      0x05
#define DISPI_VIRT_W    0x06
#define DISPI_VIRT_H    0x07
#define DISPI_VIDEO_MEM 0x0A

#define DISPI_DISABLED  0x00
#define DISPI_ENABLED   0x01
#define DISPI_LFB       0x40

static void dispi_write(uint16_t idx, uint16_t val)
{
    bd_outw(DISPI_INDEX, idx);
    bd_outw(DISPI_DATA, val);
}

static uint16_t dispi_read(uint16_t idx)
{
    bd_outw(DISPI_INDEX, idx);
    return bd_inw(DISPI_DATA);
}

/* ---- PCI 配置空间（只读 BAR0，够用） ---- */
static uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off)
{
    uint32_t addr = 0x80000000u
                  | ((uint32_t)bus << 16)
                  | ((uint32_t)dev << 11)
                  | ((uint32_t)fn  << 8)
                  | (off & 0xFC);
    bd_outl(0xCF8, addr);
    return bd_inl(0xCFC);
}

/* 结果缓存，供 init 序列读取 */
static uint32_t bd_fb_phys = 0;
static uint32_t bd_width = 0;
static uint32_t bd_height = 0;
static uint32_t bd_bpp = 0;

uint32_t bochs_dispi_fb_phys(void) { return bd_fb_phys; }
uint32_t bochs_dispi_width(void)   { return bd_width; }
uint32_t bochs_dispi_height(void)  { return bd_height; }
uint32_t bochs_dispi_bpp(void)     { return bd_bpp; }

static void dputs(const char *s)
{
    extern void serial_puts(uint16_t, const char *);
    serial_puts(0x3F8, s);
}

static void dputhex(uint32_t v)
{
    extern void serial_puthex(uint16_t, uint32_t);
    serial_puthex(0x3F8, v);
}

/* 探测并切换到真彩色线性帧缓冲。
 * 成功返回 0，并把宽度/高度/bpp/物理地址存入 bd_* 缓存。
 * 失败返回负值，调用方应退回 VGA 0x13。 */
int bochs_dispi_init(uint32_t want_w, uint32_t want_h, uint32_t want_bpp)
{
    uint16_t id = dispi_read(DISPI_ID);

    dputs("[BD-VBE] dispi id=0x");
    dputhex(id);
    dputs("\n");

    /* QEMU bochs VBE 的 ID 区间是 0xB0C0..0xB0C5 */
    if (id < 0xB0C0 || id > 0xB0C5) {
        dputs("[BD-VBE] not a bochs dispi adapter, skip\n");
        return -1;
    }

    /* 先关掉，再设参数，最后打开（顺序不能反，否则 QEMU 会用旧参数） */
    dispi_write(DISPI_ENABLE, DISPI_DISABLED);
    dispi_write(DISPI_XRES, (uint16_t)want_w);
    dispi_write(DISPI_YRES, (uint16_t)want_h);
    dispi_write(DISPI_BPP,  (uint16_t)want_bpp);
    dispi_write(DISPI_VIRT_W, (uint16_t)want_w);
    dispi_write(DISPI_VIRT_H, (uint16_t)want_h);
    dispi_write(DISPI_ENABLE, DISPI_ENABLED | DISPI_LFB);

    /* 读回验证：QEMU 不支持的模式会被夹到最近值 */
    uint32_t rx = dispi_read(DISPI_XRES);
    uint32_t ry = dispi_read(DISPI_YRES);
    uint32_t rb = dispi_read(DISPI_BPP);

    dputs("[BD-VBE] mode set: ");
    dputhex(rx); dputs("x"); dputhex(ry);
    dputs("@"); dputhex(rb); dputs("bpp\n");

    if (rx != want_w || ry != want_h || rb != want_bpp) {
        dputs("[BD-VBE] mode not accepted by adapter\n");
        dispi_write(DISPI_ENABLE, DISPI_DISABLED);
        return -2;
    }

    /* VGA 设备的 BAR0 就是线性帧缓冲物理地址 */
    uint32_t bar0 = pci_read32(0, 2, 0, 0x10) & 0xFFFFFFF0u;
    dputs("[BD-VBE] pci 0:2.0 bar0=0x");
    dputhex(bar0);
    dputs("\n");

    if (bar0 == 0 || bar0 == 0xFFFFFFF0u) {
        dputs("[BD-VBE] BAR0 not assigned\n");
        dispi_write(DISPI_ENABLE, DISPI_DISABLED);
        return -3;
    }

    bd_width  = rx;
    bd_height = ry;
    bd_bpp    = rb;
    bd_fb_phys = bar0;

    /* 注意: 这里试过关 VGA 输出(AR 0x20)想清掉 screendump 最上面几行
     * 的 VGA 文本残留, 但 QEMU 会因此把显示退回 640x480 默认模式 ——
     * screendump 拿到 640x480 全黑。已回退。那 6px 只影响 QEMU 的 screendump,
     * 真机 LFB 没有这层东西, 保持现状。 */

    dputs("[BD-VBE] linear framebuffer ready\n");
    return 0;
}
