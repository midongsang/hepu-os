/* linuxbrk.c - HEPU Linux ABI brk 堆内存测试
 * 验证: brk(45) 翻译 → 堆扩展 → 写入 → 读取 → 收缩 → 退出
 * 编译: gcc -m32 -nostdlib -static -Wl,-e,_start */
static int sys_write(int fd, const void *buf, int len)
{
    int r;
    __asm__ volatile ("int $0x80"
        : "=a"(r)
        : "a"(4), "b"(fd), "c"(buf), "d"(len));
    return r;
}
static int sys_brk(unsigned addr)
{
    int r;
    __asm__ volatile ("int $0x80" : "=a"(r) : "a"(45), "b"(addr));
    return r;
}
static int sys_exit(int code)
{
    int r;
    __asm__ volatile ("int $0x80" : "=a"(r) : "a"(1), "b"(code));
    return r;
}
static void puts_str(const char *s)
{
    int n = 0;
    while (s[n]) n++;
    sys_write(1, s, n);
}
static void puthex(unsigned v)
{
    const char *hex = "0123456789ABCDEF";
    char buf[11];
    buf[0] = '0'; buf[1] = 'x'; buf[2] = 0;
    int i;
    for (i = 0; i < 8; i++) buf[2 + i] = hex[(v >> (28 - i * 4)) & 0xF];
    buf[10] = '\n';
    sys_write(1, buf, 11);
}

void _start(void)
{
    puts_str("BRK test: initial brk=");
    puthex(sys_brk(0));

    /* 扩展堆 16KB */
    unsigned base = sys_brk(0);
    unsigned new_end = base + 0x4000;
    unsigned got = sys_brk(new_end);
    puts_str("BRK test: grow to 0x4000 -> brk=");
    puthex(got);

    /* 在堆上写入并读回 */
    volatile char *p = (volatile char *)base;
    int i;
    for (i = 0; i < 0x1000; i++) p[i] = (char)(i & 0xFF);
    int ok = 1;
    for (i = 0; i < 0x1000; i++) {
        if (p[i] != (char)(i & 0xFF)) { ok = 0; break; }
    }
    puts_str("BRK test: heap write+read -> ");
    puts_str(ok ? "PASS\n" : "FAIL\n");

    /* 查询当前 brk (应 >= base+0x4000) */
    puts_str("BRK test: brk after grow=");
    puthex(sys_brk(0));

    /* 收缩回 base */
    got = sys_brk(base);
    puts_str("BRK test: shrink -> brk=");
    puthex(got);

    puts_str("BRK test: done, exit\n");
    sys_exit(0);
    for (;;) { }
}
