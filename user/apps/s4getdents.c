/* ============================================================================
 * Hepu OS - S4 用户态 getdents 非根目录探针
 *
 * 内核自检 fs_s4_selftest() 已在 /s4dir 下建好 p4-getdents.txt。
 * 本程序从用户态走真正的 syscall：open("/s4dir") -> getdents64(fd)，
 * 断言目录项里能看到该文件，而不是只读根目录。
 * ========================================================================== */
#include "../ulib.h"

static void out(const char *s)
{
    while (*s) write(STDOUT_FILENO, s++, 1);
}

static void out_num(int v)
{
    char b[12];
    int i = 0;
    if (v == 0) { write(STDOUT_FILENO, "0", 1); return; }
    while (v && i < 12) { b[i++] = (char)('0' + v % 10); v /= 10; }
    while (i > 0) write(STDOUT_FILENO, &b[--i], 1);
}

int main(void)
{
    unsigned char buf[512];
    int fd, n, off, found = 0;

    out("[S4U] getdents probe begin\n");

    fd = open("/s4dir", 0);
    out("[S4U] open(/s4dir) fd="); out_num(fd); out("\n");
    if (fd < 3) {
        out("[S4U] getdents-nonroot FAIL\n");
        return 1;
    }

    n = (int)syscall3(SYS_LNX64_GETDENTS, (uint64_t)fd,
                      (uint64_t)buf, sizeof(buf));
    out("[S4U] getdents bytes="); out_num(n); out("\n");
    if (n <= 0) {
        close(fd);
        out("[S4U] getdents-nonroot FAIL\n");
        return 1;
    }

    off = 0;
    while (off + 19 <= n) {
        uint16_t reclen = (uint16_t)buf[off + 16] |
                          ((uint16_t)buf[off + 17] << 8);
        int nl = 0;
        char name[256];
        if (reclen < 19 || off + reclen > n) break;
        while (nl < 255 && off + 19 + nl < off + reclen) {
            name[nl] = (char)buf[off + 19 + nl];
            if (!name[nl]) break;
            nl++;
        }
        name[nl] = 0;
        if (strcmp(name, "p4-getdents.txt") == 0) found = 1;
        off += reclen;
    }
    close(fd);

    if (!found) {
        out("[S4U] getdents-nonroot FAIL\n");
        return 1;
    }
    out("[S4U] getdents-nonroot PASS\n");
    return 0;
}

void _start(void) { exit(main()); }
