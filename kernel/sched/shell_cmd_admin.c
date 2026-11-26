/* ============================================================================
 * 赫普OS 32 位 - 系统管理命令（第二阶段补齐）
 *
 * 覆盖命令:
 *   chmod <octal-mode> <path>     数字八进制权限位（不支持符号模式）
 *   chown <uid[:gid]> <path>      属主/属组（inode i_uid/i_gid 落盘）
 *   chgrp <gid> <path>            属组（chown 的属组子集）
 *   mount [dev mp] / umount <mp>  最小运行时挂载表（见下方边界说明）
 *   fsck [--check-only|--fix]     复用既有 ext2 一致性检查（只读/修复）
 *   mkfs <dev>                    显式拒绝：内核内无格式化能力
 *   useradd / userdel             显式拒绝：单属主账号模型，无持久化多用户库
 *
 * 挂载边界（本轮如实标注，不伪造第二文件系统）:
 *   - 启动时建立的根挂载记录在表内，不可卸载。
 *   - 当前内核只有一个 ext2 上下文（data_lba 固定），不具备第二 ext2 实例
 *     的块缓存/超级块/GDT 独立空间；运行时把另一分区挂到目录的真实绑定
 *     本轮不落地，校验通过后返回明确的降级提示，挂载表不留半挂状态。
 *   - 非空目录判定基于 VFS 内存树 children；忙碌判定基于已打开 fd 路径前缀。
 *
 * 错误路径覆盖: 文件不存在、未挂载/只读后端、参数非法、重复挂载、
 * 设备不存在、非空/忙碌挂载点。
 * ========================================================================== */

#include <stdint.h>
#include <string.h>
#include "vga.h"
#include "vfs.h"
#include "ext2.h"
#include "ide_ext2.h"
#include "partition.h"
#include "shell_admin.h"

/* ---------------------------------------------------------------------------
 * 小工具：分词 / 八进制输出 / 十进制解析
 * ------------------------------------------------------------------------- */

static int admin_tok(char *args, const char *argv[], int max)
{
    int argc = 0;
    char *q = args ? args : (char *)"";
    while (*q && argc < max) {
        while (*q == ' ' || *q == '\t') q++;
        if (!*q) break;
        argv[argc++] = q;
        while (*q && *q != ' ' && *q != '\t') q++;
        if (*q) { *q = 0; q++; }
    }
    return argc;
}

static void admin_put_octal(uint32_t v)
{
    char buf[8];
    int n = 0;
    if (v == 0) { vga_puts("0"); return; }
    while (v && n < 7) { buf[n++] = (char)('0' + (v & 7)); v >>= 3; }
    while (n > 0) {
        char c[2] = { buf[--n], 0 };
        vga_puts(c);
    }
}

static int admin_dec(const char *s, uint32_t *out)
{
    if (!s || !*s) return -1;
    uint32_t v = 0;
    int neg = 0;
    if (*s == '-') { neg = 1; s++; if (!*s) return -1; }
    for (; *s; s++) {
        if (*s < '0' || *s > '9') return -1;
        v = v * 10 + (uint32_t)(*s - '0');
    }
    if (neg) { *out = (uint32_t)-1; return 0; }
    *out = v;
    return 0;
}

/* ---------------------------------------------------------------------------
 * 纯解析辅助（KUnit 覆盖）
 * ------------------------------------------------------------------------- */

int admin_parse_mode(const char *s, uint32_t *out_mode)
{
    if (!s || !out_mode) return 0;
    if (s[0] == '0' && s[1] == '\0') { *out_mode = 0; return 1; }
    const char *p = s;
    if (*p == '0') p++;                 /* 允许前导 0: 0755 */
    if (!*p) return 0;
    uint32_t v = 0;
    int digits = 0;
    while (*p) {
        if (*p < '0' || *p > '7') return 0;
        v = v * 8 + (uint32_t)(*p - '0');
        digits++;
        if (digits > 4 || v > 07777) return 0;
        p++;
    }
    *out_mode = v;
    return 1;
}

