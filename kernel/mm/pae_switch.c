/* ============================================================================
 * 赫普OS - PAE安全切换 (V6.80 独立分支)
 *
 * 安全的PAE页表切换流程：
 * 1. 检查CPU PAE支持
 * 2. 验证PAE页表结构
 * 3. 切换到PAE模式
 * 4. 验证切换后关键地址映射
 * 5. 失败时输出诊断信息
 *
 * 警告：PAE切换是高风险操作，可能导致系统崩溃
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include "paging.h"
#include "pae.h"
#include "vga.h"
#include "serial.h"

/* PAE切换状态 */
static int pae_switch_state = 0;
static void pae_dump_page_tables(void *pdpt_v);  /* 0=未切换, 1=已切换 */

/* 验证PAE页表结构（切换前调用） */
static int pae_verify_page_tables(void *pdpt)
{
    if (!pdpt) return -1;

    extern uint64_t paging_pae_get_phys(void *, uint32_t);

    /* 验证内核空间映射 (0-64MB，抽样验证避免太慢) */
    for (uint32_t addr = 0; addr < 64 * 1024 * 1024; addr += 1024 * 1024) {
        uint64_t phys = paging_pae_get_phys(pdpt, addr);
        if (phys != addr) {
            char buf[128];
            snprintf(buf, sizeof(buf), "[PAE-SWITCH] verify FAIL: 0x%x -> 0x%x%x (expected 0x%x)\n",
                     addr, (uint32_t)(phys >> 32), (uint32_t)phys, addr);
            serial_puts(0x3F8, buf);
            vga_printf("[PAE-SWITCH] verify FAIL: 0x%x -> 0x%x%x\n",
                       addr, (uint32_t)(phys >> 32), (uint32_t)phys);
            return -2;
        }
    }

    /* 验证帧缓冲区映射 (0x80000000) */
    uint64_t fb_phys = paging_pae_get_phys(pdpt, 0x80000000);
    if (fb_phys != 0x80000000) {
        vga_printf("[PAE-SWITCH] verify FAIL: FB 0x80000000 -> 0x%x%x\n",
                   (uint32_t)(fb_phys >> 32), (uint32_t)fb_phys);
        return -3;
    }

    serial_puts(0x3F8, "[PAE-SWITCH] page table verification passed\n");
    return 0;
}

/* 执行PAE切换（汇编级操作）
 * 安全流程：关中断→禁用分页→设置CR4.PAE→加载CR3→启用分页→刷新TLB→开中断
 * 禁用分页后CPU使用物理地址，内核在前64MB恒等映射范围内，安全 */
static void pae_do_switch(uint32_t pdpt_phys)
{
    __asm__ volatile(
        "cli\n"
        /* 禁用分页 (CR0.PG=0) */
        "mov %%cr0, %%eax\n"
        "and $0x7FFFFFFF, %%eax\n"
        "mov %%eax, %%cr0\n"
        /* 设置CR4.PAE */
        "mov %%cr4, %%eax\n"
        "or $0x20, %%eax\n"
        "mov %%eax, %%cr4\n"
        /* 加载PDPT到CR3 */
        "mov %0, %%cr3\n"
        /* 启用分页 (CR0.PG=1) */
        "mov %%cr0, %%eax\n"
        "or $0x80000000, %%eax\n"
        "mov %%eax, %%cr0\n"
        /* 刷新TLB */
        "mov %%cr3, %%eax\n"
        "mov %%eax, %%cr3\n"
        /* 开中断 */
        "sti\n"
        : : "r"(pdpt_phys) : "eax"
    );
}

/* 验证PAE切换后状态 */
static int pae_verify_after_switch(void)
{
    /* 读取CR4，检查PAE位 */
    uint32_t cr4;
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    if (!(cr4 & (1 << 5))) {
        vga_puts("[PAE-SWITCH] FAIL: CR4.PAE not set\n");
        return -1;
    }

    /* 读取CR0，检查PG位 */
    uint32_t cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    if (!(cr0 & 0x80000000)) {
        vga_puts("[PAE-SWITCH] FAIL: CR0.PG not set\n");
        return -2;
    }

    /* 测试内存访问（读写一个已知地址） */
    volatile uint32_t *test = (volatile uint32_t *)0x1000;
    *test = 0x12345678;
    if (*test != 0x12345678) {
        vga_puts("[PAE-SWITCH] FAIL: memory access test failed\n");
        return -3;
    }

    serial_puts(0x3F8, "[PAE-SWITCH] post-switch verification passed\n");
    return 0;
}

