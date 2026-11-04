/* ============================================================================
 * 赫普OS - 动态链接器增强（V8.81新增，P5-2）
 * PLT/GOT延迟绑定、符号哈希查找、运行时API、初始化数组
 * ========================================================================== */

#include "ldso.h"

/* 外部状态 */
extern ldso_state_t state;

/* 简单字符串函数 */
static int lazy_strlen(const char *s) {
    int len = 0; while (s[len]) len++; return len;
}
static int lazy_strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *s1 - *s2;
}
static void *lazy_memcpy(void *dst, const void *src, uint32_t n) {
    uint8_t *d = dst; const uint8_t *s = src;
    for (uint32_t i = 0; i < n; i++) d[i] = s[i];
    return dst;
}

/* ============================================================================
 * ELF哈希表（DT_HASH）
 * ========================================================================== */

typedef struct {
    uint32_t nbucket;
    uint32_t nchain;
    uint32_t *bucket;
    uint32_t *chain;
} elf_hash_t;

/* ELF标准哈希函数 */
static uint32_t elf_hash(const char *name)
{
    uint32_t h = 0, g;
    while (*name) {
        h = (h << 4) + (uint8_t)*name++;
        g = h & 0xf0000000;
        if (g) h ^= g >> 24;
        h &= ~g;
    }
    return h;
}

/* 初始化哈希表 */
static int hash_init(elf_hash_t *hash, void *hash_addr)
{
    if (!hash_addr) return -1;
    uint32_t *data = (uint32_t *)hash_addr;
    hash->nbucket = data[0];
    hash->nchain = data[1];
    hash->bucket = &data[2];
    hash->chain = &data[2 + hash->nbucket];
    return 0;
}

/* 使用哈希表快速查找符号 */
static void *find_symbol_hash(shared_lib_t *lib, const char *name,
                              elf_hash_t *hash)
{
    if (!lib || !lib->symtab || !lib->strtab || !hash) return NULL;

    uint32_t h = elf_hash(name) % hash->nbucket;
    uint32_t idx = hash->bucket[h];

    while (idx != STN_UNDEF && idx < hash->nchain) {
        Elf32_Sym *sym = &lib->symtab[idx];
        const char *sym_name = lib->strtab + sym->st_name;

        if (lazy_strcmp(sym_name, name) == 0) {
            int bind = ELF32_ST_BIND(sym->st_info);
            if (bind == STB_GLOBAL || bind == STB_WEAK) {
                if (sym->st_value != 0 ||
                    ELF32_ST_TYPE(sym->st_info) == STT_NOTYPE) {
                    return (void *)((uint8_t *)lib->base + sym->st_value);
                }
            }
        }
        idx = hash->chain[idx];
    }
    return NULL;
}

/* ============================================================================
 * PLT/GOT延迟绑定
 * ========================================================================== */

/* PLT桩结构（典型布局）：
 * PLT[0]: pushl GOT[1]; jmp *GOT[2]  （公共桩，调用解析器）
 * PLT[n]: jmp *GOT[n+3]; pushl $reloc_index; jmp PLT[0]
 *
 * 首次调用时：GOT[n+3]指向PLT[n]+6（push指令）
 * 解析后：GOT[n+3]被替换为目标函数地址
 */

#define PLT_ENTRY_SIZE  16
#define GOT_PLT_OFFSET  3  /* GOT[0..2]保留，函数从GOT[3]开始 */

/* 延迟绑定解析函数
 * 由PLT[0]调用，参数：
 *   - 重定位索引（通过pushl压栈）
 *   - 返回地址（调用者地址）
 * 返回：目标函数地址（通过eax）
 */
