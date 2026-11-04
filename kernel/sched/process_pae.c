/* ============================================================================
 * 赫普OS - PAE模式进程管理 (V6.80 独立分支)
 *
 * PAE模式下进程创建和切换的适配层
 * - page_dir字段存储PDPT物理地址（与传统模式兼容，汇编无需修改）
 * - paging_create_dir调用PAE版本
 * - paging_map调用PAE版本
 * - paging_switch_dir调用PAE版本
 *
 * 注意：本模块仅用于PAE分支开发，当前主线仍使用传统分页
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include "paging.h"
#include "pae.h"
#include "process.h"
#include "vga.h"
#include "serial.h"

/* PAE模式标志（由启动参数设置） */
static int pae_mode_enabled = 0;

/* 启用PAE模式 */
void pae_process_mode_enable(void)
{
    pae_mode_enabled = 1;
    serial_puts(0x3F8, "[PAE-PROC] PAE process mode enabled\n");
}

/* 查询PAE模式 */
int pae_process_mode_is_enabled(void)
{
    return pae_mode_enabled;
}

/* PAE模式下创建进程地址空间 */
void *pae_process_create_dir(void)
{
    if (!pae_mode_enabled) return NULL;

    /* 使用PAE版本创建PDPT */
    extern void *paging_pae_create_dir(void);
    void *pdpt = paging_pae_create_dir();
    if (!pdpt) {
        serial_puts(0x3F8, "[PAE-PROC] failed to create PDPT\n");
        return NULL;
    }

    return pdpt;
}

/* PAE模式下映射页面 */
int pae_process_map(void *pdpt, uint32_t virt, uint64_t phys, uint32_t flags)
{
    if (!pae_mode_enabled || !pdpt) return -1;

    extern int paging_pae_map(void *, uint32_t, uint64_t, uint32_t);
    return paging_pae_map(pdpt, virt, phys, flags);
}

/* PAE模式下切换地址空间 */
void pae_process_switch(void *pdpt)
{
    if (!pae_mode_enabled || !pdpt) return;

    extern void paging_pae_switch(void *);
    paging_pae_switch(pdpt);
}

/* PAE模式下获取物理地址 */
uint64_t pae_process_get_phys(void *pdpt, uint32_t virt)
{
    if (!pae_mode_enabled || !pdpt) return 0;

    extern uint64_t paging_pae_get_phys(void *, uint32_t);
    return paging_pae_get_phys(pdpt, virt);
}

/* PAE进程自测（验证数据结构，不实际切换） */
void pae_process_selftest(void)
{
    serial_puts(0x3F8, "[PAE-PROC] selftest: start\n");
    vga_puts("[PAE-PROC] selftest: start\n");

    /* 检测CPU PAE支持 */
    extern int pae_cpu_supported(void);
    if (!pae_cpu_supported()) {
        vga_puts("[PAE-PROC] CPU does not support PAE, skip\n");
        return;
    }

    /* 测试创建PDPT */
    void *pdpt = pae_process_create_dir();
    if (!pdpt) {
        vga_puts("[PAE-PROC] selftest: FAIL (create dir)\n");
        return;
    }
    vga_puts("[PAE-PROC] PDPT created\n");

    /* 测试映射 */
    int ret = pae_process_map(pdpt, 0x40000000, 0x100000, 0x3);
    if (ret != 0) {
        vga_printf("[PAE-PROC] selftest: FAIL (map, ret=%d)\n", ret);
        return;
    }
    vga_puts("[PAE-PROC] map 0x40000000 -> 0x100000 OK\n");

    /* 测试地址转换 */
    uint64_t phys = pae_process_get_phys(pdpt, 0x40000000);
    vga_printf("[PAE-PROC] virt 0x40000000 -> phys 0x%x%x\n",
               (uint32_t)(phys >> 32), (uint32_t)phys);

    vga_puts("[PAE-PROC] selftest: PASS (data structures)\n");
    serial_puts(0x3F8, "[PAE-PROC] selftest PASS\n");
}
