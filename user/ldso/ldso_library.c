/* ============================================================================
 * 赫普OS - 共享库管理器
 * V8.98: 共享库文件系统加载、依赖解析、全局符号表
 * P5-2 动态链接完整实现
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* 外部函数声明（来自内核/系统调用） */
extern int32_t sys_open(const char *path, int32_t flags, int32_t mode);
extern int32_t sys_close(int32_t fd);
extern int32_t sys_read(int32_t fd, void *buf, int32_t count);
extern int32_t sys_lseek(int32_t fd, int32_t offset, int32_t whence);
extern int32_t sys_fstat(int32_t fd, void *statbuf);
extern void *sys_malloc(uint32_t size);
extern void sys_free(uint32_t addr);

/* ELF结构定义 */
#define EI_NIDENT 16
#define ET_DYN    3
#define PT_LOAD   1
#define PT_DYNAMIC 2
#define PT_INTERP 3
#define DT_NULL   0
#define DT_NEEDED 1
#define DT_STRTAB 5
#define DT_SYMTAB 6
#define DT_STRSZ  10
#define DT_SYMENT 11
#define DT_HASH   4
#define DT_PLTGOT 3
#define DT_JMPREL 23
#define DT_PLTREL 20
#define DT_PLTRELSZ 2
#define DT_RELA   7
#define DT_RELASZ 8
#define DT_RELAENT 9
#define DT_REL    17
#define DT_RELSZ  18
#define DT_RELENT 19
#define DT_SONAME 14
#define DT_NEEDED_str 1

typedef struct {
    unsigned char e_ident[EI_NIDENT];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint32_t e_entry;
    uint32_t e_phoff;
    uint32_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} elf_header_t;

typedef struct {
    uint32_t p_type;
    uint32_t p_offset;
    uint32_t p_vaddr;
    uint32_t p_paddr;
    uint32_t p_filesz;
    uint32_t p_memsz;
    uint32_t p_flags;
    uint32_t p_align;
} elf_phdr_t;

typedef struct {
    int32_t d_tag;
    union {
        uint32_t d_val;
        uint32_t d_ptr;
    } d_un;
} elf_dyn_t;

typedef struct {
    uint32_t st_name;
    uint32_t st_value;
    uint32_t st_size;
    unsigned char st_info;
    unsigned char st_other;
    uint16_t st_shndx;
} elf_sym_t;

/* 共享库状态 */
#define MAX_LOADED_LIBS  32
#define MAX_DEPENDENCIES 16
#define MAX_SYMBOL_CACHE 256

typedef struct {
    char          name[64];       /* 库名称，如 "libc.so" */
    char          path[128];      /* 完整路径 */
    void         *base_addr;      /* 加载基地址 */
    uint32_t      size;           /* 库大小 */
    int           ref_count;      /* 引用计数 */
    int           loaded;         /* 是否已加载 */

    /* ELF信息 */
    void         *dyn_addr;       /* 动态段地址 */
    void         *symtab;         /* 符号表 */
    void         *strtab;         /* 字符串表 */
    uint32_t      strtab_size;
    uint32_t      syment;
    void         *hash;           /* 哈希表 */
    void         *plt_got;        /* PLT/GOT */
    void         *jmprel;         /* 跳转重定位 */
    uint32_t      jmprel_size;
    void         *rel;            /* 相对重定位 */
    uint32_t      rel_size;

    /* 依赖库 */
    char          deps[MAX_DEPENDENCIES][64];
    int           dep_count;

    /* 初始化/终止函数 */
    void         *init_array;
    uint32_t      init_array_count;
    void         *fini_array;
    uint32_t      fini_array_count;
} loaded_lib_t;

/* 全局符号缓存 */
typedef struct {
    char     name[64];
    void    *addr;
    int      lib_index;
    int      used;
} symbol_cache_t;

static loaded_lib_t g_libs[MAX_LOADED_LIBS];
static symbol_cache_t g_sym_cache[MAX_SYMBOL_CACHE];
static int g_lib_count = 0;
static int g_cache_count = 0;
static int g_initialized = 0;

