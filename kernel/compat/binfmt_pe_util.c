/* ============================================================================
 * 赫普OS - Windows PE32 加载器：工具函数
 * 从 binfmt_pe.c 拆分（V6.46 代码重构，大文件拆分目标<500行）
 * 包含：pe_va2phys/pe_u32_read/pe_u32_write/pe_name_read/pe_match_api/
 *       pe_parse/pe_enter_user/pe_mainline_save
 * ========================================================================== */

#include <stdint.h>
#include <string.h>
#include "process.h"
#include "paging.h"
#include "binfmt.h"

uint32_t pe_va2phys(process_t *proc, uint32_t va)
{
    return paging_get_phys(proc->page_dir, va);
}

/* 从用户虚拟地址读/写 (经物理帧) */
int pe_u32_read(process_t *proc, uint32_t va, uint32_t *out)
{
    uint32_t phys = pe_va2phys(proc, va);
    if (!phys) return -1;
    *out = *(const uint32_t *)(uintptr_t)phys;
    return 0;
}
int pe_u32_write(process_t *proc, uint32_t va, uint32_t val)
{
    uint32_t phys = pe_va2phys(proc, va);
    if (!phys) return -1;
    *(uint32_t *)(uintptr_t)phys = val;
    return 0;
}

/* 读取用户虚拟地址处以 \0 结尾的 ASCII 名 (节内安全) */
int pe_name_read(process_t *proc, uint32_t va, char *buf, uint32_t cap)
{
    if (!cap) return -1;
    uint32_t i = 0;
    for (;;) {
        uint32_t phys = pe_va2phys(proc, va + i);
        if (!phys) return -1;
        char c = *(const char *)(uintptr_t)phys;
        if (c == '\0' || i >= cap - 1) break;
        buf[i++] = c;
    }
    buf[i] = '\0';
    return (int)i;
}

/* ---- 导入函数名 → stub 地址匹配 (Win32 Console API 子集) ---- */
uint32_t pe_match_api(const char *name)
{
    if (strcmp(name, "WriteConsoleA") == 0 ||
        strcmp(name, "WriteConsoleW") == 0 ||
        strcmp(name, "WriteFile") == 0) {
        return PE_STUB_WRITE;
    }
    if (strcmp(name, "ReadConsoleA") == 0 ||
        strcmp(name, "ReadFile") == 0) {
        return PE_STUB_READ;
    }
    if (strcmp(name, "ExitProcess") == 0) {
        return PE_STUB_EXIT;
    }
    if (strcmp(name, "GetStdHandle") == 0) {
        return PE_STUB_STDHANDLE;
    }
    if (strcmp(name, "GetCommandLineA") == 0) {
        return PE_STUB_CMDLINE;
    }
    if (strcmp(name, "GetModuleFileNameA") == 0) {
        return PE_STUB_MODNAME;
    }
    if (strcmp(name, "GetTickCount") == 0) {
        return PE_STUB_TICK;
    }
    if (strcmp(name, "Sleep") == 0) {
        return PE_STUB_SLEEP;
    }
    return PE_STUB_UNSUP;
}

/* ---- 解析 PE 头 (越界安全) ---- */
int pe_parse(const uint8_t *data, uint32_t size,
                    pe_coff_header_t *coff, pe_optional_header_t *opt,
                    pe_section_header_t *secs, uint32_t *nsec)
{
    if (size < 0x40 + 4) return BINFMT_ERR_FORMAT;
    const pe_dos_header_t *dos = (const pe_dos_header_t *)data;
    if (dos->e_magic != PE_MZ_MAGIC) return BINFMT_ERR_FORMAT;
    if (dos->e_lfanew == 0 || dos->e_lfanew > size - 4) return BINFMT_ERR_FORMAT;

    const uint8_t *p = data + dos->e_lfanew;
    if (*(const uint32_t *)p != PE_SIG) return BINFMT_ERR_FORMAT;
    if (dos->e_lfanew + 4 + sizeof(pe_coff_header_t) > size) {
        return BINFMT_ERR_FORMAT;
    }
    const pe_coff_header_t *c = (const pe_coff_header_t *)(p + 4);
    if (c->Machine != PE_MACHINE_I386) return BINFMT_ERR_UNSUP;

    uint32_t opt_off = (uint32_t)(p + 4 - data) + sizeof(pe_coff_header_t);
    if (opt_off + sizeof(pe_optional_header_t) > size) return BINFMT_ERR_FORMAT;
    const pe_optional_header_t *o = (const pe_optional_header_t *)(data + opt_off);
    if (o->Magic != PE_MAGIC_PE32) return BINFMT_ERR_UNSUP;

    uint32_t sec_off = opt_off + c->SizeOfOptionalHeader;
    if (c->NumberOfSections > PE_MAX_SECTIONS) return BINFMT_ERR_FORMAT;
    if (sec_off + (uint64_t)c->NumberOfSections * sizeof(pe_section_header_t)
        > size) return BINFMT_ERR_FORMAT;

    *coff = *c;
    *opt  = *o;
    for (uint32_t i = 0; i < c->NumberOfSections; i++) {
        const pe_section_header_t *s =
            (const pe_section_header_t *)(data + sec_off +
                                          (uint64_t)i * sizeof(pe_section_header_t));
        secs[i] = *s;
    }
    *nsec = c->NumberOfSections;
    return BINFMT_OK;
}

/* 主线保存点 (noinline) */
int pe_mainline_save(void) __attribute__((noinline));
/* 进入用户态 (noinline) */
int pe_enter_user(process_t *proc) __attribute__((noinline));

/* ---- PE32 加载执行 ---- */
int pe_enter_user(process_t *proc) __attribute__((noinline));
int pe_enter_user(process_t *proc)
{
    if (pe_mainline_save() != 0) {
        /* V6.47 BUG-20修复: sys_exit longjmp 回来后, sys_exit 已将
         * current 置为 NULL(回收分支). 后续 vga_printf/kmalloc 等若
         * 引用 current 会 NULL 解引用 → 崩溃. 恢复 current 为 Shell
         * 进程(PID=1), 保证主线 Shell 环境完整. */
        extern process_t *current;
        extern process_t proc_table[];
        if (!current) {
            for (int i = 0; i < 64; i++) {
                if (proc_table[i].pid == 1 && proc_table[i].state != 0) {
                    current = &proc_table[i];
                    break;
                }
            }
        }
        return BINFMT_OK;   /* PE 进程已退出, 回主线 */
    }
    process_enter_user_mode(proc);
    return BINFMT_OK;
}

/* 主线保存点——与 ELF 加载器共用全局 mainline_jmp_buf
 * (sys_exit longjmp 回最近 setjmp 者: PE → 本处) */
int pe_mainline_save(void) __attribute__((noinline));
int pe_mainline_save(void)
{
    extern uint32_t mainline_jmp_buf[6];
    extern int hepu_setjmp(uint32_t *buf);
    return hepu_setjmp(mainline_jmp_buf);
}
