/* ============================================================================
 * 赫普OS 64位 - 内核驱动模块加载器 (kmod64)
 * V0.13.24: P0 底层 — 驱动模块内核态加载 (ELF64 可重定位对象 ET_REL)
 *
 * 参考 Linux modprobe 机制: 驱动以 ELF .o 形式发布,
 * 加载时合并节、解析符号(本地 + 内核导出)、应用重定位, 调用 module_init。
 *
 * 支持的 x86-64 重定位类型:
 *   R_X86_64_64(1) 绝对64位     R_X86_64_PC32(2) 相对32位
 *   R_X86_64_PLT32(4) 相对(调用) R_X86_64_32(10)/R_X86_64_32S(11) 截断绝对
 *   R_X86_64_PC64(24) 相对64位
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>

extern void *kmalloc(uint64_t size);
extern void kfree(void *ptr);
extern void vga_puts(const char *s);
extern int ext2_64_read_file(const char *path, uint8_t *buffer, uint32_t max_size);

/* ---------- ELF64 结构 ---------- */
typedef struct {
    uint8_t  e_ident[16];
    uint16_t e_type, e_machine;
    uint32_t e_version;
    uint64_t e_entry, e_phoff, e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx;
} __attribute__((packed)) kmod_ehdr_t;

typedef struct {
    uint32_t sh_name, sh_type;
    uint64_t sh_flags;
    uint64_t sh_addr, sh_offset, sh_size;
    uint32_t sh_link, sh_info;
    uint64_t sh_addralign, sh_entsize;
} __attribute__((packed)) kmod_shdr_t;

typedef struct {
    uint32_t st_name;
    uint8_t  st_info, st_other;
    uint16_t st_shndx;
    uint64_t st_value, st_size;
} __attribute__((packed)) kmod_sym_t;

typedef struct {
    uint64_t r_offset;
    uint64_t r_info;
    int64_t  r_addend;
} __attribute__((packed)) kmod_rela_t;

/* 重定位类型 */
#define R_X86_64_64    1
#define R_X86_64_PC32  2
#define R_X86_64_PLT32 4
#define R_X86_64_32   10
#define R_X86_64_32S  11
#define R_X86_64_PC64 24
#define R_X86_64_GOTPCRELX 42

/* ---------- 内核导出符号表 ---------- */
typedef struct { const char *name; void *addr; } kmod_export_t;

#define KMOD_EXPORT(fn) { #fn, (void *)fn }
static const kmod_export_t kmod_exports[] = {
    KMOD_EXPORT(vga_puts),
    KMOD_EXPORT(kmalloc),
    KMOD_EXPORT(kfree),
    KMOD_EXPORT(ext2_64_read_file),
};
static const int kmod_export_count =
    sizeof(kmod_exports) / sizeof(kmod_exports[0]);

/* ---------- 已加载模块 ---------- */
#define KMOD_MAX_MODULES 16
#define KMOD_MAX_SYMS    256

typedef struct {
    char name[48];
    void *base;              /* 分配内存基址 */
    uint64_t size;           /* 分配大小 */
    uint64_t text_vaddr;     /* .text 重定位基址(=base) */
    int (*init_fn)(void);
    void (*exit_fn)(void);
    kmod_sym_t *symtab;      /* 保留符号表 */
    const char *strtab;
    int sym_count;
    uint64_t got_base;       /* 合成 GOT 基址 */
    uint64_t got_size;
    int used;
} kmod_module_t;

static kmod_module_t g_kmods[KMOD_MAX_MODULES];

/* ---------- 字符串/内存工具 ---------- */
static int kmod_strcmp(const char *a, const char *b)
{
    while (*a && *b) {
        if (*a != *b) return (uint8_t)*a - (uint8_t)*b;
        a++; b++;
    }
    return (uint8_t)*a - (uint8_t)*b;
}

