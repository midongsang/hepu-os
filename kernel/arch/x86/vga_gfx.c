/* ============================================================================
 * 赫普OS - VGA 图形模式驱动 (320x200 256色, 模式0x13)
 * ========================================================================== */

#include <stdint.h>
#include <string.h>
#include "vga_gfx.h"
#include "vga.h"
#include "font8x16.h"
#include "paging.h"

/* 双缓冲: 所有绘制写入内存 back_buffer, gfx_flip() 垂直同步后整帧拷贝到 0xA0000,
 * 消除 GUI 单缓冲直接写显存导致的闪烁/撕裂 */
/* V2.40: 运行时分辨率变量 (VBE LFB) */
int gfx_width = GFX_WIDTH;
int gfx_height = GFX_HEIGHT;
int gfx_pitch = GFX_WIDTH;
int gfx_bpp = 8;
int gfx_refresh_rate = 60;  /* V6.96: 默认60Hz */
uint8_t *gfx_fb_addr = (uint8_t *)GFX_FB;
int gfx_scale_x = 100;
int gfx_scale_y = 100;

/* V2.40: 启动时存储的 VBE framebuffer 信息 (由 kernel.c 设置) */
static int vbe_width = 0, vbe_height = 0, vbe_pitch = 0, vbe_bpp = 0;
static uint8_t *vbe_fb = NULL;

void gfx_set_vbe_info(int w, int h, uint8_t *fb, int pitch, int bpp)
{
    vbe_width = w; vbe_height = h; vbe_fb = fb;
    vbe_pitch = pitch; vbe_bpp = bpp;
}

/* 双缓冲: 最大支持 1024x768x32 (4MB) */
/* V6.94: 增大双缓冲支持1920x1080@32bpp (约8MB)
 * 更高分辨率(2K/4K)可动态分配或使用单缓冲直接写帧缓冲 */
#define GFX_MAX_BUF (1920 * 1080 * 4)
static uint8_t back_buffer[GFX_MAX_BUF];
uint8_t *framebuffer = back_buffer;  /* V6.60: 导出供抗锯齿字体渲染使用 */

/* V6.50: 24/32bpp真彩色调色板 (VGA 256色→RGB888转换表) */
static uint32_t palette_rgb[256];
static int palette_inited = 0;

/* V6.50: 将8位调色板索引转换为24/32位RGB值 */
static inline uint32_t gfx_color_to_rgb(uint8_t color)
{
    if (!palette_inited) {
        /* 默认VGA调色板 (简化版, 前16色标准CGA) */
        static const uint8_t default_pal[16][3] = {
            {0,0,0},{0,0,170},{0,170,0},{0,170,170},
            {170,0,0},{170,0,170},{170,85,0},{170,170,170},
            {85,85,85},{85,85,255},{85,255,85},{85,255,255},
            {255,85,85},{255,85,255},{255,255,85},{255,255,255}
        };
        for (int i = 0; i < 256; i++) {
            if (i < 16) {
                palette_rgb[i] = (default_pal[i][0] << 16) |
                                 (default_pal[i][1] << 8) |
                                 default_pal[i][2];
            } else {
                /* 灰度渐变 */
                int g = (i - 16) * 255 / 239;
                palette_rgb[i] = (g << 16) | (g << 8) | g;
            }
        }
        palette_inited = 1;
    }
    return palette_rgb[color];
}

/* V6.50: 写入像素到back_buffer (支持8/24/32bpp) */
static inline void gfx_write_pixel(int x, int y, uint8_t color)
{
    if (x < 0 || x >= gfx_width || y < 0 || y >= gfx_height) return;
    if (gfx_bpp == 8) {
        framebuffer[y * gfx_pitch + x] = color;
    } else if (gfx_bpp == 24) {
        uint32_t rgb = gfx_color_to_rgb(color);
        uint8_t *p = &framebuffer[y * gfx_pitch + x * 3];
        p[0] = rgb & 0xFF;        /* B */
        p[1] = (rgb >> 8) & 0xFF; /* G */
        p[2] = (rgb >> 16) & 0xFF;/* R */
    } else if (gfx_bpp == 32) {
        uint32_t rgb = gfx_color_to_rgb(color);
        uint32_t *p = (uint32_t *)&framebuffer[y * gfx_pitch + x * 4];
        *p = rgb | 0xFF000000; /* BGRA, A=0xFF */
    }
}


/* ----------------------------------------------------------------------------
 * VGA 端口操作
 * -------------------------------------------------------------------------- */
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void io_wait(void) {
    __asm__ volatile ("outb %%al, $0x80" : : "a"(0));
}

/* ----------------------------------------------------------------------------
 * VGA 模式 0x13 寄存器设置表
 * -------------------------------------------------------------------------- */

/* 序列器寄存器 (0x3C4/0x3C5) */
/* CRT控制器寄存器 (0x3D4/0x3D5)
 * V2.33: 采用标准 VGA mode 13h (320x200x256) CRTC 值.
 * 原值 (CR6=0xBF/CR7=0x1F/CR16=0x9C/CR18=0x8F 等) 偏离标准时序,
 * QEMU std VGA 宽容可显示, 但真机 NVIDIA 等 VGA 兼容显卡严格遵守
 * 时序寄存器导致乱码. 标准值经 DOS/Linux VGA 驱动广泛验证, 全兼容. */
