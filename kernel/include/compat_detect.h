/* ============================================================================
 * 赫普OS - 自动兼容检测模块头文件 (V0.10.5)
 * ========================================================================== */

#ifndef COMPAT_DETECT_H
#define COMPAT_DETECT_H

#include <stdint.h>

/* 兼容级别 */
typedef enum {
    COMPAT_LEVEL_MINIMAL = 0,
    COMPAT_LEVEL_BASIC = 1,
    COMPAT_LEVEL_STANDARD = 2,
    COMPAT_LEVEL_ENHANCED = 3,
    COMPAT_LEVEL_FULL = 4,
} compat_level_t;

/* 检测结果结构 */
typedef struct {
    int is_uefi;
    int boot_drive;
    int int13_ext_supported;
    int int13_version;
    int dma_supported;
    int ahci_supported;
    int vga_supported;
    int vbe_supported;
    int vbe_version;
    int max_width;
    int max_height;
    int max_bpp;
    int cpu_64bit;
    int pae_supported;
    int sse_supported;
    int apic_supported;
    uint32_t memory_kb;
    uint32_t usable_memory_kb;
    compat_level_t compat_level;
    int safe_mode;
} compat_info_t;

/* 初始化兼容检测 */
void autocompat_init(void);

/* 获取兼容信息 */
compat_info_t *compat_get_info(void);

/* 查询兼容级别 */
int compat_get_level(void);

/* 查询是否安全模式 */
int compat_is_safe_mode(void);

/* 查询是否UEFI */
int compat_is_uefi(void);

/* 查询VBE支持 */
int compat_vbe_supported(void);

/* 查询INT13扩展支持 */
int compat_int13_supported(void);

/* 检查是否应该使用文本模式 */
int compat_use_text_mode(void);

#endif /* COMPAT_DETECT_H */
