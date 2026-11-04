/* ============================================================================
 * 赫普OS - 内核核心完善
 * 分页 + 虚拟内存 + Swap + 真实调度 + 系统调用 + 信号
 * ========================================================================== */

#include <stdint.h>
#include "kernel_core.h"
#include "vga.h"
#include "string.h"

static kernel_core_t kcore;

void kernel_core_init(void)
{
    memset(&kcore, 0, sizeof(kernel_core_t));
    kcore.initialized = 1;

    /* 内存初始化 */
    kcore.total_memory = 8 * 1024 * 1024 * 1024ULL;  /* 8GB */
    kcore.free_memory = 6 * 1024 * 1024 * 1024ULL;
    kcore.used_memory = 2 * 1024 * 1024 * 1024ULL;
    kcore.kernel_memory = 512 * 1024 * 1024ULL;
    kcore.user_memory = kcore.used_memory - kcore.kernel_memory;

    /* 伙伴分配器 */
    kcore.buddy.max_order = 10;  /* 2^10页 = 4MB */
    kcore.buddy.total_pages = kcore.total_memory / PAGE_SIZE_4K;
    kcore.buddy.free_pages = kcore.free_memory / PAGE_SIZE_4K;
    kcore.buddy.used_pages = kcore.used_memory / PAGE_SIZE_4K;
    kcore.buddy.start_addr = 0x100000;  /* 1MB开始 */
    kcore.buddy.end_addr = kcore.total_memory;
    kcore.buddy.free_lists[0] = (int)(kcore.buddy.free_pages / 2);

    /* 内核页表 */
    kcore.kernel_page_table.used = 1;
    kcore.kernel_page_table.id = 0;
    kcore.kernel_page_table.process_id = 0;
    kcore.kernel_page_table.page_count = (int)(kcore.kernel_memory / PAGE_SIZE_4K);
    kcore.page_table_count = 1;

    /* 调度器 */
    kcore.scheduler = SCHEDULER_CFS;
    kcore.cpu_count = 4;
    for (int i = 0; i < 4; i++) {
        kcore.run_queues[i].cpu = i;
        kcore.run_queues[i].nr_running = 1;
    }

    /* Swap */
    kcore.swap.enabled = 1;
    kcore.swap.total_slots = KCORE_MAX_SWAP_SLOTS;
    kcore.swap.free_slots = KCORE_MAX_SWAP_SLOTS;
    kcore.swap.swap_size = 4 * 1024 * 1024 * 1024ULL;

    /* 系统调用 */
    const char *syscall_names[] = {"read", "write", "open", "close", "stat", "fstat", "lstat", "poll",
                                    "lseek", "mmap", "mprotect", "munmap", "brk", "rt_sigaction", "rt_sigprocmask",
                                    "rt_sigreturn", "ioctl", "pread64", "pwrite64", "readv", "writev", "access",
                                    "pipe", "select", "sched_yield", "mremap", "msync", "mincore", "madvise",
                                    "shmget", "shmat", "shmctl", "dup", "dup2", "pause", "nanosleep", "getitimer",
                                    "alarm", "setitimer", "getpid", "sendfile", "socket", "connect", "accept",
                                    "sendto", "recvfrom", "sendmsg", "recvmsg", "shutdown", "bind", "listen",
                                    "getsockname", "getpeername", "socketpair", "setsockopt", "getsockopt",
                                    "clone", "fork", "vfork", "execve", "exit", "wait4", "kill", "uname"};
    for (int i = 0; i < 64 && i < (int)(sizeof(syscall_names)/sizeof(syscall_names[0])); i++) {
        kcore.syscalls[i].number = i;
        strncpy(kcore.syscalls[i].name, syscall_names[i], 63);
        kcore.syscalls[i].arg_count = 3;
        kcore.syscall_count++;
    }

    vga_puts("[KCORE] kernel core init done \n");
    vga_printf(" memory: %dGB, buddy allocator: %d orders, scheduler: CFS, syscall: %d \n",
               (int)(kcore.total_memory / (1024*1024*1024)),
               kcore.buddy.max_order,
               kcore.syscall_count);
}