static const uint8_t crtc_regs[] = {
    0x5F, 0x4F, 0x50, 0x82, 0x54, 0x80, 0xBF, 0x1F,
    0x00, 0x41, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x9C, 0x8E, 0x8F, 0x28, 0x40, 0x96, 0xB9, 0xA3,
    0xFF
};

/* 图形控制器寄存器 (0x3CE/0x3CF) */
static const uint8_t gc_regs[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x0F,
    0xFF
};

/* 属性控制器寄存器 (0x3C0) */
static const uint8_t attr_regs[] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
    0x41, 0x00, 0x0F, 0x00, 0x00
};

/* ----------------------------------------------------------------------------
 * 初始化 VGA 图形模式 0x13
 * -------------------------------------------------------------------------- */
/* V2.25: VGA显存(0xA0000)页表关闭CPU缓存(PCD=1),
 * 防止CPU缓存VGA显存导致花屏/画面残留. 兼容4KB页表与4MB大页. */
static void gfx_disable_vga_cache(void)
{
    uint32_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    /* V2.35: 分页未启用时 CR3=0——直接跳过, 避免解引用 NULL 写坏物理 0 区
     * (开机 logo 在 paging_init 后、paging_enable 前调用时会触发) */
    if (cr3 == 0) return;
    uint32_t *pd = (uint32_t *)cr3;
    uint32_t pd_idx = GFX_FB >> 22;              /* 0 */
    uint32_t pt_idx = (GFX_FB >> 12) & 0x3FF;    /* 160 */
    uint32_t entry = pd[pd_idx];
    if (entry & 0x80) {
        /* 4MB 大页 (PSE): 直接设 PCD */
        pd[pd_idx] = entry | 0x10;
    } else if (entry & 0x01) {
        /* 4KB 页表: 设 PCD */
        uint32_t *pt = (uint32_t *)(entry & 0xFFFFF000);
        pt[pt_idx] |= 0x10;
    }
    /* 刷新 TLB */
    __asm__ volatile("invlpg (%0)" :: "r"((uint32_t)GFX_FB));
}


/* V3.82: 读当前 VGA 寄存器状态 (启动早期=SeaBIOS文本模式; 用于对照恢复序列) */
void vga_dump_regs(void)
{
    extern void serial_puts(uint16_t, const char *);
    extern void serial_puthex(uint16_t, uint32_t);
    serial_puts(0x3F8, "[VGAREG] misc=");
    serial_puthex(0x3F8, inb(0x3CC));
    serial_puts(0x3F8, "\n[VGAREG] seq:");
    for (int i = 0; i < 5; i++) {
        outb(0x3C4, i);
        serial_puthex(0x3F8, inb(0x3C5));
    }
    serial_puts(0x3F8, "\n[VGAREG] crtc:");
    for (int i = 0; i < 25; i++) {
        outb(0x3D4, i);
        serial_puthex(0x3F8, inb(0x3D5));
    }
    serial_puts(0x3F8, "\n[VGAREG] gc:");
    for (int i = 0; i < 9; i++) {
        outb(0x3CE, i);
        serial_puthex(0x3F8, inb(0x3CF));
    }
    serial_puts(0x3F8, "\n");
}

/* V3.82: 强制 VGA 0x13 (供 splash/文本恢复场景——VBE LFB 模式下
   vga_gfx_text_mode 的标准VGA寄存器序列无法切回文本, 屏幕花屏) */
void gfx_force_vga13(void)
{
    vbe_fb = NULL; vbe_width = 0; vbe_height = 0;
}