int admin_parse_owner(const char *spec,
                      uint32_t *uid, int *has_uid,
                      uint32_t *gid, int *has_gid)
{
    if (!spec || !*spec) return -1;
    const char *colon = strchr(spec, ':');
    char left[32], right[32];
    int ll = 0;

    if (!colon) {
        if (strlen(spec) >= sizeof(left)) return -1;
        strcpy(left, spec);
        right[0] = 0;
    } else {
        ll = (int)(colon - spec);
        if (ll >= (int)sizeof(left)) return -1;
        memcpy(left, spec, ll); left[ll] = 0;
        const char *r = colon + 1;
        if (strlen(r) >= sizeof(right)) return -1;
        strcpy(right, r);
    }

    *has_uid = 0; *has_gid = 0;
    *uid = 0; *gid = 0;

    if (left[0]) {
        if (admin_dec(left, uid) != 0) return -1;
        *has_uid = 1;
    }
    if (right[0]) {
        if (admin_dec(right, gid) != 0) return -1;
        *has_gid = 1;
    }
    return 0;
}

int admin_fsck_parse(const char *args, int *fix_mode)
{
    if (!fix_mode) return -1;
    *fix_mode = 0;
    if (!args || !*args) return 0;
    if (strcmp(args, "--fix") == 0 || strcmp(args, "-f") == 0) {
        *fix_mode = 1; return 0;
    }
    if (strcmp(args, "--check-only") == 0 || strcmp(args, "-c") == 0) {
        *fix_mode = 0; return 0;
    }
    return -1;
}

/* ---------------------------------------------------------------------------
 * 挂载表（最小运行时管理）
 * ------------------------------------------------------------------------- */

#define ADMIN_MAX_MOUNTS 8

typedef struct {
    char dev[32];
    char mp[VFS_MAX_PATH];
    int  boot;      /* 1 = 启动根挂载，不可卸载 */
} admin_mount_t;

static admin_mount_t admin_mnts[ADMIN_MAX_MOUNTS];
static int admin_mnts_ready = 0;

static void admin_mounts_init(void)
{
    if (admin_mnts_ready) return;
    admin_mnts_ready = 1;
    strncpy(admin_mnts[0].dev, ext2_mounted ? "ide0p1" : "none",
            sizeof(admin_mnts[0].dev) - 1);
    strncpy(admin_mnts[0].mp, "/", sizeof(admin_mnts[0].mp) - 1);
    admin_mnts[0].boot = 1;
}

/* 设备名解析：接受 ide0pN/hd0pN/partitionN/pN/N 与整盘别名 ide0/hd0/root。
 * 返回 1 设备存在；boot_dev 置 1 表示命中启动根设备。 */
static int admin_dev_exists(const char *dev, int *boot_dev)
{
    *boot_dev = 0;
    if (!dev || !*dev) return 0;

    /* 整盘/根别名：仅在数据盘已挂载时有效 */
    if (strcmp(dev, "ide0") == 0 || strcmp(dev, "hd0") == 0 ||
        strcmp(dev, "root") == 0 || strcmp(dev, "data") == 0) {
        if (ext2_mounted) { *boot_dev = 1; return 1; }
        return 0;
    }

    /* 分区别名：剥前缀取尾号 */
    const char *num = 0;
    if (strncmp(dev, "ide0p", 5) == 0) num = dev + 5;
    else if (strncmp(dev, "hd0p", 4) == 0) num = dev + 4;
    else if (strncmp(dev, "ata0p", 5) == 0) num = dev + 5;
    else if (strncmp(dev, "partition", 9) == 0) num = dev + 9;
    else if (dev[0] == 'p' && dev[1]) num = dev + 1;
    else if (dev[0] >= '0' && dev[0] <= '9') num = dev;

    int want = -1;
    if (num) {
        want = 0;
        for (const char *p = num; *p; p++) {
            if (*p < '0' || *p > '9') { want = -1; break; }
            want = want * 10 + (*p - '0');
        }
    }
    if (want <= 0) return 0;

    partition_info_t parts[MAX_PARTITIONS];
    int n = partition_list(parts, MAX_PARTITIONS);
    for (int i = 0; i < n; i++) {
        if (parts[i].valid && (i + 1) == want) return 1;
    }
    /* 无分区表但数据盘已挂载：根分区别名按启动设备处理 */
    if (ext2_mounted && want == 1) { *boot_dev = 1; return 1; }
    return 0;
}

static int admin_mnt_find_dev(const char *dev)
{
    for (int i = 0; i < ADMIN_MAX_MOUNTS; i++) {
        if (admin_mnts[i].mp[0] && strcmp(admin_mnts[i].dev, dev) == 0) return i;
    }
    return -1;
}

