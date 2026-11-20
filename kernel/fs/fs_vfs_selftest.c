/* fs_vfs_selftest.c — shell FS→VFS 改造的回归自测（P1/P2/P3 落盘验证）
 *
 * 为什么要有这个东西：
 *   P1/P2 把 shell 的 ls/stat/cat/write 改走 VFS，P3 之前发现 vfs_mkdir/rmdir/unlink
 *   只改内存树不写盘（1c229ca）。9b55b06 补了 ext2 后端，但"重启后还在吗"这件事
 *   一直没验成——环境里 QEMU 反复 SIGKILL。
 *
 * 验法（关键）：不用 system_reset（无头 QEMU 下不可靠），改成
 *   同一张盘 + 两次独立 QEMU 进程。第一次跑写阶段，进程退出；第二次跑读阶段，
 *   看第一次写的内容还在不在。这才是真正的跨重启证据。
 *
 * 阶段由根分区 /vfs_selftest 标记文件的首字节决定：
 *   'w' → 写阶段      'r' → 读阶段（同一张盘第二次启动）
 */

#include "vfs.h"
#include "ext2.h"

extern void serial_puts(uint16_t port, const char *s);

/* 32 位没有 serial_hex / sprintf，自己打数字 */
static void vs_put_dec(uint16_t port, int v)
{
    char b[12];
    int i = 0, neg = 0;
    if (v < 0) { neg = 1; v = -v; }
    if (v == 0) { b[i++] = '0'; }
    while (v > 0) { b[i++] = (char)('0' + (v % 10)); v /= 10; }
    if (neg) b[i++] = '-';
    for (int j = i - 1; j >= 0; j--) {
        char c[2]; c[0] = b[j]; c[1] = 0;
        serial_puts(port, c);
    }
}


static void vs_put_hex(uint16_t port, unsigned v)
{
    char b[12]; int i = 0;
    if (v == 0) { b[i++] = '0'; }
    while (v > 0) { b[i++] = "0123456789ABCDEF"[v & 15]; v >>= 4; }
    serial_puts(port, "0x");
    for (int j = i - 1; j >= 0; j--) { char c[2]; c[0] = b[j]; c[1] = 0; serial_puts(port, c); }
}

static void vs_line(uint16_t port, const char *tag, int no, int ok, const char *detail)
{
    serial_puts(port, "[VFS-TEST] ");
    serial_puts(port, tag);
    serial_puts(port, " ");
    vs_put_dec(port, no);
    serial_puts(port, "/6 ");
    serial_puts(port, ok ? "PASS" : "FAIL");
    if (detail && *detail) {
        serial_puts(port, "  ");
        serial_puts(port, detail);
    }
    serial_puts(port, "\n");
}

/* 32 位 vfs_open 不认 VFS_O_CREAT，新文件必须先落 ext2 建出来（P2 里同样的兜底） */
extern int ext2_path_create(const char *path, uint16_t mode);

#define VS_FILE   "/p2test.txt"
#define VS_DIR    "/p3test"
#define VS_TEXT   "HEPU_P2_OK"
#define VS_LEN    9