/* 搜索路径 */
static const char *g_search_paths[] = {
    "/lib",
    "/usr/lib",
    "/lib/ldso",
    NULL
};

/* ============================================================================
 * 工具函数
 * ============================================================================ */

static int lib_strlen(const char *s)
{
    int n = 0;
    while (s && s[n]) n++;
    return n;
}

static int lib_strcmp(const char *s1, const char *s2)
{
    while (*s1 && *s2 && *s1 == *s2) { s1++; s2++; }
    return *s1 - *s2;
}

static void *lib_memcpy(void *dst, const void *src, uint32_t n)
{
    uint8_t *d = (uint8_t*)dst;
    const uint8_t *s = (const uint8_t*)src;
    for (uint32_t i = 0; i < n; i++) d[i] = s[i];
    return dst;
}

static void *lib_memset(void *dst, int c, uint32_t n)
{
    uint8_t *d = (uint8_t*)dst;
    for (uint32_t i = 0; i < n; i++) d[i] = (uint8_t)c;
    return dst;
}

static void lib_strcpy(char *dst, const char *src)
{
    while (*src) *dst++ = *src++;
    *dst = '\0';
}

static void lib_strcat(char *dst, const char *src)
{
    while (*dst) dst++;
    while (*src) *dst++ = *src++;
    *dst = '\0';
}

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

/* ============================================================================
 * 文件读取辅助
 * ============================================================================ */

static int read_file(const char *path, void **out_data, uint32_t *out_size)
{
    int32_t fd = sys_open(path, 0, 0);  /* O_RDONLY */
    if (fd < 0) return -1;

    /* 获取文件大小 */
    uint8_t statbuf[128];
    if (sys_fstat(fd, statbuf) < 0) {
        sys_close(fd);
        return -1;
    }
    /* 简化：从statbuf中读取size（假设在偏移28处） */
    uint32_t size = *(uint32_t*)(statbuf + 28);
    if (size == 0 || size > 16 * 1024 * 1024) {
        sys_close(fd);
        return -1;
    }

    void *data = sys_malloc(size);
    if (!data) {
        sys_close(fd);
        return -1;
    }

    int32_t total = 0;
    while (total < (int32_t)size) {
        int32_t n = sys_read(fd, (uint8_t*)data + total, size - total);
        if (n <= 0) break;
        total += n;
    }

    sys_close(fd);

    if (total != (int32_t)size) {
        sys_free((uint32_t)data);
        return -1;
    }

    *out_data = data;
    *out_size = size;
    return 0;
}

/* ============================================================================
 * ELF解析
 * ============================================================================ */

static int parse_elf_dynamic(loaded_lib_t *lib, void *elf_data, uint32_t elf_size)
{
    elf_header_t *eh = (elf_header_t*)elf_data;

    /* 验证ELF */
    if (eh->e_ident[0] != 0x7f || eh->e_ident[1] != 'E' ||
        eh->e_ident[2] != 'L' || eh->e_ident[3] != 'F') {
        return -1;
    }
    if (eh->e_type != ET_DYN) return -1;  /* 必须是共享库 */

    /* 查找动态段 */
    elf_phdr_t *ph = (elf_phdr_t*)((uint8_t*)elf_data + eh->e_phoff);
    for (int i = 0; i < eh->e_phnum; i++) {
        if (ph[i].p_type == PT_DYNAMIC) {
            lib->dyn_addr = (uint8_t*)lib->base_addr + ph[i].p_vaddr;
            break;
        }
    }

    if (!lib->dyn_addr) return -1;

    /* 解析动态段 */
    elf_dyn_t *dyn = (elf_dyn_t*)lib->dyn_addr;
    while (dyn->d_tag != DT_NULL) {
        switch (dyn->d_tag) {
            case DT_NEEDED:
                if (lib->dep_count < MAX_DEPENDENCIES) {
                    const char *dep_name = (const char*)lib->strtab + dyn->d_un.d_val;
                    if (dep_name && lib_strlen(dep_name) < 64) {
                        lib_strcpy(lib->deps[lib->dep_count], dep_name);
                        lib->dep_count++;
                    }
                }
                break;
            case DT_STRTAB:
                lib->strtab = (uint8_t*)lib->base_addr + dyn->d_un.d_ptr;
                break;
            case DT_STRSZ:
                lib->strtab_size = dyn->d_un.d_val;
                break;
            case DT_SYMTAB:
                lib->symtab = (uint8_t*)lib->base_addr + dyn->d_un.d_ptr;
                break;
            case DT_SYMENT:
                lib->syment = dyn->d_un.d_val;
                break;
            case DT_HASH:
                lib->hash = (uint8_t*)lib->base_addr + dyn->d_un.d_ptr;
                break;
            case DT_PLTGOT:
                lib->plt_got = (uint8_t*)lib->base_addr + dyn->d_un.d_ptr;
                break;
            case DT_JMPREL:
                lib->jmprel = (uint8_t*)lib->base_addr + dyn->d_un.d_ptr;
                break;
            case DT_PLTRELSZ:
                lib->jmprel_size = dyn->d_un.d_val;
                break;
            case DT_REL:
                lib->rel = (uint8_t*)lib->base_addr + dyn->d_un.d_ptr;
                break;
            case DT_RELSZ:
                lib->rel_size = dyn->d_un.d_val;
                break;
        }
        dyn++;
    }

    return 0;
}