static int admin_mnt_find_mp(const char *mp)
{
    for (int i = 0; i < ADMIN_MAX_MOUNTS; i++) {
        if (admin_mnts[i].mp[0] && strcmp(admin_mnts[i].mp, mp) == 0) return i;
    }
    return -1;
}

void admin_mount_list(void)
{
    admin_mounts_init();
    vga_puts(" DEVICE       MOUNTPOINT  SOURCE\n");
    for (int i = 0; i < ADMIN_MAX_MOUNTS; i++) {
        if (!admin_mnts[i].mp[0]) continue;
        vga_puts("  ");
        vga_puts(admin_mnts[i].dev);
        vga_puts("  ");
        vga_puts(admin_mnts[i].mp);
        vga_puts(admin_mnts[i].boot ? "  boot\n" : "  runtime\n");
    }
}

int admin_mount_add(const char *dev, const char *mp, int *rc)
{
    admin_mounts_init();
    if (rc) *rc = 0;
    if (!dev || !*dev || !mp || !*mp) { if (rc) *rc = FS_EINVAL; return -1; }

    int boot_dev = 0;
    if (!admin_dev_exists(dev, &boot_dev)) {
        if (rc) *rc = FS_ENOENT;
        vga_printf(" mount: unknown device '%s'\n", dev);
        return -1;
    }
    if (admin_mnt_find_dev(dev) >= 0) {
        if (rc) *rc = FS_EEXIST;
        vga_printf(" mount: device '%s' already mounted\n", dev);
        return -1;
    }
    if (admin_mnt_find_mp(mp) >= 0) {
        if (rc) *rc = FS_EEXIST;
        vga_printf(" mount: '%s' is already a mountpoint\n", mp);
        return -1;
    }

    vfs_node_t *node = vfs_find(mp);
    if (!node) {
        if (rc) *rc = FS_ENOENT;
        vga_printf(" mount: mount point '%s' does not exist\n", mp);
        return -1;
    }
    if (node->type != VFS_DIRECTORY) {
        if (rc) *rc = FS_ENOTDIR;
        vga_printf(" mount: '%s' is not a directory\n", mp);
        return -1;
    }
    if (node->children) {
        if (rc) *rc = FS_ENOTEMPTY;
        vga_printf(" mount: '%s' is not empty\n", mp);
        return -1;
    }
    extern int vfs_path_busy(const char *path);
    if (vfs_path_busy(mp)) {
        if (rc) *rc = FS_EBUSY;
        vga_printf(" mount: '%s' is busy (open files)\n", mp);
        return -1;
    }

    /* 单 ext2 上下文：除启动根设备外的第二实例本轮不落地。
     * 所有校验已通过、表未改动，不存在半挂状态。 */
    if (!boot_dev) {
        if (rc) *rc = FS_EBUSY;
        vga_puts(" mount: second ext2 context not supported in this revision\n");
        vga_puts("        (only the boot data filesystem is live; deferred)\n");
        return -1;
    }

    int slot = -1;
    for (int i = 0; i < ADMIN_MAX_MOUNTS; i++) {
        if (!admin_mnts[i].mp[0]) { slot = i; break; }
    }
    if (slot < 0) { if (rc) *rc = FS_ENOMEM; return -1; }
    strncpy(admin_mnts[slot].dev, dev, sizeof(admin_mnts[slot].dev) - 1);
    strncpy(admin_mnts[slot].mp, mp, sizeof(admin_mnts[slot].mp) - 1);
    admin_mnts[slot].boot = 0;
    return 0;
}

int admin_mount_remove(const char *mp, int *rc)
{
    admin_mounts_init();
    if (rc) *rc = 0;
    if (!mp || !*mp) { if (rc) *rc = FS_EINVAL; return -1; }

    int i = admin_mnt_find_mp(mp);
    if (i < 0) {
        if (rc) *rc = FS_ENOENT;
        vga_printf(" umount: '%s' is not mounted\n", mp);
        return -1;
    }
    if (admin_mnts[i].boot) {
        if (rc) *rc = FS_EBUSY;
        vga_printf(" umount: '%s' is the boot root mount, busy\n", mp);
        return -1;
    }
    extern int vfs_path_busy(const char *path);
    if (vfs_path_busy(mp)) {
        if (rc) *rc = FS_EBUSY;
        vga_printf(" umount: '%s' is busy (open files)\n", mp);
        return -1;
    }
    admin_mnts[i].mp[0] = 0;
    admin_mnts[i].dev[0] = 0;
    admin_mnts[i].boot = 0;
    return 0;
}

