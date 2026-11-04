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
uint64_t g_elf_last_code_start = 0;
uint64_t g_elf_last_entry = 0;

/* ============================================================================
 * V0.33: ABI 判定 + 加载偏置
 *
 * 背景：Linux 的静态可执行文件链接在 0x400000（1MB），而 Hepu 的用户窗口是
 * [0x40000000, 0x80000000)（1GB）—— 两者差了三个数量级，Linux 二进制
 * 直接落在窗口外，加载器只能报「code range outside user window」。
 *
 * ET_EXEC（非 PIE）的 Linux 二进制**无法**用偏置搬进窗口：它的绝对地址
 * 已经烧进代码和数据里（链接器在静态链接时就解析完了），搬过去必然跑飞。
 *   → 所以这条路走不通，不做。
 *
 * ET_DYN（PIE）则天生位置无关：所有绝对引用都留在重定位表里，加载器
 * 选一个基址摆进去、把 R_X86_64_RELATIVE 的加数加上基址即可。
 *   → 这条路走得通，V0.33 把它做出来。
 *
 * 判定「这是不是 Linux/GNU 对象」不靠猜链接地址（Linux 静态程序也可能链在
 * 0x40000000），而是靠对象自身的客观标志：PT_INTERP（动态链接器）、
 * .note.ABI-tag（GNU ABI 标记）、ET_DYN、以及链接基址是否落在 Hepu 原生
 * 程序区之外。
 * ========================================================================== */

/* 取链接基址：最小 PT_LOAD 的 p_vaddr */
uint64_t elf64_link_base(void *elf_data, uint64_t size)
{
    if (!elf_data || size < sizeof(elf64_header_t)) return 0;
    elf64_header_t *ehdr = (elf64_header_t*)elf_data;
    if (ehdr->e_phoff > size ||
        (uint64_t)ehdr->e_phnum * sizeof(elf64_phdr_t) > size - ehdr->e_phoff) {
        return 0;
    }
    elf64_phdr_t *phdr = (elf64_phdr_t*)((uint8_t*)elf_data + ehdr->e_phoff);
    uint64_t base = 0xFFFFFFFFFFFFFFFFULL;
    for (int i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type != PT_LOAD) continue;
        if (phdr[i].p_vaddr < base) base = phdr[i].p_vaddr;
    }
    return (base == 0xFFFFFFFFFFFFFFFFULL) ? 0 : base;
}

/* V0.33 SEC-15: 判断 ELF 是否带 PT_INTERP（动态链接器）。
 *
 * HepuOS **不实现** 动态链接器加载：既不加载 ld-linux.so.2，也不解析
 * DT_NEEDED 依赖链。一个带 PT_INTERP 的程序把入口点交给内核后，
 * 内核必须先跑动态链接器才能进入 main —— 那段代码根本不存在。
 *
 * 所以正确做法是**在 exec 阶段明确拒绝**，而不是加载到一半让程序
 * 在没有初始化好的 GOT/PLT 上跑飞。
 *
 * 历史上这里踩过一个很坑的坑：判据不是「ELF 里有没有 PT_INTERP」，
 * 而是「**路径里含不含 dyn 三个字符**」（见 user_process.c 的旧 is_dyn
 * 启发式）—— /bin/dynhello.elf 恰好撞上，于是走了 V0.20.80 的遗留
 * 动态链接路径，算出一个乱七八糟的 load bias，然后 #GP。
 * 判据必须来自 ELF 本身，不能来自文件名。 */
int elf64_has_interp(void *elf_data, uint64_t size)
{
    if (!elf_data || size < sizeof(elf64_header_t)) return 0;
    elf64_header_t *ehdr = (elf64_header_t*)elf_data;
    if (ehdr->e_phoff > size ||
        (uint64_t)ehdr->e_phnum * sizeof(elf64_phdr_t) > size - ehdr->e_phoff) {
        return 0;
    }
    elf64_phdr_t *phdr = (elf64_phdr_t*)((uint8_t*)elf_data + ehdr->e_phoff);
    for (int i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type == PT_INTERP) return 1;
    }
    return 0;
}

