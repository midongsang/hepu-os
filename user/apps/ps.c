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

/* 单字符必须用 write 直接发，不能喂给 puts：puts 内部 strlen 会一路读到底，
 * 单字符后面没有 \0 时 n 会变成巨大值 → write 输出巨石量 → 进程卡死
 * （实测被 watchdog 在 RIP=0x4000017D 抓到并强制终止）。 */
static void put_ch(char c) { write(STDOUT_FILENO, &c, 1); }

static void put_int(int v)
{
    char b[16];
    int i = 0;
    if (v < 0) { put_ch('-'); v = -v; }
    if (v == 0) { put_ch('0'); return; }
    while (v > 0 && i < 16) { b[i++] = (char)('0' + v % 10); v /= 10; }
    while (i > 0) put_ch(b[--i]);
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
    struct hep_ps_entry e[8];
    int n = (int)syscall2(SYS_HEPU_PS, (uint64_t)e, 8);
    if (n < 0) {
        puts("ps: syscall failed\n");
        return 1;
    }
    if (n == 0) {
        puts("ps: no user process\n");
        return 0;
    }

    puts("PID PPID STATE PRI PAGES NAME\n");
    for (int i = 0; i < n; i++) {
        char nm[33];
        for (int k = 0; k < 32; k++) nm[k] = e[i].name[k];
        nm[32] = 0;

        put_int(e[i].pid); put_ch(' ');
        put_int(e[i].ppid); put_ch(' ');
        puts(state_str(e[i].state)); put_ch(' ');
        put_int(e[i].priority); put_ch(' ');
        put_int((int)e[i].mem_pages); put_ch(' ');
        puts(nm); puts("\n");
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

