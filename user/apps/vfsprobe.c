/* ============================================================================
 * Hepu OS - vfsprobe（VFS64 伪设备后端探针，P3-6 验收）
 *
 * 验收点：新增只读伪设备只实现 open/read/close 三个 ops，不改任何 syscall
 * 代码。本探针经标准 open/read/close 系统call 打开 VFS 注册的两个节点：
 *
 *   /dev/vfsdemo   read 返回固定内容 "HEPU-VFS-DEMO\n"（14 字节）
 *   /dev/nullread  read 恒返回 EOF（0 字节）
 *
 * 写法对齐 statprobe.c：不用 printf（variadic 在 freestanding 下会 #PF），
 * 分段 write；全局/静态计数器不落 .bss，段保持 R E。
 * ========================================================================== */
#include "../ulib.h"

static void out(const char *s)
{
    while (*s) write(STDOUT_FILENO, s++, 1);
}

static void out_dec(int v)
{
    char b[12];
    int i = 0;
    if (v == 0) { write(STDOUT_FILENO, "0", 1); return; }
    while (v && i < 12) { b[i++] = (char)('0' + v % 10); v /= 10; }
    while (i > 0) write(STDOUT_FILENO, &b[--i], 1);
}

/* "HEPU-VFS-DEMO\n" 长度，与内核 vfs64_dev.c 的 DEMO_MSG 对齐。 */
#define DEMO_LEN 14

/* O_CREAT 字面量，与内核 fs_open_sys.c 的 FS_O_CREAT 对齐（0x40）。 */
#define VFS_O_CREAT 0x40

static void out_num(int v)
{
    out_dec(v);
}

