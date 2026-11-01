/* ============================================================================
 * 赫普OS - ELF32 标准结构定义
 * 依据: System V ABI / ELF 规范 (公开标准), 32-bit 小端 (i386)
 * 用途: Linux ELF32 静态可执行程序兼容加载 (V3.87)
 * ========================================================================== */

#ifndef HEPU_ELF32_H
#define HEPU_ELF32_H

#include <stdint.h>

/* --- 魔数 --- */
#define ELF_MAGIC       0x464C457Fu  /* "\x7FELF" 小端 */

/* 文件类型 e_type */
#define ET_NONE         0
#define ET_REL          1
#define ET_EXEC         2       /* 静态可执行 (固定地址) */
#define ET_DYN          3       /* 动态/共享 (PIE) */

/* 机器类型 e_machine */
#define EM_386          3       /* Intel 80386 */

/* 程序头类型 p_type */
#define PT_NULL         0
#define PT_LOAD         1       /* 可加载段 */
#define PT_DYNAMIC      2
#define PT_INTERP       3       /* 解释器 (动态链接, 阶段A不支持) */
#define PT_PHDR         6

/* 段标志 p_flags */
#define PF_X            1
#define PF_W            2
#define PF_R            4

/* ELF 头 (52 字节) */
typedef struct {
    uint32_t    e_magic;        /* 0x464C457F "\x7FELF" */
    uint8_t     e_class;        /* 1=32位 */
    uint8_t     e_data;         /* 1=小端 */
    uint8_t     e_version;
    uint8_t     e_osabi;
    uint8_t     e_abiversion;
    uint8_t     e_pad[7];
    uint16_t    e_type;         /* ET_EXEC=2 */
    uint16_t    e_machine;      /* EM_386=3 */
    uint32_t    e_version2;
    uint32_t    e_entry;        /* 入口点虚拟地址 */
    uint32_t    e_phoff;        /* 程序头表偏移 */
    uint32_t    e_shoff;        /* 节头表偏移 */
    uint32_t    e_flags;
    uint16_t    e_ehsize;
    uint16_t    e_phentsize;
    uint16_t    e_phnum;
    uint16_t    e_shentsize;
    uint16_t    e_shnum;
    uint16_t    e_shstrndx;
} __attribute__((packed)) elf32_ehdr_t;

/* 程序头 (32 字节) */
typedef struct {
    uint32_t    p_type;         /* PT_LOAD=1 */
    uint32_t    p_offset;       /* 段在文件中的偏移 */
    uint32_t    p_vaddr;        /* 虚拟地址 */
    uint32_t    p_paddr;
    uint32_t    p_filesz;       /* 文件中的大小 */
    uint32_t    p_memsz;        /* 内存中的大小 (>= filesz, 超出=bss) */
    uint32_t    p_flags;        /* PF_R/PF_W/PF_X */
    uint32_t    p_align;        /* 对齐 */
} __attribute__((packed)) elf32_phdr_t;

/* 加载段描述 (binfmt_elf 输出) */
typedef struct {
    uint32_t    vaddr;          /* 页对齐起始虚拟地址 */
    uint32_t    offset;         /* 页对齐文件偏移 */
    uint32_t    filesz_pg;      /* 需从文件复制的字节数 (页内) */
    uint32_t    memsz_pg;       /* 映射总大小 (含bss, 页对齐) */
    uint32_t    flags;          /* PF_* */
} elf_load_seg_t;

#define ELF_MAX_LOAD_SEGS  8

#endif /* HEPU_ELF32_H */