void vga_gfx_init(void)
{
    /* V6.50: 串口调试输出 */
    extern void serial_puts(uint16_t, const char *);
    char gfx_dbg[128];
    extern int snprintf(char *, size_t, const char *, ...);
    snprintf(gfx_dbg, sizeof(gfx_dbg), "[GFX-INIT] vbe_fb=%p w=%d h=%d bpp=%d\n",
             vbe_fb, vbe_width, vbe_height, vbe_bpp);
    serial_puts(0x3F8, gfx_dbg);

    /* V2.40: VBE LFB 模式——如果启动时设置了 VBE 信息, 直接用线性帧缓冲 */
    if (vbe_fb && vbe_width > 0 && vbe_height > 0) {
        gfx_width = vbe_width;
        gfx_height = vbe_height;
        gfx_pitch = (vbe_pitch > 0) ? vbe_pitch : vbe_width;
        gfx_bpp = (vbe_bpp > 0) ? vbe_bpp : 8;
        gfx_fb_addr = vbe_fb;

        /* V6.94: 高分辨率支持 - 计算所需缓冲区大小 */
        int bpp_bytes = (gfx_bpp == 8) ? 1 : (gfx_bpp == 24) ? 3 :
                        (gfx_bpp == 64) ? 8 : 4;
        uint32_t needed_size = gfx_pitch * gfx_height * bpp_bytes;

        snprintf(gfx_dbg, sizeof(gfx_dbg),
                 "[GFX-INIT] mode=%dx%d@%dbpp pitch=%d need=%dKB max=%dKB\n",
                 gfx_width, gfx_height, gfx_bpp, gfx_pitch,
                 needed_size / 1024, GFX_MAX_BUF / 1024);
        serial_puts(0x3F8, gfx_dbg);

        /* 如果静态缓冲区不够，使用单缓冲直接写帧缓冲 */
        if (needed_size > GFX_MAX_BUF) {
            serial_puts(0x3F8, "[GFX-INIT] high-res mode: using direct framebuffer (single buffer)\n");
            framebuffer = vbe_fb;  /* 直接写硬件帧缓冲 */
        } else {
            framebuffer = back_buffer;  /* 使用双缓冲 */
        }

        /* V6.86真机兼容: 映射GOP帧缓冲区到页表。 */
        if ((uint32_t)vbe_fb >= 0x10000000) {
            extern page_directory_t *paging_get_current_dir(void);
            page_directory_t *dir = paging_get_current_dir();
            uint32_t fb_phys = (uint32_t)vbe_fb;
            uint32_t fb_size = gfx_pitch * gfx_height * bpp_bytes;
            uint32_t fb_start = fb_phys & ~0xFFF;
            uint32_t fb_end = (fb_phys + fb_size + 0xFFF) & ~0xFFF;
            serial_puts(0x3F8, "[GFX-MAP] mapping GOP framebuffer\n");
            for (uint32_t addr = fb_start; addr < fb_end; addr += 4096) {
                paging_map(dir, addr, addr, 0x3);
            }
            __asm__ volatile("mov %%cr3, %%eax; mov %%eax, %%cr3" ::: "eax");
            serial_puts(0x3F8, "[GFX-MAP] GOP framebuffer mapped\n");
        }

        /* V2.41: 计算 UI 缩放因子 (基准 320x200) */
        gfx_scale_x = (vbe_width * 100) / 320;
        gfx_scale_y = (vbe_height * 100) / 200;
        if (gfx_scale_x < 100) gfx_scale_x = 100;
        if (gfx_scale_y < 100) gfx_scale_y = 100;
        vga_printf("[GFX] VBE LFB: %dx%d bpp=%d pitch=%d fb=%p\n",
                   vbe_width, vbe_height, vbe_bpp, vbe_pitch, vbe_fb);

        /* 清空缓冲区 */
        if (framebuffer == back_buffer) {
            memset(back_buffer, 0, needed_size);
        }
        gfx_clear(0);
        gfx_flip();
        return;
    }
    /* 默认 VGA 模式 0x13 (320x200x256) */
    gfx_width = GFX_WIDTH;
    gfx_height = GFX_HEIGHT;
    gfx_pitch = GFX_WIDTH;
    gfx_bpp = 8;
    gfx_fb_addr = (uint8_t *)GFX_FB;
    gfx_scale_x = 100;
    gfx_scale_y = 100;

    /* 关闭中断 */
    __asm__ volatile ("cli");

    /* V2.25: 清空后台缓冲区(双缓冲), 防止进入图形模式残留旧画面 */
    memset(back_buffer, 0, gfx_width * gfx_height);

    /* V2.25: VGA显存关闭CPU缓存(PCD=1) */
    gfx_disable_vga_cache();

    /* 0. 混合输出寄存器 (先设时钟/IO, 标准顺序 Misc 第一) */
    outb(0x3C2, 0x63);

    /* 1. 序列器 — V3.82: 标准 mode 13h 流程.
     * 必须先 SR0=0x01 同步重置, 再写 SR1-4, 最后 SR0=0x03 取消重置.
     * (旧实现直接写 SR0=0x03 后写 SR1-4, QEMU std VGA 会忽略
     *  未重置状态的 SR4 写入 → Chain4 未生效 → 平面错位竖条纹) */
    outb(0x3C4, 0x00); outb(0x3C5, 0x03);   /* SR0=0x03: bit1 同步重置 + bit0 显示重置 */
    outb(0x3C4, 0x01); outb(0x3C5, 0x01);
    outb(0x3C4, 0x02); outb(0x3C5, 0x0F);
    outb(0x3C4, 0x03); outb(0x3C5, 0x00);
    outb(0x3C4, 0x04); outb(0x3C5, 0x07);   /* SR4=0x07: Chain4 ON */
    outb(0x3C4, 0x00); outb(0x3C5, 0x03);   /* 取消重置, 8-dot */

    /* 2. 解锁CRTC寄存器 */
    outb(0x3D4, 0x11);
    outb(0x3D5, inb(0x3D5) & ~0x80);

    /* 3. CRT控制器 */
    for (int i = 0; i < 25; i++) {
        outb(0x3D4, i);
        outb(0x3D5, crtc_regs[i]);
    }

    /* 4. 图形控制器 */
    for (int i = 0; i < 9; i++) {
        outb(0x3CE, i);
        outb(0x3CF, gc_regs[i]);
    }

    /* 5. 属性控制器 */
    inb(0x3DA); /* 重置属性控制器翻转锁 */
    for (int i = 0; i < 21; i++) {
        outb(0x3C0, i);
        outb(0x3C0, attr_regs[i]);
    }
    outb(0x3C0, 0x20); /* 启用显示 */

    /* 7. 设置默认调色板 (前16色为标准CGA颜色) */
    gfx_set_palette(0,  0,   0,   0);   /* 黑 */
    gfx_set_palette(1,  0,   0,   170); /* 蓝 */
    gfx_set_palette(2,  0,   170, 0);   /* 绿 */
    gfx_set_palette(3,  0,   170, 170); /* 青 */
    gfx_set_palette(4,  170, 0,   0);   /* 红 */
    gfx_set_palette(5,  170, 0,   170); /* 品红 */
    gfx_set_palette(6,  170, 85,  0);   /* 棕 */
    gfx_set_palette(7,  170, 170, 170); /* 浅灰 */
    gfx_set_palette(8,  85,  85,  85);  /* 深灰 */
    gfx_set_palette(9,  85,  85,  255); /* 浅蓝 */
    gfx_set_palette(10, 85,  255, 85);  /* 浅绿 */
    gfx_set_palette(11, 85,  255, 255); /* 浅青 */
    gfx_set_palette(12, 255, 85,  85);  /* 浅红 */
    gfx_set_palette(13, 255, 85,  255); /* 浅品红 */
    gfx_set_palette(14, 255, 255, 85);  /* 黄 */
    gfx_set_palette(15, 255, 255, 255); /* 白 */

    /* 清屏 */
    gfx_clear(GFX_COLOR_BLACK);

    /* 重新开启中断 */
    __asm__ volatile ("sti");
}

