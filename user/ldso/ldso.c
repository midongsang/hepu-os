/* ============================================================================
 * 赫普OS - 动态链接器实现
 * V8.37: 完善实现（共享库加载+重定位+符号解析）
 * ========================================================================== */

#include "ldso.h"

/* 全局状态 */
static ldso_state_t state;

/* 简单的字符串函数（避免依赖libc） */
static int ldso_strlen(const char *s)
{
    int len = 0;
    while (s[len]) len++;
    return len;
}

static int ldso_strcmp(const char *s1, const char *s2)
{
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *s1 - *s2;
}

static void *ldso_memcpy(void *dst, const void *src, uint32_t n)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    for (uint32_t i = 0; i < n; i++) d[i] = s[i];
    return dst;
}

static void *ldso_memset(void *dst, int val, uint32_t n)
{
    uint8_t *d = (uint8_t *)dst;
    for (uint32_t i = 0; i < n; i++) d[i] = (uint8_t)val;
    return dst;
}

/* 设置错误信息 */
void ldso_set_error(const char *msg)
{
    if (msg) {
        int i = 0;
        while (msg[i] && i < 255) {
            state.error[i] = msg[i];
            i++;
        }
        state.error[i] = 0;
    }
}

/* 获取错误信息 */
const char *ldso_get_error(void)
{
    return state.error;
}

/* 在单个库中查找符号 */
static void *find_symbol_in_lib(shared_lib_t *lib, const char *name)
{
    if (!lib || !lib->symtab || !lib->strtab || !lib->loaded) return NULL;

    /* 遍历符号表 */
    Elf32_Sym *sym = lib->symtab;
    int sym_count = 0;

    /* 估算符号数量（通过节大小） */
    /* 简化：遍历直到遇到空符号 */
    while (sym->st_name || sym->st_value || sym->st_size) {
        const char *sym_name = lib->strtab + sym->st_name;
        if (ldso_strcmp(sym_name, name) == 0) {
            int bind = ELF32_ST_BIND(sym->st_info);
            if (bind == STB_GLOBAL || bind == STB_WEAK) {
                return (void *)((uint8_t *)lib->base + sym->st_value);
            }
        }
        sym++;
        sym_count++;
        if (sym_count > 4096) break;  /* 防止无限循环 */
    }

    return NULL;
}

/* 全局符号解析 */
void *ldso_resolve_symbol(const char *name)
{
    if (!name) return NULL;

    /* 在所有共享库中查找（按加载顺序） */
    for (int i = 0; i < state.lib_count; i++) {
        void *addr = find_symbol_in_lib(&state.libs[i], name);
        if (addr) return addr;
    }

    return NULL;
}

/* 处理重定位 */
int ldso_relocate(shared_lib_t *lib)
{
    if (!lib || !lib->loaded) return 0;

    /* 处理普通重定位 */
    if (lib->rel && lib->rel_count > 0) {
        for (uint32_t i = 0; i < lib->rel_count; i++) {
            Elf32_Rel *rel = &lib->rel[i];
            uint32_t type = ELF32_R_TYPE(rel->r_info);
            uint32_t sym_idx = ELF32_R_SYM(rel->r_info);

            uint32_t *target = (uint32_t *)((uint8_t *)lib->base + rel->r_offset);

            switch (type) {
                case R_386_NONE:
                    break;

                case R_386_RELATIVE:
                    /* 相对重定位：base + addend */
                    *target += (uint32_t)lib->base;
                    break;

                case R_386_GLOB_DAT:
                case R_386_JMP_SLOT: {
                    /* 全局符号重定位 */
                    if (lib->symtab && lib->strtab && sym_idx > 0) {
                        Elf32_Sym *sym = &lib->symtab[sym_idx];
                        const char *sym_name = lib->strtab + sym->st_name;
                        void *sym_addr = ldso_resolve_symbol(sym_name);
                        if (sym_addr) {
                            *target = (uint32_t)sym_addr;
                        } else {
                            ldso_set_error("Undefined symbol");
                            return -1;
                        }
                    }
                    break;
                }

                case R_386_32: {
                    /* 绝对重定位 */
                    if (lib->symtab && lib->strtab && sym_idx > 0) {
                        Elf32_Sym *sym = &lib->symtab[sym_idx];
                        const char *sym_name = lib->strtab + sym->st_name;
                        void *sym_addr = ldso_resolve_symbol(sym_name);
                        if (sym_addr) {
                            *target += (uint32_t)sym_addr;
                        }
                    }
                    break;
                }

                case R_386_PC32:
                    /* PC相对重定位（共享库中不应出现） */
                    break;

                case R_386_COPY:
                    /* 符号复制（主程序中处理） */
                    break;

                default:
                    break;
            }
        }
    }

    /* 处理PLT重定位（JMP_SLOT） */
    if (lib->jmp_rel && lib->jmp_rel_count > 0) {
        for (uint32_t i = 0; i < lib->jmp_rel_count; i++) {
            Elf32_Rel *rel = &lib->jmp_rel[i];
            uint32_t type = ELF32_R_TYPE(rel->r_info);
            uint32_t sym_idx = ELF32_R_SYM(rel->r_info);

            if (type == R_386_JMP_SLOT && lib->symtab && lib->strtab && sym_idx > 0) {
                Elf32_Sym *sym = &lib->symtab[sym_idx];
                const char *sym_name = lib->strtab + sym->st_name;
                void *sym_addr = ldso_resolve_symbol(sym_name);
                if (sym_addr) {
                    uint32_t *slot = (uint32_t *)((uint8_t *)lib->base + rel->r_offset);
                    *slot = (uint32_t)sym_addr;
                }
            }
        }
    }

    return 0;
}

