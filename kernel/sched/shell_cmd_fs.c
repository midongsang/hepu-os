/* ============================================================================
 * 赫普OS - Shell：文件系统/磁盘/图形命令
 * 从 shell.c 拆分（V6.46 代码重构，大文件拆分目标<500行）
 * 包含：show_partitions/cmd_fs（P4 之后 cp/rm 全部走 VFS，直调旧路径已删）
 *       sys_reboot_after_gfx/cmd_gfxtest/cmd_disk/cmd_gfx
 * ========================================================================== */

#include <stdint.h>
#include <string.h>
#include "vga.h"
#include "process.h"
#include "shell.h"
#include "serial.h"
#include "pit.h"
#include "hepu.h"
#include "i18n.h"
#include "vfs.h"
#include "ext2.h"
#include "ide_ext2.h"
#include "ide.h"
#include "wm.h"

static void show_partitions(const uint8_t *mbr)
{
    if (mbr[510] != 0x55 || mbr[511] != 0xAA) {
        vga_puts(" [DISK] no valid MBR signature (0x55AA)\n");
        return;
    }
    vga_puts(" [DISK] MBR partitions:\n");
    int n = 0;
    for (int i = 0; i < 4; i++) {
        const uint8_t *e = mbr + 446 + i * 16;
        uint8_t type = e[4];
        if (type == 0) continue;
        uint32_t lba = (uint32_t)e[8] | ((uint32_t)e[9] << 8) |
                       ((uint32_t)e[10] << 16) | ((uint32_t)e[11] << 24);
        uint32_t secs = (uint32_t)e[12] | ((uint32_t)e[13] << 8) |
                        ((uint32_t)e[14] << 16) | ((uint32_t)e[15] << 24);
        vga_printf("  #%d type=0x%02x boot=%s lba=%d size=%d MB\n",
                   i, type, (e[0] & 0x80) ? "Y" : "N", lba,
                   (secs + 2047) / 2048);
        n++;
    }
    if (n == 0) vga_puts("  (no active partitions)\n");
}

/* V2.25: gfx 纯色填充测试——定位花屏根源: 依次填红绿蓝白 */
void cmd_dmesg(char *arg)
{
    extern void klog_dump(void);
    extern void klog_dump_level(int);
    if (arg && strncmp(arg, "-l", 2) == 0 && arg[2] == ' ') {
        int lvl = 0;
        for (int i = 3; arg[i]; i++) lvl = lvl * 10 + (arg[i] - '0');
        klog_dump_level(lvl);
        return;
    }
    klog_dump();
}

/* V3.87-E14: 设置全局日志阈值 0=ERR 1=WARN 2=INFO 3=DEBUG */
void cmd_loglevel(char *arg)
{
    extern void klog_set_level(int);
    extern int klog_get_level(void);
    if (!arg || !*arg) {
        vga_printf(" loglevel <0..3> (当前=%d, 0=ERR 1=WARN 2=INFO 3=DEBUG)\n",
                   klog_get_level());
        return;
    }
    int v = 0;
    for (int i = 0; arg[i]; i++) v = v * 10 + (arg[i] - '0');
    klog_set_level(v);
    vga_printf(" loglevel set to %d\n", v);
}

/* V4.44-E89: 递归复制 helper——支持文件/目录/批量
 * dirpath: 源目录 ("/" 或 "docs"), name: 源条目名, dstbase: 目标目录名 (根下)
 * depth 防循环 (最大 4 层) */

/* 2026-11-20 P1: 路径拼接 helper —— 现在各处手工 while 拼路径重复了 6 处，
 * 改走 VFS 前先收敛，避免把同样的边界 bug 复制一遍。 */
static void fs_join_path(const char *dir, const char *name, char *out, int max)
{
    int n = 0;
    if (dir && dir[0]) {
        while (*dir && n < max - 1) out[n++] = *dir++;
    } else {
        out[n++] = '/';
    }
    if (n > 1 && out[n - 1] == '/') n--;
    if (name && name[0]) {
        if (n < max - 1) out[n++] = '/';
        while (*name && n < max - 1) out[n++] = *name++;
    }
    out[n] = 0;
}

