/* ============================================================================
 * 赫普OS 64位 - 用户空间访问安全（架构相关实现）
 * 接口定义见 kernel/shared/security/user_access.h
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include "../../shared/security/user_access.h"
#include "hepu_smap.h"

extern void vga_puts(const char *s);

/* 64位用户空间上限（规范地址下半部） */
#define USER_SPACE_LIMIT 0x00007FFFFFFFFFFFULL
#define KERNEL_SPACE_BASE 0xFFFF800000000000ULL

/* 安全修复：严格用户地址窗口。
 * 旧实现只要求地址 <= 0x7FFF_FFFF_FFFF，而内核本身（0x100000 起）和整个物理内存恒等映射
 * 都落在这个"用户范围"里，所以 read/write/open 等系统调用可以被用来读写任意内核内存。
 * 实际用户地址空间是 user.ld 链接的 0x40000000 起、栈顶 0x70000000、brk 上限 0x60000000，
 * 动态库在 0x40000000+n*0x04000000，全部落在 [0x40000000, 0x80000000) 内。
 * 如需放宽（例如内核自测在 ring0 里用内核指针调用系统调用），编译时 -DHEPU_STRICT_USER_RANGE=0 */
#ifndef HEPU_STRICT_USER_RANGE
#define HEPU_STRICT_USER_RANGE 1
#endif
#define HEPU_USER_WIN_START 0x0000000040000000ULL
#define HEPU_USER_WIN_END   0x0000000080000000ULL   /* 不含 */

/* 共享变量：用户空间上限 */
uintptr_t hepu_user_space_limit = USER_SPACE_LIMIT;

/* 检查地址是否在用户空间（共享接口实现） */
int hepu_is_user_address(uintptr_t addr, size_t size)
{
    if (addr == 0) return 0;
    if (addr + size < addr) return 0;  /* 溢出 */
#if HEPU_STRICT_USER_RANGE
    if (addr < HEPU_USER_WIN_START) return 0;
    if (addr + size > HEPU_USER_WIN_END) return 0;
#else
    if (addr > USER_SPACE_LIMIT) return 0;
    if (addr + size > USER_SPACE_LIMIT + 1) return 0;
#endif
    return 1;
}

/* ---------------------------------------------------------------------------
 * 安全修复：拷贝前检查目标页确实已映射（走当前 CR3 的 4 级页表）。
 * 旧实现只检查地址范围，系统调用传入"范围内但未映射"的地址会在内核态触发缺页，
 * 缺页处理器对非换出页直接 hlt，整机死机（任意用户进程可触发）。
 * 要求：各级 P=1；用户态可访问（各级 U/S=1）；写入时各级 R/W=1。
 * 支持 1GB / 2MB / 4KB 页。
 * ------------------------------------------------------------------------- */
#define PTE_P    0x001ULL
#define PTE_W    0x002ULL
#define PTE_U    0x004ULL
#define PTE_PS   0x080ULL
#define PTE_ADDR 0x000FFFFFFFFFF000ULL

#ifdef HEPU_UACCESS_HOST_TEST
extern uint64_t hepu_test_cr3;
extern uint64_t *hepu_test_phys(uint64_t phys);
#define UA_READ_CR3()  (hepu_test_cr3)
#define UA_PHYS_PTR(p) (hepu_test_phys(p))
#else
static inline uint64_t ua_read_cr3(void)
{
    uint64_t v;
    __asm__ volatile("mov %%cr3, %0" : "=r"(v));
    return v;
}
#define UA_READ_CR3()  (ua_read_cr3())
/* 内核对物理内存是恒等映射，页表可直接按物理地址访问 */
#define UA_PHYS_PTR(p) ((uint64_t *)(uintptr_t)(p))
#endif

static int ua_page_ok(uint64_t addr, int write)
{
    uint64_t *tbl = UA_PHYS_PTR(UA_READ_CR3() & PTE_ADDR);
    for (int lvl = 0; lvl < 4; lvl++) {
        uint64_t e = tbl[(addr >> (39 - 9 * lvl)) & 0x1FF];
        if (!(e & PTE_P)) return 0;
        if (!(e & PTE_U)) return 0;
        if (write && !(e & PTE_W)) return 0;
        if (lvl == 3) return 1;                                  /* 4KB 页 */
        if (lvl >= 1 && (e & PTE_PS)) return 1;                  /* 1GB(PDPT) / 2MB(PD) 大页 */
        tbl = UA_PHYS_PTR(e & PTE_ADDR);
    }
    return 0;
}

/* 范围内每一页都必须可访问 */
static int ua_range_mapped(uint64_t addr, size_t size, int write)
{
    if (size == 0) return 1;
    uint64_t last = addr + size - 1;
    for (uint64_t p = addr & ~0xFFFULL; ; p += 4096) {
        if (!ua_page_ok(p, write)) return 0;
        if (p >= (last & ~0xFFFULL)) break;
    }
    return 1;
}

/* 检查地址是否在用户空间（内部兼容） */
static int is_user_address(uint64_t addr, size_t size)
{
    return hepu_is_user_address(addr, size);
}

