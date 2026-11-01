/* ============================================================================
 * 赫普OS - Windows PE32 (i386) 控制台程序加载器
 * V3.87 阶段B-1: 解析/加载/执行最小 PE32 控制台程序 (Console API 子集)
 *
 * 支持范围 (公开 PE/COFF 规范, Microsoft PE Format):
 *   - DOS 头 'MZ' + e_lfanew → PE\0\0 签名
 *   - COFF 头 Machine=0x14C (IMAGE_FILE_MACHINE_I386), PE32 (Magic 0x10B)
 *   - 节表映射 (SectionAlignment=0x1000 页对齐), bss 清零
 *   - 导入表 (Import Directory): KERNEL32.dll 子集 API
 *   - 子集 API 经用户态 trampoline stub 直通 HEPU syscall (内核零改动)
 *
 * 沙箱 (APK 式, 与 ELF 兼容层一致):
 *   - compat_abi=2 → sys_fs 白名单 /dev/ /app/ /data/ 自动生效
 *   - 系统文件/内核文件不可见, 仅标准流可用
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "binfmt.h"
#include "constants.h"
#include "paging.h"
#include "process.h"
#include "vfs.h"
#include "vga.h"
#include "serial.h"


typedef struct {
    uint32_t OriginalFirstThunk;      /* RVA → Hint/Name 表 */
    uint32_t TimeDateStamp;
    uint32_t ForwarderChain;
    uint32_t Name;                    /* RVA → DLL 名 */
    uint32_t FirstThunk;              /* RVA → IAT (加载器改写) */
} pe_import_desc_t;
/* V6.48 BUG-20修复: 修正extern声明大小(与binfmt_pe_stub.c定义一致).
 * 之前硬编码大小全部错误(stub_write[18]实际23等), 导致sizeof偏小,
 * memcpy缺失ret指令, CPU执行全零指令, 最终int 0x80触发时eip错误. */
extern const uint8_t stub_write[23];
extern const uint8_t stub_read[33];
extern const uint8_t stub_exit[12];
extern const uint8_t stub_stdhandle[8];
extern const uint8_t stub_unsup[8];
extern const uint8_t stub_cmdline[8];
extern const uint8_t stub_tick[16];
extern const uint8_t stub_sleep[14];
extern const uint8_t stub_modname[24];
/* PE工具函数已拆分到 binfmt_pe_util.c */
extern uint32_t pe_va2phys(process_t *proc, uint32_t va);
extern int pe_u32_read(process_t *proc, uint32_t va, uint32_t *out);
extern int pe_u32_write(process_t *proc, uint32_t va, uint32_t val);
extern int pe_name_read(process_t *proc, uint32_t va, char *buf, uint32_t cap);
extern uint32_t pe_match_api(const char *name);
extern int pe_parse(const uint8_t *data, uint32_t size, pe_coff_header_t *coff, pe_optional_header_t *opt, pe_section_header_t *secs, uint32_t *nsec);
extern int pe_enter_user(process_t *proc);
extern int pe_mainline_save(void);

