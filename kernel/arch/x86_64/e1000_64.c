/*
 * e1000_64.c — Intel 82540EM (e1000) 极简驱动 (V0.13.9)
 *
 * 目标: QEMU socket 后端联网。rtl8139 模型在该 QEMU 构建下双向不通,
 * 故切换到 QEMU 支持最完整的 e1000 (82540EM)。
 * 仅实现: PCI 探测 / MMIO 恒等映射 / TX 单描述符轮询发送 / RX 单描述符轮询接收。
 */
#include <stdint.h>

/* ---- e1000 寄存器偏移 (MMIO, 32位) ---- */
#define E1000_CTRL      0x0000
#define E1000_STATUS    0x0008
#define E1000_ICR       0x00C0
#define E1000_IMS       0x00D0
#define E1000_RCTL      0x0100
#define E1000_TCTL      0x0400
#define E1000_TIPG      0x0410
#define E1000_RDBAL     0x2800
#define E1000_RDBAH     0x2804
#define E1000_RDLEN     0x2808
#define E1000_RDH       0x2810
#define E1000_RDT       0x2818
#define E1000_TDBAL     0x3800
#define E1000_TDBAH     0x3804
#define E1000_TDLEN     0x3808
#define E1000_TDH       0x3810
#define E1000_TDT       0x3818
#define E1000_RAL       0x5400
#define E1000_RAH       0x5404
#define E1000_MTA       0x5200

/* CTRL 位 */
#define E1000_CTRL_RST      0x04000000
#define E1000_CTRL_SLU      0x00000040
#define E1000_CTRL_ASDE     0x00000020
/* TCTL 位 */
#define E1000_TCTL_EN       0x00000002
#define E1000_TCTL_PSP      0x00000008
#define E1000_TCTL_CT      0x00000FF0
#define E1000_TCTL_COLD     0x00FFF000
/* RCTL 位 */
#define E1000_RCTL_EN       0x00000002
#define E1000_RCTL_SBP      0x00000004
#define E1000_RCTL_UPE      0x00000008
#define E1000_RCTL_MPE      0x00000010
#define E1000_RCTL_BAM      0x00008000
#define E1000_RCTL_BSIZE    0x00000300   /* 2KB */
#define E1000_RCTL_SEC      0x10000000
/* TX cmd 位 */
#define E1000_TXD_CMD_EOP   0x01
#define E1000_TXD_CMD_IFCS  0x02
#define E1000_TXD_CMD_RS    0x08
#define E1000_TXD_STAT_DD   0x01
/* RX stat 位 */
#define E1000_RXD_STAT_DD   0x01
#define E1000_RXD_STAT_EOP  0x02

extern void vga_puts(const char *s);
extern void vga_put_hex(uint64_t v);
extern uint16_t pci_find_device(uint16_t vendor_id, uint16_t device_id);
extern uint32_t pci_config_read32(int bus, int dev, int func, int off);

/* ---- 驱动状态 ---- */
static volatile uint32_t *e1000_regs = 0;   /* MMIO 虚拟地址 (恒等映射) */
static int e1000_initialized = 0;
static uint8_t e1000_mac[6] = {0};

/* TX/RX 描述符与缓冲 (静态数组, 恒等映射物理地址直用) */
typedef struct {
    uint64_t addr;
    uint16_t length;
    uint8_t  cso;
    uint8_t  cmd;
    uint8_t  status;
    uint8_t  css;
    uint16_t special;
} e1000_desc_t;

#define E1000_RING_SZ  8
static e1000_desc_t e1000_tx_desc[E1000_RING_SZ] __attribute__((aligned(16)));
static e1000_desc_t e1000_rx_desc[E1000_RING_SZ] __attribute__((aligned(16)));
static uint8_t e1000_tx_data[E1000_RING_SZ][2048] __attribute__((aligned(16)));
static uint8_t e1000_rx_data[E1000_RING_SZ][2048] __attribute__((aligned(16)));
static int e1000_tx_next = 0;
static int e1000_rx_next = 0;

/* 统计: 总字节 + 每秒速率 (V0.13.9 网络面板) */
static uint64_t e1000_tx_bytes = 0, e1000_rx_bytes = 0;
static uint32_t e1000_tx_rate = 0, e1000_rx_rate = 0;
static uint64_t e1000_stat_last_sec = 0;
static uint64_t e1000_tx_prev = 0, e1000_rx_prev = 0;

static inline uint32_t e1000_read(uint32_t reg)
{
    return e1000_regs[reg / 4];
}
static inline void e1000_write(uint32_t reg, uint32_t val)
{
    e1000_regs[reg / 4] = val;
}

