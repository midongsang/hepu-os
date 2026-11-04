#ifndef PAE_H
#define PAE_H

#include <stdint.h>

/* PAE页表结构 */
typedef struct { uint64_t e[4]; }   pae_pdpt_t;
typedef struct { uint64_t e[512]; } pae_pd_t;
typedef struct { uint64_t e[512]; } pae_pt_t;

/* 检测CPU是否支持PAE */
int pae_cpu_supported(void);

/* 启用/禁用PAE */
int pae_enable(void);
void pae_disable(void);

/* 创建页表结构 */
pae_pdpt_t *pae_create_pdpt(void);
pae_pd_t *pae_create_pd(void);
pae_pt_t *pae_create_pt(void);

/* 设置页表项 */
void pae_set_pdpt_entry(pae_pdpt_t *pdpt, int index, uint64_t pd_phys,
                         int present, int writable);
void pae_set_pd_entry(pae_pd_t *pd, int index, uint64_t pt_phys,
                       int present, int writable, int user, int large_page);
void pae_set_pt_entry(pae_pt_t *pt, int index, uint64_t phys_addr,
                       int present, int writable, int user, int nx);

/* 映射页面 */
int pae_map_page(pae_pdpt_t *pdpt, uint32_t virt, uint64_t phys,
                  int writable, int user);
int pae_map_2mb_page(pae_pdpt_t *pdpt, uint32_t virt, uint64_t phys,
                      int writable, int user);

/* 加载/刷新 */
void pae_load_pdpt(pae_pdpt_t *pdpt);
void pae_flush_tlb(void);

/* 查询 */
uint64_t pae_get_max_phys_addr(void);
int pae_is_enabled(void);

/* 自测 */
void pae_selftest(void);

/* V6.80: PAE初始化框架 */
void pae_init(void);
int pae_is_initialized(void);
uint32_t pae_get_kernel_pdpt_phys(void);

#endif
