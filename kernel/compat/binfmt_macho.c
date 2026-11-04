/* ============================================================================
 * 赫普OS - Apple Mach-O 加载器 (阶段 C 原型, 32 位 x86)
 * 识别 MH_MAGIC=0xFEEDFACE 的 MH_EXECUTE 文件:
 *   1. 解析 Mach-O 头 + load commands (LC_SEGMENT __TEXT / LC_UNIXTHREAD)
 *   2. 段映射到 vmaddr (页对齐) → 注入用户态 stub 页 (write/exit 桥接)
 *   3. 按固定 patch 偏移回填 demo 的 call rel32 / imm32 (stub 地址)
 *   4. process_create + setjmp 主线保存 → 进入用户态
 * 沙箱: compat_abi=3 (Mach-O ABI), is_app=1 (超时回收), 独立页目录.
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include "binfmt.h"
#include "vga.h"
#include "string.h"
#include "process.h"
#include "paging.h"
#include "vfs.h"

#define MH_MAGIC        0xFEEDFACEu
#define MH_EXECUTE      2u
#define LC_SEGMENT      0x1u
#define LC_UNIXTHREAD   0x5u
#define X86_THREAD_STATE 1u

#define MACH_STUB_PAGES 1u

/* stub 页数据区 (相对 stub_va) */
#define M_CMDLINE_STR   0x100u    /* 命令整串 (≤64B) */
#define M_ARGV0_STR     0x160u    /* argv[0] 程序名 (≤32B) */
#define M_ARGV1_STR     0x180u    /* argv[1] (≤32B) */
#define M_ARGV2_STR     0x1A0u    /* argv[2] (≤32B) */
#define M_ARGV3_STR     0x1C0u    /* argv[3] (≤32B) */
#define M_ARGV_TBL      0x1E0u    /* [0]=argc [1..n]=argv ptr [n+1]=NULL */

/* stub: write(addr,len) → DARWIN_SYS_write (0x2000004) fd=1, ret 8.
 * 经 darwin_abi_translate → SYS_WRITE, 验证真实 Mach trap 号翻译链. */
static const uint8_t m_stub_write[] = {
    0xB8,0x04,0x00,0x00,0x20,          /* mov eax,0x20000004 (DARWIN write) */
    0xBB,0x01,0x00,0x00,0x00,          /* mov ebx,1  (fd=stdout) */
    0x8B,0x4C,0x24,0x04,               /* mov ecx,[esp+4]  (addr) */
    0x8B,0x54,0x24,0x08,               /* mov edx,[esp+8]  (len) */
    0xCD,0x80,                         /* int 0x80 */
    0xC2,0x08,0x00,                    /* ret 8 */
};
/* stub: exit(code) → DARWIN_SYS_exit (0x2000001) */
static const uint8_t m_stub_exit[] = {
    0xB8,0x01,0x00,0x00,0x20,          /* mov eax,0x20000001 (DARWIN exit) */
    0x8B,0x5C,0x24,0x04,               /* mov ebx,[esp+4] (code) */
    0xCD,0x80,
    0xCC,                              /* int3 (fallback) */
};

/* demo patch 偏移 (gen_macho.py 固定布局: push imm32 的 imm 起始 = opcode+1)
 * V3.87 C-2: 新增 argv0/argv1 打印 (4 个打印块), 偏移全部重算 */
#define P_MSG_ADDR   0x06u   /* imm32: msg 地址   (push @0x05) */
#define P_CALL1      0x0Bu   /* rel32: call stub_write #1 */
#define P_LBL2_ADDR  0x18u   /* imm32: label2 地址 (push @0x17) */
#define P_CALL2      0x1Du   /* rel32: call stub_write #2 */
#define P_CMD_ADDR   0x2Au   /* imm32: cmdline 地址(push @0x29) */
#define P_CALL3      0x2Fu   /* rel32: call stub_write #3 */
#define P_NL_ADDR    0x3Cu   /* imm32: nl 地址     (push @0x3B) */
#define P_CALL4      0x41u   /* rel32: call stub_write #4 */
#define P_AV0_ADDR   0x4Eu   /* imm32: argv0 地址  (push @0x4D) */
#define P_CALL5      0x53u   /* rel32: call stub_write #5 */
#define P_NL2_ADDR   0x60u   /* imm32: nl 地址     (push @0x5F) */
#define P_CALL6      0x65u   /* rel32: call stub_write #6 */
#define P_AV1_ADDR   0x72u   /* imm32: argv1 地址  (push @0x71) */
#define P_CALL7      0x77u   /* rel32: call stub_write #7 */
#define P_NL3_ADDR   0x84u   /* imm32: nl 地址     (push @0x83) */
#define P_CALL8      0x89u   /* rel32: call stub_write #8 */
#define P_CALL_EXIT  0x93u   /* rel32: call stub_exit (call @0x92) */
/* call 指令后一条指令地址 (rel = target - next) */
#define N_CALL1      0x100Fu
#define N_CALL2      0x1021u
#define N_CALL3      0x1033u
#define N_CALL4      0x1045u
#define N_CALL5      0x1057u
#define N_CALL6      0x1069u
#define N_CALL7      0x107Bu
#define N_CALL8      0x108Du
#define N_EXIT       0x1097u
/* demo 数据区 (段内绝对地址) */
#define M_MSG_ADDR   0x1099u
#define M_LBL2_ADDR  0x10B1u
#define M_NL_ADDR    0x10BAu

