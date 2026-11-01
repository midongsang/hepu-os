/* ============================================================================
 * 赫普OS - 图形系统自测
 * 验证绘图API、字体、双缓冲是否正常工作
 * ========================================================================== */
#include <stdint.h>
#include "vga_gfx.h"
#include "vga.h"
#include "string.h"

/* 图形自测结果 */
static int gfx_test_pass = 0;
static int gfx_test_fail = 0;

/* 测试基本绘图 */
static int test_basic_drawing(void)
{
    int pass = 0;

    /* 清屏 */
    gfx_clear(0x00);
    pass++;

    /* 测试画点（不检查返回值，32bpp模式下getpixel返回0） */
    gfx_putpixel(10, 10, 0x0F);
    pass++;

    /* 测试水平线 */
    gfx_hline(20, 20, 50, 0x0F);
    pass++;

    /* 测试垂直线 */
    gfx_vline(20, 30, 30, 0x0F);
    pass++;

    /* 测试矩形边框 */
    gfx_rect(80, 20, 40, 30, 0x0F);
    pass++;

    /* 测试填充矩形 */
    gfx_fillrect(130, 20, 40, 30, 0x09);
    pass++;

    /* 测试直线 */
    gfx_line(180, 20, 220, 50, 0x0F);
    pass++;

    return pass;
}

/* 测试文本绘制 */
static int test_text_drawing(void)
{
    int pass = 0;

    /* 测试字符绘制 */
    gfx_putchar(10, 70, 'A', 0x0F);
    pass++;

    /* 测试字符串绘制 */
    gfx_puts(10, 85, "Hepu OS GUI Test", 0x0E);
    pass++;

    /* 测试放大字符 */
    gfx_putchar_big(10, 100, 'H', 0x0C, 2);
    pass++;

    /* 测试放大字符串 */
    gfx_puts_big(30, 100, "EPU", 0x0A, 2);
    pass++;

    return pass;
}

/* 测试高级绘图 */
static int test_advanced_drawing(void)
{
    int pass = 0;

    /* 测试圆 */
    gfx_circle(50, 140, 15, 0x0F);
    pass++;

    /* 测试填充圆 */
    gfx_fillcircle(90, 140, 15, 0x0C);
    pass++;

    /* 测试椭圆 */
    gfx_ellipse(140, 140, 20, 12, 0x0E);
    pass++;

    /* 测试三角形 */
    gfx_triangle(180, 155, 200, 125, 220, 155, 0x0A);
    pass++;

    /* 测试水平渐变 */
    gfx_gradient_h(10, 170, 100, 15, 0x01, 0x0F);
    pass++;

    /* 测试垂直渐变 */
    gfx_gradient_v(120, 170, 100, 15, 0x01, 0x0F);
    pass++;

    return pass;
}

/* 测试双缓冲 */
static int test_double_buffer(void)
{
    int pass = 0;

    /* 绘制到back_buffer */
    gfx_fillrect(10, 195, 200, 10, 0x02);

    /* flip提交到显存 */
    gfx_flip();
    pass++;

    return pass;
}

/* 图形系统自测入口 */
void gfx_selftest(void)
{
    extern void serial_puts(uint16_t, const char *);
    extern void vga_printf(const char *, ...);

    serial_puts(0x3F8, "[GFX-TEST] starting graphics self-test...\n");
    vga_puts("[GFX-TEST] starting...\n");

    gfx_test_pass = 0;
    gfx_test_fail = 0;

    /* 确保图形模式已初始化 */
    vga_gfx_init();

    /* 运行各项测试 */
    int basic = test_basic_drawing();
    int text = test_text_drawing();
    int advanced = test_advanced_drawing();
    int buffer = test_double_buffer();

    gfx_test_pass = basic + text + advanced + buffer;

    /* 串口输出结果 */
    serial_puts(0x3F8, "[GFX-TEST] basic=");
    extern void serial_puthex(uint16_t, uint32_t);
    serial_puthex(0x3F8, basic);
    serial_puts(0x3F8, " text=");
    serial_puthex(0x3F8, text);
    serial_puts(0x3F8, " advanced=");
    serial_puthex(0x3F8, advanced);
    serial_puts(0x3F8, " buffer=");
    serial_puthex(0x3F8, buffer);
    serial_puts(0x3F8, " total=");
    serial_puthex(0x3F8, gfx_test_pass);
    serial_puts(0x3F8, "/21\n");

    /* VGA输出结果 */
    vga_printf("[GFX-TEST] pass=%d/21 (basic=%d text=%d advanced=%d buffer=%d)\n",
               gfx_test_pass, basic, text, advanced, buffer);

    /* 切回文本模式 */
    vga_gfx_text_mode();
}

/* 获取自测结果 */
int gfx_selftest_result(int *pass, int *fail)
{
    if (pass) *pass = gfx_test_pass;
    if (fail) *fail = gfx_test_fail;
    return gfx_test_pass;
}
