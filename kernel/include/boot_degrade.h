/* ============================================================================
 * 赫普OS - 启动降级保护模块头文件 (V0.10.5)
 * ========================================================================== */

#ifndef BOOT_DEGRADE_H
#define BOOT_DEGRADE_H

#include <stdint.h>

/* 降级级别 */
typedef enum {
    BOOT_MODE_GRAPHICAL = 0,    /* 图形模式（桌面） */
    BOOT_MODE_VGA_GFX = 1,      /* VGA图形模式（320x200） */
    BOOT_MODE_TEXT = 2,         /* VGA文本模式（80x25） */
    BOOT_MODE_SERIAL = 3        /* 仅串口输出（无显示） */
} boot_mode_t;

/* 初始化降级模块 */
void boot_degrade_init(void);

/* 请求降级到下一级别，返回0成功，-1已到最低级 */
int boot_degrade_request(const char *reason);

/* 获取当前启动模式 */
boot_mode_t boot_degrade_get_mode(void);

/* 检查是否应该使用图形模式 */
int boot_degrade_use_graphics(void);

/* 检查是否应该使用文本模式 */
int boot_degrade_use_text(void);

/* 检查是否应该启动桌面 */
int boot_degrade_should_start_desktop(void);

/* 获取降级次数 */
int boot_degrade_get_count(void);

/* 获取降级原因 */
const char *boot_degrade_get_reason(void);

/* 打印降级状态 */
void boot_degrade_print_status(void);

/* 在文本模式下显示错误信息 */
void boot_degrade_show_text_error(const char *title, const char *msg);

#endif /* BOOT_DEGRADE_H */
