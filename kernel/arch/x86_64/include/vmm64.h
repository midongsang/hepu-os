/* ============================================================================
 * 赫普OS - 64位虚拟内存管理契约接口
 * V0.10.6: 按照系统包袱准则，定义稳定契约边界
 * ========================================================================== */

#ifndef HEPU_VMM64_H
#define HEPU_VMM64_H

#include <stdint.h>
#include <stddef.h>

/* 页表标志 */
#define VMM_PAGE_PRESENT    (1 << 0)
#define VMM_PAGE_WRITABLE   (1 << 1)
#define VMM_PAGE_USER       (1 << 2)
#define VMM_PAGE_HUGE       (1 << 7)
#define VMM_PAGE_NX         (1ULL << 63)

/* 虚拟内存管理操作表（稳定契约） */
struct vmm_ops {
    /* 初始化虚拟内存管理 */
    int (*init)(uint64_t pml4_addr);

    /* 映射一个页面 */
    int (*map_page)(uint64_t pml4_addr, uint64_t virt, uint64_t phys, uint64_t flags);

    /* 取消映射一个页面 */
    void (*unmap_page)(uint64_t pml4_addr, uint64_t virt);

    /* 虚拟地址转物理地址 */
    uint64_t (*virt_to_phys)(uint64_t pml4_addr, uint64_t virt);

    /* 加载页表到CR3 */
    void (*load_cr3)(uint64_t pml4_addr);

    /* 获取内核页表 */
    uint64_t (*get_kernel_pml4)(void);
};

/* 获取默认的虚拟内存管理操作表 */
const struct vmm_ops *vmm64_get_ops(void);

/* 便捷函数 */
static inline int vmm64_init(uint64_t pml4_addr) {
    return vmm64_get_ops()->init(pml4_addr);
}

static inline int vmm64_map_page(uint64_t pml4_addr, uint64_t virt, uint64_t phys, uint64_t flags) {
    return vmm64_get_ops()->map_page(pml4_addr, virt, phys, flags);
}

static inline void vmm64_unmap_page(uint64_t pml4_addr, uint64_t virt) {
    vmm64_get_ops()->unmap_page(pml4_addr, virt);
}

static inline uint64_t vmm64_virt_to_phys(uint64_t pml4_addr, uint64_t virt) {
    return vmm64_get_ops()->virt_to_phys(pml4_addr, virt);
}

static inline void vmm64_load_cr3(uint64_t pml4_addr) {
    vmm64_get_ops()->load_cr3(pml4_addr);
}

#endif /* HEPU_VMM64_H */
