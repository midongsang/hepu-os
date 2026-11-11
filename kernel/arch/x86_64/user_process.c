/* ============================================================================
 * 赫普OS 64位 - 用户态进程切换
 * 支持：用户态创建、独立页表、上下文切换、系统调用入口
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include "hepu_smap.h"
#include "elf64.h"
#include "domain64.h"

/* V0.13.43: 与 GDT Linux 标准布局一致 (0x18=用户数据, 0x20=用户代码) */
#define USER_CS 0x20  /* 用户代码段选择子 */
#define USER_DS 0x18  /* 用户数据段选择子 */
#define KERNEL_CS 0x08
#define KERNEL_DS 0x10

#define USER_STACK_TOP 0x70000000
/* V0.33: 用户栈页数（1MB）。必须与 user_stack64.c 的 HEPU_STACK_PAGES 一致：
 * 初始栈内容按「栈顶页 = 物理页 0」的约定写入，两边页数不一致就会写错页。 */
#define HEPU_USER_STACK_PAGES 256
#define USER_CODE_BASE 0x40000000  /* 4GB以上，避免与内核前64MB恒等映射冲突 */
/* 进程入口初始 RSP。GCC 按普通函数编译 _start, 假设入口 rsp ≡ 8 (mod 16)
 * (调用方 call 已压 8 字节); 真实 Linux ELF 的 crt1 自带 and $-16 重对齐, 不受影响。
 * 若直接给 16 对齐的栈顶, 内联系统调用的 movaps 会因栈未对齐触发 #GP(0xD)。 */
#define USER_INIT_RSP   (USER_STACK_TOP - 8)

#define PAGE_PRESENT  (1 << 0)
#define PAGE_WRITABLE (1 << 1)
#define PAGE_USER     (1 << 2)

extern void vga_puts(const char *s);
extern void vga_put_hex(uint64_t val);
extern uint64_t pmm64_alloc_page(void);
extern uint64_t vmm64_get_pml4(void);
extern int vmm64_map_page(uint64_t pml4, uint64_t virt, uint64_t phys, uint64_t flags);
extern int vmm64_page_present(uint64_t pml4, uint64_t virt);
extern void pmm64_free_page(uint64_t addr);
extern void kfree(void *ptr);
extern uint64_t uaspace_create(uint64_t kernel_pml4, int want_isolation, int *isolated_out);
extern int uaspace_free_isolated(uint64_t pml4_phys, uint64_t cur_cr3);

/* 用户地址空间隔离（阶段1，见 docs/security/AUDIT_64BIT.md 漏洞 A、user_aspace64.c）。
 * 默认开启（Makefile 传 -DHEPU_USER_ISOLATION=1）；make USER_ISOLATION=0 临时关闭。
 * 仅作用于 ELF 进程（user_process_exec）；内核函数当入口的内置测试进程
 * （user_process_create）需要 ring3 执行内核镜像，始终走旧模式。 */
#ifndef HEPU_USER_ISOLATION
#define HEPU_USER_ISOLATION 1
#endif
#define UWIN_START 0x0000000040000000ULL   /* 用户窗口（PDPT[1]） */
#define UWIN_END   0x0000000080000000ULL

/* 用户进程结构 */
typedef struct {
    int pid;
    int active;
    uint64_t rip;
    uint64_t rsp;
    uint64_t rflags;
    uint64_t page_table;  /* PML4物理地址 */
    uint8_t *kernel_stack;
    uint64_t kernel_rsp;
    char name[32];
    int kill_requested;   /* V0.13.46: 进程终止请求（未响应强制结束） */
    uint64_t start_tick;  /* V0.13.46: 切换运行时的tick（未响应看门狗） */
    int blocked;          /* V0.20.85: futex阻塞标志 */
    uint64_t futex_wait_addr; /* 等待的futex地址 */
    uint64_t fsbase;     /* V0.21.2: 用户FS_BASE */
    int faulted;         /* V0.33: 是否因异常终止（区别于正常退出/被强杀） */
    uint64_t fault_vector; /* V0.33: 终止时的异常向量号 */
    int env_type;        /* V0.33: 0=原生ABI, 1=Linux兼容（按进程判定，不做全局开关） */
    int domain_id;       /* V0.54: 所属隔离域 ID */
    uint64_t brk_cur;    /* V0.33: 本进程的用户堆 break（每进程独立，不能用全局静态量） */
    int tls_reserved;    /* V0.33: TLS 预留区是否已为本进程映射好 */
    uint64_t mem_pages;       /* P1: 本域已计入的物理页（mmap/brk/模块） */
    uint64_t mem_limit_pages; /* P1: 本域内存配额（页），0=不限制 */
} user_process_t;

#define MAX_USER_PROCS 8
user_process_t user_procs[MAX_USER_PROCS];
static uint8_t g_proc_isolated[MAX_USER_PROCS];   /* 该槽位的页表是否为隔离模式（不改 user_process_t 布局） */
static int g_last_isolated;                        /* 最近一次创建页表实际使用的模式 */
static void user_free_aspace(int idx);
int current_user_proc = -1;
static int user_proc_initialized = 0;

/* 返回当前隔离 ELF 进程的页表 PML4 物理地址；无活动用户进程返回 0 */
uint64_t user_process_current_pml4(void)
{
    if (current_user_proc < 0 || current_user_proc >= MAX_USER_PROCS) return 0;
    if (!user_procs[current_user_proc].active) return 0;
    return user_procs[current_user_proc].page_table;
}

/* V0.33: 最近一次创建的进程的用户栈物理页（4页，栈顶那页在前）
 * 初始栈内容（argc/argv/envp/auxv）要写进这些页。 */
static uint64_t g_user_stack_phys[HEPU_USER_STACK_PAGES];

/* 用户态退出后返回Shell的上下文 */
uint64_t g_user_saved_rsp = 0;
uint64_t g_user_saved_rip = 0;
uint64_t g_user_saved_cr3 = 0;
int g_user_in_user_mode = 0;

/* V0.13.46: 进程释放（前向声明，供user_return_point清理） */
static void user_process_release(int idx);

/* P1: 域内存配额（前向声明，创建路径在定义之前调用） */
static void user_process_mem_init(int slot);

/* 汇编函数：切换到用户态 */
extern void user_enter(uint64_t rip, uint64_t rsp, uint64_t rflags) __attribute__((noreturn));

/* 创建用户页表：复制内核PML4条目（用户态可访问内核），添加用户栈和代码段映射 */
static void ua_fail(uint64_t pml4_phys, int isolated)
{
    if (isolated) {
        uint64_t cr3;
        __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
        uaspace_free_isolated(pml4_phys, cr3);
    }
}

static uint64_t create_user_page_table_ex(void *code_data, uint64_t code_size, uint64_t code_vaddr,
                                          int want_isolation, int *isolated_out, void *raw_elf)
{
    uint64_t kernel_pml4 = vmm64_get_pml4();
    int isolated = 0;
    if (isolated_out) *isolated_out = 0;

    /* 旧模式：逐项复制内核PML4（共享下层页表）；隔离模式：私有PML4/PDPT，内核映射 ring3 不可访问 */
    uint64_t user_pml4_phys = uaspace_create(kernel_pml4, want_isolation, &isolated);
    if (!user_pml4_phys) return 0;
    if (isolated_out) *isolated_out = isolated;

    /* 隔离模式下代码必须落在用户窗口内：窗口外的虚拟地址会写进与内核共享的页表 */
    if (isolated && code_data && code_size > 0) {
        uint64_t pages = (code_size + 4095) / 4096;
        if (code_vaddr < UWIN_START || pages > (UWIN_END - UWIN_START) / 4096 ||
            code_vaddr + pages * 4096 > UWIN_END) {
            vga_puts("[USER64] isolated exec: code range outside user window 0x40000000-0x7FFFFFFF\n");
            ua_fail(user_pml4_phys, isolated);
            return 0;
        }
    }

    /* 分配用户栈物理页（1MB = 256 页）
     * V0.33: 记下这些物理页 —— 初始栈内容（argc/argv/envp/auxv）
     * 要写进栈顶那几页，见 hepu_build_initial_stack。
     * 页数从 4（16KB）提到 256（1MB）：glibc static-PIE 启动阶段就要几十 KB 栈，
     * 16KB 必然爆栈。 */
    for (int i = 0; i < HEPU_USER_STACK_PAGES; i++) {
        uint64_t phys = pmm64_alloc_page();
        if (!phys) { ua_fail(user_pml4_phys, isolated); return 0; }
        uint64_t virt = USER_STACK_TOP - (i + 1) * 4096;
        vmm64_map_page(user_pml4_phys, virt, phys,
                       PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);
        g_user_stack_phys[i] = phys;
    }

    /* 映射用户代码段（按4KB页对齐） */
    if (code_data && code_size > 0) {
        /* V0.20.93: 如果code_vaddr不是页对齐到0x40000000, 补一页放ELF头 */
        if (code_vaddr > 0x40000000ULL) {
            uint64_t phys0 = pmm64_alloc_page();
            if (phys0) {
                extern uint64_t vmm64_phys_to_virt(uint64_t phys);
                uint8_t *dst0 = (uint8_t *)vmm64_phys_to_virt(phys0);
                for (int z = 0; z < 4096; z++) dst0[z] = 0;
                /* V0.21.2: 拷第一个LOAD段(ELF头+phdr+rela.plt)从raw_elf */
                uint64_t copy0 = 4096;
                for (uint64_t z = 0; z < copy0; z++) dst0[z] = ((uint8_t*)raw_elf)[z];
                vmm64_map_page(user_pml4_phys, 0x40000000ULL, phys0,
                               PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);
            }
        }
        uint64_t code_pages = (code_size + 4095) / 4096;
        uint8_t *src = (uint8_t *)code_data;
        for (uint64_t i = 0; i < code_pages; i++) {
            uint64_t phys = pmm64_alloc_page();
            if (!phys) { ua_fail(user_pml4_phys, isolated); return 0; }
            uint64_t virt = code_vaddr + i * 4096;
            /* 复制代码到物理页 */
            extern uint64_t vmm64_phys_to_virt(uint64_t phys);
            uint8_t *dst = (uint8_t *)vmm64_phys_to_virt(phys);
            uint64_t copy_len = 4096;
            if (i * 4096 + 4096 > code_size) copy_len = code_size - i * 4096;
            for (uint64_t j = 0; j < copy_len; j++) dst[j] = src[i * 4096 + j];
            /* 映射到用户地址空间（可读可执行可写） */
            vmm64_map_page(user_pml4_phys, virt, phys,
                           PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);
        }
        vga_puts("[USER64] Mapped code: ");
        char buf[16];
        int n = code_pages, i = 0;
        char tmp[16];
        do { tmp[i++] = n % 10 + '0'; } while ((n /= 10) > 0);
        int j = 0;
        while (i > 0) buf[j++] = tmp[--i];
        buf[j] = 0;
        vga_puts(buf);
        vga_puts(" pages @ ");
        char hex[] = "0123456789ABCDEF";
        char hbuf[19];
        uint64_t addr = code_vaddr;
        hbuf[0]='0'; hbuf[1]='x';
        for (int i = 0; i < 16; i++) { hbuf[17-i] = hex[addr & 0xF]; addr >>= 4; }
        hbuf[18] = 0;
        vga_puts(hbuf);
        vga_puts("\n");
    }

    /* V0.20.58: vDSO 页 - 映射到用户空间固定地址 0x7FFF0000 */
    {
        extern uint64_t pit64_get_ticks(void);
        uint64_t vdso_phys = pmm64_alloc_page();
        if (vdso_phys) {
            uint8_t *vdso = (uint8_t *)vdso_phys;
            for (int i = 0; i < 4096; i++) vdso[i] = 0;
            /* vDSO 数据布局: offset 0 = jiffies (ms) */
            uint64_t ticks = pit64_get_ticks();
            vdso[0] = ticks & 0xFF; vdso[1] = (ticks>>8)&0xFF;
            vdso[2] = (ticks>>16)&0xFF; vdso[3] = (ticks>>24)&0xFF;
            vdso[4] = (ticks>>32)&0xFF; vdso[5] = (ticks>>40)&0xFF;
            vdso[6] = (ticks>>48)&0xFF; vdso[7] = (ticks>>56)&0xFF;
            /* offset 8 = wall_sec */
            vmm64_map_page(user_pml4_phys, 0x7FFF0000, vdso_phys,
                           PAGE_PRESENT | PAGE_USER);
        }
    }

    return user_pml4_phys;
}

/* ELF 进程：按编译开关 HEPU_USER_ISOLATION 决定是否隔离 */
static uint64_t create_user_page_table_with_code(void *code_data, uint64_t code_size, uint64_t code_vaddr, void *raw_elf)
{
    int iso = 0;
    uint64_t p = create_user_page_table_ex(code_data, code_size, code_vaddr, HEPU_USER_ISOLATION, &iso, raw_elf);
    g_last_isolated = p ? iso : 0;
    return p;
}

/* 创建用户页表（仅栈，内核函数入口的内置测试进程；始终旧模式） */
static uint64_t create_user_page_table(void)
{
    int iso = 0;
    uint64_t p = create_user_page_table_ex(NULL, 0, 0, 0, &iso, NULL);
    g_last_isolated = 0;
    return p;
}

/* 初始化用户态进程管理 */
void user_process_init(void)
{
    for (int i = 0; i < MAX_USER_PROCS; i++) {
        user_procs[i].active = 0;
        user_procs[i].pid = -1;
    }
    user_proc_initialized = 1;
    vga_puts("[USER64] User process manager initialized\n");
}

/* 创建用户态进程 */
int user_process_create(const char *name, void (*entry)(void))
{
    if (!user_proc_initialized) return -1;

    int idx = -1;
    for (int i = 0; i < MAX_USER_PROCS; i++) {
        if (!user_procs[i].active) {
            idx = i;
            break;
        }
    }
    if (idx < 0) {
        vga_puts("[USER64] No free process slot\n");
        return -1;
    }

    user_process_t *proc = &user_procs[idx];
    proc->pid = idx + 100;  /* 用户进程PID从100开始 */
    proc->active = 1;
    proc->env_type = ENV_NATIVE;   /* P1: 内置测试进程属于原生交互域 */
    proc->domain_id = domain64_env_to_id(ENV_NATIVE);
    user_process_mem_init(idx);    /* P1: 初始化域内存配额 */
    proc->rip = (uint64_t)entry;
    proc->rsp = USER_INIT_RSP;
    proc->rflags = 0x202;  /* IF=1 */

    /* 创建独立用户页表（复制内核映射+用户栈） */
    proc->page_table = create_user_page_table();
    g_proc_isolated[idx] = 0;
    if (!proc->page_table) {
        vga_puts("[USER64] Failed to create user page table\n");
        proc->active = 0;
        return -1;
    }

    /* 分配内核栈（16KB） */
    proc->kernel_stack = (uint8_t *)pmm64_alloc_page();
#ifdef HEPU_TRACE_LINUX_SYSCALL
    {
        /* V0.33 诊断：核对用户栈页与内核栈页有没有撞车（PMM 重复分配） */
        vga_puts("[USER64] phys user stack pages:");
        for (int i = 0; i < 4; i++) { vga_puts(" "); vga_put_hex(g_user_stack_phys[i]); }
        vga_puts(" kstack="); vga_put_hex((uint64_t)proc->kernel_stack);
        vga_puts("\n");
    }
#endif
    if (!proc->kernel_stack) {
        proc->active = 0;
        return -1;
    }
    proc->kernel_rsp = (uint64_t)proc->kernel_stack + 4096;

    /* 复制名称 */
    if (name) {
        int i = 0;
        while (name[i] && i < 31) {
            proc->name[i] = name[i];
            i++;
        }
        proc->name[i] = 0;
    }

    vga_puts("[USER64] Created user process ");
    vga_puts(proc->name);
    vga_puts(" (PID=");
    char pidstr[8];
    int n = proc->pid, i = 0;
    char tmp[8];
    do { tmp[i++] = n % 10 + '0'; } while ((n /= 10) > 0);
    int j = 0;
    while (i > 0) pidstr[j++] = tmp[--i];
    pidstr[j] = 0;
    vga_puts(pidstr);
    vga_puts(")\n");

    return proc->pid;
}