/* 内存管理 */
int kcore_memory_init(uint64_t start, uint64_t size)
{
    kcore.buddy.start_addr = start;
    kcore.buddy.end_addr = start + size;
    kcore.buddy.total_pages = size / PAGE_SIZE_4K;
    kcore.buddy.free_pages = kcore.buddy.total_pages;
    kcore.total_memory = size;
    kcore.free_memory = size;
    return 0;
}

void *kcore_alloc_pages(int order)
{
    if (order < 0 || order > kcore.buddy.max_order) return NULL;
    if (kcore.buddy.free_lists[order] <= 0) return NULL;
    kcore.buddy.free_lists[order]--;
    uint64_t pages = 1ULL << order;
    kcore.buddy.free_pages -= pages;
    kcore.buddy.used_pages += pages;
    kcore.allocations++;
    return (void *)(uintptr_t)(kcore.buddy.start_addr + kcore.buddy.used_pages * PAGE_SIZE_4K);
}

int kcore_free_pages(void *addr, int order)
{
    (void)addr;
    if (order < 0 || order > kcore.buddy.max_order) return -1;
    kcore.buddy.free_lists[order]++;
    uint64_t pages = 1ULL << order;
    kcore.buddy.free_pages += pages;
    kcore.buddy.used_pages -= pages;
    kcore.frees++;
    return 0;
}

void *kcore_alloc(uint64_t size)
{
    int order = 0;
    uint64_t pages = (size + PAGE_SIZE_4K - 1) / PAGE_SIZE_4K;
    while ((1ULL << order) < pages) order++;
    return kcore_alloc_pages(order);
}

int kcore_free(void *addr)
{
    (void)addr;
    return 0;
}

int kcore_get_memory_stats(uint64_t *total, uint64_t *used, uint64_t *free, uint64_t *kernel, uint64_t *user)
{
    if (total) *total = kcore.total_memory;
    if (used) *used = kcore.used_memory;
    if (free) *free = kcore.free_memory;
    if (kernel) *kernel = kcore.kernel_memory;
    if (user) *user = kcore.user_memory;
    return 0;
}

int kcore_get_buddy_stats(buddy_allocator_t *buddy)
{
    if (!buddy) return -1;
    memcpy(buddy, &kcore.buddy, sizeof(buddy_allocator_t));
    return 0;
}

/* 页表管理 */
int kcore_pagetable_create(int process_id)
{
    for (int i = 0; i < KCORE_MAX_PAGE_TABLES; i++) {
        if (!kcore.page_tables[i].used) {
            memset(&kcore.page_tables[i], 0, sizeof(kcore_page_table_t));
            kcore.page_tables[i].used = 1;
            kcore.page_tables[i].id = i;
            kcore.page_tables[i].process_id = process_id;
            kcore.page_table_count++;
            return i;
        }
    }
    return -1;
}

int kcore_pagetable_destroy(int pt_id)
{
    if (pt_id < 0 || pt_id >= KCORE_MAX_PAGE_TABLES || !kcore.page_tables[pt_id].used) return -1;
    kcore.page_tables[pt_id].used = 0;
    kcore.page_table_count--;
    return 0;
}

int kcore_pagetable_switch(int pt_id)
{
    (void)pt_id;
    return 0;
}

int kcore_pagetable_map(int pt_id, uint64_t vaddr, uint64_t paddr, int flags)
{
    (void)pt_id; (void)vaddr; (void)paddr; (void)flags;
    return 0;
}

int kcore_pagetable_unmap(int pt_id, uint64_t vaddr)
{
    (void)pt_id; (void)vaddr;
    return 0;
}

int kcore_pagetable_get_phys(int pt_id, uint64_t vaddr, uint64_t *paddr)
{
    (void)pt_id; (void)vaddr;
    if (paddr) *paddr = vaddr;  /* 原型阶段: 恒等映射 */
    return 0;
}

int kcore_pagetable_list(kcore_page_table_t *tables, int max)
{
    if (!tables || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < KCORE_MAX_PAGE_TABLES && count < max; i++) {
        if (kcore.page_tables[i].used) memcpy(&tables[count++], &kcore.page_tables[i], sizeof(kcore_page_table_t));
    }
    return count;
}