/* 扫描 PT_NOTE，找 NT_GNU_ABI_TAG（GNU/Linux ABI 标记）
 * note 格式：namesz(4) descsz(4) type(4) name(namesz,4字节对齐) desc(descsz,4字节对齐) */
static int elf64_has_gnu_abi_tag(void *elf_data, uint64_t size)
{
    if (!elf_data || size < sizeof(elf64_header_t)) return 0;
    elf64_header_t *ehdr = (elf64_header_t*)elf_data;
    if (ehdr->e_phoff > size ||
        (uint64_t)ehdr->e_phnum * sizeof(elf64_phdr_t) > size - ehdr->e_phoff) {
        return 0;
    }
    elf64_phdr_t *phdr = (elf64_phdr_t*)((uint8_t*)elf_data + ehdr->e_phoff);
    for (int i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type != PT_NOTE) continue;
        if (phdr[i].p_offset > size || phdr[i].p_filesz > size - phdr[i].p_offset) continue;
        uint64_t off = 0;
        uint64_t end = phdr[i].p_filesz;
        while (off + 12 <= end) {
            uint8_t *n = (uint8_t*)elf_data + phdr[i].p_offset + off;
            uint32_t namesz = *(uint32_t*)(n);
            uint32_t descsz = *(uint32_t*)(n + 4);
            uint32_t type   = *(uint32_t*)(n + 8);
            uint64_t name_pad = (namesz + 3) & ~3ULL;
            uint64_t desc_pad = (descsz + 3) & ~3ULL;
            if (off + 12 + name_pad + desc_pad > end) break;
            /* NT_GNU_ABI_TAG = 1, name = "GNU\0" */
            if (type == 1 && namesz == 4 && n[12] == 'G' && n[13] == 'N' && n[14] == 'U') {
                return 1;
            }
            off += 12 + name_pad + desc_pad;
        }
    }
    return 0;
}

/* Hepu 原生程序区：原生 ELF 链接在这里 */
#define HEPU_NATIVE_PROG_BASE 0x40000000ULL
#define HEPU_NATIVE_PROG_END  0x50000000ULL

int elf64_is_linux_abi(void *elf_data, uint64_t size)
{
    if (!elf_data || size < sizeof(elf64_header_t)) return 0;
    if (elf64_verify(elf_data) != 0) return 0;
    elf64_header_t *ehdr = (elf64_header_t*)elf_data;

    /* 1) 动态链接器 = 一定是 Linux 风格可执行文件 */
    if (elf64_find_interp(elf_data, size)) return 1;
    /* 2) GNU/Linux ABI 标记 */
    if (elf64_has_gnu_abi_tag(elf_data, size)) return 1;
    /* 3) PIE：需要加载偏置才能落进用户窗口，走兼容路径 */
    if (ehdr->e_type == ET_DYN) return 1;
    /* 4) 链接基址在原生程序区之外 */
    uint64_t base = elf64_link_base(elf_data, size);
    if (base && (base < HEPU_NATIVE_PROG_BASE || base >= HEPU_NATIVE_PROG_END)) return 1;
    return 0;
}


/* V0.20.34: 查找 PT_INTERP 路径, 返回静态字符串指针 */
const char *elf64_find_interp(void *elf_data, uint64_t size)
{
    if (!elf_data || size < sizeof(elf64_header_t)) return NULL;
    elf64_header_t *ehdr = (elf64_header_t*)elf_data;
    if (ehdr->e_phoff > size || ehdr->e_phnum == 0) return NULL;
    elf64_phdr_t *phdr = (elf64_phdr_t*)((uint8_t*)elf_data + ehdr->e_phoff);
    for (int i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type == 3 /*PT_INTERP*/) {
            if (phdr[i].p_offset + phdr[i].p_filesz > size) return NULL;
            return (const char*)((uint8_t*)elf_data + phdr[i].p_offset);
        }
    }
    return NULL;
}