/* 创建带ELF代码段的用户进程（真正的用户空间映射） */
/* ==========================================================================
 * V0.33: R_X86_64_IRELATIVE 重定位（通用实现）
 *
 * 为什么要内核做这件事：
 *   static-PIE（ET_DYN 且没有 PT_INTERP）里，glibc 的 _dl_relocate_static_pie
 *   只处理 R_X86_64_RELATIVE，**不处理 IRELATIVE**。
 *   IRELATIVE 的语义是「这个函数指针的真实地址由 resolver 在运行时算出来」，
 *   Linux 上由内核在加载时逐个调用 resolver 并把结果写回 GOT。
 *   少这一步，GOT 里就留着未初始化的桩地址，glibc 启动时 __ctype_init
 *   之类会通过空指针读 —— 表现为“在 __ctype_init 里读 0 地址崩溃”。
 *
 * 契约（与 ELF/Linux 一致）：
 *   - 内核负责映射段 + 处理 IRELATIVE；
 *   - RELATIVE 之类的常规重定位由加载器（这里是 glibc 自己）负责，
 *     内核不重复做，避免同一批指针被加两次基址。
 * ========================================================================== */
#define HEPU_R_X86_64_IRELATIVE 37

extern uint64_t vmm64_phys_to_virt(uint64_t phys);

/* 在 ELF 文件里把虚拟地址换成文件偏移；换不了返回 0 */
static uint64_t hepu_elf_vaddr_to_fileoff(const uint8_t *elf, uint64_t elf_size, uint64_t vaddr, uint64_t *out_size)
{
    if (!elf || elf_size < 64) return 0;
    uint64_t e_phoff = *(uint64_t *)(elf + 0x20);
    uint16_t e_phnum = *(uint16_t *)(elf + 0x38);
    uint16_t e_phentsize = *(uint16_t *)(elf + 0x36);
    if (e_phoff == 0 || e_phnum == 0 || e_phnum > 128 || e_phentsize < 56) return 0;
    if (e_phoff + (uint64_t)e_phnum * e_phentsize > elf_size) return 0;
    for (int i = 0; i < e_phnum; i++) {
        const uint8_t *ph = elf + e_phoff + (uint64_t)i * e_phentsize;
        if (*(uint32_t *)(ph + 0) != 1 /* PT_LOAD */) continue;
        uint64_t p_offset = *(uint64_t *)(ph + 8);
        uint64_t p_vaddr = *(uint64_t *)(ph + 16);
        uint64_t p_filesz = *(uint64_t *)(ph + 32);
        if (p_offset + p_filesz > elf_size) continue;
        if (vaddr >= p_vaddr && vaddr < p_vaddr + p_filesz) {
            uint64_t off = p_offset + (vaddr - p_vaddr);
            if (out_size) *out_size = p_filesz - (vaddr - p_vaddr);
            return off;
        }
    }
    return 0;
}

/* 通过用户页表把虚拟地址翻译成内核可写的指针；失败返回 0 */

/* V0.33.5: W^X 写保护版本 — 拒绝写到可执行页。
 * 内核写用户页时(GOT重定位等), 如果该页 NX=0(可执行), 拒绝写。 */
static void *hepu_user_write_ptr(uint64_t pml4_phys, uint64_t vaddr)
{
    if (vaddr < 0x40000000ULL || vaddr >= 0x80000000ULL) return 0;
    uint64_t *l4 = (uint64_t *)vmm64_phys_to_virt(pml4_phys);
    uint64_t e4 = l4[(vaddr >> 39) & 0x1FF];
    if (!(e4 & 1) || !(e4 & 4)) return 0;
    uint64_t *l3 = (uint64_t *)vmm64_phys_to_virt(e4 & 0x000FFFFFFFFFF000ULL);
    uint64_t e3 = l3[(vaddr >> 30) & 0x1FF];
    if (!(e3 & 1) || !(e3 & 4)) return 0;
    if (e3 & 0x80) {
        if (!(e3 & (1ULL << 63))) return 0; /* NX=0, 可执行页, 拒绝写 */
        uint64_t phys = e3 & 0x000FFFFFE00000ULL;
        return (uint8_t *)vmm64_phys_to_virt(phys) + (vaddr & 0x1FFFFF);
    }
    uint64_t *l2 = (uint64_t *)vmm64_phys_to_virt(e3 & 0x000FFFFFFFFFF000ULL);
    uint64_t e2 = l2[(vaddr >> 21) & 0x1FF];
    if (!(e2 & 1) || !(e2 & 4)) return 0;
    if (e2 & 0x80) {
        if (!(e2 & (1ULL << 63))) return 0;
        uint64_t phys = e2 & 0x000FFFFFFFE00000ULL;
        return (uint8_t *)vmm64_phys_to_virt(phys) + (vaddr & 0x1FFFFF);
    }
    uint64_t *l1 = (uint64_t *)vmm64_phys_to_virt(e2 & 0x000FFFFFFFFFF000ULL);
    uint64_t e1 = l1[(vaddr >> 12) & 0x1FF];
    if (!(e1 & 1) || !(e1 & 4)) return 0;
    if (!(e1 & (1ULL << 63))) return 0; /* NX=0, 可执行页, 拒绝写 */
    uint64_t phys = e1 & 0x000FFFFFFFFFF000ULL;
    return (uint8_t *)vmm64_phys_to_virt(phys) + (vaddr & 0xFFF);
}

/* 对已建好页表的 Linux 兼容进程执行 IRELATIVE 重定位，返回处理条数 */
static int hepu_apply_irelative(user_process_t *proc, const uint8_t *elf, uint64_t elf_size, uint64_t load_bias)
{
    if (!proc || !elf || !proc->page_table || elf_size < 64) return 0;
    if (elf[0] != 0x7F || elf[1] != 'E' || elf[2] != 'L' || elf[3] != 'F') return 0;

    uint64_t e_phoff = *(uint64_t *)(elf + 0x20);
    uint16_t e_phnum = *(uint16_t *)(elf + 0x38);
    uint16_t e_phentsize = *(uint16_t *)(elf + 0x36);
    /* 边界检查：程序头表必须整体落在文件内（避免 e_phoff/e_phnum 溢出越界读） */
    if (e_phentsize < 56 || e_phoff > elf_size ||
        (uint64_t)e_phnum > (elf_size - e_phoff) / e_phentsize) return 0;

    /* 找 PT_DYNAMIC */
    uint64_t dyn_fileoff = 0, dyn_size = 0;
    for (int i = 0; i < e_phnum; i++) {
        const uint8_t *ph = elf + e_phoff + (uint64_t)i * e_phentsize;
        if (*(uint32_t *)(ph + 0) != 2 /* PT_DYNAMIC */) continue;
        dyn_fileoff = *(uint64_t *)(ph + 8);
        dyn_size = *(uint64_t *)(ph + 32);
        break;
    }
    if (!dyn_fileoff || dyn_size < 16) return 0;
    /* 边界检查：动态段必须整体落在文件内 */
    if (dyn_fileoff > elf_size || dyn_size > elf_size - dyn_fileoff) return 0;

    /* 扫 DT_ 找 DT_JMPREL / DT_PLTRELSZ */
    uint64_t jmprel_vaddr = 0, pltrelsz = 0;
    uint64_t rela_vaddr = 0, relasz = 0;
    for (uint64_t off = 0; off + 16 <= dyn_size; off += 16) {
        int64_t d_tag = *(int64_t *)(elf + dyn_fileoff + off);
        uint64_t d_val = *(uint64_t *)(elf + dyn_fileoff + off + 8);
        if (d_tag == 0 /* DT_NULL */) break;
        if (d_tag == 23 /* DT_JMPREL */) jmprel_vaddr = d_val;
        else if (d_tag == 2 /* DT_PLTRELSZ */) pltrelsz = d_val;
        else if (d_tag == 7 /* DT_RELA */) rela_vaddr = d_val;
        else if (d_tag == 8 /* DT_RELASZ */) relasz = d_val;
    }
#ifdef HEPU_TRACE_LINUX_SYSCALL
    vga_puts("[IREL] dyn_off="); vga_put_hex(dyn_fileoff);
    vga_puts(" dyn_size="); vga_put_hex(dyn_size);
    vga_puts(" jmprel="); vga_put_hex(jmprel_vaddr);
    vga_puts(" pltrelsz="); vga_put_hex(pltrelsz);
    vga_puts("\n");
#endif
    /* .rela.dyn 里的 IRELATIVE 也要处理（ifunc 解析器常在这里）。
     * 注意：static-PIE 的链接器**不一定**在动态段里留 DT_RELA，
     * 所以除了 DT_RELA 还要直接看 section header 里的 .rela.dyn / .rela.plt。 */
    uint64_t rela2_size = 0, rela2_off = 0;
    if (rela_vaddr && relasz) {
        rela2_off = hepu_elf_vaddr_to_fileoff(elf, elf_size, rela_vaddr, &rela2_size);
        if (relasz > rela2_size) relasz = rela2_size;
    }
    uint64_t rela_size = 0;
    uint64_t rela_off = 0;
    if (jmprel_vaddr && pltrelsz) {
        rela_off = hepu_elf_vaddr_to_fileoff(elf, elf_size, jmprel_vaddr, &rela_size);
        if (pltrelsz > rela_size) pltrelsz = rela_size;
    }
    /* 收集所有 SHT_RELA 段：IRELATIVE 可能落在 .rela.dyn / .rela.plt /
     * .rela.iplt（glibc static-PIE 的 ifunc 解析器就在 .rela.iplt 里）。
     * 动态段未必留 DT_RELA，所以 section header 才是可靠的来源。 */
    uint64_t tab_off[8], tab_size[8];
    int ntab = 0;
    {
        uint64_t e_shoff = *(uint64_t *)(elf + 0x28);
        uint16_t e_shnum = *(uint16_t *)(elf + 0x3C);
        uint16_t e_shentsize = *(uint16_t *)(elf + 0x3A);
        /* 边界检查：节头表必须整体落在文件内，每项至少 64 字节 */
        if (e_shoff && e_shnum && e_shentsize >= 64 &&
            e_shoff <= elf_size &&
            (uint64_t)e_shnum <= (elf_size - e_shoff) / e_shentsize) {
            for (int i = 0; i < e_shnum && ntab < 8; i++) {
                const uint8_t *sh = elf + e_shoff + (uint64_t)i * e_shentsize;
                if (*(uint32_t *)(sh + 4) != 4 /* SHT_RELA */) continue;
                uint64_t off = *(uint64_t *)(sh + 0x18);
                uint64_t sz  = *(uint64_t *)(sh + 0x20);
                /* 边界检查：重定位表必须整体落在文件内 */
                if (off > elf_size || sz > elf_size - off) continue;
                tab_off[ntab] = off;
                tab_size[ntab] = sz;
                ntab++;
            }
        }
    }
    /* DT_JMPREL 指向的表也要在列表里（有些产物没有对应 section 名字）。
     * V0.33 修复：必须去重 —— 绝大多数产物的 .rela.plt 同时出现在
     * section header 列表和 DT_JMPREL 里，不去重就会把同一批
     * IRELATIVE 应用两遍（resolver 被调两次，浪费且行为不确定）。 */
    if (rela_off && ntab < 8) {
        int dup = 0;
        for (int i = 0; i < ntab; i++) {
            if (tab_off[i] == rela_off) { dup = 1; break; }
        }
        if (!dup) {
            tab_off[ntab] = rela_off;
            tab_size[ntab] = pltrelsz;
            ntab++;
        }
    }
    /* V0.55.1 修复: DT_RELA 指向的 .rela.dyn 此前只算偏移(rela2_off)却从未加入
     * 上表, 若 IRELATIVE 只出现在 DT_RELA 而不在 section header 列表里, 这些
     * ifunc 解析器会被完全跳过。此处按与上面相同的去重规则补入。 */
    if (rela2_off && relasz && ntab < 8) {
        int dup2 = 0;
        for (int i = 0; i < ntab; i++) {
            if (tab_off[i] == rela2_off) { dup2 = 1; break; }
        }
        if (!dup2) { tab_off[ntab] = rela2_off; tab_size[ntab] = relasz; ntab++; }
    }
    if (ntab == 0) return 0;

#ifdef HEPU_TRACE_LINUX_SYSCALL
    vga_puts("[IREL] rela_off="); vga_put_hex(rela_off);
    vga_puts(" rela_size="); vga_put_hex(rela_size);
    vga_puts(" rela2_off="); vga_put_hex(rela2_off);
    vga_puts(" relasz="); vga_put_hex(relasz);
    vga_puts("\n");
#endif
    if (!rela_off && !ntab) return 0;

    /* 切到该进程的用户页表, 写GOT槽位 */
    uint64_t saved_cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(saved_cr3));
    __asm__ volatile("mov %0, %%cr3" : : "r"(proc->page_table) : "memory");
    int ac_prev = hepu_user_access_enter();

    /* V0.33.2 安全修复: 不再在 ring0 调用用户态 resolver (SMEP/SMAP绕过)。
     * 直接把 r_addend+load_bias 写进 GOT。
     * 代价: ifunc 的 CPU 特性检测不执行 (memcmp/strcmp 用通用版本, 慢)。
     * 正确长期方案: glibc 静态PIE自己 _dl_relocate_static_pie, 内核不管。 */
    int applied = 0;
    for (int which = 0; which < ntab; which++) {
    uint64_t tbl_off  = tab_off[which];
    uint64_t tbl_size = tab_size[which];
    for (uint64_t off = 0; off + 24 <= tbl_size; off += 24) {
        const uint8_t *rela = elf + tbl_off + off;
        uint64_t r_offset = *(uint64_t *)(rela);
        uint64_t r_info = *(uint64_t *)(rela + 8);
        int64_t r_addend = *(int64_t *)(rela + 16);
        uint32_t type = (uint32_t)(r_info & 0xFFFFFFFF);
        if (type != HEPU_R_X86_64_IRELATIVE) continue;

        /* 直接写 resolver 地址到 GOT (不调用它) */
        uint64_t resolved = (uint64_t)r_addend + load_bias;
        void *slot = hepu_user_write_ptr(proc->page_table, r_offset + load_bias);
        if (slot) {
            *(uint64_t *)slot = resolved;
            applied++;
        }
    }
    }

    hepu_user_access_exit(ac_prev);
    __asm__ volatile("mov %0, %%cr3" : : "r"(saved_cr3) : "memory");
    return applied;
}

