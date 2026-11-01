/* ============================================================================
 * 赫普OS 64位 - ELF加载器实现
 * 从32位dynld.c移植，修改为64位ELF结构
 * V0.10.75: 初始版本
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include "elf64.h"

/* 外部函数 */
extern void vga_puts(const char *s);
extern void vga_put_hex(uint64_t val);
extern void *kmalloc(uint64_t size);
extern void kfree(void *ptr);

/* 最近一次ELF加载的大小（用于user_process_exec） */
uint64_t g_elf_last_load_size = 0;

/* 简单的内存操作函数（避免依赖string.h） */
static void elf64_memcpy(void *dst, const void *src, uint64_t n) {
    uint8_t *d = (uint8_t*)dst;
    const uint8_t *s = (const uint8_t*)src;
    for (uint64_t i = 0; i < n; i++) d[i] = s[i];
}

static void elf64_memset(void *dst, uint8_t val, uint64_t n) {
    uint8_t *d = (uint8_t*)dst;
    for (uint64_t i = 0; i < n; i++) d[i] = val;
}

static int elf64_strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *(const uint8_t*)s1 - *(const uint8_t*)s2;
}

static void elf64_strcpy(char *dst, const char *src) {
    while (*src) *dst++ = *src++;
    *dst = 0;
}

/* 已加载的模块表 */
static elf64_loaded_t loaded_modules[ELF64_MAX_LOADED];
static int elf64_initialized = 0;

/* 初始化ELF加载器 */
void elf64_init(void)
{
    for (int i = 0; i < ELF64_MAX_LOADED; i++) {
        loaded_modules[i].used = 0;
    }
    elf64_initialized = 1;
    vga_puts("[ELF64] ELF64 loader initialized\n");
}

/* 验证ELF64文件 */
int elf64_verify(void *elf_data)
{
    if (!elf_data) return -1;

    elf64_header_t *ehdr = (elf64_header_t*)elf_data;

    /* 验证魔数 */
    if (ehdr->e_magic != ELF_MAGIC) {
        vga_puts("[ELF64] ERROR: Invalid ELF magic\n");
        return -1;
    }

    /* 验证64位 */
    if (ehdr->e_class != ELFCLASS64) {
        vga_puts("[ELF64] ERROR: Not a 64-bit ELF (class=");
        vga_put_hex(ehdr->e_class);
        vga_puts(")\n");
        return -1;
    }

    /* 验证小端 */
    if (ehdr->e_data != ELFDATA2LSB) {
        vga_puts("[ELF64] ERROR: Not little-endian\n");
        return -1;
    }

    /* 验证x86_64架构 */
    if (ehdr->e_machine != EM_X86_64) {
        vga_puts("[ELF64] ERROR: Not x86_64 (machine=");
        vga_put_hex(ehdr->e_machine);
        vga_puts(")\n");
        return -1;
    }

    /* 验证类型（EXEC或DYN） */
    if (ehdr->e_type != ET_EXEC && ehdr->e_type != ET_DYN) {
        vga_puts("[ELF64] ERROR: Unsupported type (");
        vga_put_hex(ehdr->e_type);
        vga_puts(")\n");
        return -1;
    }

    return 0;
}

/* 获取入口点 */
uint64_t elf64_get_entry(void *elf_data)
{
    if (!elf_data) return 0;
    elf64_header_t *ehdr = (elf64_header_t*)elf_data;
    return ehdr->e_entry;
}