/* V0.20.35: 遍历 PT_DYNAMIC 找 DT_NEEDED, 调用回调 */
void elf64_enum_needed(void *mod_base, void (*cb)(const char *soname, void *arg), void *arg)
{
    if (!mod_base || !cb) return;
    elf64_header_t *ehdr = (elf64_header_t*)mod_base;
    if (ehdr->e_phoff > 0x100000) return;
    elf64_phdr_t *phdr = (elf64_phdr_t*)((uint8_t*)mod_base + ehdr->e_phoff);
    for (int i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type != 2 /*PT_DYNAMIC*/) continue;
        elf64_dyn_t *dyn = (elf64_dyn_t*)((uint8_t*)mod_base + phdr[i].p_vaddr);
        char *strtab = NULL;
        /* 先找 strtab */
        for (int j = 0; j < (int)(phdr[i].p_memsz / sizeof(elf64_dyn_t)); j++) {
            if (dyn[j].d_tag == 0) break;
            if (dyn[j].d_tag == 5 /*DT_STRTAB*/) strtab = (char*)((uint8_t*)mod_base + dyn[j].d_un);
        }
        if (!strtab) break;
        /* 再找 NEEDED */
        for (int j = 0; j < (int)(phdr[i].p_memsz / sizeof(elf64_dyn_t)); j++) {
            if (dyn[j].d_tag == 0) break;
            if (dyn[j].d_tag == 1 /*DT_NEEDED*/) {
                cb(strtab + dyn[j].d_un, arg);
            }
        }
        break;
    }
}

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

/* 加载限制（安全修复） */
#define ELF64_MAX_PHNUM    128
#define ELF64_MAX_LOADSZ   (16ULL * 1024 * 1024)
#define ELF64_USER_CANON   0x0000800000000000ULL   /* 规范地址下半部上限 */

/* V0.33: 段扫描（加载与「问大小」共用一套算法，避免两处逻辑漂移）
 * 输出：code_start（镜像原点）、total_size（镜像大小）、max_end
 * 返回 0=成功，-1=非法
 */
static int elf64_scan_segments(void *elf_data, uint64_t size,
                                uint64_t *code_start_out, uint64_t *total_size_out)
{
    elf64_header_t *ehdr = (elf64_header_t*)elf_data;
    uint64_t min_vaddr = 0xFFFFFFFFFFFFFFFF;
    uint64_t max_end = 0;
    uint64_t code_start = 0;
    int have_entry_seg = 0;
    elf64_phdr_t *phdr = (elf64_phdr_t*)((uint8_t*)elf_data + ehdr->e_phoff);

    for (int i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type != PT_LOAD) continue;
        uint64_t seg_end;
        if (phdr[i].p_filesz > phdr[i].p_memsz ||
            phdr[i].p_memsz > ELF64_MAX_LOADSZ ||
            phdr[i].p_offset > size ||
            phdr[i].p_filesz > size - phdr[i].p_offset ||
            __builtin_add_overflow(phdr[i].p_vaddr, phdr[i].p_memsz, &seg_end) ||
            seg_end >= ELF64_USER_CANON) {
            return -1;
        }
        uint64_t start = phdr[i].p_vaddr & ~0xFFFULL;
        uint64_t end = (seg_end + 0xFFF) & ~0xFFFULL;
        if (start < min_vaddr) min_vaddr = start;
        if (end > max_end) max_end = end;
        if (!have_entry_seg && phdr[i].p_vaddr <= ehdr->e_entry &&
            ehdr->e_entry < seg_end) {
            code_start = start;
            have_entry_seg = 1;
        }
    }
    if (max_end == 0 || min_vaddr == 0xFFFFFFFFFFFFFFFF) return -1;
    if (!have_entry_seg) return -1;
    /* 基准段之下若有紧邻的段（PIE 的 ELF 头段），把基准下移到最低段 */
    if (min_vaddr < code_start && (code_start - min_vaddr) <= 0x10000ULL) {
        code_start = min_vaddr;
    }
    uint64_t total_size = max_end - code_start;
    if (total_size == 0 || total_size > ELF64_MAX_LOADSZ) return -1;
    *code_start_out = code_start;
    *total_size_out = total_size;
    return 0;
}

