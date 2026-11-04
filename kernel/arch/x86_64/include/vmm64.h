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

/* 查询 virt 所在页是否已映射（1=已映射，0=未映射；含 2MB 大页） */
int vmm64_page_present(uint64_t pml4_addr, uint64_t virt);

static inline void vmm64_load_cr3(uint64_t pml4_addr) {
    vmm64_get_ops()->load_cr3(pml4_addr);
}

/* ============================================================================
 * V0.54.6: hepu_vmm_* 统一接口 → vmm64_* 私有 API 映射
 * 新代码 include "mm/hepu_vmm.h" 后直接用 hepu_vmm_* 即可跨架构
 * root=0 表示当前页表
 * ========================================================================== */
#include "mm/hepu_vmm.h"

static inline uint64_t vmm64_get_current_root(void) {
    /* 当前页表根：从 CR3 读；通过 get_kernel_pml4 简化 */
    return vmm64_get_kernel_pml4();  /* 64 位简化：内核页表即当前 */
}

static inline int hepu_vmm_map_page(uint64_t root, uint64_t virt, uint64_t phys, uint64_t flags) {
    return vmm64_map_page(root ? root : vmm64_get_current_root(), virt, phys, flags);
}
static inline void hepu_vmm_unmap_page(uint64_t root, uint64_t virt) {
    vmm64_unmap_page(root ? root : vmm64_get_current_root(), virt);
}
static inline uint64_t hepu_vmm_virt_to_phys(uint64_t root, uint64_t virt) {
    return vmm64_virt_to_phys(root ? root : vmm64_get_current_root(), virt);
}
static inline void hepu_vmm_load_root(uint64_t root) {
    if (root == 0) return;
    vmm64_load_cr3(root);
}
static inline uint64_t hepu_vmm_get_kernel_root(void) {
    return vmm64_get_kernel_pml4();
}
static inline uint64_t hepu_vmm_get_current_root(void) {
    return vmm64_get_current_root();
}
static inline uint64_t hepu_vmm_create_root(void) {
    /* 64 位暂无 create_root stub，调用 vmm64_create_pml4_impl */
    extern uint64_t vmm64_create_pml4_impl(void);
    return vmm64_create_pml4_impl();
}
static inline void hepu_vmm_free_root(uint64_t root) {
    if (root == 0) return;
    extern void vmm64_free_pml4_impl(uint64_t pml4);
    vmm64_free_pml4_impl(root);
}
static inline int hepu_vmm_set_flags(uint64_t root, uint64_t virt, uint64_t flags) {
    extern int vmm64_protect_page_impl(uint64_t pml4, uint64_t virt, uint64_t flags);
    return vmm64_protect_page_impl(root ? root : vmm64_get_current_root(), virt, flags);
}
static inline void hepu_vmm_flush_tlb_single(uint64_t virt) {
    extern void vmm64_flush_tlb_single_impl(uint64_t virt);
    vmm64_flush_tlb_single_impl(virt);
}
static inline int hepu_vmm_page_present(uint64_t root, uint64_t virt) {
    return vmm64_page_present(root ? root : vmm64_get_current_root(), virt);
}

#endif /* HEPU_VMM64_H */