/* setjmp 主线保存 (与 PE/ELF 共用全局 mainline_jmp_buf) */
static int macho_mainline_save(void) __attribute__((noinline));
static int macho_mainline_save(void)
{
    extern uint32_t mainline_jmp_buf[6];
    extern int hepu_setjmp(uint32_t *buf);
    return hepu_setjmp(mainline_jmp_buf);
}

static int macho_enter_user(process_t *proc) __attribute__((noinline));
static int macho_enter_user(process_t *proc)
{
    if (macho_mainline_save() != 0) {
        return BINFMT_OK;   /* Mach-O 进程已退出, 回主线 */
    }
    extern void process_enter_user_mode(process_t *);
    process_enter_user_mode(proc);
    return BINFMT_OK;
}

/* 魔数识别: 0xFEEDFACE (32 位 x86 小端) */
int macho_probe(const uint8_t *buf, uint32_t len)
{
    if (!buf || len < 28) return 0;
    uint32_t magic = (uint32_t)buf[0] | ((uint32_t)buf[1] << 8) |
                     ((uint32_t)buf[2] << 16) | ((uint32_t)buf[3] << 24);
    return magic == MH_MAGIC;
}

int binfmt_macho_load(const char *path, const uint8_t *data, uint32_t size)
{
    extern const char *binfmt_get_cmdline(void);
    if (!data || size < 28) return BINFMT_ERR_IO;
    if (!macho_probe(data, size)) return BINFMT_ERR_FORMAT;

    const uint8_t *d = data;
    uint32_t magic = *(uint32_t *)(void *)(d + 0);
    uint32_t filetype = *(uint32_t *)(void *)(d + 12);
    uint32_t ncmds = *(uint32_t *)(void *)(d + 16);
    if (magic != MH_MAGIC || filetype != MH_EXECUTE) {
        vga_puts("[MACHO] err: not MH_EXECUTE\n");
        return BINFMT_ERR_FORMAT;
    }

    /* 遍历 load commands: 找 LC_SEGMENT (首段) + LC_UNIXTHREAD (eip) */
    uint32_t seg_vmaddr = 0, seg_vmsize = 0, seg_fileoff = 0, seg_filesize = 0;
    uint32_t entry = 0;
    uint32_t off = 28;
    for (uint32_t i = 0; i < ncmds && off + 8 <= size; i++) {
        uint32_t cmd = *(uint32_t *)(void *)(d + off);
        uint32_t csz = *(uint32_t *)(void *)(d + off + 4);
        if (csz < 8 || off + csz > size) break;
        if (cmd == LC_SEGMENT && csz >= 56) {
            if (seg_vmaddr == 0) {           /* 首段 (__TEXT) */
                seg_vmaddr  = *(uint32_t *)(void *)(d + off + 24);
                seg_vmsize  = *(uint32_t *)(void *)(d + off + 28);
                seg_fileoff = *(uint32_t *)(void *)(d + off + 32);
                seg_filesize= *(uint32_t *)(void *)(d + off + 36);
            }
        } else if (cmd == LC_UNIXTHREAD && csz >= 80) {
            uint32_t flavor = *(uint32_t *)(void *)(d + off + 8);
            uint32_t count  = *(uint32_t *)(void *)(d + off + 12);
            if (flavor == X86_THREAD_STATE && count >= 16) {
                entry = *(uint32_t *)(void *)(d + off + 16 + 10 * 4); /* regs[10]=eip */
            }
        }
        off += csz;
    }
    if (!seg_vmaddr || !entry) {
        vga_printf("[MACHO] err: seg=0x%x entry=0x%x\n", seg_vmaddr, entry);
        return BINFMT_ERR_FORMAT;
    }
    if (seg_fileoff + seg_filesize > size) {
        vga_puts("[MACHO] err: segment range\n");
        return BINFMT_ERR_IO;
    }

    vga_printf("[MACHO] load %s: vmaddr=0x%x vmsize=0x%x entry=0x%x\n",
               path, seg_vmaddr, seg_vmsize, entry);

    extern process_t *process_create(const char *, uint32_t,
                                     privilege_t, uint32_t);
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
    proc->compat_abi = 3;   /* Mach-O ABI: 沙箱生效 */
    proc->is_app = 1;       /* 可超时回收 */

    /* 绑定标准流 */
    {
        extern vfs_node_t *vfs_find(const char *);
        extern int vfs_bind_stdio(vfs_node_t *);
        vfs_node_t *console = vfs_find("/dev/console");
        if (console) vfs_bind_stdio(console);
        else vga_puts("[MACHO] warn: /dev/console missing\n");
    }

    /* ---- 段映射: vmaddr 页对齐 → mm_alloc_page + memcpy ---- */
    uint32_t base = seg_vmaddr & ~(PAGE_SIZE - 1);
    uint32_t end = seg_vmaddr + seg_vmsize;
    uint32_t max_end = 0;
    for (uint32_t vaddr = base; vaddr < end; vaddr += PAGE_SIZE) {
        uint32_t frame = mm_alloc_page();
        if (!frame) {
            vga_puts("[MACHO] error: frame alloc fail\n");
            return BINFMT_ERR_MEM;
        }
        if (paging_map(proc->page_dir, vaddr, frame,
                       PAGE_PRESENT | PAGE_WRITE | PAGE_USER) < 0) {
            vga_puts("[MACHO] error: map fail\n");
            return BINFMT_ERR_MEM;
        }
        uint32_t phys = paging_get_phys(proc->page_dir, vaddr);
        memset((void *)(uintptr_t)phys, 0, PAGE_SIZE);
        /* 从文件拷本页 (seg_fileoff 相对文件起点) */
        uint32_t pg_off = vaddr - base;
        if (pg_off < seg_filesize) {
            uint32_t copy = seg_filesize - pg_off;
            if (copy > PAGE_SIZE) copy = PAGE_SIZE;
            if (seg_fileoff + pg_off + copy <= size) {
                memcpy((void *)(uintptr_t)phys, data + seg_fileoff + pg_off, copy);
            }
        }
        if (vaddr + PAGE_SIZE > max_end) max_end = vaddr + PAGE_SIZE;
    }
    vga_printf("[MACHO]   image end=0x%x\n", max_end);

    /* ---- stub 页: max_end 页对齐处 (demo 段 0x1000 + 1 页 → 0x2000) ---- */
    uint32_t stub_va = (max_end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    for (uint32_t pg = 0; pg < MACH_STUB_PAGES; pg++) {
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
            memcpy((void *)(uintptr_t)(phys + 0x00), m_stub_write, sizeof(m_stub_write));
            memcpy((void *)(uintptr_t)(phys + 0x20), m_stub_exit, sizeof(m_stub_exit));
        }
    }

    /* ---- 注入 cmdline / argv (stub 页数据区) ---- */
    /* V3.87 C-2: 真实命令行——shell `run macho_demo alpha beta` 由
     * binfmt_set_cmdline 传入; 跳过首 token(程序名) 后分词 argv[1..],
     * argv[0]=pname(path 提取). argc 上限 4 (槽区 0x160-0x1DF 固定). */
    extern const char *binfmt_get_cmdline(void);
    const char *real_cmd = binfmt_get_cmdline();
    uint32_t cphys = paging_get_phys(proc->page_dir, stub_va);
    if (real_cmd && *real_cmd) {
        uint32_t n = 0;
        while (real_cmd[n] && n < 63) {
            *(char *)(uintptr_t)(cphys + M_CMDLINE_STR + n) = real_cmd[n];
            n++;
        }
        *(char *)(uintptr_t)(cphys + M_CMDLINE_STR + n) = '\0';
    } else {
        *(char *)(uintptr_t)(cphys + M_CMDLINE_STR) = '\0';
    }
    {
        uint32_t n = 0;
        while (pname[n] && n < 31) {
            *(char *)(uintptr_t)(cphys + M_ARGV0_STR + n) = pname[n];
            n++;
        }
        *(char *)(uintptr_t)(cphys + M_ARGV0_STR + n) = '\0';
    }
    /* argv[1..3]: 分词 (空格/tab/串尾截断, 支持 real_cmd 内部指针) */
    {
        uint32_t argc = 1;
        const char *argv_src[4];
        argv_src[0] = pname;
        uint32_t extra = 0;
        if (real_cmd && *real_cmd) {
            const char *s = real_cmd;
            while (*s && *s != ' ' && *s != '\t') s++;   /* 跳过 prog */
            while (*s == ' ' || *s == '\t') s++;
            while (*s && extra < 3) {                     /* 最多 3 个额外参数 */
                argv_src[extra + 1] = s;
                extra++;
                while (*s && *s != ' ' && *s != '\t') s++;
                while (*s == ' ' || *s == '\t') s++;
            }
        }
        argc = 1 + extra;
        for (uint32_t a = 0; a < argc; a++) {
            uint32_t base = M_ARGV0_STR + a * 0x20;
            const char *s = argv_src[a];
            uint32_t l = 0;
            while (s[l] && s[l] != ' ' && s[l] != '\t' && l < 31) {
                *(char *)(uintptr_t)(cphys + base + l) = s[l];
                l++;
            }
            *(char *)(uintptr_t)(cphys + base + l) = '\0';
        }
        /* argv 表: [0]=argc [1..argc]=argv ptr [argc+1]=NULL (demo 暂不读, 备后续) */
        *(uint32_t *)(uintptr_t)(cphys + M_ARGV_TBL) = argc;
        for (uint32_t a = 0; a < argc; a++) {
            *(uint32_t *)(uintptr_t)(cphys + M_ARGV_TBL + 4 + a * 4) =
                stub_va + M_ARGV0_STR + a * 0x20;
        }
        *(uint32_t *)(uintptr_t)(cphys + M_ARGV_TBL + 4 + argc * 4) = 0;
    }

    /* ---- patch demo 代码 (imm32 地址 + call rel32) ---- */
    {
        uint32_t dphys = paging_get_phys(proc->page_dir, seg_vmaddr);
        uint32_t rel;
        rel = stub_va - N_CALL1;
        *(uint32_t *)(uintptr_t)(dphys + P_CALL1) = rel;
        rel = stub_va - N_CALL2;
        *(uint32_t *)(uintptr_t)(dphys + P_CALL2) = rel;
        rel = stub_va - N_CALL3;
        *(uint32_t *)(uintptr_t)(dphys + P_CALL3) = rel;
        rel = stub_va - N_CALL4;
        *(uint32_t *)(uintptr_t)(dphys + P_CALL4) = rel;
        rel = stub_va - N_CALL5;
        *(uint32_t *)(uintptr_t)(dphys + P_CALL5) = rel;
        rel = stub_va - N_CALL6;
        *(uint32_t *)(uintptr_t)(dphys + P_CALL6) = rel;
        rel = stub_va - N_CALL7;
        *(uint32_t *)(uintptr_t)(dphys + P_CALL7) = rel;
        rel = stub_va - N_CALL8;
        *(uint32_t *)(uintptr_t)(dphys + P_CALL8) = rel;
        rel = (stub_va + 0x20) - N_EXIT;
        *(uint32_t *)(uintptr_t)(dphys + P_CALL_EXIT) = rel;
        *(uint32_t *)(uintptr_t)(dphys + P_MSG_ADDR)  = M_MSG_ADDR;
        *(uint32_t *)(uintptr_t)(dphys + P_LBL2_ADDR) = M_LBL2_ADDR;
        *(uint32_t *)(uintptr_t)(dphys + P_NL_ADDR)   = M_NL_ADDR;
        *(uint32_t *)(uintptr_t)(dphys + P_CMD_ADDR)  = stub_va + M_CMDLINE_STR;
        *(uint32_t *)(uintptr_t)(dphys + P_AV0_ADDR)  = stub_va + M_ARGV0_STR;
        *(uint32_t *)(uintptr_t)(dphys + P_NL2_ADDR)  = M_NL_ADDR;
        *(uint32_t *)(uintptr_t)(dphys + P_AV1_ADDR)  = stub_va + M_ARGV1_STR;
        *(uint32_t *)(uintptr_t)(dphys + P_NL3_ADDR)  = M_NL_ADDR;
    }

    /* 用户堆 */
    proc->heap_start = stub_va + MACH_STUB_PAGES * PAGE_SIZE;
    proc->heap_end = proc->heap_start;

    vga_printf("[MACHO] %s: loaded (PID=%d, entry=0x%x, sandbox)\n",
               path, proc->pid, entry);
    return macho_enter_user(proc);
}
