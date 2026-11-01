/* ============================================================================
 * 赫普OS 64位 - 用户态进程切换
 * 支持：用户态创建、独立页表、上下文切换、系统调用入口
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>

#define USER_CS 0x18  /* 用户代码段选择子 */
#define USER_DS 0x20  /* 用户数据段选择子 */
#define KERNEL_CS 0x08
#define KERNEL_DS 0x10

#define USER_STACK_TOP 0x70000000
#define USER_CODE_BASE 0x40000000  /* 4GB以上，避免与内核前64MB恒等映射冲突 */

#define PAGE_PRESENT  (1 << 0)
#define PAGE_WRITABLE (1 << 1)
#define PAGE_USER     (1 << 2)

extern void vga_puts(const char *s);
extern uint64_t pmm64_alloc_page(void);
extern uint64_t vmm64_get_pml4(void);
extern int vmm64_map_page(uint64_t pml4, uint64_t virt, uint64_t phys, uint64_t flags);

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
} user_process_t;

#define MAX_USER_PROCS 8
static user_process_t user_procs[MAX_USER_PROCS];
static int current_user_proc = -1;
static int user_proc_initialized = 0;

/* 用户态退出后返回Shell的上下文 */
static uint64_t g_user_saved_rsp = 0;
static uint64_t g_user_saved_rip = 0;
static uint64_t g_user_saved_cr3 = 0;
static int g_user_in_user_mode = 0;

/* 汇编函数：切换到用户态 */
extern void user_enter(uint64_t rip, uint64_t rsp, uint64_t rflags);

static void uproc_memcpy(void *dst, const void *src, int n) {
    uint8_t *d = (uint8_t*)dst;
    const uint8_t *s = (const uint8_t*)src;
    for (int i = 0; i < n; i++) d[i] = s[i];
}

