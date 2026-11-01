#ifndef KERNEL_CORE_H
#define KERNEL_CORE_H

#include <stdint.h>

#define KCORE_MAX_PAGE_TABLES 64
#define KCORE_MAX_VMA 128
#define KCORE_MAX_SWAP_SLOTS 1024
#define KCORE_MAX_SYSCALLS 64

/* 页表项标志 */
#define KCORE_PAGE_PRESENT    0x001
#define KCORE_PAGE_WRITABLE   0x002
#define KCORE_PAGE_USER       0x004
#define KCORE_PAGE_WRITE_THROUGH 0x008
#define KCORE_PAGE_CACHE_DISABLE 0x010
#define KCORE_PAGE_ACCESSED   0x020
#define KCORE_PAGE_DIRTY      0x040
#define KCORE_PAGE_HUGE       0x080
#define KCORE_PAGE_GLOBAL     0x100
#define KCORE_PAGE_NO_EXEC    0x8000000000000000ULL

/* 页大小 */
#define PAGE_SIZE_4K    4096
#define PAGE_SIZE_2M    0x200000
#define PAGE_SIZE_1G    0x40000000

/* 虚拟内存区域类型 */
typedef enum {
    VMA_FREE = 0,
    VMA_CODE,
    VMA_DATA,
    VMA_BSS,
    VMA_HEAP,
    VMA_STACK,
    VMA_MMAP,
    VMA_SHARED,
    VMA_IO,
    VMA_GUARD,
} vma_type_t;

/* 虚拟内存区域 */
typedef struct {
    int used;
    int id;
    uint64_t start;
    uint64_t end;
    vma_type_t type;
    int permissions;  /* PROT_READ/PROT_WRITE/PROT_EXEC */
    int flags;  /* MAP_PRIVATE/MAP_SHARED */
    int file_backed;
    char file_path[256];
    uint64_t file_offset;
    int page_count;
    int resident_pages;
    int swapped_pages;
} vma_t;

/* 页表 */
typedef struct {
    int used;
    int id;
    uint64_t pml4_phys;  /* PML4物理地址 */
    uint64_t *pml4_virt;  /* PML4虚拟地址 */
    int process_id;
    int page_count;
    int used_pages;
    int free_pages;
} kcore_page_table_t;

/* 物理页帧 */
typedef struct {
    uint64_t phys_addr;
    int used;
    int order;  /* 伙伴分配阶 */
    int reference_count;
    int dirty;
    int accessed;
    int swap_slot;
    int vma_id;
    int process_id;
} page_frame_t;

/* 伙伴分配器 */
typedef struct {
    int max_order;  /* 最大阶 (2^max_order页) */
    uint64_t total_pages;
    uint64_t free_pages;
    uint64_t used_pages;
    uint64_t reserved_pages;
    uint64_t start_addr;
    uint64_t end_addr;
    int free_lists[16];  /* 每阶空闲页数 */
} buddy_allocator_t;

/* Swap槽位 */
typedef struct {
    int used;
    int id;
    uint64_t page_phys;  /* 原物理页 */
    uint64_t vaddr;  /* 虚拟地址 */
    int process_id;
    int dirty;
    uint64_t swap_offset;
} swap_slot_t;

/* Swap信息 */
typedef struct {
    int enabled;
    uint64_t total_slots;
    uint64_t used_slots;
    uint64_t free_slots;
    uint64_t swap_size;  /* 字节 */
    uint64_t swap_used;  /* 字节 */
    int swap_in_count;
    int swap_out_count;
    swap_slot_t slots[KCORE_MAX_SWAP_SLOTS];
} swap_info_t;

/* 调度器类型 */
typedef enum {
    SCHEDULER_CFS = 0,
    SCHEDULER_RT,
    SCHEDULER_DEADLINE,
    SCHEDULER_BFS,  /* Brain Fuck Scheduler */
    SCHEDULER_O1,
} scheduler_type_t;

/* 调度实体 */
typedef struct {
    int pid;
    int priority;  /* -20到19 */
    int nice;
    uint64_t vruntime;  /* 虚拟运行时间 */
    uint64_t sum_exec_runtime;
    uint64_t se_weight;
    uint64_t se_inv_weight;
    int on_rq;  /* 在运行队列上 */
    int cpu;
    scheduler_type_t sched_class;
    uint64_t deadline;
    uint64_t runtime;
    uint64_t period;
} sched_entity_t;

/* 运行队列 */
typedef struct {
    int cpu;
    int nr_running;
    uint64_t load_weight;
    uint64_t nr_switches;
    uint64_t nr_migrations;
    sched_entity_t *curr;
    sched_entity_t *idle;
    sched_entity_t *stop;
    int cfs_nr_running;
    int rt_nr_running;
    int dl_nr_running;
    uint64_t cfs_load;
    uint64_t rt_load;
} run_queue_t;

/* 系统调用表项 */
typedef struct {
    int number;
    char name[64];
    void *handler;
    int arg_count;
    uint64_t call_count;
    uint64_t total_time_ns;
} syscall_entry_t;

