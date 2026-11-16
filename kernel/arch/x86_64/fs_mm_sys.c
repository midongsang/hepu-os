/* ============================================================================
 * Hepu OS - mmap / munmap / mprotect 业务实现
 *
 * 从 syscall64_full.c 切出的第四批（KERNEL_SCOPE §5-5 第 4 步）。
 *
 * 这批和前几批不同: 校验逻辑**散在业务里** —— 窗口校验(5 处)、长度溢出、
 * 保留区相交、配额检查、arena 推进、部分失败回滚。搬家时漏掉任何一条 if,
 * 都变成"以前拒绝的地址现在不拒绝了", 而标准回归看不出来(正常程序不碰
 * 非法地址)。所以先有 user/apps/mmprobe.c: 20 项边界用例, 旧代码上先跑
 * 全绿(那一步证明判据本身对), 搬完再跑, 逐项比对。
 *
 * 用户地址窗口常量也一起搬过来。理由: 它们不是"分发层的知识", 而是
 * **用户地址空间的定义** —— 谁定义这个空间, 谁能判断地址合不合法, 就该
 * 和判断逻辑住在一起。留在 syscall 层的结果是: 搬出去的文件还得重写一份
 * 窗口常量, 于是同一件事有两个真相。
 *
 * 注意 user_access64.c 里另有一份 HEPU_USER_WIN_END(边界校验用, 语义相同
 * 但字面不同源)。这批没合并它 —— 它属"用户指针访问"那条路径, 与本文件
 * 的 mm 分区契约是两件事, 合并要先确认两者真的一致。
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include "fs_sys.h"

/* P0-A: 用户虚拟地址窗口与分区契约。
 * 与 user_process.c（UWIN_START/UWIN_END）、sys_brk、vDSO 映射保持一致。
 * 任何 mmap/munmap 都必须先落在 [USER_WIN_BASE, USER_WIN_END) 内，
 * 否则会改写内核共享页表（PDPT[0]/高半区）——这是跨进程的破坏性越权。 */
#define USER_WIN_BASE   0x40000000ULL
#define USER_WIN_END    0x80000000ULL
#define USER_MMAP_BASE  0x64000000ULL   /* 匿名/文件映射区起点
                                         * V0.54.6: 原 0x50000000 与兼容程序区
                                         * [0x50000000,0x60000000) 重叠 —— 动态
                                         * 链接器一 mmap 就把 PIE 主程序页覆盖掉。
                                         * 移到共享库区上方，避开：兼容程序区
                                         * 0x50000000-0x60000000、解释器/库区
                                         * 0x60000000-0x70000000、TLS 预留 0x67810000。
                                         * 上界收在 TLS 预留区之前。 */
#define USER_MMAP_LIMIT 0x67800000ULL
#define USER_BRK_LO     0x68000000ULL   /* brk 堆 [0x68000000,0x6F000000) */
#define USER_BRK_HI     0x6F000000ULL
#define USER_STACK_LO   0x6FF00000ULL   /* 1MB 栈 [0x6FF00000,0x70000000) */
#define USER_STACK_HI   0x70000000ULL
#define USER_VDSO_LO    0x7FFF0000ULL   /* vDSO 页 [0x7FFF0000,0x7FFF1000) */
#define USER_VDSO_HI    0x7FFF1000ULL

/* 区间 [start,end) 必须非空且完整落在用户窗口内 */
static int user_va_range_ok(uint64_t start, uint64_t end)
{
    return end > start && start >= USER_WIN_BASE && end <= USER_WIN_END;
}

/* 区间 [start,end) 是否与保留区 [rstart,rend) 相交 */
static int user_va_range_hits(uint64_t start, uint64_t end, uint64_t rstart, uint64_t rend)
{
    return start < rend && end > rstart;
}

/* 显式地址不得覆盖 brk / 栈 / vDSO 等内核或加载器维护的保留区 */
static int user_va_is_reserved(uint64_t start, uint64_t end)
{
    return user_va_range_hits(start, end, USER_BRK_LO, USER_BRK_HI) ||
           user_va_range_hits(start, end, USER_STACK_LO, USER_STACK_HI) ||
           user_va_range_hits(start, end, USER_VDSO_LO, USER_VDSO_HI);
}

