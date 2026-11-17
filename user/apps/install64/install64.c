/* install64.elf - 赫普OS 64位 系统安装向导
 *
 * 流程: 读源盘分区表 -> 菜单(试用/一键安装/自定义) -> 克隆引导链(MBR+GPT+ESP) ->
 *       源盘 rootfs 原样扇区拷进目标盘 P1 -> 格式化 P2/P3 -> 重启。
 *
 * OPEN-04 修复要点:
 *  - syscall 号改用内核唯一真源 kernel/include/syscall.h 宏(旧版硬编码 1/2/6/26/27/28/31
 *    与 64 位分发表整体错位, 一启动就是 "Unimplemented syscall")。
 *  - 裸盘读写 / mkfs / reboot 由内核侧安装器白名单放行(见 user_process.c)。
 *  - 引导链与 rootfs 一律写目标盘 drive1(旧版 MBR 误写回源盘 drive0)。
 *  - UEFI 启动链(保护性 MBR + GPT 主/备表 + ESP FAT32: BOOTX64.EFI/KERNEL64.BIN/
 *    HEPBOOT.CFG)与源盘 rootfs ext2 一并按扇区克隆到目标盘对应 LBA; P1 保留拷入的
 *    完整 rootfs, 不再 mkfs。
 *  - 空白目标盘(MBR 无 0x55AA 签名)视为无人值守安装介质, 自动走一键安装; 已有系统
 *    的盘才进交互菜单。
 */
#include <stdint.h>
#include <stddef.h>
#include "syscall.h"   /* SYS_READ/WRITE/EXIT/DISK_READ/DISK_WRITE/REBOOT/MKFS */

/* UEFI 镜像布局(与 tools/build_uefi_dual.py 一致) */
#define DISK_SECTORS   524288u    /* 256MiB / 512B */
#define ESP_START      2048u      /* ESP(FAT32) 起始 LBA, 1MiB 对齐 */
#define ESP_SECTORS    262144u    /* 128MiB */
#define GPT_PRIM_LBA   1u         /* GPT 主表: 头 LBA1 + 条目 LBA2..33 */
#define GPT_PRIM_SECT  33u
#define GPT_BAK_LBA    524256u    /* 备份条目 + 备份头(盘尾 33 扇区内) */
#define GPT_BAK_SECT   32u

static long syscall1(long n, long a)
{
    long r;
    __asm__ volatile("syscall" : "=a"(r) : "a"(n), "D"(a)
                     : "rcx", "r11", "memory");
    return r;
}

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

static long syscall5(long n, long a, long b, long c, long d, long e)
{
    long r;
    register long r10 asm("r10") = d;
    register long r8 asm("r8") = e;
    __asm__ volatile("syscall"
                     : "=a"(r)
                     : "a"(n), "D"(a), "S"(b), "d"(c), "r"(r10), "r"(r8)
                     : "rcx", "r11", "memory");
    return r;
}

/* ---- 磁盘/格式化/重启封装（号取自 syscall.h，drive: 0=源盘 1=目标盘） ---- */
static long disk_read(uint32_t lba, void *buf, uint32_t count, int drive)
{
    return syscall4(SYS_DISK_READ, (long)lba, (long)buf, (long)count, drive);
}

static long disk_write(uint32_t lba, const void *buf, uint32_t count, int drive)
{
    return syscall4(SYS_DISK_WRITE, (long)lba, (long)buf, (long)count, drive);
}

static long mkfs_part(uint32_t lba, uint32_t sectors, const char *label, int drive)
{
    return syscall5(SYS_MKFS, (long)lba, (long)sectors, (long)label,
                    0x4D4B4653u, drive);
}

static void system_reboot(void)
{
    syscall1(SYS_REBOOT, 0);
}

/* 协作式让出 CPU: 整盘拷贝是长任务, 每批 yield 一次, 避免被未响应看门狗误杀。 */
static void yield_cpu(void)
{
    syscall1(SYS_LNX64_SCHED_YIELD, 0);
}

static void putstr(const char *s)
{
    size_t len = 0;
    while (s[len]) len++;
    syscall3(SYS_WRITE, 1, (long)s, (long)len);
}

static void putchar(char c)
{
    syscall3(SYS_WRITE, 1, (long)&c, 1);
}

