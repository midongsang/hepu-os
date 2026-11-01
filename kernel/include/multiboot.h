#ifndef MULTIBOOT_H
#define MULTIBOOT_H

#include <stdint.h>

/* 内存区域类型 */
#define MB_MEM_AVAILABLE 1
#define MB_MEM_RESERVED  2
#define MB_MEM_ACPI      3
#define MB_MEM_NVS       4

/* 内存区域 */
typedef struct {
    uint32_t base;
    uint32_t length;
    uint32_t type;
} mb_memory_region_t;

/* 帧缓冲信息 */
typedef struct {
    uint32_t addr;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint8_t  bpp;
    uint8_t  type;
} mb_framebuffer_t;

/* 初始化Multiboot解析 */
void multiboot_init(uint32_t magic, void *info);

/* 内存 */
uint32_t multiboot_get_memory_kb(void);
uint32_t multiboot_get_mmap(mb_memory_region_t **regions);

/* 命令行 */
const char *multiboot_get_cmdline(void);
int         multiboot_has_param(const char *key);

/* 启动模块 */
uint32_t multiboot_get_module_count(void);
int      multiboot_get_module(uint32_t index, uint32_t *start, uint32_t *end);

/* 帧缓冲 */
int multiboot_get_framebuffer(mb_framebuffer_t *fb);

/* V6.52: HepuBoot安全/恢复模式 */
int multiboot_is_safe_mode(void);
int multiboot_is_recovery_mode(void);
void multiboot_force_recovery_mode(void);

#endif
