/* ============================================================================
 * 赫普OS - RTL8139 网卡驱动
 * Realtek RTL8139 Family PCI Fast Ethernet Controller
 * QEMU默认使用此网卡：-netdev user,id=n0 -device rtl8139,netdev=n0
 *
 * 参考：RTL8139 Datasheet, PCI Local Bus Specification
 * V7.95: 初始实现
 * ========================================================================== */

#include <stdint.h>
#include <string.h>
#include "pci.h"
#include "vga.h"
#include "serial.h"
#include "eth.h"

/* RTL8139 PCI IDs */
#define RTL8139_VENDOR_ID   0x10EC
#define RTL8139_DEVICE_ID   0x8139

/* RTL8139 寄存器偏移（I/O端口） */
#define REG_IDR0        0x00    /* MAC地址 (6字节) */
#define REG_MAR0        0x08    /* 多播过滤 (8字节) */
#define REG_TSD0        0x10    /* 发送状态描述符0 */
#define REG_TSAD0       0x20    /* 发送地址描述符0 */
#define REG_RBSTART     0x30    /* 接收缓冲区地址 */
#define REG_ERSR        0x36    /* 早期接收状态 */
#define REG_CMD         0x37    /* 命令寄存器 */
#define REG_CAPR        0x38    /* 当前地址指针读取 */
#define REG_CBR         0x3A    /* 当前缓冲区地址 */
#define REG_IMR         0x3C    /* 中断掩码 */
#define REG_ISR         0x3E    /* 中断状态 */
#define REG_TCR         0x40    /* 发送配置 */
#define REG_RCR         0x44    /* 接收配置 */
#define REG_TCTR        0x48    /* 计时器 */
#define REG_MPC         0x4C    /* 丢包计数 */
#define REG_9346CR      0x50    /* 93C46命令寄存器 */
#define REG_CONFIG0     0x51    /* 配置0 */
#define REG_CONFIG1     0x52    /* 配置1 */
#define REG_MSR         0x58    /* 媒体状态 */
#define REG_CONFIG3     0x59    /* 配置3 */
#define REG_CONFIG4     0x5A    /* 配置4 */
#define REG_MULINT      0x5C    /* 多中断 */
#define REG_PCIIDR      0x5E    /* PCI修订 */
#define REG_BMCR        0x62    /* 基本MII控制 */
#define REG_BMSR        0x64    /* 基本MII状态 */
#define REG_ANAR        0x66    /* 自动协商广告 */
#define REG_ANLPAR      0x68    /* 链路伙伴能力 */
#define REG_ANER        0x6A    /* 自动协商扩展 */
#define REG_DIS         0x6C    /* 断开计数器 */
#define REG_FCSC        0x6E    /* FCS计数器 */
#define REG_NTR         0x70    /* 网络测试 */
#define REG_CR          0x74    /* 配置 */
#define REG_FER         0x76    /* 功能事件 */
#define REG_FEMR        0x78    /* 功能事件掩码 */
#define REG_FPSR        0x7A    /* 功能存在状态 */
#define REG_FFER        0x7C    /* 功能事件使能 */

/* 命令寄存器位 */
#define CMD_RESET       0x10    /* 软件复位 */
#define CMD_RX_ENABLE   0x08    /* 接收使能 */
#define CMD_TX_ENABLE   0x04    /* 发送使能 */
#define CMD_RX_EMPTY    0x01    /* 接收缓冲区空 */

/* 中断状态位 */
#define ISR_ROK         0x01    /* 接收OK */
#define ISR_RER         0x02    /* 接收错误 */
#define ISR_TOK         0x04    /* 发送OK */
#define ISR_TER         0x08    /* 发送错误 */
#define ISR_RXOVW       0x10    /* 接收缓冲区溢出 */
#define ISR_PUN         0x20    /* 链路变化 */
#define ISR_FOVW        0x40    /* FIFO溢出 */
#define ISR_LENCHG      0x2000  /* 链路长度变化 */
#define ISR_TIMEOUT     0x4000  /* 超时 */
#define ISR_SYSERR      0x8000  /* 系统错误 */

/* 接收配置位 */
#define RCR_AAP         0x01    /* 接受所有物理地址 */
#define RCR_APM         0x02    /* 接受物理地址匹配 */
#define RCR_AM          0x04    /* 接受多播 */
#define RCR_AB          0x08    /* 接受广播 */
#define RCR_AR          0x10    /* 接受错误包 */
#define RCR_AER         0x20    /* 接受错误包 */
#define RCR_WRAP        0x80    /* 接收缓冲区回绕 */
#define RCR_MXDMA_SHIFT 8       /* DMA突发长度移位 */
#define RCR_RXFTH_SHIFT 13      /* 接收FIFO阈值移位 */
#define RCR_RBLEN_SHIFT 11      /* 接收缓冲区长度移位 */

