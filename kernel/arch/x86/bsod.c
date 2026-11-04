/* ============================================================================
 * 赫普OS - 蓝屏死机 (Blue Screen of Death) 界面
 *
 * 显示逻辑：
 *   1. 优先图形模式（320x200 256色），蓝色背景+白色文字
 *   2. 图形模式失败则降级到VGA文本模式
 *   3. 同时输出到串口
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include "bsod.h"
#include "vga.h"
#include "vga_gfx.h"
#include "serial.h"

/* 异常名称表（与 idt.c 中 exception_messages 对应） */
static const char *bsod_exception_names[] = {
    "Division By Zero",        /* 0  */
    "Debug",                    /* 1  */
    "NMI Interrupt",            /* 2  */
    "Breakpoint",               /* 3  */
    "Overflow",                 /* 4  */
    "Bound Range Exceeded",     /* 5  */
    "Invalid Opcode",           /* 6  */
    "Device Not Available",     /* 7  */
    "Double Fault",             /* 8  */
    "Coprocessor Overrun",      /* 9  */
    "Invalid TSS",              /* 10 */
    "Segment Not Present",      /* 11 */
    "Stack Fault",              /* 12 */
    "General Protection Fault", /* 13 */
    "Page Fault",               /* 14 */
    "Reserved",                 /* 15 */
    "x87 FPU Error",            /* 16 */
    "Alignment Check",          /* 17 */
    "Machine Check",            /* 18 */
    "SIMD FPU Exception",       /* 19 */
    "Virtualization Exception", /* 20 */
    "Reserved", "Reserved", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved",
    "Reserved", "Reserved"      /* 21-31 */
};

/* 将32位整数转为十六进制字符串（8位） */
static void bsod_hex32(uint32_t val, char *buf)
{
    static const char hex[] = "0123456789ABCDEF";
    buf[0] = '0'; buf[1] = 'x';
    for (int i = 0; i < 8; i++) {
        buf[2 + i] = hex[(val >> (28 - i * 4)) & 0xF];
    }
    buf[10] = '\0';
}

/* 串口输出崩溃信息 */
static void bsod_serial_output(const char *reason, uint32_t isr_number,
                                uint32_t error_code, uint32_t eip,
                                uint32_t cs, uint32_t esp)
{
    char hexbuf[12];
    serial_puts(0x3F8, "\r\n=== HEPU OS CRASH (BSOD) ===\r\n");
    serial_puts(0x3F8, "Reason: ");
    serial_puts(0x3F8, reason);
    serial_puts(0x3F8, "\r\nException: ");
    if (isr_number < 32) {
        serial_puts(0x3F8, bsod_exception_names[isr_number]);
    } else {
        serial_puts(0x3F8, "Unknown");
    }
    serial_puts(0x3F8, " (ISR ");
    /* 自定义十进制输出 */
    {
        char numbuf[12];
        uint32_t n = isr_number;
        int i = 0;
        if (n == 0) numbuf[i++] = '0';
        while (n > 0) { numbuf[i++] = '0' + (n % 10); n /= 10; }
        numbuf[i] = '\0';
        for (int j = 0; j < i / 2; j++) {
            char t = numbuf[j]; numbuf[j] = numbuf[i-1-j]; numbuf[i-1-j] = t;
        }
        serial_puts(0x3F8, numbuf);
    }
    serial_puts(0x3F8, ")\r\n");
    serial_puts(0x3F8, "Error Code: ");
    bsod_hex32(error_code, hexbuf);
    serial_puts(0x3F8, hexbuf);
    serial_puts(0x3F8, "\r\nEIP: ");
    bsod_hex32(eip, hexbuf);
    serial_puts(0x3F8, hexbuf);
    serial_puts(0x3F8, "\r\nCS: ");
    bsod_hex32(cs, hexbuf);
    serial_puts(0x3F8, hexbuf);
    serial_puts(0x3F8, "\r\nESP: ");
    bsod_hex32(esp, hexbuf);
    serial_puts(0x3F8, hexbuf);
    serial_puts(0x3F8, "\r\n==============================\r\n");
}

/* 文本模式降级显示 */
void bsod_text_mode(const char *reason, uint32_t isr_number,
                     uint32_t error_code, uint32_t eip,
                     uint32_t cs, uint32_t esp)
{
    char hexbuf[12];

    /* 确保在文本模式 */
    vga_init();

    /* 清屏为蓝色背景 */
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    vga_clear();

    /* 标题 */
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    vga_puts("\n\n");
    vga_puts("  :(  赫普OS遇到问题，需要重新启动\n");
    vga_puts("  --------------------------------\n\n");

    /* 崩溃原因 */
    vga_puts("  崩溃原因: ");
    vga_puts(reason);
    vga_puts("\n\n");

    /* 异常类型 */
    vga_puts("  异常类型: ");
    if (isr_number < 32) {
        vga_puts(bsod_exception_names[isr_number]);
    } else {
        vga_puts("Unknown");
    }
    vga_puts(" (ISR ");
    /* 简单数字输出 */
    {
        char numbuf[12];
        int n = isr_number, i = 0;
        if (n == 0) numbuf[i++] = '0';
        while (n > 0) { numbuf[i++] = '0' + (n % 10); n /= 10; }
        numbuf[i] = '\0';
        /* 反转 */
        for (int j = 0; j < i / 2; j++) {
            char t = numbuf[j]; numbuf[j] = numbuf[i-1-j]; numbuf[i-1-j] = t;
        }
        vga_puts(numbuf);
    }
    vga_puts(")\n\n");

    /* 错误代码 */
    vga_puts("  错误代码: ");
    bsod_hex32(error_code, hexbuf);
    vga_puts(hexbuf);
    vga_puts("\n\n");

    /* 寄存器信息 */
    vga_puts("  崩溃地址:\n");
    vga_puts("    EIP = ");
    bsod_hex32(eip, hexbuf);
    vga_puts(hexbuf);
    vga_puts("\n    CS  = ");
    bsod_hex32(cs, hexbuf);
    vga_puts(hexbuf);
    vga_puts("\n    ESP = ");
    bsod_hex32(esp, hexbuf);
    vga_puts(hexbuf);
    vga_puts("\n\n");

    /* 底部提示 */
    vga_puts("  收集错误信息完成...\n");
    vga_puts("  请记录错误代码后按重启键重新启动系统\n");

    /* 同时输出到串口 */
    bsod_serial_output(reason, isr_number, error_code, eip, cs, esp);
}

