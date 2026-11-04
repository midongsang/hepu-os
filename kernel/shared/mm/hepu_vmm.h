/* ============================================================================
 * 赫普OS - 统一虚拟内存/页表 API
 * V0.54.6: 两边共用的页表接口（32 位 paging_* + 64 位 vmm64_* 统一在此）
 *
 * 设计原则：
 *   - 虚拟地址、物理地址一律 uint64_t
 *   - root 用 uint64_t 承载页表根（32 位 page_directory_t* 强转，64 位 pml4 原生）
 *   - root=0 表示当前页表（CR3 / current_dir）
 *   - flags 用 mm_shared.h 的 MM_PROT_* + 此处的 VMM_FLAG_*
 *   - 两边各自包装，调用点零改动
 * ========================================================================== */

#ifndef HEPU_VMM_H
#define HEPU_VMM_H

#include <stdint.h>
#include <stddef.h>
#include "mm_shared.h"

/* ---- 页表标志（补充 mm_shared.h 的 MM_PROT_*） ---- */
#define VMM_FLAG_PRESENT   0x0001
#define VMM_FLAG_WRITABLE  0x0002
#define VMM_FLAG_USER      0x0004
#define VMM_FLAG_EXECUTE   0x0008
#define VMM_FLAG_HUGE      0x0080   /* 2MB 大页 */
#define VMM_FLAG_NX        0x8000000000000000ULL  /* 64 位 NX bit */

/* ---- 核心 API ---- */

/* 映射 virt → phys，flags 控制权限。root=0 表示当前页表。返回 0 成功，-1 失败 */
static inline int hepu_vmm_map_page(uint64_t root, uint64_t virt, uint64_t phys, uint64_t flags);

/* 取消映射 */
static inline void hepu_vmm_unmap_page(uint64_t root, uint64_t virt);

/* 虚拟地址 → 物理地址；未映射返回 0 */
static inline uint64_t hepu_vmm_virt_to_phys(uint64_t root, uint64_t virt);

/* root=0 时加载当前页表到 CR3/切换 directory */
static inline void hepu_vmm_load_root(uint64_t root);

/* 获取内核页表根 */
static inline uint64_t hepu_vmm_get_kernel_root(void);

/* 获取当前页表根 */
static inline uint64_t hepu_vmm_get_current_root(void);

/* 创建新页表根（复制内核空间）；返回 root；失败返回 0 */
static inline uint64_t hepu_vmm_create_root(void);

/* 释放页表根（必须是 hepu_vmm_create_root 返回的） */
static inline void hepu_vmm_free_root(uint64_t root);

/* 修改页权限（flags 同 map_page） */
static inline int hepu_vmm_set_flags(uint64_t root, uint64_t virt, uint64_t flags);

/* 刷新单页 TLB */
static inline void hepu_vmm_flush_tlb_single(uint64_t virt);

/* 查询页是否存在（1=存在，0=不存在） */
static inline int hepu_vmm_page_present(uint64_t root, uint64_t virt);

#endif /* HEPU_VMM_H */