static void kmod_memcpy(void *dst, const void *src, uint64_t n)
{
    uint8_t *d = dst; const uint8_t *s = src;
    for (uint64_t i = 0; i < n; i++) d[i] = s[i];
}

static void kmod_memset(void *dst, uint8_t val, uint64_t n)
{
    uint8_t *d = dst;
    for (uint64_t i = 0; i < n; i++) d[i] = val;
}

#define KMOD_DEBUG_TRACE 0
static void kmod_hex(uint64_t v)
{
    const char *h = "0123456789abcdef";
    char t[20]; int p = 0;
    for (int i = 60; i >= 0; i -= 4) t[p++] = h[(v >> i) & 0xF];
    t[p] = 0;
    vga_puts(t);
}

static uint64_t kmod_resolve_symbol(kmod_module_t *mod, const char *name)
{
    /* 1. 本地符号(已定义, 非未定义节) */
    if (mod->symtab) {
        for (int i = 0; i < mod->sym_count; i++) {
            kmod_sym_t *s = &mod->symtab[i];
            if (s->st_shndx == 0 || s->st_name == 0) continue;
            if (s->st_shndx == 0xFFF1) continue; /* ABS 绝对 */
            const char *n = mod->strtab + s->st_name;
            if (kmod_strcmp(n, name) == 0) {
                return mod->text_vaddr + s->st_value;
            }
        }
    }
    /* 2. 内核导出表 */
    for (int i = 0; i < kmod_export_count; i++) {
        if (kmod_strcmp(kmod_exports[i].name, name) == 0)
            return (uint64_t)kmod_exports[i].addr;
    }
    return 0;
}

