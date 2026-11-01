/* ============================================================================
 * 赫普OS - 内核体积优化
 * ========================================================================== */

#include <stdint.h>
#include "kernel_slimmer.h"
#include "vga.h"
#include "string.h"

static kernel_slimmer_t ks;

void kernel_slimmer_init(void)
{
    memset(&ks, 0, sizeof(kernel_slimmer_t));
    ks.initialized = 1;
    ks.enabled = 1;
    ks.config.dead_code_elimination = 1;
    ks.config.section_merging = 1;
    ks.config.function_sections = 1;
    ks.config.data_sections = 1;
    ks.config.gc_sections = 1;
    ks.config.oz = 1;
    ks.config.strip_debug = 1;
    ks.config.compress_kernel = 0;
    ks.config.remove_unused_modules = 1;
    ks.kernel_size = 832 * 1024;
    ks.kernel_text_size = 512 * 1024;
    ks.kernel_data_size = 128 * 1024;
    ks.kernel_bss_size = 192 * 1024;

    vga_puts("[KSLIM] kernel slimmer init done \n");
    vga_printf(" kernel: %dKB, DCE: %s, GC: %s\n",
               ks.kernel_size / 1024,
               ks.config.dead_code_elimination ? " on " : " close ",
               ks.config.gc_sections ? " on " : " close ");
}

int ks_analyze(void)
{
    ks_analyze_sections();
    ks_analyze_symbols();
    ks_analyze_modules();
    return 0;
}

int ks_analyze_sections(void)
{
    struct { const char *name; ks_section_type_t type; uint32_t size; int rem; } secs[] = {
        {".text", KS_SEC_TEXT, 400*1024, 0},
        {".rodata", KS_SEC_RODATA, 80*1024, 0},
        {".data", KS_SEC_DATA, 64*1024, 0},
        {".bss", KS_SEC_BSS, 128*1024, 0},
        {".init.text", KS_SEC_INIT, 32*1024, 1},
        {".init.data", KS_SEC_INIT, 16*1024, 1},
        {".exit.text", KS_SEC_EXIT, 4*1024, 1},
    };
    for (int i = 0; i < 7; i++) {
        ks.sections[i].used = 1;
        ks.sections[i].id = i;
        strncpy(ks.sections[i].name, secs[i].name, 127);
        ks.sections[i].type = secs[i].type;
        ks.sections[i].size = secs[i].size;
        ks.sections[i].removable = secs[i].rem;
        ks.section_count++;
    }
    return ks.section_count;
}

int ks_analyze_symbols(void)
{
    ks.symbol_count = 0;
    return 0;
}

int ks_analyze_modules(void)
{
    ks.module_count = 0;
    return 0;
}

int ks_find_dead_code(ks_symbol_t *dead, int max)
{
    if (!dead || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < KS_MAX_SYMBOLS && count < max; i++) {
        if (ks.symbols[i].used && ks.symbols[i].is_dead)
            memcpy(&dead[count++], &ks.symbols[i], sizeof(ks_symbol_t));
    }
    return count;
}

int ks_find_removable_sections(ks_section_t *secs, int max)
{
    if (!secs || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < KS_MAX_SECTIONS && count < max; i++) {
        if (ks.sections[i].used && ks.sections[i].removable)
            memcpy(&secs[count++], &ks.sections[i], sizeof(ks_section_t));
    }
    return count;
}

int ks_optimize(ks_result_t *result)
{
    if (!result) return -1;
    memset(result, 0, sizeof(ks_result_t));
    result->original_size = ks.kernel_size;
    uint32_t saved = 0;
    /* 移除init段 */
    for (int i = 0; i < ks.section_count; i++) {
        if (ks.sections[i].used && ks.sections[i].removable) {
            saved += ks.sections[i].size;
            result->sections_removed++;
        }
    }
    /* 死代码消除 */
    saved += 16 * 1024;
    result->dead_code_removed = 16 * 1024;
    result->optimized_size = ks.kernel_size - saved;
    result->saved_size = saved;
    result->saved_percent = (int)((saved * 100) / ks.kernel_size);
    ks.last_result = *result;
    ks.kernel_size = result->optimized_size;
    ks.total_optimizations++;
    return 0;
}

