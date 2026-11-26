/* ============================================================================
 * 赫普OS 64位 - 启动参数解析实现
 *
 * 直接读取引导器(hepboot UEFI)构建在低内存的 multiboot info 结构,
 * 解析 cmdline 中的 hepu.* 参数。布局与 tools/hepboot/uefi/hepboot_uefi.c
 * 中的 multiboot_info_t 保持一致。
 *
 * 支持参数：
 *   hepu.graphics=0   保护模式: 禁用图形桌面, 进入文本模式(强制, 运行时不可反开)
 *   hepu.nographics=1 旧名同义参数, 同样按强制文本模式处理
 *   hepu.dev=1       开发者模式
 *   hepu.lock=0      关闭开机锁屏(默认开启)
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "bootopts64.h"

/* 与引导器 multiboot_info_t 同布局(只取到 cmdline 字段, 后面字段保留对齐) */
typedef struct {
    uint32_t flags;
    uint32_t mem_lower;
    uint32_t mem_upper;
    uint32_t boot_device;
    uint32_t cmdline;
    uint32_t mods_count;
    uint32_t mods_addr;
    uint32_t syms[4];
    uint32_t mmap_length;
    uint32_t mmap_addr;
    uint32_t drives_length;
    uint32_t drives_addr;
    uint32_t config_table;
    uint32_t boot_loader_name;
    uint32_t apm_table;
    uint32_t vbe_control_info;
    uint32_t vbe_mode_info;
    uint16_t vbe_mode;
    uint16_t vbe_interface_seg;
    uint16_t vbe_interface_off;
    uint16_t vbe_interface_len;
    uint64_t framebuffer_addr;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint8_t  framebuffer_bpp;
    uint8_t  framebuffer_type;
    uint8_t  color_info[6];
} __attribute__((packed)) mb_info64_t;

static int s_nographics = 0;
static int s_nographics_forced = 0;
static int s_dev = 0;
static int s_lock_disabled = 0;
static int s_svcprobe = 0;
static const char *s_cmdline = 0;

/* desktop64.c 中定义的全局文本模式标志 */
extern int hepu_nographics;
extern void vga_puts(const char *s);

static int has_param(const char *line, const char *key)
{
    if (!line || !key) return 0;
    return strstr(line, key) != 0;
}

void bootopts64_init(uint32_t magic, uint64_t mb_info_addr)
{
    (void)magic;
    const mb_info64_t *mb = (const mb_info64_t *)(uintptr_t)mb_info_addr;
    if (!mb_info_addr) return;
    if (!(mb->flags & (1u << 2))) return;   /* bit2 = cmdline 有效 */

    const char *cmdline = (const char *)(uintptr_t)mb->cmdline;
    if (!cmdline || !cmdline[0]) return;
    s_cmdline = cmdline;

    if (has_param(cmdline, "hepu.graphics=0") ||
        has_param(cmdline, "hepu.nographics=1")) {
        s_nographics = 1;
        s_nographics_forced = 1;
        hepu_nographics = 1;
    }
    if (has_param(cmdline, "hepu.dev=1")) s_dev = 1;
    if (has_param(cmdline, "hepu.lock=0")) s_lock_disabled = 1;
    if (has_param(cmdline, "hepu.svcprobe=1")) s_svcprobe = 1;

    /* 保护模式标记: 只在 graphics=0 时输出, 正常模式不新增任何日志 */
    if (s_nographics) {
        vga_puts("[PROTECT] hepu.graphics=0: graphics disabled, text terminal only (arch=x86_64)\n");
    }
}

int bootopts_nographics(void)
{
    return s_nographics;
}

int bootopts_dev(void)
{
    return s_dev;
}

int bootopts_nographics_forced(void)
{
    return s_nographics_forced;
}

const char *bootopts_cmdline(void)
{
    return s_cmdline;
}

int bootopts_lock_disabled(void)
{
    return s_lock_disabled;
}

int bootopts_svcprobe(void)
{
    return s_svcprobe;
}