int user_process_create_elf(const char *name, void *code_data, uint64_t code_size,
                            uint64_t code_vaddr, uint64_t entry_vaddr,
                            void *raw_elf, uint64_t raw_elf_size,
                            int env_type, uint64_t at_entry_va, uint64_t interp_base)
{
    if (!user_proc_initialized) return -1;

    int idx = -1;
    for (int i = 0; i < MAX_USER_PROCS; i++) {
        if (!user_procs[i].active) {
            idx = i;
            break;
        }
    }
    if (idx < 0) {
        vga_puts("[USER64] No free process slot\n");
        return -1;
    }

    user_process_t *proc = &user_procs[idx];
    proc->pid = idx + 100;
    proc->active = 1;
    proc->kill_requested = 0;
    proc->env_type = env_type;   /* V0.33: 进程级 ABI 标记 */
    proc->domain_id = domain64_env_to_id(env_type);
    user_process_mem_init(idx);  /* P1: 按域（原生/兼容）初始化内存配额 */
    proc->brk_cur = 0;           /* V0.33: 每进程独立的堆 break（槽位复用时要清） */
    proc->tls_reserved = 0;      /* V0.33: TLS 预留区标记同理（槽位复用会沿用旧值） */
    proc->rip = entry_vaddr;  /* 用户空间入口点 */
    proc->rsp = USER_INIT_RSP;
    proc->rflags = 0x202;  /* IF=1 */

    /* 创建带代码段的用户页表 */
    proc->page_table = create_user_page_table_with_code(code_data, code_size, code_vaddr, raw_elf);
    g_proc_isolated[idx] = proc->page_table ? (uint8_t)g_last_isolated : 0;
    if (!proc->page_table) {
        vga_puts("[USER64] Failed to create user page table with code\n");
        proc->active = 0;
        return -1;
    }

    /* V0.33: Linux ABI 兼容进程要带一套完整的初始栈（argc/argv/envp/auxv）。
     * Hepu 原生进程不用这套约定（ulib 体系不需要），栈保持空栈 —— 
     * 按 ABI 类型区分，不给所有进程都塞一套隐式语义。 */
    if (env_type == ENV_LINUX_COMPAT) {
        extern uint64_t hepu_build_initial_stack(const uint64_t *stack_phys, const char *path,
                                                 uint64_t entry_va, uint64_t phdr_va,
                                                 uint32_t phnum, uint32_t phent,
                                                 uint64_t user_pml4,
                                                 uint64_t at_entry_va, uint64_t interp_base);
        uint64_t phdr_va = 0;
        uint32_t phnum = 0, phent = 0;
        if (raw_elf && raw_elf != code_data) {
            /* 程序头表在 ELF 映像里的虚拟地址 = 映射基址 + e_phoff
             * （映像原点 code_start 已包含在 g_elf_last_code_start 里） */
            uint8_t *eh = (uint8_t *)raw_elf;
            if (eh[0] == 0x7F && eh[1] == 'E' && eh[2] == 'L' && eh[3] == 'F') {
                uint64_t e_phoff = *(uint64_t *)(eh + 0x20);
                phnum = *(uint16_t *)(eh + 0x38);
                phent = *(uint16_t *)(eh + 0x36);
                phdr_va = code_vaddr + e_phoff;
            }
        }
        uint64_t rsp = hepu_build_initial_stack(g_user_stack_phys, name,
                                                entry_vaddr, phdr_va, phnum, phent,
                                                proc->page_table,
                                                at_entry_va ? at_entry_va : entry_vaddr,
                                                interp_base);
        if (!rsp) {
            vga_puts("[USER64] Failed to build initial stack\n");
            proc->active = 0;
            return -1;
        }
        proc->rsp = rsp;

        /* V0.33: TLS 预留区 —— glibc 的 static-PIE 在 __libc_setup_tls 里
         * 自己去 brk(0) 拿程序断点，把 TLS 块放在断点下方约 2.6MB 处，
         * 然后直接把 TLS 初始化镜像 memcpy 进去。Linux 上这段内存是内核
         * 通过 PT_TLS 预留的；Hepu 没有这个机制 → glibc 往没人映射的地址
         * 写 → #PF（CR2 落在堆起点下方）。
         *
         * 做法（确定性优先）：进程一建好就把这段地址一次性映射掉 ——
         * 落在用户窗口的私有分区（0x67810000-0x68010000），不碰内核页表。
         * 放在建进程时做（而不是等 brk 增长时），是因为 glibc 的 TLS 初始化
         * 可能一个 brk 都不发就写 TLS 块。
         */
        {
            extern uint64_t pmm64_alloc_page(void);
            extern int vmm64_map_page(uint64_t pml4, uint64_t virt, uint64_t phys, uint64_t flags);
            #define HEPU_TLS_RESERVE_LO 0x67810000ULL
            #define HEPU_TLS_RESERVE_HI 0x68010000ULL
            #define HEPU_TLS_PAGE_FLAGS   (0x001ULL | 0x002ULL | 0x004ULL | (1ULL << 63)) /* P|W|U|NX */
            for (uint64_t v = HEPU_TLS_RESERVE_LO; v < HEPU_TLS_RESERVE_HI; v += 4096) {
                uint64_t phys = pmm64_alloc_page();
                if (!phys) break;
                if (vmm64_map_page(proc->page_table, v, phys, HEPU_TLS_PAGE_FLAGS) != 0) {
                    pmm64_free_page(phys);
                    break;
                }
            }
            proc->tls_reserved = 1;

            /* V0.33: 初始 break 所在的那一页也要映射。
             * brk(0) 返回 0x68010000，但这一页在进程建立时并不存在 ——
             * glibc 的 malloc 会在调用 brk 之前就往 [break, break+N) 里写，
             * 于是 #PF（实测 CR2 落在初始 break + 0x3D0）。
             * 堆页是可写+用户+NX（数据页不可执行，W^X）。 */
            {
                extern uint64_t pmm64_alloc_page(void);
                extern int vmm64_map_page(uint64_t pml4, uint64_t virt, uint64_t phys, uint64_t flags);
                #define HEPU_BRK_INIT 0x68010000ULL
                #define HEPU_DATA_FLAGS (0x001ULL | 0x002ULL | 0x004ULL | (1ULL << 63))
                uint64_t bp = HEPU_BRK_INIT & ~0xFFFULL;
                uint64_t bphys = pmm64_alloc_page();
                if (bphys) {
                    if (vmm64_map_page(proc->page_table, bp, bphys, HEPU_DATA_FLAGS) == 0) {
                        /* 页内容清零：malloc 的元数据不能是垃圾 */
                        extern uint64_t vmm64_phys_to_virt(uint64_t phys);
                        uint8_t *bdst = (uint8_t *)vmm64_phys_to_virt(bphys);
                        for (int z = 0; z < 4096; z++) bdst[z] = 0;
                    } else {
                        pmm64_free_page(bphys);
                    }
                }
            }
        }

        /* V0.33: IRELATIVE 重定位 —— static-PIE 的 glibc 自己不做这个，
         * 必须由内核在装载时调用 resolver 并写回 GOT，
         * 否则 GOT 里留着桩地址，glibc 启动会踩空指针。 */
        if (raw_elf && !elf64_has_interp(raw_elf, raw_elf_size)) {
            /* 仅 static-PIE：内核代跑 IRELATIVE。
             * 动态 ELF（PT_INTERP）的 IRELATIVE 必须由 ld.so 在符号表/重定位
             * 全部就绪之后处理 —— 内核提前跑 resolver 会在未重定位的 GOT 上跑飞。 */
            /* 装载偏置 = 实际映射基址 - 映像原点(最低 PT_LOAD 的 p_vaddr) */
            uint64_t code_start = 0;
            int have_start = 0;
            {
                uint8_t *eh = (uint8_t *)raw_elf;
                uint64_t e_phoff = *(uint64_t *)(eh + 0x20);
                uint16_t e_phnum = *(uint16_t *)(eh + 0x38);
                uint16_t e_phesz = *(uint16_t *)(eh + 0x36);
                for (int i = 0; i < e_phnum; i++) {
                    uint8_t *ph = eh + e_phoff + (uint64_t)i * e_phesz;
                    if (*(uint32_t *)(ph + 0) != 1) continue;
                    uint64_t pva = *(uint64_t *)(ph + 16);
                    /* V0.33 修复：原来用 `code_start == 0` 当「还没找到」哨兵，
                     * 而 static-PIE 的最低 PT_LOAD 恰好 p_vaddr == 0 ——
                     * 于是哨兵和合法值撞车，code_start 被改成**第二个**段的
                     * p_vaddr（0x9000），load_bias 少算 0x9000，
                     * 所有 IRELATIVE resolver 都被加到错的地址上。 */
                    if (!have_start || pva < code_start) { code_start = pva; have_start = 1; }
                }
            }
            uint64_t load_bias = code_vaddr - code_start;
#ifdef HEPU_TRACE_LINUX_SYSCALL
            vga_puts("[IREL0] code_vaddr="); vga_put_hex(code_vaddr);
            vga_puts(" code_start="); vga_put_hex(code_start);
            vga_puts(" load_bias="); vga_put_hex(load_bias);
            vga_puts("\n");
#endif
            int n = hepu_apply_irelative(proc, (const uint8_t *)raw_elf, raw_elf_size, load_bias);
            if (n > 0) {
                vga_puts("[USER64] IRELATIVE applied: ");
                char nb[8]; int k = 0, m = n;
                do { nb[k++] = (char)('0' + m % 10); } while ((m /= 10) > 0);
                for (int q = k - 1; q >= 0; q--) vga_putchar(nb[q]);
                vga_puts("\n");
            }
        }

        vga_puts("[USER64] initial stack: rsp=");
        vga_put_hex(rsp);
        vga_puts(" argc/argv/envp/auxv ready\n");
    }

    /* 分配内核栈（16KB） */
    proc->kernel_stack = (uint8_t *)pmm64_alloc_page();
    if (!proc->kernel_stack) {
        user_free_aspace(idx);
        proc->active = 0;
        return -1;
    }
    proc->kernel_rsp = (uint64_t)proc->kernel_stack + 4096;

    /* 复制名称 */
    if (name) {
        int i = 0;
        while (name[i] && i < 31) {
            proc->name[i] = name[i];
            i++;
        }
        proc->name[i] = 0;
    }

    vga_puts("[USER64] Created ELF user process ");
    vga_puts(proc->name);
    vga_puts(" (PID=");
    char pidstr[8];
    int n = proc->pid, i = 0;
    char tmp[8];
    do { tmp[i++] = n % 10 + '0'; } while ((n /= 10) > 0);
    int j = 0;
    while (i > 0) pidstr[j++] = tmp[--i];
    pidstr[j] = 0;
    vga_puts(pidstr);
    vga_puts(", entry=");
    char hex[] = "0123456789ABCDEF";
    char hbuf[19];
    uint64_t addr = entry_vaddr;
    hbuf[0]='0'; hbuf[1]='x';
    for (int i = 0; i < 16; i++) { hbuf[17-i] = hex[addr & 0xF]; addr >>= 4; }
    hbuf[18] = 0;
    vga_puts(hbuf);
    vga_puts(")\n");

    /* V0.33: 旧的「按文件名含 gli 就走专用 auxv/IRELATIVE 路径」已删除。
     *
     * 旧实现的问题：
     *   - AT_PHDR 硬编码 0x40000040、IRELATIVE 表硬编码文件偏移 0x2e8、条数硬编码 24；
     *   - 只对名字含 "gli" 的程序生效，其余 Linux ELF 拿不到正确的初始栈；
     *   - 与通用路径重复装载，两条路都写一遍栈/重定位。
     * 现在统一由上面的 ENV_LINUX_COMPAT 分支处理：
     *   hepu_build_initial_stack() 按 ELF 实际 phdr 计算 AT_PHDR/AT_PHENT/AT_PHNUM，
     *   hepu_apply_irelative() 扫描全部 SHT_RELA 段并加装载偏置。
     * 「原生 ABI 优先、兼容层是附加层」—— 兼容语义由 env_type 决定，不由文件名决定。
     */

    return proc->pid;
}

/* V0.20.78: 动态链接 — 把额外内存映射到已创建进程的地址空间 */
int user_process_map_extra(int pid, void *data, uint64_t size, uint64_t vaddr)
{
    for (int i = 0; i < MAX_USER_PROCS; i++) {
        if (user_procs[i].active && user_procs[i].pid == pid) {
            uint64_t pml4 = user_procs[i].page_table;
            if (!pml4) return -1;
            extern uint64_t vmm64_phys_to_virt(uint64_t phys);
            /* V0.20.78: 正确解析ELF PT_LOAD段, 按p_vaddr映射 */
            uint8_t *elf = (uint8_t *)data;
            uint64_t e_phoff = *(uint64_t*)(elf + 0x20);
            uint16_t e_phnum = *(uint16_t*)(elf + 0x38);
            uint16_t e_phentsize = *(uint16_t*)(elf + 0x36);
            for (int si = 0; si < e_phnum; si++) {
                uint8_t *ph = elf + e_phoff + si * e_phentsize;
                uint32_t p_type = *(uint32_t*)(ph + 0);
                if (p_type != 1) continue;  /* PT_LOAD */
                uint64_t p_offset = *(uint64_t*)(ph + 0x08);
                uint64_t p_vaddr = *(uint64_t*)(ph + 0x10);
                uint64_t p_filesz = *(uint64_t*)(ph + 0x20);
                if (p_filesz == 0) continue;
                if (p_vaddr < 0x40000000ULL) continue;  /* 跳过低位ELF头段 */
                uint64_t start_page = p_vaddr & ~0xFFFULL;
                uint64_t end_page = (p_vaddr + p_filesz + 0xFFF) & ~0xFFFULL;
                for (uint64_t pg = start_page; pg < end_page; pg += 4096) {
                    uint64_t phys = pmm64_alloc_page();
                    if (!phys) return -1;
                    uint8_t *dst = (uint8_t *)vmm64_phys_to_virt(phys);
                    /* zero page first */
                    for (int z = 0; z < 4096; z++) dst[z] = 0;
                    /* copy file bytes for this page */
                    /* V0.55.1: 旧代码用 uint64 做 "<0" 判断(恒假), copy_lo 从未生效;
                     * 页内偏移实际由下面的 copy_start/copy_end 正确裁剪, 故删除。 */
                    uint64_t seg_start = p_vaddr;
                    uint64_t seg_end = p_vaddr + p_filesz;
                    uint64_t copy_start = (pg > seg_start) ? pg : seg_start;
                    uint64_t copy_end = (pg + 4096 < seg_end) ? pg + 4096 : seg_end;
                    if (copy_start < copy_end) {
                        uint64_t off_in_file = p_offset + (copy_start - p_vaddr);
                        uint64_t off_in_page = copy_start - pg;
                        uint64_t n = copy_end - copy_start;
                        for (uint64_t j = 0; j < n; j++)
                            dst[off_in_page + j] = elf[off_in_file + j];
                    }
                    vmm64_map_page(pml4, pg, phys,
                                   PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);
                }
            }
            return 0;
        }
    }
    return -1;
}