int e1000_init(void)
{
    vga_puts("[E1000] Initializing...\n");

    /* 查找 e1000 (82540EM: 0x8086/0x100E; 也接受 82545EM 0x100F 等) */
    int found = 0;
    for (int bus = 0; bus < 8 && !found; bus++)
        for (int dev = 0; dev < 32 && !found; dev++)
            for (int func = 0; func < 8; func++) {
                uint16_t vendor = pci_config_read32(bus, dev, func, 0x00) & 0xFFFF;
                uint16_t device = (pci_config_read32(bus, dev, func, 0x00) >> 16) & 0xFFFF;
                if (vendor == 0x8086 && (device == 0x100E || device == 0x100F || device == 0x1000)) {
                    /* 读 BAR0 (MMIO) */
                    uint32_t bar0 = pci_config_read32(bus, dev, func, 0x10);
                    uint32_t mmio_phys = bar0 & 0xFFFFFFF0;
                    /* 启用总线主控 */
                    uint32_t cmd = pci_config_read32(bus, dev, func, 0x04);
                    cmd |= 0x00000007;
                    pci_config_read32(bus, dev, func, 0x04);
                    /* 写 command 寄存器需要写回 — 用低16位写 */
                    extern void pci_config_write32(int bus, int dev, int func, int off, uint32_t val);
                    pci_config_write32(bus, dev, func, 0x04, cmd);
                    vga_puts("[E1000] Found at MMIO ");
                    vga_put_hex(mmio_phys);
                    vga_puts(" (dev ");
                    vga_put_hex(dev);
                    vga_puts(")\n");

                    /* 恒等映射 MMIO (vmm64_map_page: virt==phys) */
                    extern int vmm64_map_page(uint64_t pml4_addr, uint64_t virt, uint64_t phys, uint64_t flags);
                    uint64_t cr3;
                    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
                    for (uint64_t off = 0; off < 0x20000; off += 0x1000) {
                        vmm64_map_page(cr3, mmio_phys + off, mmio_phys + off, 0x03 | 0x10);
                    }
                    e1000_regs = (volatile uint32_t *)(uintptr_t)mmio_phys;
                    found = 1;
                    break;
                }
            }
    if (!found) {
        vga_puts("[E1000] Device not found\n");
        return -1;
    }

    /* 软件复位 */
    e1000_write(E1000_CTRL, E1000_CTRL_RST);
    for (int i = 0; i < 1000000; i++) {
        if (!(e1000_read(E1000_CTRL) & E1000_CTRL_RST)) break;
    }

    /* 读 MAC (RAL/RAH 是网卡侧只读? 用 PCI 配置空间 EEPROM 替代 — 直接从寄存器读) */
    uint32_t ral = e1000_read(E1000_RAL);
    uint32_t rah = e1000_read(E1000_RAH);
    e1000_mac[0] = ral & 0xFF;
    e1000_mac[1] = (ral >> 8) & 0xFF;
    e1000_mac[2] = (ral >> 16) & 0xFF;
    e1000_mac[3] = (ral >> 24) & 0xFF;
    e1000_mac[4] = rah & 0xFF;
    e1000_mac[5] = (rah >> 8) & 0xFF;
    vga_puts("[E1000] MAC: ");
    for (int i = 0; i < 6; i++) { vga_put_hex(e1000_mac[i]); if (i < 5) vga_puts(":"); }
    vga_puts("\n");

    /* 设置 MAC 地址过滤 (RA0) */
    e1000_write(E1000_RAL, ral);
    e1000_write(E1000_RAH, rah | 0x80000000);   /* AV=1 */

    /* ---- RX 初始化 ---- */
    e1000_write(E1000_RCTL, 0);                  /* 先关 */
    for (int i = 0; i < E1000_RING_SZ; i++) {
        e1000_rx_desc[i].addr = (uint64_t)(uintptr_t)e1000_rx_data[i];
        e1000_rx_desc[i].status = 0;
    }
    e1000_write(E1000_RDBAL, (uint32_t)(uintptr_t)e1000_rx_desc);
    e1000_write(E1000_RDBAH, 0);
    e1000_write(E1000_RDLEN, E1000_RING_SZ * 16);
    e1000_write(E1000_RDH, 0);
    e1000_write(E1000_RDT, E1000_RING_SZ - 1);   /* 描述符全部可用 */
    e1000_rx_next = 0;
    e1000_write(E1000_RCTL, E1000_RCTL_EN | E1000_RCTL_UPE | E1000_RCTL_MPE |
                            E1000_RCTL_BAM | E1000_RCTL_BSIZE);

    /* ---- TX 初始化 ---- */
    e1000_write(E1000_TCTL, 0);
    for (int i = 0; i < E1000_RING_SZ; i++) {
        e1000_tx_desc[i].addr = (uint64_t)(uintptr_t)e1000_tx_data[i];
        e1000_tx_desc[i].status = E1000_TXD_STAT_DD;
    }
    e1000_write(E1000_TDBAL, (uint32_t)(uintptr_t)e1000_tx_desc);
    e1000_write(E1000_TDBAH, 0);
    e1000_write(E1000_TDLEN, E1000_RING_SZ * 16);
    e1000_write(E1000_TDH, 0);
    e1000_write(E1000_TDT, 0);
    e1000_tx_next = 0;
    e1000_write(E1000_TIPG, 0x0060200A);
    e1000_write(E1000_TCTL, E1000_TCTL_EN | E1000_TCTL_PSP |
                            E1000_TCTL_CT | (0x10 << 4) | E1000_TCTL_COLD | (0x40 << 12));

    /* 链接 + 使能发送时钟 */
    e1000_write(E1000_CTRL, e1000_read(E1000_CTRL) | E1000_CTRL_SLU);
    e1000_write(E1000_IMS, 0);                   /* 屏蔽中断, 轮询 */

    e1000_initialized = 1;
    vga_puts("[E1000] STATUS=");
    vga_put_hex(e1000_read(0x0008));
    vga_puts(" CTRL=");
    vga_put_hex(e1000_read(0x0000));
    vga_puts("\n");
    extern void eth64_update_mac(void);
    eth64_update_mac();
    vga_puts("[OK] E1000 initialized\n");
    return 0;
}

