/* ============================================================================
 * 赫普OS 64位 - 用户程序加载器（P3-6 §5-3 第一阶段拆分）
 *
 * 本文件由 user_process.c 的 uproc_exec_impl() 中「纯加载」段平移而来：
 *   从 ext2 读 ELF → elf64_verify → 取入口 → ABI/PIE 判定 → PT_INTERP 处理
 *   → 主程序/解释器偏置加载。
 *
 * 平移纪律：错误码口径、串口取证日志、缓冲/VA 释放路径与原实现逐条对齐，
 * 不新增日志、不改 ABI 判定逻辑（V0.33：按 ELF 自身判定 elf64_is_linux_abi，
 * 记到进程 env_type，不走全局 g_running_linux_elf 开关）。
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include "elf64.h"
#include "uproc.h"
#include "uproc_loader64.h"

extern void vga_puts(const char *s);
extern void vga_put_hex(uint64_t val);
extern void *kmalloc(uint32_t size);
extern void kfree(void *ptr);
extern int ext2_64_read_file(const char *path, uint8_t *buf, int max_size);
extern int ext2_64_stat(const char *path, uint32_t *size, uint8_t *isdir);

/* 用户窗口下界（PDPT[1]）；链接在窗口外的非 PIE Linux ELF 搬不动，明确拒绝。 */
#define UWIN_START 0x0000000040000000ULL