/* 切换回文本模式 (80x25) */
void vga_gfx_text_mode(void)
{
    __asm__ volatile ("cli");

    /* 简化的文本模式设置 */
    outb(0x3C2, 0x67);

    /* 序列器 */
    outb(0x3C4, 0x00);
    /* V2.33: mode 03h 序列器. SR4(Memory Mode)=0x07 是关键:
     * bit0=8/16bit字符, bit1=chain, bit2=odd/even寻址(文本模式必需).
     * 原值 0x02 缺少 odd/even 使能, 导致字体(plane2)访问/渲染异常→条纹. */
    static const uint8_t text_seq[] = {0x03, 0x00, 0x03, 0x00, 0x02};  /* V3.82: SeaBIOS实测=0x02(仅奇偶寻址); 0x07含扩展内存位→平面映射错乱花屏 */
    for (int i = 0; i < 5; i++) {
        outb(0x3C4, i);
        outb(0x3C5, text_seq[i]);
    }

    /* CRTC */
    outb(0x3D4, 0x11);
    outb(0x3D5, inb(0x3D5) & ~0x80);
    /* V2.33: 标准 mode 03h (80x25 彩色文本) CRTC 值,
     * 修复原错误值(CR6=0xBF/CR9=0x47/CR17=0x0E/CR18=0x8F等)导致
     * gfxtest 切回文本后显示竖条纹/乱码的问题 */
    /* V2.33: 标准 mode 03h (80x25 彩色文本, 400线) CRTC 值,
     * 修复原错误值(CR6=0xBF/CR9=0x47/CR17=0x0E/CR18=0x8F等)导致
     * gfxtest 切回文本后显示竖条纹/乱码的问题.
     * 400线时序: Vertical Total=449, Display End=400 */
    /* V3.82: 采用 SeaBIOS(QEMU std VGA) 实测 mode 03 寄存器值——
     * 原值 CR6=0xC1/CR18=0x90/CR20=0x00/CR22=0x02 等垂直时序错误,
     * 与 BIOS 不一致导致文本恢复花屏 */
    static const uint8_t text_crtc[] = {
        0x5F, 0x4F, 0x50, 0x82, 0x55, 0x81, 0xBF, 0x1F,
        0x00, 0x4F, 0x0D, 0x0E, 0x00, 0x00, 0x00, 0x00,
        0x9C, 0x8E, 0x8F, 0x28, 0x1F, 0x96, 0xB9, 0xA3, 0xFF
    };
    for (int i = 0; i < 25; i++) {
        outb(0x3D4, i);
        outb(0x3D5, text_crtc[i]);
    }

    /* 图形控制器 */
    static const uint8_t text_gc[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x0E, 0x0F, 0xFF};  /* V3.82: GC7=0x0F(SeaBIOS实测,全平面ColorDon'tCare) */
    for (int i = 0; i < 9; i++) {
        outb(0x3CE, i);
        outb(0x3CF, text_gc[i]);
    }

    /* 属性控制器 */
    inb(0x3DA);
    static const uint8_t text_attr[] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
        0x08, 0x00, 0x0F, 0x00, 0x00
    };
    for (int i = 0; i < 21; i++) {
        outb(0x3C0, i);
        outb(0x3C0, text_attr[i]);
    }
    outb(0x3C0, 0x20);

    /* V3.82: 重载 8x16 字体到平面 2 (plane 2)——
     * 图形模式(0x13/VBE)切换会破坏字体平面, 切回文本后字符渲染为
     * 密集竖条纹(花屏). 从内嵌标准 VGA 字体(hepu_font8x16)恢复:
     *   1) Map Mask 只选 plane 2  (SR2=0x04)
     *   2) GC6=0x04: 0xA0000 段映射 plane 2 可写
     *   3) 写入 256 字符 x 16 行 = 4096 字节
     *   4) 恢复 GC6=0x0E / Map Mask=0x0F */
    outb(0x3C4, 0x02);
    outb(0x3C5, 0x04);                    /* Map Mask = plane 2 */
    outb(0x3CE, 0x06);
    outb(0x3CF, 0x05);                    /* GC6 = 0x05: 图形模式段选择 + 128K
                                              (文本模式 0xA0000 段未映射, 读回0x0F) */
    inb(0x3DA);                            /* IO 延迟: 寄存器同步 */
    memcpy((void *)0xA0000, hepu_font8x16, 4096);
    outb(0x3CE, 0x06);
    outb(0x3CF, 0x0E);                    /* GC6 恢复 0x0E (文本模式) */
    outb(0x3C4, 0x02);
    outb(0x3C5, 0x0F);                    /* Map Mask 恢复全平面 */

    __asm__ volatile ("sti");
}

