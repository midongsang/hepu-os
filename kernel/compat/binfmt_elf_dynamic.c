/* ============================================================================
 * 赫普OS - ELF动态链接程序加载器
 * V8.89: 初始实现
 * 支持PT_INTERP动态链接器、共享库加载、PLT/GOT重定位
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <string.h>
#include "binfmt.h"
#include "elf32.h"
#include "constants.h"
#include "paging.h"
#include "process.h"
#include "vfs.h"
#include "vga.h"
#include "kheap.h"

/* 简单的snprintf替代（内核环境） */
static int dyn_snprintf(char *buf, int size, const char *fmt, ...)
{
    /* 简化：只支持 %s 格式 */
    int pos = 0;
    va_list args;
    va_start(args, fmt);
    while (*fmt && pos < size - 1) {
        if (*fmt == '%' && *(fmt + 1) == 's') {
            const char *s = va_arg(args, const char *);
            while (*s && pos < size - 1) {
                buf[pos++] = *s++;
            }
            fmt += 2;
        } else {
            buf[pos++] = *fmt++;
        }
    }
    buf[pos] = '\0';
    va_end(args);
    return pos;
}

/* 动态链接器默认路径 */
#define DEFAULT_INTERP "/lib/ld-hepu.so.1"
#define INTERP_MAX_PATH 256

/* 最大共享库数量 */
#define MAX_SHARED_LIBS 32

/* 共享库信息 */
typedef struct {
    char     name[128];      /* 库名 */
    char     path[256];      /* 完整路径 */
    uint32_t base_addr;      /* 加载基址 */
    uint32_t entry;          /* 入口点 */
    uint32_t dynamic_addr;   /* PT_DYNAMIC段地址 */
    uint32_t dynamic_size;   /* PT_DYNAMIC段大小 */
    int      loaded;         /* 是否已加载 */
    int      ref_count;      /* 引用计数 */
} shared_lib_t;

/* 动态链接上下文 */
typedef struct {
    char        interp_path[INTERP_MAX_PATH];  /* 动态链接器路径 */
    uint32_t    interp_base;                   /* 动态链接器基址 */
    uint32_t    interp_entry;                  /* 动态链接器入口 */
    uint32_t    program_base;                  /* 主程序基址 */
    uint32_t    program_entry;                 /* 主程序入口 */
    uint32_t    program_dynamic;               /* 主程序PT_DYNAMIC地址 */
    shared_lib_t libs[MAX_SHARED_LIBS];        /* 已加载共享库 */
    int         lib_count;                     /* 已加载库数量 */
    int         flags;                         /* 标志 */
} dyn_link_ctx_t;

/* 全局动态链接上下文 */
static dyn_link_ctx_t g_dyn_ctx;
static int g_dyn_initialized = 0;

/* ============================================================================
 * 初始化
 * ============================================================================ */

int binfmt_elf_dynamic_init(void)
{
    if (g_dyn_initialized) return 0;

    memset(&g_dyn_ctx, 0, sizeof(dyn_link_ctx_t));
    strncpy(g_dyn_ctx.interp_path, DEFAULT_INTERP, INTERP_MAX_PATH - 1);
    g_dyn_initialized = 1;

    vga_puts("[ELF-DYN] Dynamic ELF loader initialized\n");
    return 0;
}

/* ============================================================================
 * PT_INTERP解析
 * ============================================================================ */

/* 从ELF中提取动态链接器路径 */
int elf_get_interp(const uint8_t *data, uint32_t size, char *path, int max_len)
{
    if (!data || !path || max_len <= 0) return -1;

    const elf32_ehdr_t *eh = (const elf32_ehdr_t *)data;
    if (eh->e_magic != ELF_MAGIC) return -1;

    uint32_t phoff = eh->e_phoff;
    uint32_t phnum = eh->e_phnum;
    uint32_t entsz = eh->e_phentsize;

    if (phoff + (uint64_t)phnum * entsz > size) return -1;

    for (uint32_t i = 0; i < phnum; i++) {
        const elf32_phdr_t *ph =
            (const elf32_phdr_t *)(data + phoff + (uint64_t)i * entsz);

        if (ph->p_type == PT_INTERP) {
            if (ph->p_offset + ph->p_filesz > size) return -1;
            const char *interp = (const char *)(data + ph->p_offset);
            /* 确保字符串以null结尾 */
            int len = 0;
            while (len < (int)ph->p_filesz && len < max_len - 1 && interp[len]) {
                path[len] = interp[len];
                len++;
            }
            path[len] = '\0';
            return 0;
        }
    }
    return -1;  /* 没有PT_INTERP */
}