int uproc64_load_image(const char *path, int argc, const char *const argv[],
                       uproc_image_t *img)
{
    /* 参数由 syscall 入口（hepu_exec）统一校验；这里防御性清零结果结构。 */
    for (unsigned int i = 0; i < sizeof(*img); i++) ((uint8_t *)img)[i] = 0;
    (void)argc; (void)argv;   /* 初始栈构造仍在进程模块，第二阶段下沉 */

    vga_puts("[USER64] exec: ");
    vga_puts(path);
    vga_puts("\n");

    /* 1. 从ext2读取ELF文件 - V0.20.15: 动态分配, 支持大ELF
     *
     * V0.33 修正两件事：
     *  a) 缓冲区按**文件真实大小**分配，而不是固定 2MB。
     *     旧实现每次 exec 都从 8MB 内核堆扣 2MB：启动自检连跑十几个用户程序时
     *     内核堆直接耗尽（[KHEAP] OOM: req=2097152），桌面起来之后任何用户程序
     *     都跑不起来。2MB 是为了迁就 256KB 静态缓冲上限的历史包袱
     *     （VERSIONING 0.20.7），现在有动态分配就不该再付这个代价。
     *  b) 两条错误返回路径漏掉了 kfree(elf_buffer)，每次泄漏 2MB。
     */
    /* 先读 ELF 头拿到真实大小, 再分配 */
    uint8_t elf_hdr[64];
    int hdr_size = ext2_64_read_file(path, elf_hdr, sizeof(elf_hdr));
    if (hdr_size < 64) {
        vga_puts("[USER64] exec ERROR: Cannot read file\n");
        return HEPU_E_NOENT;
    }
    /* 取文件真实大小（stat 拿不到就退回一个保守上限） */
    uint32_t file_size_hint = 0;
    {
        uint8_t isdir = 0;
        if (ext2_64_stat(path, &file_size_hint, &isdir) != 0 || isdir ||
            file_size_hint < 64 || file_size_hint > (16u << 20)) {
            file_size_hint = 0;   /* 0 = 未知，走兜底 */
        }
    }
    uint32_t elf_size = file_size_hint ? file_size_hint : (2 * 1024 * 1024);
    uint8_t *elf_buffer = (uint8_t *)kmalloc(elf_size);
    if (!elf_buffer) {
        vga_puts("[USER64] exec ERROR: kmalloc failed\n");
        return HEPU_E_NOMEM;
    }
    /* 把已读的头拷进去 */
    for (int i = 0; i < hdr_size; i++) elf_buffer[i] = elf_hdr[i];
    int file_size = ext2_64_read_file(path, elf_buffer, elf_size);
    if (file_size < 64) {
        vga_puts("[USER64] exec ERROR: Cannot read file\n");
        kfree(elf_buffer);
        return HEPU_E_NOENT;
    }

    vga_puts("[USER64] exec: read ");
    char buf[16];
    int n = file_size, i = 0;
    char tmp[16];
    do { tmp[i++] = n % 10 + '0'; } while ((n /= 10) > 0);
    int j = 0;
    while (i > 0) buf[j++] = tmp[--i];
    buf[j] = 0;
    vga_puts(buf);
    vga_puts(" bytes\n");

    /* 2. 验证ELF64 */
    if (elf64_verify(elf_buffer) != 0) {
        vga_puts("[USER64] exec ERROR: Invalid ELF64\n");
        kfree(elf_buffer);   /* V0.33: 补漏（原来这条路径泄漏整个缓冲区） */
        return HEPU_E_LOAD;
    }

    /* 3. 获取入口点 */
    uint64_t entry = elf64_get_entry(elf_buffer);
    vga_puts("[USER64] exec: entry=");
    char hex[] = "0123456789ABCDEF";
    char hbuf[19];
    uint64_t addr = entry;
    hbuf[0]='0'; hbuf[1]='x';
    for (int i = 0; i < 16; i++) { hbuf[17-i] = hex[addr & 0xF]; addr >>= 4; }
    hbuf[18] = 0;
    vga_puts(hbuf);
    vga_puts("\n");

    /* 4. 判定 ABI 并选择映射基址（V0.33）
     *
     * 三种情况：
     *  a) Hepu 原生 ELF：链接在 [0x40000000,0x50000000)，按链接地址原样映射，
     *     走**原生**系统调用号表。
     *  b) Linux PIE（ET_DYN）：位置无关，可以在用户窗口里任选基址。
     *     分配到「兼容程序区」[0x50000000,0x60000000)，用加载偏置把镜像摆进去，
     *     入口点与 R_X86_64_RELATIVE 一起修正。走 **Linux** 系统调用号表。
     *  c) Linux ET_EXEC（非 PIE）：链接在 0x400000 这类窗口外地址。
     *     这类二进制的绝对地址已经烧进代码和数据里，**搬不动**
     *     （搬过去必然跑飞），所以明确拒绝并给出可执行的建议，
     *     而不是丢一句「code range outside user window」让人猜。
     */
    int is_linux_abi = elf64_is_linux_abi(elf_buffer, file_size);
    uint64_t link_base = elf64_link_base(elf_buffer, file_size);
    uint64_t map_base = 0;           /* 0 = 按链接地址映射 */
    int is_pie = 0;
    {
        is_pie = (elf64_get_type(elf_buffer) == 3 /*ET_DYN*/);
    }

    /* V0.54.6（P1-7）: PT_INTERP —— 用户态动态链接器。
     *
     * 内核只负责两件事：把主程序和解释器（ld.so）各自按偏置摆进用户地址
     * 空间，并把 System V 初始栈里的 AT_BASE/AT_ENTRY/AT_PHDR 填对。
     * DT_NEEDED、符号查找、重定位、TLS 初始化全部由解释器自己完成 ——
     * 这正是 Linux 的内核/ld.so 分工，也是能直接吃 glibc 动态二进制的唯一
     * 现实路径（在用户态重新实现整个 glibc 链接器不现实）。 */
    int has_interp = elf64_has_interp(elf_buffer, file_size);
    const char *interp_path = NULL;
    void *interp_loaded = NULL;
    uint64_t interp_load_size = 0, interp_code_vaddr = 0, interp_entry_vaddr = 0;
    if (has_interp) {
        interp_path = elf64_find_interp(elf_buffer, file_size);
        if (!interp_path || interp_path[0] != '/') {
            vga_puts("[USER64] exec ERROR: PT_INTERP path missing/invalid\n");
            kfree(elf_buffer);
            return HEPU_E_LOAD;
        }
        vga_puts("[USER64] exec: dynamic ELF, interpreter=");
        vga_puts(interp_path);
        vga_puts("\n");
    }

    if (is_linux_abi && link_base && link_base < UWIN_START) {
        /* 链接在用户窗口之外 */
        if (!is_pie) {
            vga_puts("[USER64] exec ERROR: Linux ET_EXEC linked at ");
            vga_put_hex(link_base);
            vga_puts("\n");
            vga_puts("[USER64]   outside Hepu user window [0x40000000,0x80000000).\n");
            vga_puts("[USER64]   ET_EXEC absolute addresses are baked in - cannot be rebased.\n");
            vga_puts("[USER64]   Rebuild as PIE (-fPIE / -static-pie) to run on Hepu.\n");
            kfree(elf_buffer);
            return HEPU_E_LOAD;
        }
    }

    if (is_pie) {
        /* PIE：从兼容程序区分配一个基址 */
        uint64_t need = elf64_image_size(elf_buffer, file_size);
        if (!need) {
            vga_puts("[USER64] exec ERROR: bad PIE layout\n");
            kfree(elf_buffer);
            return HEPU_E_LOAD;
        }
        map_base = uproc_va_alloc_pending(VA_COMPAT_BASE, VA_COMPAT_END, need);
        if (!map_base) {
            vga_puts("[USER64] exec ERROR: no VA space for PIE image\n");
            kfree(elf_buffer);
            return HEPU_E_NOMEM;
        }
        vga_puts("[USER64] PIE load bias: base=");
        vga_put_hex(map_base);
        vga_puts(" size=");
        vga_put_hex(need);
        vga_puts("\n");
    }

    /* 加载ELF到内核内存（带偏置） */
    void *loaded = elf64_load_biased(elf_buffer, file_size, map_base);
    if (!loaded) {
        vga_puts("[USER64] exec ERROR: ELF load failed\n");
        if (map_base) uproc_va_free_pending(map_base);
        kfree(elf_buffer);
        return HEPU_E_LOAD;
    }
    /* V0.20.80: elf_buffer保留到重定位完成后再free */

    /* V0.33: 偏置加载后 g_elf_last_* 已是修正后的主程序取值。 */
    uint64_t code_vaddr = g_elf_last_code_start;
    uint64_t entry_vaddr = g_elf_last_entry ? g_elf_last_entry : entry;

    /* V0.33 修复：只复制**映像实际大小**，不能拿文件大小当映像大小。
     *
     * 原来：exec_code_size = max(映像大小, 文件大小)。
     * 但 elf64_load_biased 返回的缓冲区只有「映像大小」那么长
     * （= 最高 PT_LOAD 段末尾 - 映像原点），文件大小可能更大
     * （文件里还有符号表、字符串表、重定位表等不加载的部分）。
     * 于是这里会从内核堆缓冲**越界读** 几十 KB，
     * 并把越界的垃圾内容映射进用户空间。
     * 正确做法：映像多大就映多大。
     */
    uint64_t exec_code_size = g_elf_last_load_size;

    /* P1-7: 解释器（ld.so）按 ET_DYN 偏置装进「库区」[0x60000000,0x70000000)。
     * 主程序留在兼容区 [0x50000000,0x60000000)；两层互不重叠，
     * ld.so 之后用 mmap 把 libc 等 .so 放到 0x64000000 以上。 */
    if (has_interp) {
        uint32_t isize = 0;
        uint8_t isdir = 0;
        if (ext2_64_stat(interp_path, &isize, &isdir) != 0 || isdir ||
            isize < 64 || isize > 8u * 1024u * 1024u) {
            vga_puts("[USER64] exec ERROR: cannot stat interpreter\n");
            if (map_base) uproc_va_free_pending(map_base);
            kfree(loaded);
            kfree(elf_buffer);
            return HEPU_E_NOENT;
        }
        uint8_t *ibuf = (uint8_t *)kmalloc(isize);
        if (!ibuf) {
            vga_puts("[USER64] exec ERROR: OOM for interpreter\n");
            if (map_base) uproc_va_free_pending(map_base);
            kfree(loaded);
            kfree(elf_buffer);
            return HEPU_E_NOMEM;
        }
        int got = ext2_64_read_file(interp_path, ibuf, (int)isize);
        if (got < 64 || elf64_verify(ibuf) != 0) {
            vga_puts("[USER64] exec ERROR: bad interpreter image\n");
            kfree(ibuf);
            if (map_base) uproc_va_free_pending(map_base);
            kfree(loaded);
            kfree(elf_buffer);
            return HEPU_E_LOAD;
        }
        uint64_t need = elf64_image_size(ibuf, (uint64_t)got);
        if (!need) {
            vga_puts("[USER64] exec ERROR: bad interpreter layout\n");
            kfree(ibuf);
            if (map_base) uproc_va_free_pending(map_base);
            kfree(loaded);
            kfree(elf_buffer);
            return HEPU_E_LOAD;
        }
        interp_code_vaddr = uproc_va_alloc_pending(VA_LIB_BASE, VA_LIB_END, need);
        if (!interp_code_vaddr) {
            vga_puts("[USER64] exec ERROR: no VA space for interpreter\n");
            kfree(ibuf);
            if (map_base) uproc_va_free_pending(map_base);
            kfree(loaded);
            kfree(elf_buffer);
            return HEPU_E_NOMEM;
        }
        interp_loaded = elf64_load_biased(ibuf, (uint64_t)got, interp_code_vaddr);
        if (!interp_loaded) {
            vga_puts("[USER64] exec ERROR: interpreter load failed\n");
            uproc_va_free_pending(interp_code_vaddr);
            kfree(ibuf);
            if (map_base) uproc_va_free_pending(map_base);
            kfree(loaded);
            kfree(elf_buffer);
            return HEPU_E_LOAD;
        }
        interp_load_size = g_elf_last_load_size;
        interp_entry_vaddr = g_elf_last_entry;
        kfree(ibuf);
        vga_puts("[USER64] interpreter loaded at ");
        vga_put_hex(interp_code_vaddr);
        vga_puts(", entry=");
        vga_put_hex(interp_entry_vaddr);
        vga_puts("\n");
    }

    /* 动态程序：CPU 从解释器入口开始跑；AT_ENTRY 仍指向主程序 _start。 */
    uint64_t at_entry_vaddr = entry_vaddr;
    if (has_interp) {
        at_entry_vaddr = entry_vaddr;
        entry_vaddr = interp_entry_vaddr;
    }

    img->loaded          = loaded;
    img->code_vaddr      = code_vaddr;
    img->entry_vaddr     = entry_vaddr;
    img->exec_code_size  = exec_code_size;
    img->at_entry_vaddr  = at_entry_vaddr;
    img->env_type        = is_linux_abi ? ENV_LINUX_COMPAT : ENV_NATIVE;
    img->has_interp      = has_interp;
    img->interp_loaded   = interp_loaded;
    img->interp_load_size= interp_load_size;
    img->interp_code_vaddr = interp_code_vaddr;
    img->map_base        = map_base;
    img->elf_buffer      = elf_buffer;
    img->file_size       = (uint64_t)file_size;
    return 0;
}

void uproc64_image_release_pending(uproc_image_t *img)
{
    if (!img) return;
    if (img->map_base) {
        uproc_va_free_pending(img->map_base);
        img->map_base = 0;
    }
}