/* 取 ELF 类型（ET_EXEC / ET_DYN） */
uint16_t elf64_get_type(void *elf_data)
{
    if (!elf_data) return 0;
    elf64_header_t *ehdr = (elf64_header_t*)elf_data;
    if (ehdr->e_magic != ELF_MAGIC) return 0;
    return ehdr->e_type;
}

/* 取镜像大小（加载前就能知道要占多大 VA） */
uint64_t elf64_image_size(void *elf_data, uint64_t size)
{
    if (!elf_data || size < sizeof(elf64_header_t)) return 0;
    if (elf64_verify(elf_data) != 0) return 0;
    elf64_header_t *ehdr = (elf64_header_t*)elf_data;
    if (ehdr->e_phentsize != sizeof(elf64_phdr_t) || ehdr->e_phnum == 0 ||
        ehdr->e_phnum > ELF64_MAX_PHNUM) return 0;
    if (ehdr->e_phoff > size ||
        (uint64_t)ehdr->e_phnum * sizeof(elf64_phdr_t) > size - ehdr->e_phoff) return 0;
    uint64_t cs = 0, ts = 0;
    if (elf64_scan_segments(elf_data, size, &cs, &ts) != 0) return 0;
    return ts;
}

/* V0.33: ELF64 加载（支持加载偏置）
 * map_base == 0 → 按链接地址原样映射（原生程序）
 * map_base != 0 → 镜像摆到 map_base（ET_DYN/PIE 用），入口点与
 *                 R_X86_64_RELATIVE 重定位按同一偏置修正。
 *
 * 镜像模型（统一、无歧义）：
 *   内核缓冲 load_base 持有「镜像原点在 code_start 处」的那段字节；
 *   镜像里虚拟地址 V 的字节，在用户空间被映射到 map_base + (V - code_start)。
 *   因此 入口点 = map_base + (e_entry - code_start)
 *          RELATIVE 值 = map_base + (addend - code_start)
 * map_base == 0 时 map_base := code_start，上述式子退化成原样地址。
 */