/* sys_mmap: 匿名页分配, 尊重 prot 权限
 * prot: PROT_READ=1, PROT_WRITE=2, PROT_EXEC=4
 */
 int64_t sys_mmap(uint64_t addr, uint64_t length, int prot, int flags, int fd, int64_t off)
{
    if (length == 0) return 0;
    /* 长度上限：整个窗口最多 1GB；同时避免 length+4095 溢出 */
    if (length > USER_WIN_END - USER_WIN_BASE) return -1;
    extern uint64_t user_process_current_pml4(void);
    extern int vmm64_map_page(uint64_t pml4, uint64_t virt, uint64_t phys, uint64_t flags);
    extern uint64_t pmm64_alloc_page(void);
    extern void pmm64_free_page(uint64_t phys);
    extern uint64_t vmm64_phys_to_virt(uint64_t phys);
    extern int user_process_mem_charge(uint64_t pages);
    extern void user_process_mem_uncharge(uint64_t pages);
    static uint64_t mmap_heap = USER_MMAP_BASE;
    uint64_t pml4 = user_process_current_pml4();
    if (!pml4) return -1;

    /* 文件映射：glibc 的 ld.so 用 mmap(fd, offset, MAP_PRIVATE|MAP_FIXED)
     * 把 .so 的每个 PT_LOAD 段映射进用户空间。旧实现无条件当作匿名映射，
     * 于是 ld.so 拿到的是一整片零页 —— 加载 libc 的第一步就失败。
     * 这里按「目标页虚拟地址 - 映射起点 + 文件偏移」逐页从 ext2 拷入。 */
    /* 文件映射要读 fd 指向的路径。不直接摸 fd 表 —— 表和锁都在 syscall 层,
     * 这里只要"这条 fd 指向哪个路径"这一件事(fs_fd_get_path 返回 -1 也顺带
     * 表达了"这条 fd 没被占用", 与原来的 sys_files[fd].used 判断等价)。 */
    char fpath[256];
    int file_backed = 0;
    uint32_t file_size = 0;
    if (!(flags & 0x20 /* MAP_ANONYMOUS */) && fd >= 0) {
        if (fs_fd_get_path(fd, fpath, sizeof(fpath)) == 0) {
            uint8_t isdir = 0;
            extern int ext2_64_stat(const char *path, uint32_t *size, uint8_t *isdir);
            extern int ext2_64_read_file_offset(const char *path, uint8_t *buffer,
                                                uint32_t max_size, uint32_t offset);
            if (off < 0) return -1;
            if (ext2_64_stat(fpath, &file_size, &isdir) != 0 || isdir) return -1;
            file_backed = 1;
        }
    }

    uint64_t bytes = (length + 0xFFFULL) & ~0xFFFULL;
    uint64_t aligned;
    uint64_t end;
    uint64_t map_addr;
    if (addr) {
        aligned = addr & ~0xFFFULL;
        map_addr = addr;               /* 保留未对齐的原始地址，文件偏移要用它换算 */
        if (!user_va_range_ok(aligned, aligned + bytes)) return -1;
        if (user_va_is_reserved(aligned, aligned + bytes)) return -1;
    } else {
        /* 内核分配的匿名区：只能从 arena 向 brk 方向增长 */
        aligned = mmap_heap;
        map_addr = aligned;
        if (!user_va_range_ok(aligned, aligned + bytes)) return -1;
        if (aligned + bytes > USER_MMAP_LIMIT) return -1;
    }
    end = aligned + bytes;
    /* P1: 先记入本域配额/水位检查，超限直接拒绝 */
    uint64_t mem_pages = bytes / 4096;
    if (user_process_mem_charge(mem_pages) != 0) return -1;
    /* PTE flags: PRESENT=1, USER=4, WRITABLE=2; NX=bit63 */
    uint64_t pte_flags = 0x001 | 0x004;  /* PRESENT | USER */
    if (prot & 2) pte_flags |= 0x002;    /* PROT_WRITE */
    /* 默认 NX=1 (不可执行); PROT_EXEC 时清 NX */
    pte_flags |= (1ULL << 63);
    if (prot & 4) pte_flags &= ~(1ULL << 63);  /* PROT_EXEC */
    uint64_t mapped = aligned;
    for (uint64_t v = aligned; v < end; v += 4096) {
        uint64_t phys = pmm64_alloc_page();
        if (!phys) goto fail;
        /* V0.20.86: 清零匿名页 (malloc需要) */
        uint8_t *kvirt = (uint8_t *)vmm64_phys_to_virt(phys);
        for (int z = 0; z < 4096; z++) kvirt[z] = 0;
        if (file_backed) {
            /* 关键：addr/off 的低 12 位相同但**不一定是 0**（glibc 映射
             * libc 的第 4 个 LOAD 时 addr=...+0xC60、off=...+0xC60）。
             * 页 v 对应的文件偏移必须是 off + (v - addr)，而不是
             * off + (v - 页对齐后的 addr)——后者会整体错位 0xC60 字节，
             * 读出来的 PT_DYNAMIC 是一堆垃圾（表现为 libc「没有版本信息」）。 */
            int64_t delta = (int64_t)v - (int64_t)map_addr;
            int64_t file_off = (int64_t)off + delta;
            if (file_off >= 0 && (uint64_t)file_off < file_size) {
                uint32_t want = 4096;
                if ((uint64_t)file_off + want > file_size)
                    want = (uint32_t)(file_size - (uint64_t)file_off);
                if (ext2_64_read_file_offset(fpath, kvirt, want,
                                             (uint32_t)file_off) < 0) {
                    pmm64_free_page(phys);
                    goto fail;
                }
            }
        }
        if (vmm64_map_page(pml4, v, phys, pte_flags) != 0) {
            pmm64_free_page(phys);
            goto fail;
        }
        mapped = v + 4096;
    }
    /* arena 只向前推进：显式映射落在 arena 上方时，后续匿名映射从其后开始，
     * 避免与已显式映射的区间相撞；显式低地址映射不得把 arena 拉回代码区。
     * 显式映射越过 arena 上界时，直接把 arena 标记为耗尽（保守但确定）。 */
    if (!addr) {
        mmap_heap = end;
    } else if (end > mmap_heap) {
        mmap_heap = (end <= USER_MMAP_LIMIT) ? end : USER_MMAP_LIMIT;
    }
    return (int64_t)aligned;

fail:
    /* 部分失败时回收已映射页，避免"返回成功但只映射了一半" */
    {
        extern void vmm64_unmap_page(uint64_t pml4, uint64_t virt);
        extern uint64_t vmm64_virt_to_phys(uint64_t pml4, uint64_t virt);
        for (uint64_t v = aligned; v < mapped; v += 4096) {
            uint64_t phys = vmm64_virt_to_phys(pml4, v);
            vmm64_unmap_page(pml4, v);
            if (phys) pmm64_free_page(phys & ~0xFFFULL);
        }
        user_process_mem_uncharge(mem_pages);
        __asm__ volatile("mov %%cr3, %%rax; mov %%rax, %%cr3" ::: "rax", "memory");
    }
    return -1;
}

