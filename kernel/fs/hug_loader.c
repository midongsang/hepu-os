/* ============================================================================
 * 赫普OS - .hug 应用加载器
 * 解析 .hug (Hyper User Gear) 应用包，加载并执行用户态应用
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include "hug_loader.h"
#include "initrd.h"
#include "ide_ext2.h"
#include "vga.h"
#include "string.h"
#include "process.h"
#include "paging.h"

/* V3.85: 主线 Shell 上下文保存区——hug_run 创建用户进程前 setjmp,
 * 用户进程退出(sys_exit)后 longjmp 回到此处, 原 Shell 循环继续,
 * 不再 hlt 停机/重复启动 shell_main. */
uint32_t mainline_jmp_buf[6];

/* V3.85: noinline 包装——保证 setjmp 的栈保存点在标准函数调用边界
 * (见 hug_run 内注释: 直接内联调用会被优化器复用参数区导致 esp 偏移). */
static int mainline_save(void) __attribute__((noinline));
static int mainline_save(void)
{
    extern int hepu_setjmp(uint32_t *buf);
    return hepu_setjmp(mainline_jmp_buf);
}

int hug_verify(const void *data, uint32_t size)
{
    if (!data || size < sizeof(hug_header_t)) {
        return -1;
    }

    const hug_header_t *header = (const hug_header_t *)data;

    if (header->magic != HUG_MAGIC) {
        vga_printf("[HUG] verify: magic=0x%x expect=0x%x data=%p\n",
                   header->magic, HUG_MAGIC, data);
        return -2;
    }

    if (header->version != HUG_VERSION) {
        return -3;
    }

    /* 检查各段偏移是否在文件范围内（V3.21: 用 64 位中间运算防 32 位溢出绕过） */
    if ((uint64_t)header->manifest_offset + header->manifest_size > size) return -4;
    if ((uint64_t)header->code_offset + header->code_size > size) return -5;
    if ((uint64_t)header->data_offset + header->data_size > size) return -6;

    /* V3.21: entry_point 必须在 code 段内，防止执行文件外任意位置 */
    if (header->entry_point >= header->code_size) return -7;

    return 0;
}

int hug_parse(const void *data, uint32_t size, hug_app_t *app)
{
    if (!data || !app) return -1;

    int ret = hug_verify(data, size);
    if (ret != 0) return ret;

    const hug_header_t *header = (const hug_header_t *)data;
    memcpy(&app->header, header, sizeof(hug_header_t));

    /* 设置各段指针 */
    app->manifest = (const char *)data + header->manifest_offset;
    app->code = (uint8_t *)data + header->code_offset;
    app->data = (uint8_t *)data + header->data_offset;
    app->entry = header->entry_point;

    return 0;
}

void hug_info(const hug_app_t *app)
{
    if (!app) return;

    vga_puts("=== .hug app info ===\n");
    vga_printf(" version: %d\n", app->header.version);
    vga_printf(" code seg: %d bytes @ 0x%x\n",
               app->header.code_size, app->header.code_offset);
    vga_printf(" data seg: %d bytes @ 0x%x\n",
               app->header.data_size, app->header.data_offset);
    vga_printf("  Manifest: %d bytes\n", app->header.manifest_size);
    vga_printf(" entry: 0x%x\n", app->header.entry_point);

    /* 显示manifest前128字节 */
    if (app->manifest && app->header.manifest_size > 0) {
        vga_puts(" Manifest(prev 128 bytes):\n");
        uint32_t len = app->header.manifest_size;
        if (len > 128) len = 128;
        for (uint32_t i = 0; i < len; i++) {
            vga_putc(app->manifest[i]);
        }
        vga_putc('\n');
    }
}

