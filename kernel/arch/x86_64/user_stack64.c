/* ============================================================================
 * 赫普OS 64位 - 进程初始栈构建（System V ABI / Linux 兼容）
 *
 * V0.33 新增。
 *
 * 为什么要做：Linux 程序的入口不是「直接跳过去就能跑」，它按 System V ABI
 * 约定从栈上读初始状态：
 *
 *     [rsp]        argc
 *     [rsp+8]      argv[0] ... argv[argc-1]
 *     [..]         NULL
 *     [..]         envp[0] ... NULL
 *     [..]         auxv: (type, value) 对 ... AT_NULL 结尾
 *     [..]         字符串区（argv/envp 的字符串、AT_RANDOM 的 16 字节、平台串）
 *
 * glibc 启动代码（_start → __libc_start_main）靠这些信息找程序头（AT_PHDR，
 * 用于定位 TLS 段）、入口（AT_ENTRY）、栈保护随机数（AT_RANDOM）。
 * 没有这套初始栈，Linux 程序一进去就崩（读 argv[0] 直接 #PF）。
 *
 * Hepu 原生程序（ulib 体系）不使用这套约定，它们的栈是空的 —— 保持原样，
 * 不受影响：**契约按 ABI 类型区分，不搞「所有进程都塞一套」的隐式语义**。
 * ========================================================================== */
#include <stdint.h>
#include <stddef.h>

/* auxv 类型号（Linux x86-64） */
#define AT_NULL   0
#define AT_IGNORE 1
#define AT_EXECFD 2
#define AT_PHDR   3
#define AT_PHENT  4
#define AT_PHNUM  5
#define AT_PAGESZ 6
#define AT_BASE   7
#define AT_FLAGS  8
#define AT_ENTRY  9
#define AT_UID    11
#define AT_EUID   12
#define AT_GID    13
#define AT_EGID   14
#define AT_HWCAP  16
#define AT_CLKTCK 17
#define AT_SECURE 23
#define AT_RANDOM 25
#define AT_EXECFN 31

/* V0.33: 栈页数从 4（16KB）提到 256（1MB）。
 * 16KB 是给 Hepu 原生小程序的；glibc 的 static-PIE 启动阶段就要用
 * 几十 KB 栈（__libc_setup_tls / __ctype_init / locale 初始化），
 * 16KB 必然爆栈（实测：写到 0x70000000 即栈顶 → #PF）。
 * 1MB 与 Linux 的默认主线程栈相当。 */
#define HEPU_STACK_PAGES 256
#define HEPU_STACK_SIZE  (HEPU_STACK_PAGES * 4096)

/* 用户栈顶（与 user_process.c 的 USER_STACK_TOP 一致） */
#define HEPU_USER_STACK_TOP 0x70000000ULL

extern void vga_puts(const char *s);
extern uint64_t vmm64_phys_to_virt(uint64_t phys);
extern void vga_put_hex(uint64_t val);

/*
 * 构建初始栈并写入用户栈页。
 * stack_phys[] : 4 个用户栈物理页（stack_phys[0] 是栈顶那页）
 * path          : 程序路径（作为 argv[0]，也作为 AT_EXECFN）
 * entry_va      : 修正后的入口点（AT_ENTRY）
 * phdr_va       : 程序头表在用户空间的虚拟地址（AT_PHDR），0 = 不提供
 * phnum/phent   : 程序头个数 / 每项大小
 * 返回：初始 RSP（0=失败）
 */

/* V0.33: 通过**用户自己的页表**写用户内存。
 *
 * 为什么不直接写物理地址（vmm64_phys_to_virt + 偏移）：
 *   内核的低身份映射（0..1GB，2MB 大页）是内核访问自己物理内存的唯一窗口，
 *   而这个窗口**会被用户态的映射动作破坏**（用户堆、extra page 等都在
 *   0x40000000-0x60000000，会在共享的 PDPT[0] 下拆大页、动到内核的页表树）。
 *   一旦身份映射被改写，「按物理地址写用户栈」就会写到别的物理页上 ——
 *   症状是初始栈写不进去、auxv 里出现别的程序留下的旧值。
 *
 *   走用户页表翻译则永远和 CPU 的访问路径一致：写哪儿，用户就看到哪儿。
 */