/* ----------------------------------------------------------------------------
 * 绘图 API
 * -------------------------------------------------------------------------- */

void gfx_putpixel(int x, int y, uint8_t color)
{
    gfx_write_pixel(x, y, color);
}

/* V6.90: 32位真彩色绘图API (用于现代桌面UI) */
/* V6.94: 写入真彩色像素 (支持24/32/64bpp) */
static inline void gfx_write_pixel_rgb(int x, int y, uint32_t rgb)
{
    if (x < 0 || x >= gfx_width || y < 0 || y >= gfx_height) return;
    if (gfx_bpp == 32) {
        uint32_t *p = (uint32_t *)&framebuffer[y * gfx_pitch + x * 4];
        *p = rgb | 0xFF000000;
    } else if (gfx_bpp == 24) {
        uint8_t *p = &framebuffer[y * gfx_pitch + x * 3];
        p[0] = rgb & 0xFF;
        p[1] = (rgb >> 8) & 0xFF;
        p[2] = (rgb >> 16) & 0xFF;
    } else if (gfx_bpp == 64) {
        /* 64bpp RGBA: 每个通道16位 (广色域)
         * 将8位通道扩展到16位: v8 * 0x101 = v16 */
        uint16_t *p = (uint16_t *)&framebuffer[y * gfx_pitch + x * 8];
        uint8_t r = (rgb >> 16) & 0xFF;
        uint8_t g = (rgb >> 8) & 0xFF;
        uint8_t b = rgb & 0xFF;
        p[0] = b * 0x101;  /* B */
        p[1] = g * 0x101;  /* G */
        p[2] = r * 0x101;  /* R */
        p[3] = 0xFFFF;     /* A */
    } else {
        /* 8bpp回退: 找最近调色板色 */
        gfx_write_pixel(x, y, 0x0F);
    }
}

void gfx_putpixel_rgb(int x, int y, uint32_t rgb)
{
    gfx_write_pixel_rgb(x, y, rgb);
}

void gfx_fillrect_rgb(int x, int y, int w, int h, uint32_t rgb)
{
    if (w <= 0 || h <= 0) return;
    for (int row = 0; row < h; row++) {
        for (int col = 0; col < w; col++) {
            gfx_write_pixel_rgb(x + col, y + row, rgb);
        }
    }
}

void gfx_hline_rgb(int x, int y, int w, uint32_t rgb)
{
    if (y < 0 || y >= gfx_height) return;
    if (x < 0) { w += x; x = 0; }
    if (x + w > gfx_width) w = gfx_width - x;
    if (w <= 0) return;
    for (int i = 0; i < w; i++) gfx_write_pixel_rgb(x + i, y, rgb);
}

void gfx_vline_rgb(int x, int y, int h, uint32_t rgb)
{
    if (x < 0 || x >= gfx_width) return;
    if (y < 0) { h += y; y = 0; }
    if (y + h > gfx_height) h = gfx_height - y;
    if (h <= 0) return;
    for (int i = 0; i < h; i++) gfx_write_pixel_rgb(x, y + i, rgb);
}

/* 真彩色垂直渐变 */
void gfx_gradient_v_rgb(int x, int y, int w, int h, uint32_t c1, uint32_t c2)
{
    if (h <= 0) return;
    uint8_t r1 = (c1 >> 16) & 0xFF, g1 = (c1 >> 8) & 0xFF, b1 = c1 & 0xFF;
    uint8_t r2 = (c2 >> 16) & 0xFF, g2 = (c2 >> 8) & 0xFF, b2 = c2 & 0xFF;
    for (int row = 0; row < h; row++) {
        int t = row * 256 / (h > 1 ? h - 1 : 1);
        uint8_t r = r1 + (r2 - r1) * t / 256;
        uint8_t g = g1 + (g2 - g1) * t / 256;
        uint8_t b = b1 + (b2 - b1) * t / 256;
        uint32_t rgb = (r << 16) | (g << 8) | b;
        gfx_hline_rgb(x, y + row, w, rgb);
    }
}

/* 真彩色填充圆 */
void gfx_fillcircle_rgb(int cx, int cy, int r, uint32_t rgb)
{
    if (r <= 0) return;
    for (int y = -r; y <= r; y++) {
        for (int x = -r; x <= r; x++) {
            if (x * x + y * y <= r * r) {
                gfx_write_pixel_rgb(cx + x, cy + y, rgb);
            }
        }
    }
}

/* 真彩色圆角矩形 */
void gfx_rounded_rect_rgb(int x, int y, int w, int h, int radius, uint32_t rgb)
{
    /* 中心矩形 */
    gfx_fillrect_rgb(x + radius, y, w - 2 * radius, h, rgb);
    /* 左右矩形 */
    gfx_fillrect_rgb(x, y + radius, radius, h - 2 * radius, rgb);
    gfx_fillrect_rgb(x + w - radius, y + radius, radius, h - 2 * radius, rgb);
    /* 四个角 */
    gfx_fillcircle_rgb(x + radius, y + radius, radius, rgb);
    gfx_fillcircle_rgb(x + w - radius, y + radius, radius, rgb);
    gfx_fillcircle_rgb(x + radius, y + h - radius, radius, rgb);
    gfx_fillcircle_rgb(x + w - radius, y + h - radius, radius, rgb);
}

