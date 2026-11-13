/* ============================================================================
 * 赫普OS - 64位IDE驱动（简化版）
 * V0.10.6: 初始实现（PIO模式读写）
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>

/* IDE端口 */
#define IDE_PRIMARY_DATA    0x1F0
#define IDE_PRIMARY_ERROR   0x1F1
#define IDE_PRIMARY_SECTOR  0x1F2
#define IDE_PRIMARY_LBA0    0x1F3
#define IDE_PRIMARY_LBA1    0x1F4
#define IDE_PRIMARY_LBA2    0x1F5
#define IDE_PRIMARY_DRIVE   0x1F6
#define IDE_PRIMARY_STATUS  0x1F7
#define IDE_PRIMARY_COMMAND 0x1F7
#define IDE_PRIMARY_CONTROL 0x3F6

/* IDE状态位 */
#define IDE_STATUS_BSY      0x80
#define IDE_STATUS_DRDY     0x40
#define IDE_STATUS_DF       0x20
#define IDE_STATUS_DRQ      0x08
#define IDE_STATUS_ERR      0x01

/* IDE命令 */
#define IDE_CMD_READ        0x20
#define IDE_CMD_WRITE       0x30
#define IDE_CMD_IDENTIFY    0xEC

/* 磁盘选择：0=master, 1=slave */
static int ide64_drive = 0;

/* 设置当前磁盘 */
void ide64_set_drive(int drive)
{
    ide64_drive = drive;
}

/* 获取当前磁盘 */
int ide64_get_drive(void)
{
    return ide64_drive;
}

/* 外部函数 */
extern void vga_puts(const char *str);
extern void vga_put_hex(uint64_t val);
extern void *memcpy(void *dst, const void *src, size_t n);

