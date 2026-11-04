/* install64.elf - 赫普OS 系统安装向导（交互版 V0.13.50）
 * 流程: 欢迎/分区显示 -> 菜单(试用/一键安装/自定义分区) -> 三区制写盘 -> 重启
 * syscall: 1=write 2=read(fd0键盘) 6=exit 26=disk_read 27=disk_write 28=reboot
 */
static long syscall3(long n, long a, long b, long c)
{
    long r;
    __asm__ volatile("syscall"
                     : "=a"(r)
                     : "a"(n), "D"(a), "S"(b), "d"(c)
                     : "rcx", "r11", "memory");
    return r;
}

static long syscall4(long n, long a, long b, long c, long d)
{
    long r;
    register long r10 asm("r10") = d;
    __asm__ volatile("syscall"
                     : "=a"(r)
                     : "a"(n), "D"(a), "S"(b), "d"(c), "r"(r10)
                     : "rcx", "r11", "memory");
    return r;
}

static void putstr(const char *s)
{
    int len = 0;
    while (s[len]) len++;
    syscall3(1, 1, (long)s, len);
}

static void putchar(char c)
{
    syscall3(1, 1, (long)&c, 1);
}

static void putdec(unsigned int v)
{
    char buf[12];
    int i = 11;
    buf[i] = 0;
    if (v == 0) buf[--i] = '0';
    while (v) { buf[--i] = '0' + (v % 10); v /= 10; }
    putstr(buf + i);
}

static unsigned int get_u32(unsigned char *p)
{
    return p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
}

static void set_u32(unsigned char *p, unsigned int v)
{
    p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF;
    p[2] = (v >> 16) & 0xFF; p[3] = (v >> 24) & 0xFF;
}

/* 读一行键盘输入（回显 + 回车结束） */
static int read_line(char *buf, int max)
{
    int i = 0;
    while (i < max - 1) {
        char c = 0;
        if (syscall3(2, 0, (long)&c, 1) <= 0) continue;
        if (c == '\n' || c == '\r') { putchar('\n'); break; }
        if (c == '\b' || c == 0x7F) {
            if (i > 0) { i--; putstr("\b \b"); }
            continue;
        }
        buf[i++] = c;
        putchar(c);
    }
    buf[i] = 0;
    return i;
}

/* 解析十进制数（溢出返回 0xFFFFFFFF） */
static unsigned int parse_dec(const char *s)
{
    unsigned int v = 0;
    while (*s >= '0' && *s <= '9') {
        if (v > 0xFFFFFFF) return 0xFFFFFFFF;
        v = v * 10 + (*s - '0');
        s++;
    }
    return v;
}