/* V0.20.78: 动态链接 — 写用户内存 (GOT重定位用) */
int user_process_write_mem(int pid, uint64_t vaddr, const void *data, uint64_t len)
{
    for (int i = 0; i < MAX_USER_PROCS; i++) {
        if (user_procs[i].active && user_procs[i].pid == pid) {
            uint64_t pml4 = user_procs[i].page_table;
            if (!pml4) return -1;
            /* 临时切到该进程页表, 直接写 */
            uint64_t saved_cr3;
            __asm__ volatile("mov %%cr3, %0" : "=r"(saved_cr3));
            __asm__ volatile("mov %0, %%cr3" : : "r"(pml4) : "memory");
            /* 允许内核访问用户页（仅当 CR4.SMAP 置位时才发 STAC，否则会 #UD） */
            int ac_prev = hepu_user_access_enter();
            for (uint64_t j = 0; j < len; j++) {
                ((volatile uint8_t*)vaddr)[j] = ((const uint8_t*)data)[j];
            }
            hepu_user_access_exit(ac_prev);
            __asm__ volatile("mov %0, %%cr3" : : "r"(saved_cr3) : "memory");
            return 0;
        }
    }
    return -1;
}

/* ==========================================================================
 * P1: 域内存水位与 OOM 限流
 *
 * 域分类（当前实现）：
 *   交互域 = ENV_NATIVE（原生程序）；兼容/代理域 = ENV_LINUX_COMPAT。
 * 策略：
 *   1. 每个域有独立配额，超配额直接拒绝分配；
 *   2. 物理内存低于紧急水位时，只对兼容/代理域限流，原生交互域不受阻断；
 *   3. 限流前先回收块缓存（可回收页），回收后仍不足才拒绝；不杀进程。
 * ========================================================================== */
#define MEM_MIN_NATIVE_PAGES    (32UL * 1024 * 1024 / 4096)
#define MEM_MIN_COMPAT_PAGES    (16UL * 1024 * 1024 / 4096)
#define MEM_CRITICAL_FREE_PAGES (32UL * 1024 * 1024 / 4096)

extern void pmm64_get_stats(uint64_t *total, uint64_t *used, uint64_t *free);
extern void mem_pressure_reclaim_cache(void);

static int mem_domain_is_interactive(const user_process_t *proc)
{
    return proc->env_type == ENV_NATIVE;
}

/* 进程创建/槽位复用时初始化配额。配额按物理内存总量比例给出，并设下限。 */
static void user_process_mem_init(int slot)
{
    if (slot < 0 || slot >= MAX_USER_PROCS) return;
    uint64_t total = 0, used = 0, freeb = 0;
    pmm64_get_stats(&total, &used, &freeb);
    uint64_t total_pages = total / 4096;
    uint64_t limit;
    if (user_procs[slot].env_type == ENV_LINUX_COMPAT) {
        limit = total_pages / 4;
        if (limit < MEM_MIN_COMPAT_PAGES) limit = MEM_MIN_COMPAT_PAGES;
    } else {
        limit = total_pages / 2;
        if (limit < MEM_MIN_NATIVE_PAGES) limit = MEM_MIN_NATIVE_PAGES;
    }
    user_procs[slot].mem_pages = 0;
    user_procs[slot].mem_limit_pages = limit;
}

/* 记入 pages 页；成功返回 0，超配额或系统水位不足返回 -1（不修改计数）。 */
static int user_process_mem_charge_slot(int slot, uint64_t pages)
{
    if (pages == 0) return 0;
    if (slot < 0 || slot >= MAX_USER_PROCS) return 0;   /* 内核上下文不配额 */
    user_process_t *proc = &user_procs[slot];
    if (!proc->active || !proc->mem_limit_pages) return 0;

    if (proc->mem_pages + pages > proc->mem_limit_pages) return -1;

    if (!mem_domain_is_interactive(proc)) {
        uint64_t total = 0, used = 0, freeb = 0;
        pmm64_get_stats(&total, &used, &freeb);
        if (freeb / 4096 < MEM_CRITICAL_FREE_PAGES) {
            mem_pressure_reclaim_cache();      /* 先回收可回收页 */
            pmm64_get_stats(&total, &used, &freeb);
            if (freeb / 4096 < MEM_CRITICAL_FREE_PAGES) return -1;
        }
    }
    proc->mem_pages += pages;
    return 0;
}

static void user_process_mem_uncharge_slot(int slot, uint64_t pages)
{
    if (pages == 0 || slot < 0 || slot >= MAX_USER_PROCS) return;
    user_process_t *proc = &user_procs[slot];
    if (pages > proc->mem_pages) proc->mem_pages = 0;
    else proc->mem_pages -= pages;
}

/* 当前进程记入/退还页数；无活动进程时视为内核上下文，直接放行。 */
int user_process_mem_charge(uint64_t pages)
{
    return user_process_mem_charge_slot(current_user_proc, pages);
}

void user_process_mem_uncharge(uint64_t pages)
{
    user_process_mem_uncharge_slot(current_user_proc, pages);
}

/* ==========================================================================
 * V0.20.79: 用户态模块加载器 (dlopen 雏形)
 * - 每个模块独立VA区间, 不复用旧地址
 * - 解析PT_DYNAMIC, 自动符号查找+重定位
 * - 支持unload
 * ========================================================================== */
#define HEPU_MAX_MODULES 8
typedef struct {
    int used;
    uint64_t base;       /* 用户空间基址 */
    uint64_t size;       /* 总大小(页对齐) */
    uint64_t refcount;
    char name[32];
    /* V0.20.80: 符号表信息 */
    uint64_t dynsym;    /* 模块内.dynsym偏移 */
    uint64_t dynstr;    /* 模块内.dynstr偏移 */
    uint64_t syment;
    uint64_t strsz;
    uint64_t link_base; /* 链接时基址 */
    uint8_t *kbuf;      /* 保留内核缓冲用于符号查找 */
} hepu_module_t;

/* V0.33 P1: 每进程一份模块表。
 * 模块的加载基址与保留缓冲都属于该进程的地址空间/资源，
 * 全局共享会导致 A 进程的符号泄漏给 B 进程、卸载后槽位无法回收。 */
static hepu_module_t g_module_slots[MAX_USER_PROCS][HEPU_MAX_MODULES];

/* ==========================================================================
 * V0.20.83: VA区间管理器 (独立模块)
 * 用户空间 [0x40000000, 0x80000000)
 * - 模块/库加载区间: [0x60000000, 0x70000000)
 * - 堆区间: 未来扩展
 * - 栈: 0x70000000 (固定)
 * ========================================================================== */
#define VA_REGION_MAX 16
typedef struct {
    uint64_t start;
    uint64_t end;
    int used;
} va_region_t;

/* V0.33: 用户窗口分区（契约，别随手改）
 *   [0x40000000,0x50000000)  原生程序区：Hepu 原生 ELF 按链接地址原样映射
 *   [0x50000000,0x60000000)  兼容程序区：Linux PIE 按加载偏置摆进来
 *   [0x60000000,0x70000000)  共享库区：DT_NEEDED 加载的 .so
 *   [0x70000000,...)         用户栈
 * 三个区间互不重叠，VA 区间管理器按区分配，互不串味。 */
#define VA_COMPAT_BASE 0x50000000ULL
#define VA_COMPAT_END  0x60000000ULL
#define VA_LIB_BASE    0x60000000ULL
#define VA_LIB_END     0x70000000ULL

/* V0.33 P1: 每进程一份 VA 区间表。
 * exec 在进程槽位创建之前就要为 PIE 预留基址，故另设 pending 表，
 * 进程创建成功后再移交到该进程的区间表。 */
static va_region_t g_va_regions[MAX_USER_PROCS][VA_REGION_MAX];
static va_region_t g_va_pending[VA_REGION_MAX];
#define VA_OWNER_PENDING (-1)

/* pid → 进程槽位；不存在返回 -1 */
static int user_proc_slot_by_pid(int pid)
{
    if (pid <= 0) return -1;
    for (int i = 0; i < MAX_USER_PROCS; i++) {
        if (user_procs[i].active && user_procs[i].pid == pid) return i;
    }
    return -1;
}

static va_region_t *va_table_for_owner(int owner)
{
    if (owner == VA_OWNER_PENDING) return g_va_pending;
    if (owner < 0 || owner >= MAX_USER_PROCS) return 0;
    return g_va_regions[owner];
}

/* 在 [pool_base, pool_end) 区间里分配 size 字节的 VA，返回起始地址 (0=失败)
 * owner: 进程槽位，或 VA_OWNER_PENDING（exec 创建进程前的临时预留） */
static uint64_t va_alloc_in(int owner, uint64_t pool_base, uint64_t pool_end, uint64_t size)
{
    va_region_t *regions = va_table_for_owner(owner);
    if (!regions || size == 0) return 0;
    size = (size + 0xFFFF) & ~0xFFFFULL;  /* 64K对齐 */
    if (size > pool_end - pool_base) return 0;

    for (uint64_t base = pool_base; base + size <= pool_end; base += 0x10000) {
        int conflict = 0;
        for (int i = 0; i < VA_REGION_MAX; i++) {
            if (!regions[i].used) continue;
            if (base < regions[i].end && base + size > regions[i].start) {
                conflict = 1; break;
            }
        }
        if (!conflict) {
            for (int i = 0; i < VA_REGION_MAX; i++) {
                if (!regions[i].used) {
                    regions[i].used = 1;
                    regions[i].start = base;
                    regions[i].end = base + size;
                    return base;
                }
            }
            return 0;  /* 表满 */
        }
    }
    return 0;
}

/* 分配 size 字节的共享库区 VA，返回起始地址 (0=失败) */
static uint64_t va_alloc(int owner, uint64_t size)
{
    return va_alloc_in(owner, VA_LIB_BASE, VA_LIB_END, size);
}

static void va_free(int owner, uint64_t base)
{
    va_region_t *regions = va_table_for_owner(owner);
    if (!regions) return;
    for (int i = 0; i < VA_REGION_MAX; i++) {
        if (regions[i].used && regions[i].start == base) {
            regions[i].used = 0;
            return;
        }
    }
}

/* 进程创建成功：把 exec 预留的 PIE 区间移交给该进程的区间表 */
static void user_va_adopt_pending(int pid)
{
    int slot = user_proc_slot_by_pid(pid);
    if (slot < 0) return;
    for (int p = 0; p < VA_REGION_MAX; p++) {
        if (!g_va_pending[p].used) continue;
        for (int i = 0; i < VA_REGION_MAX; i++) {
            if (!g_va_regions[slot][i].used) {
                g_va_regions[slot][i] = g_va_pending[p];
                g_va_pending[p].used = 0;
                break;
            }
        }
    }
}

/* 槽位释放：清空该进程的 VA 区间与模块表，回收模块保留的内核缓冲。
 * 页表的物理页由 user_free_aspace 统一回收，这里只清账本。 */
static void user_va_release_slot(int slot)
{
    if (slot < 0 || slot >= MAX_USER_PROCS) return;
    for (int i = 0; i < VA_REGION_MAX; i++) g_va_regions[slot][i].used = 0;
    for (int m = 0; m < HEPU_MAX_MODULES; m++) {
        if (!g_module_slots[slot][m].used) continue;
        if (g_module_slots[slot][m].kbuf) kfree(g_module_slots[slot][m].kbuf);
        g_module_slots[slot][m].kbuf = 0;
        g_module_slots[slot][m].used = 0;
    }
}

/* 在用户进程页表映射一段内存 */
static int module_map_pages(uint64_t pml4, uint64_t vaddr, const void *data, uint64_t size)
{
    extern uint64_t vmm64_phys_to_virt(uint64_t phys);
    uint64_t pages = (size + 4095) / 4096;
    const uint8_t *src = (const uint8_t *)data;
    for (uint64_t pg = 0; pg < pages; pg++) {
        uint64_t phys = pmm64_alloc_page();
        if (!phys) return -1;
        uint8_t *dst = (uint8_t *)vmm64_phys_to_virt(phys);
        for (int z = 0; z < 4096; z++) dst[z] = 0;
        uint64_t copy_len = 4096;
        if (pg * 4096 + 4096 > size) copy_len = size - pg * 4096;
        for (uint64_t j = 0; j < copy_len; j++) dst[j] = src[pg * 4096 + j];
        vmm64_map_page(pml4, vaddr + pg * 4096, phys,
                       PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);
    }
    return 0;
}

/* P1-7: 把一块已加载的镜像（解释器 ld.so）映射进指定进程的用户地址空间。
 * 与 module_map_pages 同样的页属性（可写、可执行、用户态可访问）。 */
int user_process_map_image(int pid, const void *data, uint64_t size, uint64_t vaddr)
{
    if (!data || !size) return -1;
    int idx = -1;
    for (int i = 0; i < MAX_USER_PROCS; i++) {
        if (user_procs[i].active && user_procs[i].pid == pid) { idx = i; break; }
    }
    if (idx < 0) return -1;
    return module_map_pages(user_procs[idx].page_table, vaddr, data, size);
}

