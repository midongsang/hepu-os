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

    out(fails ? "[VFSP] RESULT: FAIL\n" : "[VFSP] RESULT: PASS\n");
    return fails ? 1 : 0;
}

void _start(void) { exit(main()); }
