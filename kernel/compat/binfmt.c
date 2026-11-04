/* ============================================================================
 * 赫普OS - binfmt 格式注册框架
 * 魔数识别 → 分发到格式加载器
 * V3.87: 阶段 A-1 —— ELF32 识别注册 + 自检
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include "binfmt.h"
#include "elf32.h"
#include "vga.h"

#define BINFMT_MAX_FORMATS 8

static binfmt_t formats[BINFMT_MAX_FORMATS];
static uint32_t fmt_count = 0;

/* V3.87 B-3: 当前进程命令行 (shell `run` 真实传参, 单任务模型全局缓冲) */
static char g_cmdline[128];
static char g_cmdline_dirty = 0;

void binfmt_set_cmdline(const char *cmdline)
{
    uint32_t n = 0;
    g_cmdline_dirty = 1;
    if (cmdline) {
        while (cmdline[n] && n < sizeof(g_cmdline) - 1) {
            g_cmdline[n] = cmdline[n];
            n++;
        }
    }
    g_cmdline[n] = '\0';
}

const char *binfmt_get_cmdline(void)
{
    return g_cmdline_dirty ? g_cmdline : NULL;
}

/* ELF32 魔数: 0x7F 'E' 'L' 'F' */
static const uint8_t magic_elf32[4] = { 0x7F, 'E', 'L', 'F' };

int binfmt_register(const binfmt_t *fmt)
{
    if (!fmt || fmt_count >= BINFMT_MAX_FORMATS) return -1;
    formats[fmt_count++] = *fmt;
    return 0;
}

static int probe_elf32(const uint8_t *buf, uint32_t len)
{
    if (!buf || len < sizeof(elf32_ehdr_t)) return 0;
    const elf32_ehdr_t *eh = (const elf32_ehdr_t *)buf;
    /* 魔数 + 32位 + 小端 + 类型 + 机器 */
    if (eh->e_magic != ELF_MAGIC) return 0;
    if (eh->e_class != 1) return 0;      /* ELFCLASS32 */
    if (eh->e_data != 1) return 0;       /* ELFDATA2LSB */
    if (eh->e_type != ET_EXEC && eh->e_type != ET_DYN) return 0;
    if (eh->e_machine != EM_386) return 0;
    return 1;
}

/* ELF 加载器 (binfmt_elf.c) */
extern int binfmt_elf_load(const char *path, const uint8_t *data, uint32_t size);

/* PE 加载器 (V3.87 阶段B) */
extern int binfmt_pe_load(const char *path, const uint8_t *data, uint32_t size);

/* Mach-O 加载器 (远期预留) */

/* PE32 probe: MZ 魔数 + e_lfanew 处 PE\0\0 签名 */
static int probe_pe32(const uint8_t *buf, uint32_t len)
{
    if (len < 0x40 + 4) return 0;
    if (buf[0] != 'M' || buf[1] != 'Z') return 0;
    uint32_t lfanew = (uint32_t)buf[0x3C] | ((uint32_t)buf[0x3D] << 8) |
                      ((uint32_t)buf[0x3E] << 16) | ((uint32_t)buf[0x3F] << 24);
    if (lfanew == 0 || lfanew > len - 4) return 0;
    if (buf[lfanew] != 'P' || buf[lfanew+1] != 'E' ||
        buf[lfanew+2] != 0 || buf[lfanew+3] != 0) return 0;
    return 1;
}

void binfmt_init(void)
{
    fmt_count = 0;
    /* ELF32 (阶段A) */
    binfmt_t elf = {
        .id = BINFMT_ID_ELF32,
        .name = "Linux ELF32",
        .magic_len = 4,
        .magic = magic_elf32,
        .probe = probe_elf32,
        .load = binfmt_elf_load,
    };
    binfmt_register(&elf);
    /* PE32 (阶段B) */
    binfmt_t pe = {
        .id = BINFMT_ID_PE32,
        .name = "Windows PE32",
        .magic_len = 2,
        .magic = NULL,
        .probe = probe_pe32,
        .load = binfmt_pe_load,
    };
    binfmt_register(&pe);
    /* Mach-O (阶段C) */
    extern int binfmt_macho_load(const char *, const uint8_t *, uint32_t);
    extern int macho_probe(const uint8_t *, uint32_t);
    binfmt_t mo = {
        .id = BINFMT_ID_MACHO,
        .name = "Apple Mach-O",
        .magic_len = 4,
        .magic = NULL,
        .probe = macho_probe,
        .load = binfmt_macho_load,
    };
    binfmt_register(&mo);
    vga_puts("[BINFMT] formats: ELF32, PE32, Mach-O registered\n");
}

uint32_t binfmt_probe(const uint8_t *buf, uint32_t len)
{
    for (uint32_t i = 0; i < fmt_count; i++) {
        if (formats[i].probe(buf, len)) return formats[i].id;
    }
    return BINFMT_ID_UNKNOWN;
}

int binfmt_exec(const char *path, const uint8_t *data, uint32_t size)
{
    if (!data || size == 0) return BINFMT_ERR_IO;
    uint32_t id = binfmt_probe(data, size);
    for (uint32_t i = 0; i < fmt_count; i++) {
        if (formats[i].id == id) {
            vga_printf("[BINFMT] exec %s as %s\n", path, formats[i].name);
            return formats[i].load(path, data, size);
        }
    }
    return BINFMT_ERR_FORMAT;
}

/* 自检: 用内嵌的最小 ELF32 头验证识别链 */
void binfmt_selftest(void)
{
    /* 构造一个最小的 ET_EXEC/EM_386 ELF32 头 (仅识别测试) */
    uint8_t sample[52];
    for (int i = 0; i < 52; i++) sample[i] = 0;
    sample[0] = 0x7F; sample[1] = 'E'; sample[2] = 'L'; sample[3] = 'F';
    sample[4] = 1;                    /* ELFCLASS32 */
    sample[5] = 1;                    /* ELFDATA2LSB */
    sample[16] = 2; sample[17] = 0;   /* ET_EXEC */
    sample[18] = 3; sample[19] = 0;   /* EM_386 */
    uint32_t id = binfmt_probe(sample, sizeof(sample));
    if (id == BINFMT_ID_ELF32) {
        vga_puts("[BINFMT-SELFTEST] ELF32 probe PASS\n");
    } else {
        vga_puts("[BINFMT-SELFTEST] ELF32 probe FAIL\n");
    }
}