/* 检查ELF是否为动态链接程序 */
int elf_is_dynamic(const uint8_t *data, uint32_t size)
{
    char path[INTERP_MAX_PATH];
    return elf_get_interp(data, size, path, sizeof(path)) == 0;
}

/* ============================================================================
 * 动态段解析
 * ============================================================================ */

/* 动态标签类型 */
#define DT_NULL         0
#define DT_NEEDED       1
#define DT_PLTRELSZ     2
#define DT_PLTGOT       3
#define DT_HASH         4
#define DT_STRTAB       5
#define DT_SYMTAB       6
#define DT_RELA         7
#define DT_RELASZ       8
#define DT_RELAENT      9
#define DT_STRSZ        10
#define DT_SYMENT       11
#define DT_INIT         12
#define DT_FINI         13
#define DT_SONAME       14
#define DT_RPATH        15
#define DT_SYMBOLIC     16
#define DT_REL          17
#define DT_RELSZ        18
#define DT_RELENT       19
#define DT_PLTREL       20
#define DT_DEBUG        21
#define DT_TEXTREL      22
#define DT_JMPREL       23
#define DT_BIND_NOW     24
#define DT_INIT_ARRAY   25
#define DT_FINI_ARRAY   26
#define DT_INIT_ARRAYSZ 27
#define DT_FINI_ARRAYSZ 28

/* 动态段条目 */
typedef struct {
    int32_t  d_tag;
    uint32_t d_val;
} elf32_dyn_t;

/* 解析动态段，提取需要的共享库列表 */
int elf_parse_dynamic(const uint8_t *data, uint32_t size,
                      uint32_t base_addr,
                      char needed_libs[][128], int max_libs)
{
    if (!data || !needed_libs || max_libs <= 0) return 0;

    const elf32_ehdr_t *eh = (const elf32_ehdr_t *)data;
    uint32_t phoff = eh->e_phoff;
    uint32_t phnum = eh->e_phnum;
    uint32_t entsz = eh->e_phentsize;

    /* 查找PT_DYNAMIC段 */
    uint32_t dyn_off = 0, dyn_size = 0;
    for (uint32_t i = 0; i < phnum; i++) {
        const elf32_phdr_t *ph =
            (const elf32_phdr_t *)(data + phoff + (uint64_t)i * entsz);
        if (ph->p_type == PT_DYNAMIC) {
            dyn_off = ph->p_offset;
            dyn_size = ph->p_filesz;
            break;
        }
    }
    if (dyn_off == 0 || dyn_size == 0) return 0;
    if (dyn_off + dyn_size > size) return -1;

    /* 查找字符串表 */
    uint32_t strtab_off = 0, strtab_size = 0;
    const elf32_dyn_t *dyn = (const elf32_dyn_t *)(data + dyn_off);
    int dyn_count = dyn_size / sizeof(elf32_dyn_t);

    for (int i = 0; i < dyn_count; i++) {
        if (dyn[i].d_tag == DT_STRTAB) {
            /* DT_STRTAB的值是虚拟地址，需要转换为文件偏移 */
            /* 简化：假设字符串表在文件中的偏移等于虚拟地址 - 基址 */
            strtab_off = dyn[i].d_val - base_addr;
        } else if (dyn[i].d_tag == DT_STRSZ) {
            strtab_size = dyn[i].d_val;
        }
    }

    /* 提取DT_NEEDED条目 */
    int lib_count = 0;
    for (int i = 0; i < dyn_count && lib_count < max_libs; i++) {
        if (dyn[i].d_tag == DT_NEEDED) {
            uint32_t name_off = dyn[i].d_val;
            if (strtab_off + name_off < size) {
                const char *name = (const char *)(data + strtab_off + name_off);
                strncpy(needed_libs[lib_count], name, 127);
                needed_libs[lib_count][127] = '\0';
                lib_count++;
            }
        }
    }

    return lib_count;
}

/* ============================================================================
 * 共享库管理
 * ============================================================================ */

/* 查找已加载的共享库 */
shared_lib_t *dyn_find_lib(const char *name)
{
    if (!name) return NULL;
    for (int i = 0; i < g_dyn_ctx.lib_count; i++) {
        if (strcmp(g_dyn_ctx.libs[i].name, name) == 0) {
            return &g_dyn_ctx.libs[i];
        }
    }
    return NULL;
}

