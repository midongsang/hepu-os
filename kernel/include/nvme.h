#ifndef NVME_H
#define NVME_H

#include <stdint.h>

#define NVME_MAX_NAMESPACES 4
#define NVME_MAX_QUEUES 8
#define NVME_QUEUE_SIZE 64
#define NVME_SECTOR_SIZE 512

/* NVMe控制器寄存器 */
typedef struct {
    uint64_t cap;     /* 0x00 能力 */
    uint32_t vs;      /* 0x08 版本 */
    uint32_t intms;   /* 0x0C 中断掩码 */
    uint32_t intmc;   /* 0x10 中断清除 */
    uint32_t cc;      /* 0x14 控制器配置 */
    uint32_t reserved1;
    uint32_t csts;    /* 0x1C 控制器状态 */
    uint32_t nssr;    /* 0x20 NVM子系统重置 */
    uint32_t aqa;     /* 0x24 管理队列属性 */
    uint64_t asq;     /* 0x28 管理提交队列基地址 */
    uint64_t acq;     /* 0x30 管理完成队列基地址 */
    uint32_t cmbloc;  /* 0x38 控制器内存缓冲区位置 */
    uint32_t cmbsz;   /* 0x3C 控制器内存缓冲区大小 */
    uint32_t bpinfo;  /* 0x40 引导分区信息 */
    uint32_t bprsel;  /* 0x44 引导分区读选择 */
    uint64_t bpmbl;   /* 0x48 引导分区内存缓冲区位置 */
    uint32_t cmbmsc;  /* 0x50 CMB镜像大小控制 */
    uint32_t cmbsts;  /* 0x54 CMB镜像状态 */
    uint32_t cmbebs;  /* 0x58 CMB镜像弹性缓冲区大小 */
    uint32_t cmbswtp; /* 0x5C CMB镜像写吞吐量 */
    uint32_t nssd;    /* 0x60 NVM子系统关闭 */
    uint8_t  reserved2[0x400-0x64];
    uint32_t doorbells[0]; /* 0x400 门铃寄存器 */
} __attribute__((packed)) nvme_ctrl_t;

/* NVMe完成队列条目 */
typedef struct {
    uint32_t dw0;     /* 命令特定 */
    uint32_t dw1;     /* 保留 */
    uint16_t sqhd;    /* 提交队列头指针 */
    uint16_t sqid;    /* 提交队列ID */
    uint16_t cid;     /* 命令ID */
    uint16_t status;  /* 状态字段 */
} __attribute__((packed)) nvme_cqe_t;

/* NVMe提交队列条目 */
typedef struct {
    uint32_t cdw0;    /* 操作码+命令ID */
    uint32_t nsid;    /* 命名空间ID */
    uint32_t cdw2[2]; /* 保留 */
    uint64_t mptr;    /* 元数据指针 */
    uint64_t prp1;    /* PRP条目1 */
    uint64_t prp2;    /* PRP条目2 */
    uint32_t cdw10[6];/* 命令特定 */
} __attribute__((packed)) nvme_sqe_t;

/* NVMe命名空间信息 */
typedef struct {
    int present;
    uint32_t nsid;
    uint64_t total_sectors;
    uint32_t sector_size;
    uint32_t lba_format;
    char model[41];
    char serial[21];
    char firmware[9];
} nvme_namespace_t;

/* NVMe控制器信息 */
typedef struct {
    int present;
    nvme_ctrl_t *regs;
    uint16_t vendor_id;
    uint16_t device_id;
    uint32_t max_queue_entries;
    uint32_t max_namespaces;
    uint32_t page_size;
    nvme_namespace_t namespaces[NVME_MAX_NAMESPACES];
    int namespace_count;
} nvme_ctrl_info_t;

/* NVMe命令操作码 */
#define NVME_CMD_FLUSH    0x00
#define NVME_CMD_WRITE    0x01
#define NVME_CMD_READ     0x02
#define NVME_CMD_IDENTIFY 0x06

void nvme_init(void);
int nvme_detect(nvme_ctrl_t *ctrl);
int nvme_read_sectors(nvme_ctrl_t *ctrl, uint32_t nsid, uint64_t lba,
                      uint8_t *buffer, uint32_t count);
int nvme_write_sectors(nvme_ctrl_t *ctrl, uint32_t nsid, uint64_t lba,
                       const uint8_t *buffer, uint32_t count);
int nvme_namespace_list(nvme_namespace_t *namespaces, int max);
int nvme_get_ctrl_info(nvme_ctrl_info_t *info);

#endif /* NVME_H */