static void puthex(unsigned int v)
{
    const char h[] = "0123456789ABCDEF";
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

static void menu(void)
{
    putstr("\n===== 赫普OS 64位 安装向导 =====\n");
    putstr("  1. 试用系统（不写盘）\n");
    putstr("  2. 一键安装（推荐）\n");
    putstr("  3. 自定义分区安装\n");
    putstr("请输入选择: ");
}

static char read_key(void)
{
    char c = 0;
    while (1) {
        long r = syscall3(SYS_READ, 0, (long)&c, 1);
        if (r > 0) break;
    }
    return c;
}

/* 源盘根分区(0x83)位置，从源盘 MBR 解析得到 */
static uint32_t sys_start = 0;
static uint32_t sys_sec = 0;

/* 把源盘 drive0 的 [src_lba, src_lba+secs) 扇区原样克隆到目标盘 drive1 同布局。
 * 全零批跳过写盘: ESP 大部分簇为空, 逐批 FLUSH CACHE 是主要耗时, 空簇对端本就
 * 全零, 不必回写。 */
static int clone_region(uint32_t src_lba, uint32_t dst_lba, uint32_t secs)
{
    static unsigned char secbuf[32768];   /* 64 扇区一批 */
    uint32_t off = 0;
    while (off < secs) {
        uint32_t cnt = secs - off; if (cnt > 64) cnt = 64;
        if (disk_read(src_lba + off, secbuf, cnt, 0) < 0) {
            putstr("ERROR: 读源盘区域失败 LBA="); putdec(src_lba + off); putstr("\n");
            return -1;
        }
        int nz = 0;
        for (uint32_t b = 0; b < cnt * 512; b++) {
            if (secbuf[b]) { nz = 1; break; }
        }
        if (nz && disk_write(dst_lba + off, secbuf, cnt, 1) < 0) {
            putstr("ERROR: 写目标盘区域失败 LBA="); putdec(dst_lba + off); putstr("\n");
            return -1;
        }
        off += cnt;
        yield_cpu();
    }
    return 0;
}

int main(void)
{
    unsigned char mbr[512];

    putstr("\n赫普OS 64位 安装程序 (OPEN-04)\n");

    /* [1/5] 读源盘 MBR，打印分区，定位根分区 */
    putstr("\n[1/5] 读取源盘分区表...\n");
    if (disk_read(0, mbr, 1, 0) < 0) {
        putstr("ERROR: 读取MBR失败\n");
        syscall1(SYS_EXIT, 1);
    }
    if (mbr[510] != 0x55 || mbr[511] != 0xAA) {
        putstr("ERROR: MBR 签名无效\n");
        syscall1(SYS_EXIT, 1);
    }
    for (int i = 0; i < 4; i++) {
        unsigned char type = mbr[446 + i * 16 + 4];
        uint32_t lba = (uint32_t)mbr[446 + i * 16 + 8]
                     | ((uint32_t)mbr[446 + i * 16 + 9] << 8)
                     | ((uint32_t)mbr[446 + i * 16 + 10] << 16)
                     | ((uint32_t)mbr[446 + i * 16 + 11] << 24);
        uint32_t secs = (uint32_t)mbr[446 + i * 16 + 12]
                     | ((uint32_t)mbr[446 + i * 16 + 13] << 8)
                     | ((uint32_t)mbr[446 + i * 16 + 14] << 16)
                     | ((uint32_t)mbr[446 + i * 16 + 15] << 24);
        putstr("  分区 "); putchar('1' + i);
        putstr(": 类型="); puthex(type);
        putstr(" LBA="); putdec(lba);
        putstr(" 扇区="); putdec(secs);
        putstr("\n");
        if (type == 0x83 && sys_sec == 0) {
            sys_start = lba;
            sys_sec = secs;
        }
    }
    if (sys_sec == 0) {
        putstr("WARN: 源盘未找到 0x83 根分区，rootfs 拷贝将跳过\n");
    }

    /* 探测目标盘是否为空白盘: 无 0x55AA 签名即视为无人值守安装, 自动一键安装。 */
    unsigned char tb[512];
    int blank_target = 1;
    if (disk_read(0, tb, 1, 1) == 0 && tb[510] == 0x55 && tb[511] == 0xAA) {
        blank_target = 0;
    }

    /* 菜单循环: 已有系统的盘才等待交互选择; 空白盘自动选 2。 */
    char choice = 0;
    if (blank_target) {
        choice = '2';
        putstr("[AUTO] 目标盘为空白盘, 自动选择一键安装\n");
    } else {
        for (;;) {
            menu();
            choice = read_key();
            putchar(choice);
            putchar('\n');
            if (choice == '1' || choice == '2' || choice == '3') break;
            putstr("无效选择，请重试。\n");
        }
    }
    if (choice == '1') {
        putstr("\n试用模式: 返回系统，不写盘。\n");
        syscall1(SYS_EXIT, 0);
    }

    /* [2/5] 目标盘分区布局: P1 与源盘根分区同位; 剩余空间二分给 P2/P3, 不越盘尾。 */
    uint32_t p1_start = sys_start;
    uint32_t sys_sec2 = sys_sec;
    uint32_t p2_start = p1_start + sys_sec2;
    /* 盘尾 33 扇区是 GPT 备份表, P2/P3 不得进入, 否则 mkfs 会覆盖备份 GPT。 */
    uint32_t remaining = GPT_BAK_LBA - p2_start;
    uint32_t p2_sec = remaining / 2;
    uint32_t p3_start = p2_start + p2_sec;
    uint32_t p3_sec = remaining - p2_sec;

    putstr("\n[2/5] 目标盘分区布局:\n");
    putstr("  P1(系统) LBA="); putdec(p1_start);  putstr(" 扇区="); putdec(sys_sec2); putstr("\n");
    putstr("  P2(配置) LBA="); putdec(p2_start);  putstr(" 扇区="); putdec(p2_sec);  putstr("\n");
    putstr("  P3(自由) LBA="); putdec(p3_start);  putstr(" 扇区="); putdec(p3_sec);   putstr("\n");

    /* [3/5] 克隆 UEFI 引导链到目标盘 drive1:
     *   LBA0      保护性/混合 MBR
     *   LBA1..33  GPT 主表(头 + 条目)
     *   LBA2048.. ESP FAT32(BOOTX64.EFI + KERNEL64.BIN + HEPBOOT.CFG)
     *   盘尾      GPT 备份表
     * 说明: 本镜像为 UEFI 引导, 旧版从 LBA34/LBA2000 拷 legacy stage2/kernel 的区域
     * 在 GPT/FAT 盘上并非有效 legacy 引导扇区, 拷过去也无法 BIOS 启动, 故改为整段克隆
     * GPT+ESP。 */
    putstr("\n[3/5] 克隆引导链(MBR + GPT + ESP) ...\n");
    if (clone_region(0, 0, 1) < 0) { putstr("ERROR: 克隆 MBR 失败\n"); syscall1(SYS_EXIT, 1); }
    putstr("  MBR 已克隆。\n");
    if (clone_region(GPT_PRIM_LBA, GPT_PRIM_LBA, GPT_PRIM_SECT) < 0) {
        putstr("ERROR: 克隆 GPT 主表失败\n"); syscall1(SYS_EXIT, 1);
    }
    putstr("  GPT 主表已克隆。\n");
    if (clone_region(ESP_START, ESP_START, ESP_SECTORS) < 0) {
        putstr("ERROR: 克隆 ESP 失败\n"); syscall1(SYS_EXIT, 1);
    }
    putstr("  ESP 已克隆。\n");
    if (clone_region(GPT_BAK_LBA, GPT_BAK_LBA, GPT_BAK_SECT) < 0) {
        putstr("WARN: 克隆 GPT 备份表失败\n");
    } else {
        putstr("  GPT 备份表已克隆。\n");
    }

    /* [4/5] 源盘根分区 rootfs ext2 按扇区原样拷进目标盘 P1。
     * 拷入的是本次构建打进安装介质的 rootfs(/bin 程序/系统配置/字体/壁纸与目录结构);
     * P1 保留这份 ext2, 不再 mkfs。 */
    putstr("\n[4/5] 拷贝 rootfs 到 P1 ...\n");
    uint32_t copy_sec = sys_sec;
    if (sys_sec2 < copy_sec) copy_sec = sys_sec2;
    if (copy_sec == 0) {
        putstr("ERROR: 源盘根分区大小为 0，无法拷贝 rootfs\n");
    } else if (clone_region(sys_start, p1_start, copy_sec) < 0) {
        putstr("ERROR: rootfs 拷贝失败\n");
    } else {
        putstr("rootfs 拷贝完成, 扇区="); putdec(copy_sec); putstr("\n");
    }

    /* [5/5] 格式化 P2(配置) / P3(自由); P1 已含 rootfs, 不格式化。 */
    putstr("\n[5/5] 格式化 P2/P3 ...\n");
    if (mkfs_part(p2_start, p2_sec, "P2", 1) < 0) {
        putstr("P2 mkfs 失败\n");
    } else {
        putstr("P2 格式化完成\n");
    }
    if (mkfs_part(p3_start, p3_sec, "P3", 1) < 0) {
        putstr("P3 mkfs 失败\n");
    } else {
        putstr("P3 格式化完成\n");
    }

    putstr("\n安装完成，即将重启...\n");
    system_reboot();
    for (;;) { }
    return 0;
}

/* user.ld 入口 = _start; 包一层调用 main 并按返回值退出。 */
void _start(void)
{
    int rc = main();
    syscall1(SYS_EXIT, rc);
    for (;;) { }
}
