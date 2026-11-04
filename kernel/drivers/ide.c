/* ============================================================================
 * 赫普OS - IDE磁盘驱动 (PIO模式)
 * 支持LBA28寻址，PIO读写
 * ========================================================================== */

#include <stdint.h>
#include "ide.h"
#include "vga.h"
#include "string.h"
#include "serial.h"

static ide_drive_t drives[MAX_IDE_DRIVES];
static int ide_initialized = 0;

/* V6.49: 当前驱动器索引（-1=自动选择第一个可用驱动器）
 * 用于支持多硬盘场景（如系统盘+ext2数据盘） */
static int current_drive_index = -1;

/* 设置当前驱动器（-1=自动选择第一个可用驱动器） */
void ide_set_current_drive(int index)
{
    if (index < -1 || index >= MAX_IDE_DRIVES) return;
    current_drive_index = index;
}

/* 获取当前驱动器指针（根据current_drive_index选择） */
static ide_drive_t *ide_get_current_drive(void)
{
    if (current_drive_index >= 0 && current_drive_index < MAX_IDE_DRIVES) {
        if (drives[current_drive_index].present)
            return &drives[current_drive_index];
    }
    /* 自动选择第一个可用驱动器 */
    for (int i = 0; i < MAX_IDE_DRIVES; i++) {
        if (drives[i].present)
            return &drives[i];
    }
    return NULL;
}

