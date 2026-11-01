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

/* 等待IDE就绪 */
static int ide_wait_ready(void)
{
    for (int i = 0; i < 100000; i++) {
        uint8_t status = inb(IDE_PRIMARY_STATUS);
        if (status & IDE_STATUS_ERR) {
            return -2;  /* 错误 */
        }
        if (!(status & IDE_STATUS_BSY)) {
            return (status & IDE_STATUS_DRDY) ? 0 : -1;
        }
    }
    return -1;
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
    /* 先选择磁盘 */
    outb(IDE_PRIMARY_DRIVE, (ide64_drive ? 0xF0 : 0xE0) | ((lba >> 24) & 0x0F));

    /* 等待所选磁盘就绪 */
    if (ide_wait_ready() != 0) {
        return -1;
    }

    /* 发送LBA地址 */
    outb(IDE_PRIMARY_SECTOR, 1);  /* 读取1个扇区 */
    outb(IDE_PRIMARY_LBA0, (uint8_t)(lba & 0xFF));
    outb(IDE_PRIMARY_LBA1, (uint8_t)((lba >> 8) & 0xFF));
    outb(IDE_PRIMARY_LBA2, (uint8_t)((lba >> 16) & 0xFF));
    /* 再次选择磁盘（有些控制器需要） */
    outb(IDE_PRIMARY_DRIVE, (ide64_drive ? 0xF0 : 0xE0) | ((lba >> 24) & 0x0F));

    /* 发送读命令 */
    outb(IDE_PRIMARY_COMMAND, IDE_CMD_READ);

    /* 等待DRQ（数据请求）位设置 */
    for (int i = 0; i < 100000; i++) {
        uint8_t status = inb(IDE_PRIMARY_STATUS);
        if (status & IDE_STATUS_ERR) return -1;
        if (status & IDE_STATUS_DRQ) break;
        if (!(status & IDE_STATUS_BSY)) return -1;
    }

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

    /* 等待所选磁盘就绪 */
    if (ide_wait_ready() != 0) {
        return -1;
    }

    /* 发送LBA地址 */
    outb(IDE_PRIMARY_SECTOR, 1);  /* 写入1个扇区 */
    outb(IDE_PRIMARY_LBA0, (uint8_t)(lba & 0xFF));
    outb(IDE_PRIMARY_LBA1, (uint8_t)((lba >> 8) & 0xFF));
    outb(IDE_PRIMARY_LBA2, (uint8_t)((lba >> 16) & 0xFF));
    outb(IDE_PRIMARY_DRIVE, (ide64_drive ? 0xF0 : 0xE0) | ((lba >> 24) & 0x0F));

    /* 发送写命令 */
    outb(IDE_PRIMARY_COMMAND, IDE_CMD_WRITE);

    /* 等待DRQ（数据请求）位设置 */
    for (int i = 0; i < 100000; i++) {
        uint8_t status = inb(IDE_PRIMARY_STATUS);
        if (status & IDE_STATUS_ERR) return -1;
        if (status & IDE_STATUS_DRQ) break;
        if (!(status & IDE_STATUS_BSY)) return -1;
    }

    /* 写入数据 */
    for (int i = 0; i < 256; i++) {
        outw(IDE_PRIMARY_DATA, ((const uint16_t *)buffer)[i]);
    }

    /* 刷新缓存 */
    outb(IDE_PRIMARY_COMMAND, 0xE7);  /* FLUSH CACHE */
    ide_wait_ready();

    return 0;
}

/* 读取多个扇区 */
int ide64_read_sectors(uint32_t lba, uint8_t count, uint8_t *buffer)
{
    for (uint8_t i = 0; i < count; i++) {
        if (ide64_read_sector(lba + i, buffer + i * 512) != 0) {
            return -1;
        }
    }
    return 0;
}

/* 写入多个扇区 */
int ide64_write_sectors(uint32_t lba, uint8_t count, const uint8_t *buffer)
{
    for (uint8_t i = 0; i < count; i++) {
        if (ide64_write_sector(lba + i, buffer + i * 512) != 0) {
            return -1;
        }
    }
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
