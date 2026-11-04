/* ============================================================================
 * 赫普OS 64位 - 启动参数解析（轻量版）
 *
 * 不依赖 32 位 multiboot.c，直接读取引导器传递的 multiboot info 结构，
 * 解析 cmdline 中的 hepu.* 参数。
 *
 * 支持参数：
 *   hepu.nographics=1   禁用图形桌面，进入文本模式（强制，运行时不可反开）
 *   hepu.dev=1          开机进入开发者模式
 * ========================================================================== */

#ifndef HEPU_BOOTOPTS64_H
#define HEPU_BOOTOPTS64_H

#include <stdint.h>

/* 解析 multiboot info 中的 cmdline，设置启动参数 */
void bootopts64_init(uint32_t magic, uint64_t mb_info_addr);

/* 查询启动参数 */
int bootopts_nographics(void);   /* 1=禁用图形 */
int bootopts_dev(void);          /* 1=开发者模式 */
int bootopts_nographics_forced(void); /* 1=nographics 由 cmdline 强制，gui 不可反开 */

/* 获取原始 cmdline 字符串（可能为 NULL） */
const char *bootopts_cmdline(void);

#endif /* HEPU_BOOTOPTS64_H */