/* 发送配置位 */
#define TCR_MXDMA_SHIFT 8       /* DMA突发长度移位 */
#define TCR_TXRR_SHIFT  16      /* 发送重试次数移位 */
#define TCR_IFG_SHIFT   24      /* 帧间隔移位 */

/* 发送状态位 */
#define TSD_OWN         0x2000  /* 0=网卡拥有, 1=CPU拥有 */
#define TSD_TOK         0x0800  /* 发送OK */
#define TSD_TUN         0x0400  /* 发送FIFO不足 */
#define TSD_TOK_FIFO    0x0200  /* 发送FIFO溢出 */
#define TSD_ABORT       0x0100  /* 发送中止 */
#define TSD_CRS         0x0080  /* 载波丢失 */
#define TSD_TABT        0x0040  /* 发送中止 */
#define TSD_OWC         0x0020  /* 超出窗口冲突 */
#define TSD_CDH         0x0010  /* CD心跳 */
#define TSD_NCC_SHIFT   0       /* 冲突次数移位 */
#define TSD_SIZE_SHIFT  16      /* 包大小移位 */

/* 接收缓冲区大小：8KB + 16字节（回绕空间） */
#define RX_BUF_SIZE     8192
#define RX_BUF_TOTAL    (RX_BUF_SIZE + 16)

/* 发送缓冲区大小 */
#define TX_BUF_SIZE     1792

/* 驱动状态 */
typedef struct {
    int      present;
    int      iobase;
    int      irq;
    uint8_t  mac[6];
    uint8_t *rx_buf;
    uint8_t *tx_buf;
    int      rx_cur;
    int      link_up;
    uint32_t tx_packets;
    uint32_t rx_packets;
    uint32_t tx_bytes;
    uint32_t rx_bytes;
} rtl8139_dev_t;

static rtl8139_dev_t rtl8139_dev;