/* 端口IO */
static inline uint8_t inb(uint16_t port)
{
    uint8_t ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outb(uint16_t port, uint8_t val)
{
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint16_t inw(uint16_t port)
{
    uint16_t ret;
    __asm__ volatile("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outw(uint16_t port, uint16_t val)
{
    __asm__ volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}

/* 等待IDE控制器就绪 (V4.02-E47: 忙等让步——每64次pause) */
static int ide_wait_ready(uint16_t io_base, int timeout)
{
    uint8_t status;
    extern void iopolicy_wait_start(int);
    extern void iopolicy_relax(int);
    iopolicy_wait_start(1);
    while (timeout-- > 0) {
        status = inb(io_base + IDE_REG_STATUS);
        iopolicy_relax(1);
        if (!(status & IDE_STATUS_BUSY)) {
            if (status & IDE_STATUS_DRDY) return 0;
            if (status & IDE_STATUS_ERR) return -1;
        }
    }
    return -1;
}

/* 等待数据请求 (V4.02-E47: 忙等让步) */
static int ide_wait_drq(uint16_t io_base, int timeout)
{
    uint8_t status;
    extern void iopolicy_wait_start(int);
    extern void iopolicy_relax(int);
    iopolicy_wait_start(1);
    while (timeout-- > 0) {
        status = inb(io_base + IDE_REG_STATUS);
        iopolicy_relax(1);
        if (status & IDE_STATUS_DRQ) return 0;
        if (status & IDE_STATUS_ERR) return -1;
    }
    return -1;
}

/* 选择驱动器 */
static void ide_select_drive(uint16_t io_base, int drive)
{
    outb(io_base + IDE_REG_DRIVE, 0xA0 | (drive << 4));
}

/* 读取扇区 (PIO)
 * V2.26修复: 多扇区读必须"每扇区等DRQ→读256words", 原实现只等一次DRQ
 * 就连续读count*256 words, 会超前设备就绪读到垃圾 → 大扇区批量读失败. */
static void ide_read_pio(uint16_t io_base, uint8_t *buffer, uint32_t count)
{
    uint16_t *buf = (uint16_t *)buffer;
    for (uint32_t s = 0; s < count; s++) {
        /* 等本扇区 DRQ */
        for (int i = 0; i < 10000; i++) {
            uint8_t st = inb(io_base + IDE_REG_STATUS);
            if (st & IDE_STATUS_DRQ) break;
            if (st & IDE_STATUS_ERR) return;
        }
        for (int w = 0; w < 256; w++) {
            buf[s * 256 + w] = inw(io_base + IDE_REG_DATA);
        }
    }
}

/* 写入扇区 (PIO)
 * V2.26修复: 多扇区写必须"每扇区等DRQ→写256words", 与读对称. */
static void ide_write_pio(uint16_t io_base, const uint8_t *buffer, uint32_t count)
{
    const uint16_t *buf = (const uint16_t *)buffer;
    for (uint32_t s = 0; s < count; s++) {
        /* 等本扇区 DRQ */
        for (int i = 0; i < 10000; i++) {
            uint8_t st = inb(io_base + IDE_REG_STATUS);
            if (st & IDE_STATUS_DRQ) break;
            if (st & IDE_STATUS_ERR) return;
        }
        for (int w = 0; w < 256; w++) {
            outw(io_base + IDE_REG_DATA, buf[s * 256 + w]);
        }
    }
}

/* IDE识别命令 */
int ide_identify(int channel, int drive, ide_drive_t *info)
{
    uint16_t io_base = (channel == 0) ? IDE_PRIMARY_IO : IDE_SECONDARY_IO;

    ide_select_drive(io_base, drive);
    if (ide_wait_ready(io_base, 40000) != 0) return -1;

    /* 发送IDENTIFY命令 */
    outb(io_base + IDE_REG_COMMAND, IDE_CMD_IDENTIFY);

    if (ide_wait_drq(io_base, 40000) != 0) return -1;

    /* 读取识别数据 */
    uint16_t identify[256];
    for (int i = 0; i < 256; i++) {
        identify[i] = inw(io_base + IDE_REG_DATA);
    }

    if (info) {
        info->present = 1;
        info->channel = channel;
        info->drive = drive;
        info->total_sectors = identify[60] | (identify[61] << 16);
        info->lba_support = (identify[49] & 0x0200) ? 1 : 0;

        /* 解析型号 (字节交换) */
        for (int i = 0; i < 20; i += 2) {
            info->model[i] = identify[27 + i/2] >> 8;
            info->model[i+1] = identify[27 + i/2] & 0xFF;
        }
        info->model[40] = 0;

        /* 解析序列号 */
        for (int i = 0; i < 10; i += 2) {
            info->serial[i] = identify[10 + i/2] >> 8;
            info->serial[i+1] = identify[10 + i/2] & 0xFF;
        }
        info->serial[20] = 0;
    }

    return 0;
}

void ide_init(void)
{
    memset(drives, 0, sizeof(drives));
    ide_initialized = 1;

    vga_puts("[IDE] init IDE controller ...\n");

    /* 探测4个驱动器 */
    int found = 0;
    for (int ch = 0; ch < 2; ch++) {
        for (int dr = 0; dr < 2; dr++) {
            int idx = ch * 2 + dr;
            if (ide_identify(ch, dr, &drives[idx]) == 0) {
                vga_printf("[IDE] drive found %d: %s(%d sectors)\n",
                           idx, drives[idx].model, drives[idx].total_sectors);
                serial_puts(0x3F8, "[IDE] drive found: ");
                serial_puts(0x3F8, drives[idx].model);
                serial_putc(0x3F8, '\n');
                found++;
            }
        }
    }

    if (found == 0) {
        vga_puts("[IDE] not found IDE drive (QEMU/ VM env may need config )\n");
    } else {
        vga_printf("[IDE] found total %d drive \n", found);
    }
}

/* V6.99: 查询IDE是否已初始化 */
int ide_is_initialized(void)
{
    return ide_initialized;
}

/* 读取扇区 */
int ide_read_sectors(uint32_t lba, uint8_t *buffer, uint32_t count)
{
    /* V3.21: LBA28 上限检查 + count 分块（IDE 单命令 SECCOUNT 上限 255） */
    if (!ide_initialized || !buffer || count == 0) return -1;
    if (lba > 0x0FFFFFFF) return -1;
    if (count > 255) {
        uint32_t done = 0;
        while (done < count) {
            uint32_t chunk = count - done;
            if (chunk > 255) chunk = 255;
            if (ide_read_sectors(lba + done, buffer + done * 512, chunk) != (int)chunk)
                return -1;
            done += chunk;
        }
        return (int)count;
    }

    /* 使用当前驱动器（V6.49: 支持指定驱动器，默认自动选择第一个可用） */
    ide_drive_t *drive = ide_get_current_drive();
    if (!drive) return -1;

    uint16_t io_base = (drive->channel == 0) ? IDE_PRIMARY_IO : IDE_SECONDARY_IO;

    ide_select_drive(io_base, drive->drive);
    if (ide_wait_ready(io_base, 40000) != 0) {
        serial_puts(0x3F8, "[IDE] read: wait_ready FAIL\n");
        return -1;
    }

    /* 设置LBA地址 */
    outb(io_base + IDE_REG_SECCOUNT, count & 0xFF);
    outb(io_base + IDE_REG_LBA0, lba & 0xFF);
    outb(io_base + IDE_REG_LBA1, (lba >> 8) & 0xFF);
    outb(io_base + IDE_REG_LBA2, (lba >> 16) & 0xFF);
    outb(io_base + IDE_REG_DRIVE, 0xE0 | (drive->drive << 4) | ((lba >> 24) & 0x0F));

    /* 发送读命令 */
    outb(io_base + IDE_REG_COMMAND, IDE_CMD_READ_PIO);

    if (ide_wait_drq(io_base, 40000) != 0) {
        serial_puts(0x3F8, "[IDE] read: wait_drq FAIL\n");
        return -1;
    }

    /* 读取数据 */
    ide_read_pio(io_base, buffer, count);

    return count;
}

/* 写入扇区 */
int ide_write_sectors(uint32_t lba, const uint8_t *buffer, uint32_t count)
{
    /* V3.21: LBA28 上限检查 + count 分块（与读函数对称） */
    if (!ide_initialized || !buffer || count == 0) return -1;
    if (lba > 0x0FFFFFFF) return -1;
    if (count > 255) {
        uint32_t done = 0;
        while (done < count) {
            uint32_t chunk = count - done;
            if (chunk > 255) chunk = 255;
            if (ide_write_sectors(lba + done, buffer + done * 512, chunk) != (int)chunk)
                return -1;
            done += chunk;
        }
        return (int)count;
    }

    /* 使用当前驱动器（V6.49: 支持指定驱动器，默认自动选择第一个可用） */
    ide_drive_t *drive = ide_get_current_drive();
    if (!drive) return -1;

    uint16_t io_base = (drive->channel == 0) ? IDE_PRIMARY_IO : IDE_SECONDARY_IO;

    ide_select_drive(io_base, drive->drive);
    if (ide_wait_ready(io_base, 40000) != 0) return -1;

    /* 设置LBA地址 */
    outb(io_base + IDE_REG_SECCOUNT, count & 0xFF);
    outb(io_base + IDE_REG_LBA0, lba & 0xFF);
    outb(io_base + IDE_REG_LBA1, (lba >> 8) & 0xFF);
    outb(io_base + IDE_REG_LBA2, (lba >> 16) & 0xFF);
    outb(io_base + IDE_REG_DRIVE, 0xE0 | (drive->drive << 4) | ((lba >> 24) & 0x0F));

    /* 发送写命令 */
    outb(io_base + IDE_REG_COMMAND, IDE_CMD_WRITE_PIO);

    if (ide_wait_drq(io_base, 40000) != 0) return -1;

    /* 写入数据 */
    ide_write_pio(io_base, buffer, count);

    /* 刷新缓存：FLUSH 后只需等待 !BUSY（QEMU 对 FLUSH 的 DRDY 置位时序宽松，强制等 DRDY 会超时） */
    outb(io_base + IDE_REG_COMMAND, IDE_CMD_FLUSH);
    {
        int to2 = 100000;
        uint8_t st;
        while (to2-- > 0) {
            st = inb(io_base + IDE_REG_STATUS);
            if (!(st & IDE_STATUS_BUSY)) break;
        }
        if (to2 <= 0 && (inb(io_base + IDE_REG_STATUS) & IDE_STATUS_BUSY)) {
            return -1;
        }
    }

    return count;
}

int ide_get_drive_info(int index, ide_drive_t *info)
{
    if (index < 0 || index >= MAX_IDE_DRIVES || !info) return -1;
    if (!drives[index].present) return -1;
    *info = drives[index];
    return 0;
}

/* ============================================================================
 * V2.24: IDE 磁盘IO自测 (诊断版启动时自动执行, 输出到串口)
 * 验证: 读LBA0(MBR) + 写读回LBA1(避开MBR) 全链路 PIO IO
 * ========================================================================== */
void ide_selftest(void)
{
        if (!ide_initialized) {
        serial_puts(0x3F8, "[IDE] selftest: not initialized, skip\n");
        return;
    }
    ide_drive_t d;
    if (ide_get_drive_info(0, &d) != 0) {
        serial_puts(0x3F8, "[IDE] selftest: no drive present, skip\n");
        return;
    }

    /* 1. 读扇区0 (MBR) */
    uint8_t buf[512];
    if (ide_read_sectors(0, buf, 1) > 0) {
        serial_puts(0x3F8, "[IDE] selftest: read LBA0 OK, first bytes: ");
        for (int i = 0; i < 8; i++) {
            char hex[3];
            hex[0] = "0123456789abcdef"[(buf[i] >> 4) & 0xF];
            hex[1] = "0123456789abcdef"[buf[i] & 0xF];
            hex[2] = 0;
            serial_puts(0x3F8, hex);
            serial_putc(0x3F8, ' ');
        }
        serial_putc(0x3F8, '\n');

        /* V2.25: MBR 分区表解析输出 */
        if (buf[510] == 0x55 && buf[511] == 0xAA) {
            serial_puts(0x3F8, "[IDE] selftest: MBR valid, partitions:\n");
            int np = 0;
            for (int i = 0; i < 4; i++) {
                const uint8_t *e = buf + 446 + i * 16;
                if (e[4] == 0) continue;
                uint32_t lba = (uint32_t)e[8] | ((uint32_t)e[9] << 8) |
                               ((uint32_t)e[10] << 16) | ((uint32_t)e[11] << 24);
                uint32_t secs = (uint32_t)e[12] | ((uint32_t)e[13] << 8) |
                                ((uint32_t)e[14] << 16) | ((uint32_t)e[15] << 24);
                serial_puts(0x3F8, "  #");
                serial_putc(0x3F8, '0' + i);
                serial_puts(0x3F8, " type=0x");
                char hx[3];
                hx[0] = "0123456789abcdef"[(e[4] >> 4) & 0xF];
                hx[1] = "0123456789abcdef"[e[4] & 0xF];
                hx[2] = 0;
                serial_puts(0x3F8, hx);
                serial_puts(0x3F8, " lba=");
                serial_putc(0x3F8, '0' + ((lba / 100000) % 10));
                serial_putc(0x3F8, '0' + ((lba / 10000) % 10));
                serial_putc(0x3F8, '0' + ((lba / 1000) % 10));
                serial_putc(0x3F8, '0' + ((lba / 100) % 10));
                serial_putc(0x3F8, '0' + ((lba / 10) % 10));
                serial_putc(0x3F8, '0' + (lba % 10));
                serial_puts(0x3F8, " sectors=");
                /* secs 十进制简化输出 */
                uint32_t s = secs;
                char sdec[12]; int sn = 0;
                if (s == 0) sdec[sn++] = '0';
                while (s > 0) { sdec[sn++] = '0' + (s % 10); s /= 10; }
                while (sn > 0) serial_putc(0x3F8, sdec[--sn]);
                serial_putc(0x3F8, '\n');
                np++;
            }
            if (np == 0) serial_puts(0x3F8, "  (no partitions)\n");
        } else {
            serial_puts(0x3F8, "[IDE] selftest: no MBR signature\n");
        }
    } else {
        serial_puts(0x3F8, "[IDE] selftest: read LBA0 FAIL\n");
        return;
    }

    /* 2. 写读回最后扇区 (PIO 写验证)
       V3.82: 原写 LBA1 会覆盖引导器 stage2(第二段), 运行后磁盘下次启动必坏!
       改为写磁盘末尾扇区, 不触碰 MBR/stage2/内核区 */
    uint32_t test_lba = (d.total_sectors > 1) ? (d.total_sectors - 1) : 0;
    uint8_t wbuf[512];
    for (int i = 0; i < 512; i++) wbuf[i] = (uint8_t)(i & 0xFF);
    if (ide_write_sectors(test_lba, wbuf, 1) > 0) {
        uint8_t rbuf[512];
        if (ide_read_sectors(test_lba, rbuf, 1) > 0) {
            int match = 1;
            for (int i = 0; i < 512; i++) {
                if (wbuf[i] != rbuf[i]) { match = 0; break; }
            }
            serial_puts(0x3F8, match ?
                "[IDE] selftest: write+readback last-sector PASS\n" :
                "[IDE] selftest: write+readback last-sector MISMATCH\n");
        } else {
            serial_puts(0x3F8, "[IDE] selftest: readback last-sector FAIL\n");
        }
    } else {
        serial_puts(0x3F8, "[IDE] selftest: write last-sector FAIL\n");
    }
}
