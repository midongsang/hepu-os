/* ============================================================================
 * 赫普OS - 分页虚拟内存管理 (V0.98深化)
 * x86 二级分页：页目录(PD) + 页表(PT)
 * 新增：按需分配(Demand Paging) + 写时复制(COW) + 页错误统计
 * ========================================================================== */
#include <stdint.h>
#include <stddef.h>
#include "paging.h"
#include "process.h"  /* V3.87-E7: 资源限额记账 */

#include "mm.h"
#include "kheap.h"
#include "string.h"
#include "vga.h"
#include "pae.h"  /* V6.79: PAE支持 */

/* 汇编函数：加载 CR3 启用分页 */
extern void paging_load_dir(uint32_t dir_phys);
extern void paging_enable_paging(void);

/* V7.72安全修复: 内存隔离配置（漏洞A）
 * 默认启用: 用户进程无法访问内核区
 * 可通过启动参数 mem_isolation=0 禁用（兼容性回退） */
int memory_isolation_enabled = 1;

/* V7.87: 内存隔离初始化——解析启动参数
 * 支持: mem_isolation=0 禁用隔离, mem_isolation=1 启用隔离(默认)
 * 在paging_init之后、第一个用户进程创建之前调用 */
void paging_init_memory_isolation(void)
{
    extern int multiboot_has_param(const char *key);
    if (multiboot_has_param("mem_isolation=0")) {
        memory_isolation_enabled = 0;
        vga_puts("[PAGING] memory isolation DISABLED by cmdline (compat mode)\n");
    } else {
        memory_isolation_enabled = 1;
        vga_puts("[PAGING] memory isolation ENABLED (user/kernel separated)\n");
    }
}

/* V6.79: PAE模式标记 (检测完成，实际页表切换待独立分支) */
static int use_pae = 0;

static page_directory_t *kernel_dir = NULL;

/* V2.22: 获取内核页目录(供进程模块映射内核栈等) */
page_directory_t *paging_get_kernel_dir(void)
{
    return kernel_dir;
}
static page_directory_t *current_dir = NULL;

/* 前向声明 */
static page_entry_t *get_page_entry(page_directory_t *dir, uint32_t virt, int create);

/* 页错误统计 */
static uint32_t pf_total = 0;
static uint32_t pf_demand_alloc = 0;
static uint32_t pf_cow = 0;
static uint32_t pf_protection = 0;
static uint32_t pf_kernel = 0;
static uint32_t pf_user = 0;

/* COW标记：使用页表项的available位(bit 9)标记 */
#define PAGE_COW_FLAG  0x200  /* bit 9 */

/* 物理页分配（使用 pmm） */
static uint32_t alloc_frame(void)
{
    return mm_alloc_page();
}

static void __attribute__((unused)) free_frame(uint32_t phys)
{
    mm_free_page(phys);
}