/* 颜色混合 (alpha: 0=全c1, 255=全c2) */
uint32_t gfx_blend_rgb(uint32_t c1, uint32_t c2, int alpha)
{
    if (alpha <= 0) return c1;
    if (alpha >= 255) return c2;
    uint8_t r1 = (c1 >> 16) & 0xFF, g1 = (c1 >> 8) & 0xFF, b1 = c1 & 0xFF;
    uint8_t r2 = (c2 >> 16) & 0xFF, g2 = (c2 >> 8) & 0xFF, b2 = c2 & 0xFF;
    uint8_t r = r1 + (r2 - r1) * alpha / 255;
    uint8_t g = g1 + (g2 - g1) * alpha / 255;
    uint8_t b = b1 + (b2 - b1) * alpha / 255;
    return (r << 16) | (g << 8) | b;
}

/* V6.95: 矩形边框 RGB */
void gfx_rect_rgb(int x, int y, int w, int h, uint32_t rgb)
{
    gfx_hline_rgb(x, y, w, rgb);
    gfx_hline_rgb(x, y + h - 1, w, rgb);
    gfx_vline_rgb(x, y, h, rgb);
    gfx_vline_rgb(x + w - 1, y, h, rgb);
}

/* V6.95: 直线 RGB (Bresenham) */
void gfx_line_rgb(int x0, int y0, int x1, int y1, uint32_t rgb)
{
    int dx = (x1 > x0) ? (x1 - x0) : (x0 - x1);
    int dy = (y1 > y0) ? (y1 - y0) : (y0 - y1);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;
    while (1) {
        gfx_putpixel_rgb(x0, y0, rgb);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx) { err += dx; y0 += sy; }
    }
}

/* V6.95: 椭圆边框 RGB */
void gfx_ellipse_rgb(int cx, int cy, int rx, int ry, uint32_t rgb)
{
    if (rx <= 0 || ry <= 0) return;
    int x = 0, y = ry;
    int a2 = rx * rx, b2 = ry * ry;
    int d = b2 - a2 * ry + a2 / 4;
    int dx = 2 * b2 * x, dy = 2 * a2 * y;
    while (dx < dy) {
        gfx_putpixel_rgb(cx + x, cy + y, rgb);
        gfx_putpixel_rgb(cx - x, cy + y, rgb);
        gfx_putpixel_rgb(cx + x, cy - y, rgb);
        gfx_putpixel_rgb(cx - x, cy - y, rgb);
        if (d < 0) { x++; dx += 2 * b2; d += dx + b2; }
        else { x++; y--; dx += 2 * b2; dy -= 2 * a2; d += dx - dy + b2; }
    }
    d = b2 * (x + 1) * (x + 1) + a2 * (y - 1) * (y - 1) - a2 * b2;
    while (y >= 0) {
        gfx_putpixel_rgb(cx + x, cy + y, rgb);
        gfx_putpixel_rgb(cx - x, cy + y, rgb);
        gfx_putpixel_rgb(cx + x, cy - y, rgb);
        gfx_putpixel_rgb(cx - x, cy - y, rgb);
        if (d > 0) { y--; dy -= 2 * a2; d += a2 - dy; }
        else { y--; x++; dx += 2 * b2; dy -= 2 * a2; d += dx - dy + a2; }
    }
}

/* V6.95: 三角形边框 RGB */
void gfx_triangle_rgb(int x1, int y1, int x2, int y2, int x3, int y3, uint32_t rgb)
{
    gfx_line_rgb(x1, y1, x2, y2, rgb);
    gfx_line_rgb(x2, y2, x3, y3, rgb);
    gfx_line_rgb(x3, y3, x1, y1, rgb);
}

uint8_t gfx_getpixel(int x, int y)
{
    if (x < 0 || x >= gfx_width || y < 0 || y >= gfx_height) return 0;
    if (gfx_bpp == 8) {
        return framebuffer[y * gfx_pitch + x];
    }
    /* 24/32bpp: 返回近似调色板索引 (简化) */
    return 0;
}

void gfx_hline(int x, int y, int w, uint8_t color)
{
    if (y < 0 || y >= gfx_height) return;
    if (x < 0) { w += x; x = 0; }
    if (x + w > gfx_width) w = gfx_width - x;
    if (w <= 0) return;
    if (gfx_bpp == 8) {
        for (int i = 0; i < w; i++) {
            framebuffer[y * gfx_pitch + x + i] = color;
        }
    } else if (gfx_bpp == 24) {
        /* V6.50: 24bpp优化 - 直接写内存, 不逐像素调用 */
        uint32_t rgb = gfx_color_to_rgb(color);
        uint8_t b = rgb & 0xFF, g = (rgb >> 8) & 0xFF, r = (rgb >> 16) & 0xFF;
        uint8_t *p = &framebuffer[y * gfx_pitch + x * 3];
        for (int i = 0; i < w; i++) {
            p[i*3] = b; p[i*3+1] = g; p[i*3+2] = r;
        }
    } else if (gfx_bpp == 32) {
        uint32_t rgb = gfx_color_to_rgb(color) | 0xFF000000;
        uint32_t *p = (uint32_t *)&framebuffer[y * gfx_pitch + x * 4];
        for (int i = 0; i < w; i++) p[i] = rgb;
    }
}