/* 加载一个共享模块到用户进程, 返回基址 (0=失败) */
uint64_t hepu_module_load(int pid, const char *path)
{
    /* 找进程 */
    int idx = -1;
    for (int i = 0; i < MAX_USER_PROCS; i++) {
        if (user_procs[i].active && user_procs[i].pid == pid) { idx = i; break; }
    }
    if (idx < 0) return 0;
    /* 本进程的模块表视图（每进程独立，避免跨进程符号泄漏） */
    hepu_module_t *g_modules = g_module_slots[idx];
    uint64_t pml4 = user_procs[idx].page_table;

    /* 读文件 */
    extern void *kmalloc(uint32_t size);
    uint8_t *buf = (uint8_t *)kmalloc(65536);
    if (!buf) return 0;
    extern int ext2_64_read_file(const char *path, uint8_t *buf, int max);
    int fsize = ext2_64_read_file(path, buf, 65536);
    if (fsize < 64) { kfree(buf); return 0; }

    /* 解析ELF头计算VA需求 */
    uint64_t e_phoff = *(uint64_t*)(buf + 0x20);
    uint16_t e_phnum = *(uint16_t*)(buf + 0x38);
    uint16_t e_phesize = *(uint16_t*)(buf + 0x36);
    uint64_t map_start = 0, map_end = 0;
    uint64_t link_base = 0;
    for (int si = 0; si < e_phnum; si++) {
        uint8_t *ph = buf + e_phoff + si * e_phesize;
        uint32_t p_type = *(uint32_t*)(ph);
        if (p_type != 1) continue;
        uint64_t p_vaddr = *(uint64_t*)(ph + 0x10);
        uint64_t p_filesz = *(uint64_t*)(ph + 0x20);
        uint64_t seg_start = p_vaddr & ~0xFFFULL;
        uint64_t seg_end = (p_vaddr + p_filesz + 0xFFF) & ~0xFFFULL;
        if (seg_start < map_start || map_start == 0) map_start = seg_start;
        if (seg_end > map_end) map_end = seg_end;
        if (link_base == 0) link_base = p_vaddr;
    }
    uint64_t total = map_end - map_start;

    /* V0.20.83: 使用VA区间管理器分配 */
    uint64_t base = va_alloc(idx, total);
    if (!base) { kfree(buf); return 0; }
    /* P1: 账本之外再核对页表，区间内不得已有映射 */
    for (uint64_t v = base; v < base + total; v += 4096) {
        if (vmm64_page_present(pml4, v)) {
            vga_puts("[MODULE] VA range already mapped, reject\n");
            va_free(idx, base);
            kfree(buf);
            return 0;
        }
    }

    /* P1: 记入本域内存配额，超配额/水位不足则拒绝加载 */
    uint64_t module_charge_pages = (total + 4095) / 4096;
    if (user_process_mem_charge_slot(idx, module_charge_pages) != 0) {
        vga_puts("[MODULE] Domain memory quota exceeded, reject\n");
        va_free(idx, base);
        kfree(buf);
        return 0;
    }

    /* 映射PT_LOAD段到 base + (p_vaddr - map_start) */
    uint64_t module_map_lo = 0, module_map_hi = 0, module_mapped_pages = 0;
    for (int si = 0; si < e_phnum; si++) {
        uint8_t *ph = buf + e_phoff + si * e_phesize;
        uint32_t p_type = *(uint32_t*)(ph);
        if (p_type != 1) continue;
        uint64_t p_offset = *(uint64_t*)(ph + 0x08);
        uint64_t p_vaddr = *(uint64_t*)(ph + 0x10);
        uint64_t p_filesz = *(uint64_t*)(ph + 0x20);
        if (p_filesz == 0) continue;
        /* 逐页拷贝 */
        uint64_t start_page = p_vaddr & ~0xFFFULL;
        uint64_t end_page = (p_vaddr + p_filesz + 0xFFF) & ~0xFFFULL;
        extern uint64_t vmm64_phys_to_virt(uint64_t phys);
        for (uint64_t pg = start_page; pg < end_page; pg += 4096) {
            uint64_t phys = pmm64_alloc_page();
            if (!phys) goto module_map_fail;
            uint8_t *dst = (uint8_t *)vmm64_phys_to_virt(phys);
            for (int z = 0; z < 4096; z++) dst[z] = 0;
            uint64_t seg_start = p_vaddr;
            uint64_t seg_end = p_vaddr + p_filesz;
            uint64_t copy_start = (pg > seg_start) ? pg : seg_start;
            uint64_t copy_end = (pg + 4096 < seg_end) ? pg + 4096 : seg_end;
            if (copy_start < copy_end) {
                uint64_t off_in_file = p_offset + (copy_start - p_vaddr);
                uint64_t off_in_page = copy_start - pg;
                uint64_t n = copy_end - copy_start;
                for (uint64_t j = 0; j < n; j++)
                    dst[off_in_page + j] = buf[off_in_file + j];
            }
            if (vmm64_map_page(pml4, base + (pg - map_start), phys,
                               PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER) != 0) {
                pmm64_free_page(phys);
                goto module_map_fail;
            }
            {
                uint64_t va = base + (pg - map_start);
                if (!module_map_lo) module_map_lo = va;
                module_map_hi = va + 4096;
                module_mapped_pages++;
            }
        }
    }
    goto module_map_done;

module_map_fail: ;
    /* P1: 映射中途失败必须回滚已映射页与配额，否则区间/计数永久泄漏 */
    {
        extern void vmm64_unmap_page(uint64_t pml4, uint64_t virt);
        extern uint64_t vmm64_virt_to_phys(uint64_t pml4, uint64_t virt);
        uint64_t freed = 0;
        for (uint64_t va = module_map_lo; va < module_map_hi; va += 4096) {
            uint64_t phys = vmm64_virt_to_phys(pml4, va);
            vmm64_unmap_page(pml4, va);
            if (phys) { pmm64_free_page(phys & ~0xFFFULL); freed++; }
        }
        user_process_mem_uncharge_slot(idx, module_charge_pages);
        va_free(idx, base);
        kfree(buf);
        return 0;
    }

module_map_done: ;
    /* 配额按实际映射页结算：预记的是整个 VA 区间，未映射的空洞必须退还 */
    if (module_charge_pages > module_mapped_pages)
        user_process_mem_uncharge_slot(idx, module_charge_pages - module_mapped_pages);
    /* 登记模块 */
    for (int m = 0; m < HEPU_MAX_MODULES; m++) {
        if (!g_modules[m].used) {
            g_modules[m].used = 1;
            g_modules[m].base = base;
            g_modules[m].size = total;
            g_modules[m].refcount = 1;
            g_modules[m].kbuf = buf;  /* 保留内核缓冲 */
            g_modules[m].dynsym = 0;
            g_modules[m].dynstr = 0;
            g_modules[m].syment = 24;
            g_modules[m].strsz = 0;
            g_modules[m].link_base = link_base;
            /* 解析PT_DYNAMIC找symtab/strtab */
            for (int si = 0; si < e_phnum; si++) {
                uint8_t *ph = buf + e_phoff + si * e_phesize;
                uint32_t p_type = *(uint32_t*)(ph);
                if (p_type != 2) continue;  /* PT_DYNAMIC */
                uint64_t p_offset = *(uint64_t*)(ph + 0x08);
                uint64_t p_filesz = *(uint64_t*)(ph + 0x20);
                for (uint64_t d = 0; d + 16 <= p_filesz; d += 16) {
                    uint64_t tag = *(uint64_t*)(buf + p_offset + d);
                    uint64_t val = *(uint64_t*)(buf + p_offset + d + 8);
                    if (tag == 0) break;
                    if (tag == 5) g_modules[m].dynstr = val;       /* DT_STRTAB */
                    else if (tag == 6) g_modules[m].dynsym = val;  /* DT_SYMTAB */
                    else if (tag == 10) g_modules[m].strsz = val;  /* DT_STRSZ */
                    else if (tag == 11) g_modules[m].syment = val; /* DT_SYMENT */
                }
                break;
            }
            /* V0.20.82: 处理模块自身的RELA重定位 */
            for (int si = 0; si < e_phnum; si++) {
                uint8_t *ph = buf + e_phoff + si * e_phesize;
                if (*(uint32_t*)ph != 2) continue;
                uint64_t p_offset = *(uint64_t*)(ph + 0x08);
                uint64_t p_filesz = *(uint64_t*)(ph + 0x20);
                uint64_t rela = 0, relasz = 0;
                for (uint64_t d = 0; d + 16 <= p_filesz; d += 16) {
                    uint64_t tag = *(uint64_t*)(buf + p_offset + d);
                    uint64_t val = *(uint64_t*)(buf + p_offset + d + 8);
                    if (tag == 0) break;
                    if (tag == 7) rela = val;        /* DT_RELA */
                    else if (tag == 8) relasz = val;  /* DT_RELASZ */
                }
                if (rela && relasz) {
                    int n = relasz / 24;
                    for (int r = 0; r < n; r++) {
                        uint64_t r_offset = *(uint64_t*)(buf + rela + r*24);
                        uint64_t r_info = *(uint64_t*)(buf + rela + r*24 + 8);
                        int64_t r_addend = *(int64_t*)(buf + rela + r*24 + 16);
                        uint32_t sym_idx = r_info >> 32;
                        uint32_t rtype = r_info & 0xFFFFFFFF;
                        /* 目标VA = base + (r_offset - link_base) */
                        uint64_t target = base + (r_offset - link_base);
                        uint64_t resolved = 0;
                        if (rtype == 1 || rtype == 6) {  /* R_X86_64_64 / GLOB_DAT */
                            /* 从本模块symtab找符号 */
                            uint64_t sym_off = g_modules[m].dynsym + sym_idx * g_modules[m].syment;
                            uint64_t st_value = *(uint64_t*)(buf + sym_off + 8);
                            resolved = base + (st_value - link_base) + r_addend;
                        } else if (rtype == 7) {  /* R_X86_64_RELATIVE */
                            resolved = base + r_addend;
                        }
                        if (resolved) {
                            user_process_write_mem(pid, target, &resolved, 8);
                        }
                    }
                    vga_puts("[MODULE] Applied ");
                    { char b[8]; int c=n,i=0; if(c==0)vga_puts("0");
                      else { do{b[i++]=c%10+'0';}while((c/=10)>0);
                        while(i>0){ char t[2]={b[--i],0}; vga_puts(t); } } }
                    vga_puts(" internal relocs\n");
                }
                break;
            }
            int k = 0; while (path[k] && k < 31) { g_modules[m].name[k]=path[k]; k++; }
            g_modules[m].name[k] = 0;
            break;
        }
    }

    vga_puts("[MODULE] Loaded "); vga_puts(path);
    vga_puts(" @ ");
    { char h[19]; char hex[]="0123456789ABCDEF"; uint64_t a=base;
      h[0]='0';h[1]='x'; for(int i=0;i<16;i++){h[17-i]=hex[a&0xF];a>>=4;} h[18]=0;
      vga_puts(h); }
    vga_puts("\n");

    /* buf保留在g_modules[m].kbuf用于符号查找, 不free */
    return base;
}

/* 卸载模块: unmap所有页 + 释放物理页 */
int hepu_module_unload(int pid, uint64_t base)
{
    /* 找进程页表 */
    int idx = -1;
    for (int i = 0; i < MAX_USER_PROCS; i++) {
        if (user_procs[i].active && user_procs[i].pid == pid) { idx = i; break; }
    }
    if (idx < 0) return -1;
    /* 本进程的模块表视图 */
    hepu_module_t *g_modules = g_module_slots[idx];
    uint64_t pml4 = user_procs[idx].page_table;

    for (int m = 0; m < HEPU_MAX_MODULES; m++) {
        if (g_modules[m].used && g_modules[m].base == base) {
            g_modules[m].refcount--;
            if (g_modules[m].refcount <= 0) {
                /* 真正unmap */
                if (pml4) {
                    extern void vmm64_unmap_page(uint64_t pml4, uint64_t virt);
                    extern uint64_t vmm64_virt_to_phys(uint64_t pml4, uint64_t virt);
                    extern void pmm64_free_page(uint64_t phys);
                    uint64_t pages = (g_modules[m].size + 4095) / 4096;
                    uint64_t freed = 0;
                    for (uint64_t pg = 0; pg < pages; pg++) {
                        uint64_t va = base + pg * 4096;
                        uint64_t phys = vmm64_virt_to_phys(pml4, va);
                        vmm64_unmap_page(pml4, va);
                        if (phys) { pmm64_free_page(phys & ~0xFFFULL); freed++; }
                    }
                    user_process_mem_uncharge_slot(idx, freed);
                    /* TLB刷新: 写CR3 */
                    uint64_t cr3;
                    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
                    __asm__ volatile("mov %0, %%cr3" : : "r"(cr3) : "memory");
                }
                if (g_modules[m].kbuf) { kfree(g_modules[m].kbuf); g_modules[m].kbuf = 0; }
                va_free(idx, base);
                g_modules[m].used = 0;
                vga_puts("[MODULE] Unloaded @ ");
                { char h[19]; char hex[]="0123456789ABCDEF"; uint64_t a=base;
                  h[0]='0';h[1]='x'; for(int i=0;i<16;i++){h[17-i]=hex[a&0xF];a>>=4;} h[18]=0;
                  vga_puts(h); }
                vga_puts("\n");
            }
            return 0;
        }
    }
    return -1;
}

/* V0.20.80: 在指定进程的已加载模块中按名查找符号, 返回用户空间地址 */
uint64_t hepu_module_lookup_symbol(int pid, const char *symname)
{
    if (!symname) return 0;
    int idx = user_proc_slot_by_pid(pid);
    if (idx < 0) return 0;
    hepu_module_t *g_modules = g_module_slots[idx];
    for (int m = 0; m < HEPU_MAX_MODULES; m++) {
        if (!g_modules[m].used || !g_modules[m].kbuf) continue;
        uint8_t *kbuf = g_modules[m].kbuf;
        uint64_t symtab = g_modules[m].dynsym;
        uint64_t strtab = g_modules[m].dynstr;
        uint64_t syment = g_modules[m].syment ? g_modules[m].syment : 24;
        if (!symtab || !strtab) continue;
        /* 最多扫256个符号 */
        for (int s = 0; s < 256; s++) {
            uint64_t off = symtab + s * syment;
            if (off + syment > 65536) break;
            uint32_t st_name = *(uint32_t*)(kbuf + off);
            uint64_t st_value = *(uint64_t*)(kbuf + off + 8);
            if (st_name == 0) continue;
            if (g_modules[m].strsz && st_name >= g_modules[m].strsz) continue;
            const char *name = (const char *)(kbuf + strtab + st_name);
            /* 比较字符串 */
            int match = 1;
            for (int i = 0; name[i] && symname[i]; i++) {
                if (name[i] != symname[i]) { match = 0; break; }
            }
            if (match && name[0] == symname[0]) {
                /* 完整比较 */
                int ok = 1;
                for (int i = 0; ; i++) {
                    if (name[i] != symname[i]) { ok = 0; break; }
                    if (name[i] == 0) break;
                }
                if (ok && st_value != 0) {
                    uint64_t addr = st_value - g_modules[m].link_base + g_modules[m].base;
                    vga_puts("[SYM] Found "); vga_puts(symname);
                    vga_puts(" @ ");
                    { char h[19]; char hex[]="0123456789ABCDEF"; uint64_t a=addr;
                      h[0]='0';h[1]='x'; for(int i=0;i<16;i++){h[17-i]=hex[a&0xF];a>>=4;} h[18]=0;
                      vga_puts(h); }
                    vga_puts("\n");
                    return addr;
                }
            }
        }
    }
    vga_puts("[SYM] NOT FOUND: "); vga_puts(symname); vga_puts("\n");
    return 0;
}

