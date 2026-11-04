#ifndef PAGING_H
#define PAGING_H

#include <stdint.h>
#include <stddef.h>

#define PAGE_SIZE    4096
#define PAGE_PRESENT 0x1
#define PAGE_WRITE   0x2
#define PAGE_USER    0x4
#define PAGE_DIRTY   0x40

/* 页表项 */
typedef struct {
    uint32_t present  : 1;
    uint32_t writable : 1;
    uint32_t user     : 1;
    uint32_t accessed : 1;
    uint32_t dirty    : 1;
    uint32_t unused   : 7;
    uint32_t frame    : 20;  /* 物理页框号 (物理地址 >> 12) */
} page_entry_t;

/* 页目录项 */
typedef struct {
    uint32_t present  : 1;
    uint32_t writable : 1;
    uint32_t user     : 1;
    uint32_t accessed : 1;
    uint32_t dirty    : 1;
    uint32_t unused   : 7;
    uint32_t frame    : 20;  /* 页表物理地址 >> 12 */
} page_dir_entry_t;

/* 页目录 */
typedef struct {
    page_dir_entry_t entries[1024];
} page_directory_t;

/* 页表 */
typedef struct {
    page_entry_t entries[1024];
} page_table_t;

/* 初始化分页 */
void paging_init(uint32_t mem_size_kb);

/* V7.87: 内存隔离初始化——解析启动参数
 * 支持: mem_isolation=0 禁用隔离, mem_isolation=1 启用隔离(默认) */
void paging_init_memory_isolation(void);

/* V7.72: 内存隔离开关（漏洞A）
 * 1=启用(用户进程无法访问内核区), 0=禁用(兼容性回退) */
extern int memory_isolation_enabled;

/* 启用分页 */
void paging_enable(void);

/* 映射虚拟地址到物理地址 */
int paging_map(page_directory_t *dir, uint32_t virt, uint32_t phys,
               uint32_t flags);

/* 取消映射 */
void paging_unmap(page_directory_t *dir, uint32_t virt);

/* 获取虚拟地址对应的物理地址 */
uint32_t paging_get_phys(page_directory_t *dir, uint32_t virt);
page_directory_t *paging_get_kernel_dir(void);

/* 切换页目录 */
void paging_switch_dir(page_directory_t *dir);

/* 获取当前页目录 */
page_directory_t *paging_get_current_dir(void);

/* 创建新的页目录（用于用户进程） */
page_directory_t *paging_create_dir(void);

/* V3.78: 释放进程页目录（只归还顶层目录帧; 调用前须已切回内核目录） */
void paging_free_dir(page_directory_t *dir);

/* 页错误处理 */
void page_fault_handler(uint32_t fault_addr, uint32_t error_code);

/* 设置页标志 */
int paging_set_flags(page_directory_t *dir, uint32_t virt, uint32_t flags);

/* 写时复制(COW) */
int paging_mark_cow(page_directory_t *dir, uint32_t virt);
int paging_is_cow(page_directory_t *dir, uint32_t virt);

/* 页错误统计 */
void paging_fault_stats(uint32_t *total, uint32_t *demand,
                        uint32_t *cow, uint32_t *protection);

/* V6.80 PAE分支: PAE分页函数 */
void paging_pae_init(uint32_t mem_size_kb);
void paging_pae_enable(void);
int paging_pae_map(void *pdpt, uint32_t virt, uint64_t phys, uint32_t flags);
void paging_pae_unmap(void *pdpt, uint32_t virt);
uint64_t paging_pae_get_phys(void *pdpt, uint32_t virt);
void paging_pae_switch(void *pdpt);
void *paging_pae_create_dir(void);
void *paging_pae_get_kernel_dir(void);
void *paging_pae_get_current_dir(void);
int paging_pae_is_enabled(void);
void paging_pae_fault_handler(uint32_t fault_addr, uint32_t error_code);
void paging_pae_fault_stats(uint32_t *total, uint32_t *kernel,
                             uint32_t *user, uint32_t *protection);

