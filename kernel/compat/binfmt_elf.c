/* ============================================================================
 * 赫普OS - Linux ELF32 静态可执行程序加载器
 * V3.87 阶段A: 解析 ET_EXEC 静态程序 → 段映射计划 → 加载到用户空间
 *
 * 支持范围:
 *   - ELF32 小端 / EM_386 / ET_EXEC (固定地址静态程序, 0x08048000 基址)
 *   - PT_LOAD 多段 (代码/数据/bss)
 *   - 静态链接 (无 PT_INTERP 动态链接器; 动态程序返回 BINFMT_ERR_UNSUP)
 *
 * 布局 (Linux i386 标准):
 *   TEXT/DATA 基址 0x08048000
 *   bss 随数据段 (p_memsz > p_filesz 部分清零)
 *   栈: 用户栈顶向下构造 argv/envp/auxv
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "binfmt.h"
#include "elf32.h"
#include "binfmt_elf_dynamic.h"
#include "constants.h"
#include "paging.h"
#include "process.h"
#include "vfs.h"
#include "vga.h"

/* V3.87: ELF 主线保存点 (noinline 包装, 定义见文件尾) */
/* V3.87-E10: 下一 ELF 进程的 CPU 限额(tick@100Hz, 0=不限)——shell `setappcpu` 设置 */
uint32_t binfmt_next_cpu_limit = 0;

static int elf_mainline_save(void) __attribute__((noinline));
/* V3.87: 独立浅栈进入用户态 + setjmp 挂起点 (noinline, 定义见文件尾) */
static int elf_enter_user(process_t *proc) __attribute__((noinline));

/* ELF 固定加载基址 (Linux i386 默认) */
#define LINUX_ELF_BASE    0x08048000u

/* 可加载最大段数 */
#define MAX_PHDR_LOAD     16

/* ---- 内部: 从数据区安全读取 (防越界) ---- */
static int elf_parse(const uint8_t *data, uint32_t size,
                     elf32_ehdr_t *eh_out, elf32_phdr_t *ph_out,
                     uint32_t *phnum_out)
{
    if (size < sizeof(elf32_ehdr_t)) return BINFMT_ERR_FORMAT;
    const elf32_ehdr_t *eh = (const elf32_ehdr_t *)data;

    if (eh->e_magic != ELF_MAGIC)   return BINFMT_ERR_FORMAT;
    if (eh->e_class != 1)           return BINFMT_ERR_FORMAT;  /* ELFCLASS32 */
    if (eh->e_data != 1)            return BINFMT_ERR_FORMAT;  /* LSB */
    if (eh->e_machine != EM_386)    return BINFMT_ERR_FORMAT;
    /* V8.52: 支持ET_EXEC(静态)和ET_DYN(动态/PIE) */
    if (eh->e_type != ET_EXEC && eh->e_type != ET_DYN)
        return BINFMT_ERR_UNSUP;

    /* 程序头表 */
    uint32_t phnum = eh->e_phnum;
    uint32_t entsz = eh->e_phentsize;
    if (entsz < sizeof(elf32_phdr_t)) return BINFMT_ERR_FORMAT;
    if (phnum > MAX_PHDR_LOAD)      return BINFMT_ERR_FORMAT;
    uint32_t phoff = eh->e_phoff;
    if (phoff > size || phoff + (uint64_t)phnum * entsz > size) {
        return BINFMT_ERR_FORMAT;
    }

    *eh_out = *eh;
    for (uint32_t i = 0; i < phnum; i++) {
        const elf32_phdr_t *p =
            (const elf32_phdr_t *)(data + phoff + (uint64_t)i * entsz);
        ph_out[i] = *p;
    }
    *phnum_out = phnum;
    return BINFMT_OK;
}

/* 检查是否存在动态链接器 (PT_INTERP) 或动态段 */
static int elf_is_static(const elf32_phdr_t *ph, uint32_t phnum)
{
    for (uint32_t i = 0; i < phnum; i++) {
        if (ph[i].p_type == PT_INTERP)  return 0;  /* 动态 */
        if (ph[i].p_type == PT_DYNAMIC) return 0;
    }
    return 1;
}

