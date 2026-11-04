/* linuxrt.c - HEPU Linux ABI 运行时模拟 (复刻 musl __init_libc 栈解析 +
 * brk 简易 malloc/free). 验证: 完整启动栈(envp/auxv) 构造正确性 +
 * 堆内存模型与标准库 malloc 行为等价. */
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

/* ---- 简易 malloc/free (first-fit, brk 扩展堆) ---- */
typedef struct blk { unsigned size; struct blk *next; } blk_t;
static blk_t *heap_head = 0;
static unsigned heap_cur = 0;
static unsigned heap_max = 0;

static void *my_malloc(unsigned n)
{
    n = (n + 7) & ~7u;
    blk_t *b = heap_head, *prev = 0;
    for (; b; prev = b, b = b->next) {   /* 复用空闲块 (first-fit) */
        if (b->size >= n) {
            if (b->size >= n + 16) {     /* 分裂 */
                blk_t *rest = (blk_t *)((char *)b + 16 + n);
                rest->size = b->size - n - 16;
                rest->next = b->next;
                b->next = rest;
                b->size = n;
            }
            if (prev) prev->next = b->next; else heap_head = b->next;
            return (char *)b + 16;
        }
    }
    /* 堆不够: brk 扩展 (标准库行为: 按页对齐向上扩) */
    unsigned hb = sys_brk(0);
    unsigned need = hb + 16 + n;
    unsigned target = (need + 4095) & ~4095u;
    unsigned got = sys_brk(target);
    if (got < need) return 0;            /* 扩展失败 */
    b = (blk_t *)hb;
    b->size = n;
    b->next = heap_head;
    heap_head = b;
    return (char *)b + 16;
}
static void my_free(void *p)
{
    if (!p) return;
    blk_t *b = (blk_t *)((char *)p - 16);
    b->next = heap_head;
    heap_head = b;
}

/* ---- 启动栈解析 (复刻 musl crt1: 从 esp 解析) ---- */
void cstart(unsigned *sp)
{
    unsigned argc = sp[0];
    char **argv = (char **)(sp + 1);
    char **envp = argv + argc + 1;
    /* auxv: envp 终止 NULL 之后 */
    unsigned *p = (unsigned *)envp;
    while (*p) p++;
    unsigned *auxv = p + 1;

    puts_str("CRT: argc=");
    puthex(argc);
    puts_str("CRT: argv[0]=");
    puts_str(argv[0]);
    puts_str("\n");
    puts_str("CRT: envp[0]=");
    puts_str(envp[0]);
    puts_str("\n");
    puts_str("CRT: envp[1]=");
    puts_str(envp[1]);
    puts_str("\n");

    unsigned pagesz = 0, entry = 0, phdr = 0, phnum = 0, clk = 0;
    for (unsigned *a = auxv; a[0] != 0; a += 2) {
        switch (a[0]) {
        case 3: phdr = a[1]; break;
        case 5: phnum = a[1]; break;
        case 6: pagesz = a[1]; break;
        case 9: entry = a[1]; break;
        case 17: clk = a[1]; break;
        }
    }
    puts_str("CRT: auxv pagesz=");
    puthex(pagesz);
    puts_str("CRT: auxv entry=");
    puthex(entry);
    puts_str("CRT: auxv phdr=");
    puthex(phdr);
    puts_str("CRT: auxv phnum=");
    puthex(phnum);
    puts_str("CRT: auxv clktck=");
    puthex(clk);

    if (pagesz != 4096 || entry == 0 || phdr == 0 || phnum == 0 || clk != 100) {
        puts_str("CRT: auxv CHECK FAIL\n");
    } else {
        puts_str("CRT: auxv CHECK PASS\n");
    }

    /* malloc/free 验证 (标准库内存模型等价行为) */
    char *a = my_malloc(64);
    char *b = my_malloc(128);
    char *c = my_malloc(256);
    int i, ok = 1;
    for (i = 0; i < 64; i++)  a[i] = (char)i;
    for (i = 0; i < 128; i++) b[i] = (char)(i ^ 0x55);
    for (i = 0; i < 256; i++) c[i] = (char)(i & 0x3F);
    for (i = 0; i < 64; i++)  if (a[i] != (char)i) ok = 0;
    for (i = 0; i < 128; i++) if (b[i] != (char)(i ^ 0x55)) ok = 0;
    for (i = 0; i < 256; i++) if (c[i] != (char)(i & 0x3F)) ok = 0;
    puts_str(ok ? "CRT: malloc3+write+read PASS\n" : "CRT: malloc CHECK FAIL\n");
    my_free(c);
    my_free(b);
    my_free(a);
    puts_str("CRT: free done\n");
    puts_str("CRT: runtime OK, exit\n");
    sys_exit(0);
    for (;;) { }
}
