/* ============================================================================
 * 赫普OS - Intel e1000 网卡驱动
 * Intel 82540EM/82545EM Gigabit Ethernet Controller
 * QEMU默认网卡之一：-netdev user,id=n0 -device e1000,netdev=n0
 *
 * V8.03: 初始实现
 * ========================================================================== */

#include <stdint.h>
#include <string.h>
#include "pci.h"
#include "vga.h"
#include "serial.h"
#include "eth.h"

/* e1000 PCI IDs */
#define E1000_VENDOR_ID    0x8086
#define E1000_DEVICE_82540EM  0x100E
#define E1000_DEVICE_82545EM  0x100F

/* e1000 寄存器偏移（内存映射） */
#define REG_CTRL      0x0000  /* 设备控制 */
#define REG_STATUS    0x0008  /* 设备状态 */
#define REG_EECD      0x0010  /* EEPROM控制 */
#define REG_EERD      0x0014  /* EEPROM读取 */
#define REG_CTRL_EXT  0x0018  /* 扩展控制 */
#define REG_MDIC      0x0020  /* MDI控制 */
#define REG_FCAL      0x0028  /* 流控地址低 */
#define REG_FCAH      0x002C  /* 流控地址高 */
#define REG_FCT       0x0030  /* 流控类型 */
#define REG_VET       0x0038  /* VLAN标签 */
#define REG_PBA       0x0100  /* 数据包缓冲区分配 */
#define REG_ICR       0x00C0  /* 中断原因读取 */
#define REG_ITR       0x00C4  /* 中断节流率 */
#define REG_ICS       0x00C8  /* 中断原因设置 */
#define REG_IMS       0x00D0  /* 中断掩码设置 */
#define REG_IMC       0x00D8  /* 中断掩码清除 */
#define REG_IAM       0x00E0  /* 中断自动掩码 */
#define REG_RCTL      0x0100  /* 接收控制 */
#define REG_RDLEN     0x2800  /* 接收描述符环长度 */
#define REG_RDH       0x2810  /* 接收描述符环头 */
#define REG_RDT       0x2818  /* 接收描述符环尾 */
#define REG_RDBAL     0x2800  /* 接收描述符环地址低 */
#define REG_RDBAH     0x2804  /* 接收描述符环地址高 */
#define REG_TCTL      0x0400  /* 发送控制 */
#define REG_TDLEN     0x3800  /* 发送描述符环长度 */
#define REG_TDH       0x3810  /* 发送描述符环头 */
#define REG_TDT       0x3818  /* 发送描述符环尾 */
#define REG_TDBAL     0x3800  /* 发送描述符环地址低 */
#define REG_TDBAH     0x3804  /* 发送描述符环地址高 */
#define REG_MTA       0x5200  /* 多播表数组 */
#define REG_RAL       0x5400  /* 接收地址低 */
#define REG_RAH       0x5404  /* 接收地址高 */

/* 控制寄存器位 */
#define CTRL_LRST     0x00000008  /* 链路复位 */
#define CTRL_ILOS     0x00000040  /* 反转丢失信号 */
#define CTRL_SLU      0x00000040  /* 设置链路打开 */
#define CTRL_FRCSPD   0x00000800  /* 强制速度 */
#define CTRL_FRCDPLX  0x00001000  /* 强制双工 */
#define CTRL_RST      0x04000000  /* 设备复位 */
#define CTRL_VME      0x40000000  /* VLAN模式使能 */
#define CTRL_PHY_RST  0x80000000  /* PHY复位 */

/* 接收控制寄存器位 */
#define RCTL_EN       0x00000002  /* 接收使能 */
#define RCTL_SBP      0x00000004  /* 存储坏包 */
#define RCTL_UPE      0x00000008  /* 单播使能 */
#define RCTL_MPE      0x00000010  /* 多播使能 */
#define RCTL_LPE      0x00000020  /* 长包使能 */
#define RCTL_BAM      0x00008000  /* 广播接受模式 */
#define RCTL_BSIZE_256  0x00000000  /* 缓冲区大小256 */
#define RCTL_BSIZE_512  0x00010000  /* 缓冲区大小512 */
#define RCTL_BSIZE_1024 0x00020000  /* 缓冲区大小1024 */
#define RCTL_BSIZE_2048 0x00030000  /* 缓冲区大小2048 */
#define RCTL_BSEX      0x02000000  /* 缓冲区大小扩展 */
#define RCTL_SECRC     0x04000000  /* 剥离CRC */

/* 发送控制寄存器位 */
#define TCTL_EN       0x00000002  /* 发送使能 */
#define TCTL_PSP      0x00000008  /* 填充短包 */
#define TCTL_CT_SHIFT 4           /* 冲突阈值移位 */
#define TCTL_COLD_SHIFT 12        /* 冲突距离移位 */