/* 初始化分页 */
void paging_init(uint32_t mem_size_kb)
{
    (void)mem_size_kb;
    vga_puts("[PAGING] init paging ...\n");

    /* V6.79: 检测CPU是否支持PAE */
    if (pae_cpu_supported()) {
        use_pae = 1;
        vga_puts("[PAGING] CPU supports PAE (36-bit addressing, 64GB max)\n");
        extern void serial_puts(uint16_t, const char *);
        serial_puts(0x3F8, "[PAGING] CPU supports PAE (64GB max)\n");
        /* 注意: 完整PAE页表切换正在开发中，当前仍使用非PAE分页 */
        vga_puts("[PAGING] PAE detected, using legacy paging (full PAE switch pending)\n");
        serial_puts(0x3F8, "[PAGING] PAE detected, legacy paging active\n");
    } else {
        vga_puts("[PAGING] CPU does not support PAE, using legacy 32-bit paging\n");
        extern void serial_puts(uint16_t, const char *);
        serial_puts(0x3F8, "[PAGING] no PAE, legacy 32-bit paging\n");
    }

    /* 分配内核页目录（物理地址连续） */
    uint32_t dir_phys = alloc_frame();
    kernel_dir = (page_directory_t *)dir_phys;
    memset(kernel_dir, 0, sizeof(page_directory_t));

    /* 身份映射前 16MB 内存（内核空间）
     * V2.19: 加 PAGE_USER——用户态进程(init/procb)需要访问/取指内核
     * identity 区(user_init_start 位于内核 .text 0x100xxx)。
     * 之前无 USER 位, ring3 取指立即 #PF, 用户态进程从未真正运行,
     * 导致 "enter user proc" 后无输出、Shell 也无法启动.
     * 原型阶段不追求 MMU 隔离(TSS 亦标 TBD), 全 USER 可接受.
     * V3.86: 范围从 16MB 扩到 256MB 全物理直映——用户代码加载
     * (hug_loader 复制 .hug 到 pmm 分配的高区物理帧) 需要内核能
     * 直接 memcpy 写任意物理地址, 否则 16MB 外写物理地址会触发
     * demand paging 映射到错误的帧(实测 clock.hug 代码区全零). */
    uint32_t max_pages = (256 * 1024 * 1024) / PAGE_SIZE;
    for (uint32_t i = 0; i < max_pages; i++) {
        uint32_t phys = i * PAGE_SIZE;
        uint32_t virt = i * PAGE_SIZE;
        paging_map(kernel_dir, virt, phys,
                   PAGE_PRESENT | PAGE_WRITE | PAGE_USER);
    }

    /* 映射内核堆区域 (16MB - 32MB) */
    for (uint32_t i = 16 * 1024 * 1024; i < 32 * 1024 * 1024; i += PAGE_SIZE) {
        paging_map(kernel_dir, i, i,
                   PAGE_PRESENT | PAGE_WRITE | PAGE_USER);
    }

    current_dir = kernel_dir;
    pf_total = pf_demand_alloc = pf_cow = pf_protection = 0;
    pf_kernel = pf_user = 0;

    /* V6.85: 完整性检查——验证前256MB所有页都已映射，修复alloc_frame偶发失败导致的页表空洞 */
    {
        extern void serial_puts(uint16_t, const char *);
        extern void serial_puthex(uint16_t, uint32_t);
        serial_puts(0x3F8, "[PAGING] starting integrity check...\n");
        int fixed = 0;
        for (uint32_t addr = 0; addr < 256 * 1024 * 1024; addr += PAGE_SIZE) {
            page_entry_t *e = get_page_entry(kernel_dir, addr, 0);
            if (!e || !e->present) {
                paging_map(kernel_dir, addr, addr,
                           PAGE_PRESENT | PAGE_WRITE | PAGE_USER);
                fixed++;
            }
        }
        serial_puts(0x3F8, "[PAGING] integrity check done, fixed=");
        serial_puthex(0x3F8, fixed);
        serial_puts(0x3F8, "\n");
    }

    /* V6.85: 登记所有页表使用的物理页，防止mm_alloc_page返回页表页导致覆盖 */
    {
        extern void mm_register_page_table_page(uint32_t);
        int marked = 0;
        for (int i = 0; i < 1024; i++) {
            if (kernel_dir->entries[i].present) {
                uint32_t table_phys = kernel_dir->entries[i].frame << 12;
                if (table_phys) {
                    mm_register_page_table_page(table_phys);
                    marked++;
                }
            }
        }
        /* 登记页目录本身 */
        mm_register_page_table_page(dir_phys);
        marked++;
        extern void serial_puts(uint16_t, const char *);
        extern void serial_puthex(uint16_t, uint32_t);
        serial_puts(0x3F8, "[PAGING] registered ");
        serial_puthex(0x3F8, marked);
        serial_puts(0x3F8, " page table pages\n");
    }

    vga_printf("[PAGING] kernel pagedir created done(physical addr =0x%x)\n", dir_phys);
    vga_puts("[PAGING] identity map: 0x00000000 - 0x01000000(16MB)\n");

    /* V7.72安全修复: 内存隔离状态（漏洞A） */
    if (memory_isolation_enabled) {
        vga_puts("[PAGING] memory isolation ENABLED (user cannot access kernel)\n");
    } else {
        vga_puts("[PAGING] memory isolation DISABLED (compatibility mode)\n");
    }
}

/* 启用分页 */
void paging_enable(void)
{
    if (!kernel_dir) return;
    uint32_t dir_phys = (uint32_t)kernel_dir;
    paging_load_dir(dir_phys);
    paging_enable_paging();
    vga_puts("[PAGING] paging enabled \n");
}

/* 获取页表项指针 */
static page_entry_t *get_page_entry(page_directory_t *dir, uint32_t virt,
                                     int create)
{
    uint32_t dir_idx = virt >> 22;
    uint32_t table_idx = (virt >> 12) & 0x3FF;
    page_dir_entry_t *dir_e = &dir->entries[dir_idx];

    if (!dir_e->present) {
        if (!create) return NULL;
        uint32_t table_phys = alloc_frame();
        if (!table_phys) return NULL;
        page_table_t *table = (page_table_t *)table_phys;
        memset(table, 0, sizeof(page_table_t));
        dir_e->frame = table_phys >> 12;
        dir_e->present = 1;
        dir_e->writable = 1;
        dir_e->user = 1;
    }
    page_table_t *table = (page_table_t *)(dir_e->frame << 12);
    return &table->entries[table_idx];
}

