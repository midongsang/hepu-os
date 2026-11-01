/* ============================================================================
 * 赫普OS - USB大容量存储类驱动头文件
 * USB Mass Storage Class (Bulk-Only Transport)
 * V8.33: 初始实现
 * ========================================================================== */

#ifndef _USB_MSC_H
#define _USB_MSC_H

#include "../usb/usb_core.h"

/* USB大容量存储类码 */
#define MSC_SUBCLASS_RBC       0x01  /* 简化块命令 */
#define MSC_SUBCLASS_SFF8020I  0x02  /* SFF-8020i (ATAPI) */
#define MSC_SUBCLASS_QIC       0x03  /* QIC-157 */
#define MSC_SUBCLASS_UFI       0x04  /* UFI (软盘) */
#define MSC_SUBCLASS_SFF8070I  0x05  /* SFF-8070i */
#define MSC_SUBCLASS_SCSI      0x06  /* SCSI透明命令集 */

/* MSC协议 */
#define MSC_PROTOCOL_CBI       0x00  /* CBI (带中断) */
#define MSC_PROTOCOL_CBI_NO_INT 0x01 /* CBI (无中断) */
#define MSC_PROTOCOL_BOT       0x50  /* Bulk-Only Transport */

/* MSC类特定请求 */
#define MSC_REQ_ADSC           0x00  /* 接受设备特定命令 */
#define MSC_REQ_GET_REQUEST    0xFC  /* 获取请求 */
#define MSC_REQ_PUT_REQUEST    0xFD  /* 放置请求 */
#define MSC_REQ_GET_MAX_LUN    0xFE  /* 获取最大LUN */
#define MSC_REQ_BOMSR          0xFF  /* Bulk-Only Mass Storage Reset */

/* CBW (Command Block Wrapper) 签名 */
#define MSC_CBW_SIGNATURE      0x43425355  /* "USBC" */
#define MSC_CSW_SIGNATURE      0x53425355  /* "USBS" */

/* CBW标志 */
#define MSC_CBW_FLAG_OUT       0x00  /* 数据输出（主机→设备） */
#define MSC_CBW_FLAG_IN        0x80  /* 数据输入（设备→主机） */

/* CSW状态 */
#define MSC_CSW_STATUS_PASSED  0x00
#define MSC_CSW_STATUS_FAILED  0x01
#define MSC_CSW_STATUS_PHASE   0x02

/* SCSI命令码 */
#define SCSI_TEST_UNIT_READY   0x00
#define SCSI_REQUEST_SENSE     0x03
#define SCSI_INQUIRY           0x12
#define SCSI_READ_CAPACITY     0x25
#define SCSI_READ_10           0x28
#define SCSI_WRITE_10          0x2A
#define SCSI_MODE_SENSE_6      0x1A
#define SCSI_START_STOP_UNIT   0x1B
#define SCSI_PREVENT_ALLOW_MEDIUM_REMOVAL 0x1E

/* CBW结构 */
typedef struct {
    uint32_t signature;      /* 0x43425355 */
    uint32_t tag;            /* 命令标签 */
    uint32_t data_transfer_length; /* 数据传输长度 */
    uint8_t  flags;          /* 标志位 */
    uint8_t  lun;            /* 逻辑单元号 */
    uint8_t  cb_length;      /* 命令块长度 */
    uint8_t  cb[16];         /* 命令块 */
} __attribute__((packed)) msc_cbw_t;

/* CSW结构 */
typedef struct {
    uint32_t signature;      /* 0x53425355 */
    uint32_t tag;            /* 命令标签 */
    uint32_t data_residue;   /* 剩余数据 */
    uint8_t  status;         /* 状态 */
} __attribute__((packed)) msc_csw_t;

/* SCSI INQUIRY响应 */
typedef struct {
    uint8_t  peripheral_type;
    uint8_t  removable;
    uint8_t  version;
    uint8_t  response_format;
    uint8_t  additional_length;
    uint8_t  reserved[3];
    char     vendor[8];
    char     product[16];
    char     revision[4];
} __attribute__((packed)) scsi_inquiry_t;

/* SCSI READ CAPACITY响应 */
typedef struct {
    uint32_t last_lba;       /* 最后一个LBA（大端） */
    uint32_t block_size;     /* 块大小（大端） */
} __attribute__((packed)) scsi_capacity_t;

/* SCSI READ(10)命令 */
typedef struct {
    uint8_t  opcode;         /* 0x28 */
    uint8_t  flags;
    uint32_t lba;            /* 起始LBA（大端） */
    uint8_t  group;
    uint16_t transfer_length; /* 传输块数（大端） */
    uint8_t  control;
} __attribute__((packed)) scsi_read10_t;

/* SCSI WRITE(10)命令 */
typedef struct {
    uint8_t  opcode;         /* 0x2A */
    uint8_t  flags;
    uint32_t lba;            /* 起始LBA（大端） */
    uint8_t  group;
    uint16_t transfer_length; /* 传输块数（大端） */
    uint8_t  control;
} __attribute__((packed)) scsi_write10_t;

/* USB大容量存储设备 */
typedef struct {
    usb_device_t *usb_dev;   /* 关联的USB设备 */
    int  subclass;           /* 子类（RBC/SCSI等） */
    int  protocol;           /* 协议（BOT/CBI） */
    int  max_lun;            /* 最大LUN */

    int  bulk_in_ep;         /* 批量输入端点 */
    int  bulk_out_ep;        /* 批量输出端点 */
    int  bulk_in_ep_size;    /* 批量输入端点最大包大小 */
    int  bulk_out_ep_size;   /* 批量输出端点最大包大小 */

    /* 设备信息 */
    uint32_t block_count;    /* 块数量 */
    uint32_t block_size;     /* 块大小（通常512） */
    uint64_t capacity;       /* 总容量（字节） */
    int      removable;      /* 是否可移动 */

    char vendor[9];          /* 厂商ID */
    char product[17];        /* 产品ID */
    char revision[5];        /* 版本 */

    uint32_t tag;            /* 当前命令标签 */
} msc_device_t;

/* 函数声明 */
int usb_msc_init(void);
int usb_msc_probe(usb_device_t *dev);
int usb_msc_bulk_only_reset(usb_device_t *dev);
int usb_msc_get_max_lun(usb_device_t *dev);
int usb_msc_command(usb_device_t *dev, msc_device_t *msc,
                     const void *cb, int cb_len,
                     void *data, int data_len, int direction);
int usb_msc_test_unit_ready(msc_device_t *msc);
int usb_msc_inquiry(msc_device_t *msc, scsi_inquiry_t *inquiry);
int usb_msc_read_capacity(msc_device_t *msc);
int usb_msc_read_blocks(msc_device_t *msc, uint32_t lba,
                         uint16_t count, void *buffer);
int usb_msc_write_blocks(msc_device_t *msc, uint32_t lba,
                          uint16_t count, const void *buffer);
int usb_msc_request_sense(msc_device_t *msc, void *sense, int len);

#endif /* _USB_MSC_H */
