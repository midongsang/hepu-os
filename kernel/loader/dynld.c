/* ============================================================================
 * 赫普OS - ELF 动态链接器
 * ELF加载、符号解析、重定位、共享库管理
 * ========================================================================== */

#include <stdint.h>
#include "dynld.h"
#include "vga.h"
#include "string.h"
#include "kheap.h"

static loaded_lib_t loaded_libs[MAX_LOADED_LIBS];
static int dynld_initialized = 0;

void dynld_init(void)
{
    for (int i = 0; i < MAX_LOADED_LIBS; i++) {
        loaded_libs[i].used = 0;
    }
    dynld_initialized = 1;
    vga_puts("[DYNLD] dynld init done \n");
}

int dynld_verify_elf(void *elf_data)
{
    if (!elf_data) return -1;
    elf_header_t *ehdr = (elf_header_t *)elf_data;
    if (ehdr->e_magic != ELF_MAGIC) {
        vga_puts("[DYNLD] error: not valid ELF file \n");
        return -1;
    }
    if (ehdr->e_class != ELF_CLASS32) {
        vga_puts("[DYNLD] error: not supports 64 bit ELF\n");
        return -1;
    }
    if (ehdr->e_machine != EM_386) {
        vga_puts("[DYNLD] error: unsupported arch \n");
        return -1;
    }
    return 0;
}

uint32_t dynld_get_entry(void *elf_data)
{
    if (!elf_data) return 0;
    elf_header_t *ehdr = (elf_header_t *)elf_data;
    return ehdr->e_entry;
}

void *dynld_load_elf(void *elf_data, uint32_t size)
{
    (void)size;
    if (dynld_verify_elf(elf_data) != 0) return NULL;

    elf_header_t *ehdr = (elf_header_t *)elf_data;
    vga_printf("[DYNLD] load ELF: entry =0x%x, prog hdr =%d \n",
               ehdr->e_entry, ehdr->e_phnum);

    /* 分配内存加载所有PT_LOAD段 */
    uint32_t total_size = 0;
    elf_phdr_t *phdr = (elf_phdr_t *)((uint8_t *)elf_data + ehdr->e_phoff);
    for (int i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type == PT_LOAD) {
            uint32_t end = phdr[i].p_vaddr + phdr[i].p_memsz;
            if (end > total_size) total_size = end;
        }
    }

    void *load_base = kmalloc(total_size);
    if (!load_base) {
        vga_puts("[DYNLD] error: mem alloc failed \n");
        return NULL;
    }
    memset(load_base, 0, total_size);

    /* 加载段 */
    for (int i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type == PT_LOAD) {
            memcpy((uint8_t *)load_base + phdr[i].p_vaddr,
                   (uint8_t *)elf_data + phdr[i].p_offset,
                   phdr[i].p_filesz);
            vga_printf(" seg %d: vaddr=0x%x filesz=%d memsz=%d\n",
                       i, phdr[i].p_vaddr, phdr[i].p_filesz, phdr[i].p_memsz);
        }
    }

    /* 查找符号表和字符串表 */
    elf_shdr_t *shdr = (elf_shdr_t *)((uint8_t *)elf_data + ehdr->e_shoff);
    loaded_lib_t *lib = NULL;
    for (int i = 0; i < MAX_LOADED_LIBS; i++) {
        if (!loaded_libs[i].used) {
            lib = &loaded_libs[i];
            lib->used = 1;
            break;
        }
    }
    if (lib) {
        strcpy(lib->name, "main");
        lib->base = load_base;
        lib->size = total_size;
        lib->ehdr = ehdr;
        lib->symtab = NULL;
        lib->strtab = NULL;

        for (int i = 0; i < ehdr->e_shnum; i++) {
            if (shdr[i].sh_type == SHT_SYMTAB || shdr[i].sh_type == SHT_DYNSYM) {
                lib->symtab = (elf_sym_t *)((uint8_t *)elf_data + shdr[i].sh_offset);
            }
            if (shdr[i].sh_type == SHT_STRTAB && shdr[i].sh_link != 0) {
                lib->strtab = (char *)((uint8_t *)elf_data + shdr[i].sh_offset);
                lib->strtab_size = shdr[i].sh_size;
            }
        }
    }

    vga_puts("[DYNLD] ELF load done \n");
    return load_base;
}

int dynld_load_library(const char *name, void *data, uint32_t size)
{
    if (!name || !data) return -1;
    if (dynld_verify_elf(data) != 0) return -1;

    for (int i = 0; i < MAX_LOADED_LIBS; i++) {
        if (!loaded_libs[i].used) {
            loaded_lib_t *lib = &loaded_libs[i];
            memset(lib, 0, sizeof(loaded_lib_t));
            lib->used = 1;
            strncpy(lib->name, name, 63);
            lib->base = data;
            lib->size = size;
            lib->ehdr = (elf_header_t *)data;
            vga_printf("[DYNLD] load shared lib: %s\n", name);
            return i;
        }
    }
    return -1;
}