/* ---------- 加载 ELF 可重定位对象 ---------- */
int kmod64_load(const char *name, void *elf_data, uint64_t size)
{
    if (!elf_data || size < 64) return -1;

    /* 找空槽 */
    int slot = -1;
    for (int i = 0; i < KMOD_MAX_MODULES; i++) {
        if (!g_kmods[i].used) { slot = i; break; }
    }
    if (slot < 0) return -1;

    kmod_ehdr_t *eh = (kmod_ehdr_t *)elf_data;
    if (eh->e_ident[0] != 0x7F || eh->e_ident[1] != 'E' ||
        eh->e_ident[2] != 'L' || eh->e_ident[3] != 'F')
        return -1;
    if (eh->e_type != 1) return -1;            /* ET_REL */
    if (eh->e_machine != 62) return -1;        /* x86-64 */
    if (eh->e_shoff + eh->e_shnum * eh->e_shentsize > size) return -1;

    kmod_shdr_t *sh = (kmod_shdr_t *)((uint8_t *)elf_data + eh->e_shoff);

    /* 1. 计算分配大小(ALIGN 8) */
    uint64_t mem_size = 0;
    for (int i = 0; i < eh->e_shnum; i++) {
        uint32_t t = sh[i].sh_type;
        if (t == 1 /*PROGBITS*/ || t == 8 /*NOBITS*/) {
            if (sh[i].sh_flags & 0x2 /*ALLOC*/) {
                uint64_t end = sh[i].sh_offset + sh[i].sh_size;
                /* 注意: .o 里 sh_offset 是文件偏移; 内存布局按节顺序估算 */
                if (end > mem_size) mem_size = end;
            }
        }
    }
    if (mem_size == 0) return -1;

    /* 2. 统计 GOT 项并分配内存 (页对齐, 可执行) */
    int got_count = 0;
    for (int i = 0; i < eh->e_shnum; i++) {
        if (sh[i].sh_type != 4 /*RELA*/) continue;
        kmod_rela_t *rr = (kmod_rela_t *)((uint8_t *)elf_data + sh[i].sh_offset);
        int nr = (int)(sh[i].sh_size / sizeof(kmod_rela_t));
        for (int j = 0; j < nr; j++) {
            if ((rr[j].r_info & 0xFFFFFFFF) == R_X86_64_GOTPCRELX) got_count++;
        }
    }
    /* PLT32(外部函数调用)也需要合成 PLT 存根 */
    int plt_count = 0;
    for (int i = 0; i < eh->e_shnum; i++) {
        if (sh[i].sh_type != 4) continue;
        kmod_rela_t *rr = (kmod_rela_t *)((uint8_t *)elf_data + sh[i].sh_offset);
        int nr = (int)(sh[i].sh_size / sizeof(kmod_rela_t));
        for (int j = 0; j < nr; j++) {
            uint64_t t = rr[j].r_info & 0xFFFFFFFF;
            if (t == R_X86_64_PLT32) plt_count++;
        }
    }
    uint64_t got_size = (uint64_t)(got_count + plt_count) * 8;
    uint64_t plt_size = (uint64_t)plt_count * 16;   /* 每槽 16B: jmp *[rip+..] */
    uint64_t mem_total = (mem_size + got_size + plt_size + 0xFFF) & ~0xFFFULL;
    uint64_t alloc = mem_total;
    uint8_t *base = (uint8_t *)kmalloc(alloc);
    if (!base) return -1;
    kmod_memset(base, 0, alloc);

    /* 3. 复制 PROGBITS 节 (保持文件偏移布局); 合成 GOT 紧随其后 */
    uint64_t got_base = (uint64_t)base + ((mem_size + 7) & ~7ULL);
    uint64_t plt_base = got_base + got_size;
    uint64_t got_idx = 0;   /* GOT 槽统一分配 */

    /* 3b. 复制 PROGBITS 节 */
    for (int i = 0; i < eh->e_shnum; i++) {
        if (sh[i].sh_type == 1 /*PROGBITS*/ && (sh[i].sh_flags & 0x2)) {
            if (sh[i].sh_offset + sh[i].sh_size > size) { kfree(base); return -1; }
            kmod_memcpy(base + sh[i].sh_offset,
                        (uint8_t *)elf_data + sh[i].sh_offset, sh[i].sh_size);
        }
    }

    /* 4. 符号表 */
    kmod_sym_t *symtab = 0;
    const char *strtab = 0;
    int sym_count = 0;
    for (int i = 0; i < eh->e_shnum; i++) {
        if (sh[i].sh_type == 2 /*SYMTAB*/) {
            symtab = (kmod_sym_t *)((uint8_t *)elf_data + sh[i].sh_offset);
            sym_count = (int)(sh[i].sh_size / sizeof(kmod_sym_t));
        } else if (sh[i].sh_type == 3 /*STRTAB*/ && i != eh->e_shstrndx) {
            strtab = (const char *)((uint8_t *)elf_data + sh[i].sh_offset);
        }
    }

    /* 5. 应用重定位 (.rela.*) */
    for (int i = 0; i < eh->e_shnum; i++) {
        if (sh[i].sh_type != 4 /*RELA*/) continue;
        int target_sec = (int)sh[i].sh_info;   /* 目标节 */
        kmod_rela_t *rela = (kmod_rela_t *)((uint8_t *)elf_data + sh[i].sh_offset);
        int nrela = (int)(sh[i].sh_size / sizeof(kmod_rela_t));
        for (int j = 0; j < nrela; j++) {
            uint64_t type = rela[j].r_info & 0xFFFFFFFF;
            uint64_t sym_idx = rela[j].r_info >> 32;
            /* 重定位写入位置: 目标节基址 + 节内偏移 */
            uint64_t paddr = (uint64_t)base + sh[target_sec].sh_offset + rela[j].r_offset;
            int64_t S = 0;
            if (sym_idx < (uint64_t)sym_count && symtab) {
                kmod_sym_t *sym = &symtab[sym_idx];
                if (sym->st_shndx == 0) {
                    /* 外部符号: 内核导出 */
                    const char *n = strtab ? strtab + sym->st_name : "";
                    S = (int64_t)kmod_resolve_symbol(&g_kmods[slot], n);
                    if (S == 0) { kfree(base); return -2; }  /* 未解析符号 */
                } else {
                    /* 本地符号: 所在节在内存中的位置 + 节内偏移 */
                    uint16_t sec = sym->st_shndx;
                    uint64_t sec_base = 0;
                    if (sec < eh->e_shnum)
                        sec_base = sh[sec].sh_offset;
                    S = (int64_t)base + sec_base + sym->st_value;
                }
            }
            int64_t A = rela[j].r_addend;
            uint64_t P = paddr;

            switch (type) {
            case R_X86_64_64:
                if (KMOD_DEBUG_TRACE) vga_puts("[kmod] R_64\n");
                *(uint64_t *)paddr = (uint64_t)(S + A); break;
            case R_X86_64_PC32:
                if (KMOD_DEBUG_TRACE) { vga_puts("[kmod] PC32 S="); kmod_hex((uint64_t)S);
                    vga_puts(" A="); kmod_hex((uint64_t)(int64_t)A);
                    vga_puts(" P="); kmod_hex(P); vga_puts("\n"); }
                *(uint32_t *)paddr = (uint32_t)(int64_t)(S + A - P); break;
            case R_X86_64_PLT32: {
                /* 合成 PLT: jmp *[rip+disp] -> GOT 槽(内容=符号地址) */
                static uint64_t plt_slot_idx = 0;
                uint64_t pslot = plt_base + plt_slot_idx * 16;
                uint64_t gslot = got_base + got_idx * 8;
                plt_slot_idx++;
                got_idx++;
                *(uint64_t *)gslot = (uint64_t)(S + (A + 4));   /* addend -4 恢复 */
                uint8_t *stub = (uint8_t *)pslot;
                stub[0] = 0xFF; stub[1] = 0x25;                 /* jmp qword ptr [rip+disp32] */
                int32_t disp = (int32_t)(int64_t)(gslot - (pslot + 6));
                stub[2] = (uint8_t)disp; stub[3] = (uint8_t)(disp >> 8);
                stub[4] = (uint8_t)(disp >> 16); stub[5] = (uint8_t)(disp >> 24);
                /* call 指向 PLT 槽 (PC32 语义: rel32 = pslot + A - P) */
                if (KMOD_DEBUG_TRACE) { vga_puts("[kmod] PLT pslot="); kmod_hex(pslot);
                    vga_puts(" A="); kmod_hex((uint64_t)(int64_t)A);
                    vga_puts(" P="); kmod_hex(P);
                    vga_puts(" paddr="); kmod_hex(paddr);
                    vga_puts(" write="); kmod_hex((uint64_t)(uint32_t)(int64_t)(pslot + A - P)); vga_puts("\n"); }
                *(uint32_t *)paddr = (uint32_t)(int64_t)(pslot + A - P);
                break;
            }
            case R_X86_64_32:
                *(uint32_t *)paddr = (uint32_t)(S + A); break;
            case R_X86_64_32S:
                *(uint32_t *)paddr = (uint32_t)(int32_t)(S + A); break;
            case R_X86_64_PC64:
                *(uint64_t *)paddr = (uint64_t)(S + A - P); break;
            case R_X86_64_GOTPCRELX: {
                /* 合成 GOT: 槽内容=符号地址, 指令指向槽 */
                uint64_t slot = got_base + got_idx * 8;
                got_idx++;
                *(uint64_t *)slot = (uint64_t)(S + (A + 4));  /* addend -4 恢复 */
                *(uint32_t *)paddr = (uint32_t)(int64_t)(slot - P);  /* GOTPCRELX 是 lea: 无 -4 */
                break;
            }
            default:
                kfree(base); return -3;  /* 不支持的重定位 */
            }
        }
    }

    /* 6. 找 init/exit 入口 */
    int (*init_fn)(void) = 0;
    void (*exit_fn)(void) = 0;
    if (symtab && strtab) {
        for (int i = 0; i < sym_count; i++) {
            kmod_sym_t *s = &symtab[i];
            if (s->st_shndx == 0 || s->st_name == 0) continue;
            const char *n = strtab + s->st_name;
            if (kmod_strcmp(n, "module_init") == 0 && s->st_shndx != 0xFFF1)
                init_fn = (int (*)(void))(base + (s->st_shndx < eh->e_shnum ?
                          sh[s->st_shndx].sh_offset : 0) + s->st_value);
            if (kmod_strcmp(n, "module_exit") == 0 && s->st_shndx != 0xFFF1)
                exit_fn = (void (*)(void))(base + (s->st_shndx < eh->e_shnum ?
                          sh[s->st_shndx].sh_offset : 0) + s->st_value);
        }
    }

    /* 7. 登记 */
    kmod_module_t *m = &g_kmods[slot];
    kmod_memset((void *)m, 0, sizeof(*m));
    m->used = 1;
    int np = 0;
    while (name[np] && np < 47) { m->name[np] = name[np]; np++; }
    m->name[np] = 0;
    m->base = base;
    m->size = alloc;
    m->text_vaddr = (uint64_t)base;
    m->init_fn = init_fn;
    m->exit_fn = exit_fn;
    m->symtab = symtab;
    m->strtab = strtab;
    m->sym_count = sym_count;
    m->got_base = got_base;
    m->got_size = got_size;

    return slot;
}