/* 加载ELF64文件到内存 */
void *elf64_load(void *elf_data, uint64_t size)
{
    (void)size;

    if (!elf64_initialized) elf64_init();
    if (elf64_verify(elf_data) != 0) return NULL;

    elf64_header_t *ehdr = (elf64_header_t*)elf_data;

    vga_puts("[ELF64] Loading ELF64: entry=0x");
    vga_put_hex(ehdr->e_entry);
    vga_puts(", phdr=");
    vga_put_hex(ehdr->e_phnum);
    vga_puts("\n");

    /* 计算需要的总内存大小（基于实际段范围，不是从0开始） */
    uint64_t min_vaddr = 0xFFFFFFFFFFFFFFFF;
    uint64_t max_end = 0;
    elf64_phdr_t *phdr = (elf64_phdr_t*)((uint8_t*)elf_data + ehdr->e_phoff);
    for (int i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type == PT_LOAD) {
            uint64_t start = phdr[i].p_vaddr & ~0xFFF;  /* 页对齐 */
            uint64_t end = phdr[i].p_vaddr + phdr[i].p_memsz;
            end = (end + 0xFFF) & ~0xFFF;  /* 页对齐 */
            if (start < min_vaddr) min_vaddr = start;
            if (end > max_end) max_end = end;
        }
    }

    if (max_end == 0 || min_vaddr == 0xFFFFFFFFFFFFFFFF) {
        vga_puts("[ELF64] ERROR: No PT_LOAD segments\n");
        return NULL;
    }

    uint64_t total_size = max_end - min_vaddr;
    if (total_size == 0) {
        vga_puts("[ELF64] ERROR: Zero size segments\n");
        return NULL;
    }

    vga_puts("[ELF64] Total memory needed: ");
    vga_put_hex(total_size);
    vga_puts(" bytes (min_vaddr=0x");
    vga_put_hex(min_vaddr);
    vga_puts(")\n");

    /* 分配内存 */
    void *load_base = kmalloc(total_size);
    if (!load_base) {
        vga_puts("[ELF64] ERROR: Memory allocation failed\n");
        return NULL;
    }
    elf64_memset(load_base, 0, total_size);

    /* 加载所有PT_LOAD段（用相对于min_vaddr的偏移） */
    for (int i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type != PT_LOAD) continue;

        vga_puts("[ELF64] Segment ");
        vga_put_hex(i);
        vga_puts(": vaddr=0x");
        vga_put_hex(phdr[i].p_vaddr);
        vga_puts(", filesz=");
        vga_put_hex(phdr[i].p_filesz);
        vga_puts(", memsz=");
        vga_put_hex(phdr[i].p_memsz);
        vga_puts("\n");

        /* 复制文件内容到虚拟地址（相对于min_vaddr） */
        if (phdr[i].p_filesz > 0) {
            elf64_memcpy((uint8_t*)load_base + (phdr[i].p_vaddr - min_vaddr),
                        (uint8_t*)elf_data + phdr[i].p_offset,
                        phdr[i].p_filesz);
        }
        /* p_memsz > p_filesz的部分（BSS）已经被memset清零 */
    }

    /* 查找符号表和字符串表 */
    elf64_shdr_t *shdr = (elf64_shdr_t*)((uint8_t*)elf_data + ehdr->e_shoff);
    elf64_loaded_t *mod = NULL;
    for (int i = 0; i < ELF64_MAX_LOADED; i++) {
        if (!loaded_modules[i].used) {
            mod = &loaded_modules[i];
            mod->used = 1;
            break;
        }
    }

    if (mod) {
        elf64_strcpy(mod->name, "main");
        mod->base = load_base;
        mod->size = total_size;
        mod->ehdr = ehdr;
        mod->symtab = NULL;
        mod->strtab = NULL;
        mod->strtab_size = 0;

        for (int i = 0; i < ehdr->e_shnum; i++) {
            if (shdr[i].sh_type == SHT_SYMTAB || shdr[i].sh_type == SHT_DYNSYM) {
                mod->symtab = (elf64_sym_t*)((uint8_t*)elf_data + shdr[i].sh_offset);
            }
            if (shdr[i].sh_type == SHT_STRTAB && shdr[i].sh_link != 0) {
                mod->strtab = (char*)((uint8_t*)elf_data + shdr[i].sh_offset);
                mod->strtab_size = shdr[i].sh_size;
            }
        }
    }

    vga_puts("[ELF64] ELF loaded successfully\n");
    g_elf_last_load_size = total_size;
    return load_base;
}

/* 加载共享库 */
int elf64_load_library(const char *name, void *data, uint64_t size)
{
    if (!name || !data) return -1;
    if (elf64_verify(data) != 0) return -1;

    for (int i = 0; i < ELF64_MAX_LOADED; i++) {
        if (!loaded_modules[i].used) {
            elf64_loaded_t *mod = &loaded_modules[i];
            elf64_memset(mod, 0, sizeof(elf64_loaded_t));
            mod->used = 1;
            elf64_strcpy(mod->name, name);
            mod->base = data;
            mod->size = size;
            mod->ehdr = (elf64_header_t*)data;
            vga_puts("[ELF64] Loaded library: ");
            vga_puts(name);
            vga_puts("\n");
            return i;
        }
    }
    return -1;
}