int e1000_send(const uint8_t *data, uint32_t length)
{
    if (!e1000_initialized || length == 0 || length > 1500) return -1;

    int i = e1000_tx_next;
    /* 等待描述符可用 */
    for (int w = 0; w < 2000000; w++) {
        if (e1000_tx_desc[i].status & E1000_TXD_STAT_DD) break;
    }
    if (!(e1000_tx_desc[i].status & E1000_TXD_STAT_DD)) return -1;

    /* 拷数据 */
    for (uint32_t k = 0; k < length; k++) e1000_tx_data[i][k] = data[k];
    for (int hh = 0; hh < 8 && hh < (int)length; hh++) {
        vga_put_hex((uint64_t)data[hh]); vga_puts(" ");
    }
    vga_puts("\n");

    e1000_tx_desc[i].length = length;
    e1000_tx_desc[i].cmd = E1000_TXD_CMD_EOP | E1000_TXD_CMD_IFCS | E1000_TXD_CMD_RS;
    e1000_tx_desc[i].status = 0;
    uint32_t tdh_before = e1000_read(E1000_TDH);
    e1000_write(E1000_TDT, (uint32_t)((i + 1) % E1000_RING_SZ));

    /* 等待发送完成: TDH 推进 或 描述符 DD */
    int sent = 0;
    for (int w = 0; w < 3000000; w++) {
        if (e1000_read(E1000_TDH) != tdh_before) { sent = 1; break; }
        if (e1000_tx_desc[i].status & E1000_TXD_STAT_DD) { sent = 1; break; }
    }
    if (!sent) {
        e1000_tx_next = (i + 1) % E1000_RING_SZ;
        return -1;
    }
    e1000_tx_bytes += (uint64_t)length;
    e1000_tx_next = (i + 1) % E1000_RING_SZ;
    return (int)length;
}

int e1000_recv(uint8_t *data, uint32_t max_len)
{
    if (!e1000_initialized) return -1;

    int i = e1000_rx_next;
    if (!(e1000_rx_desc[i].status & E1000_RXD_STAT_DD)) return -1;
    vga_puts("[E1000] RX got frame len=");
    vga_put_hex(e1000_rx_desc[i].length);
    vga_puts(" hdr: ");
    for (int hh = 0; hh < 8; hh++) { vga_put_hex((uint64_t)e1000_rx_data[i][hh]); vga_puts(" "); }
    vga_puts("\n");

    uint32_t len = e1000_rx_desc[i].length;
    if (len > max_len) len = max_len;
    for (uint32_t k = 0; k < len; k++) data[k] = e1000_rx_data[i][k];

    e1000_rx_desc[i].status = 0;
    e1000_rx_next = (i + 1) % E1000_RING_SZ;
    e1000_write(E1000_RDT, (uint32_t)(e1000_rx_next + E1000_RING_SZ - 1) % E1000_RING_SZ);

    e1000_rx_bytes += (uint64_t)len;
    return (int)len;
}

/* 每秒刷新一次速率 (由网络面板/桌面主循环调用) */
void e1000_tick_stats(void)
{
    extern uint64_t rtc64_get_timestamp(void);
    uint64_t sec = rtc64_get_timestamp();
    if (sec != e1000_stat_last_sec) {
        e1000_tx_rate = (uint32_t)(e1000_tx_bytes - e1000_tx_prev);
        e1000_rx_rate = (uint32_t)(e1000_rx_bytes - e1000_rx_prev);
        e1000_tx_prev = e1000_tx_bytes;
        e1000_rx_prev = e1000_rx_bytes;
        e1000_stat_last_sec = sec;
    }
}

void e1000_get_net_stats(uint64_t *tx_bytes, uint64_t *rx_bytes,
                         uint32_t *tx_rate, uint32_t *rx_rate)
{
    e1000_tick_stats();
    if (tx_bytes) *tx_bytes = e1000_tx_bytes;
    if (rx_bytes) *rx_bytes = e1000_rx_bytes;
    if (tx_rate)  *tx_rate  = e1000_tx_rate;
    if (rx_rate)  *rx_rate  = e1000_rx_rate;
}

/* V0.13.38: 链路状态查询 (STATUS bit1 = Link Up) */
int e1000_link_status(void)
{
    if (!e1000_initialized) return 0;
    return (e1000_read(E1000_STATUS) & 0x2) ? 1 : 0;
}

int e1000_get_mac(uint8_t *mac)
{
    for (int i = 0; i < 6; i++) mac[i] = e1000_mac[i];
    return 0;
}