void *elf64_load_biased(void *elf_data, uint64_t size, uint64_t map_base)
{
    if (!elf64_initialized) elf64_init();

    if (!elf_data || size < sizeof(elf64_header_t)) {
        vga_puts("[ELF64] ERROR: file too small\n");
        return NULL;
    }
    if (elf64_verify(elf_data) != 0) return NULL;

    elf64_header_t *ehdr = (elf64_header_t*)elf_data;

    if (ehdr->e_phentsize != sizeof(elf64_phdr_t) ||
        ehdr->e_phnum == 0 || ehdr->e_phnum > ELF64_MAX_PHNUM) {
        vga_puts("[ELF64] ERROR: Bad program header table\n");
        return NULL;
    }
    if (ehdr->e_phoff > size ||
        (uint64_t)ehdr->e_phnum * sizeof(elf64_phdr_t) > size - ehdr->e_phoff) {
        vga_puts("[ELF64] ERROR: Program headers out of file\n");
        return NULL;
    }
    if (ehdr->e_entry >= ELF64_USER_CANON) {
        vga_puts("[ELF64] ERROR: Entry point not in user address range\n");
        return NULL;
    }

    vga_puts("[ELF64] Loading ELF64: entry=");
    vga_put_hex(ehdr->e_entry);
    vga_puts(", phdr=");
    vga_put_hex(ehdr->e_phnum);
    vga_puts("\n");

    /* 段扫描：算出镜像原点与大小（与 elf64_image_size 共用同一套算法） */
    uint64_t code_start = 0, total_size = 0;
    if (elf64_scan_segments(elf_data, size, &code_start, &total_size) != 0) {
        vga_puts("[ELF64] ERROR: Bad segment layout\n");
        return NULL;
    }
    elf64_phdr_t *phdr = (elf64_phdr_t*)((uint8_t*)elf_data + ehdr->e_phoff);

    /* 映射基址：偏置加载时用 map_base，否则用链接基址 */
    uint64_t map_at = map_base ? map_base : code_start;
    uint64_t bias = map_at - code_start;   /* 虚拟地址修正量 */

    vga_puts("[ELF64] Total memory needed: ");
    vga_put_hex(total_size);
    vga_puts(" bytes (code_start=");
    vga_put_hex(code_start);
    vga_puts(", map_at=");
    vga_put_hex(map_at);
    vga_puts(")\n");

    /* 分配内存 */
    void *load_base = kmalloc(total_size);
    if (!load_base) {
        vga_puts("[ELF64] ERROR: Memory allocation failed\n");
        return NULL;
    }
    elf64_memset(load_base, 0, total_size);

    /* 加载所有PT_LOAD段（用相对于code_start的偏移, 跳过低于基准的段） */
    for (int i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type != PT_LOAD) continue;
        if (phdr[i].p_vaddr < code_start) continue;  /* 头部段等, 用户态不需要 */

        vga_puts("[ELF64] Segment ");
        vga_put_hex(i);
        vga_puts(": vaddr=");
        vga_put_hex(phdr[i].p_vaddr);
        vga_puts(", filesz=");
        vga_put_hex(phdr[i].p_filesz);
        vga_puts(", memsz=");
        vga_put_hex(phdr[i].p_memsz);
        vga_puts("\n");

        /* 复制文件内容到虚拟地址（相对于code_start） */
        if (phdr[i].p_filesz > 0) {
            uint64_t rel = phdr[i].p_vaddr - code_start;
            /* 无溢出写法；p_filesz<=p_memsz 且已在上面校验，此处作纵深防御 */
            if (rel > total_size || phdr[i].p_filesz > total_size - rel) {
                vga_puts("[ELF64] ERROR: Segment exceeds load area\n");
                kfree(load_base);
                return NULL;
            }
            elf64_memcpy((uint8_t*)load_base + rel,
                        (uint8_t*)elf_data + phdr[i].p_offset,
                        phdr[i].p_filesz);
        }
        /* p_memsz > p_filesz的部分（BSS）已经被memset清零 */
    }

    /* V0.33: 应用 R_X86_64_RELATIVE 重定位（PIE 必需）
     *
     * ET_DYN 的绝对指针全部留在重定位表里：值 = 映射基址 + (addend - code_start)。
     * 无偏置时（原生 ET_EXEC）这个式子等于 addend 本身，即「什么都不改」，
     * 所以对原生程序零影响。
     *
     * 重定位表本身在文件里按 PT_DYNAMIC 的 DT_RELA/DT_RELASZ 定位；
     * 写入目标是镜像缓冲里的 GOT 项（镜像缓冲随后被复制进用户页）。
     */
    {
        /* 找 PT_DYNAMIC，读 DT_RELA / DT_RELASZ */
        uint64_t dyn_rela = 0, dyn_relasz = 0;
        for (int i = 0; i < ehdr->e_phnum; i++) {
            if (phdr[i].p_type != PT_DYNAMIC) continue;
            if (phdr[i].p_offset > size || phdr[i].p_filesz > size - phdr[i].p_offset) break;
            for (uint64_t d = 0; d + 16 <= phdr[i].p_filesz; d += 16) {
                uint8_t *de = (uint8_t*)elf_data + phdr[i].p_offset + d;
                uint64_t tag = *(uint64_t*)de;
                uint64_t val = *(uint64_t*)(de + 8);
                if (tag == 0) break;
                if (tag == 7) dyn_rela = val;      /* DT_RELA   = .rela.dyn 的地址 */
                if (tag == 8) dyn_relasz = val;    /* DT_RELASZ = .rela.dyn 的字节数 */
                /* 注意：DT_RELAENT(9) 是**每项大小**(24)，不是地址 ——
                 * 早期版本在这里把 dyn_rela 覆盖成了 24，导致重定位表读飞，
                 * 1305 条 RELATIVE 只应用了 2 条，PIE 程序因此写飞。 */
            }
            break;
        }
        /* V0.54.6（P1-7）：所有 ET_DYN 的重定位都交给镜像自己/解释器完成。
         * - static-PIE：glibc 的 _dl_relocate_static_pie 自举重定位；
         * - 动态链接：ld.so 解析 PT_DYNAMIC 后统一重定位（含主程序 RELATIVE）。
         * 内核如果再应用一遍 RELATIVE，会把基址加两次。 */
        int self_reloc = (ehdr->e_type == ET_DYN);
        if (dyn_rela && dyn_relasz && !self_reloc) {
            /* vaddr → 文件偏移：落在哪个 PT_LOAD 段里就用哪段换算 */
            uint64_t rela_file_off = 0;
            int rela_found = 0;
            for (int i = 0; i < ehdr->e_phnum; i++) {
                if (phdr[i].p_type != PT_LOAD) continue;
                uint64_t seg_end = phdr[i].p_vaddr + phdr[i].p_memsz;
                if (dyn_rela >= phdr[i].p_vaddr && dyn_rela < seg_end) {
                    rela_file_off = phdr[i].p_offset + (dyn_rela - phdr[i].p_vaddr);
                    rela_found = 1;
                    break;
                }
            }
            if (rela_found && rela_file_off <= size && dyn_relasz <= size - rela_file_off) {
                uint64_t count = dyn_relasz / 24;
                uint64_t applied = 0;
                for (uint64_t i = 0; i < count; i++) {
                    uint8_t *rela = (uint8_t*)elf_data + rela_file_off + i * 24;
                    uint64_t r_offset = *(uint64_t*)(rela);
                    uint64_t r_info   = *(uint64_t*)(rela + 8);
                    int64_t  r_addend = *(int64_t*)(rela + 16);
                    uint32_t type = (uint32_t)(r_info & 0xFFFFFFFF);
                    if (type != 8) continue;             /* R_X86_64_RELATIVE */
                    if (r_offset < code_start) continue; /* 不在镜像内 */
                    uint64_t off = r_offset - code_start;
                    if (off + 8 > total_size) continue;   /* 越界，丢弃 */
                    uint64_t *loc = (uint64_t*)((uint8_t*)load_base + off);
                    *loc = map_at + (uint64_t)r_addend - code_start;
                    applied++;
                }
                if (applied) {
                    vga_puts("[ELF64] RELATIVE relocs applied: ");
                    vga_put_hex(applied);
                    vga_puts("\n");
                }
            }
        }
    }

    /* 登记已加载模块（只记录基址/大小；不保留指向调用方缓冲区的指针） */
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
        mod->ehdr = NULL;
        mod->symtab = NULL;
        mod->strtab = NULL;
        mod->strtab_size = 0;
    }

    vga_puts("[ELF64] ELF loaded successfully\n");
    g_elf_last_load_size = total_size;
    g_elf_last_code_start = map_at;                                   /* 映射基址 */
    g_elf_last_entry = map_at + (ehdr->e_entry - code_start);          /* 修正后的入口点 */
    (void)bias;
    return load_base;
}

