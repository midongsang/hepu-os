/* ============================================================================
 * 赫普OS - 64位RTL8139网卡驱动
 * Realtek RTL8139 Family PCI Fast Ethernet Controller
 * QEMU默认使用此网卡：-netdev user,id=n0 -device rtl8139,netdev=n0
 * V0.10.6: 初始实现（初始化、发送、轮询接收）
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>

/* RTL8139 PCI IDs */
#define RTL8139_VENDOR_ID   0x10EC
#define RTL8139_DEVICE_ID   0x8139

/* RTL8139 寄存器偏移 */
#define REG_IDR0        0x00    /* MAC地址 */
#define REG_TSD0        0x10    /* 发送状态描述符0 */
#define REG_TSAD0       0x20    /* 发送地址描述符0 */
#define REG_RBSTART     0x30    /* 接收缓冲区地址 */
#define REG_CMD         0x37    /* 命令寄存器 */
#define REG_CAPR        0x38    /* 当前地址指针读取 */
#define REG_CBR         0x3A    /* 当前缓冲区地址 */
#define REG_IMR         0x3C    /* 中断掩码 */
#define REG_ISR         0x3E    /* 中断状态 */
#define REG_TCR         0x40    /* 发送配置 */
#define REG_RCR         0x44    /* 接收配置 */
#define REG_9346CR      0x50    /* 93C46命令寄存器 */
#define REG_CONFIG1     0x52    /* 配置1 */

/* 命令寄存器位 */
#define CMD_RESET       0x10
#define CMD_RX_ENABLE   0x08
#define CMD_TX_ENABLE   0x04
#define CMD_RX_EMPTY    0x01

/* 中断状态位 */
#define ISR_ROK         0x01
#define ISR_TOK         0x04
#define ISR_RXOVW       0x10

/* 接收配置位 */
#define RCR_AAP         0x01
#define RCR_APM         0x02
#define RCR_AM          0x04
#define RCR_AB          0x08
#define RCR_WRAP        0x80

/* 发送状态位 */
#define TSD_OWN         0x2000  /* 网卡拥有（正在发送） */
#define TSD_OK          0x0800  /* 发送成功 */

/* 外部函数 */
extern void vga_puts(const char *str);
extern void vga_put_hex(uint64_t val);
extern void *memset(void *dst, int val, size_t n);
extern void *memcpy(void *dst, const void *src, size_t n);
extern uint16_t pci_find_device(uint16_t vendor_id, uint16_t device_id);

