/* ============================================================================
 * Hepu OS - SEC-04 用户态凭据闸负向探针（64 位，S4 自测）
 *
 * 目的：在真实 ring3 进程里验证：
 *   1) setuid(1000) 能把当前进程切到非 root（每进程凭据生效）；
 *   2) 非 root、无 RAW_DISK 能力位的进程调用 disk_read 必须被拒绝；
 *   3) 拒绝后进程仍可正常输出并退出，不会拖垮整机。
 *
 * 不验证安装器允许路径（安装器由 OPEN-04 的独立夹具覆盖）。
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
    if (v < 0) { write(STDOUT_FILENO, "-", 1); v = -v; }
    while (v && i < 12) { b[i++] = (char)('0' + v % 10); v /= 10; }
    while (i > 0) write(STDOUT_FILENO, &b[--i], 1);
}

int main(void)
{
    unsigned char sec[512];
    int r;

    out("[CREDTEST] user gate probe begin\n");

    r = (int)syscall1(SYS_SETUID, 1000);
    if (r != 0) {
        out("[CREDTEST] setuid(1000) FAIL rc="); out_num(r); out("\n");
        return 1;
    }
    int uid = (int)syscall0(SYS_GETUID);
    out("[CREDTEST] uid="); out_num(uid); out("\n");
    if (uid != 1000) {
        out("[CREDTEST] uid verify FAIL\n");
        return 1;
    }

    /* 非 root 且未获 RAW_DISK 能力位，裸盘读必须返回负值。 */
    r = (int)syscall4(SYS_DISK_READ, 0, (uint64_t)sec, 1, 0);
    out("[CREDTEST] disk_read rc="); out_num(r); out("\n");
    if (r >= 0) {
        out("[CREDTEST] denied FAIL (unexpected success)\n");
        return 1;
    }

    out("[CREDTEST] PASS\n");
    return 0;
}

void _start(void) { exit(main()); }