/* 中断原因位 */
#define ICR_TXDW      0x00000001  /* 发送描述符回写 */
#define ICR_TXQE      0x00000002  /* 发送队列空 */
#define ICR_LSC       0x00000004  /* 链路状态变化 */
#define ICR_RXSEQ     0x00000008  /* 接收序列号错误 */
#define ICR_RXDMT0    0x00000010  /* 接收描述符最小阈值 */
#define ICR_RXO       0x00000040  /* 接收溢出 */
#define ICR_RXT0      0x00000080  /* 接收定时器 */
#define ICR_MDAC      0x00000200  /* MDIO访问完成 */
#define ICR_RXCFG     0x00000400  /* 接收/配置 */

/* 描述符数量 */
#define NUM_RX_DESC   32
#define NUM_TX_DESC   8

/* 接收描述符 */
typedef struct {
    uint64_t addr;       /* 缓冲区地址 */
    uint16_t length;     /* 包长度 */
    uint16_t checksum;   /* 校验和 */
    uint8_t  status;     /* 状态 */
    uint8_t  errors;     /* 错误 */
    uint16_t special;    /* 特殊 */
} __attribute__((packed)) e1000_rx_desc_t;

/* 发送描述符 */
typedef struct {
    uint64_t addr;       /* 缓冲区地址 */
    uint16_t length;     /* 包长度 */
    uint8_t  cso;        /* 校验和偏移 */
    uint8_t  cmd;        /* 命令 */
    uint8_t  status;     /* 状态 */
    uint8_t  css;        /* 校验和起始 */
    uint16_t special;    /* 特殊 */
} __attribute__((packed)) e1000_tx_desc_t;

/* 发送描述符命令位 */
#define TX_CMD_EOP     0x01  /* 包结束 */
#define TX_CMD_IFCS    0x02  /* 插入FCS */
#define TX_CMD_RS      0x08  /* 报告状态 */

/* 发送描述符状态位 */
#define TX_STATUS_DD   0x01  /* 描述符完成 */

/* 接收描述符状态位 */
#define RX_STATUS_DD   0x01  /* 描述符完成 */
#define RX_STATUS_EOP  0x02  /* 包结束 */

/* 驱动状态 */
typedef struct {
    int      present;
    uint32_t *mmio_base;
    int      irq;
    uint8_t  mac[6];
    e1000_rx_desc_t *rx_descs;
    e1000_tx_desc_t *tx_descs;
    uint8_t  *rx_bufs;
    uint8_t  *tx_bufs;
    int      rx_cur;
    int      tx_cur;
    int      link_up;
    uint32_t tx_packets;
    uint32_t rx_packets;
    uint32_t tx_bytes;
    uint32_t rx_bytes;
} e1000_dev_t;

static e1000_dev_t e1000_dev;

/* 寄存器读写 */
static inline uint32_t e1000_read(uint32_t reg)
{
    return e1000_dev.mmio_base[reg / 4];
}

static inline void e1000_write(uint32_t reg, uint32_t val)
{
    e1000_dev.mmio_base[reg / 4] = val;
}

/* 从EEPROM读取MAC地址 */
static int e1000_read_mac(void)
{
    /* 简化：从EEPROM读取MAC地址 */
    for (int i = 0; i < 3; i++) {
        e1000_write(REG_EERD, 1 | (i << 8));
        /* 等待读取完成 */
        for (int j = 0; j < 10000; j++) {
            if (e1000_read(REG_EERD) & 0x10) break;
        }
        uint16_t data = (e1000_read(REG_EERD) >> 16) & 0xFFFF;
        e1000_dev.mac[i * 2] = data & 0xFF;
        e1000_dev.mac[i * 2 + 1] = (data >> 8) & 0xFF;
    }
    return 0;
}