/* 把 "hello" / "/hello" 统一成 "/hello" */
static void fs_abs_path(const char *in, char *out, int max)
{
    int n = 0;
    if (!in || !in[0]) { if (max > 1) { out[0] = '/'; out[1] = 0; } return; }
    if (in[0] != '/') out[n++] = '/';
    while (*in && n < max - 1) out[n++] = *in++;
    out[n] = 0;
}

/* ===== P4: VFS 版复制 =====
 * 原来的 fs_cp_recursive 全是 ide_ext2_* 直调（fm_entry_t、write_raw 那一套），
 * 这里换成 vfs_open/read/write + vfs_readdir，目录递归靠 vfs_mkdir 建目标。 */
static int fs_cp_file_vfs(const char *src, const char *dst)
{
    int sfd = vfs_open(src, VFS_O_RDONLY);
    if (sfd < 0) return -1;
    int dfd = vfs_open(dst, VFS_O_WRONLY | VFS_O_CREAT | VFS_O_TRUNC);
    if (dfd < 0) { vfs_close(sfd); return -1; }

    static char cpbuf[2048];
    int total = 0, err = 0;
    for (int guard = 0; guard < 8192; guard++) {
        int r = vfs_read(sfd, cpbuf, sizeof(cpbuf));
        if (r < 0) { err = 1; break; }
        if (r == 0) break;
        int w = vfs_write(dfd, cpbuf, (uint32_t)r);
        if (w != r) { err = 1; break; }
        total += w;
    }
    vfs_close(sfd);
    vfs_close(dfd);
    vfs_sync();          /* 复制完必须落盘，否则重启就没了 */
    return err ? -1 : total;
}

static int fs_cp_dir_vfs(const char *src, const char *dst, int depth)
{
    if (depth > 6) return -1;    /* 防深递归把栈打穿 */
    if (vfs_mkdir(dst) != 0) {
        vfs_node_t *d = vfs_find(dst);
        if (!d || d->type != VFS_DIRECTORY) return -1;
    }
    vfs_dirent_t ents[64];
    int n = vfs_readdir(src, ents, 64);
    if (n < 0) return -1;
    int ok = 0;
    for (int i = 0; i < n; i++) {
        if (strcmp(ents[i].name, ".") == 0 || strcmp(ents[i].name, "..") == 0) continue;
        char sp[192], dp[192];
        fs_join_path(src, ents[i].name, sp, sizeof(sp));
        fs_join_path(dst, ents[i].name, dp, sizeof(dp));
        if (ents[i].type == VFS_DIRECTORY) ok += fs_cp_dir_vfs(sp, dp, depth + 1);
        else ok += fs_cp_file_vfs(sp, dp);
    }
    return ok;
}