/* ---------- 调用 init ---------- */
int kmod64_init_module(int slot)
{
    if (slot < 0 || slot >= KMOD_MAX_MODULES || !g_kmods[slot].used) return -1;
    if (!g_kmods[slot].init_fn) return 0;   /* 无 init 也视为成功 */
    return g_kmods[slot].init_fn();
}

/* ---------- 调用 exit 并卸载 ---------- */
int kmod64_unload(int slot)
{
    if (slot < 0 || slot >= KMOD_MAX_MODULES || !g_kmods[slot].used) return -1;
    kmod_module_t *m = &g_kmods[slot];
    if (m->exit_fn) m->exit_fn();
    if (m->base) kfree(m->base);
    m->used = 0;
    return 0;
}

/* ---------- 按名字查询槽位 ---------- */
int kmod64_find(const char *name)
{
    for (int i = 0; i < KMOD_MAX_MODULES; i++) {
        if (g_kmods[i].used && kmod_strcmp(g_kmods[i].name, name) == 0)
            return i;
    }
    return -1;
}

/* ---------- 列表 ---------- */
void kmod64_list(void)
{
    vga_puts("== Kernel Modules ==\n");
    int n = 0;
    for (int i = 0; i < KMOD_MAX_MODULES; i++) {
        if (g_kmods[i].used) {
            vga_puts("  [");
            vga_puts(g_kmods[i].name);
            vga_puts("] init=");
            vga_puts(g_kmods[i].init_fn ? "yes" : "no");
            vga_puts(" size=");
            /* 十进制 */
            uint64_t v = g_kmods[i].size;
            char t[16]; int p = 0;
            while (v && p < 15) { t[p++] = '0' + v % 10; v /= 10; }
            for (int k = 0; k < p / 2; k++) { char c = t[k]; t[k] = t[p-1-k]; t[p-1-k] = c; }
            t[p] = 0;
            vga_puts(t);
            vga_puts(" B\n");
            n++;
        }
    }
    if (n == 0) vga_puts("  (none loaded)\n");
    vga_puts("== end ==\n");
}

int kmod64_get_count(void)
{
    int n = 0;
    for (int i = 0; i < KMOD_MAX_MODULES; i++) if (g_kmods[i].used) n++;
    return n;
}

/* 调试/宿主测试用: 返回模块基址 */
void *kmod64_get_base(int slot)
{
    if (slot < 0 || slot >= KMOD_MAX_MODULES || !g_kmods[slot].used) return 0;
    return g_kmods[slot].base;
}