/* 安全的PAE切换入口 */
int pae_safe_switch(void)
{
    extern void serial_puts(uint16_t, const char *);
    serial_puts(0x3F8, "[PAE-SWITCH] starting safe PAE switch...\n");
    vga_puts("[PAE-SWITCH] starting safe PAE switch...\n");

    /* 1. 检查CPU PAE支持 */
    extern int pae_cpu_supported(void);
    if (!pae_cpu_supported()) {
        vga_puts("[PAE-SWITCH] FAIL: CPU does not support PAE\n");
        return -1;
    }
    vga_puts("[PAE-SWITCH] CPU supports PAE\n");

    /* 2. 获取PAE页表 */
    extern void *paging_pae_get_kernel_dir(void);
    extern void paging_pae_init(uint32_t);
    void *pdpt = paging_pae_get_kernel_dir();
    if (!pdpt) {
        vga_puts("[PAE-SWITCH] initializing PAE page tables...\n");
        paging_pae_init(8192);  /* 8MB */
        pdpt = paging_pae_get_kernel_dir();
        if (!pdpt) {
            vga_puts("[PAE-SWITCH] FAIL: cannot create PDPT\n");
            return -2;
        }
    }

    /* 3. 验证PAE页表结构 */
    pae_dump_page_tables(pdpt);
    serial_puts(0x3F8, "[PAE-SWITCH] verifying page tables...\n");
    vga_puts("[PAE-SWITCH] verifying page tables...\n");
    if (pae_verify_page_tables(pdpt) != 0) {
        serial_puts(0x3F8, "[PAE-SWITCH] FAIL: page table verification failed\n");
        vga_puts("[PAE-SWITCH] FAIL: page table verification failed\n");
        return -3;
    }
    serial_puts(0x3F8, "[PAE-SWITCH] page tables OK\n");

    /* 4. 执行PAE切换 */
    serial_puts(0x3F8, "[PAE-SWITCH] switching to PAE mode...\n");
    vga_puts("[PAE-SWITCH] switching to PAE mode...\n");
    uint32_t pdpt_phys = (uint32_t)pdpt;
    pae_do_switch(pdpt_phys);
    serial_puts(0x3F8, "[PAE-SWITCH] switch instruction executed\n");

    /* 5. 验证切换后状态 */
    serial_puts(0x3F8, "[PAE-SWITCH] verifying after switch...\n");
    vga_puts("[PAE-SWITCH] verifying after switch...\n");
    if (pae_verify_after_switch() != 0) {
        serial_puts(0x3F8, "[PAE-SWITCH] FAIL: post-switch verification failed\n");
        vga_puts("[PAE-SWITCH] FAIL: post-switch verification failed\n");
        return -4;
    }

    pae_switch_state = 1;
    vga_puts("[PAE-SWITCH] PAE mode enabled successfully!\n");
    serial_puts(0x3F8, "[PAE-SWITCH] PAE mode enabled successfully!\n");
    return 0;
}

/* 查询PAE切换状态 */
int pae_is_switched(void)
{
    return pae_switch_state;
}

/* 诊断：打印页表内容 */
static void pae_dump_page_tables(void *pdpt_v)
{
    extern void serial_puts(uint16_t, const char *);
    char buf[128];
    pae_pdpt_t *pdpt = (pae_pdpt_t *)pdpt_v;

    snprintf(buf, sizeof(buf), "[PAE-DUMP] PDPT at 0x%x\n", (uint32_t)pdpt);
    serial_puts(0x3F8, buf);

    for (int i = 0; i < 4; i++) {
        snprintf(buf, sizeof(buf), "[PAE-DUMP] PDPT[%d] = 0x%x%x (present=%d)\n",
                 i, (uint32_t)(pdpt->e[i] >> 32), (uint32_t)pdpt->e[i],
                 (int)(pdpt->e[i] & 1));
        serial_puts(0x3F8, buf);

        if (pdpt->e[i] & 1) {
            pae_pd_t *pd = (pae_pd_t *)(uint32_t)(pdpt->e[i] & 0xFFFFF000);
            snprintf(buf, sizeof(buf), "[PAE-DUMP]   PD at 0x%x\n", (uint32_t)pd);
            serial_puts(0x3F8, buf);

            for (int j = 0; j < 4; j++) {
                snprintf(buf, sizeof(buf), "[PAE-DUMP]   PD[%d] = 0x%x%x (present=%d)\n",
                         j, (uint32_t)(pd->e[j] >> 32), (uint32_t)pd->e[j],
                         (int)(pd->e[j] & 1));
                serial_puts(0x3F8, buf);

                if ((pd->e[j] & 1) && !(pd->e[j] & 0x80)) {
                    pae_pt_t *pt = (pae_pt_t *)(uint32_t)(pd->e[j] & 0xFFFFF000);
                    for (int k = 0; k < 4; k++) {
                        snprintf(buf, sizeof(buf), "[PAE-DUMP]     PT[%d] = 0x%x%x\n",
                                 k, (uint32_t)(pt->e[k] >> 32), (uint32_t)pt->e[k]);
                        serial_puts(0x3F8, buf);
                    }
                }
            }
        }
    }
}