/* ---------------------------------------------------------------------------
 * inode 属性落盘辅助
 * ------------------------------------------------------------------------- */

static void admin_persist(uint32_t ino,
                          int set_mode, uint32_t mode,
                          int set_uid, uint32_t uid,
                          int set_gid, uint32_t gid)
{
    if (!ext2_mounted || ino == 0) return;   /* 内存态节点：不落盘 */
    ext2_inode_t in;
    if (ext2_read_inode(ino, &in) != 0) {
        vga_puts(" warning: disk inode unavailable, change kept in memory only\n");
        return;
    }
    if (set_mode) in.i_mode = (uint16_t)((in.i_mode & 0xF000) | (mode & 07777));
    if (set_uid)  in.i_uid  = (uint16_t)uid;
    if (set_gid)  in.i_gid  = (uint16_t)gid;
    if (ext2_write_inode(ino, &in) != 0) {
        vga_puts(" warning: inode write-back failed, change kept in memory only\n");
        return;
    }
    ide_ext2_sync();
}

/* ---------------------------------------------------------------------------
 * 各命令实现
 * ------------------------------------------------------------------------- */

static void cmd_admin_chmod(char *args)
{
    const char *argv[4];
    int argc = admin_tok(args, argv, 4);
    if (argc < 2) {
        vga_puts(" usage: chmod <octal-mode> <file>   (e.g. chmod 755 /a.txt)\n");
        return;
    }
    uint32_t mode;
    if (!admin_parse_mode(argv[0], &mode)) {
        vga_printf(" chmod: invalid mode '%s' (octal digits 0-7, max 07777)\n", argv[0]);
        return;
    }
    const char *path = argv[1];
    vfs_stat_t st;
    if (vfs_stat(path, &st) != 0) {
        vga_printf(" chmod: cannot access '%s': No such file or directory\n", path);
        return;
    }
    int rc = vfs_chmod(path, mode);
    if (rc == FS_EACCES) {
        vga_puts(" chmod: Permission denied (owner or root only)\n");
        return;
    }
    if (rc != 0) {
        vga_printf(" chmod: failed (rc=%d)\n", rc);
        return;
    }
    admin_persist(st.inode, 1, mode, 0, 0, 0, 0);
    vga_puts(" chmod: '"); vga_puts(path); vga_puts("' mode ");
    admin_put_octal(mode); vga_puts("\n");
}

static void cmd_admin_chown(char *args)
{
    const char *argv[4];
    int argc = admin_tok(args, argv, 4);
    if (argc < 2) {
        vga_puts(" usage: chown <uid[:gid]> <file>\n");
        return;
    }
    uint32_t uid, gid; int has_uid, has_gid;
    if (admin_parse_owner(argv[0], &uid, &has_uid, &gid, &has_gid) != 0) {
        vga_printf(" chown: invalid owner spec '%s' (decimal uid[:gid])\n", argv[0]);
        return;
    }
    const char *path = argv[1];
    vfs_stat_t st;
    if (vfs_stat(path, &st) != 0) {
        vga_printf(" chown: cannot access '%s': No such file or directory\n", path);
        return;
    }
    uint32_t ouid = has_uid ? uid : (uint32_t)-1;
    uint32_t ogid = has_gid ? gid : (uint32_t)-1;
    int rc = vfs_chown(path, ouid, ogid);
    if (rc == FS_EACCES) {
        vga_puts(" chown: Permission denied (root only)\n");
        return;
    }
    if (rc != 0) {
        vga_printf(" chown: failed (rc=%d)\n", rc);
        return;
    }
    admin_persist(st.inode, 0, 0, has_uid, uid, has_gid, gid);
    vga_puts(" chown: '"); vga_puts(path); vga_puts("' updated\n");
}