/* ============================================================================
 * 库加载
 * ============================================================================ */

static loaded_lib_t *find_loaded_lib(const char *name)
{
    for (int i = 0; i < g_lib_count; i++) {
        if (g_libs[i].loaded && lib_strcmp(g_libs[i].name, name) == 0) {
            return &g_libs[i];
        }
    }
    return NULL;
}

static int find_library_path(const char *name, char *path, int path_size)
{
    /* 先检查是否是完整路径 */
    if (name[0] == '/') {
        lib_strcpy(path, name);
        return 0;
    }

    /* 搜索标准路径 */
    for (int i = 0; g_search_paths[i]; i++) {
        int len = lib_strlen(g_search_paths[i]) + 1 + lib_strlen(name) + 1;
        if (len < path_size) {
            lib_strcpy(path, g_search_paths[i]);
            lib_strcat(path, "/");
            lib_strcat(path, name);
            /* 检查文件是否存在（尝试打开） */
            int32_t fd = sys_open(path, 0, 0);
            if (fd >= 0) {
                sys_close(fd);
                return 0;
            }
        }
    }

    return -1;
}

loaded_lib_t *ldso_load_library(const char *name)
{
    if (!name || !g_initialized) return NULL;

    /* 检查是否已加载 */
    loaded_lib_t *existing = find_loaded_lib(name);
    if (existing) {
        existing->ref_count++;
        return existing;
    }

    /* 查找空闲槽位 */
    if (g_lib_count >= MAX_LOADED_LIBS) return NULL;
    loaded_lib_t *lib = &g_libs[g_lib_count];
    lib_memset(lib, 0, sizeof(loaded_lib_t));

    /* 查找库路径 */
    char path[128];
    if (find_library_path(name, path, sizeof(path)) != 0) {
        return NULL;
    }

    /* 读取库文件 */
    void *elf_data = NULL;
    uint32_t elf_size = 0;
    if (read_file(path, &elf_data, &elf_size) != 0) {
        return NULL;
    }

    /* 分配加载地址（简化：使用ELF数据本身作为加载地址） */
    lib->base_addr = elf_data;
    lib->size = elf_size;
    lib_strcpy(lib->name, name);
    lib_strcpy(lib->path, path);

    /* 解析ELF动态段 */
    if (parse_elf_dynamic(lib, elf_data, elf_size) != 0) {
        sys_free((uint32_t)elf_data);
        return NULL;
    }

    lib->loaded = 1;
    lib->ref_count = 1;
    g_lib_count++;

    /* 递归加载依赖库 */
    for (int i = 0; i < lib->dep_count; i++) {
        ldso_load_library(lib->deps[i]);
    }

    return lib;
}