/* 计算段映射计划: 页对齐后输出到 segs.
 * 返回段数 (0=无PT_LOAD, 负数=错误) */
static int elf_build_segs(const elf32_phdr_t *ph, uint32_t phnum,
                          elf_load_seg_t *segs)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < phnum && n < ELF_MAX_LOAD_SEGS; i++) {
        if (ph[i].p_type != PT_LOAD) continue;
        uint32_t vaddr = ph[i].p_vaddr;
        uint32_t off   = ph[i].p_offset;
        uint32_t filesz = ph[i].p_filesz;
        uint32_t memsz  = ph[i].p_memsz;

        /* V7.67安全修复: 校验段地址范围，防止映射到内核空间
         * 用户空间: 0x00000000-0xBFFFFFFF
         * 内核空间: 0xC0000000-0xFFFFFFFF (禁止映射) */
        if (vaddr >= 0xC0000000) {
            vga_printf("[ELF] seg %d vaddr 0x%x in kernel space, rejected\n", i, vaddr);
            return BINFMT_ERR_FORMAT;
        }
        if (vaddr + memsz < vaddr || vaddr + memsz > 0xC0000000) {
            vga_printf("[ELF] seg %d overflow or into kernel space\n", i);
            return BINFMT_ERR_FORMAT;
        }

        /* 页对齐: 文件偏移对齐到 0x1000, 虚拟地址同步下取整 */
        uint32_t page_off = vaddr & 0xFFF;
        uint32_t align_off = off & 0xFFF;   /* 通常==page_off */
        uint32_t base = vaddr - page_off;   /* 页对齐起始虚拟地址 */
        uint32_t fbase = off - align_off;   /* 页对齐文件偏移 */
        uint32_t extra = page_off - align_off; /* 文件到虚拟页偏移差 */

        /* 映射大小: 从页对齐起点到 memsz 末端 */
        uint32_t end = vaddr + memsz;
        uint32_t map_end = (end + 0xFFF) & ~0xFFFu;
        uint32_t map_start = base;
        if (map_end < map_start) return BINFMT_ERR_FORMAT;
        uint32_t map_sz = map_end - map_start;

        /* 需从文件复制的字节: 从 fbase 起到 filesz 末端 (页内) */
        uint32_t copy_off = fbase + extra;      /* 段数据在文件中的起点 */
        uint32_t copy_end = copy_off + filesz;
        if (copy_end > map_end - map_start + fbase) {
            copy_end = map_start - fbase + map_sz; /* 截断到映射末端 */
        }
        uint32_t copy_sz = (copy_end > copy_off) ? (copy_end - copy_off) : 0;

        segs[n].vaddr     = map_start;
        segs[n].offset    = fbase;
        segs[n].filesz_pg = copy_sz;
        segs[n].memsz_pg  = map_sz;
        segs[n].flags     = ph[i].p_flags;
        n++;
    }
    return (int)n;
}

/* 加载并执行 ELF32 静态程序.
 * 阶段A-1: 完成解析 + 布局计划输出 (实际映射/执行在 V3.87 阶段A-2 接入
 * .hug 同款用户空间映射与进程创建). 本函数先做完整校验与信息打印. */