void cmd_fs(char *args)
{
    if (!args || args[0] == 0) {
        vga_puts(" fs usage: fs mount [-f] | ls | cat <name> | write <name> <text> | stat <name> | df | sync | mkdir | rm | cp <src> <dst> | cpall <dst> | mv | fsck\n");
        return;
    }
    if (strncmp(args, "mount", 5) == 0) {
        extern int ide_ext2_mount(void);
        int full = (args[5] == ' ' && args[6] == '-' && args[7] == 'f');
        if (ide_ext2_mount() == 0) {
            vga_puts(" ext2 fs mounted (IDE disk)\n");
            if (full) {
                /* D1：全量扫描只是调试模式。正式路径是懒加载，启动流程不调用。 */
                extern int ext2_mount(const char *);
                if (ext2_mount("/") == 0) vga_puts(" full scan done (debug mode)\n");
                else vga_puts(" full scan FAIL\n");
            }
        } else {
            vga_puts(" mount FAIL\n");
        }
    } else if (strncmp(args, "ls", 2) == 0) {
        /* P1: 改走 VFS（支持带目录参数，子目录经 ext2_readdir_ino 读真实条目） */
        const char *path = args + 2;
        while (*path == ' ') path++;
        if (*path == 0) path = "/";
        vfs_dirent_t ents[64];
        int cnt = vfs_readdir(path, ents, 64);
        if (cnt < 0) {
            vga_printf(" ls %s FAIL\n", path);
        } else {
            vga_printf(" %s (%d):\n", path, cnt);
            for (int i = 0; i < cnt; i++)
                vga_printf("   %s%s  %d\n", ents[i].name,
                           ents[i].type == VFS_DIRECTORY ? "/" : "", ents[i].size);
        }
    } else if (strncmp(args, "cat ", 4) == 0) {
        /* P2: 改走 VFS（open + read + close） */
        char abs[192];
        fs_abs_path(args + 4, abs, sizeof(abs));
        int fd = vfs_open(abs, VFS_O_RDONLY);
        if (fd < 0) {
            vga_printf(" cat %s FAIL (open)\n", abs);
        } else {
            static char cbuf[4096];
            int rd = 0;
            while (rd < (int)sizeof(cbuf) - 1) {
                int n = vfs_read(fd, cbuf + rd, sizeof(cbuf) - 1 - rd);
                if (n <= 0) break;
                rd += n;
            }
            cbuf[rd > 0 ? rd : 0] = 0;
            vga_puts(" ");
            vga_puts(cbuf);
            vga_puts("\n");
            vfs_close(fd);
        }
    } else if (strncmp(args, "write ", 6) == 0) {
        /* P2: 改走 VFS（open+write+close+sync） */
        const char *rest = args + 6;
        /* 拆分 name 和 text(第一个空格分隔) */
        const char *name = rest;
        const char *sp = rest;
        while (*sp && *sp != ' ') sp++;
        if (*sp == 0) { vga_puts(" fs write: need name and text\n"); return; }
        char nm[128];
        uint32_t nl = sp - name;
        if (nl > 127) nl = 127;
        memcpy(nm, name, nl);
        nm[nl] = 0;
        const char *data = sp + 1;
        {
            char abs[192];
            fs_abs_path(nm, abs, sizeof(abs));
            /* vfs_open 现在认 O_CREAT（先落 ext2、再补内存节点），
             * 不用再手工 ext2_path_create 兜底了 —— shell 这条路径彻底走 VFS。 */
            int fd = vfs_open(abs, VFS_O_WRONLY | VFS_O_CREAT | VFS_O_TRUNC);
            if (fd < 0) {
                vga_printf(" write %s FAIL (open)\n", abs);
            } else {
                int w = vfs_write(fd, data, strlen(data));
                vfs_close(fd);
                /* 必须显式同步：vfs_write 只进块缓存，不落盘 */
                if (w >= 0 && vfs_sync() == 0)
                    vga_printf(" wrote %d bytes to %s (synced)\n", w, abs);
                else if (w >= 0)
                    vga_printf(" wrote %d bytes to %s (SYNC FAIL)\n", w, abs);
                else
                    vga_printf(" write %s FAIL\n", abs);
            }
        }
    } else if (strncmp(args, "stat ", 5) == 0) {
        /* P1: 改走 VFS */
        char abs[192];
        fs_abs_path(args + 5, abs, sizeof(abs));
        vfs_node_t *n = vfs_find(abs);
        if (!n) {
            vga_printf(" stat %s: not found\n", abs);
        } else {
            vga_printf(" stat %s: %s size=%d inode=%d\n", abs,
                       n->type == VFS_DIRECTORY ? "dir" : "file",
                       n->size, n->inode_num);
        }
    } else if (strcmp(args, "df") == 0) {
        /* P5: 故意保留直调。
         * df 读的是超级块里的全局块/inode 计数, 不涉及路径解析, 也不改内存树;
         * 走 VFS 反而要额外维护"每个挂载点的用量"这一层抽象, 当前只有一个
         * ext2 挂载点, 没有必要。等出现第二个文件系统时再统一。 */
        extern int ide_ext2_df(void);
        if (ide_ext2_df() != 0) vga_puts(" df FAIL\n");
    } else if (strcmp(args, "sync") == 0) {
        extern int ide_ext2_sync(void);
        if (ide_ext2_sync() == 0) vga_puts(" fs synced\n");
        else vga_puts(" sync FAIL\n");
    } else if (strncmp(args, "rm ", 3) == 0) {
        /* P3: 改走 VFS。
         * 旧写法把整段 "/p3shell" 当名字传给 fs_rm_recursive("/", name)，
         * 拼出来是 "//p3shell"，永远找不到 —— 所以 rm 一直"删完还在"。
         * 现在统一规整成绝对路径，再按节点类型分派 rmdir / unlink。 */
        char abs[192];
        fs_abs_path(args + 3, abs, sizeof(abs));
        vfs_node_t *node = vfs_find(abs);
        if (!node) {
            vga_printf(" rm %s: not found\n", abs);
        } else if (node->type == VFS_DIRECTORY) {
            if (vfs_rmdir(abs) == 0) vga_printf(" rm: dir %s removed\n", abs);
            else vga_printf(" rm: dir %s FAIL (非空或删除失败)\n", abs);
        } else {
            if (vfs_unlink(abs) == 0) vga_printf(" rm: %s removed\n", abs);
            else vga_printf(" rm: %s FAIL\n", abs);
        }
    } else if (strncmp(args, "cpall ", 6) == 0) {
        /* P4: 改走 VFS —— 遍历根目录，逐项递归复制 */
        const char *dst = args + 6;
        while (*dst == ' ') dst++;
        if (*dst == 0) { vga_puts(" fs cpall <dst>\n"); return; }
        char dabs[192];
        fs_abs_path(dst, dabs, sizeof(dabs));
        if (!vfs_find(dabs) && vfs_mkdir(dabs) != 0) {
            vga_printf(" cpall: mkdir %s FAIL\n", dabs);
            return;
        }

        vfs_dirent_t ents[64];
        int cnt = vfs_readdir("/", ents, 64);
        if (cnt <= 0) { vga_puts(" cpall: empty root\n"); return; }
        int ok = 0, bad = 0;
        for (int i = 0; i < cnt; i++) {
            if (strcmp(ents[i].name, ".") == 0 || strcmp(ents[i].name, "..") == 0) continue;
            /* cpall 只复制真实磁盘内容。proc/dev/sys 是内核伪文件系统，
             * 逐项复制会造出成千上万个 1 块文件，几十秒内把 58MB 盘写满，
             * 后续任何写操作(包括终端编辑器)都会 ENOSPC。 */
            if (strcmp(ents[i].name, "proc") == 0 || strcmp(ents[i].name, "dev") == 0 ||
                strcmp(ents[i].name, "sys") == 0 || strcmp(ents[i].name, "tmp") == 0) continue;
            char sp[192], dp[192];
            fs_join_path("/", ents[i].name, sp, sizeof(sp));
            if (strcmp(sp, dabs) == 0) continue;   /* 跳过目标自身，防自我嵌套 */
            fs_join_path(dabs, ents[i].name, dp, sizeof(dp));
            int r = (ents[i].type == VFS_DIRECTORY)
                  ? fs_cp_dir_vfs(sp, dp, 1)
                  : fs_cp_file_vfs(sp, dp);
            if (r >= 0) ok++; else bad++;
        }
        vga_printf(" cpall: %d items copied to %s", ok, dabs);
        if (bad) vga_printf(" (%d failed)", bad);
        vga_puts("\n");
    } else if (strncmp(args, "cp ", 3) == 0) {
        /* P4: 改走 VFS。目标若是已存在目录，拷到它里面（保留源文件名）。 */
        const char *p1 = args + 3;
        const char *p2 = p1;
        while (*p2 && *p2 != ' ') p2++;
        if (*p2 == 0) { vga_puts(" fs cp: need src and dst\n"); return; }
        char srcn[128], dstn[128];
        uint32_t n1 = p2 - p1; if (n1 > 127) n1 = 127;
        memcpy(srcn, p1, n1); srcn[n1] = 0;
        const char *p3 = p2 + 1;
        while (*p3 == ' ') p3++;
        uint32_t n2 = 0;
        while (p3[n2] && p3[n2] != ' ' && n2 < 127) n2++;
        memcpy(dstn, p3, n2); dstn[n2] = 0;

        char sabs[192], dabs[192], final[192];
        fs_abs_path(srcn, sabs, sizeof(sabs));
        fs_abs_path(dstn, dabs, sizeof(dabs));

        vfs_node_t *sn = vfs_find(sabs);
        if (!sn) { vga_printf(" cp: %s not found\n", sabs); return; }

        vfs_node_t *dn = vfs_find(dabs);
        if (dn && dn->type == VFS_DIRECTORY) {
            const char *base = sabs;
            for (const char *q = sabs; *q; q++) if (*q == '/') base = q + 1;
            if (!*base) { vga_puts(" cp: bad src name\n"); return; }
            fs_join_path(dabs, base, final, sizeof(final));
        } else {
            int k = 0;
            while (dabs[k] && k < 191) { final[k] = dabs[k]; k++; }
            final[k] = 0;
        }

        int r = (sn->type == VFS_DIRECTORY)
              ? fs_cp_dir_vfs(sabs, final, 0)
              : fs_cp_file_vfs(sabs, final);
        if (r >= 0) vga_printf(" cp %s -> %s ok\n", sabs, final);
        else vga_printf(" cp %s -> %s FAIL\n", sabs, final);
    } else if (strncmp(args, "mv ", 3) == 0) {
        /* P4: 改走 VFS（vfs_rename 内部先落 ext2 再改内存树） */
        const char *p1 = args + 3;
        const char *p2 = p1;
        while (*p2 && *p2 != ' ') p2++;
        if (*p2 == 0) { vga_puts(" fs mv: need old and new\n"); return; }
        char on[128], nn[128];
        uint32_t ol = p2 - p1; if (ol > 127) ol = 127;
        memcpy(on, p1, ol); on[ol] = 0;
        const char *p3 = p2 + 1;
        while (*p3 == ' ') p3++;
        uint32_t nl = 0;
        while (p3[nl] && p3[nl] != ' ' && nl < 127) nl++;
        memcpy(nn, p3, nl); nn[nl] = 0;

        char oabs[192], nabs[192];
        fs_abs_path(on, oabs, sizeof(oabs));
        fs_abs_path(nn, nabs, sizeof(nabs));
        if (vfs_rename(oabs, nabs) == 0) vga_printf(" mv %s -> %s ok\n", oabs, nabs);
        else vga_printf(" mv %s -> %s FAIL\n", oabs, nabs);
    } else if (strncmp(args, "mkdir ", 6) == 0) {
        /* P3: 改走 VFS（vfs_mkdir 内部先落 ext2 再建内存节点） */
        char abs[192];
        fs_abs_path(args + 6, abs, sizeof(abs));
        if (vfs_mkdir(abs) == 0) vga_printf(" mkdir %s ok\n", abs);
        else vga_printf(" mkdir %s FAIL\n", abs);
    } else if (strncmp(args, "lsdir ", 6) == 0) {
        extern int ide_ext2_ls_dir(const char *name);
        const char *name = args + 6;
        static char dname[128];
        uint32_t k = 0;
        while (name[k] && name[k] != ' ' && k < 127) { dname[k] = name[k]; k++; }
        dname[k] = 0;
        ide_ext2_ls_dir(dname);
    } else if (strncmp(args, "fsck", 4) == 0) {
        /* P5: 故意保留直调。
         * fsck 必须在 VFS 之上工作 —— 它要修的就是 VFS 依赖的底层结构
         * (inode 位图/块位图/目录项/链接计数)。此时内存树可能已被损坏的
         * 磁盘内容污染, 经 VFS 读取等于用坏数据校验坏数据。 */
        extern int ide_ext2_fsck(void);
        extern int ide_ext2_fsck_fix(void);
        /* V3.89-E24: fs fsck -f (或 fix) 进入修复模式(清理孤儿块/inode) */
        const char *p = args + 4;
        while (*p == ' ') p++;
        if ((*p == '-' && *(p + 1) == 'f') || strncmp(p, "fix", 3) == 0)
            ide_ext2_fsck_fix();
        else ide_ext2_fsck();
    } else if (strncmp(args, "sync", 4) == 0) {
        extern int ide_ext2_sync(void);
        if (ide_ext2_sync() == 0) vga_puts(" fs synced to IDE disk\n");
        else vga_puts(" fs sync FAIL (not mounted?)\n");
    } else {
        vga_printf(" unknown fs subcmd: %s\n", args);
    }
}

