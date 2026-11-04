/* ============================================================================
 * 赫普OS - Linux 兼容层测试程序（PIE / ET_DYN）
 *
 * 用途：验证「Linux ABI 兼容加载路径」——
 *   - 这个程序用**标准 Linux 工具链**（gcc/glibc）编译成 PIE（ET_DYN），
 *     链接基址是 0（位置无关），落在 Hepu 用户窗口之外；
 *   - Hepu 的加载器给它分配一个兼容程序区基址（0x50000000 起），
 *     用加载偏置把镜像摆进去，并按同一偏置修正入口点与
 *     R_X86_64_RELATIVE 重定位；
 *   - 系统调用按 **Linux 号表**翻译（该进程 env_type = linux_compat）。
 *
 * 编译（宿主机）：
 *   gcc -static-pie -O2 -fPIE -o linux_pie_test.elf linux_pie_test.c
 *   （Debian 12 的 glibc 2.36 支持 -static-pie）
 *
 * V0.33 诊断（BUG_FIX_GUIDELINES「证据优先」）：
 *   glibc 退出路径会把 atexit/dtor 函数指针用「pointer guard」混淆后存放，
 *   调用时再解混淆（ror17 + xor %fs:0x30）。若 guard 在「注册」与「调用」
 *   之间被改动，解出来的就是野指针（本例实测 rip=0x202）。
 *   这里打印 canary(%fs:0x28) / guard(%fs:0x30) 的时间序列，
 *   直接判定 TCB 是否在运行期被改写。
 * ========================================================================== */
#include <stdio.h>
#include <unistd.h>
#include <string.h>

/* 直接读 TLS 偏移：x86-64 的 canary 在 %fs:0x28，pointer guard 在 %fs:0x30 */
static unsigned long long fs_at(int off)
{
    unsigned long long v;
    if (off == 0x28)
        __asm__ volatile("movq %%fs:0x28, %0" : "=r"(v));
    else
        __asm__ volatile("movq %%fs:0x30, %0" : "=r"(v));
    return v;
}

static void probe(const char *tag)
{
    char buf[128];
    int n = snprintf(buf, sizeof(buf),
                     "[demo] %s canary=0x%016llx guard=0x%016llx\n",
                     tag, fs_at(0x28), fs_at(0x30));
    write(1, buf, (long)n);
}

/* atexit 探针：它在退出处理链里**最先**执行（LIFO），
 * 能跑通就说明 exit 机制本身没问题，出问题的是别的条目。 */
static void at_exit_probe(void)
{
    probe("atexit");
}

int main(void)
{
    /* 用 write(2) 而不是 printf：避免拉起 glibc 全套缓冲区初始化，
     * 让测试聚焦在「能不能跑起来」这件事上。 */
    const char *msg = "Hello from Linux PIE on Hepu!\n";
    write(1, msg, (long)strlen(msg));

    probe("main-entry");

    /* 走一次 glibc 内部路径（strlen 是 glibc 的函数），确认 libc 也能用 */
    char buf[64];
    int n = snprintf(buf, sizeof(buf), "PIE ok, len=%d\n", (int)strlen(msg));
    write(1, buf, (long)n);

    atexit(at_exit_probe);
    probe("main-exit");

    /* 返回值走 exit_group(231) → Hepu exit(6) */
    return 0;
}