/* I/O端口读写 */
static inline uint8_t rtl_inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline uint16_t rtl_inw(uint16_t port) {
    uint16_t ret;
    __asm__ volatile ("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline uint32_t rtl_inl(uint16_t port) {
    uint32_t ret;
    __asm__ volatile ("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void rtl_outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline void rtl_outw(uint16_t port, uint16_t val) {
    __asm__ volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}

static inline void rtl_outl(uint16_t port, uint32_t val) {
    __asm__ volatile ("outl %0, %1" : : "a"(val), "Nd"(port));
}

/* 初始化RTL8139 */
int rtl8139_init(void)
{
    memset(&rtl8139_dev, 0, sizeof(rtl8139_dev));

    /* 查找PCI设备 */
    int idx = pci_find_device(RTL8139_VENDOR_ID, RTL8139_DEVICE_ID);
    if (idx < 0) {
        serial_puts(0x3F8, "[RTL8139] device not found\n");
        return -1;
    }

    pci_device_t *pci = &pci_devices[idx];
    serial_puts(0x3F8, "[RTL8139] found at PCI ");
    serial_puthex(0x3F8, pci->bus);
    serial_puts(0x3F8, ":");
    serial_puthex(0x3F8, pci->dev);
    serial_puts(0x3F8, "\n");

    /* 获取I/O端口基地址（BAR0） */
    rtl8139_dev.iobase = pci->bar[0] & ~0x3;
    rtl8139_dev.irq = pci->irq_line;

    serial_puts(0x3F8, "[RTL8139] iobase=0x");
    serial_puthex(0x3F8, rtl8139_dev.iobase);
    serial_puts(0x3F8, " irq=");
    serial_puthex(0x3F8, rtl8139_dev.irq);
    serial_puts(0x3F8, "\n");

    /* 启用PCI总线主控 */
    uint32_t cmd = pci_config_read(pci->bus, pci->dev, pci->func, 0x04);
    cmd |= 0x07; /* I/O空间 + 内存空间 + 总线主控 */
    pci_config_write(pci->bus, pci->dev, pci->func, 0x04, cmd);

    /* 软件复位 */
    rtl_outb(rtl8139_dev.iobase + REG_CMD, CMD_RESET);
    int timeout = 1000;
    while ((rtl_inb(rtl8139_dev.iobase + REG_CMD) & CMD_RESET) && timeout > 0) {
        timeout--;
        for (volatile int i = 0; i < 1000; i++);
    }
    if (timeout == 0) {
        serial_puts(0x3F8, "[RTL8139] reset timeout\n");
        return -1;
    }

    /* 分配接收缓冲区（8KB对齐） */
    extern void *kmalloc(unsigned int);
    rtl8139_dev.rx_buf = (uint8_t *)kmalloc(RX_BUF_TOTAL + 4096);
    if (!rtl8139_dev.rx_buf) {
        serial_puts(0x3F8, "[RTL8139] rx buffer alloc failed\n");
        return -1;
    }
    /* 对齐到4KB */
    rtl8139_dev.rx_buf = (uint8_t *)(((uint32_t)rtl8139_dev.rx_buf + 4095) & ~4095);
    memset(rtl8139_dev.rx_buf, 0, RX_BUF_TOTAL);

    /* 分配发送缓冲区 */
    rtl8139_dev.tx_buf = (uint8_t *)kmalloc(TX_BUF_SIZE);
    if (!rtl8139_dev.tx_buf) {
        serial_puts(0x3F8, "[RTL8139] tx buffer alloc failed\n");
        return -1;
    }

    /* 设置接收缓冲区地址 */
    rtl_outl(rtl8139_dev.iobase + REG_RBSTART, (uint32_t)rtl8139_dev.rx_buf);

    /* 读取MAC地址 */
    for (int i = 0; i < 6; i++) {
        rtl8139_dev.mac[i] = rtl_inb(rtl8139_dev.iobase + REG_IDR0 + i);
    }

    serial_puts(0x3F8, "[RTL8139] MAC: ");
    for (int i = 0; i < 6; i++) {
        serial_puthex(0x3F8, rtl8139_dev.mac[i]);
        if (i < 5) serial_puts(0x3F8, ":");
    }
    serial_puts(0x3F8, "\n");

    /* 设置接收配置：接受广播+多播+物理地址，DMA突发64字节，回绕模式 */
    uint32_t rcr = RCR_AB | RCR_AM | RCR_APM | RCR_WRAP;
    rcr |= (3 << RCR_MXDMA_SHIFT);  /* DMA突发=64字节 */
    rcr |= (7 << RCR_RXFTH_SHIFT);  /* FIFO阈值=无限制 */
    rcr |= (0 << RCR_RBLEN_SHIFT);  /* 缓冲区大小=8KB */
    rtl_outl(rtl8139_dev.iobase + REG_RCR, rcr);

    /* 设置发送配置：DMA突发64字节，最大重试16次 */
    uint32_t tcr = (3 << TCR_MXDMA_SHIFT) | (15 << TCR_TXRR_SHIFT);
    rtl_outl(rtl8139_dev.iobase + REG_TCR, tcr);

    /* 设置中断掩码：接收OK + 发送OK + 接收错误 + 发送错误 */
    rtl_outw(rtl8139_dev.iobase + REG_IMR, ISR_ROK | ISR_TOK | ISR_RER | ISR_TER | ISR_RXOVW);

    /* 清除中断状态 */
    rtl_outw(rtl8139_dev.iobase + REG_ISR, 0xFFFF);

    /* 初始化CAPR */
    rtl_outw(rtl8139_dev.iobase + REG_CAPR, 0);
    rtl8139_dev.rx_cur = 0;

    /* 启用接收和发送 */
    rtl_outb(rtl8139_dev.iobase + REG_CMD, CMD_RX_ENABLE | CMD_TX_ENABLE);

    /* 检查链路状态 */
    uint8_t msr = rtl_inb(rtl8139_dev.iobase + REG_MSR);
    rtl8139_dev.link_up = (msr & 0x04) ? 1 : 0;

    serial_puts(0x3F8, "[RTL8139] link ");
    serial_puts(0x3F8, rtl8139_dev.link_up ? "up" : "down");
    serial_puts(0x3F8, "\n");

    rtl8139_dev.present = 1;

    /* 更新eth0设备信息 */
    extern int eth_set_mac(const char *name, const uint8_t *mac);
    extern int eth_open(const char *name);
    eth_set_mac("eth0", rtl8139_dev.mac);
    if (rtl8139_dev.link_up) {
        eth_open("eth0");
    }

    serial_puts(0x3F8, "[RTL8139] initialization complete\n");
    return 0;
}

/* 发送数据包 */
int rtl8139_send(const uint8_t *data, int len)
{
    if (!rtl8139_dev.present || len <= 0 || len > TX_BUF_SIZE) return -1;

    /* 复制到发送缓冲区 */
    memcpy(rtl8139_dev.tx_buf, data, len);

    /* 填充到最小包大小（64字节） */
    if (len < 64) {
        memset(rtl8139_dev.tx_buf + len, 0, 64 - len);
        len = 64;
    }

    /* 等待发送描述符可用（4个发送描述符轮询） */
    static int tx_idx = 0;
    int timeout = 10000;
    while (!(rtl_inl(rtl8139_dev.iobase + REG_TSD0 + tx_idx * 4) & TSD_OWN) && timeout > 0) {
        timeout--;
        for (volatile int i = 0; i < 100; i++);
    }
    if (timeout == 0) {
        serial_puts(0x3F8, "[RTL8139] tx timeout\n");
        return -1;
    }

    /* 设置发送地址和大小 */
    rtl_outl(rtl8139_dev.iobase + REG_TSAD0 + tx_idx * 4, (uint32_t)rtl8139_dev.tx_buf);
    rtl_outl(rtl8139_dev.iobase + REG_TSD0 + tx_idx * 4, (len << TSD_SIZE_SHIFT) | TSD_OWN);

    tx_idx = (tx_idx + 1) % 4;

    rtl8139_dev.tx_packets++;
    rtl8139_dev.tx_bytes += len;

    return len;
}

/* 接收数据包 */
int rtl8139_recv(uint8_t *buf, int max_len)
{
    if (!rtl8139_dev.present) return -1;

    /* 检查是否有数据包 */
    if (rtl_inb(rtl8139_dev.iobase + REG_CMD) & CMD_RX_EMPTY) {
        return 0; /* 无数据包 */
    }

    /* 读取接收缓冲区 */
    uint16_t capr = rtl_inw(rtl8139_dev.iobase + REG_CAPR);
    uint8_t *pkt = rtl8139_dev.rx_buf + capr;

    /* 接收包头：状态(2) + 长度(2) */
    uint16_t status = *(uint16_t *)pkt;
    uint16_t pkt_len = *(uint16_t *)(pkt + 2);

    if (!(status & ISR_ROK)) {
        /* 错误包，跳过 */
        capr = (capr + pkt_len + 4 + 3) & ~3;
        rtl_outw(rtl8139_dev.iobase + REG_CAPR, capr - 0x10);
        return 0;
    }

    /* 复制数据包（去掉CRC的4字节） */
    int copy_len = pkt_len - 4;
    if (copy_len > max_len) copy_len = max_len;
    memcpy(buf, pkt + 4, copy_len);

    /* 更新CAPR（对齐到4字节，减去16字节偏移） */
    capr = (capr + pkt_len + 4 + 3) & ~3;
    if (capr >= RX_BUF_SIZE) capr -= RX_BUF_SIZE;
    rtl_outw(rtl8139_dev.iobase + REG_CAPR, capr - 0x10);

    rtl8139_dev.rx_packets++;
    rtl8139_dev.rx_bytes += copy_len;

    return copy_len;
}

/* 获取设备状态 */
void rtl8139_get_stats(uint32_t *tx_pkts, uint32_t *rx_pkts,
                        uint32_t *tx_bytes, uint32_t *rx_bytes, int *link)
{
    if (tx_pkts) *tx_pkts = rtl8139_dev.tx_packets;
    if (rx_pkts) *rx_pkts = rtl8139_dev.rx_packets;
    if (tx_bytes) *tx_bytes = rtl8139_dev.tx_bytes;
    if (rx_bytes) *rx_bytes = rtl8139_dev.rx_bytes;
    if (link) *link = rtl8139_dev.link_up;
}

/* 中断处理 */
void rtl8139_irq_handler(void)
{
    if (!rtl8139_dev.present) return;

    uint16_t isr = rtl_inw(rtl8139_dev.iobase + REG_ISR);

    if (isr & ISR_ROK) {
        /* 接收中断，数据包已在缓冲区中 */
    }
    if (isr & ISR_TOK) {
        /* 发送完成 */
    }
    if (isr & (ISR_RER | ISR_TER | ISR_RXOVW)) {
        serial_puts(0x3F8, "[RTL8139] error interrupt\n");
    }

    /* 清除中断 */
    rtl_outw(rtl8139_dev.iobase + REG_ISR, isr);
}
