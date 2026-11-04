/* ============================================================================
 * 赫普OS - Shell：文件系统/磁盘/图形命令
 * 从 shell.c 拆分（V6.46 代码重构，大文件拆分目标<500行）
 * 包含：show_partitions/fs_cp_recursive/fs_rm_recursive/cmd_fs/
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
static int fs_cp_recursive(const char *dirpath, const char *name,
                           const char *dstbase, int depth)
{
    extern int ide_ext2_read_file(const char *, const char *, char *, int);
    extern int ide_ext2_write_raw(const char *, const char *, const char *, int);
    extern int ide_ext2_list_entries(const char *, fm_entry_t *, int);
    extern int ide_ext2_mkdir(const char *);
    if (depth > 4) return -1;

    /* 找源条目 (列目录匹配) */
    fm_entry_t ents[FM_MAX_ENTRIES];
    int cnt = ide_ext2_list_entries(dirpath, ents, FM_MAX_ENTRIES);
    if (cnt <= 0) return -1;
    int idx = -1;
    for (int i = 0; i < cnt; i++) {
        if (strcmp(ents[i].name, name) == 0) { idx = i; break; }
    }
    if (idx < 0) { vga_printf(" cp: %s not found in %s\n", name, dirpath); return -1; }

    /* V4.44-E89 语义: dstbase 即完整目标路径——
     * 文件: dstbase=目标文件; 目录: dstbase=目标目录(副本根, 内容直接放其下) */
    const char *dstpath = dstbase;

    if (ents[idx].is_dir) {
        /* 目录: 建目标目录(副本根) + 递归复制其内容 */
        extern int ide_ext2_mkdir(const char *);
        if (ide_ext2_mkdir(dstpath) != 0) { /* 已存在则忽略 */ }
        /* 源子目录路径 = dirpath + "/" + name */
        static char srcdir[160];
        int sn = 0;
        const char *sd = dirpath;
        while (*sd && sn < 150) srcdir[sn++] = *sd++;
        if (sn > 0 && srcdir[sn - 1] != '/') srcdir[sn++] = '/';
        for (int i = 0; ents[idx].name[i] && sn < 150; i++) srcdir[sn++] = ents[idx].name[i];
        srcdir[sn] = 0;
        fm_entry_t sub[FM_MAX_ENTRIES];
        int sc = ide_ext2_list_entries(srcdir, sub, FM_MAX_ENTRIES);
        if (sc <= 0) return 0;
        int ok = 0;
        for (int i = 0; i < sc; i++) {
            if (strcmp(sub[i].name, ".") == 0 || strcmp(sub[i].name, "..") == 0)
                continue;
            /* 子项目标 = 目标目录 + "/" + 子项名 (保留原名) */
            static char subdst[160];
            int tn = 0;
            const char *td = dstpath;
            while (*td && tn < 140) subdst[tn++] = *td++;
            if (tn > 0 && subdst[tn - 1] != '/') subdst[tn++] = '/';
            for (int j = 0; sub[i].name[j] && tn < 150; j++) subdst[tn++] = sub[i].name[j];
            subdst[tn] = 0;
            if (fs_cp_recursive(srcdir, sub[i].name, subdst, depth + 1) == 0)
                ok++;
        }
        vga_printf(" cp: dir %s -> %s (%d items)\n", name, dstpath, ok);
        return 0;
    }

    /* 文件: 读全量 (按 size 分配) + 写 */
    uint32_t sz = ents[idx].size;
    if (sz == 0) sz = 1;
    /* V4.47-E92: +16 余量, 防 ext2 读路径异常多写 (KHEAP bad magic 防御) */
    char *buf = (char *)kmalloc(sz + 16);
    if (!buf) { vga_puts(" cp: OOM\n"); return -1; }
    int rd = ide_ext2_read_file(dirpath, name, buf, (int)sz + 1);
    if (rd < 0) { kfree(buf); vga_printf(" cp: read %s FAIL\n", name); return -1; }
    /* 拆分目标完整路径为 目录 + 文件名 (write_raw 按 dirpath/name 拼接) */
    static char ddir[160], dname[128];
    strncpy(ddir, dstpath, 159);
    ddir[159] = 0;
    char *slash = NULL;
    for (char *q = ddir; *q; q++) if (*q == '/') slash = q;
    if (!slash) {
        /* V4.44-E89: 目标无目录分隔 → 根目录下的文件 (fs cp abc abccopy2) */
        ddir[0] = '/'; ddir[1] = 0;
        strncpy(dname, dstpath, 127);
        dname[127] = 0;
    } else {
        *slash = 0;
        strncpy(dname, slash + 1, 127);
        dname[127] = 0;
    }
    int w = ide_ext2_write_raw(ddir, dname, buf, rd);
    if (w >= 0) {
        vga_printf(" cp: %s -> %s (%d bytes)\n", name, dstpath, rd);
        kfree(buf);
        return 0;
    }
    kfree(buf);
    vga_printf(" cp: write %s FAIL\n", dstpath);
    return -1;
}

