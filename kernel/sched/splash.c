/* ============================================================================
 * 赫普OS - 开机 logo (V2.35 → V3.82 文本模式版)
 * 开机画面: 框线 + HEPU 块字符大字 + 标语 + 启动进度条动画。
 *
 * V3.82 重大变更: 由图形模式(0x13/VBE)改为纯文本模式绘制。
 * 根因: splash 切图形后, vga_gfx_text_mode 无法完整恢复 VGA 字体平面
 * (plane 2) —— 图形模式会改写 VGA 内存映射/寄存器, 切回文本后字符
 * 渲染为密集竖条纹, 内核日志与 Shell 全部不可见(用户实测花屏)。
 * 文本模式(80x25, 0xB8000)不涉及模式切换, 字体由 SeaBIOS 加载后
 * 全程保留, 零花屏风险。图形能力保留在 gfxtest/gui 命令(显式调用)。
 * ========================================================================== */

#include "vga.h"
#include "theme.h"

extern const char *hepu_kernel_version;

/* 忙等延时 (约 200ms @ QEMU) */
static void splash_delay(void)
{
    for (volatile int i = 0; i < 8000000; i++);
}

/* 整数转字符串 (百分比显示) */
static void itoa(int v, char *buf)
{
    char tmp[8];
    int n = 0;
    if (v == 0) { buf[0] = '0'; buf[1] = 0; return; }
    while (v > 0) { tmp[n++] = '0' + (v % 10); v /= 10; }
    for (int i = 0; i < n; i++) buf[i] = tmp[n - 1 - i];
    buf[n] = 0;
}

void splash_show(void)
{
    /* 主题初始化 (确保颜色常量可用) */
    theme_init();

    /* ---- 文本模式开机画面 (80x25) ---- */
    vga_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLUE);
    vga_clear();

    /* 顶部框线 */
    vga_gotoxy(0, 2);
    vga_puts("\xDA");                    /* ┌ */
    for (int i = 0; i < 36; i++) vga_puts("\xC4"); /* ─ */
    vga_puts("\xBF");                    /* ┐ */

    /* HEPU 块字符大字 (4 字母 x 6 宽, 5 行) */
    const char *logo[5] = {
        " \xDB\xDB\xDB  \xDB\xDB\xDB  \xDB\xDB\xDB\xDB \xDB\xDB \xDB\xDB ",
        " \xDB\x20\xDB  \xDB\x20\xDB  \xDB\x20\x20\xDB \xDB\x20\x20\xDB ",
        " \xDB\xDB\xDB  \xDB\xDB\xDB  \xDB\xDB\xDB\xDB \xDB\x20\x20\xDB ",
        " \xDB\x20\xDB  \xDB\x20\xDB  \xDB\x20\x20\xDB \xDB\x20\x20\xDB ",
        " \xDB\x20\xDB  \xDB\xDB\xDB  \xDB\x20\x20\xDB \xDB\xDB \xDB\xDB "
    };
    for (int r = 0; r < 5; r++) {
        vga_gotoxy(2 + r, 4);
        vga_puts(logo[r]);
    }

    /* 品牌分隔线 */
    vga_gotoxy(8, 2);
    vga_puts("\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4"
             "\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4"
             "\xC4\xC4\xC4\xC4");

    /* 标语 (英文——文本模式仅 CP437 字符集) */
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    vga_gotoxy(10, 12);
    vga_puts("HYPER EVOLVE OS");
    vga_gotoxy(11, 12);
    vga_puts("Microkernel + .hug apps");

    /* 版本 + 开源声明 */
    {
        char vbuf[28];
        int n = 0;
        const char *src = "Version ";
        while (*src && n < 26) vbuf[n++] = *src++;
        if (hepu_kernel_version) {
            const char *vv = hepu_kernel_version;
            while (*vv && n < 26) vbuf[n++] = *vv++;
        }
        vbuf[n] = 0;
        vga_gotoxy(12, 14);
        vga_puts(vbuf);
    }
    vga_gotoxy(13, 13);
    vga_puts("Open Source - GPLv3");

    /* 底部框线 */
    vga_gotoxy(14, 2);
    vga_puts("\xC0");                    /* └ */
    for (int i = 0; i < 36; i++) vga_puts("\xC4");
    vga_puts("\xD9");                    /* ┘ */

    /* ---- 启动进度条动画 (0% -> 100%) ---- */
    for (int p = 0; p <= 100; p += 10) {
        int filled = p / 10;
        char bar[12];
        for (int i = 0; i < 10; i++)
            bar[i] = (i < filled) ? '\xDB' : '\xB0';  /* █ vs ░ */
        bar[10] = 0;
        char pbuf[6];
        itoa(p, pbuf);
        pbuf[4] = '%'; pbuf[5] = 0;
        vga_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLUE);
        vga_gotoxy(16, 8);
        vga_puts("Booting [");
        vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
        vga_puts(bar);
        vga_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLUE);
        vga_puts("] ");
        vga_puts(pbuf);
        splash_delay();
    }

    /* 保持最后一帧约 0.4s */
    splash_delay();

    /* 清屏交还启动日志 (文本模式, 无模式切换) */
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    vga_clear();
}
