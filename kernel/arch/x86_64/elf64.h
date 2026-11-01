/* ============================================================================
 * 赫普OS 64位 - ELF加载器
 * 从32位dynld.c移植，修改为64位ELF结构
 * V0.10.75: 初始版本，支持ELF64验证、加载、符号解析、重定位
 * ========================================================================== */

#ifndef ELF64_H
#define ELF64_H

#include <stdint.h>

/* ELF魔数 */
#define ELF_MAGIC 0x464C457F  /* "\x7FELF" */

/* ELF类 */
#define ELFCLASS32 1
#define ELFCLASS64 2

/* 数据编码 */
#define ELFDATA2LSB 1

/* 机器类型 */
#define EM_386    3
#define EM_X86_64 62

/* ELF类型 */
#define ET_EXEC 2
#define ET_DYN  3

/* ELF64头 */
typedef struct {
    uint32_t e_magic;        /* 0x7F 'E' 'L' 'F' */
    uint8_t  e_class;        /* 1=32位, 2=64位 */
    uint8_t  e_data;         /* 1=小端 */
    uint8_t  e_ident_version; /* 1 */
    uint8_t  e_osabi;        /* OS ABI */
    uint8_t  e_abiversion;   /* ABI版本 */
    uint8_t  e_pad[7];       /* 填充 */
    uint16_t e_type;         /* 2=EXEC, 3=DYN */
    uint16_t e_machine;      /* 62=AMD64 */
    uint32_t e_version;      /* 1 */
    uint64_t e_entry;        /* 入口点地址 */
    uint64_t e_phoff;        /* 程序头偏移 */
    uint64_t e_shoff;        /* 节头偏移 */
    uint32_t e_flags;        /* 标志 */
    uint16_t e_ehsize;       /* ELF头大小 */
    uint16_t e_phentsize;    /* 程序头条目大小 */
    uint16_t e_phnum;        /* 程序头数量 */
    uint16_t e_shentsize;    /* 节头条目大小 */
    uint16_t e_shnum;        /* 节头数量 */
    uint16_t e_shstrndx;     /* 节名字符串表索引 */
} __attribute__((packed)) elf64_header_t;

/* ELF64程序头 */
typedef struct {
    uint32_t p_type;         /* 段类型 */
    uint32_t p_flags;        /* 段标志 */
    uint64_t p_offset;       /* 文件偏移 */
    uint64_t p_vaddr;        /* 虚拟地址 */
    uint64_t p_paddr;        /* 物理地址 */
    uint64_t p_filesz;       /* 文件中大小 */
    uint64_t p_memsz;        /* 内存中大小 */
    uint64_t p_align;        /* 对齐 */
} __attribute__((packed)) elf64_phdr_t;

/* 程序头类型 */
#define PT_NULL    0
#define PT_LOAD    1
#define PT_DYNAMIC 2
#define PT_INTERP  3
#define PT_NOTE    4
#define PT_SHLIB   5
#define PT_PHDR    6

/* 程序头标志 */
#define PF_X 1
#define PF_W 2
#define PF_R 4

/* ELF64节头 */
typedef struct {
    uint32_t sh_name;        /* 节名索引 */
    uint32_t sh_type;        /* 节类型 */
    uint64_t sh_flags;       /* 节标志 */
    uint64_t sh_addr;        /* 虚拟地址 */
    uint64_t sh_offset;      /* 文件偏移 */
    uint64_t sh_size;        /* 节大小 */
    uint32_t sh_link;        /* 链接索引 */
    uint32_t sh_info;        /* 附加信息 */
    uint64_t sh_addralign;   /* 对齐 */
    uint64_t sh_entsize;     /* 条目大小 */
} __attribute__((packed)) elf64_shdr_t;

/* 节类型 */
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
#define SHT_DYNSYM   11

/* ELF64符号表项 */
typedef struct {
    uint32_t st_name;        /* 符号名索引 */
    uint8_t  st_info;        /* 类型和绑定 */
    uint8_t  st_other;       /* 可见性 */
    uint16_t st_shndx;       /* 节索引 */
    uint64_t st_value;       /* 符号值 */
    uint64_t st_size;        /* 符号大小 */
} __attribute__((packed)) elf64_sym_t;

/* ELF64重定位项（带附加数） */
typedef struct {
    uint64_t r_offset;       /* 重定位偏移 */
    uint64_t r_info;         /* 符号索引和类型 */
    int64_t  r_addend;       /* 附加数 */
} __attribute__((packed)) elf64_rela_t;

/* 重定位宏 */
#define ELF64_R_SYM(i)    ((i) >> 32)
#define ELF64_R_TYPE(i)   ((i) & 0xFFFFFFFF)

/* x86_64重定位类型 */
#define R_X86_64_NONE       0
#define R_X86_64_64         1
#define R_X86_64_PC32       2
#define R_X86_64_GOT32      3
#define R_X86_64_PLT32      4
#define R_X86_64_COPY       5
#define R_X86_64_GLOB_DAT   6
#define R_X86_64_JUMP_SLOT  7
#define R_X86_64_RELATIVE   8
#define R_X86_64_GOTPCREL   9
#define R_X86_64_32         10
#define R_X86_64_32S        11
#define R_X86_64_16         12
#define R_X86_64_PC16       13
#define R_X86_64_8          14
#define R_X86_64_PC8        15

/* ELF64动态段项 */
typedef struct {
    int64_t  d_tag;
    uint64_t d_un;
} __attribute__((packed)) elf64_dyn_t;

/* 动态标签 */
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
#define DT_SONAME   14
#define DT_RPATH    15
#define DT_SYMBOLIC 16
#define DT_REL      17
#define DT_RELSZ    18
#define DT_RELENT   19
#define DT_PLTREL   20
#define DT_DEBUG    21
#define DT_JMPREL   23

/* 已加载的ELF模块 */
typedef struct {
    int used;
    char name[64];
    void *base;              /* 加载基地址 */
    uint64_t size;           /* 总大小 */
    elf64_header_t *ehdr;    /* ELF头 */
    elf64_sym_t *symtab;     /* 符号表 */
    char *strtab;            /* 字符串表 */
    uint64_t strtab_size;    /* 字符串表大小 */
} elf64_loaded_t;

#define ELF64_MAX_LOADED 8

/* 函数声明 */
void elf64_init(void);
int elf64_verify(void *elf_data);
uint64_t elf64_get_entry(void *elf_data);
void *elf64_load(void *elf_data, uint64_t size);
int elf64_load_library(const char *name, void *data, uint64_t size);
void *elf64_lookup_symbol(const char *name);
int elf64_relocate(elf64_loaded_t *lib);

#endif /* ELF64_H */
