/* ============================================================================
 * 赫普OS - CJK 渲染管线 (V4.22-E67)
 *
 * 1) cjk_init(): 把 16x16 点阵写入 VGA 文本模式字符生成器 plane 2——
 *    每汉字占 2 个 8x16 槽 (0x80+n*2=左半, +1=右半), 最多 64 字.
 * 2) cjk_puts_utf8(): 解码 3 字节 UTF-8 → 查点阵表 → 输出 2 字符.
 * 3) vga_puts 集成: 遇 0xE0..0xEF 开头字节走 CJK 管线, 未知字显 '?'.
 * ========================================================================== */

#include <stdint.h>
#include "font_cjk16.h"
#include "font_cjk_big.h"
#include "vga.h"

static inline void outb(uint16_t port, uint8_t val)
{
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t v;
    __asm__ volatile ("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

/* V4.23-E68: 槽位管理——64 字滑动窗口 (128 槽 = 每字 2 槽)
 * 启动静态加载前 32 字(核心文案), 其余动态 LRU 替换 */
#define CJK_SLOTS 64
static uint32_t cjk_slot_uni[CJK_SLOTS];
static uint8_t  cjk_slot_age[CJK_SLOTS];
static int      cjk_slot_used = 0;
static uint32_t cjk_hits = 0, cjk_misses = 0, cjk_loads = 0;

/* 写 plane 2 的一个 8x16 槽 */
static void cjk_write_slot(uint8_t slot, const uint8_t *glyph)
{
    uint8_t *dst = (uint8_t *)0xA0000 + (uint32_t)slot * 32;
    for (int r = 0; r < 16; r++) dst[r] = glyph[r];
}

/* 写一字到 2 槽 (含寄存器序列) */
static void cjk_load_glyph(int slot, const cjk_glyph_t *g)
{
    outb(0x3C4, 0x04); outb(0x3C5, 0x02);   /* SR4 关奇偶 */
    outb(0x3C4, 0x02); outb(0x3C5, 0x04);   /* SR2 plane 2 */
    outb(0x3CE, 0x05); outb(0x3CF, 0x00);   /* GC5 读模式 0 */
    outb(0x3CE, 0x06); outb(0x3CF, 0x04);   /* GC6 文本段 */
    inb(0x3DA);
    uint8_t left[16], right[16];
    for (int r = 0; r < 16; r++) {
        left[r]  = g->glyph[r * 2];
        right[r] = g->glyph[r * 2 + 1];
    }
    cjk_write_slot((uint8_t)(0x80 + slot * 2), left);
    cjk_write_slot((uint8_t)(0x80 + slot * 2 + 1), right);
    outb(0x3CE, 0x06); outb(0x3CF, 0x0E);   /* GC6 恢复 */
    outb(0x3C4, 0x02); outb(0x3C5, 0x0F);   /* SR2 恢复 */
    outb(0x3C4, 0x04); outb(0x3C5, 0x03);   /* SR4 恢复 */
    cjk_slot_uni[slot] = g->unicode;
    cjk_loads++;
}

/* 查字: 命中槽或从大表动态加载; 返回 1 可输出 */
static int cjk_find_or_load(uint32_t u)
{
    /* 1. 已加载槽 */
    for (int i = 0; i < cjk_slot_used; i++) {
        if (cjk_slot_uni[i] == u) {
            cjk_slot_age[i] = (uint8_t)(cjk_slot_age[i] + 1);
            cjk_hits++;
            return i;
        }
    }
    cjk_misses++;
    /* 2. 大表 (V4.28-E73: GB2312 一级 3755 字, unicode 有序 -> 二分) */
    int lo = 0, hi = (int)HEPU_CJK_BIG_COUNT - 1;
    int found = -1;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        if (hepu_cjk_big[mid].unicode == u) { found = mid; break; }
        if (hepu_cjk_big[mid].unicode < u) lo = mid + 1;
        else hi = mid - 1;
    }
    if (found >= 0) {
        if (cjk_slot_used < CJK_SLOTS) {
            int slot = cjk_slot_used++;
            cjk_load_glyph(slot, &hepu_cjk_big[found]);
            return slot;
        }
        /* LRU: 替换 age 最小 */
        int oldest = 0;
        for (int j = 1; j < CJK_SLOTS; j++)
            if (cjk_slot_age[j] < cjk_slot_age[oldest]) oldest = j;
        cjk_load_glyph(oldest, &hepu_cjk_big[found]);
        return oldest;
    }
    return -1;  /* 不在字库 */
}

void cjk_init(void)
{
    /* 标准字体写入序列 (Linux vgacon 同款):
     * 1) SR4=0x02 关闭奇偶模式(平面写使能)  2) SR2=0x04 Map Mask plane 2
     * 3) GC5=0 读模式0  4) GC6=0x04 文本段 0xA0000 */
    outb(0x3C4, 0x04); outb(0x3C5, 0x02);   /* SR4 = 2: 关闭奇偶 */
    outb(0x3C4, 0x02); outb(0x3C5, 0x04);   /* SR2 = 4: Map Mask plane 2 */
    outb(0x3CE, 0x05); outb(0x3CF, 0x00);   /* GC5 = 0 */
    outb(0x3CE, 0x06); outb(0x3CF, 0x04);   /* GC6 = 0x04: 文本段 0xA0000 */
    inb(0x3DA);                             /* 寄存器同步 */

    for (int i = 0; i < (int)HEPU_CJK_FONT_COUNT; i++) {
        /* 手写演示子集 → 槽 0-5 */
        uint8_t left[16], right[16];
        for (int r = 0; r < 16; r++) {
            left[r]  = hepu_cjk_font[i].glyph[r * 2];
            right[r] = hepu_cjk_font[i].glyph[r * 2 + 1];
        }
        cjk_write_slot((uint8_t)(0x80 + i * 2), left);
        cjk_write_slot((uint8_t)(0x80 + i * 2 + 1), right);
        cjk_slot_uni[i] = hepu_cjk_font[i].unicode;
    }
    /* V4.23-E68: 静态加载大库前 (32-6)=26 字 (核心文案) */
    int npre = 26;
    if (HEPU_CJK_BIG_COUNT < npre) npre = (int)HEPU_CJK_BIG_COUNT;
    for (int i = 0; i < npre; i++) {
        cjk_load_glyph((int)HEPU_CJK_FONT_COUNT + i, &hepu_cjk_big[i]);
    }
    cjk_slot_used = (int)HEPU_CJK_FONT_COUNT + npre;

    outb(0x3CE, 0x06); outb(0x3CF, 0x0E);   /* GC6 恢复 */
    outb(0x3C4, 0x02); outb(0x3C5, 0x0F);   /* SR2 恢复 */
    outb(0x3C4, 0x04); outb(0x3C5, 0x03);   /* SR4 恢复奇偶 */

    vga_printf("[CJK] font: %d glyphs (%dKB), %d slots preloaded\n",
               (int)HEPU_CJK_BIG_COUNT,
               (int)(HEPU_CJK_BIG_COUNT * 32 / 1024),
               cjk_slot_used);
}

/* V4.22-E67: 读回验证——槽 0x80(左半"中") 与点阵表比对 */
void cjk_write_slot_check(void)
{
    outb(0x3C4, 0x04); outb(0x3C5, 0x03);   /* SR4 = 3 正常奇偶 */
    outb(0x3CE, 0x04); outb(0x3CF, 0x02);   /* GC4 = 2: 读 plane 2 */
    outb(0x3CE, 0x05); outb(0x3CF, 0x00);   /* GC5 = 0 读模式 */
    outb(0x3CE, 0x06); outb(0x3CF, 0x04);   /* GC6 = 0x04: 文本段可读 */
    inb(0x3DA);
    int ok = 1;
    const uint8_t *g = hepu_cjk_font[0].glyph;   /* 中 */
    uint8_t *src = (uint8_t *)0xA0000 + 0x80 * 32;
    for (int r = 0; r < 16; r++) {
        if (src[r] != g[r * 2]) { ok = 0; break; }
    }
    outb(0x3CE, 0x04); outb(0x3CF, 0x00);   /* 恢复读平面 0 */
    outb(0x3CE, 0x06); outb(0x3CF, 0x0E);   /* GC6 恢复文本 */
    vga_printf("cjk check: %s\n", ok ? "MATCH (plane2 glyph ok)" : "FAIL");
    if (!ok) {
        /* debug: 读回前 4 字节 vs 期望 */
        uint8_t *src2 = (uint8_t *)0xA0000 + 0x80 * 32;
        for (int r = 0; r < 4; r++) {
            vga_printf("  row%d: got 0x%02x expect 0x%02x\n", r,
                       (unsigned)src2[r], (unsigned)g[r * 2]);
        }
    }
}

/* 处理 3 字节 UTF-8 序列; 返回 1=已处理(含未知字), 0=非 3 字节 */
int cjk_puts_utf8(const char **pp)
{
    const unsigned char *p = (const unsigned char *)*pp;
    if (p[0] < 0xE0 || p[0] > 0xEF) return 0;
    if (p[1] == 0 || p[2] == 0) return 0;
    uint32_t u = ((uint32_t)(p[0] & 0x0F) << 12) |
                 ((uint32_t)(p[1] & 0x3F) << 6) |
                 (uint32_t)(p[2] & 0x3F);
    *pp += 3;
    int slot = cjk_find_or_load(u);
    if (slot < 0) {
        vga_putc('?');
        return 1;
    }
    vga_putc((char)(0x80 + slot * 2));
    vga_putc((char)(0x80 + slot * 2 + 1));
    return 1;
}

/* V4.25-E70: 供 IME 等外部按 unicode 渲染单字; 返回 0 成功 */
int cjk_render_unicode(uint32_t u)
{
    int slot = cjk_find_or_load(u);
    if (slot < 0) return -1;
    vga_putc((char)(0x80 + slot * 2));
    vga_putc((char)(0x80 + slot * 2 + 1));
    return 0;
}

/* V4.23-E68: 统计 */
void cjk_stats(void)
{
    vga_printf("[CJK] glyphs=%d slots=%d/%d hits=%u misses=%u loads=%u\n",
               (int)HEPU_CJK_BIG_COUNT, cjk_slot_used, CJK_SLOTS,
               cjk_hits, cjk_misses, cjk_loads);
    /* V4.55-E100: GB2312 覆盖率统计 */
    {
        int zh_count = 2151;   /* ZH_FONT_COUNT from font_zh_data.h */
        int gb_total = 6763;   /* GB2312 总字数 */
        int pct = zh_count * 100 / gb_total;
        vga_printf("[CJK] GB2312 coverage: %d/%d = %d%% (level1+common)\n",
                   zh_count, gb_total, pct);
    }
}
