/* ============================================================================
 * 赫普OS - ELF动态链接程序加载器头文件
 * V8.89: 初始实现
 * ========================================================================== */

#ifndef _BINFMT_ELF_DYNAMIC_H
#define _BINFMT_ELF_DYNAMIC_H

#include <stdint.h>

/* 共享库信息 */
typedef struct {
    char     name[128];
    char     path[256];
    uint32_t base_addr;
    uint32_t entry;
    uint32_t dynamic_addr;
    uint32_t dynamic_size;
    int      loaded;
    int      ref_count;
} shared_lib_t;

/* 动态链接上下文 */
typedef struct {
    char        interp_path[256];
    uint32_t    interp_base;
    uint32_t    interp_entry;
    uint32_t    program_base;
    uint32_t    program_entry;
    uint32_t    program_dynamic;
    shared_lib_t libs[32];
    int         lib_count;
    int         flags;
} dyn_link_ctx_t;

/* 初始化 */
int binfmt_elf_dynamic_init(void);

/* PT_INTERP解析 */
int elf_get_interp(const uint8_t *data, uint32_t size, char *path, int max_len);
int elf_is_dynamic(const uint8_t *data, uint32_t size);

/* 动态段解析 */
int elf_parse_dynamic(const uint8_t *data, uint32_t size,
                      uint32_t base_addr,
                      char needed_libs[][128], int max_libs);

/* 共享库管理 */
shared_lib_t *dyn_find_lib(const char *name);
shared_lib_t *dyn_load_library(const char *name, const char *search_path);
int dyn_unload_library(const char *name);

/* 动态链接程序加载 */
int binfmt_elf_dynamic_load(const char *path,
                            const uint8_t *data, uint32_t size);

/* 运行时API */
void *dyn_lookup_symbol(const char *name);
int dyn_relocate(uint32_t base_addr, const uint8_t *data, uint32_t size);
dyn_link_ctx_t *dyn_get_context(void);
void dyn_reset_context(void);

#endif /* _BINFMT_ELF_DYNAMIC_H */
