#ifndef PROCESS_PAE_H
#define PROCESS_PAE_H

#include <stdint.h>

/* PAE模式控制 */
void pae_process_mode_enable(void);
int pae_process_mode_is_enabled(void);

/* PAE模式下的进程地址空间操作 */
void *pae_process_create_dir(void);
int pae_process_map(void *pdpt, uint32_t virt, uint64_t phys, uint32_t flags);
void pae_process_switch(void *pdpt);
uint64_t pae_process_get_phys(void *pdpt, uint32_t virt);

/* PAE进程自测 */
void pae_process_selftest(void);

#endif /* PROCESS_PAE_H */