/* 返回 0 = 全过，>0 = 失败项数 */
int fs_vfs_selftest(char phase)
{
    uint16_t port = 0x3F8;
    int fails = 0;

    serial_puts(port, "[VFS-TEST] phase=");
    { char c[2]; c[0] = phase; c[1] = 0; serial_puts(port, c); }
    serial_puts(port, "\n");

    if (phase == 'w') {
        /* ---- 诊断：先把挂载状态和底层返回码打出来，别靠猜 ---- */
        {
            extern int ext2_mounted;
            extern int fs_mounted;
            extern unsigned int ext2_block_size;
            serial_puts(port, "[VFS-TEST] diag: ext2_mounted=");
            vs_put_dec(port, ext2_mounted);
            serial_puts(port, " fs_mounted=");
            vs_put_dec(port, fs_mounted);
            serial_puts(port, " blk=");
            vs_put_dec(port, (int)ext2_block_size);
            serial_puts(port, "\n");
        }
        /* ---- P1：读路径（vfs_readdir / vfs_find） ---- */
        vfs_node_t *root = vfs_find("/");
        int ok1 = (root && root->type == VFS_DIRECTORY);
        vs_line(port, "P1.root", 1, ok1, ok1 ? "" : "vfs_find(/) 不是目录");
        if (!ok1) fails++;

        vfs_dirent_t ents[16];
        int n = vfs_readdir("/", ents, 16);
        int ok2 = (n > 0 && ents[0].name[0] != 0);
        vs_line(port, "P1.readdir", 2, ok2, ok2 ? ents[0].name : "vfs_readdir 返回 0");
        if (!ok2) fails++;

        /* ---- P2：写路径（vfs_open O_CREAT → write → close → vfs_sync） ---- */
        int fd = vfs_open(VS_FILE, VFS_O_WRONLY | VFS_O_CREAT | VFS_O_TRUNC);
        int ok3 = (fd >= 0);
        vs_line(port, "P2.open", 3, ok3, ok3 ? "O_CREAT 建出并打开" : "vfs_open 失败");
        if (!ok3) fails++;

        int wr = -1;
        if (ok3) {
            wr = vfs_write(fd, VS_TEXT, VS_LEN);
            vfs_close(fd);
            vfs_sync();
        }
        int ok4 = (wr == VS_LEN);
        vs_line(port, "P2.write", 4, ok4, ok4 ? "(9 bytes, synced)" : "写入字节数不对");
        if (!ok4) fails++;

        /* 写完在同一个进程里立刻读回，确认不是只进了内存缓存 */
        char buf2[32];
        int rd = -1;
        int fd2 = vfs_open(VS_FILE, VFS_O_RDONLY);
        if (fd2 >= 0) {
            rd = vfs_read(fd2, buf2, 16);
            vfs_close(fd2);
        }
        int ok5 = (rd == VS_LEN);
        if (ok5) {
            for (int i = 0; i < VS_LEN; i++) if (buf2[i] != VS_TEXT[i]) { ok5 = 0; break; }
        }
        vs_line(port, "P2.readback", 5, ok5, ok5 ? "内容一致" : "读回不一致");
        if (!ok5) fails++;

        /* ---- P3：目录落盘（vfs_mkdir 接 ext2 后端） ---- */
        int mk = vfs_mkdir(VS_DIR);
        int ok6 = (mk == 0 && vfs_find(VS_DIR) != 0);
        vs_line(port, "P3.mkdir", 6, ok6, ok6 ? "created" : "vfs_mkdir 失败");
        if (!ok6) fails++;

        serial_puts(port, "[VFS-TEST] write phase done, fails=");
        vs_put_dec(port, fails);
        serial_puts(port, "\n");
        serial_puts(port, "[VFS-TEST] === NOW RESET AND RUN READ PHASE ON SAME DISK ===\n");
    } else {
        /* ---- 读阶段：验"落盘"，不验"内存树" ----
         * 关键认知：第二次启动时 VFS 内存树是全新的，ext2_mount 只填根目录一层，
         * 不会扫描磁盘上已有什么。所以 vfs_find("/p2test.txt") 必然返回 NULL ——
         * 那不能证明"没落盘"，只能证明"内存树没重建"。
         * 因此这里盘上的证据一律走 ext2 直查（ide_ext2_read_file / ext2_readdir_ino），
         * 内存树那条单独记成 INFO（已知缺口），不参与 PASS/FAIL。 */

        extern int ide_ext2_read_file(const char *, const char *, char *, int);
        extern int ext2_readdir_ino(uint32_t, vfs_dirent_t *, int);
        char buf[32];

        int rd = ide_ext2_read_file("/", "p2test.txt", buf, 16);
        int ok1 = (rd > 0);
        vs_line(port, "disk.p2_file", 1, ok1, ok1 ? "盘上存在（跨启动存活）" : "盘上没有");
        if (!ok1) fails++;

        int ok2 = 0;
        if (ok1 && rd >= VS_LEN) {
            ok2 = 1;
            for (int i = 0; i < VS_LEN; i++) if (buf[i] != VS_TEXT[i]) { ok2 = 0; break; }
        }
        vs_line(port, "disk.p2_content", 2, ok2, ok2 ? "HEPU_P2_OK" : "内容不匹配");
        if (!ok2) fails++;

        /* 枚举根目录（inode 1）找 p3test —— 纯 ext2，不碰内存树 */
        vfs_dirent_t ents[16];
        int n = ext2_readdir_ino(2, ents, 16);
        int found_dir = 0;
        for (int i = 0; i < n; i++) {
            const char *e = ents[i].name;
            const char *t = "p3test";
            int j = 0; while (e[j] && t[j] && e[j] == t[j]) j++;
            if (e[j] == 0 && t[j] == 0 && ents[i].type == VFS_DIRECTORY) { found_dir = 1; break; }
        }
        vs_line(port, "disk.p3_dir", 3, found_dir, found_dir ? "盘上存在（跨启动存活）" : "盘上没有");
        if (!found_dir) fails++;

        /* 内存树：如实记录缺口，不算失败 */
        {
            extern vfs_node_t *vfs_find(const char *);
            serial_puts(port, "[VFS-TEST] INFO tree.p2: vfs_find(\"/p2test.txt\")=");
            serial_puts(port, vfs_find(VS_FILE) ? "found" : "NULL");
            serial_puts(port, " (32位内存树不重建，属已知缺口)\n");
        }

        /* 删除也要落盘：直调 ext2 删，再枚举确认消失 */
        {
            extern int ext2_path_unlink_v2(const char *);
            int rm = ext2_path_unlink_v2(VS_DIR);
            int ok5 = (rm == 0);
            vs_line(port, "disk.rmdir", 5, ok5, ok5 ? "removed" : "ext2 删除失败");
            if (!ok5) fails++;

            int n2 = ext2_readdir_ino(2, ents, 16);
            int still = 0;
            for (int i = 0; i < n2; i++) {
                const char *e = ents[i].name;
                const char *t = "p3test";
                int j = 0; while (e[j] && t[j] && e[j] == t[j]) j++;
                if (e[j] == 0 && t[j] == 0) { still = 1; break; }
            }
            vs_line(port, "disk.rmdir_gone", 6, !still, still ? "删完还在" : "已消失");
            if (still) fails++;
        }

        serial_puts(port, "[VFS-TEST] read phase done, fails=");
        vs_put_dec(port, fails);
        serial_puts(port, "\n");
    }

    serial_puts(port, fails ? "[VFS-TEST] RESULT: FAIL\n" : "[VFS-TEST] RESULT: PASS\n");
    return fails;
}
