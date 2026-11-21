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


static int vs_total = 6;   /* 两个阶段都是 6 项 */

static void vs_line(uint16_t port, const char *tag, int no, int ok, const char *detail)
{
    serial_puts(port, "[VFS-TEST] ");
    serial_puts(port, tag);
    serial_puts(port, " ");
    vs_put_dec(port, no);
    serial_puts(port, "/");
    vs_put_dec(port, vs_total);
    serial_puts(port, " ");
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
        vs_total = 16;
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

        /* ---- shell 命令层：直接调 cmd_fs，绕开键盘输入 ----
         * 前面 6 项验的是 VFS 这一层；这里验的是 cmd_fs 自己的参数解析
         * 和路径拼接是否真的把活干成了 —— 判定一律看盘上结果，不看它打印什么。 */
        {
            extern void cmd_fs(char *);
            extern int ide_ext2_read_file(const char *, const char *, char *, int);
            char cmdbuf[96];
            char rb[64];

            /* shell.write */
            { char c[] = "write /p2shell.txt HELLO_SHELL"; int i;
              for (i = 0; c[i]; i++) { cmdbuf[i] = c[i]; } cmdbuf[i] = 0; }
            cmd_fs(cmdbuf);
            int rd = ide_ext2_read_file("/", "p2shell.txt", rb, 32);
            int okw = 0;
            if (rd > 0) {
                const char *exp = "HELLO_SHELL"; int i;
                okw = 1;
                for (i = 0; exp[i]; i++) if (rb[i] != exp[i]) { okw = 0; break; }
            }
            vs_line(port, "shell.write", 7, okw, okw ? "盘上内容正确" : "write 没落盘/内容不对");
            if (!okw) fails++;

            /* shell.mkdir */
            { char c[] = "mkdir /p3shell"; int i;
              for (i = 0; c[i]; i++) { cmdbuf[i] = c[i]; } cmdbuf[i] = 0; }
            cmd_fs(cmdbuf);
            vfs_dirent_t se[16];
            int sn = ext2_readdir_ino(2, se, 16);
            int okm = 0;
            for (int i = 0; i < sn; i++) {
                const char *e = se[i].name; const char *t = "p3shell"; int j = 0;
                while (e[j] && t[j] && e[j] == t[j]) j++;
                if (e[j] == 0 && t[j] == 0) { okm = 1; break; }
            }
            vs_line(port, "shell.mkdir", 8, okm, okm ? "盘上建出目录" : "mkdir 没落盘");
            if (!okm) fails++;

            /* shell.rm */
            { char c[] = "rm /p3shell"; int i;
              for (i = 0; c[i]; i++) { cmdbuf[i] = c[i]; } cmdbuf[i] = 0; }
            cmd_fs(cmdbuf);
            int sn2 = ext2_readdir_ino(2, se, 16);
            int still = 0;
            for (int i = 0; i < sn2; i++) {
                const char *e = se[i].name; const char *t = "p3shell"; int j = 0;
                while (e[j] && t[j] && e[j] == t[j]) j++;
                if (e[j] == 0 && t[j] == 0) { still = 1; break; }
            }
            vs_line(port, "shell.rm", 9, !still, still ? "删完还在" : "已消失");
            if (still) fails++;

            /* shell.ls（ls 是 void 输出到屏幕，只能验"跑完还活着、根节点还有效"） */
            { char c[] = "ls /"; int i;
              for (i = 0; c[i]; i++) { cmdbuf[i] = c[i]; } cmdbuf[i] = 0; }
            cmd_fs(cmdbuf);
            {
                extern vfs_node_t *vfs_find(const char *);
                int okl = (vfs_find("/") != 0);
                vs_line(port, "shell.ls", 10, okl, okl ? "跑完 VFS 树仍完整" : "ls 之后树坏了");
                if (!okl) fails++;
            }

            /* shell.cp */
            { char c[] = "cp /p2shell.txt /p2shell.copy"; int i;
              for (i = 0; c[i]; i++) { cmdbuf[i] = c[i]; } cmdbuf[i] = 0; }
            cmd_fs(cmdbuf);
            {
                int rd2 = ide_ext2_read_file("/", "p2shell.copy", rb, 32);
                int okc = 0;
                if (rd2 > 0) {
                    const char *exp = "HELLO_SHELL"; int i;
                    okc = 1;
                    for (i = 0; exp[i]; i++) if (rb[i] != exp[i]) { okc = 0; break; }
                }
                vs_line(port, "shell.cp", 11, okc, okc ? "副本内容正确" : "cp 没落盘/内容不对");
                if (!okc) fails++;
            }

            /* shell.mv */
            { char c[] = "mv /p2shell.copy /p2shell.moved"; int i;
              for (i = 0; c[i]; i++) { cmdbuf[i] = c[i]; } cmdbuf[i] = 0; }
            cmd_fs(cmdbuf);
            {
                int has_new = ide_ext2_read_file("/", "p2shell.moved", rb, 32);
                int has_old = ide_ext2_read_file("/", "p2shell.copy", rb, 32);
                int okv = (has_new > 0 && has_old <= 0);
                vs_line(port, "shell.mv", 12, okv, okv ? "旧名消失、新名存在" : "mv 没生效");
                if (!okv) fails++;
            }

            /* shell.cp_big：8KB 大文件 —— 验多块 read/write 循环和 EOF 边界。
             * 内容用 i&0xFF 递增模式，逐字节可校验，位置错位立刻能看出来。 */
            {
                static uint8_t big[8192];
                /* 8193：ide_ext2_read_file 是"文本读"，内部按 max-1 裁剪留 \0 */
                static uint8_t got[8193];
                for (int i = 0; i < 8192; i++) big[i] = (uint8_t)(i & 0xFF);

                int wfd = vfs_open("/bigsrc.bin", VFS_O_WRONLY | VFS_O_CREAT | VFS_O_TRUNC);
                int off = 0;
                if (wfd >= 0) {
                    for (int guard = 0; guard < 64 && off < 8192; guard++) {
                        int w = vfs_write(wfd, big + off, (uint32_t)(8192 - off));
                        if (w <= 0) break;
                        off += w;
                    }
                    vfs_close(wfd);
                    vfs_sync();
                }
                int oksrc = (off == 8192);
                if (!oksrc) {
                    serial_puts(port, "[VFS-TEST] bigsrc write only ");
                    vs_put_dec(port, off);
                    serial_puts(port, " bytes\n");
                }

                { char c[] = "cp /bigsrc.bin /bigdst.bin"; int i;
                  for (i = 0; c[i]; i++) { cmdbuf[i] = c[i]; } cmdbuf[i] = 0; }
                cmd_fs(cmdbuf);

                /* 落盘证据走 fm 路径直读，不经过 VFS 缓存 */
                int rd3 = ide_ext2_read_file("/", "bigdst.bin", (char *)got, 8193);
                int okb = (rd3 == 8192);
                int badat = -1;
                if (okb) {
                    for (int i = 0; i < 8192; i++) {
                        if (got[i] != (uint8_t)(i & 0xFF)) { badat = i; okb = 0; break; }
                    }
                }
                if (!okb) {
                    serial_puts(port, "[VFS-TEST] cp_big dbg rd=");
                    vs_put_dec(port, rd3);
                    serial_puts(port, " badat=");
                    vs_put_dec(port, badat);
                    serial_puts(port, "\n");
                }
                vs_line(port, "shell.cp_big", 13, okb,
                        okb ? "8KB(多块)内容逐字节一致" : "大小或内容不对");
                if (!okb) fails++;
            }

            /* slab 缓存归属：free 掉 4 个 128B 对象，再分配 4 个，必须全数复用回原地。
             * 旧 slab_free 靠 offset 猜 cache，128B 对象 free 时清的是 32B cache
             * 的 bit0 —— 真 cache 位图没清，再分配只能拿新块。 */
            {
                extern void *kmalloc(size_t);
                extern void kfree(void *);
                void *pb[4], *pc[4];
                int oks = 1;
                for (int i = 0; i < 4; i++) pb[i] = kmalloc(120);
                for (int i = 0; i < 4; i++) if (!pb[i]) oks = 0;
                for (int i = 0; i < 4; i++)
                    for (int j = i + 1; j < 4; j++)
                        if (pb[i] == pb[j]) oks = 0;
                for (int i = 0; i < 4; i++) kfree(pb[i]);
                for (int i = 0; i < 4; i++) pc[i] = kmalloc(120);
                for (int i = 0; i < 4 && oks; i++) {
                    int hit = 0;
                    for (int j = 0; j < 4; j++) if (pc[i] == pb[j]) { hit = 1; break; }
                    if (!hit) oks = 0;
                }
                vs_line(port, "slab.reuse", 14, oks,
                        oks ? "4 个 128B 块全数复用（cache 归属正确）"
                            : "有块未复用，cache 归属错");
                if (!oks) fails++;
            }

            /* shell.cpall：遍历根目录逐项递归复制。这条代码改走 VFS 后一直没进自测，
             * 因为它会复制整个根目录 —— 但造盘后根目录只有自测自己建的那几项，
             * 成本很低，可以验。 */
            {
                char c[] = "cpall /cpall_dst"; int i;
                for (i = 0; c[i]; i++) { cmdbuf[i] = c[i]; } cmdbuf[i] = 0;
                cmd_fs(cmdbuf);
                int rdc = ide_ext2_read_file("/cpall_dst", "p2test.txt", rb, 16);
                int okc = (rdc >= VS_LEN);
                if (okc) {
                    for (int k = 0; k < VS_LEN; k++)
                        if (rb[k] != VS_TEXT[k]) { okc = 0; break; }
                }
                vs_line(port, "shell.cpall", 15, okc,
                        okc ? "根目录递归复制，子文件内容正确" : "cpall 没复制出来");
                if (!okc) fails++;
            }

            /* term.ls：终端那一层真的读到了盘。以前 ls 是写死的
             * "dev initrd proc sys bin"，这里验它列出来的东西来自 VFS。 */
            {
                extern int term_selftest_ls(void);
                extern const char *term_selftest_buf(void);
                int tn = term_selftest_ls();
                const char *tb = term_selftest_buf();
                int okt = (tn > 0);
                if (okt) {
                    const char *hay = "p2test";
                    int found = 0;
                    for (int i = 0; tb[i] && !found; i++) {
                        int j = 0;
                        while (hay[j] && tb[i + j] == hay[j]) j++;
                        if (hay[j] == 0) found = 1;
                    }
                    if (!found) okt = 0;
                }
                vs_line(port, "term.ls", 16, okt,
                        okt ? "终端 ls 列出真实目录项" : "终端 ls 没读到盘");
                if (!okt) fails++;
            }
        }

        serial_puts(port, "[VFS-TEST] write phase done, fails=");
        vs_put_dec(port, fails);
        serial_puts(port, "\n");
        serial_puts(port, "[VFS-TEST] === NOW RESET AND RUN READ PHASE ON SAME DISK ===\n");
    } else {
        /* ---- 读阶段：验"落盘" + 懒加载 ----
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

        /* 懒加载：重启后按路径能不能访问到盘上已有的文件。
         * 这条曾经是"已知缺口"（ext2_mount 只扫根目录一层 + 节点没挂树），
         * 修掉之后它应该 PASS —— 不 PASS 就说明懒加载又断了。 */
        {
            extern vfs_node_t *vfs_find(const char *);
            extern int vfs_lazy_rc;
            vfs_node_t *tf = vfs_find(VS_FILE);
            int ok7 = (tf != 0);
            serial_puts(port, "[VFS-TEST] tree.p2 4/");
            vs_put_dec(port, vs_total);
            serial_puts(port, " ");
            serial_puts(port, ok7 ? "PASS" : "FAIL");
            serial_puts(port, ok7 ? "  懒加载找到（重启后可按路径访问）" : "  NULL（懒加载未生效）");
            serial_puts(port, "  lazy_rc=");
            vs_put_dec(port, vfs_lazy_rc);
            {
                extern int ext2_mounted;
                vfs_node_t *rr = vfs_find("/");
                serial_puts(port, " root=");
                vs_put_dec(port, rr ? (int)rr->type : -1);
                serial_puts(port, " rino=");
                vs_put_dec(port, rr ? (int)rr->inode_num : -1);
                serial_puts(port, " em=");
                vs_put_dec(port, ext2_mounted);
                serial_puts(port, " magic=0x");
                { extern void serial_puthex(uint16_t, uint32_t);
                  serial_puthex(port, rr ? rr->magic : 0); }
                serial_puts(port, " sz=");
                vs_put_dec(port, rr ? (int)rr->size : -1);
            }
            serial_puts(port, "\n");
            if (!ok7) fails++;
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
