/* ============================================================================
 * 赫普OS - PAE兼容层 (V6.80 独立分支)
 *
 * 让现有代码自动适配PAE模式，无需修改73个调用点
 * - paging_map_compat: 自动分发到传统或PAE版本
 * - paging_create_dir_compat: 自动创建对应模式的地址空间
 * - paging_switch_dir_compat: 自动切换对应模式的页表
 *
 * 使用方法：在PAE模式下，将这些函数替换原有函数
 * （通过函数指针或宏定义实现）
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include "paging.h"
#include "pae.h"
#include "vga.h"
#include "serial.h"

/* PAE模式标志（由pae_safe_switch设置） */
extern int pae_is_switched(void);

/* 兼容层：映射页面 */
int paging_map_compat(void *dir, uint32_t virt, uint64_t phys, uint32_t flags)
{
    if (pae_is_switched()) {
        /* PAE模式：调用PAE版本 */
        extern int paging_pae_map(void *, uint32_t, uint64_t, uint32_t);
        return paging_pae_map(dir, virt, phys, flags);
    } else {
        /* 传统模式：调用传统版本 */
        return paging_map((page_directory_t *)dir, virt, (uint32_t)phys, flags);
    }
}

/* 兼容层：取消映射 */
void paging_unmap_compat(void *dir, uint32_t virt)
{
    if (pae_is_switched()) {
        extern void paging_pae_unmap(void *, uint32_t);
        paging_pae_unmap(dir, virt);
    } else {
        paging_unmap((page_directory_t *)dir, virt);
    }
}

/* 兼容层：获取物理地址 */
uint64_t paging_get_phys_compat(void *dir, uint32_t virt)
{
    if (pae_is_switched()) {
        extern uint64_t paging_pae_get_phys(void *, uint32_t);
        return paging_pae_get_phys(dir, virt);
    } else {
        return (uint64_t)paging_get_phys((page_directory_t *)dir, virt);
    }
}

/* 兼容层：创建地址空间 */
void *paging_create_dir_compat(void)
{
    if (pae_is_switched()) {
        extern void *paging_pae_create_dir(void);
        return paging_pae_create_dir();
    } else {
        return (void *)paging_create_dir();
    }
}

/* 兼容层：切换地址空间 */
void paging_switch_dir_compat(void *dir)
{
    if (pae_is_switched()) {
        extern void paging_pae_switch(void *);
        paging_pae_switch(dir);
    } else {
        paging_switch_dir((page_directory_t *)dir);
    }
}

/* 兼容层：获取内核地址空间 */
void *paging_get_kernel_dir_compat(void)
{
    if (pae_is_switched()) {
        extern void *paging_pae_get_kernel_dir(void);
        return paging_pae_get_kernel_dir();
    } else {
        return (void *)paging_get_kernel_dir();
    }
}

/* 兼容层：获取当前地址空间 */
void *paging_get_current_dir_compat(void)
{
    if (pae_is_switched()) {
        extern void *paging_pae_get_current_dir(void);
        return paging_pae_get_current_dir();
    } else {
        return (void *)paging_get_current_dir();
    }
}

/* 兼容层自测 */
void paging_compat_selftest(void)
{
    serial_puts(0x3F8, "[PAE-COMPAT] selftest: start\n");
    vga_puts("[PAE-COMPAT] selftest: start\n");

    /* 测试传统模式（当前应该是传统模式） */
    void *dir = paging_create_dir_compat();
    if (!dir) {
        vga_puts("[PAE-COMPAT] FAIL: create_dir\n");
        return;
    }
    vga_puts("[PAE-COMPAT] create_dir OK\n");

    int ret = paging_map_compat(dir, 0x50000000, 0x200000, 0x3);
    if (ret != 0) {
        vga_printf("[PAE-COMPAT] FAIL: map (ret=%d)\n", ret);
        return;
    }
    vga_puts("[PAE-COMPAT] map OK\n");

    uint64_t phys = paging_get_phys_compat(dir, 0x50000000);
    vga_printf("[PAE-COMPAT] get_phys: 0x50000000 -> 0x%x%x\n",
               (uint32_t)(phys >> 32), (uint32_t)phys);

    vga_puts("[PAE-COMPAT] selftest: PASS (legacy mode)\n");
    serial_puts(0x3F8, "[PAE-COMPAT] selftest PASS (legacy mode)\n");
}