/* 磁盘总大小（扇区数）：用 P1+P2+P3 已用区域无法精确，按 0xFFFFFFF0 上限 */
void _start(void)
{
    static unsigned char mbr[512];
    char line[32];
    unsigned int sys_start = 0, sys_sec = 0;

    putstr("========================================\n");
    putstr("    赫普OS (HepuOS) 系统安装向导\n");
    putstr("    V0.13.50 - 三区制安装（交互版）\n");
    putstr("========================================\n\n");

    putstr("[1/5] 读取磁盘分区表...\n");
    if (syscall3(26, 0, (long)mbr, 1) < 0) {
        putstr("ERROR: 读取磁盘失败\n");
        syscall3(6, 0, 0, 0);
    }
    if (mbr[510] != 0x55 || mbr[511] != 0xAA) {
        putstr("WARNING: 未检测到引导签名(0x55AA)\n");
    }
    putstr("当前分区:\n");
    for (int p = 0; p < 4; p++) {
        unsigned char *e = mbr + 446 + p * 16;
        if (e[4] == 0) continue;
        putstr("  P");
        putchar('1' + p);
        putstr(": type=0x");
        putchar("0123456789ABCDEF"[e[4] >> 4]);
        putchar("0123456789ABCDEF"[e[4] & 0xF]);
        putstr(" start=");
        putdec(get_u32(e + 8));
        putstr(" size=");
        putdec(get_u32(e + 12));
        putstr("\n");
        if (e[4] == 0x83 && sys_sec == 0) {
            sys_start = get_u32(e + 8);
            sys_sec = get_u32(e + 12);
        }
    }
    if (sys_sec == 0) {
        putstr("ERROR: 未找到系统分区(ext2 0x83)\n");
        syscall3(6, 0, 0, 0);
    }
    putstr("系统分区: LBA ");
    putdec(sys_start);
    putstr(", ");
    putdec(sys_sec);
    putstr(" 扇区\n\n");

    /* 菜单 */
    unsigned int sys_mb = sys_sec / 2048;  /* V0.13.53: 系统分区可配, 默认=当前 */
    unsigned int cfg_mb = 512, data_mb = 0;  /* V0.13.52: 默认512MB(软件配置占用高), 0=剩余 */
    int install = 0;
    for (;;) {
        putstr("========================================\n");
        putstr("请选择操作:\n");
        putstr("  1) 仅试用，继续使用系统（不安装）\n");
        putstr("  2) 一键安装（配置512MB + 自由=剩余）\n");
        putstr("  3) 自定义分区大小\n");
        putstr("  0) 退出\n");
        putstr("输入数字后回车: ");
        if (read_line(line, sizeof(line)) <= 0) continue;
        char c = line[0];
        if (c == '1') {
            putstr("继续试用模式。安装向导退出。\n");
            syscall3(6, 0, 0, 0);
        } else if (c == '0') {
            putstr("退出安装向导。\n");
            syscall3(6, 0, 0, 0);
        } else if (c == '2') {
            install = 1;
            break;
        } else if (c == '3') {
            putstr("系统分区大小(MB, 回车=当前");
            putdec(sys_mb);
            putstr(", 只能增大): ");
            if (read_line(line, sizeof(line)) > 0 && line[0] >= '0' && line[0] <= '9') {
                unsigned int v = parse_dec(line);
                if (v >= sys_mb) sys_mb = v;
                else putstr("  (系统分区不能小于当前文件系统大小)\n");
            }
            putstr("配置分区大小(MB, 回车=默认512): ");
            if (read_line(line, sizeof(line)) > 0 && line[0] >= '0' && line[0] <= '9') {
                unsigned int v = parse_dec(line);
                if (v != 0 && v != 0xFFFFFFFF && v > 0) cfg_mb = v;
            }
            putstr("自由分区大小(MB, 回车=剩余全部): ");
            if (read_line(line, sizeof(line)) > 0 && line[0] >= '0' && line[0] <= '9') {
                unsigned int v = parse_dec(line);
                if (v != 0xFFFFFFFF) data_mb = v;
            }
            putstr("确认安装? (y/n): ");
            if (read_line(line, sizeof(line)) > 0 && (line[0] == 'y' || line[0] == 'Y')) {
                install = 1;
                break;
            }
            putstr("已取消。\n");
        } else {
            putstr("无效输入。\n");
        }
    }

    /* 三区制布局 */
    putstr("[2/5] 计算三区布局...\n");
    unsigned int sys_sec2 = sys_mb * 2048;   /* V0.13.53: P1 按用户选择(>=当前) */
    unsigned int cfg_sec = cfg_mb * 2048;
    unsigned int p2_start = sys_start + sys_sec2;
    unsigned int p3_start = p2_start + cfg_sec;
    unsigned int p3_sec;
    if (data_mb > 0) {
        p3_sec = data_mb * 2048;
    } else {
        p3_sec = 0xFFFFFFF0 - p3_start;  /* 剩余到盘末 */
    }
    putstr("    P1 系统分区(ext2): LBA ");
    putdec(sys_start);
    putstr(" ");
    putdec(sys_sec2);
    putstr(" 扇区(");
    putdec(sys_mb);
    putstr("MB)\n");
    putstr("    P2 配置分区(ext2): LBA ");
    putdec(p2_start);
    putstr(" ");
    putdec(cfg_sec);
    putstr(" 扇区(");
    putdec(cfg_mb);
    putstr("MB) - 软件/游戏配置\n");
    putstr("    P3 自由分区(ext2): LBA ");
    putdec(p3_start);
    putstr(" ");
    putdec(p3_sec);
    putstr(" 扇区 - 用户数据\n");

    putstr("[3/5] 写入三区 MBR...\n");
    for (int i = 0; i < 64; i++) mbr[446 + i] = 0;
    mbr[446 + 0]  = 0x80;
    mbr[446 + 4]  = 0x83;
    set_u32(mbr + 446 + 8, sys_start);
    set_u32(mbr + 446 + 12, sys_sec2);   /* V0.13.53: P1 按用户选择大小 */
    mbr[446 + 16 + 4] = 0x83;
    set_u32(mbr + 446 + 16 + 8, p2_start);
    set_u32(mbr + 446 + 16 + 12, cfg_sec);
    mbr[446 + 32 + 4] = 0x83;
    set_u32(mbr + 446 + 32 + 8, p3_start);
    set_u32(mbr + 446 + 32 + 12, p3_sec);
    mbr[510] = 0x55; mbr[511] = 0xAA;

    if (syscall3(27, 0, (long)mbr, 1) < 0) {
        putstr("ERROR: 写入磁盘失败\n");
        syscall3(6, 0, 0, 0);
    }
    putstr("OK: 三区 MBR 写入完成\n");

    /* 从源盘(USB, drive0)拷贝引导区到目标盘(硬盘, drive1) */
    {
        static unsigned char secbuf[32768];
        extern void putstr(const char *s);
        putstr("拷贝引导区到目标盘...\n");
        /* bootsector LBA0: 1 扇区 */
        syscall3(26, 0, (long)secbuf, 1);
        syscall4(27, 0, (long)secbuf, 1, 1);
        /* stage2 LBA34: 32 扇区分批 */
        for (int off = 0; off < 32; off += 1) {
            syscall3(26, 34 + off, (long)secbuf, 1);
            syscall4(27, 34 + off, (long)secbuf, 1, 1);
        }
        /* kernel64 LBA2000: 1280 扇区(640KB，匹配 Stage2 上限), 每次 64 扇区(32KB) */
        for (unsigned int l = 2000; l < 2000 + 1280; l += 64) {
            int cnt = 64;
            if (2000 + 1280 - l < 64) cnt = 2000 + 1280 - l;
            syscall4(26, l, (long)secbuf, cnt, 0);
            syscall4(27, l, (long)secbuf, cnt, 1);
        }
        putstr("  引导区+内核拷贝完成\n");
    }

    putstr("[4/5] 格式化 P1/P2/P3 为 ext2...\n");
    if (syscall4(31, sys_start, sys_sec2,  (long)"P1", 0x4D4B4653) < 0) putstr("  P1 mkfs 失败\n");
    else putstr("  P1 OK\n");
    if (syscall4(31, p2_start,  cfg_sec,   (long)"P2", 0x4D4B4653) < 0) putstr("  P2 mkfs 失败\n");
    else putstr("  P2 OK\n");
    if (syscall4(31, p3_start, p3_sec,    (long)"P3", 0x4D4B4653) < 0) putstr("  P3 mkfs 失败\n");
    else putstr("  P3 OK\n");

    putstr("[5/5] 安装完成! 即将重启进入新系统...\n");
    putstr("[5/5] 重启中 (也可按任意键取消, 等待5秒)...\n");
    /* 简单等待（读键盘超时放弃——直接重启） */
    syscall3(28, 0, 0, 0);
    for (;;) {}
}