/* 创建用户页表：复制内核PML4条目（用户态可访问内核），添加用户栈和代码段映射 */
static uint64_t create_user_page_table_with_code(void *code_data, uint64_t code_size, uint64_t code_vaddr)
{
    uint64_t kernel_pml4 = vmm64_get_pml4();
    uint64_t user_pml4_phys = pmm64_alloc_page();
    if (!user_pml4_phys) return 0;

    /* 复制内核PML4全部512个条目（用户态共享内核映射） */
    uint64_t *kernel_pt = (uint64_t *)kernel_pml4;
    uint64_t *user_pt = (uint64_t *)user_pml4_phys;
    for (int i = 0; i < 512; i++) {
        user_pt[i] = kernel_pt[i];
    }

    /* 分配用户栈物理页（16KB = 4页） */
    for (int i = 0; i < 4; i++) {
        uint64_t phys = pmm64_alloc_page();
        if (!phys) return 0;
        uint64_t virt = USER_STACK_TOP - (i + 1) * 4096;
        vmm64_map_page(user_pml4_phys, virt, phys,
                       PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);
    }

    /* 映射用户代码段（按4KB页对齐） */
    if (code_data && code_size > 0) {
        uint64_t code_pages = (code_size + 4095) / 4096;
        uint8_t *src = (uint8_t *)code_data;
        for (uint64_t i = 0; i < code_pages; i++) {
            uint64_t phys = pmm64_alloc_page();
            if (!phys) return 0;
            uint64_t virt = code_vaddr + i * 4096;
            /* 复制代码到物理页 */
            uint8_t *dst = (uint8_t *)phys;
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
        vga_puts(" pages @ 0x");
        char hex[] = "0123456789ABCDEF";
        char hbuf[18];
        uint64_t addr = code_vaddr;
        hbuf[0]='0'; hbuf[1]='x';
        for (int i = 0; i < 16; i++) { hbuf[17-i] = hex[addr & 0xF]; addr >>= 4; }
        hbuf[17] = 0;
        vga_puts(hbuf);
        vga_puts("\n");
    }

    return user_pml4_phys;
}

/* 创建用户页表（仅栈，兼容旧接口） */
static uint64_t create_user_page_table(void)
{
    return create_user_page_table_with_code(NULL, 0, 0);
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
    proc->rip = (uint64_t)entry;
    proc->rsp = USER_STACK_TOP;
    proc->rflags = 0x202;  /* IF=1 */

    /* 创建独立用户页表（复制内核映射+用户栈） */
    proc->page_table = create_user_page_table();
    if (!proc->page_table) {
        vga_puts("[USER64] Failed to create user page table\n");
        proc->active = 0;
        return -1;
    }

    /* 分配内核栈（16KB） */
    proc->kernel_stack = (uint8_t *)pmm64_alloc_page();
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
int user_process_create_elf(const char *name, void *code_data, uint64_t code_size,
                            uint64_t code_vaddr, uint64_t entry_vaddr)
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
    proc->rip = entry_vaddr;  /* 用户空间入口点 */
    proc->rsp = USER_STACK_TOP;
    proc->rflags = 0x202;  /* IF=1 */

    /* 创建带代码段的用户页表 */
    proc->page_table = create_user_page_table_with_code(code_data, code_size, code_vaddr);
    if (!proc->page_table) {
        vga_puts("[USER64] Failed to create user page table with code\n");
        proc->active = 0;
        return -1;
    }

    /* 分配内核栈（16KB） */
    proc->kernel_stack = (uint8_t *)pmm64_alloc_page();
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
    vga_puts(", entry=0x");
    char hex[] = "0123456789ABCDEF";
    char hbuf[18];
    uint64_t addr = entry_vaddr;
    hbuf[0]='0'; hbuf[1]='x';
    for (int i = 0; i < 16; i++) { hbuf[17-i] = hex[addr & 0xF]; addr >>= 4; }
    hbuf[17] = 0;
    vga_puts(hbuf);
    vga_puts(")\n");

    return proc->pid;
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

    current_user_proc = idx;
    user_process_t *proc = &user_procs[idx];

    vga_puts("[USER64] Switching to user process ");
    vga_puts(proc->name);
    vga_puts(" (CR3=0x");
    char hex[] = "0123456789ABCDEF";
    char buf[18];
    uint64_t addr = proc->page_table;
    buf[0]='0'; buf[1]='x';
    for (int i = 0; i < 16; i++) { buf[17-i] = hex[addr & 0xF]; addr >>= 4; }
    buf[17] = 0;
    vga_puts(buf);
    vga_puts(")\n");

    /* 保存返回上下文（用户态退出后返回到这里） */
    g_user_saved_rip = (uint64_t)&&user_return_point;
    __asm__ volatile("movq %%rsp, %0" : "=m"(g_user_saved_rsp));
    __asm__ volatile("movq %%cr3, %0" : "=r"(g_user_saved_cr3));
    g_user_in_user_mode = 1;

    /* 切换到用户页表 */
    __asm__ volatile("mov %0, %%cr3" : : "r"(proc->page_table));

    /* 切换到用户态（IRETQ） */
    user_enter(proc->rip, proc->rsp, proc->rflags);

user_return_point:  /* 用户态退出后返回到这里 */
    g_user_in_user_mode = 0;
    vga_puts("[USER64] Returned to kernel from user mode\n");
    return 0;
}

/* 从用户态退出，返回到Shell */
void user_exit_to_kernel(void)
{
    if (!g_user_in_user_mode || g_user_saved_rsp == 0) {
        return;  /* 不在用户态，直接返回 */
    }

    vga_puts("[USER64] User process exiting, returning to kernel...\n");

    /* 恢复内核页表 */
    __asm__ volatile("mov %0, %%cr3" : : "r"(g_user_saved_cr3));

    g_user_in_user_mode = 0;

    /* 恢复内核栈并跳转到保存的返回地址 */
    __asm__ volatile(
        "movq %0, %%rsp\n"
        "jmp *%1\n"
        :
        : "m"(g_user_saved_rsp), "r"(g_user_saved_rip)
        : "memory"
    );
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
    vga_puts("[USER64] exec: ");
    vga_puts(path);
    vga_puts("\n");

    /* 1. 从ext2读取ELF文件 */
    static uint8_t elf_buffer[256 * 1024];  /* 256KB临时缓冲 */
    int file_size = ext2_64_read_file(path, elf_buffer, sizeof(elf_buffer));
    if (file_size <= 0) {
        vga_puts("[USER64] exec ERROR: Cannot read file\n");
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
        return -1;
    }

    /* 3. 获取入口点 */
    uint64_t entry = elf64_get_entry(elf_buffer);
    vga_puts("[USER64] exec: entry=0x");
    char hex[] = "0123456789ABCDEF";
    char hbuf[18];
    uint64_t addr = entry;
    hbuf[0]='0'; hbuf[1]='x';
    for (int i = 0; i < 16; i++) { hbuf[17-i] = hex[addr & 0xF]; addr >>= 4; }
    hbuf[17] = 0;
    vga_puts(hbuf);
    vga_puts("\n");

    /* 4. 加载ELF到内核内存 */
    void *loaded = elf64_load(elf_buffer, file_size);
    if (!loaded) {
        vga_puts("[USER64] exec ERROR: ELF load failed\n");
        return -1;
    }

    /* 5. 创建用户进程（真正的用户空间映射）
     * 将ELF内容映射到用户地址空间0x400000
     * 入口点：如果e_entry < 0x400000，则为相对偏移，否则为绝对地址
     */
    vga_puts("[USER64] exec: ELF loaded, mapping to user space...\n");

    uint64_t code_vaddr = USER_CODE_BASE;
    uint64_t entry_vaddr;
    if (entry < USER_CODE_BASE) {
        entry_vaddr = code_vaddr + entry;  /* 相对偏移 */
    } else {
        entry_vaddr = entry;  /* 绝对地址（假设已映射到该地址） */
        code_vaddr = entry & ~0xFFF;  /* 按页对齐 */
    }

    /* code_size使用ELF实际加载大小 */
    extern uint64_t g_elf_last_load_size;
    uint64_t exec_code_size = g_elf_last_load_size;
    if (exec_code_size < file_size) exec_code_size = file_size;

    int pid = user_process_create_elf(path, loaded, exec_code_size, code_vaddr, entry_vaddr);
    if (pid < 0) {
        vga_puts("[USER64] exec ERROR: Failed to create ELF process\n");
        return -1;
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

    /* 切换到用户态 */
    user_process_switch(pid);

    return pid;
}

/* 用户栈（16KB） */
static uint8_t user_stack[16384] __attribute__((aligned(16)));

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