/* I/O端口访问 */
static inline uint8_t rtl_inb(uint16_t port)
{
    uint8_t val;
    __asm__ volatile("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static inline uint16_t rtl_inw(uint16_t port)
{
    uint16_t val;
    __asm__ volatile("inw %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static inline uint32_t rtl_inl(uint16_t port)
{
    uint32_t val;
    __asm__ volatile("inl %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static inline void rtl_outb(uint16_t port, uint8_t val)
{
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline void rtl_outw(uint16_t port, uint16_t val)
{
    __asm__ volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}

static inline void rtl_outl(uint16_t port, uint32_t val)
{
    __asm__ volatile("outl %0, %1" : : "a"(val), "Nd"(port));
}

/* V0.13.9: 虚拟→物理 DMA 地址转换 (分页内核: 栈/堆缓冲非恒等, 原直传虚拟地址导致网卡 DMA 读错页) */
static uint64_t rtl_virt_to_phys(void *p)
{
    extern uint64_t vmm64_virt_to_phys(uint64_t pml4_addr, uint64_t virt);
    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    uint64_t phys = vmm64_virt_to_phys(cr3, (uint64_t)(uintptr_t)p);
    if (phys == 0) return (uint64_t)(uintptr_t)p;  /* 兜底: 恒等 */
    return phys;
}

/* V0.13.9: 发送 DMA 缓冲 (对齐, rtl8139 DMA 要求地址对齐) */
static uint8_t rtl_tx_buffer[1600] __attribute__((aligned(16)));

/* 驱动状态 */
static uint16_t rtl_io_base = 0;
static uint8_t rtl_mac[6] = {0};
static uint8_t rtl_rx_buffer[8192 + 16] __attribute__((aligned(4)));
static uint16_t rtl_rx_offset = 0;
static int rtl_initialized = 0;

/* 统计 */
static uint32_t rtl_tx_count = 0;
static uint32_t rtl_rx_count = 0;

/* 等待发送完成 */
static int rtl_wait_tx(void)
{
    for (int i = 0; i < 100000; i++) {
        uint32_t status = rtl_inl(rtl_io_base + REG_TSD0);
        if (status & TSD_OK) return 0;
        if (status & 0x0400) return -1;  /* 发送失败 */
    }
    return -1;
}

/* 初始化RTL8139 */
int rtl8139_init(void)
{
    vga_puts("[RTL8139] Initializing...\n");

    /* 查找PCI设备 */
    rtl_io_base = pci_find_device(RTL8139_VENDOR_ID, RTL8139_DEVICE_ID);
    if (rtl_io_base == 0) {
        vga_puts("[RTL8139] Device not found\n");
        return -1;
    }
    vga_puts("[RTL8139] Found at I/O port ");
    vga_put_hex(rtl_io_base);
    vga_puts("\n");

    /* 软件复位 */
    rtl_outb(rtl_io_base + REG_CMD, CMD_RESET);
    for (int i = 0; i < 100000; i++) {
        if (!(rtl_inb(rtl_io_base + REG_CMD) & CMD_RESET)) break;
    }

    /* 读取MAC地址 */
    for (int i = 0; i < 6; i++) {
        rtl_mac[i] = rtl_inb(rtl_io_base + REG_IDR0 + i);
    }
    vga_puts("[RTL8139] MAC: ");
    for (int i = 0; i < 6; i++) {
        vga_put_hex(rtl_mac[i]);
        if (i < 5) vga_puts(":");
    }
    vga_puts("\n");

    /* 设置接收缓冲区地址（必须是物理地址） */
    uint32_t rx_buf_phys = (uint32_t)rtl_virt_to_phys(rtl_rx_buffer);
    rtl_outl(rtl_io_base + REG_RBSTART, rx_buf_phys);

    /* 配置接收：接受物理地址匹配+广播+多播，无wrap */
    rtl_outl(rtl_io_base + REG_RCR, RCR_APM | RCR_AB | RCR_AM | RCR_WRAP);

    /* 配置发送：默认 */
    rtl_outl(rtl_io_base + REG_TCR, 0x00000600);

    /* 清零中断掩码（禁用中断，使用轮询） */
    rtl_outw(rtl_io_base + REG_IMR, 0x0000);

    /* 清零中断状态 */
    rtl_outw(rtl_io_base + REG_ISR, 0xFFFF);

    /* 启用接收和发送 */
    rtl_outb(rtl_io_base + REG_CMD, CMD_RX_ENABLE | CMD_TX_ENABLE);

    /* 初始化接收偏移 */
    rtl_rx_offset = 0;
    rtl_outw(rtl_io_base + REG_CAPR, 0);

    rtl_initialized = 1;
    vga_puts("[OK] RTL8139 initialized\n");
    return 0;
}

/* 发送数据包 */
int rtl8139_send(const uint8_t *data, uint32_t length)
{
    if (!rtl_initialized || length == 0 || length > 1500) return -1;

    /* V0.13.9: 拷贝到内部对齐缓冲 (原调用方栈缓冲可能未对齐, DMA 读错) */
    {
        extern void ip_memcpy(void *dst, const void *src, int n);
        for (int i = 0; i < (int)length; i++) rtl_tx_buffer[i] = data[i];
    }

    /* 等待上一次发送完成 */
    if (rtl_wait_tx() != 0) {
        /* 超时，重置发送 */
        rtl_outl(rtl_io_base + REG_TSD0, 0);
    }

    /* 设置发送地址（物理地址）— V0.13.9: 虚拟→物理转换 */
    uint32_t data_phys = (uint32_t)rtl_virt_to_phys(rtl_tx_buffer);
    /* 临时调试: 打印 虚拟 vs 物理 */
    { extern void vga_puts(const char*); extern void vga_put_hex(uint64_t);
      vga_puts("[RTL] tx va="); vga_put_hex((uint64_t)(uintptr_t)data);
      vga_puts(" pa="); vga_put_hex(data_phys); vga_puts("\n"); }
    rtl_outl(rtl_io_base + REG_TSAD0, data_phys);

    /* 设置发送长度并启动发送 (V0.13.9: QEMU rtl8139 要求 bit13=0(host owns 清) 才接管; 0x8000 会被写掩码抹除) */
    rtl_outl(rtl_io_base + REG_TSD0, length);

    /* 等待发送完成 */
    int ret = rtl_wait_tx();
    if (ret == 0) {
        rtl_tx_count++;
    }

    { extern void vga_puts(const char*); extern void vga_put_hex(uint64_t);
      vga_puts("[RTL] tsd="); vga_put_hex(rtl_inl(rtl_io_base + REG_TSD0));
      vga_puts(" tsa="); vga_put_hex(rtl_inl(rtl_io_base + REG_TSAD0));
      vga_puts(" isr="); vga_put_hex(rtl_inw(rtl_io_base + 0x3E)); vga_puts("\n"); }  /* 临时调试 */
    return ret;
}

/* 轮询接收数据包 */
int rtl8139_recv(uint8_t *buffer, uint32_t max_size)
{
    if (!rtl_initialized) return -1;

    /* 检查是否有数据接收 */
    uint16_t isr = rtl_inw(rtl_io_base + REG_ISR);
    if (!(isr & ISR_ROK)) {
        return 0;  /* 没有数据 */
    }

    /* 清除接收中断 */
    rtl_outw(rtl_io_base + REG_ISR, ISR_ROK);

    /* 读取接收缓冲区 */
    uint16_t cbr = rtl_inw(rtl_io_base + REG_CBR);
    uint8_t *pkt = rtl_rx_buffer + rtl_rx_offset;

    /* 读取包头：状态(2字节) + 长度(2字节) */
    uint16_t pkt_status = *(uint16_t *)(pkt);
    uint16_t pkt_length = *(uint16_t *)(pkt + 2);

    if (!(pkt_status & 0x01)) {
        /* 接收错误，跳过 */
        rtl_rx_offset = (rtl_rx_offset + pkt_length + 4 + 3) & ~3;
        if (rtl_rx_offset >= 8192) rtl_rx_offset -= 8192;
        rtl_outw(rtl_io_base + REG_CAPR, rtl_rx_offset - 0x10);
        return -1;
    }

    /* 复制数据（跳过4字节包头） */
    uint32_t copy_size = pkt_length - 4;  /* 去掉CRC */
    if (copy_size > max_size) copy_size = max_size;
    memcpy(buffer, pkt + 4, copy_size);

    /* 更新接收偏移（对齐到4字节） */
    rtl_rx_offset = (rtl_rx_offset + pkt_length + 4 + 3) & ~3;
    if (rtl_rx_offset >= 8192) rtl_rx_offset -= 8192;
    rtl_outw(rtl_io_base + REG_CAPR, rtl_rx_offset - 0x10);

    rtl_rx_count++;
    return copy_size;
}

/* 获取MAC地址 */
void rtl8139_get_mac(uint8_t *mac)
{
    memcpy(mac, rtl_mac, 6);
}

/* 获取统计信息 */
void rtl8139_get_stats(uint32_t *tx, uint32_t *rx)
{
    if (tx) *tx = rtl_tx_count;
    if (rx) *rx = rtl_rx_count;
}