int main(void)
{
    unsigned char buf[64];
    int fails = 0;

    out("[VFSP] vfs64 pseudo-device probe\n");

    /* ---- 1. 固定内容节点 ---- */
    int fd = open("/dev/vfsdemo", 0);
    if (fd < 0) {
        out("[VFSP] FAIL: open /dev/vfsdemo\n");
        return 1;
    }
    int n = read(fd, buf, sizeof(buf));
    out("[VFSP] vfsdemo read="); out_dec(n); out(" bytes: ");
    for (int i = 0; i < n; i++) write(STDOUT_FILENO, &buf[i], 1);
    out("\n");
    if (n != DEMO_LEN) fails++;
    close(fd);

    /* ---- 2. EOF 节点 ---- */
    fd = open("/dev/nullread", 0);
    if (fd < 0) {
        out("[VFSP] FAIL: open /dev/nullread\n");
        return 1;
    }
    n = read(fd, buf, sizeof(buf));
    out("[VFSP] nullread read="); out_dec(n); out(" bytes (expect 0)\n");
    if (n != 0) fails++;
    close(fd);

    /* ================= [VFSP4] S4 open 路由（含 O_CREAT） ================= */
    out("[VFSP4] open routing probe\n");

    /* 4.1 打开既有文件：fd 必须 ≥3（stdio 0/1/2 保留），读回 >0 字节。 */
    fd = open("/bin/hello.elf", 0);
    out("[VFSP4] open(/bin/hello.elf) fd="); out_num(fd); out("\n");
    if (fd < 3) fails++;
    if (fd >= 0) {
        n = read(fd, buf, sizeof(buf));
        out("[VFSP4] hello read="); out_num(n); out(" bytes\n");
        if (n <= 0) fails++;
        close(fd);
    }

    /* 4.2 O_CREAT 新建文件：写入再关闭，重开读回逐字节比对。 */
    const char *msg = "HEPU-VFS4-OK";
    int mlen = 12;
    /* 写模式 = flags&3 的 1；O_CREAT=0x40。只读打开会被 fs_do_write 拒写，
     * 与旧实现口径一致。 */
    fd = open("/vfs4_new.txt", 1 | VFS_O_CREAT);
    out("[VFSP4] open(O_CREAT|WR /vfs4_new.txt) fd="); out_num(fd); out("\n");
    if (fd < 3) fails++;
    if (fd >= 0) {
        int w = write(fd, msg, mlen);
        out("[VFSP4] write="); out_num(w); out(" bytes\n");
        if (w != mlen) fails++;
        close(fd);

        int fd2 = open("/vfs4_new.txt", 0);
        out("[VFSP4] reopen fd="); out_num(fd2); out("\n");
        if (fd2 < 3) fails++;
        if (fd2 >= 0) {
            unsigned char rb[16];
            int rn = read(fd2, rb, sizeof(rb));
            out("[VFSP4] read back="); out_num(rn); out(" bytes\n");
            if (rn != mlen) {
                fails++;
            } else {
                for (int i = 0; i < mlen; i++) {
                    if (rb[i] != (unsigned char)msg[i]) fails++;
                }
            }
            close(fd2);
        }
    }

    /* 4.3 打开不存在文件：对齐旧实现行为 —— 旧 open 不做存在性检查，
     * 仍分配 fd（不返回 ENOENT）。这里断言 fd≥3 是在锁定旧语义，
     * 不是 Linux 语义；后续补 ENOENT 时本断言需同步改。 */
    fd = open("/vfs4_missing.xyz", 0);
    out("[VFSP4] open(missing) fd="); out_num(fd); out(" (old impl: allocated, no ENOENT)\n");
    if (fd < 3) fails++;
    if (fd >= 0) close(fd);

    /* 4.4 close 后 fd 可复用：全部关闭后再开必须回到 3（无泄漏）。 */
    int fd3 = open("/bin/hello.elf", 0);
    out("[VFSP4] reuse open fd="); out_num(fd3); out(" (expect 3)\n");
    if (fd3 != 3) fails++;
    if (fd3 >= 0) close(fd3);

    /* ================= [VFSP5] S5 目录枚举（readdir/getdents） ================= */
    out("[VFSP5] directory enumerate probe\n");
    {
        unsigned char rdbuf[1024];
        int s5 = 0;
        /* readdir 打包格式：name\0 type\0，与终端/文件管理器旧口径一致。 */
        int rd = (int)syscall3(SYS_READDIR, (uint64_t)"/bin",
                              (uint64_t)rdbuf, sizeof(rdbuf));
        out("[VFSP5] readdir(/bin) bytes="); out_num(rd); out("\n");
        if (rd <= 0) {
            s5++;
        } else {
            int idx = 0; int found_hello = 0; int entries = 0; int hello_type_ok = 1;
            while (idx < rd) {
                char nm[256]; int nl = 0;
                while (idx < rd && rdbuf[idx]) { nm[nl++] = rdbuf[idx++]; }
                nm[nl] = 0;
                if (idx < rd && rdbuf[idx] == 0) idx++;   /* 跳过名字 NUL */
                char tp = '?';
                if (idx < rd) { tp = rdbuf[idx++]; }      /* 类型字节 */
                if (idx < rd && rdbuf[idx] == 0) idx++;   /* 跳过类型 NUL */
                entries++;
                if (strcmp(nm, "hello.elf") == 0) {
                    found_hello = 1;
                    if (tp != 'f') hello_type_ok = 0;
                }
            }
            out("[VFSP5] readdir entries="); out_num(entries);
            out(" hello.elf="); out(found_hello ? "found" : "MISSING");
            out(" type="); out(hello_type_ok ? "ok" : "BAD"); out("\n");
            if (!found_hello || !hello_type_ok) s5++;
        }

        /* getdents64：ino@0(8) off@8(8) reclen@16(2) type@18(1) name@19 */
        unsigned char gbuf[512];
        int gd = (int)syscall3(SYS_LNX64_GETDENTS, 0,
                               (uint64_t)gbuf, sizeof(gbuf));
        out("[VFSP5] getdents bytes="); out_num(gd); out("\n");
        if (gd <= 0) {
            s5++;
        } else {
            int goff = 0; int gent = 0;
            while (goff + 19 <= gd) {
                uint16_t reclen = (uint16_t)gbuf[goff+16] |
                                  ((uint16_t)gbuf[goff+17] << 8);
                if (reclen < 19) break;
                int nl = 0;
                while (goff + 19 + nl < gd && gbuf[goff+19+nl]) nl++;
                if (nl > 0) gent++;
                goff += reclen;
            }
            out("[VFSP5] getdents entries="); out_num(gent); out("\n");
            if (gent <= 0) s5++;
        }

        /* 不存在的目录：对齐旧实现，枚举失败返回非正数。 */
        int rm = (int)syscall3(SYS_READDIR, (uint64_t)"/no_such_dir_xyz",
                               (uint64_t)rdbuf, sizeof(rdbuf));
        out("[VFSP5] readdir(missing) rc="); out_num(rm);
        out(" (expect <=0)\n");
        if (rm > 0) s5++;

        out("[VFSP5] local fails="); out_num(s5); out("\n");
        fails += s5;
    }

    /* ================= [VFSP6] S6 目录变更（unlink/mkdir/rmdir） ================= */
    out("[VFSP6] directory mutation probe\n");
    {
        unsigned char sb[144];
        int s6 = 0;
        #define S6_IFMT  0170000
        #define S6_IFDIR 0040000
        #define S6_IFREG 0100000

        /* 6.1 mkdir 后 stat 可见且为目录。 */
        int m1 = (int)syscall2(SYS_MKDIR, (uint64_t)"/vfs6_dir", 0);
        out("[VFSP6] mkdir(/vfs6_dir) rc="); out_num(m1); out("\n");
        if (m1 != 0) s6++;
        else {
            int st1 = (int)syscall2(SYS_STAT, (uint64_t)"/vfs6_dir", (uint64_t)sb);
            uint32_t md1 = (uint32_t)sb[24] | ((uint32_t)sb[25] << 8) |
                           ((uint32_t)sb[26] << 16) | ((uint32_t)sb[27] << 24);
            out("[VFSP6] stat(/vfs6_dir) rc="); out_num(st1);
            out(" isdir="); out(((md1 & S6_IFMT) == S6_IFDIR) ? "yes" : "no"); out("\n");
            if (st1 != 0 || (md1 & S6_IFMT) != S6_IFDIR) s6++;
        }

        /* 6.2 新建普通文件，stat 可见且为普通文件。 */
        fd = open("/vfs6_file", 1 | VFS_O_CREAT);
        if (fd >= 0) { write(fd, "HEPU-VFS6!", 10); close(fd); }
        int st2 = (int)syscall2(SYS_STAT, (uint64_t)"/vfs6_file", (uint64_t)sb);
        uint32_t md2 = (uint32_t)sb[24] | ((uint32_t)sb[25] << 8) |
                       ((uint32_t)sb[26] << 16) | ((uint32_t)sb[27] << 24);
        out("[VFSP6] stat(/vfs6_file) rc="); out_num(st2);
        out(" isreg="); out(((md2 & S6_IFMT) == S6_IFREG) ? "yes" : "no"); out("\n");
        if (st2 != 0 || (md2 & S6_IFMT) != S6_IFREG) s6++;

        /* 6.3 unlink 文件后 stat 应失败（消失）。 */
        int u1 = (int)syscall2(SYS_UNLINK, (uint64_t)"/vfs6_file", 0);
        out("[VFSP6] unlink(/vfs6_file) rc="); out_num(u1); out("\n");
        if (u1 != 0) s6++;
        int st3 = (int)syscall2(SYS_STAT, (uint64_t)"/vfs6_file", (uint64_t)sb);
        out("[VFSP6] stat(after unlink) rc="); out_num(st3);
        out(" (expect !=0)\n");
        if (st3 == 0) s6++;

        /* 6.4 rmdir 空目录后 stat 应失败（消失）。 */
        int r1 = (int)syscall2(SYS_RMDIR, (uint64_t)"/vfs6_dir", 0);
        out("[VFSP6] rmdir(/vfs6_dir) rc="); out_num(r1); out("\n");
        if (r1 != 0) s6++;
        int st4 = (int)syscall2(SYS_STAT, (uint64_t)"/vfs6_dir", (uint64_t)sb);
        out("[VFSP6] stat(after rmdir) rc="); out_num(st4);
        out(" (expect !=0)\n");
        if (st4 == 0) s6++;

        /* 6.5 非空目录 rmdir：对齐 ext2 旧口径返回 -2；随后清理子项再删目录。 */
        int m2 = (int)syscall2(SYS_MKDIR, (uint64_t)"/vfs6_dir2", 0);
        out("[VFSP6] mkdir(/vfs6_dir2) rc="); out_num(m2); out("\n");
        if (m2 != 0) s6++;
        fd = open("/vfs6_dir2/child", 1 | VFS_O_CREAT);
        if (fd >= 0) { write(fd, "child", 5); close(fd); }
        int r2 = (int)syscall2(SYS_RMDIR, (uint64_t)"/vfs6_dir2", 0);
        out("[VFSP6] rmdir(nonempty) rc="); out_num(r2);
        out(" (expect -2)\n");
        if (r2 != -2) s6++;
        int u2 = (int)syscall2(SYS_UNLINK, (uint64_t)"/vfs6_dir2/child", 0);
        out("[VFSP6] unlink(child) rc="); out_num(u2); out("\n");
        if (u2 != 0) s6++;
        int r3 = (int)syscall2(SYS_RMDIR, (uint64_t)"/vfs6_dir2", 0);
        out("[VFSP6] rmdir(now empty) rc="); out_num(r3); out("\n");
        if (r3 != 0) s6++;

        /* 6.6 不存在目标：unlink/rmdir 均返回非正数（对齐旧实现）。 */
        int u3 = (int)syscall2(SYS_UNLINK, (uint64_t)"/vfs6_nope", 0);
        int r4 = (int)syscall2(SYS_RMDIR, (uint64_t)"/vfs6_nope", 0);
        out("[VFSP6] unlink(missing) rc="); out_num(u3);
        out(" rmdir(missing) rc="); out_num(r4); out(" (expect <=0)\n");
        if (u3 > 0 || r4 > 0) s6++;

        out("[VFSP6] local fails="); out_num(s6); out("\n");
        fails += s6;
    }

    out(fails ? "[VFSP] RESULT: FAIL\n" : "[VFSP] RESULT: PASS\n");
    return fails ? 1 : 0;
}

void _start(void) { exit(main()); }
