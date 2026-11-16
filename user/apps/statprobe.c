/* Hepu OS - statprobe（struct stat 布局探针，TECH_DEBT_ROUTE §5-5 第 2 批前置）
 *
 * 为什么必须先有这个探针:
 * 第 2 批要把 sys_stat/sys_fstat 从 syscall64_full.c 切出去。切错的风险不是
 * "编译不过", 而是**字段偏移错位** —— ld.so 拿错误的 (st_dev, st_ino) 去重,
 * 把 libc.so.6 认成主程序, 而标准回归照样全绿(原生 child 不用 glibc)。
 * 所以要先能"看见"结构体的真实字节, 再动代码。
 *
 * 这个探针不看结构体定义, 直接按字节偏移读原始缓冲:
 * 结构体定义本身可能就是错的(这正是要查的东西), 用它去读等于用错的尺子量错的布。
 * 参考布局(Linux x86_64 struct stat, 144 字节):
 *   0 st_dev(8) | 8 st_ino(8) | 16 st_nlink(8) | 24 st_mode(4) | 28 st_uid(4)
 *   32 st_gid(4) | 36 __pad0(4) | 40 st_rdev(8) | 48 st_size(8)
 *   56 st_blksize(8!) | 64 st_blocks(8!) | 72 st_atim(16) | 88 st_mtim(16)
 *   104 st_ctim(16) | 120 __unused(24)
 * 注意 blksize/blocks 在 x86_64 上是 **8 字节**, 不是 4 —— 写成 4 会让后面
 * 所有字段整体前移 8 字节。
 */
#include "../ulib.h"

#define ST_SIZE_OFF      48
#define ST_BLKSIZE_OFF   56
#define ST_BLOCKS_OFF    64
#define ST_ATIME_OFF     72
#define STAT_BYTES       144

#define S_IFMT  0170000
#define S_IFREG 0100000
#define S_IFDIR 0040000

static void out(const char *s) { while (*s) write(STDOUT_FILENO, s++, 1); }
static void out_hex(uint64_t v)
{
    static const char *h = "0123456789abcdef";
    char b[20]; int i = 0;
    write(STDOUT_FILENO, "0x", 2);
    if (!v) { write(STDOUT_FILENO, "0", 1); return; }
    while (v && i < 20) { b[i++] = h[v & 0xF]; v >>= 4; }
    while (i > 0) write(STDOUT_FILENO, &b[--i], 1);
}

