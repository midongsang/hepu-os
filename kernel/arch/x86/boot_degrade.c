/* ============================================================================
 * 赫普OS - 启动降级保护模块 (V0.10.5)
 *
 * 功能：
 *   1. 图形模式初始化失败时自动降级到文本模式
 *   2. 桌面启动失败时自动进入文本shell
 *   3. 记录降级原因，便于调试
 *   4. 三模式降级：VBE高分辨率 -> VGA图形 -> VGA文本
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include "string.h"

/* 降级级别 */
typedef enum {
    BOOT_MODE_GRAPHICAL = 0,    /* 图形模式（桌面） */
    BOOT_MODE_VGA_GFX = 1,      /* VGA图形模式（320x200） */
    BOOT_MODE_TEXT = 2,         /* VGA文本模式（80x25） */
    BOOT_MODE_SERIAL = 3        /* 仅串口输出（无显示） */
} boot_mode_t;

static boot_mode_t current_mode = BOOT_MODE_GRAPHICAL;
static boot_mode_t max_mode = BOOT_MODE_GRAPHICAL;
static int degrade_count = 0;
static char degrade_reason[256] = {0};

/* 外部函数声明 */
extern void serial_puts(uint16_t port, const char *s);
extern void vga_puts(const char *s);
extern void vga_clear(void);

/* 初始化降级模块 */
void boot_degrade_init(void)
{
    current_mode = BOOT_MODE_GRAPHICAL;
    max_mode = BOOT_MODE_GRAPHICAL;
    degrade_count = 0;
    degrade_reason[0] = 0;
    serial_puts(0x3F8, "[DEGRADE] init: mode=GRAPHICAL\n");
}

/* 请求降级到下一级别 */
int boot_degrade_request(const char *reason)
{
    if (current_mode >= BOOT_MODE_SERIAL) {
        serial_puts(0x3F8, "[DEGRADE] already at lowest mode (SERIAL)\n");
        return -1;
    }

    /* 记录原因 */
    if (reason && reason[0]) {
        int len = 0;
        while (reason[len] && len < 254) {
            degrade_reason[len] = reason[len];
            len++;
        }
        degrade_reason[len] = 0;
    }

    /* 降级 */
    current_mode++;
    degrade_count++;

    serial_puts(0x3F8, "[DEGRADE] degraded to mode ");
    serial_puts(0x3F8, (current_mode == BOOT_MODE_VGA_GFX) ? "VGA_GFX" :
                       (current_mode == BOOT_MODE_TEXT) ? "TEXT" :
                       (current_mode == BOOT_MODE_SERIAL) ? "SERIAL" : "?");
    serial_puts(0x3F8, "\n");

    if (reason && reason[0]) {
        serial_puts(0x3F8, "[DEGRADE] reason: ");
        serial_puts(0x3F8, reason);
        serial_puts(0x3F8, "\n");
    }

    /* 执行降级操作 */
    switch (current_mode) {
        case BOOT_MODE_VGA_GFX:
            /* 尝试VGA图形模式13h */
            serial_puts(0x3F8, "[DEGRADE] trying VGA mode 13h...\n");
            /* 这里可以调用vga_gfx_init() */
            break;
        case BOOT_MODE_TEXT:
            /* 切换到VGA文本模式 */
            serial_puts(0x3F8, "[DEGRADE] switching to VGA text mode...\n");
            /* 这里可以调用vga_init()或直接设置文本模式 */
            break;
        case BOOT_MODE_SERIAL:
            /* 仅串口输出 */
            serial_puts(0x3F8, "[DEGRADE] serial-only mode\n");
            break;
        default:
            break;
    }

    return 0;
}

/* 获取当前启动模式 */
boot_mode_t boot_degrade_get_mode(void)
{
    return current_mode;
}

/* 检查是否应该使用图形模式 */
int boot_degrade_use_graphics(void)
{
    return (current_mode <= BOOT_MODE_VGA_GFX);
}

/* 检查是否应该使用文本模式 */
int boot_degrade_use_text(void)
{
    return (current_mode >= BOOT_MODE_TEXT);
}

/* 检查是否应该启动桌面 */
int boot_degrade_should_start_desktop(void)
{
    return (current_mode == BOOT_MODE_GRAPHICAL);
}

/* 获取降级次数 */
int boot_degrade_get_count(void)
{
    return degrade_count;
}

/* 获取降级原因 */
const char *boot_degrade_get_reason(void)
{
    return degrade_reason;
}

/* 打印降级状态 */
void boot_degrade_print_status(void)
{
    serial_puts(0x3F8, "[DEGRADE] status: mode=");
    serial_puts(0x3F8, (current_mode == BOOT_MODE_GRAPHICAL) ? "GRAPHICAL" :
                       (current_mode == BOOT_MODE_VGA_GFX) ? "VGA_GFX" :
                       (current_mode == BOOT_MODE_TEXT) ? "TEXT" : "SERIAL");
    serial_puts(0x3F8, ", degrades=");
    {
        char buf[2];
        buf[0] = '0' + degrade_count;
        buf[1] = 0;
        serial_puts(0x3F8, buf);
    }
    serial_puts(0x3F8, "\n");

    if (degrade_reason[0]) {
        serial_puts(0x3F8, "[DEGRADE] last reason: ");
        serial_puts(0x3F8, degrade_reason);
        serial_puts(0x3F8, "\n");
    }
}

/* 在文本模式下显示错误信息 */
void boot_degrade_show_text_error(const char *title, const char *msg)
{
    /* 清屏 */
    vga_clear();

    /* 显示标题 */
    vga_puts("========================================\n");
    vga_puts("  HepuOS 启动降级保护\n");
    vga_puts("========================================\n\n");

    if (title) {
        vga_puts("  ");
        vga_puts(title);
        vga_puts("\n\n");
    }

    if (msg) {
        vga_puts("  ");
        vga_puts(msg);
        vga_puts("\n\n");
    }

    vga_puts("  系统已自动降级到文本模式\n");
    vga_puts("  您可以使用命令行继续操作\n\n");
    vga_puts("  输入 'help' 查看可用命令\n");
    vga_puts("========================================\n");
}