/* V3.82: 软重启——图形模式(0x13/VBE)写像素会污染 VGA 字体平面(plane 2),
 * QEMU std VGA 下无法从内核恢复字体(SeaBIOS 需从 ROM 重载), 切回文本必花屏.
 * 图形演示/界面退出后重启, 由 SeaBIOS 重新加载干净字体.
 * (真机若支持 plane 2 写入, vga_gfx_text_mode 内嵌字体恢复仍会先行尝试) */
void sys_reboot_after_gfx(void)
{
    extern void vga_clear(void);
    vga_gfx_text_mode();
    vga_clear();
    vga_puts("\n [GFX] text-mode font plane corrupted by graphics mode, rebooting to reload font...\n");
    __asm__ volatile ("cli");
    /* V3.82: 清引导器重入魔数(0x10000 内核Multiboot头)——软重启保留内存,
     * 不清则引导器误判"内核已加载"跳过读盘, 跳入被污染内核卡死 */
    *(volatile uint16_t *)0x10000 = 0;
    for (volatile int i = 0; i < 5000000; i++);  /* 让用户看到提示 ~0.15s */
    /* 优先 0xCF9 硬复位(PIIX复位控制): QEMU 下 8042软复位(0x64/0xFE)后
     * IDE 状态异常→引导器INT13无限重试卡死(实测); 0xCF9 full-reset 更彻底.
     * 真机无 0xCF9 时回退 0x64/0xFE(键盘控制器复位) */
    __asm__ volatile ("outb %0, %1" : : "a"((uint8_t)0x02), "Nd"((uint16_t)0xCF9));
    __asm__ volatile ("outb %0, %1" : : "a"((uint8_t)0x06), "Nd"((uint16_t)0xCF9));
    for (volatile int i = 0; i < 2000000; i++);  /* ~60ms */
    __asm__ volatile ("outb %0, %1" : : "a"((uint8_t)0xFE), "Nd"((uint16_t)0x64));
    for (;;) { __asm__ volatile ("hlt"); }
}