/* 从原始字节读小端 8 字节 / 4 字节 */
static uint64_t rd64(const unsigned char *p)
{
    return (uint64_t)p[0] | ((uint64_t)p[1] << 8) | ((uint64_t)p[2] << 16) |
           ((uint64_t)p[3] << 24) | ((uint64_t)p[4] << 32) | ((uint64_t)p[5] << 40) |
           ((uint64_t)p[6] << 48) | ((uint64_t)p[7] << 56);
}
static uint32_t rd32(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

/* 不用全局计数器: 全局或 static 会落进 .bss, 段就变成 RWE,
 * 而 Hepu 的加载器对 RW 段的映射和纯代码段不一样(实测 RWE 会页错误)。
 * 让它走返回值, 段保持 R E。 */
/* 手工拼成一行再一次性 write。
 * 不用 printf: ulib.h 里那个 printf 自己标了 variadic 会 #PF(栈顶之外),
 * 分段逐字符 write 又在串口里丢行。一次 write 是最可靠的。 */
static void scat(char *d, int *n, const char *s)
{
    while (*s) d[(*n)++] = *s++;
}
static void shex(char *d, int *n, uint64_t v)
{
    static const char *h = "0123456789abcdef";
    char b[20]; int i = 0;
    d[(*n)++] = '0'; d[(*n)++] = 'x';
    if (!v) { d[(*n)++] = '0'; return; }
    while (v && i < 20) { b[i++] = h[v & 0xF]; v >>= 4; }
    while (i > 0) d[(*n)++] = b[--i];
}
static int check(const char *what, int ok, uint64_t got)
{
    /* 全部走 out()(逐字符 write, 与能正常输出的那几行同一条路径)。
     * 拼串 + 一次 write 在这里实测会整行丢失, 所以退回最笨的方式:
     * 标签用字面量, 数值逐位十六进制字符写出去。 */
    out(ok ? "  ok   " : "  FAIL ");
    out(what);
    out(" = ");
    out_hex(got);
    out("\n");
    return ok ? 0 : 1;
}

int main(void)
{
    unsigned char sb[STAT_BYTES];
    int r;

    int fails = 0;
    out("[STATP] struct stat layout probe\n");

    /* ---- 1. 文件: /bin/hello.elf ---- */
    for (int i = 0; i < STAT_BYTES; i++) sb[i] = 0xAA;   /* 哨兵: 没被写到的字节留在 0xAA */
    r = (int)syscall2(24 /* SYS_STAT */, (uint64_t)"/bin/hello.elf", (uint64_t)sb);
    fails += check("stat(/bin/hello.elf) rc", r == 0, (uint64_t)(uint32_t)r);
    if (r != 0) {
        out("[STATP] RESULT: FAIL (stat 调用失败, 后续检查无意义)\n");
        return 1;
    }

    uint32_t mode = rd32(&sb[24]);
    fails += check("st_mode @24 has S_IFREG", (mode & S_IFMT) == S_IFREG, mode);

    uint64_t size = rd64(&sb[ST_SIZE_OFF]);
    fails += check("st_size @48 > 0", size > 0, size);

    /* blksize 必须是 8 字节读出的**小整数**。旧实现把它写成 4 字节 int32,
     * 于是 offset 56 处是 [blksize 低4字节][blocks 低4字节], 按 8 字节读会得到
     * 一个巨大的数(高 32 位是块数) —— 这一条正是用来抓那个错位的。 */
    uint64_t blksize = rd64(&sb[ST_BLKSIZE_OFF]);
    fails += check("st_blksize @56 sane", blksize > 0 && blksize <= 65536, blksize);

    uint64_t blocks = rd64(&sb[ST_BLOCKS_OFF]);
    fails += check("st_blocks @64 sane", blocks <= (size / 512 + 2), blocks);

    /* 尾部 16 字节(128..144) 必须被内核写过。旧实现 sizeof=128, 只拷 128 字节,
     * 用户态后 16 字节仍是哨兵 —— 但用户态 memset 出来的 stat 结构是 144,
     * 读 st_ctim 会读到未初始化内存。 */
    int tail_written = (sb[STAT_BYTES - 1] != 0xAA) || (sb[128] != 0xAA);
    fails += check("bytes 128..143 written", tail_written, sb[STAT_BYTES - 1]);

    /* ---- 2. inode 唯一性: 两个不同文件必须给出不同 st_ino ---- */
    unsigned char sb2[STAT_BYTES];
    for (int i = 0; i < STAT_BYTES; i++) sb2[i] = 0xAA;
    r = (int)syscall2(24, (uint64_t)"/bin/shell.elf", (uint64_t)sb2);
    if (r == 0) {
        uint64_t ino1 = rd64(&sb[8]);
        uint64_t ino2 = rd64(&sb2[8]);
        /* ld.so 用 (st_dev, st_ino) 判断"两个路径是不是同一个文件"。
         * 全 0 会让所有文件看起来是同一个 —— 这正是文档点名的风险。 */
        fails += check("st_ino non-zero", ino1 != 0, ino1);
        fails += check("st_ino differs per file", ino1 != ino2, ino2);
    } else {
        out("  stat(/bin/shell.elf): 跳过(文件不存在)\n");
    }

    /* ---- 3. 目录 ---- */
    unsigned char sb3[STAT_BYTES];
    for (int i = 0; i < STAT_BYTES; i++) sb3[i] = 0xAA;
    r = (int)syscall2(24, (uint64_t)"/bin", (uint64_t)sb3);
    if (r == 0) {
        uint32_t dmode = rd32(&sb3[24]);
        fails += check("stat(/bin) has S_IFDIR", (dmode & S_IFMT) == S_IFDIR, dmode);
    } else {
        out("  stat(/bin): 跳过\n");
    }

    out(fails ? "[STATP] RESULT: FAIL\n" : "[STATP] RESULT: PASS\n");
    /* 同时走串口(boot battery 抓的是串口), 这里只写 stdout, 由内核转发 */
    return fails ? 1 : 0;
}

/* 入口点：user.ld 的 ENTRY(_start)。
 * 少了这一行, ld 会把 entry 退化成段起始地址 0x40000000 —— 那里是 ELF 头
 * (字节全 0), 第一条指令就解引用空指针, 表现为 [EXCEPTION] vector=0xE
 * error=0x5 rip=0x40000000 CR2=0x0。fbdesc.c 末尾有这行, 这个探针漏了。 */
void _start(void) { exit(main()); }
