/* ============================================================================
 * 赫普OS - 动态链接器头文件
 * V8.34: 初始实现
 * ========================================================================== */

#ifndef _LDSO_H
#define _LDSO_H

#include <stdint.h>
#include <stddef.h>

/* ELF类型定义 */
typedef uint16_t Elf32_Half;
typedef uint32_t Elf32_Word;
typedef int32_t  Elf32_Sword;
typedef uint32_t Elf32_Addr;
typedef uint32_t Elf32_Off;

/* ELF头部 */
#define EI_NIDENT 16
typedef struct {
    unsigned char e_ident[EI_NIDENT];
    Elf32_Half    e_type;
    Elf32_Half    e_machine;
    Elf32_Word    e_version;
    Elf32_Addr    e_entry;
    Elf32_Off     e_phoff;
    Elf32_Off     e_shoff;
    Elf32_Word    e_flags;
    Elf32_Half    e_ehsize;
    Elf32_Half    e_phentsize;
    Elf32_Half    e_phnum;
    Elf32_Half    e_shentsize;
    Elf32_Half    e_shnum;
    Elf32_Half    e_shstrndx;
} Elf32_Ehdr;

/* 程序头部 */
typedef struct {
    Elf32_Word p_type;
    Elf32_Off  p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
} Elf32_Phdr;

/* 节头部 */
typedef struct {
    Elf32_Word sh_name;
    Elf32_Word sh_type;
    Elf32_Word sh_flags;
    Elf32_Addr sh_addr;
    Elf32_Off  sh_offset;
    Elf32_Word sh_size;
    Elf32_Word sh_link;
    Elf32_Word sh_info;
    Elf32_Word sh_addralign;
    Elf32_Word sh_entsize;
} Elf32_Shdr;

/* 符号表条目 */
typedef struct {
    Elf32_Word    st_name;
    Elf32_Addr    st_value;
    Elf32_Word    st_size;
    unsigned char st_info;
    unsigned char st_other;
    Elf32_Half    st_shndx;
} Elf32_Sym;

/* 重定位条目 */
typedef struct {
    Elf32_Addr r_offset;
    Elf32_Word r_info;
} Elf32_Rel;

typedef struct {
    Elf32_Addr  r_offset;
    Elf32_Word  r_info;
    Elf32_Sword r_addend;
} Elf32_Rela;

/* 动态段条目 */
typedef struct {
    Elf32_Sword d_tag;
    union {
        Elf32_Word d_val;
        Elf32_Addr d_ptr;
    } d_un;
} Elf32_Dyn;

/* ELF类型 */
#define ET_NONE   0
#define ET_REL    1
#define ET_EXEC   2
#define ET_DYN    3
#define ET_CORE   4

/* 程序头部类型 */
#define PT_NULL    0
#define PT_LOAD    1
#define PT_DYNAMIC 2
#define PT_INTERP  3
#define PT_NOTE    4
#define PT_SHLIB   5
#define PT_PHDR    6

/* 节头部类型 */
#define SHT_NULL     0
#define SHT_PROGBITS 1
#define SHT_SYMTAB   2
#define SHT_STRTAB   3
#define SHT_RELA     4
#define SHT_HASH     5
#define SHT_DYNAMIC  6
#define SHT_NOTE     7
#define SHT_NOBITS   8
#define SHT_REL      9
#define SHT_SHLIB    10
#define SHT_DYNSYM   11

/* 动态段标签 */
#define DT_NULL     0
#define DT_NEEDED   1
#define DT_PLTRELSZ 2
#define DT_PLTGOT   3
#define DT_HASH     4
#define DT_STRTAB   5
#define DT_SYMTAB   6
#define DT_RELA     7
#define DT_RELASZ   8
#define DT_RELAENT  9
#define DT_STRSZ    10
#define DT_SYMENT   11
#define DT_INIT     12
#define DT_FINI     13
#define DT_SONAME   14
#define DT_RPATH    15
#define DT_SYMBOLIC 16
#define DT_REL      17
#define DT_RELSZ    18
#define DT_RELENT   19
#define DT_PLTREL   20
#define DT_DEBUG    21
#define DT_TEXTREL  22
#define DT_JMPREL   23
#define DT_BIND_NOW 24
#define DT_INIT_ARRAY  25
#define DT_FINI_ARRAY  26
#define DT_INIT_ARRAYSZ 27
#define DT_FINI_ARRAYSZ 28

/* 重定位类型 */
#define R_386_NONE      0
#define R_386_32        1
#define R_386_PC32      2
#define R_386_GOT32     3
#define R_386_PLT32     4
#define R_386_COPY      5
#define R_386_GLOB_DAT  6
#define R_386_JMP_SLOT  7
#define R_386_RELATIVE  8
#define R_386_GOTOFF    9
#define R_386_GOTPC     10

/* 符号绑定 */
#define STB_LOCAL  0
#define STB_GLOBAL 1
#define STB_WEAK   2

/* 符号类型 */
#define STT_NOTYPE  0
#define STT_OBJECT  1
#define STT_FUNC    2
#define STT_SECTION 3
#define STT_FILE    4

#define ELF32_ST_BIND(info) ((info) >> 4)
#define ELF32_ST_TYPE(info) ((info) & 0xf)
#define ELF32_R_SYM(info)   ((info) >> 8)
#define ELF32_R_TYPE(info)  ((unsigned char)(info))

/* 最大共享库数量 */
#define MAX_SHARED_LIBS 16

/* 共享库结构 */
typedef struct {
    char     name[64];      /* 库名称 */
    void    *base;          /* 加载基地址 */
    void    *entry;         /* 入口点 */
    Elf32_Dyn *dynamic;     /* 动态段 */
    Elf32_Sym *symtab;      /* 符号表 */
    char     *strtab;       /* 字符串表 */
    Elf32_Rel *rel;         /* 重定位表 */
    uint32_t  rel_count;    /* 重定位数量 */
    Elf32_Rel *jmp_rel;     /* PLT重定位表 */
    uint32_t  jmp_rel_count; /* PLT重定位数量 */
    int       loaded;       /* 是否已加载 */
} shared_lib_t;

/* 动态链接器状态 */
typedef struct {
    shared_lib_t libs[MAX_SHARED_LIBS];
    int  lib_count;
    void *main_base;        /* 主程序基地址 */
    int  initialized;       /* 是否已初始化 */
    char  error[256];       /* 错误信息 */
} ldso_state_t;

/* 函数声明 */
int ldso_init(void *main_program);
int ldso_load_library(const char *name);
void *ldso_resolve_symbol(const char *name);
int ldso_relocate(shared_lib_t *lib);
int ldso_process_dynamic(shared_lib_t *lib);
void ldso_set_error(const char *msg);
const char *ldso_get_error(void);

/* 系统调用接口（用户态） */
extern void *mmap(void *addr, uint32_t length, int prot, int flags,
                   int fd, uint32_t offset);
extern int munmap(void *addr, uint32_t length);
extern void *malloc(uint32_t size);
extern void free(void *ptr);
extern void *memcpy(void *dst, const void *src, uint32_t n);
extern void *memset(void *dst, int val, uint32_t n);
extern int strlen(const char *s);
extern int strcmp(const char *s1, const char *s2);
extern char *strncpy(char *dst, const char *src, uint32_t n);

#endif /* _LDSO_H */
