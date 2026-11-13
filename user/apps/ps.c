/* Hepu OS - ps（进程列表）
 *
 * V0.57.2: 改用 ulib.h + 内核既有号 SYS_HEPU_PS(227)。
 * 旧版自己硬编码 `syscall 200`（内核没有这个号，真实号是 227）和 `syscall 1`
 * （旧版 WRITE 号，真实是 10），一跑就是 "Unimplemented syscall 1/200" + #PF。
 *
 * 结构必须与内核 syscall64_full.c 的 struct hep_ps_entry 一致：
 *   int pid; int ppid; int state; int priority; uint64_t mem_pages; char name[32];
 */
#include "../ulib.h"

struct hep_ps_entry {
    int pid;
    int ppid;
    int state;
    int priority;
    uint64_t mem_pages;
    char name[32];
};

/* V0.58.3: 行缓冲输出。
 * 旧版逐字符 write(1,&c,1)：一行要发几十次 syscall，中途只要内核往串口插
 * 一行日志，输出就变成 "0 0 RUN" 之后字符互相穿插、NAME 显示成乱码。
 * 现在整行拼好再一次性写。 */
static char g_buf[256];
static int  g_len;
static void out_ch(char c) { if (g_len < (int)sizeof(g_buf) - 1) g_buf[g_len++] = c; }
static void out_str(const char *s) { while (*s) out_ch(*s++); }
static void out_int(int v)
{
    char b[16];
    int i = 0;
    if (v < 0) { out_ch('-'); v = -v; }
    if (v == 0) { out_ch('0'); return; }
    while (v > 0 && i < 16) { b[i++] = (char)('0' + v % 10); v /= 10; }
    while (i > 0) out_ch(b[--i]);
}
static void out_flush(void)
{
    if (g_len > 0) { write(STDOUT_FILENO, g_buf, (size_t)g_len); g_len = 0; }
}

static const char *state_str(int s)
{
    switch (s) {
        case 1: return "READY";
        case 2: return "RUN";
        case 4: return "ZOMB";
        default: return "?";
    }
}

int main(void)
{
    /* V0.58.3: 必须放数据段（static），不能放栈上。
     * 8 项 × 56 字节 = 448 字节，栈上一放就跨到用户栈第二页；copy_to_user
     * 写到那一段会失败，用户这边读到的就是半截数据 + 垃圾 —— 表现正是
     * "NAME 空/乱码、多出一行、PAGES 恒 0"。 */
    static struct hep_ps_entry e[8];
    int n = (int)syscall2(SYS_HEPU_PS, (uint64_t)e, 8);
    if (n < 0) {
        puts("ps: syscall failed\n");
        return 1;
    }
    if (n == 0) {
        puts("ps: no user process\n");
        return 0;
    }

    out_str("PID PPID STATE PRI PAGES NAME\n");
    out_flush();
    for (int i = 0; i < n; i++) {
        char nm[33];
        for (int k = 0; k < 32; k++) nm[k] = e[i].name[k];
        nm[32] = 0;

        out_int(e[i].pid); out_ch(' ');
        out_int(e[i].ppid); out_ch(' ');
        out_str(state_str(e[i].state)); out_ch(' ');
        out_int(e[i].priority); out_ch(' ');
        out_int((int)e[i].mem_pages); out_ch(' ');
        out_str(nm); out_ch('\n');
        out_flush();
    }
    return 0;
}

/* 入口点。
 * user.ld 里写的是 ENTRY(_start)，而本文件只有 main —— 找不到 _start 时
 * ld 会退化成"段起始地址"作为入口，这里恰好是辅助函数 put_int 的地址
 * (0x400000)，而 main 在 0x400000e0：程序一启动就跑进 put_int，
 * 参数寄存器里是内核留下的垃圾，随后 ret 弹出栈上的随机值 ——
 * 正是收尾阶段 rip 落到 0 附近的那种 #PF 形态之一。
 * 补一层 _start 把 main 包起来，退出走 ulib.h 的 64 位 syscall。 */
void _start(void)
{
    exit(main());
}

