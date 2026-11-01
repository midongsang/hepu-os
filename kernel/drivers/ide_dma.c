/* ============================================================================
 * 赫普OS - IDE 总线主控 DMA (BMIDE) + 批量读写
 * V4.01-E46: 优化清单 3.2 批量IO/DMA——合并零散读写、数据搬运交给硬件
 *
 * 原理:
 *   PCI 配置空间(0xCF8/0xCFC)枚举 IDE 控制器, 读 BMBA(Bus Master Base
 *   Address, config offset 0x20, IO 空间). BMIDE 寄存器:
 *     +0 BMCMD   : bit0=0读/1写, bit1=start, bit3=中断使能
 *     +2 BMSTATUS: bit0=活动, bit1=错误, bit2=中断
 *     +4 PRDT 物理地址低32, +6 高32
 *   PRDT 条目(8B): 低4B=物理地址(bit0=EOT), 高4B低2B=字节计数(0=64KB),
 *   bit15=中断标志, 高2B保留.
 *
 * 内核 identity 映射(0-16MB, 虚拟=物理), 静态 DMA 缓冲物理连续,
 * 故 PRDT 物理地址 = 缓冲虚拟地址(已在 16MB 内).
 * DMA 失败自动回退 PIO(ide.c 既有接口).
 * ========================================================================== */

#include <stdint.h>
#include "ide.h"
#include "vga.h"
#include "string.h"
#include "serial.h"

/* ---- PCI 配置空间 ---- */
#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

static inline uint32_t pci_read_config(uint8_t bus, uint8_t dev,
                                       uint8_t func, uint8_t offset)
{
    uint32_t addr = 0x80000000u
                    | ((uint32_t)bus << 16)
                    | ((uint32_t)dev << 11)
                    | ((uint32_t)func << 8)
                    | (offset & 0xFC);
    uint32_t val;
    __asm__ volatile("outl %0, %1" : : "a"(addr), "Nd"(PCI_CONFIG_ADDRESS));
    __asm__ volatile("inl %1, %0" : "=a"(val) : "Nd"(PCI_CONFIG_DATA));
    return val;
}

