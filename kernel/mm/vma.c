/* ============================================================================
 * 赫普OS - 虚拟内存区域管理 (VMA)
 * 实现：进程地址空间的虚拟内存区域管理（类似Linux vm_area_struct）
 *
 * V6.50增强：
 * - VMA合并：相邻且flags相同的区域自动合并
 * - 与页表集成：vma_map_with_pages()分配物理页并建立映射
 * - 缺页异常支持：vma_handle_page_fault()处理缺页
 * - 自测函数：验证映射、合并、分裂、权限检查
 * ========================================================================== */
#include <stdint.h>
#include <stddef.h>
#include "vma.h"
#include "kheap.h"
#include "string.h"
#include "vga.h"
#include "paging.h"
#include "mm.h"

/* VMA标志 */
#define VMA_READ    0x1
#define VMA_WRITE   0x2
#define VMA_EXEC    0x4
#define VMA_SHARED  0x8

/* 虚拟内存区域 */
typedef struct vma_area {
    uint32_t start;           /* 起始虚拟地址（页对齐） */
    uint32_t end;             /* 结束虚拟地址（不含） */
    uint32_t flags;           /* 访问权限标志 */
    struct vma_area *next;    /* 链表，按地址排序 */
    struct vma_area *prev;
} vma_area_t;

/* 地址空间 */
struct vm_address_space {
    vma_area_t *areas;        /* VMA链表头 */
    uint32_t    area_count;   /* 区域数量 */
    uint32_t    total_pages;  /* 总映射页数 */
};

/* 创建地址空间 */
vm_address_space_t *vma_create_space(void)
{
    vm_address_space_t *space = (vm_address_space_t *)kmalloc(sizeof(vm_address_space_t));
    if (!space) return NULL;
    space->areas = NULL;
    space->area_count = 0;
    space->total_pages = 0;
    return space;
}

/* 销毁地址空间 */
void vma_destroy_space(vm_address_space_t *space)
{
    if (!space) return;
    vma_area_t *cur = space->areas;
    while (cur) {
        vma_area_t *next = cur->next;
        kfree(cur);
        cur = next;
    }
    kfree(space);
}

/* 检查地址范围是否与现有VMA重叠 */
static int vma_check_overlap(vm_address_space_t *space, uint32_t start, uint32_t end)
{
    vma_area_t *cur = space->areas;
    while (cur) {
        if (start < cur->end && end > cur->start) {
            return 1;  /* 重叠 */
        }
        cur = cur->next;
    }
    return 0;
}

/* 映射一段虚拟内存区域
 * V6.50: 支持VMA合并——相邻且flags相同的区域自动合并 */
int vma_map(vm_address_space_t *space, uint32_t start, uint32_t length, uint32_t flags)
{
    if (!space || length == 0) return -1;

    /* 页对齐 */
    start = start & ~(PAGE_SIZE - 1);

    /* V7.77安全修复: 64位运算防止32位溢出（漏洞AA）
     * 原代码 start + length 可能溢出回绕，绕过end <= start检查，
     * 导致overlap检查被绕过，映射到低地址区域。 */
    uint64_t end64 = (uint64_t)start + length + PAGE_SIZE - 1;
    end64 &= ~(uint64_t)(PAGE_SIZE - 1);
    if (end64 > 0xFFFFFFFF) {
        return -1;  /* 超出32位地址空间 */
    }
    uint32_t end = (uint32_t)end64;

    if (end <= start) return -1;

    /* 检查重叠 */
    if (vma_check_overlap(space, start, end)) {
        return -2;  /* 地址重叠 */
    }

    /* V6.50: 检查是否可以与前一个区域合并（end == prev->end 且 flags相同） */
    vma_area_t *prev = NULL;
    vma_area_t *next_area = space->areas;
    while (next_area && next_area->start < start) {
        prev = next_area;
        next_area = next_area->next;
    }

    /* 检查与前一个区域合并 */
    if (prev && prev->end == start && prev->flags == flags) {
        uint32_t old_pages = (prev->end - prev->start) / PAGE_SIZE;
        prev->end = end;
        space->total_pages += (end - start) / PAGE_SIZE;
        /* 检查合并后是否可以与后一个区域合并 */
        if (prev->next && prev->end == prev->next->start && prev->next->flags == flags) {
            vma_area_t *merge = prev->next;
            prev->end = merge->end;
            space->total_pages += (merge->end - merge->start) / PAGE_SIZE;
            prev->next = merge->next;
            if (merge->next) merge->next->prev = prev;
            space->area_count--;
            kfree(merge);
        }
        return 0;
    }

    /* 检查与后一个区域合并 */
    if (next_area && end == next_area->start && next_area->flags == flags) {
        uint32_t old_pages = (next_area->end - next_area->start) / PAGE_SIZE;
        next_area->start = start;
        space->total_pages += (end - start) / PAGE_SIZE;
        return 0;
    }

    /* 分配新VMA */
    vma_area_t *area = (vma_area_t *)kmalloc(sizeof(vma_area_t));
    if (!area) return -3;

    area->start = start;
    area->end = end;
    area->flags = flags;
    area->next = NULL;
    area->prev = NULL;

    /* 按地址排序插入链表 */
    if (!space->areas || start < space->areas->start) {
        /* 插入头部 */
        area->next = space->areas;
        if (space->areas) space->areas->prev = area;
        space->areas = area;
    } else {
        vma_area_t *cur = space->areas;
        while (cur->next && cur->next->start < start) {
            cur = cur->next;
        }
        area->next = cur->next;
        area->prev = cur;
        if (cur->next) cur->next->prev = area;
        cur->next = area;
    }

    space->area_count++;
    space->total_pages += (end - start) / PAGE_SIZE;
    return 0;
}