/* 无偏置加载（保持旧接口语义） */
void *elf64_load(void *elf_data, uint64_t size)
{
    return elf64_load_biased(elf_data, size, 0);
}

/* 加载共享库 */
int elf64_load_library(const char *name, void *data, uint64_t size)
{
    if (!name || !data || size < sizeof(elf64_header_t)) return -1;
    if (elf64_verify(data) != 0) return -1;

    for (int i = 0; i < ELF64_MAX_LOADED; i++) {
        if (!loaded_modules[i].used) {
            elf64_loaded_t *mod = &loaded_modules[i];
            elf64_memset(mod, 0, sizeof(elf64_loaded_t));
            mod->used = 1;
            {   /* 安全修复：有界拷贝（旧版 strcpy 可溢出 mod->name） */
                uint64_t k = 0;
                while (name[k] && k < sizeof(mod->name) - 1) { mod->name[k] = name[k]; k++; }
                mod->name[k] = 0;
            }
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
        if (!mod->ehdr) continue;

        /* V0.20.33: 支持无 section header 的模块 (用 PT_DYNAMIC 设置的 symtab) */
        int sym_count = 0;
        if (mod->ehdr->e_shoff == 0 || mod->ehdr->e_shnum == 0) {
            /* 无 section header: 最多扫 2048 个符号 */
            sym_count = 2048;
        } else {
            if (mod->ehdr->e_shoff > mod->size ||
                (uint64_t)mod->ehdr->e_shnum * sizeof(elf64_shdr_t) > mod->size - mod->ehdr->e_shoff)
                continue;
            elf64_shdr_t *shdr = (elf64_shdr_t*)((uint8_t*)mod->ehdr + mod->ehdr->e_shoff);
            for (int j = 0; j < mod->ehdr->e_shnum; j++) {
                if (shdr[j].sh_type == SHT_SYMTAB || shdr[j].sh_type == SHT_DYNSYM) {
                    sym_count = shdr[j].sh_size / sizeof(elf64_sym_t);
                    break;
                }
            }
        }
        if (sym_count <= 0) continue;

        for (int j = 0; j < sym_count; j++) {
            elf64_sym_t *sym = &mod->symtab[j];
            if (sym->st_name == 0) continue;
            /* V0.20.33: strtab_size 可能没设, 用基址+大小检查 */
            if (mod->strtab_size && sym->st_name >= mod->strtab_size) continue;

            const char *sym_name = mod->strtab + sym->st_name;
            if (elf64_strcmp(sym_name, name) == 0) {
                if (sym->st_value == 0) continue;
                return (void*)((uint64_t)mod->base + sym->st_value);
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

    /* V0.20.14: 优先用 PT_DYNAMIC（.so stripped 后没有 section header） */
    uint64_t dyn_rela = 0, dyn_relasz = 0;
    uint64_t dyn_symtab = 0, dyn_strtab = 0;
    uint64_t dyn_jmprel = 0, dyn_pltrelsz = 0;
    uint64_t dyn_syment = 24;

    elf64_phdr_t *phdr = (elf64_phdr_t*)((uint8_t*)mod->ehdr + ehdr->e_phoff);
    for (int i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type != 2 /*PT_DYNAMIC*/) continue;
        elf64_dyn_t *dyn = (elf64_dyn_t*)((uint8_t*)mod->base + phdr[i].p_vaddr);
        for (int j = 0; j < (int)(phdr[i].p_memsz / sizeof(elf64_dyn_t)); j++) {
            uint64_t tag = dyn[j].d_tag;
            uint64_t val = dyn[j].d_un;
            if (tag == 0) break;
            switch (tag) {
                case 7: dyn_rela = val; break;
                case 8: dyn_relasz = val; break;
                case 6: dyn_symtab = val; break;
                case 5: dyn_strtab = val; break;
                case 10: dyn_syment = val; break;
                case 23: dyn_jmprel = val; break;
                case 2: dyn_pltrelsz = val; break;
            }
        }
        break;
    }

    /* V0.20.33: 设置 mod 的 symtab/strtab 供跨模块查找 */
    if (dyn_symtab && dyn_strtab) {
        mod->symtab = (elf64_sym_t*)((uint8_t*)mod->base + dyn_symtab);
        mod->strtab = (char*)((uint8_t*)mod->base + dyn_strtab);
    }

    if (ehdr->e_shoff == 0 || ehdr->e_shnum == 0) {
        /* 没有 section header: 用 PT_DYNAMIC 的信息 */
        /* 辅助: 从符号表取符号名 */
        #define ELF64_SYM_NAME(sym, strtab) ((strtab) ? (strtab + (sym)->st_name) : "?")

        if (dyn_rela && dyn_relasz) {
            elf64_rela_t *rela = (elf64_rela_t*)((uint8_t*)mod->base + dyn_rela);
            int count = dyn_relasz / sizeof(elf64_rela_t);
            for (int j = 0; j < count; j++) {
                if (mod->size < 8 || rela[j].r_offset > mod->size - 8) continue;
                uint32_t type = ELF64_R_TYPE(rela[j].r_info);
                uint32_t sym_idx = ELF64_R_SYM(rela[j].r_info);
                uint64_t *loc = (uint64_t*)((uint8_t*)mod->base + rela[j].r_offset);
                if (type == 8 /*R_X86_64_RELATIVE*/) {
                    *loc = (uint64_t)mod->base + rela[j].r_addend;
                } else if ((type == 6 /*GLOB_DAT*/ || type == 7 /*JUMP_SLOT*/) && sym_idx > 0 && mod->symtab) {
                    /* V0.20.33: 符号查找 */
                    elf64_sym_t *sym = &mod->symtab[sym_idx];
                    const char *name = ELF64_SYM_NAME(sym, mod->strtab);
                    void *addr = NULL;
                    /* 先在本模块找 */
                    if (sym->st_value) {
                        addr = (void*)((uint64_t)mod->base + sym->st_value);
                    } else {
                        /* 跨模块查找 */
                        addr = elf64_lookup_symbol(name);
                    }
                    if (addr) {
                        *loc = (uint64_t)addr + (rela[j].r_addend);
                    } else {
                        /* 找不到就填本模块基址+addend */
                        *loc = (uint64_t)mod->base + rela[j].r_addend;
                    }
                } else if (type == 1 /*R_X86_64_64*/ && sym_idx > 0 && mod->symtab) {
                    elf64_sym_t *sym = &mod->symtab[sym_idx];
                    const char *name = ELF64_SYM_NAME(sym, mod->strtab);
                    void *addr = sym->st_value ? (void*)((uint64_t)mod->base + sym->st_value) : elf64_lookup_symbol(name);
                    *loc = (uint64_t)(addr ? addr : (void*)((uint64_t)mod->base + sym->st_value)) + rela[j].r_addend;
                }
            }
        }
        /* V0.20.24: 也处理 JMPREL (PLT 跳转槽) */
        if (dyn_jmprel && dyn_pltrelsz) {
            elf64_rela_t *jrel = (elf64_rela_t*)((uint8_t*)mod->base + dyn_jmprel);
            int jcount = dyn_pltrelsz / sizeof(elf64_rela_t);
            for (int j = 0; j < jcount; j++) {
                if (mod->size < 8 || jrel[j].r_offset > mod->size - 8) continue;
                uint64_t *loc = (uint64_t*)((uint8_t*)mod->base + jrel[j].r_offset);
                uint32_t sym_idx = ELF64_R_SYM(jrel[j].r_info);
                uint32_t type = ELF64_R_TYPE(jrel[j].r_info);
                if (sym_idx > 0 && mod->symtab) {
                    elf64_sym_t *sym = &mod->symtab[sym_idx];
                    const char *name = ELF64_SYM_NAME(sym, mod->strtab);
                    void *addr = sym->st_value ? (void*)((uint64_t)mod->base + sym->st_value) : elf64_lookup_symbol(name);
                    *loc = (uint64_t)(addr ? addr : (void*)((uint64_t)mod->base)) + jrel[j].r_addend;
                } else {
                    *loc = (uint64_t)mod->base + jrel[j].r_addend;
                }
            }
        }
        return 0;
    }

    /* 安全修复：节头表必须落在模块文件范围内 */
    if (ehdr->e_shoff > mod->size ||
        (uint64_t)ehdr->e_shnum * sizeof(elf64_shdr_t) > mod->size - ehdr->e_shoff) return -1;
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
        if (shdr[i].sh_offset > mod->size || shdr[i].sh_size > mod->size - shdr[i].sh_offset) continue;

        elf64_rela_t *rela = (elf64_rela_t*)((uint8_t*)mod->ehdr + shdr[i].sh_offset);
        int count = shdr[i].sh_size / sizeof(elf64_rela_t);

        for (int j = 0; j < count; j++) {
            /* 安全修复：写入8字节，需保证 r_offset+8 不越界（旧版仅判断 >= size） */
            if (mod->size < 8 || rela[j].r_offset > mod->size - 8) continue;

            uint32_t type = ELF64_R_TYPE(rela[j].r_info);
            uint32_t sym_idx = ELF64_R_SYM(rela[j].r_info);
            uint64_t *loc = (uint64_t*)((uint8_t*)mod->base + rela[j].r_offset);

            if (type == R_X86_64_RELATIVE) {
                /* 相对重定位：base + addend */
                *loc = (uint64_t)mod->base + rela[j].r_addend;
                rel_count++;
            }
            else if (type == R_X86_64_64 || type == R_X86_64_GLOB_DAT ||
                     type == R_X86_64_JUMP_SLOT) {
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