void *dynld_lookup_symbol(const char *name)
{
    if (!name) return NULL;

    for (int i = 0; i < MAX_LOADED_LIBS; i++) {
        if (!loaded_libs[i].used || !loaded_libs[i].symtab || !loaded_libs[i].strtab)
            continue;

        loaded_lib_t *lib = &loaded_libs[i];
        elf_shdr_t *shdr = (elf_shdr_t *)((uint8_t *)lib->ehdr + lib->ehdr->e_shoff);

        /* 查找符号表节的大小 */
        int sym_count = 0;
        for (int j = 0; j < lib->ehdr->e_shnum; j++) {
            if (shdr[j].sh_type == SHT_SYMTAB || shdr[j].sh_type == SHT_DYNSYM) {
                sym_count = shdr[j].sh_size / sizeof(elf_sym_t);
                break;
            }
        }
        if (sym_count <= 0) continue;

        for (int j = 0; j < sym_count; j++) {
            elf_sym_t *sym = &lib->symtab[j];
            if (sym->st_name == 0) continue;
            /* 检查符号名是否在字符串表范围内 */
            if (sym->st_name >= lib->strtab_size) continue;
            const char *sym_name = lib->strtab + sym->st_name;
            if (strcmp(sym_name, name) == 0) {
                return (void *)((uint8_t *)lib->base + sym->st_value);
            }
        }
    }
    return NULL;
}

int dynld_relocate(loaded_lib_t *lib)
{
    if (!lib || !lib->ehdr || !lib->base) return -1;

    elf_header_t *ehdr = lib->ehdr;
    if (ehdr->e_shoff == 0 || ehdr->e_shnum == 0) return 0;

    elf_shdr_t *shdr = (elf_shdr_t *)((uint8_t *)lib->ehdr + ehdr->e_shoff);

    /* 计算符号表大小 */
    int sym_count = 0;
    if (lib->symtab) {
        for (int i = 0; i < ehdr->e_shnum; i++) {
            if (shdr[i].sh_type == SHT_SYMTAB || shdr[i].sh_type == SHT_DYNSYM) {
                sym_count = shdr[i].sh_size / sizeof(elf_sym_t);
                break;
            }
        }
    }

    int rel_count = 0;
    for (int i = 0; i < ehdr->e_shnum; i++) {
        if (shdr[i].sh_type != SHT_REL) continue;
        if (shdr[i].sh_offset == 0 || shdr[i].sh_size == 0) continue;

        elf_rel_t *rel = (elf_rel_t *)((uint8_t *)lib->ehdr + shdr[i].sh_offset);
        int count = shdr[i].sh_size / sizeof(elf_rel_t);

        for (int j = 0; j < count; j++) {
            /* 检查重定位偏移是否在加载范围内 */
            if (rel[j].r_offset >= lib->size) continue;

            uint8_t type = ELF32_R_TYPE(rel[j].r_info);
            uint32_t sym_idx = ELF32_R_SYM(rel[j].r_info);
            uint32_t *loc = (uint32_t *)((uint8_t *)lib->base + rel[j].r_offset);

            if (type == R_386_RELATIVE) {
                *loc += (uint32_t)lib->base;
                rel_count++;
            }
            else if (type == R_386_32 || type == R_386_GLOB_DAT) {
                if (lib->symtab && lib->strtab && sym_idx > 0 && sym_idx < (uint32_t)sym_count) {
                    elf_sym_t *sym = &lib->symtab[sym_idx];
                    if (sym->st_name < lib->strtab_size) {
                        const char *sym_name = lib->strtab + sym->st_name;
                        void *addr = dynld_lookup_symbol(sym_name);
                        if (addr) {
                            *loc = (uint32_t)addr;
                            rel_count++;
                        }
                    }
                }
            }
            else if (type == R_386_PC32) {
                if (lib->symtab && lib->strtab && sym_idx > 0 && sym_idx < (uint32_t)sym_count) {
                    elf_sym_t *sym = &lib->symtab[sym_idx];
                    if (sym->st_name < lib->strtab_size) {
                        const char *sym_name = lib->strtab + sym->st_name;
                        void *addr = dynld_lookup_symbol(sym_name);
                        if (addr) {
                            *loc = (uint32_t)addr - (uint32_t)loc;
                            rel_count++;
                        }
                    }
                }
            }
        }
    }

    vga_printf("[DYNLD] reloc done: %d \n", rel_count);
    return rel_count;
}