/* V4.45-E90: 递归删除 helper——目录先删子项再删自身 */
static int fs_rm_recursive(const char *dirpath, const char *name)
{
    extern int ide_ext2_list_entries(const char *, fm_entry_t *, int);
    extern int ext2_path_unlink_v2(const char *);
    fm_entry_t ents[FM_MAX_ENTRIES];
    int cnt = ide_ext2_list_entries(dirpath, ents, FM_MAX_ENTRIES);
    if (cnt <= 0) return -1;
    int idx = -1;
    for (int i = 0; i < cnt; i++) {
        if (strcmp(ents[i].name, name) == 0) { idx = i; break; }
    }
    if (idx < 0) { vga_printf(" rm: %s not found in %s\n", name, dirpath); return -1; }

    /* 完整路径 = dirpath + "/" + name (兼容无前导斜杠) */
    static char full[160];
    int n = 0;
    if (dirpath[0] != '/') full[n++] = '/';
    const char *d = dirpath;
    while (*d && n < 150) full[n++] = *d++;
    if (n > 0 && full[n - 1] != '/') full[n++] = '/';
    for (int i = 0; ents[idx].name[i] && n < 150; i++) full[n++] = ents[idx].name[i];
    full[n] = 0;

    if (ents[idx].is_dir) {
        /* 递归删子项 */
        fm_entry_t sub[FM_MAX_ENTRIES];
        int sc = ide_ext2_list_entries(full, sub, FM_MAX_ENTRIES);
        if (sc > 0) {
            for (int i = 0; i < sc; i++) {
                if (strcmp(sub[i].name, ".") == 0 || strcmp(sub[i].name, "..") == 0)
                    continue;
                fs_rm_recursive(full, sub[i].name);
            }
        }
        /* V4.46-E91: 修复——static full 被递归子调用覆盖(unlink 用了子项路径)。
         * unlink 前用 dirpath/name 重建父目录完整路径。 */
        extern int ide_ext2_sync(void);
        ide_ext2_sync();
        n = 0;
        if (dirpath[0] != '/') full[n++] = '/';
        d = dirpath;
        while (*d && n < 150) full[n++] = *d++;
        if (n > 0 && full[n - 1] != '/') full[n++] = '/';
        for (int i = 0; ents[idx].name[i] && n < 150; i++) full[n++] = ents[idx].name[i];
        full[n] = 0;
        if (ext2_path_unlink_v2(full) == 0) {
            vga_printf(" rm: dir %s removed\n", name);
            return 0;
        }
        if (ext2_path_unlink_v2(full) == 0) {
            vga_printf(" rm: dir %s removed (retry)\n", name);
            return 0;
        }
        vga_printf(" rm: dir %s FAIL\n", name);
        return -1;
    }
    if (ext2_path_unlink_v2(full) == 0) {
        vga_printf(" rm: %s removed\n", name);
        return 0;
    }
    vga_printf(" rm: %s FAIL\n", name);
    return -1;
}

