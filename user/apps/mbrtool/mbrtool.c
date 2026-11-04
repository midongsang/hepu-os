/* mbrtool.elf - MBR 分区表工具（64位裸程序）
 * V0.13.47: 读取显示 MBR 分区表
 * V0.13.49: create 模式 - 三区制(系统/配置/自由)写 MBR, 安装引导流地基
 * syscall: 1=write, 6=exit, 26=disk_read, 27=disk_write, 28=reboot
 * 用法: mbrtool          -> 显示分区表
 *       mbrtool create   -> 保持P1(系统), 新增P2(配置64MB)+P3(自由=剩余)
 *       mbrtool reboot   -> 重启系统(安装后调用)
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

static void putstr(const char *s)
{
    int len = 0;
    while (s[len]) len++;
    syscall3(1, 1, (long)s, len);
}

static void puthex(unsigned int v)
{
    char h[] = "0123456789ABCDEF";
    char buf[12];
    int i = 0;
    buf[i++] = '0'; buf[i++] = 'x';
    if (v == 0) { buf[i++] = '0'; }
    else {
        int started = 0;
        for (int b = 28; b >= 0; b -= 4) {
            int d = (v >> b) & 0xF;
            if (d || started) { buf[i++] = h[d]; started = 1; }
        }
    }
    buf[i] = 0;
    putstr(buf);
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

static int str_eq(const char *a, const char *b)
{
    while (*a && *b) { if (*a != *b) return 0; a++; b++; }
    return *a == *b;
}

static const char *type_name(unsigned char t)
{
    switch (t) {
        case 0x83: return "Linux/ext2";
        case 0x0C: return "FAT32-LBA";
        case 0x07: return "NTFS/exFAT";
        case 0xEE: return "GPT保护";
        case 0x82: return "Linux-swap";
        case 0x05: return "扩展分区";
        default:   return "未知";
    }
}

static void show_mbr(unsigned char *mbr)
{
    putstr("Signature: ");
    puthex((mbr[510] << 8) | mbr[511]);
    putstr("\nPartition table:\n");
    for (int p = 0; p < 4; p++) {
        unsigned char *e = mbr + 446 + p * 16;
        unsigned char type = e[4];
        unsigned int lba = e[8] | (e[9] << 8) | (e[10] << 16) | (e[11] << 24);
        unsigned int sec = e[12] | (e[13] << 8) | (e[14] << 16) | (e[15] << 24);
        putstr("  P");
        putdec(p + 1);
        putstr(": type=");
        puthex(type);
        putstr(" (");
        putstr(type_name(type));
        putstr(") start=");
        putdec(lba);
        putstr(" size=");
        putdec(sec);
        putstr(" sectors\n");
    }
}

void _start(void)
{
    static unsigned char mbr[512];
    /* 参数: 用固定内存区模拟 argv (exec 暂不传参)
     * 简化: create/reboot 通过第二个扇区首字节标记(0xCC=create, 0xCD=reboot) */
    long r = syscall3(26, 0, (long)mbr, 1);
    if (r < 0) {
        putstr("[MBRTOOL] disk_read failed\n");
        syscall3(6, 0, 0, 0);
    }
    if (mbr[510] != 0x55 || mbr[511] != 0xAA) {
        putstr("[MBRTOOL] WARNING: no 0x55AA boot signature\n");
    }
    putstr("[MBRTOOL] Current MBR:\n");
    show_mbr(mbr);

    /* 模式检测: 扇区1首字节标记 (安装向导写入 0xCC 表示请求三区化) */
    static unsigned char cmd[512];
    syscall3(26, 1, (long)cmd, 1);
    if (cmd[0] == 0xCC) {
        putstr("\n[MBRTOOL] create: 三区制写入...\n");
        /* 找到 P1(系统分区=当前 ext2), 保持起始/大小 */
        unsigned int sys_start = 0, sys_sec = 0;
        for (int p = 0; p < 4; p++) {
            unsigned char *e = mbr + 446 + p * 16;
            if (e[4] == 0x83 && (e[8] != 0 || e[9] != 0 || e[10] != 0 || e[11] != 0)) {
                /* V0.13.49: 修复 e[8|0] 与 || 优先级瑕疵 */
                sys_start = e[8] | (e[9] << 8) | (e[10] << 16) | (e[11] << 24);
                sys_sec = e[12] | (e[13] << 8) | (e[14] << 16) | (e[15] << 24);
                break;
            }
        }
        if (sys_sec == 0) {
            putstr("ERROR: no system partition found\n");
            syscall3(6, 0, 0, 0);
        }
        /* 布局: P1=系统(保持), P2=配置(64MB), P3=自由(到磁盘末尾, 安装到真实硬盘时覆盖剩余) */
        unsigned int cfg_sec = 64 * 2048;
        unsigned int p2_start = sys_start + sys_sec;
        unsigned int p3_start = p2_start + cfg_sec;
        unsigned int p3_sec = 0xFFFFFFF0 - p3_start;  /* 尽量大(真实磁盘由安装向导按容量精算) */
        /* 清空分区表 */
        for (int i = 0; i < 64; i++) mbr[446 + i] = 0;
        /* P1: 系统 ext2 可引导 */
        mbr[446 + 0]  = 0x80;
        mbr[446 + 4]  = 0x83;
        mbr[446 + 8]  = sys_start & 0xFF;        mbr[446 + 9]  = (sys_start >> 8) & 0xFF;
        mbr[446 + 10] = (sys_start >> 16) & 0xFF; mbr[446 + 11] = (sys_start >> 24) & 0xFF;
        mbr[446 + 12] = sys_sec & 0xFF;          mbr[446 + 13] = (sys_sec >> 8) & 0xFF;
        mbr[446 + 14] = (sys_sec >> 16) & 0xFF;  mbr[446 + 15] = (sys_sec >> 24) & 0xFF;
        /* P2: 配置分区 ext2 */
        mbr[446 + 16 + 4] = 0x83;
        mbr[446 + 16 + 8]  = p2_start & 0xFF;        mbr[446 + 16 + 9]  = (p2_start >> 8) & 0xFF;
        mbr[446 + 16 + 10] = (p2_start >> 16) & 0xFF; mbr[446 + 16 + 11] = (p2_start >> 24) & 0xFF;
        mbr[446 + 16 + 12] = cfg_sec & 0xFF;          mbr[446 + 16 + 13] = (cfg_sec >> 8) & 0xFF;
        mbr[446 + 16 + 14] = (cfg_sec >> 16) & 0xFF;  mbr[446 + 16 + 15] = (cfg_sec >> 24) & 0xFF;
        /* P3: 自由分区 ext2 */
        mbr[446 + 32 + 4] = 0x83;
        mbr[446 + 32 + 8]  = p3_start & 0xFF;        mbr[446 + 32 + 9]  = (p3_start >> 8) & 0xFF;
        mbr[446 + 32 + 10] = (p3_start >> 16) & 0xFF; mbr[446 + 32 + 11] = (p3_start >> 24) & 0xFF;
        mbr[446 + 32 + 12] = p3_sec & 0xFF;          mbr[446 + 32 + 13] = (p3_sec >> 8) & 0xFF;
        mbr[446 + 32 + 14] = (p3_sec >> 16) & 0xFF;  mbr[446 + 32 + 15] = (p3_sec >> 24) & 0xFF;
        mbr[510] = 0x55; mbr[511] = 0xAA;
        long wr = syscall3(27, 0, (long)mbr, 1);
        if (wr < 0) {
            putstr("ERROR: disk_write failed\n");
            syscall3(6, 0, 0, 0);
        }
        /* 清标记 */
        cmd[0] = 0;
        syscall3(27, 1, (long)cmd, 1);
        putstr("OK: 三区 MBR 写入完成\n");
        /* 重读显示 */
        syscall3(26, 0, (long)mbr, 1);
        show_mbr(mbr);
    } else if (cmd[0] == 0xCD) {
        putstr("\n[MBRTOOL] reboot: 重启系统\n");
        cmd[0] = 0;
        syscall3(27, 1, (long)cmd, 1);
        syscall3(28, 0, 0, 0);
    }
    putstr("[MBRTOOL] done\n");
    syscall3(6, 0, 0, 0);
}