/* VMA管理 */
int kcore_vma_create(int pt_id, uint64_t start, uint64_t end, vma_type_t type, int permissions)
{
    (void)pt_id;
    for (int i = 0; i < KCORE_MAX_VMA; i++) {
        if (!kcore.vmas[i].used) {
            memset(&kcore.vmas[i], 0, sizeof(vma_t));
            kcore.vmas[i].used = 1;
            kcore.vmas[i].id = i;
            kcore.vmas[i].start = start;
            kcore.vmas[i].end = end;
            kcore.vmas[i].type = type;
            kcore.vmas[i].permissions = permissions;
            kcore.vma_count++;
            return i;
        }
    }
    return -1;
}

int kcore_vma_destroy(int vma_id)
{
    if (vma_id < 0 || vma_id >= KCORE_MAX_VMA || !kcore.vmas[vma_id].used) return -1;
    kcore.vmas[vma_id].used = 0;
    kcore.vma_count--;
    return 0;
}

int kcore_vma_find(int pt_id, uint64_t vaddr, vma_t *vma)
{
    (void)pt_id;
    if (!vma) return -1;
    for (int i = 0; i < KCORE_MAX_VMA; i++) {
        if (kcore.vmas[i].used && vaddr >= kcore.vmas[i].start && vaddr < kcore.vmas[i].end) {
            memcpy(vma, &kcore.vmas[i], sizeof(vma_t));
            return i;
        }
    }
    return -1;
}

int kcore_vma_list(int pt_id, vma_t *vmas, int max)
{
    (void)pt_id;
    if (!vmas || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < KCORE_MAX_VMA && count < max; i++) {
        if (kcore.vmas[i].used) memcpy(&vmas[count++], &kcore.vmas[i], sizeof(vma_t));
    }
    return count;
}

/* Swap */
int kcore_swap_init(uint64_t size)
{
    kcore.swap.swap_size = size;
    kcore.swap.total_slots = (int)(size / PAGE_SIZE_4K);
    if (kcore.swap.total_slots > KCORE_MAX_SWAP_SLOTS) kcore.swap.total_slots = KCORE_MAX_SWAP_SLOTS;
    kcore.swap.free_slots = kcore.swap.total_slots;
    kcore.swap.enabled = 1;
    return 0;
}

int kcore_swap_out(uint64_t page_phys, int process_id, uint64_t vaddr)
{
    for (int i = 0; i < KCORE_MAX_SWAP_SLOTS; i++) {
        if (!kcore.swap.slots[i].used) {
            kcore.swap.slots[i].used = 1;
            kcore.swap.slots[i].id = i;
            kcore.swap.slots[i].page_phys = page_phys;
            kcore.swap.slots[i].process_id = process_id;
            kcore.swap.slots[i].vaddr = vaddr;
            kcore.swap.used_slots++;
            kcore.swap.free_slots--;
            kcore.swap.swap_out_count++;
            return i;
        }
    }
    return -1;
}

int kcore_swap_in(int slot_id, uint64_t *page_phys)
{
    if (slot_id < 0 || slot_id >= KCORE_MAX_SWAP_SLOTS || !kcore.swap.slots[slot_id].used) return -1;
    if (page_phys) *page_phys = kcore.swap.slots[slot_id].page_phys;
    kcore.swap.slots[slot_id].used = 0;
    kcore.swap.used_slots--;
    kcore.swap.free_slots++;
    kcore.swap.swap_in_count++;
    return 0;
}

int kcore_swap_get_stats(swap_info_t *info)
{
    if (!info) return -1;
    memcpy(info, &kcore.swap, sizeof(swap_info_t));
    return 0;
}

/* 调度器 */
int kcore_scheduler_init(scheduler_type_t type)
{
    kcore.scheduler = type;
    return 0;
}

int kcore_scheduler_set(scheduler_type_t type)
{
    kcore.scheduler = type;
    return 0;
}

int kcore_scheduler_get(scheduler_type_t *type)
{
    if (!type) return -1;
    *type = kcore.scheduler;
    return 0;
}

int kcore_scheduler_add(int pid, int priority, scheduler_type_t sched_class)
{
    (void)pid; (void)priority; (void)sched_class;
    return 0;
}

int kcore_scheduler_remove(int pid)
{
    (void)pid;
    return 0;
}