/* 信号 */
typedef enum {
    KCORE_SIGKILL = 9,
    KCORE_SIGTERM = 15,
    KCORE_SIGINT = 2,
    KCORE_SIGSEGV = 11,
    KCORE_SIGBUS = 7,
    KCORE_SIGFPE = 8,
    KCORE_SIGILL = 4,
    KCORE_SIGABRT = 6,
    KCORE_SIGALRM = 14,
    KCORE_SIGCHLD = 17,
    KCORE_SIGCONT = 18,
    KCORE_SIGSTOP = 19,
    KCORE_SIGTSTP = 20,
    KCORE_SIGUSR1 = 10,
    KCORE_SIGUSR2 = 12,
    KCORE_SIGPIPE = 13,
    KCORE_SIGHUP = 1,
    KCORE_SIGQUIT = 3,
    KCORE_SIGTRAP = 5,
    KCORE_SIGURG = 23,
} signal_t;

/* 信号处理函数 */
typedef void (*signal_handler_t)(int);

/* 进程信号状态 */
typedef struct {
    uint64_t pending;  /* 待处理信号位图 */
    uint64_t blocked;  /* 阻塞信号位图 */
    signal_handler_t handlers[32];
    int in_handler;
    int current_signal;
} signal_state_t;

/* 内核核心状态 */
typedef struct {
    int initialized;
    /* 内存管理 */
    buddy_allocator_t buddy;
    kcore_page_table_t kernel_page_table;
    kcore_page_table_t page_tables[KCORE_MAX_PAGE_TABLES];
    int page_table_count;
    vma_t vmas[KCORE_MAX_VMA];
    int vma_count;
    swap_info_t swap;
    uint64_t total_memory;
    uint64_t used_memory;
    uint64_t free_memory;
    uint64_t kernel_memory;
    uint64_t user_memory;
    /* 调度器 */
    scheduler_type_t scheduler;
    run_queue_t run_queues[8];
    int cpu_count;
    uint64_t context_switches;
    uint64_t total_runtime;
    /* 系统调用 */
    syscall_entry_t syscalls[KCORE_MAX_SYSCALLS];
    int syscall_count;
    /* 信号 */
    signal_state_t kernel_signals;
    /* 统计 */
    uint64_t page_faults;
    uint64_t minor_faults;
    uint64_t major_faults;
    uint64_t allocations;
    uint64_t frees;
    uint64_t syscalls_total;
} kernel_core_t;

/* 初始化内核核心 */
void kernel_core_init(void);

/* 内存管理 */
int kcore_memory_init(uint64_t start, uint64_t size);
void *kcore_alloc_pages(int order);
int kcore_free_pages(void *addr, int order);
void *kcore_alloc(uint64_t size);
int kcore_free(void *addr);
int kcore_get_memory_stats(uint64_t *total, uint64_t *used, uint64_t *free, uint64_t *kernel, uint64_t *user);
int kcore_get_buddy_stats(buddy_allocator_t *buddy);

/* 页表管理 */
int kcore_pagetable_create(int process_id);
int kcore_pagetable_destroy(int pt_id);
int kcore_pagetable_switch(int pt_id);
int kcore_pagetable_map(int pt_id, uint64_t vaddr, uint64_t paddr, int flags);
int kcore_pagetable_unmap(int pt_id, uint64_t vaddr);
int kcore_pagetable_get_phys(int pt_id, uint64_t vaddr, uint64_t *paddr);
int kcore_pagetable_list(kcore_page_table_t *tables, int max);

/* VMA管理 */
int kcore_vma_create(int pt_id, uint64_t start, uint64_t end, vma_type_t type, int permissions);
int kcore_vma_destroy(int vma_id);
int kcore_vma_find(int pt_id, uint64_t vaddr, vma_t *vma);
int kcore_vma_list(int pt_id, vma_t *vmas, int max);

/* Swap */
int kcore_swap_init(uint64_t size);
int kcore_swap_out(uint64_t page_phys, int process_id, uint64_t vaddr);
int kcore_swap_in(int slot_id, uint64_t *page_phys);
int kcore_swap_get_stats(swap_info_t *info);

/* 调度器 */
int kcore_scheduler_init(scheduler_type_t type);
int kcore_scheduler_set(scheduler_type_t type);
int kcore_scheduler_get(scheduler_type_t *type);
int kcore_scheduler_add(int pid, int priority, scheduler_type_t sched_class);
int kcore_scheduler_remove(int pid);
int kcore_scheduler_yield(void);
int kcore_scheduler_tick(void);
int kcore_scheduler_get_runqueue(int cpu, run_queue_t *rq);
int kcore_scheduler_get_stats(uint64_t *switches, int *running, uint64_t *runtime);

/* 系统调用 */
int kcore_syscall_register(int number, const char *name, void *handler, int arg_count);
int kcore_syscall_unregister(int number);
int kcore_syscall_get(int number, syscall_entry_t *entry);
int kcore_syscall_list(syscall_entry_t *entries, int max);
int kcore_syscall_invoke(int number, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5, uint64_t *retval);

/* 信号 */
int kcore_signal_init(void);
int kcore_signal_set_handler(int sig, signal_handler_t handler);
int kcore_signal_send(int pid, int sig);
int kcore_signal_block(int pid, uint64_t mask);
int kcore_signal_unblock(int pid, uint64_t mask);
int kcore_signal_get_pending(int pid, uint64_t *pending);
int kcore_signal_dispatch(int pid);

/* 统计 */
int kcore_get_stats(kernel_core_t *stats);

#endif /* KERNEL_CORE_H */