void cmd_disk(void)
{
    vga_puts(" [DISK] IDE drives:\n");
    int found = 0;
    for (int i = 0; i < MAX_IDE_DRIVES; i++) {
        ide_drive_t d;
        if (ide_get_drive_info(i, &d) == 0) {
            found++;
            uint32_t mb = (uint32_t)((uint64_t)d.total_sectors * 512 / (1024 * 1024));
            vga_printf("  slot %d: %s (%d sectors, %d MB)\n",
                       i, d.model, d.total_sectors, mb);
        }
    }
    if (!found) { vga_puts("  no IDE drive present\n"); return; }

    /* 读扇区0(MBR) 验证 PIO 读 */
    uint8_t buf[512];
    if (ide_read_sectors(0, buf, 1) > 0) {
        vga_puts(" [DISK] read LBA0 OK, first bytes: ");
        for (int i = 0; i < 8; i++) vga_printf("%02x ", buf[i]);
        vga_putc('\n');
        show_partitions(buf);   /* V2.25: 解析MBR分区表 */
    } else {
        vga_puts(" [DISK] read LBA0 FAILED\n");
        return;
    }

    /* 写读回验证 PIO 写 (V3.82: 改最后扇区——LBA1 是 stage2, 写了破坏下次启动) */
    ide_drive_t td;
    uint32_t test_lba = 0;
    if (ide_get_drive_info(0, &td) == 0 && td.total_sectors > 1)
        test_lba = td.total_sectors - 1;
    uint8_t wbuf[512];
    for (int i = 0; i < 512; i++) wbuf[i] = (uint8_t)(i & 0xFF);
    if (ide_write_sectors(test_lba, wbuf, 1) > 0) {
        uint8_t rbuf[512];
        if (ide_read_sectors(test_lba, rbuf, 1) > 0) {
            int match = 1;
            for (int i = 0; i < 512; i++) if (wbuf[i] != rbuf[i]) { match = 0; break; }
            vga_printf(" [DISK] write+read-back last-sector: %s\n", match ? "PASS" : "FAIL");
        } else {
            vga_puts(" [DISK] read-back LBA1 FAILED\n");
        }
    } else {
        vga_puts(" [DISK] write LBA1 FAILED\n");
    }
}