/* 映射虚拟地址到物理地址 */
int paging_map(page_directory_t *dir, uint32_t virt, uint32_t phys,
               uint32_t flags)
{
    page_entry_t *entry = get_page_entry(dir, virt, 1);
    if (!entry) return -1;
    entry->frame = phys >> 12;
    entry->present = (flags & PAGE_PRESENT) ? 1 : 0;
    entry->writable = (flags & PAGE_WRITE) ? 1 : 0;
    entry->user = (flags & PAGE_USER) ? 1 : 0;
    return 0;
}

/* 取消映射 */
void paging_unmap(page_directory_t *dir, uint32_t virt)
{
    page_entry_t *entry = get_page_entry(dir, virt, 0);
    if (entry && entry->present) {
        entry->present = 0;
        entry->frame = 0;
    }
}

/* 获取虚拟地址对应的物理地址 */
uint32_t paging_get_phys(page_directory_t *dir, uint32_t virt)
{
    page_entry_t *entry = get_page_entry(dir, virt, 0);
    if (!entry || !entry->present) return 0;
    return (entry->frame << 12) | (virt & 0xFFF);
}

/* 设置页标志 */
int paging_set_flags(page_directory_t *dir, uint32_t virt, uint32_t flags)
{
    page_entry_t *entry = get_page_entry(dir, virt, 0);
    if (!entry) return -1;
    entry->writable = (flags & PAGE_WRITE) ? 1 : 0;
    entry->user = (flags & PAGE_USER) ? 1 : 0;
    return 0;
}

/* 标记COW页（只读+COW标记） */
int paging_mark_cow(page_directory_t *dir, uint32_t virt)
{
    page_entry_t *entry = get_page_entry(dir, virt, 0);
    if (!entry || !entry->present) return -1;
    entry->writable = 0;  /* 设为只读 */
    /* 使用available位标记COW */
    *(volatile uint32_t *)entry |= PAGE_COW_FLAG;
    return 0;
}

/* 检查是否COW页 */
int paging_is_cow(page_directory_t *dir, uint32_t virt)
{
    page_entry_t *entry = get_page_entry(dir, virt, 0);
    if (!entry) return 0;
    return (*(volatile uint32_t *)entry & PAGE_COW_FLAG) ? 1 : 0;
}

/* 切换页目录 */
void paging_switch_dir(page_directory_t *dir)
{
    if (!dir) return;
    current_dir = dir;
    paging_load_dir((uint32_t)dir);
}

page_directory_t *paging_get_current_dir(void)
{
    return current_dir;
}

/* 创建新的页目录（用于用户进程） */
page_directory_t *paging_create_dir(void)
{
    uint32_t dir_phys = alloc_frame();
    if (!dir_phys) return NULL;
    page_directory_t *dir = (page_directory_t *)dir_phys;
    memset(dir, 0, sizeof(page_directory_t));

    /* V7.72安全修复: 内存隔离（漏洞A）
     * 原代码复制内核全部1024个PDE，且内核页表项带PAGE_USER标志，
     * 用户进程可直接读写syscall表、IDT、内核堆、内核代码。
     *
     * 修复: 复制内核PDE时移除PAGE_USER标志，用户进程无法访问内核区。
     * 注意: 内核态(ring0)仍可访问所有内存，PAGE_USER只影响ring3。
     *
     * 兼容性: 当前用户态程序可能依赖访问内核内存（如sys_malloc返回内核地址），
     * 因此提供配置选项，默认启用隔离，可通过启动参数禁用。 */
    extern int memory_isolation_enabled;
    for (int i = 0; i < 1024; i++) {
        dir->entries[i] = kernel_dir->entries[i];
        if (memory_isolation_enabled && dir->entries[i].present) {
            /* 移除用户可访问标志 */
            dir->entries[i].user = 0;
        }
    }
    return dir;
}

/* V3.78: 释放进程页目录——只归还顶层目录帧。
 * 注意: PDE 浅共享内核页表页, 页表页归内核所有不可动;
 * 用户栈物理页由 user_stack_raw(kheap) 独立释放。
 * 释放前调用方必须先 paging_switch_dir(kernel_dir), 否则 CR3 悬空。 */
void paging_free_dir(page_directory_t *dir)
{
    if (!dir) return;
    if (dir == kernel_dir) return;      /* 内核目录永不释放 */
    free_frame((uint32_t)dir);
    vga_puts("[PAGING] dir freed\n");
}