/* V0.20.80: 处理主程序的JUMP_SLOT重定位 */
int hepu_dynlink_relocate(int pid, uint8_t *exe_buf, uint64_t exe_size)
{
    if (!exe_buf || exe_size < 64) return -1;
    /* 找PT_DYNAMIC */
    uint64_t e_phoff = *(uint64_t*)(exe_buf + 0x20);
    uint16_t e_phnum = *(uint16_t*)(exe_buf + 0x38);
    uint16_t e_phesize = *(uint16_t*)(exe_buf + 0x36);
    uint64_t jmprel = 0, pltrelsz = 0, dynsym = 0, dynstr = 0;
    /* 先算load bias: 从最低p_vaddr的PT_LOAD段 */
    uint64_t bias = 0;
    for (int si = 0; si < e_phnum; si++) {
        uint8_t *ph = exe_buf + e_phoff + si * e_phesize;
        if (*(uint32_t*)ph != 1) continue;  /* PT_LOAD */
        uint64_t poff = *(uint64_t*)(ph + 0x08);
        uint64_t pva = *(uint64_t*)(ph + 0x10);
        if (bias == 0 || pva < bias + poff) {
            bias = pva - poff;
        }
    }
    vga_puts("[DYNLINK] load bias=");
    { char h[19]; char hex[]="0123456789ABCDEF"; uint64_t a=bias;
      h[0]='0';h[1]='x'; for(int i=0;i<16;i++){h[17-i]=hex[a&0xF];a>>=4;} h[18]=0; vga_puts(h); }
    vga_puts("\n");
    /* 找PT_DYNAMIC */
    for (int si = 0; si < e_phnum; si++) {
        uint8_t *ph = exe_buf + e_phoff + si * e_phesize;
        if (*(uint32_t*)ph != 2) continue;  /* PT_DYNAMIC */
        uint64_t p_offset = *(uint64_t*)(ph + 0x08);
        uint64_t p_filesz = *(uint64_t*)(ph + 0x20);
        /* Pass 1: 先找dynstr */
        for (uint64_t d = 0; d + 16 <= p_filesz; d += 16) {
            uint64_t tag = *(uint64_t*)(exe_buf + p_offset + d);
            uint64_t val = *(uint64_t*)(exe_buf + p_offset + d + 8);
            if (tag == 0) break;
            if (tag == 6) dynsym = val - bias;
            else if (tag == 5) dynstr = val - bias;
        }
        /* Pass 2: 处理DT_NEEDED */
        for (uint64_t d = 0; d + 16 <= p_filesz; d += 16) {
            uint64_t tag = *(uint64_t*)(exe_buf + p_offset + d);
            uint64_t val = *(uint64_t*)(exe_buf + p_offset + d + 8);
            if (tag == 0) break;
            if (tag == 1 && dynstr) {  /* DT_NEEDED */
                const char *libname = (const char *)(exe_buf + dynstr + val);
                vga_puts("[DYNLINK] Needs: "); vga_puts(libname); vga_puts("\n");
                char libpath[64]; int k = 0;
                libpath[k++]='/';libpath[k++]='l';libpath[k++]='i';libpath[k++]='b';libpath[k++]='/';
                for (int j = 0; libname[j] && k < 60; j++) {
                    if (libname[j] == '/') { k = 5; continue; }
                    libpath[k++] = libname[j];
                }
                libpath[k] = 0;
                hepu_module_load(pid, libpath);
            }
            if (tag == 0x17) jmprel = val - bias;
            else if (tag == 0x2) pltrelsz = val;
        }
        break;
    }
    if (!jmprel || !pltrelsz || !dynsym || !dynstr) {
        vga_puts("[DYNLINK] No JMPREL/PLTRELSZ, skipping\n");
        return 0;
    }
    vga_puts("[DYNLINK] jmprel="); {
        char h[19]; char hex[]="0123456789ABCDEF"; uint64_t a=jmprel;
        h[0]='0';h[1]='x'; for(int i=0;i<16;i++){h[17-i]=hex[a&0xF];a>>=4;} h[18]=0; vga_puts(h); }
    vga_puts(" dynsym="); {
        char h[19]; char hex[]="0123456789ABCDEF"; uint64_t a=dynsym;
        h[0]='0';h[1]='x'; for(int i=0;i<16;i++){h[17-i]=hex[a&0xF];a>>=4;} h[18]=0; vga_puts(h); }
    vga_puts(" dynstr="); {
        char h[19]; char hex[]="0123456789ABCDEF"; uint64_t a=dynstr;
        h[0]='0';h[1]='x'; for(int i=0;i<16;i++){h[17-i]=hex[a&0xF];a>>=4;} h[18]=0; vga_puts(h); }
    vga_puts("\n");
    int count = pltrelsz / 24;  /* sizeof(Elf64_Rela) */
    vga_puts("[DYNLINK] Processing ");
    vga_puts(" JUMP_SLOT relocs\n");
    for (int i = 0; i < count; i++) {
        uint64_t rel_off = jmprel + i * 24;
        uint64_t r_offset = *(uint64_t*)(exe_buf + rel_off);
        uint64_t r_info = *(uint64_t*)(exe_buf + rel_off + 8);
        uint64_t r_addend = *(int64_t*)(exe_buf + rel_off + 16);
        uint32_t sym_idx = r_info >> 32;
        /* 取符号名 */
        uint64_t sym_off = dynsym + sym_idx * 24;
        uint32_t st_name = *(uint32_t*)(exe_buf + sym_off);
        const char *symname = (const char *)(exe_buf + dynstr + st_name);
        vga_puts("[DYNLINK] sym_idx=");
        { char h[19]; char hex[]="0123456789ABCDEF"; uint64_t a=sym_idx;
          h[0]='0';h[1]='x'; for(int i=0;i<16;i++){h[17-i]=hex[a&0xF];a>>=4;} h[18]=0; vga_puts(h); }
        vga_puts(" st_name=");
        { char h[19]; char hex[]="0123456789ABCDEF"; uint64_t a=st_name;
          h[0]='0';h[1]='x'; for(int i=0;i<16;i++){h[17-i]=hex[a&0xF];a>>=4;} h[18]=0; vga_puts(h); }
        vga_puts(" name='"); vga_puts(symname); vga_puts("'\n");
        uint64_t addr = hepu_module_lookup_symbol(pid, symname);
        if (addr) {
            addr += r_addend;
            user_process_write_mem(pid, r_offset, &addr, 8);
            vga_puts("[DYNLINK]   -> patched GOT\n");
        } else {
            vga_puts("[DYNLINK]   -> FAILED to resolve!\n");
        }
    }
    return 0;
}

/* 切换到用户态进程 */
int user_process_switch(int pid)
{
    int idx = -1;
    for (int i = 0; i < MAX_USER_PROCS; i++) {
        if (user_procs[i].active && user_procs[i].pid == pid) {
            idx = i;
            break;
        }
    }
    if (idx < 0) return -1;
    if (!user_procs[idx].page_table) return -1;   /* 地址空间已回收（进程已退出） */

    current_user_proc = idx;
    user_process_t *proc = &user_procs[idx];
    extern uint64_t g_kernel_tick64;
    proc->start_tick = g_kernel_tick64;

    vga_puts("[USER64] Switching to user process ");
    vga_puts(proc->name);
    vga_puts(" (CR3=");
    char hex[] = "0123456789ABCDEF";
    char buf[19];
    uint64_t addr = proc->page_table;
    buf[0]='0'; buf[1]='x';
    for (int i = 0; i < 16; i++) { buf[17-i] = hex[addr & 0xF]; addr >>= 4; }
    buf[18] = 0;
    vga_puts(buf);
    vga_puts(")\n");

    /* 保存返回上下文：用户态退出后，由全局汇编 user_return_trampoline 恢复栈并
     * 调用 user_return_cleanup，再返回本函数的调用者。
     * 不使用 GCC labels-as-values（GCC 11 在本函数结构下会把标签地址算错）。 */
    extern void user_return_trampoline(void);
    g_user_saved_rip = (uint64_t)user_return_trampoline;
    __asm__ volatile("movq %%rsp, %0" : "=m"(g_user_saved_rsp));
    __asm__ volatile("movq %%cr3, %0" : "=r"(g_user_saved_cr3));
    g_user_in_user_mode = 1;

    /* 切换到用户页表 */
    __asm__ volatile("mov %0, %%cr3" : : "r"(proc->page_table));

    /* V0.21.2: 恢复FS_BASE (中断处理可能改了) */
    if (proc->fsbase) {
        __asm__ volatile("wrmsr" : : "c"(0xC0000100),
            "a"((uint32_t)proc->fsbase), "d"((uint32_t)(proc->fsbase>>32)));
    }

    /* 切换到用户态（IRETQ） */
    user_enter(proc->rip, proc->rsp, proc->rflags);

    /* 不会到达：user_enter 是 noreturn。退出路径见 user_return_trampoline。 */
    return 0;
}

/* V0.20.85: futex 实现 */
#define FUTEX_WAIT 0
#define FUTEX_WAKE 1

/* 当前进程在futex地址上等待, 返回0=已阻塞, 非0=值不匹配 */
int hepu_futex_wait(uint64_t uaddr, uint32_t val) {
    int idx = current_user_proc;
    if (idx < 0 || idx >= MAX_USER_PROCS) return -1;
    /* 校验用户地址: 不合法直接返回-EFAULT, 防止ring0解引用野指针 */
    {
        extern int verify_user_read(const void *, size_t);
        if (!verify_user_read((const void *)uaddr, sizeof(uint32_t))) return -14;
    }
    /* 读用户空间的值 */
    uint32_t cur;
    {
        uint64_t saved_cr3;
        __asm__ volatile("mov %%cr3, %0" : "=r"(saved_cr3));
        int ac_prev = hepu_user_access_enter();
        cur = *(volatile uint32_t *)uaddr;
        hepu_user_access_exit(ac_prev);
    }
    if (cur != val) return -11;  /* EAGAIN */
    user_procs[idx].blocked = 1;
    user_procs[idx].futex_wait_addr = uaddr;
    return 0;
}

/* 唤醒在uaddr上等待的n个进程, 返回唤醒数 */
int hepu_futex_wake(uint64_t uaddr, int n) {
    int woke = 0;
    for (int i = 0; i < MAX_USER_PROCS; i++) {
        if (user_procs[i].active && user_procs[i].blocked &&
            user_procs[i].futex_wait_addr == uaddr) {
            user_procs[i].blocked = 0;
            user_procs[i].futex_wait_addr = 0;
            woke++;
            if (woke >= n) break;
        }
    }
    return woke;
}

/* 用户进程退出后（CR3 已恢复为内核页表、栈已恢复）的清理逻辑。
 * 由全局汇编 user_return_trampoline 在 user_process_switch 的栈帧上调用，
 * 返回值即 user_process_switch 的返回值。 */
int user_return_cleanup(void)
{
    g_user_in_user_mode = 0;
    /* V0.13.46: 强制终止清理（未响应进程被IRQ截获后在此释放） */
    if (current_user_proc >= 0 && user_procs[current_user_proc].kill_requested) {
        if (user_procs[current_user_proc].faulted) {
            /* V0.33: 因异常终止 —— 打印异常向量，便于定位是哪个程序/哪条指令炸的 */
            vga_puts("[USER64] Process PID ");
            vga_put_hex((uint64_t)user_procs[current_user_proc].pid);
            vga_puts(" exited due to exception, vector=");
            vga_put_hex(user_procs[current_user_proc].fault_vector);
            vga_puts("\n");
        } else {
            vga_puts("[USER64] Terminated unresponsive process PID ");
            vga_put_hex((uint64_t)user_procs[current_user_proc].pid);
            vga_puts("\n");
        }
        user_process_release(current_user_proc);
    }
    /* 进程正常退出：回收地址空间、内核栈与 fd 表，并释放槽位（active=0）以便后续 exec 复用。
     * 修复 SEC-09：此前只回收地址空间却保留 active=1，连续跑满 MAX_USER_PROCS(8) 个程序后
     * 无空闲槽，无法再启动新程序。任务管理器只列 active 进程，退出后即从列表移除（符合常见 OS 行为）。 */
    if (current_user_proc >= 0 && current_user_proc < MAX_USER_PROCS) user_process_release(current_user_proc);
    vga_puts("[USER64] Returned to kernel from user mode\n");
    return 0;
}

/* 从用户态退出，返回到Shell（sys_exit 调用）。
 * 只恢复内核 CR3，然后跳到全局 trampoline；栈恢复与 epilogue 由 trampoline 完成。 */
void user_exit_to_kernel(void)
{
    if (!g_user_in_user_mode || g_user_saved_rsp == 0) {
        return;  /* 不在用户态，直接返回 */
    }

    vga_puts("[USER64] User process exiting, returning to kernel...\n");

    /* 恢复内核页表 */
    __asm__ volatile("mov %0, %%cr3" : : "r"(g_user_saved_cr3));

    /* 跳到全局 trampoline（它会恢复栈、调用清理、返回调用者） */
    extern void user_return_trampoline(void);
    user_return_trampoline();

    /* 不会到达这里 */
}

/* 获取当前用户进程 */
user_process_t *user_process_current(void)
{
    if (current_user_proc < 0) return NULL;
    return &user_procs[current_user_proc];
}

/* 列出用户进程 */
int user_process_list(user_process_t *list, int max)
{
    int count = 0;
    for (int i = 0; i < MAX_USER_PROCS && count < max; i++) {
        if (user_procs[i].active) {
            list[count++] = user_procs[i];
        }
    }
    return count;
}

/* 示例用户态程序：简单循环（不使用syscall，先测试切换） */
void user_program_hello(void)
{
    /* 用户态代码：先测试基本切换，不使用syscall */
    volatile int i = 0;
    while (1) {
        i++;
        if (i > 100000000) {
            /* 循环足够多次后，通过串口输出标记 */
            /* 暂时不做任何事，保持循环 */
            i = 0;
        }
    }
}

/* 用户态syscall测试程序（在user_app_test.c中定义） */
extern void user_program_syscall_test(void);

/* ELF64加载器外部声明 */
extern int elf64_verify(void *elf_data);
extern uint64_t elf64_get_entry(void *elf_data);
extern void *elf64_load(void *elf_data, uint64_t size);

/* ext2文件读取 */
extern int ext2_64_read_file(const char *path, uint8_t *buf, int max_size);