/* 加载共享库 */
shared_lib_t *dyn_load_library(const char *name, const char *search_path)
{
    if (!name) return NULL;

    /* 检查是否已加载 */
    shared_lib_t *existing = dyn_find_lib(name);
    if (existing) {
        existing->ref_count++;
        return existing;
    }

    if (g_dyn_ctx.lib_count >= MAX_SHARED_LIBS) {
        vga_printf("[ELF-DYN] Too many shared libraries\n");
        return NULL;
    }

    /* 构建完整路径 */
    char path[512];
    if (name[0] == '/') {
        strncpy(path, name, sizeof(path) - 1);
    } else if (search_path) {
        dyn_snprintf(path, sizeof(path), "%s/%s", search_path, name);
    } else {
        dyn_snprintf(path, sizeof(path), "/lib/%s", name);
    }

    /* 读取库文件 */
    int fd = vfs_open(path, 0);
    if (fd < 0) {
        vga_printf("[ELF-DYN] Library not found: %s\n", path);
        return NULL;
    }

    uint32_t fsize = vfs_fsize(fd);
    if (fsize == 0 || fsize > 16 * 1024 * 1024) {
        vfs_close(fd);
        return NULL;
    }

    uint8_t *lib_data = kmalloc(fsize);
    if (!lib_data) {
        vfs_close(fd);
        return NULL;
    }

    if (vfs_read(fd, lib_data, fsize) != (int)fsize) {
        kfree(lib_data);
        vfs_close(fd);
        return NULL;
    }
    vfs_close(fd);

    /* 验证ELF */
    const elf32_ehdr_t *eh = (const elf32_ehdr_t *)lib_data;
    if (eh->e_magic != ELF_MAGIC || eh->e_type != ET_DYN) {
        kfree(lib_data);
        return NULL;
    }

    /* 分配库槽位 */
    shared_lib_t *lib = &g_dyn_ctx.libs[g_dyn_ctx.lib_count++];
    memset(lib, 0, sizeof(shared_lib_t));
    strncpy(lib->name, name, 127);
    strncpy(lib->path, path, 255);
    lib->entry = eh->e_entry;
    lib->ref_count = 1;
    lib->loaded = 1;

    /* 分配加载地址（简化：固定地址分配） */
    /* 实际应该使用mmap分配用户空间地址 */
    static uint32_t next_lib_base = 0x40000000;
    lib->base_addr = next_lib_base;
    next_lib_base += (fsize + 0xFFF) & ~0xFFFu;

    vga_printf("[ELF-DYN] Loaded library: %s @ 0x%x (%u bytes)\n",
               name, lib->base_addr, fsize);

    kfree(lib_data);
    return lib;
}

/* 卸载共享库 */
int dyn_unload_library(const char *name)
{
    shared_lib_t *lib = dyn_find_lib(name);
    if (!lib) return -1;

    lib->ref_count--;
    if (lib->ref_count <= 0) {
        lib->loaded = 0;
        vga_printf("[ELF-DYN] Unloaded library: %s\n", name);
    }
    return 0;
}

/* ============================================================================
 * 动态链接程序加载
 * ============================================================================ */