void cmd_fs(char *args)
{
    if (!args || args[0] == 0) {
        vga_puts(" fs usage: fs mount | ls | cat <name> | write <name> <text> | stat <name> | df | sync | mkdir | rm | cp <src> <dst> | cpall <dst> | mv | fsck\n");
        return;
    }
    if (strncmp(args, "mount", 5) == 0) {
        extern int ide_ext2_mount(void);
        if (ide_ext2_mount() == 0) vga_puts(" ext2 fs mounted (IDE disk)\n");
        else vga_puts(" mount FAIL\n");
    } else if (strncmp(args, "ls", 2) == 0) {
        extern void ide_ext2_list_dir(void);
        ide_ext2_list_dir();
    } else if (strncmp(args, "cat ", 4) == 0) {
        extern int ide_ext2_cat(const char *name);
        const char *name = args + 4;
        if (ide_ext2_cat(name) != 0) vga_printf(" cat %s FAIL\n", name);
    } else if (strncmp(args, "write ", 6) == 0) {
        extern int ide_ext2_writefile(const char *name, const char *data);
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
        int w = ide_ext2_writefile(nm, data);
        if (w >= 0) vga_printf(" wrote %d bytes to /%s\n", w, nm);
        else vga_printf(" write /%s FAIL\n", nm);
    } else if (strncmp(args, "stat ", 5) == 0) {
        extern int ide_ext2_stat(const char *name);
        const char *name = args + 5;
        if (ide_ext2_stat(name) != 0) vga_printf(" stat %s FAIL\n", name);
    } else if (strcmp(args, "df") == 0) {
        extern int ide_ext2_df(void);
        if (ide_ext2_df() != 0) vga_puts(" df FAIL\n");
    } else if (strcmp(args, "sync") == 0) {
        extern int ide_ext2_sync(void);
        if (ide_ext2_sync() == 0) vga_puts(" fs synced\n");
        else vga_puts(" sync FAIL\n");
    } else if (strncmp(args, "rm ", 3) == 0) {
        /* V4.45-E90: fs rm <name>——自动识别文件/目录递归删除 */
        const char *name = args + 3;
        static char rmname[128];
        uint32_t k = 0;
        while (name[k] && name[k] != ' ' && k < 127) { rmname[k] = name[k]; k++; }
        rmname[k] = 0;
        fs_rm_recursive("/", rmname);
    } else if (strncmp(args, "cpall ", 6) == 0) {
        /* V4.44-E89: fs cpall <dst>——批量复制根目录全部条目 */
        extern int ide_ext2_list_entries(const char *, fm_entry_t *, int);
        const char *dst = args + 6;
        while (*dst == ' ') dst++;
        if (*dst == 0) { vga_puts(" fs cpall <dst>\n"); return; }
        /* V4.44-E89 修复: 目标根目录必须先建 (write_raw 依赖目录存在) */
        extern int ide_ext2_mkdir(const char *);
        ide_ext2_mkdir(dst);
        fm_entry_t ents[FM_MAX_ENTRIES];
        int cnt = ide_ext2_list_entries("/", ents, FM_MAX_ENTRIES);
        if (cnt <= 0) { vga_puts(" cpall: empty root\n"); return; }
        int ok = 0;
        for (int i = 0; i < cnt; i++) {
            if (strcmp(ents[i].name, ".") == 0 || strcmp(ents[i].name, "..") == 0)
                continue;
            /* V4.44-E89: 跳过目标自身, 防自我嵌套 (backupall2 -> backupall2/backupall2) */
            if (strcmp(ents[i].name, dst) == 0) continue;
            /* 子项目标 = dst + "/" + 条目名 (保留原名) */
            static char cdst[160];
            int cn = 0;
            const char *cd = dst;
            while (*cd && cn < 140) cdst[cn++] = *cd++;
            if (cn > 0 && cdst[cn - 1] != '/') cdst[cn++] = '/';
            for (int j = 0; ents[i].name[j] && cn < 150; j++) cdst[cn++] = ents[i].name[j];
            cdst[cn] = 0;
            if (fs_cp_recursive("/", ents[i].name, cdst, 0) == 0) ok++;
        }
        vga_printf(" cpall: %d items copied to %s\n", ok, dst);
    } else if (strncmp(args, "cp ", 3) == 0) {
        /* V4.44-E89: fs cp <src> <dst>——自动识别文件/目录递归复制 (大文件整量) */
        const char *p1 = args + 3;
        const char *p2 = p1;
        while (*p2 && *p2 != ' ') p2++;
        if (*p2 == 0) { vga_puts(" fs cp: need src and dst\n"); return; }
        static char cpsrc[128], cpdst[128];
        uint32_t n1 = p2 - p1;
        if (n1 > 127) n1 = 127;
        memcpy(cpsrc, p1, n1);
        cpsrc[n1] = 0;
        const char *p3 = p2 + 1;
        while (*p3 == ' ') p3++;
        uint32_t n2 = 0;
        while (p3[n2] && p3[n2] != ' ' && n2 < 127) n2++;
        memcpy(cpdst, p3, n2);
        cpdst[n2] = 0;
        /* V4.47-E92: src 支持子目录路径 (data/backup -> parent=/data name=backup) */
        const char *srcname = cpsrc;
        static char srcparent[128];
        const char *slash = NULL;
        for (const char *q = cpsrc; *q; q++) if (*q == '/') slash = q;
        if (slash && slash != cpsrc) {
            uint32_t pn = (uint32_t)(slash - cpsrc);
            if (pn > 126) pn = 126;
            int k = 0;
            if (cpsrc[0] != '/') srcparent[k++] = '/';
            for (uint32_t i = 0; i < pn; i++) srcparent[k++] = cpsrc[i];
            srcparent[k] = 0;
            srcname = slash + 1;
            fs_cp_recursive(srcparent, srcname, cpdst, 0);
        } else if (slash == cpsrc) {
            fs_cp_recursive("/", slash + 1, cpdst, 0);
        } else {
            fs_cp_recursive("/", cpsrc, cpdst, 0);
        }
    } else if (strncmp(args, "mv ", 3) == 0) {
        extern int ide_ext2_mv(const char *oldn, const char *newn);
        const char *p1 = args + 3;
        const char *p2 = p1;
        while (*p2 && *p2 != ' ') p2++;
        if (*p2 == 0) { vga_puts(" fs mv: need old and new name\n"); return; }
        static char oldn[128];
        uint32_t olen = p2 - p1;
        if (olen > 127) olen = 127;
        memcpy(oldn, p1, olen); oldn[olen] = 0;
        const char *p3 = p2 + 1;
        ide_ext2_mv(oldn, p3);
    } else if (strncmp(args, "mkdir ", 6) == 0) {
        extern int ide_ext2_mkdir(const char *name);
        const char *name = args + 6;
        static char dname[128];
        uint32_t k = 0;
        while (name[k] && name[k] != ' ' && k < 127) { dname[k] = name[k]; k++; }
        dname[k] = 0;
        ide_ext2_mkdir(dname);
    } else if (strncmp(args, "lsdir ", 6) == 0) {
        extern int ide_ext2_ls_dir(const char *name);
        const char *name = args + 6;
        static char dname[128];
        uint32_t k = 0;
        while (name[k] && name[k] != ' ' && k < 127) { dname[k] = name[k]; k++; }
        dname[k] = 0;
        ide_ext2_ls_dir(dname);
    } else if (strncmp(args, "fsck", 4) == 0) {
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