static uint8_t *hepu_user_virt_to_ptr(uint64_t user_pml4, uint64_t vaddr)
{
    if (!user_pml4) return 0;
    uint64_t *l4 = (uint64_t *)vmm64_phys_to_virt(user_pml4);
    if (!(l4[(vaddr >> 39) & 0x1FF] & 1)) return 0;
    uint64_t *l3 = (uint64_t *)vmm64_phys_to_virt(l4[(vaddr >> 39) & 0x1FF] & 0x000FFFFFFFFFF000ULL);
    if (!(l3[(vaddr >> 30) & 0x1FF] & 1)) return 0;
    uint64_t *l2 = (uint64_t *)vmm64_phys_to_virt(l3[(vaddr >> 30) & 0x1FF] & 0x000FFFFFFFFFF000ULL);
    if (!(l2[(vaddr >> 21) & 0x1FF] & 1)) return 0;
    uint64_t *l1 = (uint64_t *)vmm64_phys_to_virt(l2[(vaddr >> 21) & 0x1FF] & 0x000FFFFFFFFFF000ULL);
    if (!(l1[(vaddr >> 12) & 0x1FF] & 1)) return 0;
    uint64_t phys = l1[(vaddr >> 12) & 0x1FF] & 0x000FFFFFFFFFF000ULL;
    return (uint8_t *)vmm64_phys_to_virt(phys) + (vaddr & 0xFFF);
}