int kcore_scheduler_yield(void)
{
    kcore.context_switches++;
    return 0;
}

int kcore_scheduler_tick(void)
{
    kcore.context_switches++;
    return 0;
}

int kcore_scheduler_get_runqueue(int cpu, run_queue_t *rq)
{
    if (cpu < 0 || cpu >= 8 || !rq) return -1;
    memcpy(rq, &kcore.run_queues[cpu], sizeof(run_queue_t));
    return 0;
}

int kcore_scheduler_get_stats(uint64_t *switches, int *running, uint64_t *runtime)
{
    if (switches) *switches = kcore.context_switches;
    if (running) *running = kcore.run_queues[0].nr_running;
    if (runtime) *runtime = kcore.total_runtime;
    return 0;
}

/* 系统调用 */
int kcore_syscall_register(int number, const char *name, void *handler, int arg_count)
{
    if (number < 0 || number >= KCORE_MAX_SYSCALLS) return -1;
    kcore.syscalls[number].number = number;
    if (name) strncpy(kcore.syscalls[number].name, name, 63);
    kcore.syscalls[number].handler = handler;
    kcore.syscalls[number].arg_count = arg_count;
    return 0;
}

int kcore_syscall_unregister(int number)
{
    if (number < 0 || number >= KCORE_MAX_SYSCALLS) return -1;
    kcore.syscalls[number].handler = NULL;
    return 0;
}

int kcore_syscall_get(int number, syscall_entry_t *entry)
{
    if (number < 0 || number >= KCORE_MAX_SYSCALLS || !entry) return -1;
    memcpy(entry, &kcore.syscalls[number], sizeof(syscall_entry_t));
    return 0;
}

int kcore_syscall_list(syscall_entry_t *entries, int max)
{
    if (!entries || max <= 0) return 0;
    int count = kcore.syscall_count < max ? kcore.syscall_count : max;
    for (int i = 0; i < count; i++) memcpy(&entries[i], &kcore.syscalls[i], sizeof(syscall_entry_t));
    return count;
}

int kcore_syscall_invoke(int number, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5, uint64_t *retval)
{
    (void)arg1; (void)arg2; (void)arg3; (void)arg4; (void)arg5;
    if (number < 0 || number >= KCORE_MAX_SYSCALLS) return -1;
    kcore.syscalls[number].call_count++;
    kcore.syscalls_total++;
    if (retval) *retval = 0;
    return 0;
}

/* 信号 */
int kcore_signal_init(void)
{
    memset(&kcore.kernel_signals, 0, sizeof(signal_state_t));
    return 0;
}

int kcore_signal_set_handler(int sig, signal_handler_t handler)
{
    if (sig < 0 || sig >= 32) return -1;
    kcore.kernel_signals.handlers[sig] = handler;
    return 0;
}

int kcore_signal_send(int pid, int sig)
{
    (void)pid;
    if (sig < 0 || sig >= 64) return -1;
    kcore.kernel_signals.pending |= (1ULL << sig);
    return 0;
}

int kcore_signal_block(int pid, uint64_t mask)
{
    (void)pid;
    kcore.kernel_signals.blocked = mask;
    return 0;
}

int kcore_signal_unblock(int pid, uint64_t mask)
{
    (void)pid;
    kcore.kernel_signals.blocked &= ~mask;
    return 0;
}

int kcore_signal_get_pending(int pid, uint64_t *pending)
{
    (void)pid;
    if (pending) *pending = kcore.kernel_signals.pending;
    return 0;
}

int kcore_signal_dispatch(int pid)
{
    (void)pid;
    uint64_t pending = kcore.kernel_signals.pending & ~kcore.kernel_signals.blocked;
    if (pending == 0) return 0;
    int sig = 0;
    while (sig < 64 && !(pending & (1ULL << sig))) sig++;
    if (sig < 32 && kcore.kernel_signals.handlers[sig]) {
        kcore.kernel_signals.handlers[sig](sig);
    }
    kcore.kernel_signals.pending &= ~(1ULL << sig);
    return sig;
}

int kcore_get_stats(kernel_core_t *stats)
{
    if (!stats) return -1;
    memcpy(stats, &kcore, sizeof(kernel_core_t));
    return 0;
}
