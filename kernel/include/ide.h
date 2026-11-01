#ifndef IDE_H
#define IDE_H

#include <stdint.h>

#define IDE_PRIMARY_IO   0x1F0
#define IDE_SECONDARY_IO 0x170
#define IDE_CTRL_PRIMARY 0x3F6
#define IDE_CTRL_SECONDARY 0x376

/* IDE寄存器偏移 */
#define IDE_REG_DATA     0x00
#define IDE_REG_ERROR    0x01
#define IDE_REG_FEATURES 0x01
#define IDE_REG_SECCOUNT 0x02
#define IDE_REG_LBA0     0x03
#define IDE_REG_LBA1     0x04
#define IDE_REG_LBA2     0x05
#define IDE_REG_DRIVE    0x06
#define IDE_REG_STATUS   0x07
#define IDE_REG_COMMAND  0x07

/* IDE状态位 */
#define IDE_STATUS_BUSY  0x80
#define IDE_STATUS_DRDY  0x40
#define IDE_STATUS_DF    0x20
#define IDE_STATUS_DSC   0x10
#define IDE_STATUS_DRQ   0x08
#define IDE_STATUS_CORR  0x04
#define IDE_STATUS_IDX   0x02
#define IDE_STATUS_ERR   0x01

/* IDE命令 */
#define IDE_CMD_READ_PIO    0x20
#define IDE_CMD_WRITE_PIO   0x30
#define IDE_CMD_IDENTIFY    0xEC
#define IDE_CMD_FLUSH       0xE7

#define IDE_SECTOR_SIZE 512
#define MAX_IDE_DRIVES 4

typedef struct {
    int present;
    int channel;  /* 0=primary, 1=secondary */
    int drive;    /* 0=master, 1=slave */
    uint32_t total_sectors;
    uint32_t lba_support;
    char model[41];
    char serial[21];
} ide_drive_t;

void ide_init(void);
/* V6.99: 查询IDE是否已初始化 */
int ide_is_initialized(void);
/* V4.01-E46: IDE 总线主控 DMA + 批量读写接口 */
void ide_dma_init(void);
int  ide_dma_is_available(void);
int  ide_batch_read(uint32_t lba, uint8_t *buffer, uint32_t count);
int  ide_batch_write(uint32_t lba, const uint8_t *buffer, uint32_t count);
int ide_read_sectors(uint32_t lba, uint8_t *buffer, uint32_t count);
int ide_write_sectors(uint32_t lba, const uint8_t *buffer, uint32_t count);
int ide_identify(int channel, int drive, ide_drive_t *info);
int ide_get_drive_info(int index, ide_drive_t *info);
/* V6.49: 设置当前驱动器（-1=自动选择第一个可用驱动器）
 * 用于支持多硬盘场景（如系统盘+ext2数据盘） */
void ide_set_current_drive(int index);

#endif /* IDE_H */
void ide_selftest(void);
