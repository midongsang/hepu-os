#ifndef DYNLD_H
#define DYNLD_H

#include <stdint.h>

/* ELF类型 */
#define ELF_MAGIC 0x464C457F  /* "\x7FELF" */
#define ELF_CLASS32 1
#define ELF_DATA2LSB 2
#define ELF_TYPE_EXEC 2
#define ELF_TYPE_DYN  3
#define EM_386 3

/* ELF头 */
typedef struct {
    uint32_t e_magic;
    uint8_t  e_class;
    uint8_t  e_data;
    uint8_t  e_version;
    uint8_t  e_osabi;
    uint8_t  e_abiversion;
    uint8_t  e_pad[7];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version2;
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
} __attribute__((packed)) elf_header_t;

/* 程序头 */
typedef struct {
    uint32_t p_type;
    uint32_t p_offset;
    uint32_t p_vaddr;
    uint32_t p_paddr;
    uint32_t p_filesz;
    uint32_t p_memsz;
    uint32_t p_flags;
    uint32_t p_align;
} __attribute__((packed)) elf_phdr_t;

#define PT_LOAD    1
#define PT_DYNAMIC 2
#define PT_INTERP  3

/* 节头 */
typedef struct {
    uint32_t sh_name;
    uint32_t sh_type;
    uint32_t sh_flags;
    uint32_t sh_addr;
    uint32_t sh_offset;
    uint32_t sh_size;
    uint32_t sh_link;
    uint32_t sh_info;
    uint32_t sh_addralign;
    uint32_t sh_entsize;
} __attribute__((packed)) elf_shdr_t;

#define SHT_SYMTAB 2
#define SHT_STRTAB 3
#define SHT_REL    9
#define SHT_DYNSYM 11

/* 符号表项 */
typedef struct {
    uint32_t st_name;
    uint32_t st_value;
    uint32_t st_size;
    uint8_t  st_info;
    uint8_t  st_other;
    uint16_t st_shndx;
} __attribute__((packed)) elf_sym_t;

/* 重定位项 */
typedef struct {
    uint32_t r_offset;
    uint32_t r_info;
} __attribute__((packed)) elf_rel_t;

#define ELF32_R_SYM(i)    ((i) >> 8)
#define ELF32_R_TYPE(i)   ((uint8_t)(i))
#define R_386_NONE        0
#define R_386_32          1
#define R_386_PC32        2
#define R_386_GLOB_DAT    6
#define R_386_JMP_SLOT    7
#define R_386_RELATIVE    8

/* 动态段项 */
typedef struct {
    int32_t  d_tag;
    uint32_t d_un;
} __attribute__((packed)) elf_dyn_t;

#define DT_NULL     0
#define DT_NEEDED   1
#define DT_STRTAB   5
#define DT_SYMTAB   6
#define DT_STRSZ    10
#define DT_SYMENT   11
#define DT_REL      17
#define DT_RELSZ    18
#define DT_RELENT   19
#define DT_JMPREL   23

/* 已加载的共享库 */
typedef struct {
    int used;
    char name[64];
    void *base;
    uint32_t size;
    elf_header_t *ehdr;
    elf_sym_t *symtab;
    char *strtab;
    uint32_t strtab_size;
} loaded_lib_t;

#define MAX_LOADED_LIBS 8

/* 初始化动态链接器 */
void dynld_init(void);

/* 加载ELF文件 */
void *dynld_load_elf(void *elf_data, uint32_t size);

/* 加载共享库 */
int dynld_load_library(const char *name, void *data, uint32_t size);

/* 查找符号 */
void *dynld_lookup_symbol(const char *name);

/* 执行重定位 */
int dynld_relocate(loaded_lib_t *lib);

/* 获取入口点 */
uint32_t dynld_get_entry(void *elf_data);

/* 验证ELF */
int dynld_verify_elf(void *elf_data);

#endif /* DYNLD_H */