/* ---- 内部工具: 用户虚拟地址 → 物理帧 (此刻在内核 CR3, 需查进程页目录) ---- */
int binfmt_pe_load(const char *path, const uint8_t *data, uint32_t size)
{
    pe_coff_header_t coff;
    pe_optional_header_t opt;
    pe_section_header_t secs[PE_MAX_SECTIONS];
    uint32_t nsec = 0;

    int rc = pe_parse(data, size, &coff, &opt, secs, &nsec);
    if (rc != BINFMT_OK) {
        vga_printf("[PE] %s: parse fail (%d)\n", path, rc);
        return rc;
    }

    uint32_t entry = opt.ImageBase + opt.AddressOfEntryPoint;
    vga_printf("[PE] load %s: imagebase=0x%x entry=0x%x secs=%u\n",
               path, opt.ImageBase, entry, nsec);

    extern process_t *process_create(const char *, uint32_t,
                                     privilege_t, uint32_t);
    extern void process_enter_user_mode(process_t *);
    extern uint32_t mm_alloc_page(void);
    extern int paging_map(page_directory_t *, uint32_t, uint32_t, uint32_t);
    extern uint32_t paging_get_phys(page_directory_t *, uint32_t);

    char pname[32];
    {
        const char *base = path;
        for (const char *p = path; *p; p++) if (*p == '/') base = p + 1;
        uint32_t i = 0;
        while (base[i] && i < 31) { pname[i] = base[i]; i++; }
        pname[i] = 0;
    }

    process_t *proc = process_create(pname, entry, PRIV_USER, 100);
    if (!proc) return BINFMT_ERR_MEM;
    proc->compat_abi = 2;   /* Win32 ABI: 沙箱生效, syscall 不经 Linux 翻译 */
    proc->is_app = 1;       /* 可超时回收 */

    /* 绑定标准流 (/dev/console) */
    {
        extern vfs_node_t *vfs_find(const char *);
        extern int vfs_bind_stdio(vfs_node_t *);
        vfs_node_t *console = vfs_find("/dev/console");
        if (console) vfs_bind_stdio(console);
        else vga_puts("[PE] warn: /dev/console missing\n");
    }

    /* ---- 映射节: VA(页对齐) → ImageBase+VA, 复制 raw, 多余部分清零 ---- */
    uint32_t max_end = 0;
    for (uint32_t s = 0; s < nsec; s++) {
        uint32_t va = opt.ImageBase + secs[s].VirtualAddress;
        uint32_t vsz = secs[s].VirtualSize;
        uint32_t rawsz = secs[s].SizeOfRawData;
        if (vsz < rawsz) vsz = rawsz;
        if (!vsz) continue;

        uint32_t pages = (vsz + PAGE_SIZE - 1) / PAGE_SIZE;
        uint32_t copy_total = rawsz;
        uint32_t file_off = secs[s].PointerToRawData;
        for (uint32_t pg = 0; pg < pages; pg++) {
            uint32_t vaddr = va + pg * PAGE_SIZE;
            uint32_t frame = mm_alloc_page();
            if (!frame) {
                vga_puts("[PE] error: frame alloc fail\n");
                return BINFMT_ERR_MEM;
            }
            if (paging_map(proc->page_dir, vaddr, frame,
                           PAGE_PRESENT | PAGE_WRITE | PAGE_USER) < 0) {
                vga_puts("[PE] error: map fail\n");
                return BINFMT_ERR_MEM;
            }
            uint32_t phys = paging_get_phys(proc->page_dir, vaddr);
            /* 从文件复制本页数据 */
            uint32_t copy = 0;
            if (copy_total > 0) {
                copy = PAGE_SIZE;
                if (copy > copy_total) copy = copy_total;
                if (file_off + copy <= size) {
                    memcpy((void *)(uintptr_t)phys, data + file_off, copy);
                } else {
                    copy = 0;
                }
                file_off += copy;
                copy_total -= copy;
            }
            if (copy < PAGE_SIZE) {
                memset((void *)(uintptr_t)(phys + copy), 0, PAGE_SIZE - copy);
            }
        }
        if (va + vsz > max_end) max_end = va + vsz;
        vga_printf("[PE]   sec%d: va=0x%x raw=%u vsz=%u\n",
                   s, va, rawsz, vsz);
    }
    vga_printf("[PE]   image end=0x%x\n", max_end);

    /* ---- 映射用户态 stub 页 (ImageBase+SizeOfImage 页对齐) ---- */
    uint32_t stub_va = opt.ImageBase + opt.SizeOfImage;
    stub_va = (stub_va + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    for (uint32_t pg = 0; pg < PE_STUB_PAGES; pg++) {
        uint32_t vaddr = stub_va + pg * PAGE_SIZE;
        uint32_t frame = mm_alloc_page();
        if (!frame) return BINFMT_ERR_MEM;
        if (paging_map(proc->page_dir, vaddr, frame,
                       PAGE_PRESENT | PAGE_WRITE | PAGE_USER) < 0) {
            return BINFMT_ERR_MEM;
        }
        uint32_t phys = paging_get_phys(proc->page_dir, vaddr);
        memset((void *)(uintptr_t)phys, 0, PAGE_SIZE);
        if (pg == 0) {
            memcpy((void *)(uintptr_t)(phys + PE_STUB_WRITE),
                   stub_write, sizeof(stub_write));
            memcpy((void *)(uintptr_t)(phys + PE_STUB_READ),
                   stub_read, sizeof(stub_read));
            memcpy((void *)(uintptr_t)(phys + PE_STUB_EXIT),
                   stub_exit, sizeof(stub_exit));
            memcpy((void *)(uintptr_t)(phys + PE_STUB_STDHANDLE),
                   stub_stdhandle, sizeof(stub_stdhandle));
            memcpy((void *)(uintptr_t)(phys + PE_STUB_UNSUP),
                   stub_unsup, sizeof(stub_unsup));
            memcpy((void *)(uintptr_t)(phys + PE_STUB_CMDLINE),
                   stub_cmdline, sizeof(stub_cmdline));
            memcpy((void *)(uintptr_t)(phys + PE_STUB_TICK),
                   stub_tick, sizeof(stub_tick));
            memcpy((void *)(uintptr_t)(phys + PE_STUB_SLEEP),
                   stub_sleep, sizeof(stub_sleep));
            memcpy((void *)(uintptr_t)(phys + PE_STUB_MODNAME),
                   stub_modname, sizeof(stub_modname));

            /* ---- B-3: 注入命令行/argv/模块名数据 (stub 页数据区) ---- */
            /* V3.87 B-3: 真实命令行——shell `run winargs.exe a b c` 的完整命令串
             * 由 binfmt_set_cmdline 传入; 加载器跳过首 token(程序名) 后分词 argv[1..],
             * argv[0]=pname(path 提取). argc 上限 4 (argv 槽区 0x160-0x1DF 固定) */
            extern const char *binfmt_get_cmdline(void);
            const char *real_cmd = binfmt_get_cmdline();
            char cmdline[96];
            uint32_t argc = 0;
            const char *argv_src[4];
            {
                argv_src[0] = pname;      /* 程序名 */
                /* 从 real_cmd 跳过首个 token (程序名) 后取剩余参数 */
                uint32_t extra = 0;
                if (real_cmd && *real_cmd) {
                    const char *s = real_cmd;
                    while (*s && *s != ' ' && *s != '\t') s++;  /* 跳过 prog */
                    while (*s == ' ' || *s == '\t') s++;
                    while (*s && extra < 3) {                    /* 最多 3 个额外参数 */
                        const char *tok = s;
                        while (*s && *s != ' ' && *s != '\t') s++;
                        argv_src[extra + 1] = tok;
                        extra++;
                        while (*s == ' ' || *s == '\t') s++;
                    }
                }
                argc = 1 + extra;
                uint32_t n = 0;
                /* 命令整串: 优先用 shell 原样传入的 real_cmd (含路径/原名),
                 * 无则按 argv 重建 (兜底) */
                if (real_cmd && *real_cmd) {
                    const char *rc = real_cmd;
                    while (*rc && n < 95) { cmdline[n++] = *rc; rc++; }
                } else {
                    for (uint32_t a = 0; a < argc; a++) {
                        const char *s = argv_src[a];
                        uint32_t l = 0;
                        while (s[l] && l < 30) l++;
                        if (n) cmdline[n++] = ' ';
                        for (uint32_t k = 0; k <= l; k++) cmdline[n++] = s[k];
                    }
                }
                cmdline[n] = '\0';
            }
            /* GetCommandLineA → 命令整串地址 */
            *(uint32_t *)(uintptr_t)(phys + PE_STUB_CMDLINE + 1) =
                stub_va + PE_CMDLINE_STR;
            /* 写命令整串 */
            for (uint32_t i = 0; i < 96; i++) {
                char c = cmdline[i];
                *(char *)(uintptr_t)(phys + PE_CMDLINE_STR + i) = c;
                if (!c) break;
            }
            /* 写 argv 字符串 (按 token 边界截断: 空格/tab/串尾, 支持 real_cmd 内部指针) */
            for (uint32_t a = 0; a < argc; a++) {
                uint32_t base = PE_ARGV_STR0 + a * 0x20;
                const char *s = argv_src[a];
                uint32_t l = 0;
                while (s[l] && s[l] != ' ' && s[l] != '\t' && l < 31) {
                    *(char *)(uintptr_t)(phys + base + l) = s[l];
                    l++;
                }
                *(char *)(uintptr_t)(phys + base + l) = '\0';
            }
            /* 写 argv 表: [0]=argc [1..argc]=argv ptr [argc+1]=NULL */
            *(uint32_t *)(uintptr_t)(phys + PE_ARGV_TBL) = argc;
            for (uint32_t a = 0; a < argc; a++) {
                *(uint32_t *)(uintptr_t)(phys + PE_ARGV_TBL + 4 + a * 4) =
                    stub_va + PE_ARGV_STR0 + a * 0x20;
            }
            *(uint32_t *)(uintptr_t)(phys + PE_ARGV_TBL + 4 + argc * 4) = 0;

            /* 模块完整路径 + GetModuleFileNameA stub patch */
            {
                uint32_t n = 0;
                while (path[n] && n < 63) {
                    *(char *)(uintptr_t)(phys + PE_MODULE_STR + n) = path[n];
                    n++;
                }
                *(char *)(uintptr_t)(phys + PE_MODULE_STR + n) = '\0';
                /* stub_modname 数组布局: [1..4]=esi imm [10..13]=ecx imm
                 * [17..20]=eax imm (见数组定义), patch 偏移勿错 */
                *(uint32_t *)(uintptr_t)(phys + PE_STUB_MODNAME + 1) =
                    stub_va + PE_MODULE_STR;          /* esi = 路径 */
                *(uint32_t *)(uintptr_t)(phys + PE_STUB_MODNAME + 10) =
                    n + 1;                            /* ecx = len+1 */
                *(uint32_t *)(uintptr_t)(phys + PE_STUB_MODNAME + 17) =
                    n;                                /* eax = len */
            }
            vga_printf("[PE]   cmdline: %s (argc=%u)\n", cmdline, argc);
            vga_printf("[PE]   module:  %s\n", path);
        }
    }
    vga_printf("[PE]   stub page @ 0x%x\n", stub_va);

    /* ---- 解析导入表 → 改写 IAT 槽为 stub 地址 ---- */
    if (opt.NumberOfRvaAndSizes > 1 && opt.DataDirectory[1].VirtualAddress) {
        uint32_t imp_rva = opt.DataDirectory[1].VirtualAddress;
        uint32_t imp_va  = opt.ImageBase + imp_rva;
        vga_printf("[PE]   import dir: rva=0x%x size=%u\n",
                   imp_rva, opt.DataDirectory[1].Size);
        for (uint32_t i = 0; i < PE_MAX_IMPORTS; i++) {
            uint32_t desc_va = imp_va + i * sizeof(pe_import_desc_t);
            pe_import_desc_t desc;
            uint32_t d0, d1, d2, d3, d4;
            if (pe_u32_read(proc, desc_va + 0, &d0) < 0) break;
            if (pe_u32_read(proc, desc_va + 4, &d1) < 0) break;
            if (pe_u32_read(proc, desc_va + 8, &d2) < 0) break;
            if (pe_u32_read(proc, desc_va + 12, &d3) < 0) break;
            if (pe_u32_read(proc, desc_va + 16, &d4) < 0) break;
            desc.OriginalFirstThunk = d0;
            desc.TimeDateStamp = d1;
            desc.ForwarderChain = d2;
            desc.Name = d3;
            desc.FirstThunk = d4;
            if (!desc.FirstThunk && !desc.OriginalFirstThunk) break;

            char dll[64];
            if (desc.Name &&
                pe_name_read(proc, opt.ImageBase + desc.Name, dll,
                             sizeof(dll)) > 0) {
                vga_printf("[PE]     import dll: %s\n", dll);
            }

            uint32_t thunk_rva = desc.OriginalFirstThunk ?
                                 desc.OriginalFirstThunk : desc.FirstThunk;
            for (uint32_t j = 0; j < PE_MAX_IMPORTS; j++) {
                uint32_t thunk = 0;
                if (pe_u32_read(proc, opt.ImageBase + thunk_rva + j * 4,
                                &thunk) < 0) break;
                if (!thunk) break;
                if (thunk & 0x80000000u) {
                    /* ordinal import: 不支持, 填 unsupported stub */
                    pe_u32_write(proc, opt.ImageBase + desc.FirstThunk + j * 4,
                                 stub_va + PE_STUB_UNSUP);
                    continue;
                }
                char fname[64];
                /* thunk 指向 IMAGE_IMPORT_BY_NAME: WORD Hint + ASCII 名
                 * (名从 hint+2 开始; 直接读表头会读到 hint 的 0x00) */
                if (pe_name_read(proc,
                                 opt.ImageBase + (thunk & 0x7FFFFFFFu) + 2,
                                 fname, sizeof(fname)) <= 0) {
                    break;
                }
                uint32_t stub_off = pe_match_api(fname);
                if (stub_off == PE_STUB_UNSUP) {
                    vga_printf("[PE]     unsupported api: %s\n", fname);
                }
                vga_printf("[PE]     bind %s -> 0x%x\n",
                           fname, stub_va + stub_off);
                pe_u32_write(proc, opt.ImageBase + desc.FirstThunk + j * 4,
                             stub_va + stub_off);
            }
        }
    }

    /* 用户堆: ImageBase+SizeOfImage 页对齐处 */
    proc->heap_start = stub_va + PE_STUB_PAGES * PAGE_SIZE;
    proc->heap_end = proc->heap_start;

    /* 进入用户态 (中断帧 user_esp 即 process_create 的 0x40010000,
     * PE 程序不依赖 argc/envp 启动栈, 无需同步) */
    vga_printf("[PE] %s: loaded (PID=%d, entry=0x%x, sandbox)\n",
               path, proc->pid, entry);
    return pe_enter_user(proc);
}

/* 独立浅栈进入用户态 + setjmp 挂起点 */