/* 查找符号 */
void *elf64_lookup_symbol(const char *name)
{
    if (!name) return NULL;

    for (int i = 0; i < ELF64_MAX_LOADED; i++) {
        if (!loaded_modules[i].used || !loaded_modules[i].symtab || !loaded_modules[i].strtab)
            continue;

        elf64_loaded_t *mod = &loaded_modules[i];
        elf64_shdr_t *shdr = (elf64_shdr_t*)((uint8_t*)mod->ehdr + mod->ehdr->e_shoff);

        /* 计算符号表大小 */
        int sym_count = 0;
        for (int j = 0; j < mod->ehdr->e_shnum; j++) {
            if (shdr[j].sh_type == SHT_SYMTAB || shdr[j].sh_type == SHT_DYNSYM) {
                sym_count = shdr[j].sh_size / sizeof(elf64_sym_t);
                break;
            }
        }
        if (sym_count <= 0) continue;

        for (int j = 0; j < sym_count; j++) {
            elf64_sym_t *sym = &mod->symtab[j];
            if (sym->st_name == 0) continue;
            if (sym->st_name >= mod->strtab_size) continue;

            const char *sym_name = mod->strtab + sym->st_name;
            if (elf64_strcmp(sym_name, name) == 0) {
                return (void*)((uint8_t*)mod->base + sym->st_value);
            }
        }
    }
    return NULL;
}

/* 执行重定位 */
int elf64_relocate(elf64_loaded_t *mod)
{
    if (!mod || !mod->ehdr || !mod->base) return -1;

    elf64_header_t *ehdr = mod->ehdr;
    if (ehdr->e_shoff == 0 || ehdr->e_shnum == 0) return 0;

    elf64_shdr_t *shdr = (elf64_shdr_t*)((uint8_t*)mod->ehdr + ehdr->e_shoff);

    /* 计算符号表大小 */
    int sym_count = 0;
    if (mod->symtab) {
        for (int i = 0; i < ehdr->e_shnum; i++) {
            if (shdr[i].sh_type == SHT_SYMTAB || shdr[i].sh_type == SHT_DYNSYM) {
                sym_count = shdr[i].sh_size / sizeof(elf64_sym_t);
                break;
            }
        }
    }

    int rel_count = 0;
    for (int i = 0; i < ehdr->e_shnum; i++) {
        if (shdr[i].sh_type != SHT_RELA) continue;
        if (shdr[i].sh_offset == 0 || shdr[i].sh_size == 0) continue;

        elf64_rela_t *rela = (elf64_rela_t*)((uint8_t*)mod->ehdr + shdr[i].sh_offset);
        int count = shdr[i].sh_size / sizeof(elf64_rela_t);

        for (int j = 0; j < count; j++) {
            if (rela[j].r_offset >= mod->size) continue;

            uint32_t type = ELF64_R_TYPE(rela[j].r_info);
            uint32_t sym_idx = ELF64_R_SYM(rela[j].r_info);
            uint64_t *loc = (uint64_t*)((uint8_t*)mod->base + rela[j].r_offset);

            if (type == R_X86_64_RELATIVE) {
                /* 相对重定位：base + addend */
                *loc = (uint64_t)mod->base + rela[j].r_addend;
                rel_count++;
            }
            else if (type == R_X86_64_64 || type == R_X86_64_GLOB_DAT) {
                /* 绝对重定位：symbol + addend */
                if (mod->symtab && mod->strtab && sym_idx > 0 && sym_idx < (uint32_t)sym_count) {
                    elf64_sym_t *sym = &mod->symtab[sym_idx];
                    if (sym->st_name < mod->strtab_size) {
                        const char *sym_name = mod->strtab + sym->st_name;
                        void *addr = elf64_lookup_symbol(sym_name);
                        if (addr) {
                            *loc = (uint64_t)addr + rela[j].r_addend;
                            rel_count++;
                        }
                    }
                }
            }
            else if (type == R_X86_64_PC32 || type == R_X86_64_PLT32) {
                /* PC相对重定位：symbol + addend - offset */
                if (mod->symtab && mod->strtab && sym_idx > 0 && sym_idx < (uint32_t)sym_count) {
                    elf64_sym_t *sym = &mod->symtab[sym_idx];
                    if (sym->st_name < mod->strtab_size) {
                        const char *sym_name = mod->strtab + sym->st_name;
                        void *addr = elf64_lookup_symbol(sym_name);
                        if (addr) {
                            uint32_t *loc32 = (uint32_t*)loc;
                            *loc32 = (uint32_t)((uint64_t)addr + rela[j].r_addend - rela[j].r_offset);
                            rel_count++;
                        }
                    }
                }
            }
        }
    }

    vga_puts("[ELF64] Relocation done: ");
    vga_put_hex(rel_count);
    vga_puts(" entries\n");
    return rel_count;
}