void gfx_vline(int x, int y, int h, uint8_t color)
{
    if (x < 0 || x >= gfx_width) return;
    if (y < 0) { h += y; y = 0; }
    if (y + h > gfx_height) h = gfx_height - y;
    if (h <= 0) return;
    for (int i = 0; i < h; i++) {
        gfx_write_pixel(x, y + i, color);
    }
}

void gfx_rect(int x, int y, int w, int h, uint8_t color)
{
    gfx_hline(x, y, w, color);
    gfx_hline(x, y + h - 1, w, color);
    gfx_vline(x, y, h, color);
    gfx_vline(x + w - 1, y, h, color);
}

void gfx_fillrect(int x, int y, int w, int h, uint8_t color)
{
    for (int i = 0; i < h; i++) {
        gfx_hline(x, y + i, w, color);
    }
}

void gfx_line(int x0, int y0, int x1, int y1, uint8_t color)
{
    int dx = (x1 > x0) ? (x1 - x0) : (x0 - x1);
    int dy = (y1 > y0) ? (y1 - y0) : (y0 - y1);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;

    while (1) {
        gfx_putpixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx)  { err += dx; y0 += sy; }
    }
}

void gfx_clear(uint8_t color)
{
    if (gfx_bpp == 8) {
        for (int i = 0; i < gfx_width * gfx_height; i++) {
            framebuffer[i] = color;
        }
    } else {
        /* 24/32bpp: 逐像素写入 */
        for (int y = 0; y < gfx_height; y++) {
            for (int x = 0; x < gfx_width; x++) {
                gfx_write_pixel(x, y, color);
            }
        }
    }
}

void gfx_set_palette(uint8_t index, uint8_t r, uint8_t g, uint8_t b)
{
    outb(0x3C8, index);
    outb(0x3C9, r >> 2);  /* VGA调色板用6位 */
    outb(0x3C9, g >> 2);
    outb(0x3C9, b >> 2);
}

uint8_t *gfx_get_framebuffer(void)
{
    return framebuffer;  /* 返回 back_buffer, 由 gfx_flip 提交显示 */
}

/* 等待 VGA 垂直回扫: 读 0x3DA bit3, 在回扫期间 flip 可避免撕裂 */
void gfx_wait_vsync(void)
{
    /* V3.87-E5: 超时保护——VGA 控制器异常时防永久忙等(真机黑屏场景相关) */
    uint32_t guard = 0;
    inb(0x3DA);
    while (inb(0x3DA) & 0x08) {  /* 等待回扫结束 */
        if (++guard > 100000) return;
    }
    guard = 0;
    while (!(inb(0x3DA) & 0x08)) {  /* 等待回扫开始 */
        if (++guard > 100000) return;
    }
}

/* 提交 back_buffer 到显存 (带垂直同步) */
/* V2.99 反色已临时回退 (调试); V3.82 清理死变量 gfx_invert */
void gfx_set_invert(int on) { (void)on; }

void gfx_flip(void)
{
    /* V2.61: 休眠灭屏状态 -> 保持黑屏 */
    extern int pm_is_screen_blanked(void);
    if (pm_is_screen_blanked()) {
        gfx_black_screen();
        return;
    }

    /* V6.94: 单缓冲模式(高分辨率) - 直接写帧缓冲, 无需拷贝 */
    if (framebuffer == gfx_fb_addr) {
        return;  /* 已经直接写入硬件帧缓冲 */
    }

    gfx_wait_vsync();
    int bytes_per_pixel = (gfx_bpp == 8) ? 1 : (gfx_bpp == 24) ? 3 :
                          (gfx_bpp == 64) ? 8 : 4;
    int src_pitch = gfx_width * bytes_per_pixel;
    /* V6.50: 调试输出 */
    static int flip_dbg_count = 0;
    if (flip_dbg_count < 3) {
        extern void serial_puts(uint16_t, const char *);
        extern int snprintf(char *, size_t, const char *, ...);
        char dbg[128];
        snprintf(dbg, sizeof(dbg), "[FLIP] fb=%p bpp=%d pitch=%d src_pitch=%d w=%d h=%d\n",
                 gfx_fb_addr, gfx_bpp, gfx_pitch, src_pitch, gfx_width, gfx_height);
        serial_puts(0x3F8, dbg);
        flip_dbg_count++;
    }
    /* V6.50: VBE LFB是内存映射, 用memcpy加速 */
    if ((uint32_t)gfx_fb_addr != 0xA0000) {
        uint8_t *fb = (uint8_t *)gfx_fb_addr;
        if (gfx_pitch == src_pitch) {
            memcpy(fb, back_buffer, gfx_pitch * gfx_height);
        } else {
            for (int y = 0; y < gfx_height; y++) {
                memcpy(fb + y * gfx_pitch, back_buffer + y * src_pitch, src_pitch);
            }
        }
    } else {
        /* VGA 0xA0000, 逐字节 */
        volatile uint8_t *fb = (volatile uint8_t *)gfx_fb_addr;
        if (gfx_pitch == src_pitch) {
            int total = gfx_pitch * gfx_height;
            for (int i = 0; i < total; i++) fb[i] = back_buffer[i];
        } else {
            for (int y = 0; y < gfx_height; y++) {
                for (int x = 0; x < src_pitch; x++) {
                    fb[y * gfx_pitch + x] = back_buffer[y * src_pitch + x];
                }
            }
        }
    }
}
void gfx_black_screen(void)
{
    /* V2.61: 直接填黑显存 (休眠灭屏) */
    volatile uint8_t *fb = (volatile uint8_t *)gfx_fb_addr;
    int bytes_per_pixel = (gfx_bpp == 8) ? 1 : (gfx_bpp == 24) ? 3 : 4;
    int total = gfx_pitch * gfx_height;
    for (int i = 0; i < total; i++) {
        fb[i] = 0x00;
    }
}