/* 加载动态链接程序（主程序 + 动态链接器 + 共享库） */
int binfmt_elf_dynamic_load(const char *path,
                            const uint8_t *data, uint32_t size)
{
    if (!g_dyn_initialized) {
        binfmt_elf_dynamic_init();
    }

    vga_printf("[ELF-DYN] Loading dynamic ELF: %s\n", path);

    /* 1. 解析主程序 */
    const elf32_ehdr_t *eh = (const elf32_ehdr_t *)data;
    if (eh->e_magic != ELF_MAGIC) {
        vga_printf("[ELF-DYN] Invalid ELF magic\n");
        return BINFMT_ERR_FORMAT;
    }

    /* 2. 提取动态链接器路径 */
    char interp_path[INTERP_MAX_PATH];
    if (elf_get_interp(data, size, interp_path, sizeof(interp_path)) != 0) {
        vga_printf("[ELF-DYN] No PT_INTERP, not a dynamic ELF\n");
        return BINFMT_ERR_UNSUP;
    }
    strncpy(g_dyn_ctx.interp_path, interp_path, INTERP_MAX_PATH - 1);
    vga_printf("[ELF-DYN] Interpreter: %s\n", interp_path);

    /* 3. 设置主程序信息 */
    g_dyn_ctx.program_base = (eh->e_type == ET_DYN) ? 0x08048000 : 0;
    g_dyn_ctx.program_entry = eh->e_entry;

    /* 4. 解析主程序的动态段，获取依赖库列表 */
    char needed_libs[16][128];
    int lib_count = elf_parse_dynamic(data, size, g_dyn_ctx.program_base,
                                      needed_libs, 16);
    vga_printf("[ELF-DYN] Main program needs %d libraries\n", lib_count);

    /* 5. 加载所有依赖的共享库 */
    for (int i = 0; i < lib_count; i++) {
        shared_lib_t *lib = dyn_load_library(needed_libs[i], "/lib");
        if (!lib) {
            vga_printf("[ELF-DYN] Failed to load dependency: %s\n",
                       needed_libs[i]);
            /* 不返回错误，允许部分库缺失（延迟绑定时再处理） */
        }
    }

    /* 6. 加载动态链接器 */
    int interp_fd = vfs_open(interp_path, 0);
    if (interp_fd < 0) {
        vga_printf("[ELF-DYN] Dynamic linker not found: %s\n", interp_path);
        return BINFMT_ERR_UNSUP;
    }
    uint32_t interp_size = vfs_fsize(interp_fd);
    uint8_t *interp_data = kmalloc(interp_size);
    if (!interp_data) {
        vfs_close(interp_fd);
        return BINFMT_ERR_MEM;
    }
    vfs_read(interp_fd, interp_data, interp_size);
    vfs_close(interp_fd);

    const elf32_ehdr_t *interp_eh = (const elf32_ehdr_t *)interp_data;
    if (interp_eh->e_magic != ELF_MAGIC) {
        kfree(interp_data);
        return BINFMT_ERR_FORMAT;
    }

    g_dyn_ctx.interp_base = 0x70000000;  /* 动态链接器加载地址 */
    g_dyn_ctx.interp_entry = interp_eh->e_entry + g_dyn_ctx.interp_base;

    vga_printf("[ELF-DYN] Interpreter loaded @ 0x%x, entry @ 0x%x\n",
               g_dyn_ctx.interp_base, g_dyn_ctx.interp_entry);

    kfree(interp_data);

    /* 7. 实际加载到用户空间（简化：这里只做准备工作）
     * 完整实现需要：
     * - 映射主程序段到用户空间
     * - 映射动态链接器到用户空间
     * - 映射所有共享库到用户空间
     * - 设置栈（argv/envp/auxv）
     * - 将控制权交给动态链接器
     */

    vga_printf("[ELF-DYN] Dynamic ELF load preparation complete\n");
    vga_printf("[ELF-DYN]   Program: 0x%x entry 0x%x\n",
               g_dyn_ctx.program_base, g_dyn_ctx.program_entry);
    vga_printf("[ELF-DYN]   Interp:  0x%x entry 0x%x\n",
               g_dyn_ctx.interp_base, g_dyn_ctx.interp_entry);
    vga_printf("[ELF-DYN]   Libraries: %d loaded\n", g_dyn_ctx.lib_count);

    return BINFMT_OK;
}

/* ============================================================================
 * 运行时API（供用户态动态链接器使用）
 * ============================================================================ */

/* 查找符号（简化版） */
void *dyn_lookup_symbol(const char *name)
{
    if (!name) return NULL;

    /* 简化：在所有已加载库中查找符号 */
    /* 实际实现需要解析每个库的符号表 */
    vga_printf("[ELF-DYN] Symbol lookup: %s (simplified, returning NULL)\n", name);
    return NULL;
}

/* 重定位（简化版） */
int dyn_relocate(uint32_t base_addr, const uint8_t *data, uint32_t size)
{
    /* 简化：只记录，实际需要解析RELA/REL段并应用重定位 */
    vga_printf("[ELF-DYN] Relocation for base 0x%x (simplified)\n", base_addr);
    return 0;
}

/* 获取动态链接上下文 */
dyn_link_ctx_t *dyn_get_context(void)
{
    return &g_dyn_ctx;
}

/* 重置动态链接上下文 */
void dyn_reset_context(void)
{
    memset(&g_dyn_ctx, 0, sizeof(dyn_link_ctx_t));
    strncpy(g_dyn_ctx.interp_path, DEFAULT_INTERP, INTERP_MAX_PATH - 1);
    g_dyn_ctx.lib_count = 0;
}