/* 从ext2加载ELF用户程序并执行 */
int user_process_exec(const char *path)
{
    /* V0.33: ABI 不再是全局开关。
     * 旧实现无条件置 g_running_linux_elf=1，于是**所有**用户程序（包括 Hepu
     * 原生程序）都走 Linux 号表翻译：原生 read(2) 被翻译成 Linux open(2)、
     * 原生 open(3) 被翻译成 close(4)……原生 ABI 被兼容层绑架了。
     * 现在按 ELF 自身判定（见 elf64_is_linux_abi），并记到**进程**的 env_type 上，
     * 符合 PHILOSOPHY.md「原生 ABI 优先，兼容层是附加层」。 */
    vga_puts("[USER64] exec: ");
    vga_puts(path);
    vga_puts("\n");

    /* 1. 从ext2读取ELF文件 - V0.20.15: 动态分配, 支持大ELF
     *
     * V0.33 修正两件事：
     *  a) 缓冲区按**文件真实大小**分配，而不是固定 2MB。
     *     旧实现每次 exec 都从 8MB 内核堆扣 2MB：启动自检连跑十几个用户程序时
     *     内核堆直接耗尽（[KHEAP] OOM: req=2097152），桌面起来之后任何用户程序
     *     都跑不起来。2MB 是为了迁就 256KB 静态缓冲上限的历史包袱
     *     （VERSIONING 0.20.7），现在有动态分配就不该再付这个代价。
     *  b) 两条错误返回路径漏掉了 kfree(elf_buffer)，每次泄漏 2MB。
     */
    extern void *kmalloc(uint32_t size);
    extern void kfree(void *ptr);
    /* 先读 ELF 头拿到真实大小, 再分配 */
    uint8_t elf_hdr[64];
    int hdr_size = ext2_64_read_file(path, elf_hdr, sizeof(elf_hdr));
    if (hdr_size < 64) {
        vga_puts("[USER64] exec ERROR: Cannot read file\n");
        return -1;
    }
    /* 取文件真实大小（stat 拿不到就退回一个保守上限） */
    uint32_t file_size_hint = 0;
    {
        extern int ext2_64_stat(const char *path, uint32_t *size, uint8_t *isdir);
        uint8_t isdir = 0;
        if (ext2_64_stat(path, &file_size_hint, &isdir) != 0 || isdir ||
            file_size_hint < 64 || file_size_hint > (16u << 20)) {
            file_size_hint = 0;   /* 0 = 未知，走兜底 */
        }
    }
    uint32_t elf_size = file_size_hint ? file_size_hint : (2 * 1024 * 1024);
    uint8_t *elf_buffer = (uint8_t *)kmalloc(elf_size);
    if (!elf_buffer) {
        vga_puts("[USER64] exec ERROR: kmalloc failed\n");
        return -1;
    }
    /* 把已读的头拷进去 */
    for (int i = 0; i < hdr_size; i++) elf_buffer[i] = elf_hdr[i];
    int file_size = ext2_64_read_file(path, elf_buffer, elf_size);
    if (file_size < 64) {
        vga_puts("[USER64] exec ERROR: Cannot read file\n");
        kfree(elf_buffer);
        return -1;
    }

    vga_puts("[USER64] exec: read ");
    char buf[16];
    int n = file_size, i = 0;
    char tmp[16];
    do { tmp[i++] = n % 10 + '0'; } while ((n /= 10) > 0);
    int j = 0;
    while (i > 0) buf[j++] = tmp[--i];
    buf[j] = 0;
    vga_puts(buf);
    vga_puts(" bytes\n");

    /* 2. 验证ELF64 */
    if (elf64_verify(elf_buffer) != 0) {
        vga_puts("[USER64] exec ERROR: Invalid ELF64\n");
        kfree(elf_buffer);   /* V0.33: 补漏（原来这条路径泄漏整个缓冲区） */
        return -1;
    }

    /* 3. 获取入口点 */
    uint64_t entry = elf64_get_entry(elf_buffer);
    vga_puts("[USER64] exec: entry=");
    char hex[] = "0123456789ABCDEF";
    char hbuf[19];
    uint64_t addr = entry;
    hbuf[0]='0'; hbuf[1]='x';
    for (int i = 0; i < 16; i++) { hbuf[17-i] = hex[addr & 0xF]; addr >>= 4; }
    hbuf[18] = 0;
    vga_puts(hbuf);
    vga_puts("\n");

    /* 4. 判定 ABI 并选择映射基址（V0.33）
     *
     * 三种情况：
     *  a) Hepu 原生 ELF：链接在 [0x40000000,0x50000000)，按链接地址原样映射，
     *     走**原生**系统调用号表。
     *  b) Linux PIE（ET_DYN）：位置无关，可以在用户窗口里任选基址。
     *     分配到「兼容程序区」[0x50000000,0x60000000)，用加载偏置把镜像摆进去，
     *     入口点与 R_X86_64_RELATIVE 一起修正。走 **Linux** 系统调用号表。
     *  c) Linux ET_EXEC（非 PIE）：链接在 0x400000 这类窗口外地址。
     *     这类二进制的绝对地址已经烧进代码和数据里，**搬不动**
     *     （搬过去必然跑飞），所以明确拒绝并给出可执行的建议，
     *     而不是丢一句「code range outside user window」让人猜。
     */
    int is_linux_abi = elf64_is_linux_abi(elf_buffer, file_size);
    uint64_t link_base = elf64_link_base(elf_buffer, file_size);
    uint64_t map_base = 0;           /* 0 = 按链接地址映射 */
    int is_pie = 0;
    {
        extern uint16_t elf64_get_type(void *elf_data);
        is_pie = (elf64_get_type(elf_buffer) == 3 /*ET_DYN*/);
    }

    /* V0.54.6（P1-7）: PT_INTERP —— 用户态动态链接器。
     *
     * 内核只负责两件事：把主程序和解释器（ld.so）各自按偏置摆进用户地址
     * 空间，并把 System V 初始栈里的 AT_BASE/AT_ENTRY/AT_PHDR 填对。
     * DT_NEEDED、符号查找、重定位、TLS 初始化全部由解释器自己完成 ——
     * 这正是 Linux 的内核/ld.so 分工，也是能直接吃 glibc 动态二进制的唯一
     * 现实路径（在用户态重新实现整个 glibc 链接器不现实）。 */
    int has_interp = elf64_has_interp(elf_buffer, file_size);
    const char *interp_path = NULL;
    void *interp_loaded = NULL;
    uint64_t interp_load_size = 0, interp_code_vaddr = 0, interp_entry_vaddr = 0;
    if (has_interp) {
        interp_path = elf64_find_interp(elf_buffer, file_size);
        if (!interp_path || interp_path[0] != '/') {
            vga_puts("[USER64] exec ERROR: PT_INTERP path missing/invalid\n");
            kfree(elf_buffer);
            return -1;
        }
        vga_puts("[USER64] exec: dynamic ELF, interpreter=");
        vga_puts(interp_path);
        vga_puts("\n");
    }

    if (is_linux_abi && link_base && link_base < UWIN_START) {
        /* 链接在用户窗口之外 */
        if (!is_pie) {
            vga_puts("[USER64] exec ERROR: Linux ET_EXEC linked at ");
            vga_put_hex(link_base);
            vga_puts("\n");
            vga_puts("[USER64]   outside Hepu user window [0x40000000,0x80000000).\n");
            vga_puts("[USER64]   ET_EXEC absolute addresses are baked in - cannot be rebased.\n");
            vga_puts("[USER64]   Rebuild as PIE (-fPIE / -static-pie) to run on Hepu.\n");
            kfree(elf_buffer);
            return -1;
        }
    }

    if (is_pie) {
        /* PIE：从兼容程序区分配一个基址 */
        uint64_t need = elf64_image_size(elf_buffer, file_size);
        if (!need) {
            vga_puts("[USER64] exec ERROR: bad PIE layout\n");
            kfree(elf_buffer);
            return -1;
        }
        map_base = va_alloc_in(VA_OWNER_PENDING, VA_COMPAT_BASE, VA_COMPAT_END, need);
        if (!map_base) {
            vga_puts("[USER64] exec ERROR: no VA space for PIE image\n");
            kfree(elf_buffer);
            return -1;
        }
        vga_puts("[USER64] PIE load bias: base=");
        vga_put_hex(map_base);
        vga_puts(" size=");
        vga_put_hex(need);
        vga_puts("\n");
    }

    /* 加载ELF到内核内存（带偏置） */
    void *loaded = elf64_load_biased(elf_buffer, file_size, map_base);
    if (!loaded) {
        vga_puts("[USER64] exec ERROR: ELF load failed\n");
        if (map_base) va_free(VA_OWNER_PENDING, map_base);
        kfree(elf_buffer);
        return -1;
    }
    /* V0.20.80: elf_buffer保留到重定位完成后再free */

    /* 5. 创建用户进程（真正的用户空间映射）
     * 将ELF内容映射到用户地址空间0x400000
     * 入口点：如果e_entry < 0x400000，则为相对偏移，否则为绝对地址
     */
    vga_puts("[USER64] exec: ELF loaded, mapping to user space...\n");

    /* V0.20.78: 直接映射到ELF原始vaddr, 不做偏移 — 修复绝对地址引用
     * V0.33: 偏置加载时 g_elf_last_code_start 已经是修正后的映射基址，
     *         g_elf_last_entry 是修正后的入口点。 */
    extern uint64_t g_elf_last_code_start;
    extern uint64_t g_elf_last_load_size;
    extern uint64_t g_elf_last_entry;
    uint64_t code_vaddr = g_elf_last_code_start;
    uint64_t entry_vaddr = g_elf_last_entry ? g_elf_last_entry : entry;

    /* V0.33 修复：只复制**映像实际大小**，不能拿文件大小当映像大小。
     *
     * 原来：exec_code_size = max(映像大小, 文件大小)。
     * 但 elf64_load_biased 返回的缓冲区只有「映像大小」那么长
     * （= 最高 PT_LOAD 段末尾 - 映像原点），文件大小可能更大
     * （文件里还有符号表、字符串表、重定位表等不加载的部分）。
     * 于是这里会从内核堆缓冲**越界读** 几十 KB，
     * 并把越界的垃圾内容映射进用户空间。
     * 正确做法：映像多大就映多大。
     */
    uint64_t exec_code_size = g_elf_last_load_size;

    /* P1-7: 解释器（ld.so）按 ET_DYN 偏置装进「库区」[0x60000000,0x70000000)。
     * 主程序留在兼容区 [0x50000000,0x60000000)；两层互不重叠，
     * ld.so 之后用 mmap 把 libc 等 .so 放到 0x64000000 以上。 */
    if (has_interp) {
        extern int ext2_64_stat(const char *path, uint32_t *size, uint8_t *isdir);
        extern int ext2_64_read_file(const char *path, uint8_t *buffer, int max_size);
        uint32_t isize = 0;
        uint8_t isdir = 0;
        if (ext2_64_stat(interp_path, &isize, &isdir) != 0 || isdir ||
            isize < 64 || isize > 8u * 1024u * 1024u) {
            vga_puts("[USER64] exec ERROR: cannot stat interpreter\n");
            if (map_base) va_free(VA_OWNER_PENDING, map_base);
            kfree(loaded);
            kfree(elf_buffer);
            return -1;
        }
        uint8_t *ibuf = (uint8_t *)kmalloc(isize);
        if (!ibuf) {
            vga_puts("[USER64] exec ERROR: OOM for interpreter\n");
            if (map_base) va_free(VA_OWNER_PENDING, map_base);
            kfree(loaded);
            kfree(elf_buffer);
            return -1;
        }
        int got = ext2_64_read_file(interp_path, ibuf, (int)isize);
        if (got < 64 || elf64_verify(ibuf) != 0) {
            vga_puts("[USER64] exec ERROR: bad interpreter image\n");
            kfree(ibuf);
            if (map_base) va_free(VA_OWNER_PENDING, map_base);
            kfree(loaded);
            kfree(elf_buffer);
            return -1;
        }
        uint64_t need = elf64_image_size(ibuf, (uint64_t)got);
        if (!need) {
            vga_puts("[USER64] exec ERROR: bad interpreter layout\n");
            kfree(ibuf);
            if (map_base) va_free(VA_OWNER_PENDING, map_base);
            kfree(loaded);
            kfree(elf_buffer);
            return -1;
        }
        interp_code_vaddr = va_alloc_in(VA_OWNER_PENDING, VA_LIB_BASE, VA_LIB_END, need);
        if (!interp_code_vaddr) {
            vga_puts("[USER64] exec ERROR: no VA space for interpreter\n");
            kfree(ibuf);
            if (map_base) va_free(VA_OWNER_PENDING, map_base);
            kfree(loaded);
            kfree(elf_buffer);
            return -1;
        }
        interp_loaded = elf64_load_biased(ibuf, (uint64_t)got, interp_code_vaddr);
        if (!interp_loaded) {
            vga_puts("[USER64] exec ERROR: interpreter load failed\n");
            va_free(VA_OWNER_PENDING, interp_code_vaddr);
            kfree(ibuf);
            if (map_base) va_free(VA_OWNER_PENDING, map_base);
            kfree(loaded);
            kfree(elf_buffer);
            return -1;
        }
        interp_load_size = g_elf_last_load_size;
        interp_entry_vaddr = g_elf_last_entry;
        kfree(ibuf);
        vga_puts("[USER64] interpreter loaded at ");
        vga_put_hex(interp_code_vaddr);
        vga_puts(", entry=");
        vga_put_hex(interp_entry_vaddr);
        vga_puts("\n");
    }

    /* 动态程序：CPU 从解释器入口开始跑；AT_ENTRY 仍指向主程序 _start。 */
    uint64_t at_entry_vaddr = entry_vaddr;
    if (has_interp) {
        at_entry_vaddr = entry_vaddr;
        entry_vaddr = interp_entry_vaddr;
    }

    int pid = user_process_create_elf(path, loaded, exec_code_size, code_vaddr, entry_vaddr, elf_buffer,
                                       (uint64_t)file_size, is_linux_abi ? ENV_LINUX_COMPAT : ENV_NATIVE,
                                       at_entry_vaddr, has_interp ? interp_code_vaddr : 0);
    kfree(loaded);   /* 代码已复制进用户页；旧版每次 exec 泄漏最多 16MB 内核堆 */
    if (pid < 0) {
        vga_puts("[USER64] exec ERROR: Failed to create ELF process\n");
        if (map_base) va_free(VA_OWNER_PENDING, map_base);   /* P1: 释放 PIE 预留 */
        kfree(elf_buffer);   /* V0.33: 补漏（原来这条路径泄漏整个缓冲区） */
        return -1;
    }
    /* P1: 进程槽位已建立，把 exec 期间预留的 PIE 区间移交给该进程 */
    if (map_base) user_va_adopt_pending(pid);

    /* P1-7: 解释器镜像映射进该进程的地址空间（之后 CPU 从它的入口开始执行） */
    if (has_interp && interp_loaded) {
        if (user_process_map_image(pid, interp_loaded, interp_load_size, interp_code_vaddr) != 0) {
            vga_puts("[USER64] exec ERROR: failed to map interpreter pages\n");
            kfree(interp_loaded);
            kfree(elf_buffer);
            return -1;
        }
        kfree(interp_loaded);
        interp_loaded = NULL;
        user_va_adopt_pending(pid);
    }

    vga_puts("[USER64] exec: process created (PID=");
    char pidstr[8];
    n = pid; i = 0;
    do { tmp[i++] = n % 10 + '0'; } while ((n /= 10) > 0);
    j = 0;
    while (i > 0) pidstr[j++] = tmp[--i];
    pidstr[j] = 0;
    vga_puts(pidstr);
    vga_puts(")\n");

    /* V0.33 SEC-15: 移除「路径含 dyn 即视为动态程序」的启发式。
     *
     * 旧判据：`path` 里出现 'd','y','n' 三个字符就算动态程序 ⇒ 走
     * hepu_dynlink_relocate()。这是**按文件名猜** ELF 性质：
     *   - /bin/dynhello.elf 撞上 → 走遗留动态链接路径 → 算出的 load bias
     *     是 0x1000（既不在兼容区也不在用户窗口）→ 程序跑飞 → #GP；
     *   - 反过来，名字里不带 dyn 的真·动态程序则会被漏判。
     *
     * 现在改成基于 ELF 本身的判据：带 PT_INTERP 的程序在 exec 阶段
     * 就已经被明确拒绝（见上方），静态/静态 PIE 程序不需要也不应该
     * 走这条路。所以这里直接删掉该调用。 */
    kfree(elf_buffer);

    /* 切换到用户态 */
    user_process_switch(pid);

    return pid;
}

/* V0.55.1: 这里曾有一个 16KB 的静态 user_stack, 但无任何代码引用它
 * (用户栈实际由 g_user_stack_phys + hepu_build_initial_stack 构建), 已移除。 */

/* 启动示例用户程序 */
void user_process_test(void)
{
    vga_puts("[USER64] Starting user process test (syscall)...\n");
    int pid = user_process_create("syscall_test", user_program_syscall_test);
    if (pid > 0) {
        vga_puts("[USER64] User process created, entering user mode...\n");
        /* 实际切换到用户态（IRETQ + 独立CR3） */
        user_process_switch(pid);
        /* 如果切换成功，不会返回到这里 */
        vga_puts("[USER64] WARNING: returned from user mode (should not happen)\n");
    }
}

/* ============================================================================
 * V0.13.46: 进程终止 API（随时中断/未响应强制结束）
 * ========================================================================== */
/* 回收隔离模式的地址空间（页帧、PT、PD、私有PDPT/PML4）；旧模式页表仍保留。幂等。 */
static void user_free_aspace(int idx)
{
    if (idx < 0 || idx >= MAX_USER_PROCS) return;
    user_process_t *proc = &user_procs[idx];
    if (!g_proc_isolated[idx] || !proc->page_table) return;
    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    if (uaspace_free_isolated(proc->page_table, cr3)) {
        proc->page_table = 0;
        g_proc_isolated[idx] = 0;
    }
}

/* 当前用户进程槽位（供 syscall 层选择该进程自己的 fd 表）；无则返回 -1 */
int user_process_get_current_slot(void)
{
    if (current_user_proc >= 0 && current_user_proc < MAX_USER_PROCS &&
        user_procs[current_user_proc].active) {
        return current_user_proc;
    }
    return -1;
}