/* 端口读写 */
static inline uint8_t inb(uint16_t port)
{
    uint8_t val;
    __asm__ volatile("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static inline void outb(uint16_t port, uint8_t val)
{
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint16_t inw(uint16_t port)
{
    uint16_t val;
    __asm__ volatile("inw %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static inline void outw(uint16_t port, uint16_t val)
{
    __asm__ volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}

/* BUG-13 修复（v0.59.1）：ATA PIO 等待逻辑整体重写。
 *
 * 原实现的三个硬伤：
 *   ① 写完 DRIVE/HEAD 寄存器后立刻读 STATUS —— ATA 规范要求先等 400 ns
 *      （读 4 次 status 即可），否则可能读到切换前那个驱动留下的旧值，
 *      "BSY 一直是 1" 就是这么来的：读到的根本不是新选中驱动器的状态。
 *   ② 等 DRQ 的循环里写着 `if (!(status & BSY)) return -1;` —— BSY 清零
 *      但 DRQ 尚未置位是**合法过渡态**（PIO 读：BSY=1 → BSY=0,DRQ=1），
 *      这里直接判失败，于是偶发"读不出来"。
 *   ③ 只查 ERR(0x01)，不查 DF(0x20) 设备故障位；超时也没有区分。
 *
 * 现在拆成两个语义清晰的等待器，并统一处理错误位。
 */
static void ide_delay_400ns(void)
{
    /* ATA 规范：读 4 次 alternate/fixed status 即可满足 400 ns 建立时间 */
    for (int i = 0; i < 4; i++) {
        (void)inb(IDE_PRIMARY_STATUS);
    }
}

/* 状态位语义：返回 0=就绪, -1=超时, -2=ERR/DF（设备报错） */
static int ide_status_error(uint8_t status)
{
    return (status & (IDE_STATUS_ERR | IDE_STATUS_DF)) ? -2 : 0;
}

/* 等 BSY 清零（不要求 DRDY，有些老盘 DRDY 是滞后置位的） */
static int ide_wait_not_busy(void)
{
    for (int i = 0; i < 200000; i++) {
        uint8_t status = inb(IDE_PRIMARY_STATUS);
        if (ide_status_error(status) != 0) return -2;
        if (!(status & IDE_STATUS_BSY)) return 0;
        __asm__ volatile("pause" ::: "memory");
    }
    return -1;
}

/* 等 BSY=0 且 DRQ=1（PIO 数据阶段的正确判据） */
static int ide_wait_drq(void)
{
    for (int i = 0; i < 200000; i++) {
        uint8_t status = inb(IDE_PRIMARY_STATUS);
        if (ide_status_error(status) != 0) return -2;
        if (!(status & IDE_STATUS_BSY)) {
            if (status & IDE_STATUS_DRQ) return 0;
            /* BSY=0 且 DRQ=0：过渡态，继续等；真失败靠上面的超时兜住 */
        }
        __asm__ volatile("pause" ::: "memory");
    }
    return -1;
}

/* 等待IDE就绪（初始化/命令间用） */
static int ide_wait_ready(void)
{
    ide_delay_400ns();
    return ide_wait_not_busy();
}

/* 初始化IDE */
void ide64_init(void)
{
    vga_puts("[IDE64] Initializing IDE controller...\n");

    /* 选择主驱动器 */
    outb(IDE_PRIMARY_DRIVE, 0xA0);  /* LBA模式, 主驱动器 */

    /* 等待就绪 */
    if (ide_wait_ready() != 0) {
        vga_puts("[IDE64] WARNING: Drive not ready\n");
    } else {
        vga_puts("[IDE64] Drive ready\n");
    }

    vga_puts("[IDE64] IDE controller initialized\n");
}

/* 读取一个扇区（512字节） */
int ide64_read_sector(uint32_t lba, uint8_t *buffer)
{
    /* 先选择磁盘（bit4=0 master / 1 slave，高 4 位放 LBA27-24） */
    outb(IDE_PRIMARY_DRIVE, (ide64_drive ? 0xF0 : 0xE0) | ((lba >> 24) & 0x0F));
    ide_delay_400ns();               /* 切换驱动器必须等 400ns */

    /* 等待所选磁盘就绪 */
    if (ide_wait_not_busy() != 0) {
        return -1;
    }

    /* 发送LBA地址 */
    outb(IDE_PRIMARY_SECTOR, 1);  /* 读取1个扇区 */
    outb(IDE_PRIMARY_LBA0, (uint8_t)(lba & 0xFF));
    outb(IDE_PRIMARY_LBA1, (uint8_t)((lba >> 8) & 0xFF));
    outb(IDE_PRIMARY_LBA2, (uint8_t)((lba >> 16) & 0xFF));
    /* 再次选择磁盘（有些控制器需要），带 LBA27-24 */
    outb(IDE_PRIMARY_DRIVE, (ide64_drive ? 0xF0 : 0xE0) | ((lba >> 24) & 0x0F));
    ide_delay_400ns();

    /* 发送读命令 */
    outb(IDE_PRIMARY_COMMAND, IDE_CMD_READ);

    /* 等 BSY=0 且 DRQ=1（合法的过渡态不再被误判为失败） */
    if (ide_wait_drq() != 0) return -1;

    /* 读取数据（256个16位字 = 512字节） */
    for (int i = 0; i < 256; i++) {
        ((uint16_t *)buffer)[i] = inw(IDE_PRIMARY_DATA);
    }

    return 0;
}

/* 写入一个扇区 */
int ide64_write_sector(uint32_t lba, const uint8_t *buffer)
{
    /* 先选择磁盘 */
    outb(IDE_PRIMARY_DRIVE, (ide64_drive ? 0xF0 : 0xE0) | ((lba >> 24) & 0x0F));
    ide_delay_400ns();

    /* 等待所选磁盘就绪 */
    if (ide_wait_not_busy() != 0) {
        return -1;
    }

    /* 发送LBA地址 */
    outb(IDE_PRIMARY_SECTOR, 1);  /* 写入1个扇区 */
    outb(IDE_PRIMARY_LBA0, (uint8_t)(lba & 0xFF));
    outb(IDE_PRIMARY_LBA1, (uint8_t)((lba >> 8) & 0xFF));
    outb(IDE_PRIMARY_LBA2, (uint8_t)((lba >> 16) & 0xFF));
    outb(IDE_PRIMARY_DRIVE, (ide64_drive ? 0xF0 : 0xE0) | ((lba >> 24) & 0x0F));
    ide_delay_400ns();

    /* 发送写命令 */
    outb(IDE_PRIMARY_COMMAND, IDE_CMD_WRITE);

    /* 等 BSY=0 且 DRQ=1 */
    if (ide_wait_drq() != 0) return -1;

    /* 写入数据 */
    for (int i = 0; i < 256; i++) {
        outw(IDE_PRIMARY_DATA, ((const uint16_t *)buffer)[i]);
    }

    /* 刷新缓存 */
    outb(IDE_PRIMARY_COMMAND, 0xE7);  /* FLUSH CACHE */
    ide_wait_ready();

    return 0;
}

/* 读取多个扇区
 * BUG-13 附带优化：PIO 支持一条命令读多个扇区（SECTOR COUNT = 0x1F2）。
 * 原来逐扇区发 READ 命令，ext2 读一个文件要发几十上百条命令，每次还要
 * 等一轮 BSY/DRQ；改成一条命令连续读，命令数降为 1，加载明显变快。
 * 上限 255 扇区（寄存器 8 位，0 表示 256，这里不碰那个边界）。 */
int ide64_read_sectors(uint32_t lba, uint8_t count, uint8_t *buffer)
{
    if (count == 0) return 0;
    if (count == 1) return ide64_read_sector(lba, buffer);

    outb(IDE_PRIMARY_DRIVE, (ide64_drive ? 0xF0 : 0xE0) | ((lba >> 24) & 0x0F));
    ide_delay_400ns();
    if (ide_wait_not_busy() != 0) return -1;

    outb(IDE_PRIMARY_SECTOR, count);
    outb(IDE_PRIMARY_LBA0, (uint8_t)(lba & 0xFF));
    outb(IDE_PRIMARY_LBA1, (uint8_t)((lba >> 8) & 0xFF));
    outb(IDE_PRIMARY_LBA2, (uint8_t)((lba >> 16) & 0xFF));
    outb(IDE_PRIMARY_DRIVE, (ide64_drive ? 0xF0 : 0xE0) | ((lba >> 24) & 0x0F));
    ide_delay_400ns();

    outb(IDE_PRIMARY_COMMAND, IDE_CMD_READ);

    for (int s = 0; s < count; s++) {
        if (ide_wait_drq() != 0) return -1;
        uint16_t *dst = (uint16_t *)(buffer + (uint64_t)s * 512);
        for (int i = 0; i < 256; i++) dst[i] = inw(IDE_PRIMARY_DATA);
    }
    return 0;
}

/* 写入多个扇区（同样一条命令写完，写后 FLUSH CACHE） */
int ide64_write_sectors(uint32_t lba, uint8_t count, const uint8_t *buffer)
{
    if (count == 0) return 0;
    if (count == 1) return ide64_write_sector(lba, buffer);

    outb(IDE_PRIMARY_DRIVE, (ide64_drive ? 0xF0 : 0xE0) | ((lba >> 24) & 0x0F));
    ide_delay_400ns();
    if (ide_wait_not_busy() != 0) return -1;

    outb(IDE_PRIMARY_SECTOR, count);
    outb(IDE_PRIMARY_LBA0, (uint8_t)(lba & 0xFF));
    outb(IDE_PRIMARY_LBA1, (uint8_t)((lba >> 8) & 0xFF));
    outb(IDE_PRIMARY_LBA2, (uint8_t)((lba >> 16) & 0xFF));
    outb(IDE_PRIMARY_DRIVE, (ide64_drive ? 0xF0 : 0xE0) | ((lba >> 24) & 0x0F));
    ide_delay_400ns();

    outb(IDE_PRIMARY_COMMAND, IDE_CMD_WRITE);

    for (int s = 0; s < count; s++) {
        if (ide_wait_drq() != 0) return -1;
        const uint16_t *src = (const uint16_t *)(buffer + (uint64_t)s * 512);
        for (int i = 0; i < 256; i++) outw(IDE_PRIMARY_DATA, src[i]);
    }
    outb(IDE_PRIMARY_COMMAND, 0xE7);   /* FLUSH CACHE */
    if (ide_wait_not_busy() != 0) return -1;
    return 0;
}

/* 测试IDE */
void ide64_test(void)
{
    vga_puts("[IDE64] Testing IDE read...\n");

    uint8_t buffer[512];
    if (ide64_read_sector(0, buffer) == 0) {
        vga_puts("[IDE64] Read sector 0 OK\n");
        vga_puts("[IDE64] First bytes: ");
        for (int i = 0; i < 8; i++) {
            vga_put_hex(buffer[i]);
            vga_puts(" ");
        }
        vga_puts("\n");
    } else {
        vga_puts("[IDE64] Read sector 0 FAILED\n");
    }
}

/* OPEN-01 取证：高 LBA 读写回环自检。
 *
 * 历史记录说「QEMU 高 LBA(P2/P3) 写成功读全 0」，一直是"验证中"。这个
 * 自检直接在选定的盘上挑几个跨越 28 位边界附近和常规区的 LBA：
 * 写入递增 pattern -> FLUSH -> 读回逐字节比对。
 * 目的不是猜，而是把结论落在数据上：通过就关闭 OPEN-01，失败就把
 * 出错 LBA 与首个差异字节打出来。
 * 注意：只在调用方指定的盘上跑（用 slave 盘做测试，别写坏系统盘）。
 */
int ide64_selftest_high_lba(int drive, uint64_t n_lba, const uint32_t *lbas)
{
    uint8_t wr[512], rd[512];
    int saved = ide64_drive;
    ide64_set_drive(drive);
    int fails = 0;

    for (uint64_t k = 0; k < n_lba; k++) {
        uint32_t lba = lbas[k];
        for (int i = 0; i < 512; i++) wr[i] = (uint8_t)((lba + (uint64_t)i * 7u) & 0xFF);
        if (ide64_write_sector(lba, wr) != 0) {
            vga_puts("[ATA] write ERR lba="); vga_put_hex(lba); vga_puts("\n");
            fails++; continue;
        }
        for (int i = 0; i < 512; i++) rd[i] = 0;
        if (ide64_read_sector(lba, rd) != 0) {
            vga_puts("[ATA] read ERR lba="); vga_put_hex(lba); vga_puts("\n");
            fails++; continue;
        }
        int first = -1;
        for (int i = 0; i < 512; i++) { if (rd[i] != wr[i]) { first = i; break; } }
        if (first >= 0) {
            vga_puts("[ATA] mismatch lba="); vga_put_hex(lba);
            vga_puts(" first_off="); vga_put_hex((uint64_t)first);
            vga_puts(" want="); vga_put_hex(wr[first]);
            vga_puts(" got="); vga_put_hex(rd[first]);
            vga_puts("\n");
            fails++;
        }
    }
    /* 多扇区回环：验证一条命令读/写多个扇区（BUG-13 的附带改动） */
    {
        static uint8_t mw[8 * 512], mr[8 * 512];
        uint32_t base = 0x10000u;
        for (int i = 0; i < 8 * 512; i++) mw[i] = (uint8_t)((i * 13u + 0x5Au) & 0xFF);
        for (int i = 0; i < 8 * 512; i++) mr[i] = 0;
        if (ide64_write_sectors(base, 8, mw) != 0) {
            vga_puts("[ATA] multi-sector write ERR\n"); fails++;
        } else if (ide64_read_sectors(base, 8, mr) != 0) {
            vga_puts("[ATA] multi-sector read ERR\n"); fails++;
        } else {
            int first = -1;
            for (int i = 0; i < 8 * 512; i++) { if (mr[i] != mw[i]) { first = i; break; } }
            if (first >= 0) {
                vga_puts("[ATA] multi-sector mismatch off=");
                vga_put_hex((uint64_t)first);
                vga_puts("\n");
                fails++;
            }
        }
    }

    ide64_set_drive(saved);
    vga_puts("[ATA] high-lba selftest drive="); vga_put_hex((uint64_t)drive);
    vga_puts(" cases="); vga_put_hex(n_lba);
    vga_puts(" fails="); vga_put_hex((uint64_t)fails);
    vga_puts("\n");
    return fails;
}

/* 磁盘容量信息 */
static uint64_t ide64_drive_capacity[2] = {0, 0};  /* master/slave扇区数 */
static int ide64_drive_detected[2] = {0, 0};

/* IDENTIFY DEVICE - 获取磁盘信息 */
static int ide64_identify(int drive, uint16_t *identify_data)
{
    /* 选择磁盘 */
    outb(IDE_PRIMARY_DRIVE, drive ? 0xF0 : 0xE0);

    /* 等待就绪 */
    if (ide_wait_ready() != 0) {
        return -1;
    }

    /* 发送IDENTIFY命令 */
    outb(IDE_PRIMARY_COMMAND, IDE_CMD_IDENTIFY);

    /* 等待数据就绪 */
    for (int i = 0; i < 100000; i++) {
        uint8_t status = inb(IDE_PRIMARY_STATUS);
        if (status & IDE_STATUS_ERR) {
            return -1;  /* 不支持IDENTIFY（可能是ATAPI） */
        }
        if (status & IDE_STATUS_DRDY) {
            break;
        }
    }

    /* 读取512字节识别数据 */
    for (int i = 0; i < 256; i++) {
        identify_data[i] = inw(IDE_PRIMARY_DATA);
    }

    return 0;
}

/* 检测磁盘容量 */
uint64_t ide64_get_capacity(int drive)
{
    if (drive < 0 || drive > 1) return 0;
    if (ide64_drive_detected[drive]) {
        return ide64_drive_capacity[drive];
    }

    uint16_t identify[256];
    if (ide64_identify(drive, identify) != 0) {
        vga_puts("[IDE64] Drive ");
        vga_puts(drive ? "slave" : "master");
        vga_puts(": not detected\n");
        return 0;
    }

    /* LBA28容量：word 60-61 */
    uint32_t lba28_sectors = identify[60] | ((uint32_t)identify[61] << 16);

    /* LBA48容量：word 100-103 */
    uint64_t lba48_sectors = 0;
    if (identify[83] & 0x400) {  /* 支持LBA48 */
        lba48_sectors = (uint64_t)identify[100] |
                        ((uint64_t)identify[101] << 16) |
                        ((uint64_t)identify[102] << 32) |
                        ((uint64_t)identify[103] << 48);
    }

    uint64_t capacity = lba48_sectors ? lba48_sectors : lba28_sectors;
    ide64_drive_capacity[drive] = capacity;
    ide64_drive_detected[drive] = 1;

    vga_puts("[IDE64] Drive ");
    vga_puts(drive ? "slave" : "master");
    vga_puts(": ");
    char buf[16];
    uint64_t mb = capacity / 2048;  /* 扇区→MB（512字节/扇区） */
    int i = 0;
    if (mb == 0) buf[i++] = '0';
    while (mb > 0) { buf[i++] = '0' + mb % 10; mb /= 10; }
    buf[i] = 0;
    for (int j = 0; j < i / 2; j++) {
        char t = buf[j]; buf[j] = buf[i-1-j]; buf[i-1-j] = t;
    }
    vga_puts(buf);
    vga_puts("MB (");
    vga_puts(lba48_sectors ? "LBA48" : "LBA28");
    vga_puts(")\n");

    return capacity;
}

/* 获取磁盘容量（GB） */
uint64_t ide64_get_capacity_gb(int drive)
{
    uint64_t sectors = ide64_get_capacity(drive);
    return sectors / 2097152;  /* 1GB = 2097152扇区 */
}