void *ldso_lazy_resolve(uint32_t reloc_index, void *return_addr)
{
    /* 找到包含此PLT的库 */
    shared_lib_t *caller_lib = NULL;

    /* 简化：在所有库中查找包含return_addr的库 */
    for (int i = 0; i < state.lib_count; i++) {
        shared_lib_t *lib = &state.libs[i];
        /* 假设库大小为64MB（简化） */
        uint8_t *base = (uint8_t *)lib->base;
        if ((uint8_t *)return_addr >= base &&
            (uint8_t *)return_addr < base + 0x04000000) {
            caller_lib = lib;
            break;
        }
    }

    if (!caller_lib || !caller_lib->jmp_rel) {
        return NULL;
    }

    /* 获取重定位条目 */
    if (reloc_index >= caller_lib->jmp_rel_count) {
        return NULL;
    }

    Elf32_Rel *rel = &caller_lib->jmp_rel[reloc_index];
    uint32_t sym_idx = ELF32_R_SYM(rel->r_info);

    if (sym_idx == 0 || !caller_lib->symtab || !caller_lib->strtab) {
        return NULL;
    }

    /* 查找符号 */
    Elf32_Sym *sym = &caller_lib->symtab[sym_idx];
    const char *sym_name = caller_lib->strtab + sym->st_name;
    void *target = ldso_resolve_symbol(sym_name);

    if (!target) {
        return NULL;
    }

    /* 更新GOT条目 */
    uint32_t *got_slot = (uint32_t *)((uint8_t *)caller_lib->base +
                                       rel->r_offset);
    *got_slot = (uint32_t)target;

    return target;
}

/* 立即绑定（BIND_NOW）- 解析所有PLT条目 */
int ldso_bind_now(shared_lib_t *lib)
{
    if (!lib || !lib->jmp_rel || !lib->loaded) return -1;

    int resolved = 0;
    for (uint32_t i = 0; i < lib->jmp_rel_count; i++) {
        Elf32_Rel *rel = &lib->jmp_rel[i];
        uint32_t type = ELF32_R_TYPE(rel->r_info);
        uint32_t sym_idx = ELF32_R_SYM(rel->r_info);

        if (type != R_386_JMP_SLOT || sym_idx == 0) continue;
        if (!lib->symtab || !lib->strtab) continue;

        Elf32_Sym *sym = &lib->symtab[sym_idx];
        const char *sym_name = lib->strtab + sym->st_name;
        void *target = ldso_resolve_symbol(sym_name);

        if (target) {
            uint32_t *slot = (uint32_t *)((uint8_t *)lib->base +
                                           rel->r_offset);
            *slot = (uint32_t)target;
            resolved++;
        }
    }
    return resolved;
}

/* ============================================================================
 * 初始化/终止函数数组
 * ========================================================================== */

/* 调用INIT_ARRAY中的函数 */
void ldso_call_init_array(shared_lib_t *lib)
{
    if (!lib || !lib->loaded || !lib->dynamic) return;

    Elf32_Dyn *dyn = lib->dynamic;
    void (*init_array)(void) = NULL;
    uint32_t init_array_size = 0;

    while (dyn->d_tag != DT_NULL) {
        if (dyn->d_tag == DT_INIT_ARRAY) {
            init_array = (void (*)(void))((uint8_t *)lib->base +
                                          dyn->d_un.d_ptr);
        } else if (dyn->d_tag == DT_INIT_ARRAYSZ) {
            init_array_size = dyn->d_un.d_val;
        }
        dyn++;
    }

    if (init_array && init_array_size > 0) {
        uint32_t count = init_array_size / sizeof(void *);
        void (**funcs)(void) = (void (**)(void))init_array;
        for (uint32_t i = 0; i < count; i++) {
            if (funcs[i]) funcs[i]();
        }
    }

    /* 同时调用旧的DT_INIT */
    if (lib->entry) {
        ((void (*)(void))lib->entry)();
    }
}

/* 调用FINI_ARRAY中的函数 */
void ldso_call_fini_array(shared_lib_t *lib)
{
    if (!lib || !lib->loaded || !lib->dynamic) return;

    Elf32_Dyn *dyn = lib->dynamic;
    void (*fini_array)(void) = NULL;
    uint32_t fini_array_size = 0;

    while (dyn->d_tag != DT_NULL) {
        if (dyn->d_tag == DT_FINI_ARRAY) {
            fini_array = (void (*)(void))((uint8_t *)lib->base +
                                          dyn->d_un.d_ptr);
        } else if (dyn->d_tag == DT_FINI_ARRAYSZ) {
            fini_array_size = dyn->d_un.d_val;
        }
        dyn++;
    }

    if (fini_array && fini_array_size > 0) {
        uint32_t count = fini_array_size / sizeof(void *);
        void (**funcs)(void) = (void (**)(void))fini_array;
        /* 逆序调用 */
        for (int i = count - 1; i >= 0; i--) {
            if (funcs[i]) funcs[i]();
        }
    }
}

/* ============================================================================
 * 运行时API（dlopen/dlsym/dlclose风格）
 * ========================================================================== */