/* 初始化e1000 */
int e1000_init(void)
{
    memset(&e1000_dev, 0, sizeof(e1000_dev));

    /* 查找PCI设备 */
    int idx = pci_find_device(E1000_VENDOR_ID, E1000_DEVICE_82540EM);
    if (idx < 0) {
        idx = pci_find_device(E1000_VENDOR_ID, E1000_DEVICE_82545EM);
    }
    if (idx < 0) {
        serial_puts(0x3F8, "[e1000] device not found\n");
        return -1;
    }

    pci_device_t *pci = &pci_devices[idx];
    serial_puts(0x3F8, "[e1000] found at PCI ");
    serial_puthex(0x3F8, pci->bus);
    serial_puts(0x3F8, ":");
    serial_puthex(0x3F8, pci->dev);
    serial_puts(0x3F8, "\n");

    /* 获取内存映射基地址（BAR0） */
    e1000_dev.mmio_base = (uint32_t *)(pci->bar[0] & ~0xF);
    e1000_dev.irq = pci->irq_line;

    serial_puts(0x3F8, "[e1000] mmio=0x");
    serial_puthex(0x3F8, (uint32_t)e1000_dev.mmio_base);
    serial_puts(0x3F8, " irq=");
    serial_puthex(0x3F8, e1000_dev.irq);
    serial_puts(0x3F8, "\n");

    /* 启用PCI总线主控和内存空间 */
    uint32_t cmd = pci_config_read(pci->bus, pci->dev, pci->func, 0x04);
    cmd |= 0x07;  /* I/O + 内存 + 总线主控 */
    pci_config_write(pci->bus, pci->dev, pci->func, 0x04, cmd);

    /* 设备复位 */
    e1000_write(REG_CTRL, CTRL_RST);
    for (int i = 0; i < 100000; i++) {
        if (!(e1000_read(REG_CTRL) & CTRL_RST)) break;
    }

    /* 读取MAC地址 */
    e1000_read_mac();
    serial_puts(0x3F8, "[e1000] MAC: ");
    for (int i = 0; i < 6; i++) {
        serial_puthex(0x3F8, e1000_dev.mac[i]);
        if (i < 5) serial_puts(0x3F8, ":");
    }
    serial_puts(0x3F8, "\n");

    /* 分配接收描述符和缓冲区 */
    extern void *kmalloc(unsigned int);
    e1000_dev.rx_descs = (e1000_rx_desc_t *)kmalloc(NUM_RX_DESC * sizeof(e1000_rx_desc_t) + 16);
    e1000_dev.rx_bufs = (uint8_t *)kmalloc(NUM_RX_DESC * 2048 + 16);
    e1000_dev.tx_descs = (e1000_tx_desc_t *)kmalloc(NUM_TX_DESC * sizeof(e1000_tx_desc_t) + 16);
    e1000_dev.tx_bufs = (uint8_t *)kmalloc(NUM_TX_DESC * 2048 + 16);

    if (!e1000_dev.rx_descs || !e1000_dev.rx_bufs ||
        !e1000_dev.tx_descs || !e1000_dev.tx_bufs) {
        serial_puts(0x3F8, "[e1000] buffer alloc failed\n");
        return -1;
    }

    /* 对齐到16字节 */
    e1000_dev.rx_descs = (e1000_rx_desc_t *)(((uint32_t)e1000_dev.rx_descs + 15) & ~15);
    e1000_dev.rx_bufs = (uint8_t *)(((uint32_t)e1000_dev.rx_bufs + 15) & ~15);
    e1000_dev.tx_descs = (e1000_tx_desc_t *)(((uint32_t)e1000_dev.tx_descs + 15) & ~15);
    e1000_dev.tx_bufs = (uint8_t *)(((uint32_t)e1000_dev.tx_bufs + 15) & ~15);

    memset(e1000_dev.rx_descs, 0, NUM_RX_DESC * sizeof(e1000_rx_desc_t));
    memset(e1000_dev.tx_descs, 0, NUM_TX_DESC * sizeof(e1000_tx_desc_t));

    /* 初始化接收描述符 */
    for (int i = 0; i < NUM_RX_DESC; i++) {
        e1000_dev.rx_descs[i].addr = (uint32_t)(e1000_dev.rx_bufs + i * 2048);
        e1000_dev.rx_descs[i].status = 0;
    }

    /* 设置接收描述符环 */
    e1000_write(REG_RDBAL, (uint32_t)e1000_dev.rx_descs);
    e1000_write(REG_RDBAH, 0);
    e1000_write(REG_RDLEN, NUM_RX_DESC * sizeof(e1000_rx_desc_t));
    e1000_write(REG_RDH, 0);
    e1000_write(REG_RDT, NUM_RX_DESC - 1);

    /* 设置发送描述符环 */
    e1000_write(REG_TDBAL, (uint32_t)e1000_dev.tx_descs);
    e1000_write(REG_TDBAH, 0);
    e1000_write(REG_TDLEN, NUM_TX_DESC * sizeof(e1000_tx_desc_t));
    e1000_write(REG_TDH, 0);
    e1000_write(REG_TDT, 0);

    /* 设置MAC地址过滤 */
    for (int i = 0; i < 6; i += 2) {
        e1000_write(REG_RAL + i, *(uint32_t *)&e1000_dev.mac[i]);
    }
    e1000_write(REG_RAH, (*(uint16_t *)&e1000_dev.mac[4]) | 0x80000000);

    /* 清除多播表 */
    for (int i = 0; i < 128; i++) {
        e1000_write(REG_MTA + i * 4, 0);
    }

    /* 配置接收控制 */
    uint32_t rctl = RCTL_EN | RCTL_BAM | RCTL_UPE | RCTL_MPE |
                    RCTL_BSIZE_2048 | RCTL_SECRC;
    e1000_write(REG_RCTL, rctl);

    /* 配置发送控制 */
    uint32_t tctl = TCTL_EN | TCTL_PSP | (15 << TCTL_CT_SHIFT) | (64 << TCTL_COLD_SHIFT);
    e1000_write(REG_TCTL, tctl);

    /* 启用中断 */
    e1000_write(REG_IMS, ICR_TXDW | ICR_RXT0 | ICR_LSC | ICR_RXO);

    /* 设置链路打开 */
    e1000_write(REG_CTRL, e1000_read(REG_CTRL) | CTRL_SLU);

    /* 检查链路状态 */
    e1000_dev.link_up = (e1000_read(REG_STATUS) & 0x02) ? 1 : 0;
    serial_puts(0x3F8, "[e1000] link ");
    serial_puts(0x3F8, e1000_dev.link_up ? "up" : "down");
    serial_puts(0x3F8, "\n");

    e1000_dev.present = 1;
    e1000_dev.rx_cur = 0;
    e1000_dev.tx_cur = 0;

    /* 注册到eth驱动 */
    extern int eth_set_mac(const char *name, const uint8_t *mac);
    eth_set_mac("eth0", e1000_dev.mac);

    serial_puts(0x3F8, "[e1000] initialization complete\n");
    return 0;
}