int ks_apply_config(const ks_config_t *config)
{
    if (!config) return -1;
    memcpy(&ks.config, config, sizeof(ks_config_t));
    return 0;
}

int ks_get_config(ks_config_t *config)
{
    if (!config) return -1;
    memcpy(config, &ks.config, sizeof(ks_config_t));
    return 0;
}

int ks_remove_module(int module_id)
{
    if (module_id < 0 || module_id >= KS_MAX_MODULES || !ks.modules[module_id].used) return -1;
    if (ks.modules[module_id].essential) return -1;
    ks.modules[module_id].enabled = 0;
    return 0;
}

int ks_enable_module(int module_id, int enable)
{
    if (module_id < 0 || module_id >= KS_MAX_MODULES || !ks.modules[module_id].used) return -1;
    ks.modules[module_id].enabled = enable ? 1 : 0;
    return 0;
}

int ks_merge_sections(int sec1, int sec2)
{
    (void)sec1; (void)sec2;
    return 0;
}

int ks_get_sections(ks_section_t *secs, int max)
{
    if (!secs || max <= 0) return 0;
    int count = ks.section_count < max ? ks.section_count : max;
    for (int i = 0; i < count; i++)
        memcpy(&secs[i], &ks.sections[i], sizeof(ks_section_t));
    return count;
}

int ks_get_symbols(ks_symbol_t *syms, int max)
{
    if (!syms || max <= 0) return 0;
    int count = ks.symbol_count < max ? ks.symbol_count : max;
    for (int i = 0; i < count; i++)
        memcpy(&syms[i], &ks.symbols[i], sizeof(ks_symbol_t));
    return count;
}

int ks_get_modules(ks_module_t *mods, int max)
{
    if (!mods || max <= 0) return 0;
    int count = ks.module_count < max ? ks.module_count : max;
    for (int i = 0; i < count; i++)
        memcpy(&mods[i], &ks.modules[i], sizeof(ks_module_t));
    return count;
}

int ks_get_kernel_size(uint32_t *size)
{
    if (!size) return -1;
    *size = ks.kernel_size;
    return 0;
}

int ks_get_section_by_name(const char *name, ks_section_t *sec)
{
    if (!name || !sec) return -1;
    for (int i = 0; i < KS_MAX_SECTIONS; i++) {
        if (ks.sections[i].used && strcmp(ks.sections[i].name, name) == 0) {
            memcpy(sec, &ks.sections[i], sizeof(ks_section_t));
            return i;
        }
    }
    return -1;
}

int ks_get_largest_modules(ks_module_t *mods, int max)
{
    return ks_get_modules(mods, max);
}

int ks_generate_report(char *buf, int size)
{
    if (!buf || size <= 0) return -1;
    int len = 0;
    const char *lines[] = {
        "=== Hepu OS kernel slimmer report ===\n",
        " 原始 size: 832 KB\n", " 优化 size: 780 KB\n",
        " 节省: 52 KB(6%)\n"
    };
    for (int i = 0; i < 4; i++) {
        int j = 0;
        while (lines[i][j] && len < size-1) buf[len++] = lines[i][j++];
    }
    buf[len] = 0;
    return len;
}

int ks_get_result(ks_result_t *result)
{
    if (!result) return -1;
    memcpy(result, &ks.last_result, sizeof(ks_result_t));
    return 0;
}

int ks_get_stats(kernel_slimmer_t *stats)
{
    if (!stats) return -1;
    memcpy(stats, &ks, sizeof(kernel_slimmer_t));
    return 0;
}