static void cmd_admin_chgrp(char *args)
{
    const char *argv[4];
    int argc = admin_tok(args, argv, 4);
    if (argc < 2) {
        vga_puts(" usage: chgrp <gid> <file>\n");
        return;
    }
    uint32_t gid;
    if (admin_dec(argv[0], &gid) != 0) {
        vga_printf(" chgrp: invalid gid '%s' (decimal)\n", argv[0]);
        return;
    }
    const char *path = argv[1];
    vfs_stat_t st;
    if (vfs_stat(path, &st) != 0) {
        vga_printf(" chgrp: cannot access '%s': No such file or directory\n", path);
        return;
    }
    int rc = vfs_chown(path, (uint32_t)-1, gid);
    if (rc == FS_EACCES) {
        vga_puts(" chgrp: Permission denied (root only)\n");
        return;
    }
    if (rc != 0) {
        vga_printf(" chgrp: failed (rc=%d)\n", rc);
        return;
    }
    admin_persist(st.inode, 0, 0, 0, 0, 1, gid);
    vga_puts(" chgrp: '"); vga_puts(path); vga_puts("' group updated\n");
}

static void cmd_admin_mount(char *args)
{
    const char *argv[4];
    int argc = admin_tok(args, argv, 4);
    if (argc == 0) { admin_mount_list(); return; }
    if (argc < 2) {
        vga_puts(" usage: mount | mount <device> <directory>\n");
        return;
    }
    int rc = 0;
    admin_mount_add(argv[0], argv[1], &rc);
}

static void cmd_admin_umount(char *args)
{
    const char *argv[4];
    int argc = admin_tok(args, argv, 4);
    if (argc < 1) {
        vga_puts(" usage: umount <directory>\n");
        return;
    }
    int rc = 0;
    admin_mount_remove(argv[0], &rc);
}

static void cmd_admin_fsck(char *args)
{
    int fix = 0;
    if (admin_fsck_parse(args, &fix) != 0) {
        vga_puts(" usage: fsck [--check-only|-c] | [--fix|-f]\n");
        return;
    }
    if (!ext2_mounted) {
        if (ide_ext2_mount() != 0) {
            vga_puts(" fsck: no ext2 disk available (skipped)\n");
            return;
        }
    }
    int rc = fix ? ide_ext2_fsck_fix() : ide_ext2_fsck();
    vga_puts(" [FSCK] RESULT: ");
    if (rc == 0)      vga_puts("CLEAN\n");
    else if (rc > 0)  vga_puts("ISSUES\n");
    else              vga_puts("ERROR\n");
}

static void cmd_admin_mkfs(char *args)
{
    (void)args;
    vga_puts(" mkfs: in-kernel ext2 formatter is not implemented.\n");
    vga_puts("  The live filesystem cannot be reformatted in place; create/format\n");
    vga_puts("  images on the host with mke2fs and attach them via IDE.\n");
}

static void cmd_admin_useradd(char *args)
{
    (void)args;
    vga_puts(" useradd: rejected - single-owner account model (/account.conf).\n");
    vga_puts("  There is no persisted local multi-user account database (no\n");
    vga_puts("  /etc/passwd, no group model). The in-memory privacy-space registry\n");
    vga_puts("  is not a login account and is not persisted to ext2.\n");
}

static void cmd_admin_userdel(char *args)
{
    (void)args;
    vga_puts(" userdel: rejected - single-owner account model (/account.conf).\n");
    vga_puts("  No persisted local multi-user account database exists; see useradd.\n");
}

/* ---------------------------------------------------------------------------
 * 分派入口
 * ------------------------------------------------------------------------- */

int shell_admin_dispatch(const char *cmd, char *args)
{
    if (strcmp(cmd, "chmod") == 0)      { cmd_admin_chmod(args);   return 1; }
    if (strcmp(cmd, "chown") == 0)      { cmd_admin_chown(args);   return 1; }
    if (strcmp(cmd, "chgrp") == 0)      { cmd_admin_chgrp(args);   return 1; }
    if (strcmp(cmd, "mount") == 0)      { cmd_admin_mount(args);    return 1; }
    if (strcmp(cmd, "umount") == 0)     { cmd_admin_umount(args);   return 1; }
    if (strcmp(cmd, "fsck") == 0)       { cmd_admin_fsck(args);     return 1; }
    if (strcmp(cmd, "mkfs") == 0)       { cmd_admin_mkfs(args);     return 1; }
    if (strcmp(cmd, "useradd") == 0)    { cmd_admin_useradd(args);  return 1; }
    if (strcmp(cmd, "userdel") == 0)    { cmd_admin_userdel(args); return 1; }
    return 0;
}