/* 发送数据包 */
int e1000_send(const uint8_t *data, int len)
{
    if (!e1000_dev.present || len <= 0 || len > 2048) return -1;

    /* 等待发送描述符可用 */
    int tx_cur = e1000_dev.tx_cur;
    if (e1000_dev.tx_descs[tx_cur].status & TX_STATUS_DD) {
        e1000_dev.tx_descs[tx_cur].status = 0;
    }

    /* 复制数据到发送缓冲区 */
    memcpy(e1000_dev.tx_bufs + tx_cur * 2048, data, len);

    /* 设置发送描述符 */
    e1000_dev.tx_descs[tx_cur].addr = (uint32_t)(e1000_dev.tx_bufs + tx_cur * 2048);
    e1000_dev.tx_descs[tx_cur].length = len;
    e1000_dev.tx_descs[tx_cur].cmd = TX_CMD_EOP | TX_CMD_IFCS | TX_CMD_RS;
    e1000_dev.tx_descs[tx_cur].status = 0;

    /* 更新发送尾指针 */
    tx_cur = (tx_cur + 1) % NUM_TX_DESC;
    e1000_dev.tx_cur = tx_cur;
    e1000_write(REG_TDT, tx_cur);

    e1000_dev.tx_packets++;
    e1000_dev.tx_bytes += len;

    return len;
}

/* 接收数据包 */
int e1000_recv(uint8_t *buf, int max_len)
{
    if (!e1000_dev.present) return -1;

    int rx_cur = e1000_dev.rx_cur;
    e1000_rx_desc_t *desc = &e1000_dev.rx_descs[rx_cur];

    /* 检查描述符是否完成 */
    if (!(desc->status & RX_STATUS_DD)) {
        return 0;  /* 无数据包 */
    }

    /* 复制数据包 */
    int len = desc->length;
    if (len > max_len) len = max_len;
    memcpy(buf, (void *)(uint32_t)desc->addr, len);

    /* 清除状态，归还描述符 */
    desc->status = 0;
    rx_cur = (rx_cur + 1) % NUM_RX_DESC;
    e1000_dev.rx_cur = rx_cur;
    e1000_write(REG_RDT, (rx_cur + NUM_RX_DESC - 1) % NUM_RX_DESC);

    e1000_dev.rx_packets++;
    e1000_dev.rx_bytes += len;

    return len;
}

/* 获取设备状态 */
void e1000_get_stats(uint32_t *tx_pkts, uint32_t *rx_pkts,
                      uint32_t *tx_bytes, uint32_t *rx_bytes, int *link)
{
    if (tx_pkts) *tx_pkts = e1000_dev.tx_packets;
    if (rx_pkts) *rx_pkts = e1000_dev.rx_packets;
    if (tx_bytes) *tx_bytes = e1000_dev.tx_bytes;
    if (rx_bytes) *rx_bytes = e1000_dev.rx_bytes;
    if (link) *link = e1000_dev.link_up;
}

/* 中断处理 */
void e1000_irq_handler(void)
{
    if (!e1000_dev.present) return;

    uint32_t icr = e1000_read(REG_ICR);

    if (icr & ICR_RXT0) {
        /* 接收中断 */
    }
    if (icr & ICR_TXDW) {
        /* 发送完成 */
    }
    if (icr & ICR_LSC) {
        /* 链路状态变化 */
        e1000_dev.link_up = (e1000_read(REG_STATUS) & 0x02) ? 1 : 0;
    }

    /* 清除中断 */
    e1000_write(REG_ICR, icr);
}
