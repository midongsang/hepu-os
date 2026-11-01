#ifndef PAGING_COMPAT_H
#define PAGING_COMPAT_H

#include <stdint.h>

/* PAE兼容层：自动适配传统/PAE分页模式 */
int paging_map_compat(void *dir, uint32_t virt, uint64_t phys, uint32_t flags);
void paging_unmap_compat(void *dir, uint32_t virt);
uint64_t paging_get_phys_compat(void *dir, uint32_t virt);
void *paging_create_dir_compat(void);
void paging_switch_dir_compat(void *dir);
void *paging_get_kernel_dir_compat(void);
void *paging_get_current_dir_compat(void);

/* 兼容层自测 */
void paging_compat_selftest(void);

#endif /* PAGING_COMPAT_H */