uint64_t hepu_build_initial_stack(const uint64_t *stack_phys, const char *path,
                                   uint64_t entry_va, uint64_t phdr_va,
                                   uint32_t phnum, uint32_t phent,
                                   uint64_t user_pml4,
                                   uint64_t at_entry_va, uint64_t interp_base)
{
    if (!stack_phys || !stack_phys[0]) return 0;

    /* ---- 1. 在内核缓冲里拼出初始栈内容 ----
     * 布局（从低地址往高）：向量区 → 字符串区。
     * 最终 RSP 指向向量区起点，向量区整体 16 字节对齐。 */
    /* V0.33: 用静态缓冲而不是栈上局部变量 ——
     * 内核栈只有 4KB（SEC-05 已知问题），4KB+512B 的局部数组会把栈冲掉，
     * 写进用户栈的内容直接变成垃圾。exec 路径是单线程的，用静态缓冲安全。 */
    static uint8_t buf[4096];

    /* 字符串区放在向量区后面（高地址），先算好各字符串的虚拟地址。
     * 字符串区基址 = 栈顶 - 字符串总长（向下生长）。 */
    const char *p0 = path ? path : "/bin/hello";
    uint32_t len_p0 = 0;
    while (p0[len_p0]) len_p0++;
    const char *plat = "Hepu";
    uint32_t len_plat = 5;
    uint32_t rand_len = 16;
    /* 动态链接进程默认不带环境变量（LD_DEBUG 等调试开关只在上一步排查时
     * 临时注入，正式内核保持 envp 为空，避免污染正常输出）。 */
    const char *denv = 0;
    uint32_t len_denv = 0;
    if (denv) { while (denv[len_denv]) len_denv++; }

    uint32_t strings_len = (len_p0 + 1) + len_plat + rand_len + (len_denv ? len_denv + 1 : 0);
    /* 16 字节对齐 */
    strings_len = (strings_len + 15) & ~15u;
    uint64_t str_base = HEPU_USER_STACK_TOP - strings_len;

    /* 向量区大小：argc + argv[0] + NULL + envp NULL + auxv 对 + AT_NULL */
    /* auxv 项数：PHDR,PHENT,PHNUM,PAGESZ,BASE,FLAGS,ENTRY,UID,EUID,GID,EGID,
     *             HWCAP,CLKTCK,SECURE,RANDOM,EXECFN,NULL = 17 项 */
    uint64_t auxv_count = 17;
    uint64_t vec_len = 8              /* argc */
                    + 8              /* argv[0] */
                    + 8              /* argv NULL */
                    + (denv ? 16 : 8) /* envp[0] + envp NULL / envp NULL */
                    + auxv_count * 16;
    vec_len = (vec_len + 15) & ~15ULL;

    uint64_t rsp = str_base - vec_len;
    /* V0.33: 进程入口的栈对齐按 SysV x86-64 调用约定给成 RSP ≡ 8 (mod 16)。
     *
     * 依据（实测自 gcc 生成的 _start 反汇编）：
     *   push r14/r13/r12/rbp/rbx + sub $0x10  →  入口后共减少 0x38
     * 之后代码里第一条 movaps %xmm0,(%rsp) 要求 RSP ≡ 0 (mod 16)，
     * 所以入口必须 ≡ 8 (mod 16)，否则 movaps 目标未对齐 → #GP
     * （#GP error code bit0=0 = 对齐检查失败）。
     *
     * 实测对照：
     *   入口 ≡ 0 → movaps 处 rsp=0x6FFFFE68 (≡8) → #GP
     *   入口 ≡ 8 → movaps 处 rsp=0x6FFFFE60 (≡0) → 对齐
     *
     * 先减 8 再对齐，保证向量区往低地址多留 8 字节，不与字符串区重叠。 */
    /* 动态链接（PT_INTERP）时内核直接进入 ld.so 的 _dl_start：
     * Linux 内核给进程的初始 RSP 是 16 字节对齐，glibc 的 ld.so 也按
     * 这个约定在入口处直接 movaps（崩溃点 rip 就是 ld.so 里的
     * `movaps %xmm1,-0x80(%rbp)`）。static-PIE 的 _start 另行按
     * 8 (mod 16) 对齐（见下面注释里的实测记录），两者不能混用。 */
    if (interp_base) {
        rsp &= ~15ULL;                 /* ≡ 0 (mod 16) */
    } else {
        rsp = ((rsp - 8) & ~15ULL) | 8ULL;
    }

    /* 把 buf 看成以 rsp 为基址的内存布局 */
    uint64_t *vec = (uint64_t *)buf;
    uint64_t vi = 0;

    vec[vi++] = 1;                                   /* argc = 1 */
    vec[vi++] = str_base;                            /* argv[0] → 路径字符串 */
    vec[vi++] = 0;                                   /* argv NULL */
    uint64_t envp0_va = str_base + len_p0 + 1 + len_plat + rand_len;
    if (denv) {
        vec[vi++] = envp0_va;                        /* envp[0] → LD_DEBUG=libs */
        vec[vi++] = 0;                               /* envp NULL */
    } else {
        vec[vi++] = 0;                               /* envp NULL（无环境变量） */
    }

    vec[vi++] = AT_PHDR;   vec[vi++] = phdr_va;
    vec[vi++] = AT_PHENT;  vec[vi++] = phent;
    vec[vi++] = AT_PHNUM;  vec[vi++] = phnum;
    vec[vi++] = AT_PAGESZ; vec[vi++] = 4096;
    vec[vi++] = AT_BASE;   vec[vi++] = interp_base;  /* 动态链接器加载基址；0=静态 */
    vec[vi++] = AT_FLAGS;  vec[vi++] = 0;
    vec[vi++] = AT_ENTRY;  vec[vi++] = at_entry_va;  /* 主程序入口（ld.so 用它跳转） */
    vec[vi++] = AT_UID;    vec[vi++] = 0;
    vec[vi++] = AT_EUID;   vec[vi++] = 0;
    vec[vi++] = AT_GID;    vec[vi++] = 0;
    vec[vi++] = AT_EGID;   vec[vi++] = 0;
    vec[vi++] = AT_HWCAP;  vec[vi++] = 0;
    vec[vi++] = AT_CLKTCK; vec[vi++] = 100;
    vec[vi++] = AT_SECURE; vec[vi++] = 0;
    vec[vi++] = AT_RANDOM; vec[vi++] = str_base + len_p0 + 1 + len_plat;
    vec[vi++] = AT_EXECFN; vec[vi++] = str_base;
    vec[vi++] = AT_NULL;   vec[vi++] = 0;

#ifdef HEPU_TRACE_LINUX_SYSCALL
    vga_puts("[STACK] phnum="); vga_put_hex(phnum);
    vga_puts(" phent="); vga_put_hex(phent);
    vga_puts(" phdr_va="); vga_put_hex(phdr_va);
    vga_puts("\n");
#endif
    uint64_t vec_used = vi * 8;
    if (vec_used > sizeof(buf)) {
        vga_puts("[STACK] initial stack too large\n");
        return 0;
    }

    /* ---- 2. 把内容写进用户栈页 ----
     * 栈顶页的虚拟地址区间是 [HEPU_USER_STACK_TOP-4096, HEPU_USER_STACK_TOP)，
     * 对应 stack_phys[0]。整个向量区+字符串区必须落在栈页范围内。 */
    uint64_t region_lo = HEPU_USER_STACK_TOP - HEPU_STACK_SIZE;
    if (rsp < region_lo || HEPU_USER_STACK_TOP < str_base) {
        vga_puts("[STACK] initial stack out of stack region\n");
        return 0;
    }

    /* 字符串区内容 */
    static uint8_t strbuf[512];
    uint32_t off = 0;
    for (uint32_t i = 0; i <= len_p0; i++) strbuf[off++] = (uint8_t)p0[i];
    for (uint32_t i = 0; i < len_plat; i++) strbuf[off++] = (uint8_t)plat[i];
    /* AT_RANDOM 的 16 字节：栈保护随机数。这里用内核 tick 派生，
     * 目的是让「每次启动不同」，不是密码学用途（内核目前没有 CSPRNG 接入）。 */
    {
        extern uint64_t g_kernel_tick64;
        uint64_t seed = g_kernel_tick64 * 0x9E3779B97F4A7C15ULL + 0x1234567;
        for (uint32_t i = 0; i < rand_len; i++) {
            seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
            strbuf[off++] = (uint8_t)(seed >> 33);
        }
    }
    if (denv) {
        for (uint32_t i = 0; i <= len_denv; i++)
            strbuf[off++] = (uint8_t)(i < len_denv ? denv[i] : 0);
    }

    /* 写入：向量区从 rsp 开始，字符串区从 str_base 开始 */
    {
        /* 向量区 */
        uint64_t vaddr = rsp;
        uint64_t left = vec_used;
        uint64_t src = 0;
        while (left) {
            uint64_t page_off = vaddr & 0xFFFULL;
            uint64_t chunk = 4096 - page_off;
            if (chunk > left) chunk = left;
            int idx = (int)((HEPU_USER_STACK_TOP - 1 - vaddr) / 4096);
            if (idx < 0 || idx >= HEPU_STACK_PAGES) { vga_puts("[STACK] bad page\n"); return 0; }
            uint8_t *dst = hepu_user_virt_to_ptr(user_pml4, vaddr);
            if (!dst) { vga_puts("[STACK] user stack page not mapped\n"); return 0; }
            for (uint64_t i = 0; i < chunk; i++) dst[i] = buf[src + i];
            src += chunk;
            vaddr += chunk;
            left -= chunk;
        }
        /* 字符串区 */
        vaddr = str_base;
        left = off;
        src = 0;
        while (left) {
            uint64_t page_off = vaddr & 0xFFFULL;
            uint64_t chunk = 4096 - page_off;
            if (chunk > left) chunk = left;
            int idx = (int)((HEPU_USER_STACK_TOP - 1 - vaddr) / 4096);
            if (idx < 0 || idx >= HEPU_STACK_PAGES) { vga_puts("[STACK] bad page\n"); return 0; }
            uint8_t *dst = hepu_user_virt_to_ptr(user_pml4, vaddr);
            if (!dst) { vga_puts("[STACK] user stack page not mapped\n"); return 0; }
            for (uint64_t i = 0; i < chunk; i++) dst[i] = strbuf[src + i];
            src += chunk;
            vaddr += chunk;
            left -= chunk;
        }
    }

#ifdef HEPU_TRACE_LINUX_SYSCALL
    /* 调试：把刚写完的向量区前 8 个字读回来（经用户页表） */
    {
        uint64_t *p = (uint64_t *)hepu_user_virt_to_ptr(user_pml4, rsp);
        vga_puts("[STACK] rsp="); vga_put_hex(rsp);
        vga_puts(" ptr="); vga_put_hex((uint64_t)p);
        vga_puts("\n[STACK] ");
        if (p) {
            for (int i = 0; i < 8; i++) { vga_puts(""); vga_put_hex(p[i]); vga_puts(" "); }
            vga_puts("\n[STACK] auxv ");
            for (int i = 4; i < 14; i++) { vga_puts(""); vga_put_hex(p[i]); vga_puts(" "); }
        } else {
            vga_puts("(unmapped)");
        }
        vga_puts("\n[STACK] phys0="); vga_put_hex(stack_phys[0]);
        vga_puts(" phys1="); vga_put_hex(stack_phys[1]);
        vga_puts(" phys_last="); vga_put_hex(stack_phys[HEPU_STACK_PAGES - 1]);
        vga_puts("\n");
    }
#endif

    return rsp;
}