/* 释放进程槽位资源 */
static void user_process_release(int idx)
{
    extern void syscall_fd_table_reset(int slot);
    extern void seccomp64_reset(int slot);
    user_process_t *proc = &user_procs[idx];
    syscall_fd_table_reset(idx);   /* 安全修复：槽位复用前清掉上一个进程遗留的 fd */
    seccomp64_reset(idx);          /* P0-4：槽位复用前清除 sandbox 模式，避免继承 */
    user_va_release_slot(idx);     /* P1: 清该进程的 VA 区间账本与模块表/kbuf */
    user_free_aspace(idx);
    if (proc->kernel_stack) {
        pmm64_free_page((uint64_t)proc->kernel_stack);
        proc->kernel_stack = 0;
    }
    /* 页表保留（内核共享PML4，后续复用由create重建） */
    proc->active = 0;
    proc->kill_requested = 0;
    if (current_user_proc == idx) {
        current_user_proc = -1;
    }
}

/* 是否有待强制终止的当前进程（IRQ截获点检查） */
int user_process_kill_pending(void)
{
    if (!g_user_in_user_mode) return 0;
    if (current_user_proc < 0 || current_user_proc >= MAX_USER_PROCS) return 0;
    return user_procs[current_user_proc].kill_requested ? 1 : 0;
}

/* V0.33: 记录当前进程的 FS_BASE（glibc 通过 prctl(ARCH_SET_FS) 设置线程指针）
 *
 * 除了记账，还要**立刻**把 MSR 写回去：
 *   系统调用返回用户态时，sysretq 不会恢复 FS_BASE —— 它不是被调用者保存的寄存器。
 *   如果这里只记账不写 MSR，用户态的 FS 指针就一直是 0，
 *   glibc 读 TLS（__ctype_init / __libc_setup_tls 都要读）直接踩空指针。
 *   （实测：static-PIE 的 glibc 在 __ctype_init 里写 0 地址而崩。）
 */
uint64_t user_process_get_fsbase(void)
{
    if (current_user_proc < 0 || current_user_proc >= MAX_USER_PROCS) return 0;
    if (!user_procs[current_user_proc].active) return 0;
    return user_procs[current_user_proc].fsbase;
}

void user_process_set_fsbase(uint64_t base)
{
    if (current_user_proc < 0 || current_user_proc >= MAX_USER_PROCS) return;
    if (!user_procs[current_user_proc].active) return;
    user_procs[current_user_proc].fsbase = base;
    /* 立即生效：写 IA32_FS_BASE */
    __asm__ volatile("wrmsr" : :
                     "c"(0xC0000100),
                     "a"((uint32_t)(base & 0xFFFFFFFFULL)),
                     "d"((uint32_t)(base >> 32)));
}

/* V0.33: TLS 预留区是否已映射（每进程一次） */
int user_process_tls_reserve_done(void)
{
    if (current_user_proc < 0 || current_user_proc >= MAX_USER_PROCS) return 1;
    return user_procs[current_user_proc].tls_reserved;
}

void user_process_tls_reserve_mark(void)
{
    if (current_user_proc < 0 || current_user_proc >= MAX_USER_PROCS) return;
    user_procs[current_user_proc].tls_reserved = 1;
}

/* V0.33: 当前用户进程的 brk（每进程独立）
 * 之前 sys_brk 用的是文件级 static 变量 —— 多进程共享同一个 break，
 * 一个进程 brk 上去，另一个进程的堆就被踩了。 */
uint64_t user_process_get_brk(void)
{
    if (current_user_proc < 0 || current_user_proc >= MAX_USER_PROCS) return 0;
    if (!user_procs[current_user_proc].active) return 0;
    return user_procs[current_user_proc].brk_cur;
}

void user_process_set_brk(uint64_t brk)
{
    if (current_user_proc < 0 || current_user_proc >= MAX_USER_PROCS) return;
    if (!user_procs[current_user_proc].active) return;
    user_procs[current_user_proc].brk_cur = brk;
}

/* V0.33: 当前用户进程的 ABI 类型（0=原生, 1=Linux兼容）
 * 系统调用翻译层按这个判定要不要走 Linux 号表 —— 按进程判定，不做全局开关。
 * 没有活动用户进程时返回 ENV_NATIVE（内核自身走原生 ABI）。 */
int user_process_current_env_type(void)
{
    if (current_user_proc < 0 || current_user_proc >= MAX_USER_PROCS) return ENV_NATIVE;
    if (!user_procs[current_user_proc].active) return ENV_NATIVE;
    return user_procs[current_user_proc].env_type;
}

/* 获取当前用户进程PID（供任务管理器显示） */
int user_process_get_current_pid(void)
{
    if (current_user_proc >= 0 && user_procs[current_user_proc].active) {
        return user_procs[current_user_proc].pid;
    }
    return -1;
}

/* 用户进程数（active槽位数，供任务管理器） */
int user_process_count(void)
{
    int n = 0;
    for (int i = 0; i < MAX_USER_PROCS; i++) {
        if (user_procs[i].active) n++;
    }
    return n;
}

/* 获取进程信息（供任务管理器绘制） */
int user_process_get_info(int idx, int *pid, char *name, int *running)
{
    if (idx < 0 || idx >= MAX_USER_PROCS) return -1;
    user_process_t *proc = &user_procs[idx];
    if (!proc->active) return -1;
    *pid = proc->pid;
    int i = 0;
    while (proc->name[i] && i < 30) { name[i] = proc->name[i]; i++; }
    name[i] = 0;
    *running = (current_user_proc == idx && g_user_in_user_mode) ? 1 : 0;
    return 0;
}

/* 终止进程：pid 为 user_procs 的 pid（100+idx）
 * - 非当前运行进程：立即释放
 * - 当前运行进程（含未响应/死循环）：标记 kill_requested，由下一次IRQ截获强制退出 */
int user_process_kill(int pid)
{
    if (!user_proc_initialized) return -1;
    for (int i = 0; i < MAX_USER_PROCS; i++) {
        if (user_procs[i].active && user_procs[i].pid == pid) {
            if (g_user_in_user_mode && current_user_proc == i) {
                /* 正在运行：请求强制终止，等IRQ截获 */
                user_procs[i].kill_requested = 1;
                vga_puts("[USER64] Kill requested for PID ");
                vga_put_hex(pid);
                vga_puts(" (will terminate at next IRQ)\n");
                return 0;
            }
            user_process_release(i);
            vga_puts("[USER64] Process PID ");
            vga_put_hex(pid);
            vga_puts(" terminated\n");
            return 0;
        }
    }
    return -1;
}

/* V0.13.46: 强制结束当前用户进程（Ctrl+Alt+K 快捷键路径） */
int user_process_kill_current(void)
{
    if (!g_user_in_user_mode) return -1;
    if (current_user_proc < 0 || current_user_proc >= MAX_USER_PROCS) return -1;
    if (!user_procs[current_user_proc].active) return -1;
    user_procs[current_user_proc].kill_requested = 1;
    vga_puts("[USER64] Force-kill current process PID ");
    vga_put_hex((uint64_t)user_procs[current_user_proc].pid);
    vga_puts(" (Ctrl+Alt+K)\\n");
    return 0;
}

/* ============================================================================
 * V0.33: 用户态异常 —— 只终止「该进程」，不整机挂死
 *
 * 契约依据：OPEN_BUGS SEC-07「用户态异常只终止该进程、不再整机 hlt」。
 * 旧实现里 64 位异常处理统一 `while(1) hlt` —— 一个用户程序随便踩个非法地址，
 * 整机就死了。这条契约在 v0.13.68 就写下了，但 64 位这条路径没落地，
 * 结果是启动自检里的 glibc_test 一旦缺 syscall 就把整个启动流程带崩，
 * 系统根本进不了桌面。
 *
 * 语义：
 *   - 返回 1：已标记该进程终止，调用方负责把中断帧改写成「返回内核」的路径；
 *   - 返回 0：当前不在用户态（或没有活动用户进程），调用方按内核态异常处理。
 * ========================================================================== */
int user_process_fault_current(uint64_t vector)
{
    if (!g_user_in_user_mode) return 0;
    if (current_user_proc < 0 || current_user_proc >= MAX_USER_PROCS) return 0;
    user_process_t *proc = &user_procs[current_user_proc];
    if (!proc->active) return 0;

    proc->kill_requested = 1;
    proc->faulted = 1;
    proc->fault_vector = vector;

    vga_puts("[USER64] User process ");
    vga_puts(proc->name);
    vga_puts(" (PID ");
    vga_put_hex((uint64_t)proc->pid);
    vga_puts(") terminated by exception, vector=");
    vga_put_hex(vector);
    vga_puts("\n");
    return 1;
}

/* V0.33 诊断: 用户态异常时回溯用户栈。
 *
 * 用途: 定位「谁跳到了野指针」——把栈上落在 Linux 兼容 PIE 装载区间
 * (0x50000000-0x60000000) 内的返回地址按序打印, 即可还原调用链,
 * 从而判断 0x202 这类小地址是哪个函数指针被错误调用。
 *
 * 遵循 BUG_FIX_GUIDELINES「证据优先」: 不猜测, 先拿调用链证据。
 * 仅在 HEPU_TRACE_LINUX_SYSCALL 诊断构建下编译, 默认内核不包含。 */
#ifdef HEPU_TRACE_LINUX_SYSCALL
void user_process_dump_user_stack(uint64_t pml4, uint64_t rsp)
{
    if (!pml4 || !rsp) return;
    vga_puts("\n  user stack trace (compat PIE ret addrs):\n");
    int shown = 0;
    for (int i = 0; i < 1024 && shown < 16; i++) {
        void *p = hepu_user_ptr(pml4, rsp + (uint64_t)i * 8);
        if (!p) break;            /* 栈页不可读 → 走到头了 */
        uint64_t v = *(uint64_t *)p;
        if (v >= 0x50000000ULL && v < 0x60000000ULL) {
            vga_puts("   sp+");
            vga_put_hex((uint64_t)i * 8);
            vga_puts(" = ");
            vga_put_hex(v);
            vga_puts("\n");
            shown++;
        }
    }
    if (!shown) vga_puts("   (no PIE-range return address found)\n");
}

/* V0.33 诊断: dump 故障点的用户指令字节。
 *
 * 用途：判断「rip 是否落在指令边界上」。
 *   - rip 落在多字节指令中间 ⇒ 控制流被破坏（栈/GOT/返回地址被改写）；
 *   - rip 正好是某条指令的起点 ⇒ 是那条指令本身触发的异常。
 * 这是区分「代码写错了」和「控制流跑飞了」的关键证据。
 */
#ifdef HEPU_TRACE_LINUX_SYSCALL
void user_process_dump_fault_bytes(uint64_t pml4, uint64_t rip)
{
    if (!pml4) return;
    uint8_t *p = (uint8_t *)hepu_user_ptr(pml4, rip);
    if (!p) { vga_puts("\n  fault bytes: unreadable\n"); return; }
    vga_puts("\n  fault bytes @");
    vga_put_hex(rip);
    vga_puts(": ");
    for (int i = 0; i < 16; i++) {
        vga_put_hex((uint64_t)p[i]);
        vga_puts(" ");
    }
    vga_puts("\n");
}
#endif

/* V0.33 诊断: dump glibc 的退出处理链（__exit_funcs 节点）。
 *
 * glibc 把 atexit/dtor 的函数指针用 pointer guard 混淆后存进 __exit_funcs，
 * 调用时再 ror17+xor 解混淆。崩溃时 r14 正是 __exit_funcs 链表头，
 * 于是可以直接看到「存进去的是什么」和「解出来的是什么」。
 *
 * 结构（实测自 glibc 2.36 反汇编）：
 *   exit_function_list { next@0, idx@8, fns[]@0x10 }
 *   exit_function      { flavor@0, fn@8, arg@0x10, dso@0x18 }  (步长 32)
 */
void user_process_dump_exit_list(uint64_t pml4, uint64_t list_head, uint64_t fs_base)
{
    if (!pml4 || !list_head) return;
    void *hp = hepu_user_ptr(pml4, list_head);
    if (!hp) { vga_puts("\n  exit list: head unreadable\n"); return; }
    uint64_t node = *(uint64_t *)hp;
    vga_puts("\n  exit list: head="); vga_put_hex(list_head);
    vga_puts(" node="); vga_put_hex(node);
    if (fs_base) {
        uint64_t *g = (uint64_t *)hepu_user_ptr(pml4, fs_base + 0x30);
        uint64_t *c = (uint64_t *)hepu_user_ptr(pml4, fs_base + 0x28);
        vga_puts("\n   tcb guard="); vga_put_hex(g ? *g : 0);
        vga_puts(" canary="); vga_put_hex(c ? *c : 0);
    }
    for (int i = 0; i < 4; i++) {
        uint64_t *e = (uint64_t *)hepu_user_ptr(pml4, node + 0x10 + (uint64_t)i * 32);
        if (!e) break;
        vga_puts("\n   entry["); vga_put_hex((uint64_t)i);
        vga_puts("] flavor="); vga_put_hex(e[0]);
        vga_puts(" fn="); vga_put_hex(e[1]);
        vga_puts(" arg="); vga_put_hex(e[2]);
    }
    vga_puts("\n");
}
#endif

/* V0.13.46: 未响应超时看门狗（10秒无返回即标记未响应）
 * 返回1表示触发强制终止；由vec40每1ms调用 */
int user_process_timeout_triggered(void)
{
    extern uint64_t g_kernel_tick64;
    if (!g_user_in_user_mode) return 0;
    if (current_user_proc < 0 || current_user_proc >= MAX_USER_PROCS) return 0;
    user_process_t *proc = &user_procs[current_user_proc];
    if (!proc->active) return 0;
    uint64_t run = g_kernel_tick64 - proc->start_tick;
    if (run > 10000) {  /* 10秒 */
        if (!proc->kill_requested) {
            proc->kill_requested = 1;
            vga_puts("[USER64] WATCHDOG: PID ");
            vga_put_hex((uint64_t)proc->pid);
            vga_puts(" unresponsive, force terminating...\n");
            extern uint64_t g_last_user_rip;
            vga_puts("[WATCHDOG] stuck at RIP=");
            vga_put_hex(g_last_user_rip);
            vga_puts("\n");
        }
        return 1;
    }
    return 0;
}

/* 当前用户进程PID */
int user_process_current_pid(void)
{
    if (current_user_proc >= 0 && user_procs[current_user_proc].active) {
        return user_procs[current_user_proc].pid;
    }
    return -1;
}

/* V0.13.46 验证用: 64位死循环用户程序（看门狗强制结束测试） */
void user_program_loop64(void)
{
    for (;;) {
        __asm__ volatile("pause");
    }
}

/* V0.13.46: 未响应进程强制退出清理（由 trampoline 调用，中断上下文）
 * 释放当前进程并恢复全局状态，随后进入桌面主循环 */
void user_process_force_exit_cleanup(void)
{
    if (current_user_proc >= 0 && current_user_proc < MAX_USER_PROCS &&
        user_procs[current_user_proc].active) {
        vga_puts("[USER64] Terminated unresponsive process PID ");
        vga_put_hex((uint64_t)user_procs[current_user_proc].pid);
        vga_puts("\n");
        user_process_release(current_user_proc);
    }
    g_user_in_user_mode = 0;
    vga_puts("[USER64] Forced exit cleanup done, resuming desktop\n");
}