/* ============================================================================
 * V0.54.6: hepu_vmm_* 统一接口 → paging_* / paging_pae_* 私有 API 映射
 * 新代码 include "mm/hepu_vmm.h" 后直接用 hepu_vmm_* 即可跨架构
 * root=0 自动选 PAE 或传统；root!=0 强转为 page_directory_t*
 * ========================================================================== */
#include "mm/hepu_vmm.h"

static inline page_directory_t *hepu_vmm_to_dir_32(uint64_t root) {
    return (page_directory_t *)(root ? (uintptr_t)root : (uintptr_t)paging_get_current_dir());
}

static inline int hepu_vmm_map_page(uint64_t root, uint64_t virt, uint64_t phys, uint64_t flags) {
    uint32_t f = (uint32_t)(flags & 0xFFFFFFFF);
    if (paging_pae_is_enabled())
        return paging_pae_map((void *)(root ? (uintptr_t)root : (uintptr_t)paging_pae_get_current_dir()),
                              (uint32_t)virt, phys, f);
    return paging_map(hepu_vmm_to_dir_32(root), (uint32_t)virt, (uint32_t)phys, f);
}
static inline void hepu_vmm_unmap_page(uint64_t root, uint64_t virt) {
    if (paging_pae_is_enabled())
        paging_pae_unmap((void *)(root ? (uintptr_t)root : (uintptr_t)paging_pae_get_current_dir()),
                         (uint32_t)virt);
    else
        paging_unmap(hepu_vmm_to_dir_32(root), (uint32_t)virt);
}
static inline uint64_t hepu_vmm_virt_to_phys(uint64_t root, uint64_t virt) {
    if (paging_pae_is_enabled())
        return paging_pae_get_phys((void *)(root ? (uintptr_t)root : (uintptr_t)paging_pae_get_current_dir()),
                                    (uint32_t)virt);
    return (uint64_t)paging_get_phys(hepu_vmm_to_dir_32(root), (uint32_t)virt);
}
static inline void hepu_vmm_load_root(uint64_t root) {
    if (root == 0) return; /* 空操作 */
    if (paging_pae_is_enabled())
        paging_pae_switch((void *)(uintptr_t)root);
    else
        paging_switch_dir((page_directory_t *)(uintptr_t)root);
}
static inline uint64_t hepu_vmm_get_kernel_root(void) {
    if (paging_pae_is_enabled())
        return (uint64_t)(uintptr_t)paging_pae_get_kernel_dir();
    return (uint64_t)(uintptr_t)paging_get_kernel_dir();
}
static inline uint64_t hepu_vmm_get_current_root(void) {
    if (paging_pae_is_enabled())
        return (uint64_t)(uintptr_t)paging_pae_get_current_dir();
    return (uint64_t)(uintptr_t)paging_get_current_dir();
}
static inline uint64_t hepu_vmm_create_root(void) {
    if (paging_pae_is_enabled())
        return (uint64_t)(uintptr_t)paging_pae_create_dir();
    return (uint64_t)(uintptr_t)paging_create_dir();
}
static inline void hepu_vmm_free_root(uint64_t root) {
    if (root == 0) return;
    /* PAE 没有 paging_pae_free_dir，用 stub */
    extern void paging_pae_free_dir_impl(void *pdpt);
    if (paging_pae_is_enabled())
        paging_pae_free_dir_impl((void *)(uintptr_t)root);
    else
        paging_free_dir((page_directory_t *)(uintptr_t)root);
}
static inline int hepu_vmm_set_flags(uint64_t root, uint64_t virt, uint64_t flags) {
    return paging_set_flags(hepu_vmm_to_dir_32(root), (uint32_t)virt, (uint32_t)flags);
}
static inline void hepu_vmm_flush_tlb_single(uint64_t virt) {
    /* 32 位通过 invlpg 实现——由 paging.c 内部处理 */
    extern void paging_flush_tlb_single_impl(uint32_t virt);
    paging_flush_tlb_single_impl((uint32_t)virt);
}
static inline int hepu_vmm_page_present(uint64_t root, uint64_t virt) {
    return hepu_vmm_virt_to_phys(root, virt) != 0 ? 1 : 0;
}

#endif /* PAGING_H */