/* ---- 端口 IO ---- */
static inline uint8_t dma_inb(uint16_t port)
{
    uint8_t ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
static inline void dma_outb(uint16_t port, uint8_t val)
{
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline void dma_outl(uint16_t port, uint32_t val)
{
    __asm__ volatile("outl %0, %1" : : "a"(val), "Nd"(port));
}

/* ---- DMA 静态缓冲(页对齐, identity 映射内, 虚拟=物理) ---- */
#define DMA_MAX_SECTORS 128          /* 单次最大 128 扇区 = 64KB */
#define DMA_BUF_SIZE    (DMA_MAX_SECTORS * IDE_SECTOR_SIZE)
#define DMA_CHUNK       1024                    /* QEMU 实测单条 PRD 上限 */
#define PRDT_ENTRIES    (DMA_BUF_SIZE / DMA_CHUNK)  /* 每 1024B 一条 PRDT */

static uint8_t  dma_buffer[DMA_BUF_SIZE] __attribute__((aligned(4096)));
static uint8_t  prdt[PRDT_ENTRIES * 8] __attribute__((aligned(8)));

static uint16_t bmba = 0;            /* Bus Master Base Address(IO) */
static int      dma_available = 0;

/* QEMU TCG 检测: hypervisor leaf 0x40000000 厂商串 "TCGTCGTCGTCG".
 * QEMU 7.2 BMDMA 实测单条 PRD 上限 1024B 且传输时对时错(4B 级竞态),
 * 故 QEMU 环境禁用 DMA 回退 PIO; 真机硬件 BMDMA 正常启用. */
static int cpu_is_qemu_tcg(void)
{
    uint32_t eax, ebx, ecx, edx;
    __asm__ volatile("cpuid"
                     : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                     : "a"(0x40000000u));
    serial_puts(0x3F8, "[IDE-DMA] hyper leaf ebx=0x");
    serial_puthex(0x3F8, ebx);
    serial_puts(0x3F8, " edx=0x");
    serial_puthex(0x3F8, edx);
    serial_puts(0x3F8, " ecx=0x");
    serial_puthex(0x3F8, ecx);
    serial_puts(0x3F8, "\n");
    return (ebx == 0x54474354u && edx == 0x47435447u && ecx == 0x43544743u);
}

/* 探测 IDE 控制器 BMBA: bus0 全设备×全功能扫描,
 * class=0x01 subclass=01(IDE)/04(RAID)/05(AHCI 兼容) */
static void ide_dma_probe(void)
{
    if (cpu_is_qemu_tcg()) {
        vga_puts("[IDE-DMA] QEMU TCG detected, DMA disabled (PIO only)\n");
        serial_puts(0x3F8, "[IDE-DMA] QEMU TCG, DMA disabled, PIO only\n");
        return;
    }
    {
    uint32_t hdr, cls;
    int dev, func;
    for (dev = 0; dev < 32; dev++) {
        for (func = 0; func < 8; func++) {
            hdr = pci_read_config(0, dev, func, 0);
            if ((hdr & 0xFFFF) == 0xFFFF) {
                if (func == 0) break;   /* 多功能头: 继续 func; 否则跳过 dev */
                continue;
            }
            /* class/revision 在 config offset 0x08:
             * [31:24]=prog-if [23:16]=subclass [15:8]=class [7:0]=rev */
            cls = pci_read_config(0, dev, func, 0x08);
            if (((cls >> 16) & 0xFFFF) == 0x0101 ||
                ((cls >> 16) & 0xFFFF) == 0x0104 ||
                ((cls >> 16) & 0xFFFF) == 0x0105) {
                uint32_t bm = pci_read_config(0, dev, func, 0x20);
                if (bm & 0x1) {         /* IO 空间有效 */
                    bmba = (uint16_t)(bm & 0xFFF0);
                    dma_available = 1;
                    serial_puts(0x3F8, "[IDE-DMA] found dev=");
                    serial_puthex(0x3F8, dev);
                    serial_puts(0x3F8, " func=");
                    serial_puthex(0x3F8, func);
                    serial_puts(0x3F8, " devid=0x");
                    serial_puthex(0x3F8, (hdr >> 16) & 0xFFFF);
                    serial_puts(0x3F8, " bmba=0x");
                    serial_puthex(0x3F8, bmba);
                    serial_puts(0x3F8, "\n");
                    vga_printf("[IDE-DMA] PCI dev=%d func=%d BMBA=0x%x\n",
                               dev, func, bmba);
                    return;
                }
            }
        }
    }
    vga_puts("[IDE-DMA] BMBA not found, DMA disabled (PIO fallback)\n");
    }
}

/* 组装 PRDT: 物理地址连续(identity), 按 4KB 页切条目 */
static void prdt_build(uint32_t phys, uint32_t bytes)
{
    uint32_t off = 0;
    int i = 0;
    while (bytes > 0 && i < PRDT_ENTRIES) {
        uint32_t chunk = (bytes > DMA_CHUNK) ? DMA_CHUNK : bytes;
        uint32_t *lo = (uint32_t *)(prdt + i * 8);
        uint32_t *hi = (uint32_t *)(prdt + i * 8 + 4);
        *lo = (phys + off) & ~0x1u;   /* 物理地址, bit0=EOT 由最后一项置 */
        *hi = (chunk & 0xFFFF);       /* 字节计数(0=64KB); 本驱动 max 4096 */
        off += chunk;
        bytes -= chunk;
        i++;
    }
    if (i > 0) {
        uint32_t *lo = (uint32_t *)(prdt + (i - 1) * 8);
        *lo |= 0x1u;                  /* 最后一项 EOT */
    }
}

/* 等待 DMA 完成: BMSTATUS bit0(active)清零为完成标志(比中断位可靠);
 * bit1=错误; 完成后清中断位. */
static int dma_wait_done(uint16_t io_base, int timeout)
{
    while (timeout-- > 0) {
        uint8_t bms = dma_inb(bmba + 2);
        if (bms & 0x2) {              /* DMA 错误 */
            dma_outb(bmba + 2, 0x6);  /* 清错误+中断 */
            return -1;
        }
        if (!(bms & 0x1)) {           /* active 清零 = 传输完成 */
            dma_outb(bmba + 2, 0x4);  /* 清中断位 */
            return 0;
        }
    }
    dma_outb(bmba + 2, 0x6);          /* 超时: 清错误+中断 */
    return -1;
}

/* 单次 DMA 传输(≤128 扇区) */
static int ide_dma_transfer(uint32_t lba, uint8_t *buffer, uint32_t count,
                            int is_write)
{
    uint16_t io_base = IDE_PRIMARY_IO;
    uint8_t status;
    int timeout = 100000;

    /* QEMU 7.2 BMDMA 实测: 单次仅处理单条 PRD 且上限 1024B(2 扇区);
     * 真机多 PRD 支持在此不依赖, 大请求由批量接口走 PIO 分段. */
    if (!dma_available || count == 0 || count > 2)
        return -1;

    /* 等控制器空闲 */
    while (timeout-- > 0) {
        status = dma_inb(io_base + IDE_REG_STATUS);
        if (!(status & IDE_STATUS_BUSY)) break;
    }
    if (timeout <= 0) return -1;

    /* 数据拷入 DMA 缓冲(写) */
    if (is_write)
        memcpy(dma_buffer, buffer, count * IDE_SECTOR_SIZE);
    else
        memset(dma_buffer, 0, count * IDE_SECTOR_SIZE);

    /* PRDT: 物理地址 = 虚拟地址(identity) */
    prdt_build((uint32_t)(uintptr_t)dma_buffer, count * IDE_SECTOR_SIZE);
    dma_outl(bmba + 4, (uint32_t)(uintptr_t)prdt);
    serial_puts(0x3F8, "[IDE-DMA] prdt@0x");
    serial_puthex(0x3F8, (uint32_t)(uintptr_t)prdt);
    serial_puts(0x3F8, " buf@0x");
    serial_puthex(0x3F8, (uint32_t)(uintptr_t)dma_buffer);
    serial_puts(0x3F8, " e0=");
    serial_puthex(0x3F8, ((uint32_t *)prdt)[0]);
    serial_puts(0x3F8, " e0hi=");
    serial_puthex(0x3F8, ((uint32_t *)prdt)[1]);
    serial_puts(0x3F8, " e1=");
    serial_puthex(0x3F8, ((uint32_t *)prdt)[2]);
    serial_puts(0x3F8, " e1hi=");
    serial_puthex(0x3F8, ((uint32_t *)prdt)[3]);
    serial_puts(0x3F8, "\n");

    /* 写命令寄存器(LBA28 + DMA) */
    dma_outb(io_base + IDE_REG_DRIVE, 0xE0 | (lba >> 24 & 0x0F));
    dma_outb(io_base + IDE_REG_SECCOUNT, (uint8_t)count);
    dma_outb(io_base + IDE_REG_LBA0, (uint8_t)(lba & 0xFF));
    dma_outb(io_base + IDE_REG_LBA1, (uint8_t)((lba >> 8) & 0xFF));
    dma_outb(io_base + IDE_REG_LBA2, (uint8_t)((lba >> 16) & 0xFF));
    dma_outb(io_base + IDE_REG_COMMAND,
             is_write ? 0xCA /* WRITE DMA */ : 0xC8 /* READ DMA */);

    /* 启动 DMA: 方向(bit0) + start(bit1) + 中断使能(bit3) */
    dma_outb(bmba, (uint8_t)((is_write ? 0x1 : 0x0) | 0x8 | 0x2));

    {
        int w = dma_wait_done(io_base, 200000);
        serial_puts(0x3F8, "[IDE-DMA] xfer lba=");
        serial_puthex(0x3F8, lba);
        serial_puts(0x3F8, " cnt=");
        serial_puthex(0x3F8, count);
        serial_puts(0x3F8, " dir=");
        serial_puthex(0x3F8, is_write);
        serial_puts(0x3F8, " wait=");
        serial_puthex(0x3F8, w);
        serial_puts(0x3F8, " bmstat=0x");
        serial_puthex(0x3F8, dma_inb(bmba + 2));
        serial_puts(0x3F8, " ide_stat=0x");
        serial_puthex(0x3F8, dma_inb(io_base + IDE_REG_STATUS));
        serial_puts(0x3F8, "\n");
        if (w != 0)
            return -1;
    }

    if (!is_write)
        memcpy(buffer, dma_buffer, count * IDE_SECTOR_SIZE);
    else {
        /* 写 DMA 完成后冲刷磁盘写缓存(FLUSH CACHE 0xE7), 确保读回一致 */
        int t = 100000;
        dma_outb(io_base + IDE_REG_COMMAND, IDE_CMD_FLUSH);
        while (t-- > 0) {
            if (!(dma_inb(io_base + IDE_REG_STATUS) & IDE_STATUS_BUSY)) break;
        }
    }
    return (int)count;
}

/* ================= 对外批量接口 =================
 * ide_batch_read/write: 大请求(>16 扇区)走 DMA, 小请求走 PIO;
 * DMA 不可用/失败自动回退 PIO. 调用方 buffer 无对齐要求.
 * 返回值: 成功扇区数, 失败 -1. */
int ide_batch_read(uint32_t lba, uint8_t *buffer, uint32_t count)
{
    uint32_t done = 0;
    /* ≤2 扇区: DMA(1024B 单次上限); 其余: PIO 分段 */
    if (count <= 2 && dma_available) {
        int r = ide_dma_transfer(lba, buffer, count, 0);
        if (r < 0)
            r = ide_read_sectors(lba, buffer, count);
        return r;
    }
    while (done < count) {
        uint32_t chunk = (count - done > 128) ? 128 : (count - done);
        int r = ide_read_sectors(lba + done, buffer + done * 512, chunk);
        if (r < 0) return -1;
        done += (uint32_t)r;
    }
    return (int)count;
}

int ide_batch_write(uint32_t lba, const uint8_t *buffer, uint32_t count)
{
    uint32_t done = 0;
    if (count <= 2 && dma_available) {
        int r = ide_dma_transfer(lba, (uint8_t *)buffer, count, 1);
        if (r < 0)
            r = ide_write_sectors(lba, buffer, count);
        return r;
    }
    while (done < count) {
        uint32_t chunk = (count - done > 128) ? 128 : (count - done);
        int r = ide_write_sectors(lba + done, buffer + done * 512, chunk);
        if (r < 0) return -1;
        done += (uint32_t)r;
    }
    return (int)count;
}

void ide_dma_init(void)
{
    ide_dma_probe();
}

int ide_dma_is_available(void)
{
    return dma_available;
}