/* 按需分配：页不存在时自动分配物理页 */
static int handle_demand_paging(uint32_t fault_addr)
{
    uint32_t virt = fault_addr & ~(PAGE_SIZE - 1);
    uint32_t phys = alloc_frame();
    if (!phys) return -1;

    /* 清零新页 */
    memset((void *)phys, 0, PAGE_SIZE);

    /* 映射到当前页目录 */
    int ret = paging_map(current_dir, virt, phys,
                         PAGE_PRESENT | PAGE_WRITE | PAGE_USER);
    if (ret < 0) {
        free_frame(phys);
        return -1;
    }
    pf_demand_alloc++;

    /* V3.87-E7: 缺页分配记账——超限则撤销映射并返回失败(进程将被判fatal) */
    {
        extern process_t *current;
        extern int process_mem_charge(process_t *, uint32_t);
        /* V3.89-E22: 内核态缺页(current==NULL)不记账, 防止 ASSERT 崩溃 */
        if (current && process_mem_charge(current, 1) != 0) {
            paging_unmap(current_dir, virt);
            free_frame(phys);
            pf_demand_alloc--;
            return -1;
        }
    }
    return 0;
}

/* 写时复制：COW页被写时复制 */
static int handle_cow(uint32_t fault_addr)
{
    uint32_t virt = fault_addr & ~(PAGE_SIZE - 1);
    page_entry_t *entry = get_page_entry(current_dir, virt, 0);
    if (!entry || !entry->present) return -1;

    uint32_t old_phys = entry->frame << 12;
    uint32_t new_phys = alloc_frame();
    if (!new_phys) return -1;

    /* 复制页面内容 */
    memcpy((void *)new_phys, (void *)old_phys, PAGE_SIZE);

    /* V3.87-E7: COW 复制页记账(超限则回滚) */
    {
        extern process_t *current;
        extern int process_mem_charge(process_t *, uint32_t);
        if (current && process_mem_charge(current, 1) != 0) {
            free_frame(new_phys);
            return -1;
        }
    }

    /* 更新映射：新物理页，可写，清除COW标记 */
    entry->frame = new_phys >> 12;
    entry->writable = 1;
    *(volatile uint32_t *)entry &= ~PAGE_COW_FLAG;

    pf_cow++;
    return 0;
}

/* 页错误处理 (ISR 14) — V0.98深化：支持按需分配+COW */
void page_fault_handler(uint32_t fault_addr, uint32_t error_code)
{
    int present = error_code & 0x1;
    int write = error_code & 0x2;
    int user = error_code & 0x4;
    int reserved = error_code & 0x8;

    pf_total++;
    if (user) pf_user++; else pf_kernel++;

    /* 保留位错误：不可恢复 */
    if (reserved) {
        goto fatal;
    }

    /* 内核空间地址范围检查：0-32MB为内核区域，不做按需分配 */
    if (fault_addr < 0x2000000 && !user) {
        goto fatal;
    }

    /* 情况1：页不存在 — 按需分配 */
    if (!present) {
        if (handle_demand_paging(fault_addr) == 0) {
            return;  /* 成功，从中断返回重试指令 */
        }
        goto fatal;  /* 内存不足 */
    }

    /* 情况2：保护违规 — 检查是否COW */
    if (write && paging_is_cow(current_dir, fault_addr)) {
        if (handle_cow(fault_addr) == 0) {
            return;  /* COW复制成功 */
        }
        goto fatal;
    }

    /* 其他保护违规 */
    pf_protection++;
    goto fatal;

fatal:
    vga_set_color(VGA_COLOR_RED, VGA_COLOR_BLACK);
    vga_printf("\n[PAGING] fatal page fault \n");
    vga_printf(" fault addr: 0x%x\n", fault_addr);
    vga_printf(" type: %s %s %s%s\n",
               present ? " protected 违规 " : " pages not exists ",
               write ? " 写 " : " 读 ",
               user ? " user mode " : " kernel 态 ",
               reserved ? " 保留 bit " : "");
    vga_printf(" stats: total %d on-demand %d COW%d protected %d\n",
               pf_total, pf_demand_alloc, pf_cow, pf_protection);
    vga_puts(" system halted .\n");
    for (;;) {
        __asm__ volatile ("hlt");
    }
}

/* 页错误统计 */
void paging_fault_stats(uint32_t *total, uint32_t *demand,
                        uint32_t *cow, uint32_t *protection)
{
    if (total) *total = pf_total;
    if (demand) *demand = pf_demand_alloc;
    if (cow) *cow = pf_cow;
    if (protection) *protection = pf_protection;
}