int binfmt_elf_load(const char *path, const uint8_t *data, uint32_t size)
{
    elf32_ehdr_t eh;
    elf32_phdr_t ph[MAX_PHDR_LOAD];
    uint32_t phnum = 0;

    int rc = elf_parse(data, size, &eh, ph, &phnum);
    if (rc != BINFMT_OK) {
        vga_printf("[ELF] %s: parse fail (%d)\n", path, rc);
        return rc;
    }
    /* V0.9.0: 支持动态链接(有PT_INTERP)，调用动态ELF加载器 */
    int has_interp = 0;
    for (uint32_t i = 0; i < phnum; i++) {
        if (ph[i].p_type == PT_INTERP) { has_interp = 1; break; }
    }
    if (has_interp) {
        vga_printf("[ELF] %s: dynamic ELF, using dynamic loader\n", path);
        return binfmt_elf_dynamic_load(path, data, size);
    }
    if (eh.e_type == ET_DYN) {
        vga_printf("[ELF] %s: static PIE (ET_DYN), loading with base relocation\n", path);
    }

    elf_load_seg_t segs[ELF_MAX_LOAD_SEGS];
    int nseg = elf_build_segs(ph, phnum, segs);
    if (nseg <= 0) {
        vga_printf("[ELF] %s: no PT_LOAD segments\n", path);
        return BINFMT_ERR_FORMAT;
    }

    /* V8.52: PIE基地址重定位 (ET_DYN程序段地址从0开始，需映射到用户空间) */
    uint32_t pie_base = 0;
    if (eh.e_type == ET_DYN) {
        /* 计算程序总大小 */
        uint32_t prog_min = 0xFFFFFFFF, prog_max = 0;
        for (int i = 0; i < nseg; i++) {
            if (segs[i].vaddr < prog_min) prog_min = segs[i].vaddr;
            if (segs[i].vaddr + segs[i].memsz_pg > prog_max)
                prog_max = segs[i].vaddr + segs[i].memsz_pg;
        }
        uint32_t prog_size = prog_max - prog_min;
        /* 分配基地址：0x08048000对齐到页，确保不与内核空间冲突 */
        pie_base = 0x08048000;
        /* 重定位所有段地址 */
        for (int i = 0; i < nseg; i++) {
            segs[i].vaddr += pie_base;
        }
        /* 重定位入口地址 */
        eh.e_entry += pie_base;
        vga_printf("[ELF] PIE: base=0x%x size=%dKB entry=0x%x\n",
                   pie_base, prog_size / 1024, eh.e_entry);
    }

    vga_printf("[ELF] load %s: entry=0x%x type=%u phnum=%u segs=%d\n",
               path, eh.e_entry, eh.e_type, phnum, nseg);

    uint32_t min_vaddr = 0xFFFFFFFFu;
    uint32_t max_end   = 0;
    for (int i = 0; i < nseg; i++) {
        vga_printf("[ELF]   seg%d: vaddr=0x%x file=0x%x copy=%u map=%u flags=%u\n",
                   i, segs[i].vaddr, segs[i].offset,
                   segs[i].filesz_pg, segs[i].memsz_pg, segs[i].flags);
        if (segs[i].vaddr < min_vaddr) min_vaddr = segs[i].vaddr;
        if (segs[i].vaddr + segs[i].memsz_pg > max_end) {
            max_end = segs[i].vaddr + segs[i].memsz_pg;
        }
    }
    vga_printf("[ELF]   layout: base=0x%x end=0x%x size=%dKB\n",
               min_vaddr, max_end, (max_end - min_vaddr) / 1024);

    /* ---- V3.87 阶段A-2: 实际加载执行 (APK 式沙箱) ----
     * 1. 创建用户进程 (独立页目录 + compat_abi=1 + is_app=1)
     * 2. 映射各 PT_LOAD 段到 ELF 固定基址 (0x08048000 区)
     * 3. 复制文件数据 + bss 清零
     * 4. 构造 Linux 启动栈 (argc/argv/envp, i386 标准协议)
     * 5. 进入用户态执行; int 0x80 经 syscall_handler 按
     *    current->compat_abi 翻译为 HEPU syscall (沙箱白名单限制) */
    extern process_t *process_create(const char *, uint32_t,
                                     privilege_t, uint32_t);
    extern void process_enter_user_mode(process_t *);
    extern uint32_t mm_alloc_page(void);
    extern int paging_map(page_directory_t *, uint32_t, uint32_t, uint32_t);
    extern uint32_t paging_get_phys(page_directory_t *, uint32_t);

    /* 进程名: 用文件名去掉路径 (最长 31 字符) */
    char pname[32];
    {
        const char *base = path;
        for (const char *p = path; *p; p++) if (*p == '/') base = p + 1;
        uint32_t i = 0;
        while (base[i] && i < 31) { pname[i] = base[i]; i++; }
        pname[i] = 0;
    }

    /* V7.67安全修复: 校验入口地址范围，防止指向内核代码 */
    if (eh.e_entry >= 0xC0000000) {
        vga_printf("[ELF] entry 0x%x in kernel space, rejected\n", eh.e_entry);
        return BINFMT_ERR_FORMAT;
    }

    process_t *proc = process_create(pname, eh.e_entry, PRIV_USER, 100);
    if (!proc) return BINFMT_ERR_MEM;
    proc->compat_abi = 1;   /* Linux i386 ABI: syscall 号翻译 */
    proc->is_app = 1;       /* 可超时回收 */

    /* V3.87-E7/E10: 用户程序默认 16MB 内存限额 + 可配置 CPU 限额
     * (binfmt_next_cpu_limit 由 shell `setappcpu` 设置, 0=不限) */
    {
        extern void process_set_limits(process_t *, uint32_t, uint32_t);
        extern uint32_t binfmt_next_cpu_limit;
        process_set_limits(proc, APP_DEFAULT_MEM_LIMIT,
                           binfmt_next_cpu_limit);
    }

    /* V3.87: 兼容进程预开标准流——Linux 程序假定 0/1/2 已打开
     * (vfs fd_table 全空, write(1,..) 会因 fd 未打开返回 -1 丢输出).
     * 把 fd 0/1/2 直接指向 /dev/console (VGA 屏幕 + 键盘). */
    {
        extern vfs_node_t *vfs_find(const char *);
        extern vfs_node_t *vfs_get_root(void);
        extern void vfs_add_child(vfs_node_t *, vfs_node_t *);
        extern int vfs_bind_stdio(vfs_node_t *);
        vfs_node_t *console = vfs_find("/dev/console");
        if (!console) {
            /* V4.02-E47: 根文件系统挂载后/dev节点可能丢失(ext2挂载替换
             * root->children), 重新创建/dev/console以保证ELF应用stdout可用. */
            vfs_node_t *root = vfs_get_root();
            vfs_node_t *dev = vfs_find("/dev");
            if (!dev) {
                dev = vfs_create_node("dev", VFS_DIRECTORY);
                if (dev && root) vfs_add_child(root, dev);
            }
            if (dev) {
                console = vfs_create_node("console", VFS_CHAR_DEVICE);
                if (console) {
                    extern vfs_file_ops_t console_ops;
                    console->ops = &console_ops;
                    vfs_add_child(dev, console);
                }
            }
        }
        if (console) vfs_bind_stdio(console);
    }

    {
        extern void serial_puts(uint16_t, const char *);
        extern void serial_puthex(uint16_t, uint32_t);
        serial_puts(0x3F8, "[ELF-DBG] proc created pid=");
        serial_puthex(0x3F8, proc->pid);
        serial_puts(0x3F8, " pdir=");
        serial_puthex(0x3F8, (uint32_t)(uintptr_t)proc->page_dir);
        serial_puts(0x3F8, " entry=");
        serial_puthex(0x3F8, eh.e_entry);
        serial_puts(0x3F8, " usrstack=");
        serial_puthex(0x3F8, proc->user_stack);
        serial_puts(0x3F8, " kstack=");
        serial_puthex(0x3F8, proc->kernel_stack);
        serial_puts(0x3F8, " kesp=");
        serial_puthex(0x3F8, proc->kernel_esp);
        serial_puts(0x3F8, "\n");
    }

    /* 映射 PT_LOAD 段: 页粒度 (对齐已在 elf_build_segs 完成) */
    for (int s = 0; s < nseg; s++) {
        uint32_t pages = segs[s].memsz_pg / PAGE_SIZE;
        if (segs[s].memsz_pg % PAGE_SIZE) pages++;
        for (uint32_t pg = 0; pg < pages; pg++) {
            uint32_t vaddr = segs[s].vaddr + pg * PAGE_SIZE;
            uint32_t frame = mm_alloc_page();
            if (!frame) {
                vga_puts("[ELF] error: frame alloc fail\n");
                return BINFMT_ERR_MEM;
            }
            if (paging_map(proc->page_dir, vaddr, frame,
                           PAGE_PRESENT | PAGE_WRITE | PAGE_USER) < 0) {
                vga_puts("[ELF] error: map fail\n");
                return BINFMT_ERR_MEM;
            }
            uint32_t phys = paging_get_phys(proc->page_dir, vaddr);
            {
                extern void serial_puts(uint16_t, const char *);
                extern void serial_puthex(uint16_t, uint32_t);
                serial_puts(0x3F8, "[ELF-DBG] s=");
                serial_puthex(0x3F8, s);
                serial_puts(0x3F8, " pg=");
                serial_puthex(0x3F8, pg);
                serial_puts(0x3F8, " vaddr=");
                serial_puthex(0x3F8, vaddr);
                serial_puts(0x3F8, " frame=");
                serial_puthex(0x3F8, frame);
                serial_puts(0x3F8, " phys=");
                serial_puthex(0x3F8, phys);
                serial_puts(0x3F8, "\n");
            }
            /* 文件数据起点 = seg 文件偏移 + 页内偏移 */
            uint32_t seg_off = segs[s].offset;
            uint32_t copy = 0;
            if (segs[s].filesz_pg > 0) {
                /* 本页内需复制的字节数 (从文件 offset 起) */
                if (pg * PAGE_SIZE < segs[s].filesz_pg) {
                    copy = PAGE_SIZE;
                    uint32_t remain = segs[s].filesz_pg - pg * PAGE_SIZE;
                    if (copy > remain) copy = remain;
                    /* 文件内偏移: seg_off + 页内已复制的部分 */
                    const uint8_t *src = data + seg_off + pg * PAGE_SIZE;
                    if ((uint32_t)(src - data) + copy <= size) {
                        memcpy((void *)phys, src, copy);
                    } else {
                        copy = 0;   /* 越界保护: 不复制 */
                    }
                }
            }
            /* bss 清零: 本页中未被文件覆盖的部分 (copy 之后) */
            if (copy < PAGE_SIZE) {
                memset((void *)(phys + copy), 0, PAGE_SIZE - copy);
            }
        }
    }

    /* V3.87: 初始化用户堆——heap_start = 段结束页对齐处,
     * 后续 sys_brk 从该地址向上扩展 (标准库 malloc 的基础). */
    {
        uint32_t heap_start = (max_end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
        proc->heap_start = heap_start;
        proc->heap_end = heap_start;   /* 初始堆空 */
    }

    /* 构造 Linux 启动栈 (i386 完整标准 ABI):
     *   esp -> argc
     *          argv[0..argc-1] 指针, NULL
     *          envp[0..n-1] 指针, NULL
     *          auxv[0..m-1] (type,value 对), 0 结束
     *          ... 字符串/随机数 (高地址)
     * 注意: 此刻 CR3 仍是内核页目录, proc->user_stack 是用户虚拟地址,
     * 不能直接写——必须经 paging_get_phys 换算到物理帧再写
     * (直接写虚拟地址 → #PF 崩溃, 实测重启循环).
     * 从栈顶向下构造: 先字符串区, 再 auxv 对, 再 envp/argv 指针, 最后 argc. */
    {
        extern void serial_puts(uint16_t, const char *);
        extern void serial_puthex(uint16_t, uint32_t);
        uint32_t sp_v = proc->user_stack;
        serial_puts(0x3F8, "[ELF-DBG] stack sp_v=");
        serial_puthex(0x3F8, sp_v);
        serial_puts(0x3F8, "\n");
        /* 用户栈映射区 = [USER_STACK_ADDR, USER_STACK_ADDR+USER_STACK_SIZE).
         * proc->user_stack 是栈顶(终点, 0x40010000)不在此区间 → 取映射内
         * 最后一页 (0x4000F000) 定位物理帧. */
        uint32_t stack_page = USER_STACK_ADDR + USER_STACK_SIZE - PAGE_SIZE;
        uint32_t stack_phys = paging_get_phys(proc->page_dir, stack_page);
        serial_puts(0x3F8, "[ELF-DBG] stack page=");
        serial_puthex(0x3F8, stack_page);
        serial_puts(0x3F8, " phys=");
        serial_puthex(0x3F8, stack_phys);
        serial_puts(0x3F8, "\n");
        if (!stack_phys) {
            vga_puts("[ELF] error: user stack not mapped\n");
            return BINFMT_ERR_MEM;
        }

        /* 栈帧 = [stack_phys, stack_phys+PAGE_SIZE).
         * ★关键: 用户栈物理帧由 kmalloc 分配, 帧外(下一帧)可能是别的
         * 进程栈/PCB——绝不可写 frame_top 及以上! 所有启动数据必须
         * 落在帧内. (实测: 从 frame_top 开始写 → 踩踏 kheap 相邻内存
         * → sys_brk 返回被污染值 0x0A051000 / linuxcrt 静默超时.) */
        uint32_t frame_top = stack_phys + PAGE_SIZE;

        /* ---- 1. 字符串区 (帧顶向下, 预留 256B 足够) ---- */
        static const char *envs[2] = { "HEPU=1", "TERM=hepu" };
        char *pstr = (char *)frame_top - 256;

        char *arg0 = pstr;
        uint32_t i = 0;
        while (pname[i] && i < 40) { arg0[i] = pname[i]; i++; }
        arg0[i] = 0;
        uint32_t arg0_virt = stack_page + ((uint32_t)arg0 - stack_phys);
        pstr += i + 1;

        char *env0 = pstr;
        i = 0;
        while (envs[0][i]) { env0[i] = envs[0][i]; i++; }
        env0[i] = 0;
        uint32_t env0_virt = stack_page + ((uint32_t)env0 - stack_phys);
        pstr += i + 1;

        char *env1 = pstr;
        i = 0;
        while (envs[1][i]) { env1[i] = envs[1][i]; i++; }
        env1[i] = 0;
        uint32_t env1_virt = stack_page + ((uint32_t)env1 - stack_phys);
        pstr += i + 1;

        /* 16 字节随机数 (AT_RANDOM, stack canary 种子) */
        uint32_t *rnd = (uint32_t *)(((uint32_t)pstr + 15) & ~15u);
        uint32_t seed = (uint32_t)frame_top ^ (uint32_t)proc->page_dir;
        for (i = 0; i < 4; i++) rnd[i] = seed ^ (i * 0x9E3779B9u);
        uint32_t rnd_virt = stack_page + ((uint32_t)rnd - stack_phys);
        pstr = (char *)rnd + 16;

        /* ---- 2. auxv 对 (从最后一对向下写) ----
         * 公开 ABI (linux/auxvec.h): 3=AT_PHDR 4=AT_PHENT 5=AT_PHNUM
         * 6=AT_PAGESZ 7=AT_BASE 8=AT_FLAGS 9=AT_ENTRY 11=AT_UID 12=AT_EUID
         * 13=AT_GID 14=AT_EGID 16=AT_HWCAP 17=AT_CLKTCK 25=AT_RANDOM
         * 31=AT_EXECFN 0=AT_NULL */
        uint32_t auxv[][2] = {
            { 31, arg0_virt },      /* AT_EXECFN */
            { 25, rnd_virt },       /* AT_RANDOM */
            { 17, 100 },            /* AT_CLKTCK */
            { 16, 0 },              /* AT_HWCAP */
            { 14, 0 },              /* AT_EGID */
            { 13, 0 },              /* AT_GID */
            { 12, 0 },              /* AT_EUID */
            { 11, 0 },              /* AT_UID */
            { 9, eh.e_entry },      /* AT_ENTRY */
            { 8, 0 },               /* AT_FLAGS */
            { 7, 0 },               /* AT_BASE (静态程序无解释器) */
            { 6, PAGE_SIZE },       /* AT_PAGESZ */
            { 5, eh.e_phnum },      /* AT_PHNUM */
            { 4, sizeof(elf32_phdr_t) }, /* AT_PHENT */
            { 3, (uint32_t)(LINUX_ELF_BASE + eh.e_phoff) }, /* AT_PHDR */
            { 0, 0 },               /* AT_NULL */
        };
        int aux_cnt = (int)(sizeof(auxv) / sizeof(auxv[0]));
        uint32_t *sp = (uint32_t *)(((uint32_t)pstr + 3) & ~3u);
        for (i = 0; i < (uint32_t)aux_cnt; i++) {
            sp--;
            *sp = auxv[aux_cnt - 1 - i][1];   /* value */
            sp--;
            *sp = auxv[aux_cnt - 1 - i][0];   /* type */
        }

        /* ---- 3. envp 指针数组 + NULL ---- */
        sp--;  *sp = 0;             /* envp[2] = NULL (终止) */
        sp--;  *sp = env1_virt;     /* envp[1] */
        sp--;  *sp = env0_virt;     /* envp[0] */

        /* ---- 4. argv 指针数组 + NULL ---- */
        sp--;  *sp = 0;             /* argv[1] = NULL */
        sp--;  *sp = arg0_virt;     /* argv[0] */

        /* ---- 5. argc ---- */
        sp--;  *sp = 1;

        proc->user_stack = stack_page + ((uint32_t)sp - stack_phys);

        /* V3.87 修复: process_create 构造中断帧时 user_esp 写的是旧栈顶
         * (0x40010000, 恰在用户栈映射区外 1 字节). 启动栈布局完成后必须
         * 同步帧内 user_esp, 否则 iret 后用户态 esp 越界: 依赖 esp 的
         * 程序(_start 把 esp 当 argv 指针传给 C 入口) 读 [0x40010000]
         * → 用户态 #PF → 卡死被调度器超时回收 (linuxcrt 静默超时根因).
         * 帧布局: ds@0 pusha@1-8 irq@9 err@10 eip@11 cs@12 eflags@13
         *          user_esp@14 ss@15 */
        {
            uint32_t *fr = (uint32_t *)proc->kernel_esp;
            fr[14] = proc->user_stack;
        }
        serial_puts(0x3F8, "[ELF-DBG] stack init done (argc/envp/auxv), esp_v=");
        serial_puthex(0x3F8, proc->user_stack);
        serial_puts(0x3F8, "\n");
    }

    vga_printf("[ELF] %s: loaded (PID=%d, entry=0x%x, sandbox)\n",
               path, proc->pid, eh.e_entry);
    {
        extern void serial_puts(uint16_t, const char *);
        extern void serial_puthex(uint16_t, uint32_t);
        serial_puts(0x3F8, "[ELF-DBG] mapped segs done, stack=");
        serial_puthex(0x3F8, proc->user_stack);
        serial_puts(0x3F8, " entering usermode\n");
    }

    /* V3.87: 进入用户态由独立浅栈函数执行——setjmp/longjmp 恢复的
     * esp 对应本函数调用点, 若在此栈帧继续使用大量局部变量(longjmp
     * 恢复 esp 后栈被截断) → 返回路径读栈垃圾 → 返回值被污染
     * (实测 rc=0x4000FFF0). 拆出后本函数只做: setjmp → 进用户态,
     * 返回路径干净. */
    return elf_enter_user(proc);
}

/* V3.87: 独立浅栈进入用户态 + setjmp 挂起点 (noinline) */
static int elf_enter_user(process_t *proc) __attribute__((noinline));
static int elf_enter_user(process_t *proc)
{
    int rc = elf_mainline_save();
    if (rc != 0) {
        return BINFMT_OK;   /* ELF 进程已退出, 回主线 */
    }
    process_enter_user_mode(proc);
    return BINFMT_OK;
}

/* V3.87: ELF 主线保存点——复用全局 mainline_jmp_buf
 * (sys_exit 退出时 longjmp(mainline_jmp_buf) 回最近 setjmp 者:
 *  ELF 进程 → 本处; .hug 进程 → hug_run). 必须 noinline 包装. */
static int elf_mainline_save(void) __attribute__((noinline));
static int elf_mainline_save(void)
{
    extern uint32_t mainline_jmp_buf[6];
    extern int hepu_setjmp(uint32_t *buf);
    return hepu_setjmp(mainline_jmp_buf);
}
