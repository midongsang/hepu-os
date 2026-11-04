#ifndef VMA_H
#define VMA_H

#include <stdint.h>

#define PAGE_SIZE 4096

/* VMA访问标志 */
#define VMA_READ    0x1
#define VMA_WRITE   0x2
#define VMA_EXEC    0x4
#define VMA_SHARED  0x8
#define VMA_USER    0x10  /* 用户态可访问（对应PTE_USER） */

/* 前向声明 */
typedef struct vm_address_space vm_address_space_t;
typedef struct vma_area vma_area_t;

/* 创建/销毁地址空间 */
vm_address_space_t *vma_create_space(void);
void                vma_destroy_space(vm_address_space_t *space);

/* 映射/取消映射 */
int  vma_map(vm_address_space_t *space, uint32_t start, uint32_t length, uint32_t flags);
int  vma_unmap(vm_address_space_t *space, uint32_t start, uint32_t length);

/* 查找/检查 */
vma_area_t *vma_find(vm_address_space_t *space, uint32_t addr);
int         vma_check_access(vm_address_space_t *space, uint32_t addr, uint32_t access_flags);

/* 统计/调试 */
void vma_stats(vm_address_space_t *space, uint32_t *areas, uint32_t *pages, uint32_t *bytes);
void vma_dump(vm_address_space_t *space);

/* V6.50: 与页表集成——映射VMA并分配物理页建立页表映射 */
int  vma_map_with_pages(vm_address_space_t *space, uint32_t start, uint32_t length, uint32_t flags);

/* V6.50: 缺页异常处理——检查地址是否在VMA中，如是则分配物理页并建立映射 */
int  vma_handle_page_fault(vm_address_space_t *space, uint32_t fault_addr, uint32_t error_code);

/* V6.50: VMA自测 */
void vma_selftest(void);

#endif