/* 运行时加载共享库 */
void *ldso_dlopen(const char *filename, int flags)
{
    if (!filename || !filename[0]) return NULL;

    /* 检查是否已加载 */
    for (int i = 0; i < state.lib_count; i++) {
        if (lazy_strcmp(state.libs[i].name, filename) == 0) {
            return &state.libs[i];
        }
    }

    /* 加载库 */
    int ret = ldso_load_library(filename);
    if (ret < 0) return NULL;

    shared_lib_t *lib = &state.libs[state.lib_count - 1];

    /* 处理动态段 */
    ldso_process_dynamic(lib);

    /* 重定位 */
    ldso_relocate(lib);

    /* 如果是立即绑定 */
    if (flags & 0x02) {  /* RTLD_NOW */
        ldso_bind_now(lib);
    }

    /* 调用初始化函数 */
    ldso_call_init_array(lib);

    return lib;
}

/* 查找符号 */
void *ldso_dlsym(void *handle, const char *symbol)
{
    if (!symbol) return NULL;

    if (handle == NULL) {
        /* 全局搜索 */
        return ldso_resolve_symbol(symbol);
    }

    shared_lib_t *lib = (shared_lib_t *)handle;
    if (!lib || !lib->loaded) return NULL;

    /* 先在指定库中查找 */
    elf_hash_t hash;
    if (lib->dynamic) {
        Elf32_Dyn *dyn = lib->dynamic;
        while (dyn->d_tag != DT_NULL) {
            if (dyn->d_tag == DT_HASH) {
                hash_init(&hash, (void *)((uint8_t *)lib->base +
                                          dyn->d_un.d_ptr));
                void *addr = find_symbol_hash(lib, symbol, &hash);
                if (addr) return addr;
            }
            dyn++;
        }
    }

    /* 回退到线性搜索 */
    /* （使用外部的find_symbol_in_lib） */
    return NULL;
}

/* 关闭共享库 */
int ldso_dlclose(void *handle)
{
    if (!handle) return -1;

    shared_lib_t *lib = (shared_lib_t *)handle;

    /* 调用终止函数 */
    ldso_call_fini_array(lib);

    /* 简化：不实际卸载（引用计数为0时才卸载） */
    lib->loaded = 0;

    return 0;
}

/* 获取错误信息 */
const char *ldso_dlerror(void)
{
    return ldso_get_error();
}

/* ============================================================================
 * 库信息查询
 * ========================================================================== */

/* 获取已加载库数量 */
int ldso_get_loaded_count(void)
{
    int count = 0;
    for (int i = 0; i < state.lib_count; i++) {
        if (state.libs[i].loaded) count++;
    }
    return count;
}

/* 获取库信息 */
int ldso_get_lib_info(int index, char *name, int name_len,
                      void **base, int *sym_count)
{
    if (index < 0 || index >= state.lib_count) return -1;
    if (!state.libs[index].loaded) return -1;

    shared_lib_t *lib = &state.libs[index];

    if (name && name_len > 0) {
        lazy_memcpy(name, lib->name, name_len - 1);
        name[name_len - 1] = 0;
    }
    if (base) *base = lib->base;
    if (sym_count) {
        /* 估算符号数量 */
        *sym_count = 0;
        if (lib->symtab && lib->strtab) {
            Elf32_Sym *sym = lib->symtab;
            while (sym->st_name || sym->st_value || sym->st_size) {
                (*sym_count)++;
                sym++;
                if (*sym_count > 4096) break;
            }
        }
    }
    return 0;
}

/* ============================================================================
 * 增强初始化
 * ========================================================================== */

int ldso_enhanced_init(void *main_program)
{
    /* 基础初始化 */
    int ret = ldso_init(main_program);
    if (ret < 0) return ret;

    /* 处理主程序的动态段 */
    /* （主程序作为第0个库） */

    /* 加载所有依赖库 */
    for (int i = 0; i < state.lib_count; i++) {
        if (state.libs[i].loaded) {
            ldso_process_dynamic(&state.libs[i]);
        }
    }

    /* 重定位所有库 */
    for (int i = 0; i < state.lib_count; i++) {
        if (state.libs[i].loaded) {
            ldso_relocate(&state.libs[i]);
        }
    }

    /* 调用初始化函数（按依赖顺序） */
    for (int i = 0; i < state.lib_count; i++) {
        if (state.libs[i].loaded) {
            ldso_call_init_array(&state.libs[i]);
        }
    }

    return 0;
}