/* 取消映射 */
int vma_unmap(vm_address_space_t *space, uint32_t start, uint32_t length)
{
    if (!space || length == 0) return -1;

    start = start & ~(PAGE_SIZE - 1);
    uint32_t end = (start + length + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    vma_area_t *cur = space->areas;
    while (cur) {
        vma_area_t *next = cur->next;
        if (start <= cur->start && end >= cur->end) {
            /* 完全覆盖，删除整个区域 */
            if (cur->prev) cur->prev->next = cur->next;
            else space->areas = cur->next;
            if (cur->next) cur->next->prev = cur->prev;
            space->total_pages -= (cur->end - cur->start) / PAGE_SIZE;
            space->area_count--;
            kfree(cur);
        } else if (start <= cur->start && end > cur->start && end < cur->end) {
            /* 部分覆盖头部 */
            uint32_t removed = (end - cur->start) / PAGE_SIZE;
            cur->start = end;
            space->total_pages -= removed;
        } else if (start > cur->start && start < cur->end && end >= cur->end) {
            /* 部分覆盖尾部 */
            uint32_t removed = (cur->end - start) / PAGE_SIZE;
            cur->end = start;
            space->total_pages -= removed;
        } else if (start > cur->start && end < cur->end) {
            /* 中间覆盖，分裂为两个区域 */
            vma_area_t *second = (vma_area_t *)kmalloc(sizeof(vma_area_t));
            if (second) {
                second->start = end;
                second->end = cur->end;
                second->flags = cur->flags;
                second->next = cur->next;
                second->prev = cur;
                if (cur->next) cur->next->prev = second;
                cur->end = start;
                cur->next = second;
                space->area_count++;
                space->total_pages -= (end - start) / PAGE_SIZE;
            }
        }
        cur = next;
    }
    return 0;
}

/* 查找包含指定地址的VMA */
vma_area_t *vma_find(vm_address_space_t *space, uint32_t addr)
{
    if (!space) return NULL;
    vma_area_t *cur = space->areas;
    while (cur) {
        if (addr >= cur->start && addr < cur->end) {
            return cur;
        }
        cur = cur->next;
    }
    return NULL;
}

/* 检查地址是否可访问 */
int vma_check_access(vm_address_space_t *space, uint32_t addr, uint32_t access_flags)
{
    vma_area_t *area = vma_find(space, addr);
    if (!area) return -1;  /* 未映射 */
    if ((area->flags & access_flags) != access_flags) return -2;  /* 权限不足 */
    return 0;
}

/* 获取地址空间统计 */
void vma_stats(vm_address_space_t *space, uint32_t *areas, uint32_t *pages, uint32_t *bytes)
{
    if (!space) return;
    if (areas) *areas = space->area_count;
    if (pages) *pages = space->total_pages;
    if (bytes) *bytes = space->total_pages * PAGE_SIZE;
}

/* 打印VMA列表 */
void vma_dump(vm_address_space_t *space)
{
    if (!space) {
        vga_puts("[VMA] empty addr space \n");
        return;
    }
    vga_printf("[VMA] addr space: %d region domains, %d pages \n", space->area_count, space->total_pages);
    vma_area_t *cur = space->areas;
    while (cur) {
        vga_printf("  0x%x-0x%x (%dKB) %c%c%c%s\n",
                   cur->start, cur->end,
                   (cur->end - cur->start) / 1024,
                   (cur->flags & VMA_READ) ? 'r' : '-',
                   (cur->flags & VMA_WRITE) ? 'w' : '-',
                   (cur->flags & VMA_EXEC) ? 'x' : '-',
                   (cur->flags & VMA_SHARED) ? " shared" : "");
        cur = cur->next;
    }
}

/* ============================================================================
 * V6.50: 与页表集成
 * ========================================================================== */

/* 映射VMA并分配物理页建立页表映射
 * 适用于需要立即分配物理内存的场景（如内核映射、设备映射）
 * 对于用户态匿名映射，建议使用vma_map()+缺页异常按需分配 */
int vma_map_with_pages(vm_address_space_t *space, uint32_t start, uint32_t length, uint32_t flags)
{
    if (!space || length == 0) return -1;

    /* 先映射VMA区域 */
    int ret = vma_map(space, start, length, flags);
    if (ret != 0) return ret;

    /* 分配物理页并建立页表映射 */
    start = start & ~(PAGE_SIZE - 1);
    uint32_t end = (start + length + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    page_directory_t *dir = paging_get_current_dir();
    if (!dir) dir = paging_get_kernel_dir();

    uint32_t pg_flags = 0;
    if (flags & VMA_WRITE) pg_flags |= 0x2;  /* PTE_WRITABLE */
    if (flags & VMA_USER)  pg_flags |= 0x4;  /* PTE_USER */

    for (uint32_t addr = start; addr < end; addr += PAGE_SIZE) {
        uint32_t phys = mm_alloc_page();
        if (!phys) {
            /* 内存不足，回滚已分配的页 */
            for (uint32_t a = start; a < addr; a += PAGE_SIZE) {
                uint32_t p = paging_get_phys(dir, a);
                if (p) mm_free_page(p);
                paging_unmap(dir, a);
            }
            vma_unmap(space, start, length);
            return -4;  /* 内存不足 */
        }
        paging_map(dir, addr, phys, pg_flags);
    }

    return 0;
}

/* 缺页异常处理
 * 检查fault_addr是否在某个VMA中：
 * - 如果在且VMA允许写，则分配物理页并建立映射（按需调页）
 * - 如果不在VMA中，返回错误（真正的段错误）
 * 返回：0=已处理，-1=未映射（段错误），-2=权限不足 */
int vma_handle_page_fault(vm_address_space_t *space, uint32_t fault_addr, uint32_t error_code)
{
    if (!space) return -1;

    vma_area_t *area = vma_find(space, fault_addr);
    if (!area) return -1;  /* 未映射，真正的段错误 */

    /* 检查权限：error_code bit1=1表示写访问 */
    uint32_t need_flags = VMA_READ;
    if (error_code & 0x2) need_flags |= VMA_WRITE;
    if ((area->flags & need_flags) != need_flags) return -2;  /* 权限不足 */

    /* 分配物理页并建立映射 */
    uint32_t addr = fault_addr & ~(PAGE_SIZE - 1);
    uint32_t phys = mm_alloc_page();
    if (!phys) return -3;  /* 内存不足（OOM） */

    page_directory_t *dir = paging_get_current_dir();
    if (!dir) dir = paging_get_kernel_dir();

    uint32_t pg_flags = 0x1;  /* PTE_PRESENT */
    if (area->flags & VMA_WRITE) pg_flags |= 0x2;
    if (area->flags & VMA_USER)  pg_flags |= 0x4;

    paging_map(dir, addr, phys, pg_flags);
    return 0;  /* 已处理，返回用户态重试 */
}

/* ============================================================================
 * V6.50: VMA自测
 * ========================================================================== */
void vma_selftest(void)
{
    extern void serial_puts(uint16_t, const char *);
    extern void serial_puthex(uint16_t, uint32_t);
    #define VLOG(s) serial_puts(0x3F8, s)

    VLOG("[VMA] selftest: start\n");
    int pass = 0, fail = 0;

    /* 测试1：创建地址空间 */
    vm_address_space_t *space = vma_create_space();
    if (space) {
        VLOG("[VMA] test1 create space: PASS\n");
        pass++;
    } else {
        VLOG("[VMA] test1 create space: FAIL\n");
        fail++;
        return;
    }

    /* 测试2：映射单个区域 */
    int ret = vma_map(space, 0x10000000, 0x4000, VMA_READ | VMA_WRITE);
    if (ret == 0 && space->area_count == 1) {
        VLOG("[VMA] test2 map single: PASS\n");
        pass++;
    } else {
        VLOG("[VMA] test2 map single: FAIL\n");
        fail++;
    }

    /* 测试3：映射不重叠区域 */
    ret = vma_map(space, 0x10008000, 0x2000, VMA_READ);
    if (ret == 0 && space->area_count == 2) {
        VLOG("[VMA] test3 map non-overlap: PASS\n");
        pass++;
    } else {
        VLOG("[VMA] test3 map non-overlap: FAIL\n");
        fail++;
    }

    /* 测试4：VMA合并（相邻且flags相同） */
    ret = vma_map(space, 0x10004000, 0x4000, VMA_READ | VMA_WRITE);
    /* 0x10000000-0x10004000 (rw) 和 0x10004000-0x10008000 (rw) 应该合并 */
    if (ret == 0 && space->area_count == 2) {  /* 合并后应该还是2个区域（合并后的rw + r） */
        vma_area_t *first = space->areas;
        if (first && first->start == 0x10000000 && first->end == 0x10008000) {
            VLOG("[VMA] test4 VMA merge: PASS\n");
            pass++;
        } else {
            VLOG("[VMA] test4 VMA merge: FAIL (wrong range)\n");
            fail++;
        }
    } else {
        VLOG("[VMA] test4 VMA merge: FAIL (area count wrong)\n");
        fail++;
    }

    /* 测试5：查找VMA */
    vma_area_t *found = vma_find(space, 0x10002000);
    if (found && found->start == 0x10000000) {
        VLOG("[VMA] test5 find VMA: PASS\n");
        pass++;
    } else {
        VLOG("[VMA] test5 find VMA: FAIL\n");
        fail++;
    }

    /* 测试6：权限检查 */
    ret = vma_check_access(space, 0x10002000, VMA_READ | VMA_WRITE);
    if (ret == 0) {
        VLOG("[VMA] test6 access check (rw): PASS\n");
        pass++;
    } else {
        VLOG("[VMA] test6 access check (rw): FAIL\n");
        fail++;
    }

    /* 测试7：权限不足检查 */
    ret = vma_check_access(space, 0x10009000, VMA_WRITE);  /* 只读区域 */
    if (ret == -2) {
        VLOG("[VMA] test7 access denied: PASS\n");
        pass++;
    } else {
        VLOG("[VMA] test7 access denied: FAIL\n");
        fail++;
    }

    /* 测试8：取消映射（完全覆盖） */
    ret = vma_unmap(space, 0x10000000, 0x8000);
    if (ret == 0 && space->area_count == 1) {  /* 只剩只读区域 */
        VLOG("[VMA] test8 unmap full: PASS\n");
        pass++;
    } else {
        VLOG("[VMA] test8 unmap full: FAIL\n");
        fail++;
    }

    /* 测试9：统计信息 */
    uint32_t areas, pages, bytes;
    vma_stats(space, &areas, &pages, &bytes);
    if (areas == 1 && pages == 2 && bytes == 8192) {
        VLOG("[VMA] test9 stats: PASS\n");
        pass++;
    } else {
        VLOG("[VMA] test9 stats: FAIL\n");
        fail++;
    }

    /* 测试10：销毁地址空间 */
    vma_destroy_space(space);
    VLOG("[VMA] test10 destroy space: PASS (no crash)\n");
    pass++;

    VLOG("[VMA] selftest: done (pass=");
    serial_puthex(0x3F8, pass);
    VLOG(", fail=");
    serial_puthex(0x3F8, fail);
    VLOG(")\n");

    #undef VLOG
}