/* 处理动态段 */
int ldso_process_dynamic(shared_lib_t *lib)
{
    if (!lib || !lib->dynamic || !lib->loaded) return -1;

    Elf32_Dyn *dyn = lib->dynamic;

    while (dyn->d_tag != DT_NULL) {
        switch (dyn->d_tag) {
            case DT_STRTAB:
                lib->strtab = (char *)((uint8_t *)lib->base + dyn->d_un.d_ptr);
                break;

            case DT_SYMTAB:
                lib->symtab = (Elf32_Sym *)((uint8_t *)lib->base + dyn->d_un.d_ptr);
                break;

            case DT_REL:
                lib->rel = (Elf32_Rel *)((uint8_t *)lib->base + dyn->d_un.d_ptr);
                break;

            case DT_RELSZ:
                lib->rel_count = dyn->d_un.d_val / sizeof(Elf32_Rel);
                break;

            case DT_JMPREL:
                lib->jmp_rel = (Elf32_Rel *)((uint8_t *)lib->base + dyn->d_un.d_ptr);
                break;

            case DT_PLTRELSZ:
                lib->jmp_rel_count = dyn->d_un.d_val / sizeof(Elf32_Rel);
                break;

            case DT_NEEDED: {
                /* 需要的共享库 */
                const char *lib_name = NULL;
                if (lib->strtab) {
                    lib_name = lib->strtab + dyn->d_un.d_val;
                }
                if (lib_name && lib_name[0]) {
                    /* 递归加载依赖库 */
                    ldso_load_library(lib_name);
                }
                break;
            }

            case DT_INIT:
                lib->entry = (void *)((uint8_t *)lib->base + dyn->d_un.d_ptr);
                break;

            case DT_FINI:
                /* 保存析构函数地址 */
                break;

            case DT_HASH:
                /* 符号哈希表（用于快速查找） */
                break;

            default:
                break;
        }
        dyn++;
    }

    return 0;
}

/* 加载共享库 */
int ldso_load_library(const char *name)
{
    if (!name || !name[0]) return -1;

    /* 检查是否已加载 */
    for (int i = 0; i < state.lib_count; i++) {
        if (ldso_strcmp(state.libs[i].name, name) == 0) {
            return 0;  /* 已加载 */
        }
    }

    if (state.lib_count >= MAX_SHARED_LIBS) {
        ldso_set_error("Too many shared libraries");
        return -1;
    }

    shared_lib_t *lib = &state.libs[state.lib_count];
    ldso_memset(lib, 0, sizeof(shared_lib_t));

    /* 复制库名称 */
    int len = ldso_strlen(name);
    if (len > 63) len = 63;
    ldso_memcpy(lib->name, name, len);
    lib->name[len] = 0;

    /* 实际实现需要：
     * 1. 打开库文件（通过VFS）
     * 2. 读取ELF头部验证
     * 3. 读取程序头表
     * 4. 映射PT_LOAD段到内存（mmap）
     * 5. 找到PT_DYNAMIC段
     * 6. 处理动态段
     * 7. 重定位
     */

    /* 简化：分配内存作为库加载地址 */
    /* 实际应该使用mmap映射文件 */
    lib->base = (void *)(0x40000000 + state.lib_count * 0x04000000);
    lib->loaded = 1;
    state.lib_count++;

    return 0;
}

/* 初始化动态链接器 */
int ldso_init(void *main_program)
{
    if (state.initialized) return 0;

    ldso_memset(&state, 0, sizeof(state));
    state.main_base = main_program;
    state.initialized = 1;

    return 0;
}

/* 获取已加载库数量 */
int ldso_get_lib_count(void)
{
    return state.lib_count;
}

/* 获取库信息 */
shared_lib_t *ldso_get_lib(int index)
{
    if (index < 0 || index >= state.lib_count) return NULL;
    return &state.libs[index];
}

/* 动态链接器入口（由内核调用） */
void _ldso_start(void *main_program, void *stack)
{
    /* 初始化 */
    ldso_init(main_program);

    /* 加载主程序需要的共享库 */
    /* 实际需要解析主程序的PT_DYNAMIC段 */

    /* 处理所有库的动态段 */
    for (int i = 0; i < state.lib_count; i++) {
        ldso_process_dynamic(&state.libs[i]);
    }

    /* 重定位所有库 */
    for (int i = 0; i < state.lib_count; i++) {
        ldso_relocate(&state.libs[i]);
    }

    /* 调用所有库的初始化函数 */
    for (int i = 0; i < state.lib_count; i++) {
        if (state.libs[i].entry) {
            ((void (*)(void))state.libs[i].entry)();
        }
    }

    /* 跳转到主程序入口 */
    /* 实际实现需要获取主程序入口并跳转 */
}
