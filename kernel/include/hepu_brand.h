#ifndef HEPU_BRAND_H
#define HEPU_BRAND_H

/* ============================================================================
 * 赫普OS 品牌定义
 * 基于 HEPU品牌视觉识别规范 V1.0
 * ========================================================================== */

/* 品牌名称 */
#define HEPU_BRAND_NAME       "HEPU"
#define HEPU_BRAND_NAME_CN    "赫普"
#define HEPU_BRAND_FULL       "HEPU Hyper Evolve OS"
#define HEPU_BRAND_FULL_CN    "赫普超感操作系统"
#define HEPU_BRAND_COMPANY    "赫普操作系统科技有限公司"
#define HEPU_BRAND_SLOGAN     "系统归人，数据归己，自由可控，极致纯净"

/* 品牌主色 - 赫普蓝 */
#define HEPU_COLOR_BLUE       0x1A365D
#define HEPU_COLOR_BLUE_R     0x1A
#define HEPU_COLOR_BLUE_G     0x36
#define HEPU_COLOR_BLUE_B     0x5D

/* 辅助色 - 进化绿 */
#define HEPU_COLOR_GREEN      0x00C9A7
#define HEPU_COLOR_GREEN_R    0x00
#define HEPU_COLOR_GREEN_G    0xC9
#define HEPU_COLOR_GREEN_B    0xA7

/* 中性色 */
#define HEPU_COLOR_DARK       0x1A1A2E
#define HEPU_COLOR_MEDIUM     0x6B7280
#define HEPU_COLOR_LIGHT      0xF5F7FA
#define HEPU_COLOR_WHITE      0xFFFFFF

/* VGA 16色映射（品牌色近似） */
#define HEPU_VGA_BLUE         0x01  /* 蓝色 */
#define HEPU_VGA_GREEN        0x0A  /* 亮绿 */
#define HEPU_VGA_DARK         0x00  /* 黑色 */
#define HEPU_VGA_WHITE        0x0F  /* 白色 */
#define HEPU_VGA_CYAN         0x0B  /* 青色（赫普蓝近似） */

/* 版本 */
#define HEPU_VERSION_MAJOR    0
#define HEPU_VERSION_MINOR    32
#define HEPU_VERSION_PATCH    0
#define HEPU_VERSION_STRING   "V0.32"

/* 开机画面文字 */
#define HEPU_BOOT_LINE1       "  H E P U  "
#define HEPU_BOOT_LINE2       "Hyper Evolve OS"
#define HEPU_BOOT_LINE3       "赫普超感操作系统"

#endif /* HEPU_BRAND_H */