/* V2.25: 纯色填充测试——定位花屏根源用.
 * 填满纯色 → flip 提交, 可逐色验证显存写入/缓存/回扫是否正常. */
void gfx_fill_test(uint8_t color)
{
    for (int i = 0; i < gfx_width * gfx_height; i++) {
        back_buffer[i] = color;
    }
    gfx_flip();
}

/* ----------------------------------------------------------------------------
 * 8x8 字体 (简化版，ASCII 32-127)
 * 每个字符8字节，每字节一行，从顶部开始
 * -------------------------------------------------------------------------- */
/* 字体数据和文本输出已拆分到 vga_gfx_text.c */
extern const uint8_t font_8x8[][8];

/* ============================================================================
 * V6.50: 扩展绘图API — 圆形/椭圆/位图blit/三角形
 * ========================================================================== */

/* 画圆 (中点圆算法) */
void gfx_circle(int cx, int cy, int r, uint8_t color)
{
    if (r <= 0) return;
    int x = r, y = 0;
    int err = 1 - r;
    while (x >= y) {
        gfx_putpixel(cx + x, cy + y, color);
        gfx_putpixel(cx + y, cy + x, color);
        gfx_putpixel(cx - y, cy + x, color);
        gfx_putpixel(cx - x, cy + y, color);
        gfx_putpixel(cx - x, cy - y, color);
        gfx_putpixel(cx - y, cy - x, color);
        gfx_putpixel(cx + y, cy - x, color);
        gfx_putpixel(cx + x, cy - y, color);
        y++;
        if (err < 0) {
            err += 2 * y + 1;
        } else {
            x--;
            err += 2 * (y - x) + 1;
        }
    }
}

/* 填充圆 */
void gfx_fillcircle(int cx, int cy, int r, uint8_t color)
{
    if (r <= 0) return;
    for (int y = -r; y <= r; y++) {
        for (int x = -r; x <= r; x++) {
            if (x * x + y * y <= r * r) {
                gfx_putpixel(cx + x, cy + y, color);
            }
        }
    }
}

/* 画椭圆 */
void gfx_ellipse(int cx, int cy, int rx, int ry, uint8_t color)
{
    if (rx <= 0 || ry <= 0) return;
    for (int angle = 0; angle < 360; angle++) {
        int x = cx + (int)(rx * 3.14159 * angle / 180.0 / 3.14159);
        int y = cy + (int)(ry * 3.14159 * angle / 180.0 / 3.14159);
        gfx_putpixel(x, y, color);
    }
}

/* 位图blit (从内存拷贝到后台缓冲区) */
void gfx_blit(int x, int y, int w, int h, const uint8_t *data)
{
    if (!data) return;
    for (int row = 0; row < h; row++) {
        for (int col = 0; col < w; col++) {
            uint8_t color = data[row * w + col];
            if (color != 0xFF) {  /* 0xFF = 透明色 */
                gfx_putpixel(x + col, y + row, color);
            }
        }
    }
}

/* 带透明度的位图blit (alpha值0=透明, 255=不透明) */
void gfx_blit_alpha(int x, int y, int w, int h, const uint8_t *data, uint8_t alpha)
{
    if (!data || alpha == 0) return;
    for (int row = 0; row < h; row++) {
        for (int col = 0; col < w; col++) {
            uint8_t color = data[row * w + col];
            if (color != 0xFF) {
                if (alpha == 255) {
                    gfx_putpixel(x + col, y + row, color);
                } else {
                    /* 简单alpha混合：与黑色混合 */
                    uint8_t bg = gfx_getpixel(x + col, y + row);
                    uint8_t mixed = (color * alpha + bg * (255 - alpha)) / 255;
                    gfx_putpixel(x + col, y + row, mixed);
                }
            }
        }
    }
}

/* 画三角形 (边框) */
void gfx_triangle(int x1, int y1, int x2, int y2, int x3, int y3, uint8_t color)
{
    gfx_line(x1, y1, x2, y2, color);
    gfx_line(x2, y2, x3, y3, color);
    gfx_line(x3, y3, x1, y1, color);
}

/* 水平渐变填充矩形 */
void gfx_gradient_h(int x, int y, int w, int h, uint8_t c1, uint8_t c2)
{
    for (int i = 0; i < w; i++) {
        /* 简单调色板索引插值 */
        uint8_t color = c1 + (c2 - c1) * i / w;
        gfx_vline(x + i, y, h, color);
    }
}

/* 垂直渐变填充矩形 */
void gfx_gradient_v(int x, int y, int w, int h, uint8_t c1, uint8_t c2)
{
    for (int i = 0; i < h; i++) {
        uint8_t color = c1 + (c2 - c1) * i / h;
        gfx_hline(x, y + i, w, color);
    }
}