int hug_run(const char *path)
{
    if (!path) return -1;

    /* V3.82: 自动补 .hug 后缀——initrd 文件表存的是 "name.hug",
     * shell 传 "sysinfo" 时精确匹配必失败 (实测 file not found) */
    static char full[64];
    int plen = 0;
    while (path[plen] && plen < 60) plen++;
    int has_ext = (plen > 4) && (path[plen-4]=='.') && (path[plen-3]=='h')
               && (path[plen-2]=='u') && (path[plen-1]=='g');
    if (has_ext) {
        memcpy(full, path, plen); full[plen] = 0;
    } else {
        memcpy(full, path, plen);
        full[plen] = '.'; full[plen+1] = 'h'; full[plen+2] = 'u'; full[plen+3] = 'g';
        full[plen+4] = 0;
    }

    vga_printf("[HUG] load app: %s\n", full);

    /* V3.87-D2: 数据盘优先——已安装(hug install)的包从数据盘读取,
     * 体现"持久安装"语义 (出厂 initrd 只是预置源区). 读取失败回退 initrd. */
    static char disk_buf[32768];   /* 覆盖最大 .hug 包 (diag.hug=16948B) */
    int size = 0;
    void *data = NULL;
    const char *src = "initrd";
    int n = ide_ext2_read_file("/app", full, disk_buf, (int)sizeof(disk_buf));
    if (n > 0) {
        data = disk_buf;
        size = n;
        src = "data disk";
        vga_printf("[HUG] from data disk: %s (%d B)\n", full, n);
    } else {
        /* 回退: 从initrd打开文件 */
        int fd = initrd_open(full);
        if (fd < 0) {
            vga_printf("[HUG] error: file not found %s\n", full);
            return -1;
        }
        size = initrd_size(fd);
        if (size < (int)sizeof(hug_header_t)) {
            vga_puts("[HUG] error: file too small \n");
            initrd_close(fd);
            return -1;
        }
        int index = initrd_find(full);
        if (index < 0) {
            initrd_close(fd);
            return -1;
        }
        data = initrd_get_data(index);
        initrd_close(fd);
        if (!data) {
            vga_puts("[HUG] error: get data ptr failed \n");
            return -1;
        }
        vga_printf("[HUG] from initrd: %s (%d B)\n", full, size);
    }
    (void)src;

    /* 解析.hug */
    hug_app_t app;
    int ret = hug_parse(data, size, &app);
    if (ret != 0) {
        vga_printf("[HUG] error: parse failed(code=%d)\n", ret);
        return -1;
    }

    hug_info(&app);

    /* V3.87-D5: 权限声明提取 + 白名单沙箱校验 (APK 式权限限制) */
    {
        extern int hug_extract_permissions(const char *, uint32_t,
                                           char (*)[20], int);
        extern int hug_check_permissions(char (*)[20], int);
        extern void hug_print_permissions(char (*)[20], int);
        char perms[4][20];
        int np = hug_extract_permissions(app.manifest, app.header.manifest_size,
                                         perms, 4);
        hug_print_permissions(perms, np);
        if (hug_check_permissions(perms, np) != 0) {
            vga_puts("[HUG] run blocked by permission policy\n");
            return -1;
        }
    }

    /* 创建用户态进程执行应用
     * V3.86: 用户空间基址加载——.hug 按 USER_TEXT_ADDR 链接(-Ttext),
     * 静态数据/字符串绝对寻址正确; 此前 -Ttext=0 链接却在内核地址
     * (initrd 区)执行, 绝对寻址错位 → 访问低地址被 demand paging
     * 静默映射, 数据错乱 → clock 等应用 EIP=0 空转. */
    uint32_t entry = USER_TEXT_ADDR + app.entry;

    vga_printf("[HUG] create proc, entry =0x%x\n", entry);

    process_t *proc = process_create(full, entry, PRIV_USER, 1);  /* E1: NORM */
    if (!proc) {
        vga_puts("[HUG] error: proc create failed \n");
        return -1;
    }
    proc->is_app = 1;   /* V3.86: 标记为 .hug 应用——调度器可超时强制回收 */

    /* V3.86: 映射用户代码区并复制 .hug 代码/数据到 USER_TEXT_ADDR.
     * 物理帧经 pmm 分配, 页目录用该进程的独占用户目录(proc->page_dir),
     * 与用户栈(0x40000000)分离. 代码区固定 64KB(16页, 覆盖最大应用).
     * V3.86: 固定映射满 16 页(64KB)——.hug ELF 含 bss 段(0x20004000 起,
     * 如 filemgr 的全局文件列表), 只映射 code+data 实际大小会导致
     * 访问 bss 触发 #PF(实测 CR2=0x20005340); 未复制区域清零(bss 语义). */
    {
        extern uint32_t mm_alloc_page(void);
        uint32_t total = app.header.code_size + app.header.data_size;

        /* 在用户目录中建立映射(不切换 CR3, 直接操作页表结构) */
        for (uint32_t i = 0; i < 16; i++) {
            uint32_t phys = mm_alloc_page();
            if (!phys) {
                vga_puts("[HUG] error: code frame alloc fail \n");
                return -1;
            }
            if (paging_map(proc->page_dir,
                           USER_TEXT_ADDR + i * PAGE_SIZE,
                           phys,
                           PAGE_PRESENT | PAGE_WRITE | PAGE_USER) < 0) {
                vga_puts("[HUG] error: code map fail \n");
                return -1;
            }
        }
        /* 复制代码/数据 + bss 清零: 物理帧地址可由 paging_get_phys 取回 */
        extern uint32_t paging_get_phys(page_directory_t *, uint32_t);
        for (uint32_t i = 0; i < 16; i++) {
            uint32_t phys = paging_get_phys(proc->page_dir,
                                            USER_TEXT_ADDR + i * PAGE_SIZE);
            uint32_t off = i * PAGE_SIZE;
            if (off < total) {
                uint32_t chunk = PAGE_SIZE;
                if (off + chunk > total) chunk = total - off;
                memcpy((void *)phys, app.code + off, chunk);
            } else {
                memset((void *)phys, 0, PAGE_SIZE);   /* bss 区清零 */
            }
        }
    }

    vga_printf("[HUG] app started: PID=%d\n", proc->pid);

    /* V3.85: 保存主线 Shell 上下文. 用户进程退出后 sys_exit 会
     * longjmp 回这里(返回值非0), 原 Shell 循环继续服务下一条命令.
     * 必须经独立 noinline 包装调用: 直接调用 hepu_setjmp 时, 优化器
     * 会复用此前 vga_printf 的挂起参数区, 使 setjmp 保存的 esp 相对
     * hug_run 栈基位偏移 16B → longjmp 恢复后 return 0 弹出错误地址
     * (实测跳飞, shell 不恢复). 包装函数保证保存/恢复都在标准调用边界. */
    if (mainline_save() != 0) {
        return 0;   /* 用户进程已退出, 回到主线 Shell */
    }

    /* 进入用户态执行 */
    process_enter_user_mode(proc);

    return 0;
}
