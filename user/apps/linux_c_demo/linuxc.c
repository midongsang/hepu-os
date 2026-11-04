/* linuxc.c - HEPU Linux ABI 兼容层 C 语言测试
 * 裸 syscall (int 0x80), 无 libc:
 *   write(1, ...)       输出到 /dev/console
 *   getpid(20)          返回当前 PID
 *   open("/data/t.txt") 沙箱白名单内 → 应放行
 *   open("/system/x")   沙箱外 → 应被 [SANDBOX] 拒绝
 *   close(fd)           关闭
 *   exit(1)             退出
 * 编译: gcc -m32 -nostdlib -static -Wl,-e,_start */
static int sys_write(int fd, const void *buf, int len)
{
    int r;
    __asm__ volatile ("int $0x80"
        : "=a"(r)
        : "a"(4), "b"(fd), "c"(buf), "d"(len));
    return r;
}
static int sys_getpid(void)
{
    int r;
    __asm__ volatile ("int $0x80" : "=a"(r) : "a"(20));
    return r;
}
static int sys_open(const char *path, int flags)
{
    int r;
    __asm__ volatile ("int $0x80"
        : "=a"(r)
        : "a"(5), "b"(path), "c"(flags));
    return r;
}
static int sys_close(int fd)
{
    int r;
    __asm__ volatile ("int $0x80" : "=a"(r) : "a"(6), "b"(fd));
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
    puts_str("LinuxC on HEPU: pid=");
    puthex(sys_getpid());
    puts_str("LinuxC: open /data/t.txt -> ");
    puthex(sys_open("/data/t.txt", 0));
    puts_str("LinuxC: open /system/x -> ");
    puthex(sys_open("/system/x", 0));
    puts_str("LinuxC: done, exit\n");
    sys_exit(0);
    for (;;) { }
}