/* 图形模式BSOD绘制 */
static void bsod_gfx_draw(const char *reason, uint32_t isr_number,
                           uint32_t error_code, uint32_t eip,
                           uint32_t cs, uint32_t esp)
{
    char hexbuf[12];
    char linebuf[80];

    /* 蓝色背景 */
    gfx_fillrect(0, 0, GFX_WIDTH, GFX_HEIGHT, GFX_COLOR_BLUE);

    /* 顶部白色细条 */
    gfx_fillrect(0, 0, GFX_WIDTH, 2, GFX_COLOR_WHITE);

    /* 标题区域 */
    gfx_puts(20, 20, ": (", GFX_COLOR_WHITE);
    gfx_puts(20, 40, "赫普OS遇到问题", GFX_COLOR_WHITE);
    gfx_puts(20, 52, "需要重新启动", GFX_COLOR_WHITE);

    /* 分隔线 */
    gfx_line(20, 70, 300, 70, GFX_COLOR_LIGHTGREY);

    /* 崩溃原因 */
    gfx_puts(20, 80, "崩溃原因:", GFX_COLOR_WHITE);
    gfx_puts(20, 92, reason, GFX_COLOR_WHITE);

    /* 异常类型 */
    gfx_puts(20, 108, "异常类型:", GFX_COLOR_WHITE);
    if (isr_number < 32) {
        gfx_puts(20, 120, bsod_exception_names[isr_number], GFX_COLOR_WHITE);
    } else {
        gfx_puts(20, 120, "Unknown", GFX_COLOR_WHITE);
    }

    /* 错误代码 */
    bsod_hex32(error_code, hexbuf);
    linebuf[0] = '\0';
    /* 简单拼接 */
    char *p = linebuf;
    const char *prefix = "错误代码: ";
    while (*prefix) *p++ = *prefix++;
    const char *h = hexbuf;
    while (*h) *p++ = *h++;
    *p = '\0';
    gfx_puts(20, 136, linebuf, GFX_COLOR_WHITE);

    /* 崩溃地址 */
    gfx_puts(20, 152, "崩溃地址:", GFX_COLOR_WHITE);

    bsod_hex32(eip, hexbuf);
    p = linebuf;
    prefix = "  EIP = ";
    while (*prefix) *p++ = *prefix++;
    h = hexbuf;
    while (*h) *p++ = *h++;
    *p = '\0';
    gfx_puts(20, 164, linebuf, GFX_COLOR_WHITE);

    bsod_hex32(cs, hexbuf);
    p = linebuf;
    prefix = "  CS  = ";
    while (*prefix) *p++ = *prefix++;
    h = hexbuf;
    while (*h) *p++ = *h++;
    *p = '\0';
    gfx_puts(20, 176, linebuf, GFX_COLOR_WHITE);

    bsod_hex32(esp, hexbuf);
    p = linebuf;
    prefix = "  ESP = ";
    while (*prefix) *p++ = *prefix++;
    h = hexbuf;
    while (*h) *p++ = *h++;
    *p = '\0';
    gfx_puts(20, 188, linebuf, GFX_COLOR_WHITE);

    /* 底部提示 */
    gfx_puts(20, 200 - 16, "收集错误信息完成，请重启系统", GFX_COLOR_LIGHTGREY);
}

/* 主入口：显示蓝屏死机界面 */
void bsod_show(const char *reason, uint32_t isr_number,
               uint32_t error_code, uint32_t eip,
               uint32_t cs, uint32_t esp)
{
    /* 先输出到串口（无论图形模式是否成功） */
    bsod_serial_output(reason, isr_number, error_code, eip, cs, esp);

    /* 尝试图形模式 */
    int gfx_ok = 0;
    /* 注意：vga_gfx_init可能在某些崩溃场景下不安全（如VGA寄存器损坏），
     * 但尝试一下无妨，失败后降级到文本模式 */
    vga_gfx_init();
    /* 简单验证：检查gfx_fb_addr是否有效 */
    if (gfx_fb_addr != (uint8_t *)0) {
        gfx_ok = 1;
    }

    if (gfx_ok) {
        /* 图形模式显示 */
        bsod_gfx_draw(reason, isr_number, error_code, eip, cs, esp);
    } else {
        /* 降级到文本模式 */
        bsod_text_mode(reason, isr_number, error_code, eip, cs, esp);
    }

    /* 最终HLT等待重启 */
    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}