/* V0.20.90: munmap 真实回收
 * P0-A: 先校验范围，非法调用不得触碰页表（尤其不能 unmap 内核高半区）。 */
 int64_t sys_munmap(uint64_t addr, uint64_t length)
{
    if (length == 0) return 0;
    if (length > USER_WIN_END - USER_WIN_BASE) return -1;
    extern uint64_t user_process_current_pml4(void);
    extern void vmm64_unmap_page(uint64_t pml4, uint64_t virt);
    extern uint64_t vmm64_virt_to_phys(uint64_t pml4, uint64_t virt);
    extern void pmm64_free_page(uint64_t phys);
    extern void user_process_mem_uncharge(uint64_t pages);
    uint64_t pml4 = user_process_current_pml4();
    if (!pml4) return -1;
    uint64_t start = addr & ~0xFFFULL;
    uint64_t bytes = (length + 0xFFFULL) & ~0xFFFULL;
    if (!user_va_range_ok(start, start + bytes)) return -1;
    uint64_t end = start + bytes;
    uint64_t freed = 0;
    for (uint64_t v = start; v < end; v += 4096) {
        /* P1: 真实回收物理页，并同步域内存计数 */
        uint64_t phys = vmm64_virt_to_phys(pml4, v);
        vmm64_unmap_page(pml4, v);
        if (phys) { pmm64_free_page(phys & ~0xFFFULL); freed++; }
    }
    if (freed) user_process_mem_uncharge(freed);
    /* TLB flush: reload CR3 */
    __asm__ volatile("mov %%cr3, %%rax; mov %%rax, %%cr3" ::: "rax", "memory");
    return 0;
}

/* sys_mprotect: 真正改页权限
 * prot: PROT_READ=1, PROT_WRITE=2, PROT_EXEC=4
 * P0-A 补齐：与 mmap/munmap 相同的窗口/长度/溢出校验，且只作用于当前用户页表。
 */
 int64_t sys_mprotect(uint64_t addr, uint64_t len, int prot) {
    extern int vmm64_protect_page(uint64_t pml4, uint64_t virt, uint64_t prot);
    extern uint64_t user_process_current_pml4(void);
    if (len == 0) return 0;
    /* 长度上限：整个用户窗口最多 1GB；同时避免 len+4095 溢出 */
    if (len > USER_WIN_END - USER_WIN_BASE) return -1;
    uint64_t pml4 = user_process_current_pml4();
    if (!pml4) return -1;
    uint64_t start = addr & ~0xFFFULL;
    uint64_t bytes = (len + 0xFFFULL) & ~0xFFFULL;
    /* start 向下取整后仍须落在窗口内，end 不得越出窗口 */
    if (!user_va_range_ok(start, start + bytes)) return -1;
    uint64_t end = start + bytes;
    for (uint64_t v = start; v < end; v += 0x1000) {
        if (vmm64_protect_page(pml4, v, (uint64_t)prot) != 0) return -1;
    }
    return 0;
}