/* 检查地址是否在用户空间且已映射（内部使用） */
static int user_range_ok(uint64_t addr, size_t size, int write)
{
    return hepu_is_user_address(addr, size) && ua_range_mapped(addr, size, write);
}

/* 从用户空间复制到内核空间
 * 返回：0=成功，-1=失败（地址无效）
 */
int copy_from_user(void *kernel_dst, const void *user_src, size_t size)
{
    if (!kernel_dst || !user_src || size == 0) {
        return -1;
    }

    uint64_t src_addr = (uint64_t)user_src;
    if (!user_range_ok(src_addr, size, 0)) {
        vga_puts("[SECURITY] copy_from_user: invalid user address 0x");
        /* 不打印完整地址，避免信息泄露 */
        vga_puts("...\n");
        return -1;
    }

    /* 安全复制。ring0 访问 ring3 页在 SMAP 开启时需先 STAC（置 AC），用完即 CLAC。
     * 关键：STAC 只在 CR4.SMAP 真正置位时才可执行，否则 #UD（SMAP 是 x86-64
     * 可选特性）。是否执行由硬件真实状态决定，见 hepu_smap.h。 */
    uint8_t *dst = (uint8_t*)kernel_dst;
    const uint8_t *src = (const uint8_t*)user_src;
    int ac_prev = hepu_user_access_enter();
    for (size_t i = 0; i < size; i++) {
        dst[i] = src[i];
    }
    hepu_user_access_exit(ac_prev);

    return 0;
}

/* 从内核空间复制到用户空间
 * 返回：0=成功，-1=失败（地址无效）
 */
int copy_to_user(void *user_dst, const void *kernel_src, size_t size)
{
    if (!user_dst || !kernel_src || size == 0) {
        return -1;
    }

    uint64_t dst_addr = (uint64_t)user_dst;
    if (!user_range_ok(dst_addr, size, 1)) {
        vga_puts("[SECURITY] copy_to_user: invalid user address\n");
        return -1;
    }

    /* 安全复制。SMAP 开启时 ring0 写用户页需 STAC（同样只在 CR4.SMAP 置位时才合法）。 */
    uint8_t *dst = (uint8_t*)user_dst;
    const uint8_t *src = (const uint8_t*)kernel_src;
    int ac_prev = hepu_user_access_enter();
    for (size_t i = 0; i < size; i++) {
        dst[i] = src[i];
    }
    hepu_user_access_exit(ac_prev);

    return 0;
}

/* 从用户空间复制字符串（带长度限制）
 * 返回：复制的字节数（不含终止符），-1=失败
 */
int copy_str_from_user(char *kernel_dst, const char *user_src, size_t max_size)
{
    if (!kernel_dst || !user_src || max_size == 0) {
        return -1;
    }

    uint64_t src_addr = (uint64_t)user_src;
    if (!is_user_address(src_addr, 1)) {
        return -1;
    }

    size_t i = 0;
    int ac_prev = hepu_user_access_enter();
    while (i < max_size - 1) {
        /* 逐字节检查地址有效性；每进入一个新页检查一次是否已映射 */
        if (!is_user_address(src_addr + i, 1) ||
            ((((src_addr + i) & 0xFFF) == 0 || i == 0) && !ua_page_ok((src_addr + i) & ~0xFFFULL, 0))) {
            kernel_dst[i] = 0;
            hepu_user_access_exit(ac_prev);
            return -1;
        }
        kernel_dst[i] = user_src[i];
        if (kernel_dst[i] == 0) break;
        i++;
    }
    hepu_user_access_exit(ac_prev);
    kernel_dst[i] = 0;
    return (int)i;
}

/* 获取用户字符串长度（带安全检查）
 * 返回：字符串长度，-1=失败
 */
int user_strlen(const char *user_str, size_t max_size)
{
    if (!user_str || max_size == 0) return -1;

    uint64_t addr = (uint64_t)user_str;
    size_t len = 0;

    while (len < max_size) {
        if (!is_user_address(addr + len, 1) ||
            ((((addr + len) & 0xFFF) == 0 || len == 0) && !ua_page_ok((addr + len) & ~0xFFFULL, 0))) {
            return -1;
        }
        if (user_str[len] == 0) break;
        len++;
    }

    if (len >= max_size) return -1;  /* 字符串太长 */
    return (int)len;
}

/* 验证用户缓冲区是否可读 */
int verify_user_read(const void *user_ptr, size_t size)
{
    if (!user_ptr || size == 0) return 0;
    return user_range_ok((uint64_t)user_ptr, size, 0);
}

/* 验证用户缓冲区是否可写 */
int verify_user_write(void *user_ptr, size_t size)
{
    if (!user_ptr || size == 0) return 0;
    return user_range_ok((uint64_t)user_ptr, size, 1);
}

/* 初始化用户空间访问安全模块 */
void user_access64_init(void)
{
    vga_puts("[USERACC] User space access protection initialized\n");
#if HEPU_STRICT_USER_RANGE
    vga_puts("[USERACC] User window: 0x40000000-0x7FFFFFFF (strict, pages must be mapped)\n");
#else
    vga_puts("[USERACC] User space limit: 0x00007FFFFFFFFFFF (strict range disabled)\n");
#endif
}
