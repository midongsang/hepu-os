#ifndef KERNEL_SLIMMER_H
#define KERNEL_SLIMMER_H

#include <stdint.h>

/* ============================================================================
 * 赫普OS 内核体积优化
 * 死代码消除 / 段合并 / 配置裁剪 / 大小分析
 * ========================================================================== */

#define KS_MAX_SECTIONS 64
#define KS_MAX_SYMBOLS 256
#define KS_MAX_MODULES 128
#define KS_NAME_LEN 128

/* 段类型 */
typedef enum {
    KS_SEC_TEXT = 0,
    KS_SEC_DATA,
    KS_SEC_BSS,
    KS_SEC_RODATA,
    KS_SEC_INIT,
    KS_SEC_EXIT,
    KS_SEC_KEEP,
    KS_SEC_OTHER,
} ks_section_type_t;

/* 段信息 */
typedef struct {
    int used;
    int id;
    char name[KS_NAME_LEN];
    ks_section_type_t type;
    uint32_t address;
    uint32_t size;
    uint32_t file_offset;
    int removable;  /* 可移除 */
    int compressed;
    uint32_t compressed_size;
} ks_section_t;

/* 符号信息 */
typedef struct {
    int used;
    int id;
    char name[KS_NAME_LEN];
    uint32_t address;
    uint32_t size;
    int section_id;
    int used_count;  /* 引用次数 */
    int is_dead;     /* 死代码 */
    char module[KS_NAME_LEN];
} ks_symbol_t;

/* 模块信息 */
typedef struct {
    int used;
    int id;
    char name[KS_NAME_LEN];
    uint32_t text_size;
    uint32_t data_size;
    uint32_t bss_size;
    uint32_t total_size;
    int enabled;
    int essential;  /* 核心模块不可移除 */
    int dependency_count;
    char dependencies[32][KS_NAME_LEN];
} ks_module_t;

/* 优化配置 */
typedef struct {
    int dead_code_elimination;
    int section_merging;
    int function_sections;
    int data_sections;
    int gc_sections;
    int lto;  /* 链接时优化 */
    int oz;   /* 优化大小 */
    int strip_debug;
    int compress_kernel;
    int remove_unused_modules;
} ks_config_t;

/* 优化结果 */
typedef struct {
    uint32_t original_size;
    uint32_t optimized_size;
    uint32_t saved_size;
    int saved_percent;
    int sections_removed;
    int symbols_removed;
    int modules_removed;
    int dead_code_removed;
} ks_result_t;

/* 内核瘦身状态 */
typedef struct {
    int initialized;
    int enabled;
    ks_config_t config;
    ks_result_t last_result;
    /* 段 */
    ks_section_t sections[KS_MAX_SECTIONS];
    int section_count;
    /* 符号 */
    ks_symbol_t symbols[KS_MAX_SYMBOLS];
    int symbol_count;
    /* 模块 */
    ks_module_t modules[KS_MAX_MODULES];
    int module_count;
    /* 统计 */
    uint32_t kernel_size;
    uint32_t kernel_text_size;
    uint32_t kernel_data_size;
    uint32_t kernel_bss_size;
    uint64_t total_optimizations;
} kernel_slimmer_t;

/* 初始化 */
void kernel_slimmer_init(void);

/* 分析 */
int ks_analyze(void);
int ks_analyze_sections(void);
int ks_analyze_symbols(void);
int ks_analyze_modules(void);
int ks_find_dead_code(ks_symbol_t *dead, int max);
int ks_find_removable_sections(ks_section_t *secs, int max);

/* 优化 */
int ks_optimize(ks_result_t *result);
int ks_apply_config(const ks_config_t *config);
int ks_get_config(ks_config_t *config);
int ks_remove_module(int module_id);
int ks_enable_module(int module_id, int enable);
int ks_merge_sections(int sec1, int sec2);

/* 查询 */
int ks_get_sections(ks_section_t *secs, int max);
int ks_get_symbols(ks_symbol_t *syms, int max);
int ks_get_modules(ks_module_t *mods, int max);
int ks_get_kernel_size(uint32_t *size);
int ks_get_section_by_name(const char *name, ks_section_t *sec);
int ks_get_largest_modules(ks_module_t *mods, int max);

/* 报告 */
int ks_generate_report(char *buf, int size);
int ks_get_result(ks_result_t *result);

/* 统计 */
int ks_get_stats(kernel_slimmer_t *stats);

#endif /* KERNEL_SLIMMER_H */
