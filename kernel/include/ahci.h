#ifndef AHCI_H
#define AHCI_H

#include <stdint.h>

#define AHCI_MAX_PORTS 32
#define AHCI_MAX_CMD_SLOTS 32
#define AHCI_SECTOR_SIZE 512

/* AHCI HBA寄存器 */
typedef struct {
    uint32_t cap;          /* 0x00 能力 */
    uint32_t ghc;          /* 0x04 全局主机控制 */
    uint32_t is;           /* 0x08 中断状态 */
    uint32_t pi;           /* 0x0C 端口实现 */
    uint32_t vs;           /* 0x10 版本 */
    uint32_t ccc_ctl;      /* 0x14 命令完成合并控制 */
    uint32_t ccc_ports;    /* 0x18 命令完成合并端口 */
    uint32_t em_loc;       /* 0x1C 封装管理位置 */
    uint32_t em_ctl;       /* 0x20 封装管理控制 */
    uint32_t cap2;         /* 0x24 能力2 */
    uint32_t bohc;         /* 0x28 BIOS/OS握手 */
    uint8_t  reserved[0x60-0x2C];
    uint8_t  vendor[0x100-0x60];
} __attribute__((packed)) ahci_hba_t;

/* AHCI端口寄存器 */
typedef struct {
    uint64_t clb;          /* 0x00 命令列表基地址 */
    uint64_t fb;           /* 0x08 FIS基地址 */
    uint32_t is;           /* 0x10 中断状态 */
    uint32_t ie;           /* 0x14 中断使能 */
    uint32_t cmd;          /* 0x18 命令和状态 */
    uint32_t reserved1;
    uint64_t tfd;          /* 0x20 任务文件数据 */
    uint32_t sig;          /* 0x28 签名 */
    uint32_t ssts;         /* 0x2C SATA状态 */
    uint32_t sctl;         /* 0x30 SATA控制 */
    uint32_t serr;         /* 0x34 SATA错误 */
    uint32_t sact;         /* 0x38 SATA活动 */
    uint32_t ci;           /* 0x3C 命令问题 */
    uint32_t sntf;         /* 0x40 SATA通知 */
    uint32_t fbs;          /* 0x44 FIS交换 */
    uint8_t  reserved2[0x70-0x48];
    uint32_t vendor[4];    /* 0x70 厂商 */
} __attribute__((packed)) ahci_port_t;

/* AHCI命令头 */
typedef struct {
    uint8_t  cfl:5;        /* 命令FIS长度 */
    uint8_t  a:1;          /* ATAPI */
    uint8_t  w:1;          /* 写 */
    uint8_t  p:1;          /* 预取 */
    uint8_t  r:1;          /* 复位 */
    uint8_t  b:1;          /* BIST */
    uint8_t  c:1;          /* 清除忙 */
    uint8_t  reserved1:1;
    uint8_t  pmp:4;        /* 端口乘数 */
    uint16_t prdtl;        /* PRDT长度 */
    uint32_t prdbc;        /* PRDT传输字节数 */
    uint64_t ctba;         /* 命令表基地址 */
    uint32_t reserved2[4];
} __attribute__((packed)) ahci_cmd_header_t;

/* AHCI物理区域描述符表项 */
typedef struct {
    uint64_t dba;          /* 数据基地址 */
    uint32_t reserved;
    uint32_t dbc:22;       /* 数据字节数 */
    uint32_t reserved1:9;
    uint32_t i:1;          /* 中断 */
} __attribute__((packed)) ahci_prdt_t;

/* AHCI命令表 */
typedef struct {
    uint8_t  cfis[64];     /* 命令FIS */
    uint8_t  acmd[16];     /* ATAPI命令 */
    uint8_t  reserved[48];
    ahci_prdt_t prdt[1];   /* PRDT */
} __attribute__((packed)) ahci_cmd_table_t;

/* 端口信息 */
typedef struct {
    int present;
    int port_num;
    ahci_port_t *regs;
    uint32_t command_list_phys;
    uint32_t fis_phys;
    uint64_t total_sectors;
    uint32_t sector_size;
    char model[41];
    char serial[21];
    char firmware[9];
} ahci_port_info_t;

void ahci_init(void);
int ahci_detect(ahci_hba_t *hba);
int ahci_port_init(ahci_port_t *port, int port_num);
int ahci_read_sectors(ahci_port_t *port, uint64_t lba, uint8_t *buffer, uint32_t count);
int ahci_write_sectors(ahci_port_t *port, uint64_t lba, const uint8_t *buffer, uint32_t count);
int ahci_port_list(ahci_port_info_t *ports, int max);

#endif /* AHCI_H */
