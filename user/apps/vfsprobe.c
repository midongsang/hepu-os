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

int main(void)
{
    unsigned char buf[16];
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

    out(fails ? "[VFSP] RESULT: FAIL\n" : "[VFSP] RESULT: PASS\n");
    return fails ? 1 : 0;
}

void _start(void) { exit(main()); }