int ldso_unload_library(loaded_lib_t *lib)
{
    if (!lib || !lib->loaded) return -1;

    lib->ref_count--;
    if (lib->ref_count > 0) return 0;  /* 还有引用，不卸载 */

    /* 释放内存 */
    if (lib->base_addr) {
        sys_free((uint32_t)lib->base_addr);
    }

    lib->loaded = 0;
    return 0;
}

/* ============================================================================
 * 符号解析
 * ============================================================================ */

void *ldso_resolve_symbol_global(const char *name)
{
    if (!name || !g_initialized) return NULL;

    /* 先查缓存 */
    for (int i = 0; i < g_cache_count; i++) {
        if (g_sym_cache[i].used && lib_strcmp(g_sym_cache[i].name, name) == 0) {
            return g_sym_cache[i].addr;
        }
    }

    /* 全局搜索所有已加载库 */
    for (int i = 0; i < g_lib_count; i++) {
        loaded_lib_t *lib = &g_libs[i];
        if (!lib->loaded || !lib->symtab || !lib->strtab) continue;

        /* 使用哈希表查找 */
        if (lib->hash) {
            uint32_t *hash = (uint32_t*)lib->hash;
            uint32_t nbucket = hash[0];
            uint32_t *bucket = &hash[2];
            uint32_t *chain = &hash[2 + nbucket];
            uint32_t h = elf_hash(name) % nbucket;

            for (uint32_t idx = bucket[h]; idx != 0xFFFFFFFF; idx = chain[idx]) {
                elf_sym_t *sym = (elf_sym_t*)((uint8_t*)lib->symtab + idx * lib->syment);
                const char *sym_name = (const char*)lib->strtab + sym->st_name;
                if (sym_name && lib_strcmp(sym_name, name) == 0 && sym->st_value) {
                    void *addr = (uint8_t*)lib->base_addr + sym->st_value;

                    /* 加入缓存 */
                    if (g_cache_count < MAX_SYMBOL_CACHE) {
                        lib_strcpy(g_sym_cache[g_cache_count].name, name);
                        g_sym_cache[g_cache_count].addr = addr;
                        g_sym_cache[g_cache_count].lib_index = i;
                        g_sym_cache[g_cache_count].used = 1;
                        g_cache_count++;
                    }

                    return addr;
                }
            }
        }
    }

    return NULL;  /* 未找到 */
}

void *ldso_resolve_symbol_in_lib(loaded_lib_t *lib, const char *name)
{
    if (!lib || !name || !lib->symtab || !lib->strtab) return NULL;

    /* 线性搜索（简化版） */
    elf_sym_t *sym = (elf_sym_t*)lib->symtab;
    for (int i = 0; ; i++) {
        const char *sym_name = (const char*)lib->strtab + sym[i].st_name;
        if (!sym_name || sym_name[0] == '\0') {
            if (i > 1000) break;  /* 防止无限循环 */
            continue;
        }
        if (lib_strcmp(sym_name, name) == 0 && sym[i].st_value) {
            return (uint8_t*)lib->base_addr + sym[i].st_value;
        }
        if (sym[i].st_name == 0 && i > 0) break;
    }

    return NULL;
}

/* ============================================================================
 * 初始化/查询
 * ============================================================================ */

int ldso_library_init(void)
{
    if (g_initialized) return 0;

    lib_memset(g_libs, 0, sizeof(g_libs));
    lib_memset(g_sym_cache, 0, sizeof(g_sym_cache));
    g_lib_count = 0;
    g_cache_count = 0;
    g_initialized = 1;

    return 0;
}

int ldso_get_loaded_libraries(loaded_lib_t *libs, int max)
{
    if (!libs || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < g_lib_count && count < max; i++) {
        if (g_libs[i].loaded) {
            libs[count++] = g_libs[i];
        }
    }
    return count;
}

int ldso_get_library_count(void)
{
    return g_lib_count;
}

void ldso_clear_symbol_cache(void)
{
    lib_memset(g_sym_cache, 0, sizeof(g_sym_cache));
    g_cache_count = 0;
}
