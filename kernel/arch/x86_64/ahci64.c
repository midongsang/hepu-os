/* ============================================================================
 * 赫普OS 64位 - AHCI/SATA驱动
 * 从32位移植，支持AHCI主机总线适配器和SATA设备，DMA读写
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include "paging.h"   /* PAGE_PRESENT / PAGE_WRITABLE / PAGE_HUGE */

#define AHCI_MAX_PORTS 32
#define AHCI_MAX_CMD_SLOTS 32
#define AHCI_SECTOR_SIZE 512

/* AHCI HBA寄存器 */
typedef struct {
    uint32_t cap;
    uint32_t ghc;
    uint32_t is;
    uint32_t pi;
    uint32_t vs;
    uint32_t ccc_ctl;
    uint32_t ccc_ports;
    uint32_t em_loc;
    uint32_t em_ctl;
    uint32_t cap2;
    uint32_t bohc;
    uint8_t  reserved[0x60-0x2C];
    uint8_t  vendor[0x100-0x60];
} __attribute__((packed)) ahci_hba_t;

/* AHCI端口寄存器（偏移对齐 AHCI 规范：PxCLB/FB 64-bit，PxTFD 为 32-bit） */
typedef struct {
    uint64_t clb;
    uint64_t fb;
    uint32_t is;
    uint32_t ie;
    uint32_t cmd;
    uint32_t reserved1;
    uint32_t tfd;
    uint32_t sig;
    uint32_t ssts;
    uint32_t sctl;
    uint32_t serr;
    uint32_t sact;
    uint32_t ci;
    uint32_t sntf;
    uint32_t fbs;
    uint8_t  reserved2[0x70-0x48];
    uint32_t vendor[4];
} __attribute__((packed)) ahci_port_t;

/* AHCI命令头 */
typedef struct {
    uint8_t  cfl:5;
    uint8_t  a:1;
    uint8_t  w:1;
    uint8_t  p:1;
    uint8_t  r:1;
    uint8_t  b:1;
    uint8_t  c:1;
    uint8_t  reserved1:1;
    uint8_t  pmp:4;
    uint16_t prdtl;
    uint32_t prdbc;
    uint64_t ctba;
    uint32_t reserved2[4];
} __attribute__((packed)) ahci_cmd_header_t;

/* AHCI PRDT项 */
typedef struct {
    uint64_t dba;
    uint32_t reserved;
    uint32_t dbc:22;
    uint32_t reserved1:9;
    uint32_t i:1;
} __attribute__((packed)) ahci_prdt_t;

/* AHCI命令表：CFIS 0x00..0x3F，ACMD 0x40..0x4F，保留 0x50..0x7F，PRDT 起点 0x80。 */
typedef struct {
    uint8_t  cfis[64];
    uint8_t  acmd[16];
    uint8_t  reserved[48];   /* 0x50..0x7F */
    ahci_prdt_t prdt[1];     /* 0x80 */
} __attribute__((packed)) ahci_cmd_table_t;

/* 端口信息 */
typedef struct {
    int present;
    int port_num;
    ahci_port_t *regs;
    uint64_t command_list_phys;
    uint64_t fis_phys;
    uint64_t total_sectors;
    uint32_t sector_size;
    char model[41];
} ahci_port_info_t;

static int ahci64_exec_cmd(ahci_port_t *port, int port_num,
                           uint8_t command, uint64_t lba,
                           uint8_t *buffer, uint32_t count, int is_write);

extern void vga_puts(const char *s);
extern void vga_put_hex(uint64_t val);
extern uint64_t pmm64_alloc_page(void);
extern uint32_t pci_config_read32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset);
extern uint8_t  pci_config_read8(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset);
extern void pci_config_write32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint32_t val);

/* 中断路由所需的 APIC/IDT 接口（启动后期 IOAPIC 已就绪） */
extern int  apic64_ioapic_available(void);
extern int  apic64_set_irq_redirect(uint8_t irq, uint8_t vector, uint8_t delivery_mode,
                                    uint8_t dest_mode, uint8_t polarity,
                                    uint8_t trigger_mode, uint8_t dest_id);
extern uint32_t apic64_get_id(void);
extern void irq64_install_gate(int vector, uint64_t handler);

/* AHCI 控制器 PCI 中断向量（与 LAPIC timer 0x40 / IPI 0x50 错开） */
#define AHCI_IRQ_VECTOR 0x41

static ahci_hba_t *ahci_hba = NULL;
static ahci_port_info_t ahci_ports[AHCI_MAX_PORTS];
static int ahci_initialized = 0;
static int ahci_port_count = 0;

static uint64_t ahci_cmd_list_pages[AHCI_MAX_PORTS];
static uint64_t ahci_fis_pages[AHCI_MAX_PORTS];
static uint64_t ahci_cmd_table_pages[AHCI_MAX_PORTS];

/* 控制器在 PCI 总线上的位置与中断线（探测阶段记录） */
static uint8_t ahci_pci_bus  = 0xFF;
static uint8_t ahci_pci_dev  = 0xFF;
static uint8_t ahci_pci_func = 0xFF;
static uint8_t ahci_irq_line = 0xFF;   /* PCI 0x3C：固件已 swizzle 好的 GSI */
static uint8_t ahci_irq_pin  = 0;      /* PCI 0x3D：1=INTA ... 4=INTD */

/* 中断计数 + 每端口待处理 IS 快照（ISR 写，等待循环可读） */
static volatile uint64_t ahci64_irq_count = 0;
static volatile uint32_t ahci_port_is_pending[AHCI_MAX_PORTS];

static inline uint32_t ahci_readl(volatile const void *reg)
{
    return *(volatile const uint32_t *)reg;
}

static inline void ahci_writel(volatile void *reg, uint32_t val)
{
    *(volatile uint32_t *)reg = val;
}

/* 分页内核下数据缓冲(.bss/栈)非恒等映射；DMA PRDT 必须填物理地址，
 * 否则 HBA 把数据写到虚拟地址对应的错误物理页，缓冲读回全 0。 */
static uint64_t ahci_virt_to_phys(void *p)
{
    extern uint64_t vmm64_virt_to_phys(uint64_t pml4_addr, uint64_t virt);
    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    uint64_t phys = vmm64_virt_to_phys(cr3, (uint64_t)(uintptr_t)p);
    return phys ? phys : (uint64_t)(uintptr_t)p;
}

static void ahci_memset(void *dst, uint8_t val, int n)
{
    uint8_t *d = (uint8_t*)dst;
    for (int i = 0; i < n; i++) d[i] = val;
}

static void ahci_strcpy(char *dst, const char *src)
{
    while (*src) *dst++ = *src++;
    *dst = 0;
}

static int ahci_wait_port_ready(ahci_port_t *port, int timeout)
{
    while (timeout-- > 0) {
        uint32_t tfd = ahci_readl(&port->tfd);
        if (!(tfd & 0x80)) return 0;
        if (tfd & 0x01) return -1;
    }
    return -1;
}

static int ahci_wait_cmd_complete(ahci_port_t *port, int slot, int timeout)
{
    while (timeout-- > 0) {
        uint32_t ci = ahci_readl(&port->ci);
        if (!(ci & (1 << slot))) return 0;
        uint32_t tfd = ahci_readl(&port->tfd);
        /* TFD: bit0=ERR, bit5=DF(设备故障)，统一判错 */
        if (tfd & (0x01 | 0x20)) return -1;
    }
    return -1;
}

/* AHCI 中断服务：计数、清 HBA/端口 IS（写 1 清零），快照待处理位。
 * 命令完成判定仍由等待循环读 CI/TFD 复核；中断负责及时推送并留下计数证据。 */
void ahci64_irq_handler(void)
{
    ahci64_irq_count++;
    if (!ahci_hba) return;
    uint32_t gis = ahci_readl(&ahci_hba->is);
    if (gis) ahci_writel(&ahci_hba->is, gis);
    for (int i = 0; i < AHCI_MAX_PORTS; i++) {
        if (!ahci_ports[i].present) continue;
        ahci_port_t *p = ahci_ports[i].regs;
        uint32_t is = ahci_readl(&p->is);
        if (is) {
            ahci_writel(&p->is, is);
            ahci_port_is_pending[i] |= is;
        }
    }
}

/* 把控制器 PCI INTx 经 IOAPIC 路由到 AHCI 专用向量；失败则保留纯轮询兜底。 */
static void ahci64_register_irq(void)
{
    if (!apic64_ioapic_available()) {
        vga_puts("[AHCI64] IOAPIC unavailable, keep polling fallback\n");
        return;
    }
    if (ahci_irq_line == 0xFF || ahci_irq_line == 0) {
        vga_puts("[AHCI64] no PCI IRQ line assigned, keep polling fallback\n");
        return;
    }
    extern void irq_entry_65(void);
    irq64_install_gate(AHCI_IRQ_VECTOR, (uint64_t)irq_entry_65);
    /* PCI INTx：低电平有效(polarity=1)、级别触发(trigger=1)，物理投递到 BSP。 */
    int rc = apic64_set_irq_redirect(ahci_irq_line, AHCI_IRQ_VECTOR,
                                     0, 0, 1, 1, (uint8_t)apic64_get_id());
    vga_puts("[AHCI64] IRQ routed gsi=");
    vga_put_hex(ahci_irq_line);
    vga_puts(" pin="); vga_put_hex(ahci_irq_pin);
    vga_puts(" vec="); vga_put_hex(AHCI_IRQ_VECTOR);
    vga_puts(" rc="); vga_put_hex((uint64_t)rc);
    vga_puts("\n");
}

/* FLUSH CACHE EXT (0xEA)：无数据传输命令，PRDTL=0。写完后让设备把写缓存落盘，
 * 并等 TFD BSY 清除、DRDY 置位（设备回到就绪态）。 */
static int ahci64_flush_cache(ahci_port_t *port, int port_num)
{
    if (!port) return -1;
    int slot = -1;
    uint32_t ci = ahci_readl(&port->ci);
    uint32_t sact = ahci_readl(&port->sact);
    for (int i = 0; i < 32; i++) {
        if (!(ci & (1 << i)) && !(sact & (1 << i))) { slot = i; break; }
    }
    if (slot < 0) return -1;

    ahci_cmd_header_t *hdr = (ahci_cmd_header_t *)ahci_cmd_list_pages[port_num] + slot;
    ahci_cmd_table_t *tbl = (ahci_cmd_table_t *)(ahci_cmd_table_pages[port_num] + slot * 128);
    ahci_memset(hdr, 0, sizeof(*hdr));
    ahci_memset(tbl, 0, sizeof(*tbl));
    ((uint8_t *)hdr)[0] = 0x05;   /* cfl=5, W=0, 无数据传输 */
    hdr->prdtl = 0;
    hdr->ctba = ahci_cmd_table_pages[port_num] + slot * 128;

    uint8_t *f = tbl->cfis;
    f[0] = 0x27; f[1] = 0x80; f[2] = 0xEA; f[3] = 0;   /* 0xEA FLUSH CACHE EXT */
    f[4] = 0; f[5] = 0; f[6] = 0; f[7] = 0x40;
    f[8] = 0; f[9] = 0; f[10] = 0; f[11] = 0;
    f[12] = 0; f[13] = 0; f[14] = 0; f[15] = 0;

    ahci_writel(&port->is, 0xFFFFFFFF);
    ahci_writel(&port->ci, 1 << slot);
    if (ahci_wait_cmd_complete(port, slot, 1000000) != 0) return -1;
    return ahci_wait_port_ready(port, 100000);
}

/* 同步/关机路径：对所有已识别端口发 FLUSH CACHE EXT 并等 BSY/DRDY 干净。 */
void ahci64_sync_flush(void)
{
    for (int i = 0; i < AHCI_MAX_PORTS; i++) {
        if (!ahci_ports[i].present || ahci_ports[i].total_sectors == 0) continue;
        ahci64_flush_cache(ahci_ports[i].regs, i);
    }
}

/* PxCMD 位：bit0=ST, bit4=FRE(命令列表有效) */
#define AHCI_CMD_ST  0x01
#define AHCI_CMD_FRE 0x10

static int ahci_stop_cmd_engine(ahci_port_t *port)
{
    uint32_t cmd = ahci_readl(&port->cmd);
    cmd &= ~AHCI_CMD_ST;
    ahci_writel(&port->cmd, cmd);
    int timeout = 100000;
    while (timeout-- > 0) {
        if (!(ahci_readl(&port->cmd) & AHCI_CMD_FRE)) return 0;
    }
    return -1;
}

static int ahci_start_cmd_engine(ahci_port_t *port)
{
    uint32_t cmd = ahci_readl(&port->cmd);
    cmd |= AHCI_CMD_ST | AHCI_CMD_FRE;
    ahci_writel(&port->cmd, cmd);
    /* 等待 FRE 置位：命令列表真正可用后再下发命令 */
    for (int i = 0; i < 100000; i++) {
        if (ahci_readl(&port->cmd) & AHCI_CMD_FRE) return 0;
    }
    return -1;
}

/* IDENTIFY DEVICE：读 512B 识别扇区，填充容量（LBA48 优先，回落 LBA28） */
static int ahci64_identify_port(ahci_port_t *port, int port_num, ahci_port_info_t *info)
{
    static uint16_t ident[256];
    for (int i = 0; i < 256; i++) ident[i] = 0;
    int exrc = ahci64_exec_cmd(port, port_num, 0xEC, 0, (uint8_t *)ident, 1, 0);
    if (exrc != 0) return -1;

    uint32_t lba28 = (uint32_t)ident[60] | ((uint32_t)ident[61] << 16);
    uint64_t lba48 = 0;
    if (ident[83] & 0x400) {
        lba48 = (uint64_t)ident[100] | ((uint64_t)ident[101] << 16) |
                ((uint64_t)ident[102] << 32) | ((uint64_t)ident[103] << 48);
    }
    info->total_sectors = lba48 ? lba48 : lba28;
    return 0;
}

int ahci64_port_init(ahci_port_t *port, int port_num)
{
    if (!port) return -1;

    uint32_t ssts = ahci_readl(&port->ssts);
    uint32_t det = ssts & 0x0F;
    uint32_t ipm = (ssts >> 8) & 0x0F;
    if (det != 3 || ipm != 1) return -1;

    if (ahci_stop_cmd_engine(port) != 0) return -1;

    uint64_t cl_page = pmm64_alloc_page();
    if (!cl_page) return -1;
    ahci_cmd_list_pages[port_num] = cl_page;
    ahci_memset((void *)cl_page, 0, 4096);

    uint64_t fis_page = pmm64_alloc_page();
    if (!fis_page) return -1;
    ahci_fis_pages[port_num] = fis_page;
    ahci_memset((void *)fis_page, 0, 4096);

    uint64_t ct_page = pmm64_alloc_page();
    if (!ct_page) return -1;
    ahci_cmd_table_pages[port_num] = ct_page;
    ahci_memset((void *)ct_page, 0, 4096);

    port->clb = cl_page;
    port->fb = fis_page;
    ahci_writel(&port->is, 0xFFFFFFFF);
    ahci_writel(&port->ie, 0xFFFFFFFF);

    ahci_start_cmd_engine(port);

    if (ahci_wait_port_ready(port, 100000) != 0) return -1;

    uint32_t sig = ahci_readl(&port->sig);
    ahci_ports[port_num].present = 1;
    ahci_ports[port_num].port_num = port_num;
    ahci_ports[port_num].regs = port;
    ahci_ports[port_num].command_list_phys = cl_page;
    ahci_ports[port_num].fis_phys = fis_page;
    ahci_ports[port_num].sector_size = AHCI_SECTOR_SIZE;

    if (sig == 0x00000101) ahci_strcpy(ahci_ports[port_num].model, "SATA HDD");
    else if (sig == 0xEB140101) ahci_strcpy(ahci_ports[port_num].model, "ATAPI CD/DVD");
    else ahci_strcpy(ahci_ports[port_num].model, "SATA Device");

    vga_puts("[AHCI64] port ");
    char numstr[8];
    int n = port_num, i = 0;
    char tmp[8];
    do { tmp[i++] = n % 10 + '0'; } while ((n /= 10) > 0);
    int j = 0;
    while (i > 0) numstr[j++] = tmp[--i];
    numstr[j] = 0;
    vga_puts(numstr);
    vga_puts(": ");
    vga_puts(ahci_ports[port_num].model);

    /* IDENTIFY 填充容量；失败时 total_sectors 保持 0，disk_present 不计入 */
    if (ahci64_identify_port(port, port_num, &ahci_ports[port_num]) != 0) {
        vga_puts(" [identify FAIL]");
    } else {
        vga_puts(" sectors=");
        vga_put_hex(ahci_ports[port_num].total_sectors);
    }
    vga_puts("\n");

    return 0;
}

/* V0.13.63: 系统盘读取入口 — 选第一个有盘的 AHCI 端口
 * 真机 SATA 盘是 AHCI 控制器, 系统盘读取链(storage HAL / ext2)必须能走 AHCI,
 * 否则真机装系统到 SATA 后内核仍用 IDE 驱动读取 -> 读不到 rootfs */
int ahci64_disk_present(void)
{
    for (int i = 0; i < AHCI_MAX_PORTS; i++) {
        if (ahci_ports[i].present && ahci_ports[i].total_sectors > 0) return 1;
    }
    return 0;
}

int ahci64_read_disk(uint32_t lba, uint8_t count, uint8_t *buf)
{
    for (int i = 0; i < AHCI_MAX_PORTS; i++) {
        if (ahci_ports[i].present && ahci_ports[i].total_sectors > 0) {
            return ahci64_read_sectors(ahci_ports[i].regs, lba, buf, count);
        }
    }
    return -1;
}

int ahci64_write_disk(uint32_t lba, uint8_t count, const uint8_t *buf)
{
    for (int i = 0; i < AHCI_MAX_PORTS; i++) {
        if (ahci_ports[i].present && ahci_ports[i].total_sectors > 0) {
            return ahci64_write_sectors(ahci_ports[i].regs, lba, (uint8_t *)buf, count);
        }
    }
    return -1;
}

int ahci64_detect(ahci_hba_t *hba)
{
    if (!hba) return -1;
    ahci_hba = hba;

    uint32_t cap = ahci_readl(&hba->cap);
    uint32_t pi = ahci_readl(&hba->pi);

    uint32_t ghc = ahci_readl(&hba->ghc);
    ghc |= 0x80000000;
    ahci_writel(&hba->ghc, ghc);
    ghc = ahci_readl(&hba->ghc);
    ghc |= 0x02;
    ahci_writel(&hba->ghc, ghc);

    ahci_port_count = 0;
    int max_ports = (cap & 0x1F) + 1;
    if (max_ports > AHCI_MAX_PORTS) max_ports = AHCI_MAX_PORTS;

    for (int i = 0; i < max_ports; i++) {
        if (pi & (1 << i)) {
            ahci_port_t *port = (ahci_port_t *)((uint8_t *)hba + 0x100 + i * 0x80);
            if (ahci64_port_init(port, i) == 0) ahci_port_count++;
        }
    }

    return ahci_port_count;
}

static int ahci64_exec_cmd(ahci_port_t *port, int port_num,
                           uint8_t command, uint64_t lba,
                           uint8_t *buffer, uint32_t count, int is_write)
{
    if (!port || !buffer || count == 0) return -1;

    int slot = -1;
    uint32_t ci = ahci_readl(&port->ci);
    uint32_t sact = ahci_readl(&port->sact);
    for (int i = 0; i < 32; i++) {
        if (!(ci & (1 << i)) && !(sact & (1 << i))) { slot = i; break; }
    }
    if (slot < 0) return -1;

    ahci_cmd_header_t *cmd_hdr = (ahci_cmd_header_t *)ahci_cmd_list_pages[port_num] + slot;
    ahci_cmd_table_t *cmd_tbl = (ahci_cmd_table_t *)(ahci_cmd_table_pages[port_num] + slot * 128);

    ahci_memset(cmd_hdr, 0, sizeof(ahci_cmd_header_t));
    ahci_memset(cmd_tbl, 0, sizeof(ahci_cmd_table_t));

    /* 头部首字节：cfl=5(bit4:0)；bit6=W，1=写(HBA 从内存读数据写盘)。
     * 直接写字节，避免位域 ABI 差异导致方向位丢失。 */
    ((uint8_t *)cmd_hdr)[0] = 0x05 | (is_write ? 0x40 : 0);
    cmd_hdr->prdtl = 1;
    cmd_hdr->ctba = ahci_cmd_table_pages[port_num] + slot * 128;

    /* H2D Register FIS (0x27) 按 SATA 规范排布：
     * 3=feature 低, 4..6=LBA 低/中/高, 7=device, 8..10=LBA 24..47,
     * 11=feature 高, 12/13=扇区计数, 14=ICC, 15=control */
    uint8_t *cfis = cmd_tbl->cfis;
    cfis[0] = 0x27;
    cfis[1] = 0x80;
    cfis[2] = command;
    cfis[3] = 0;                                   /* feature */
    cfis[4] = (uint8_t)(lba & 0xFF);
    cfis[5] = (uint8_t)((lba >> 8) & 0xFF);
    cfis[6] = (uint8_t)((lba >> 16) & 0xFF);
    cfis[7] = 0x40;                                /* device: LBA 模式 */
    cfis[8] = (uint8_t)((lba >> 24) & 0xFF);
    cfis[9] = (uint8_t)((lba >> 32) & 0xFF);
    cfis[10] = (uint8_t)((lba >> 40) & 0xFF);
    cfis[11] = 0;                                  /* feature 高 8 位 */
    cfis[12] = (uint8_t)(count & 0xFF);            /* 扇区计数低 8 位 */
    cfis[13] = (uint8_t)((count >> 8) & 0xFF);    /* 扇区计数高 8 位 */
    cfis[14] = 0;                                  /* ICC */
    cfis[15] = 0;                                  /* control */

    cmd_tbl->prdt[0].dba = ahci_virt_to_phys(buffer);
    cmd_tbl->prdt[0].dbc = (count * 512) - 1;
    cmd_tbl->prdt[0].i = 1;

    ahci_writel(&port->is, 0xFFFFFFFF);
    ahci_writel(&port->ci, 1 << slot);

    int result = ahci_wait_cmd_complete(port, slot, 1000000);
    uint32_t tfd = ahci_readl(&port->tfd);
    if (tfd & (0x01 | 0x20)) return -1;
    return result;
}

int ahci64_read_sectors(ahci_port_t *port, uint64_t lba, uint8_t *buffer, uint32_t count)
{
    if (!port || !buffer || count == 0) return -1;
    int port_num = -1;
    for (int i = 0; i < AHCI_MAX_PORTS; i++) {
        if (ahci_ports[i].present && ahci_ports[i].regs == port) { port_num = i; break; }
    }
    if (port_num < 0) return -1;
    int result = ahci64_exec_cmd(port, port_num, 0x25, lba, buffer, count, 0);
    if (result != 0) return -1;
    return count;
}

int ahci64_write_sectors(ahci_port_t *port, uint64_t lba, const uint8_t *buffer, uint32_t count)
{
    if (!port || !buffer || count == 0) return -1;
    int port_num = -1;
    for (int i = 0; i < AHCI_MAX_PORTS; i++) {
        if (ahci_ports[i].present && ahci_ports[i].regs == port) { port_num = i; break; }
    }
    if (port_num < 0) return -1;
    int result = ahci64_exec_cmd(port, port_num, 0x35, lba, (uint8_t *)buffer, count, 1);
    if (result != 0) return -1;
    /* 写命令完成后立即 FLUSH CACHE EXT，等设备写缓存落盘、BSY/DRDY 干净。 */
    if (ahci64_flush_cache(port, port_num) != 0) {
        vga_puts("[AHCI64] flush cache FAIL\n");
    }
    return count;
}

ahci_port_t *ahci64_get_first_port(void)
{
    for (int i = 0; i < AHCI_MAX_PORTS; i++) {
        if (ahci_ports[i].present) return ahci_ports[i].regs;
    }
    return NULL;
}

/* 启动自检：每个已识别端口读 LBA0，并在末扇区做写-读回环，统计 mismatch 字节数。
 * 缓冲为静态 .bss（低地址恒等映射，虚拟=物理，可直接作 DMA 地址）。 */
static void ahci64_selftest(void)
{
    static uint8_t rd[512] __attribute__((aligned(16)));
    static uint8_t wr[512] __attribute__((aligned(16)));

    for (int i = 0; i < AHCI_MAX_PORTS; i++) {
        if (!ahci_ports[i].present || ahci_ports[i].total_sectors == 0) continue;
        ahci_port_t *port = ahci_ports[i].regs;

        for (int k = 0; k < 512; k++) rd[k] = 0;
        if (ahci64_read_sectors(port, 0, rd, 1) != 1) {
            vga_puts("[AHCI64] selftest port "); vga_put_hex((uint64_t)i);
            vga_puts(": read LBA0 FAIL\n");
            continue;
        }
        vga_puts("[AHCI64] selftest port "); vga_put_hex((uint64_t)i);
        vga_puts(" LBA0: ");
        for (int k = 0; k < 8; k++) { vga_put_hex(rd[k]); vga_puts(" "); }
        vga_puts(" sig="); vga_put_hex(rd[510]); vga_put_hex(rd[511]);
        vga_puts("\n");

        if (ahci_ports[i].total_sectors <= 1) continue;
        uint64_t tlba = ahci_ports[i].total_sectors - 1;
        for (int k = 0; k < 512; k++) wr[k] = (uint8_t)((k * 13u + 0x5Au) & 0xFF);
        vga_puts("[AHCI64] selftest irq before="); vga_put_hex(ahci64_irq_count); vga_puts("\n");
        int wres = ahci64_write_sectors(port, tlba, wr, 1);
        if (wres != 1) {
            vga_puts("[AHCI64] selftest: write FAIL\n");
            continue;
        }
        for (int k = 0; k < 512; k++) rd[k] = 0;
        int rres = ahci64_read_sectors(port, tlba, rd, 1);
        if (rres != 1) {
            vga_puts("[AHCI64] selftest: readback FAIL\n");
            continue;
        }
        int mismatch = 0;
        for (int k = 0; k < 512; k++) if (rd[k] != wr[k]) mismatch++;
        vga_puts("[AHCI64] selftest loopback lba="); vga_put_hex(tlba);
        vga_puts(" mismatch="); vga_put_hex((uint64_t)mismatch);
        vga_puts(" irq after="); vga_put_hex(ahci64_irq_count);
        vga_puts("\n");
    }
}

void ahci64_init(void)
{
    if (ahci_initialized) return;
    ahci_memset(ahci_ports, 0, sizeof(ahci_ports));
    ahci_port_count = 0;

    vga_puts("[AHCI64] Initializing SATA host adapter...\n");

    /* 通过PCI查找AHCI控制器 (class 0x01, subclass 0x06) */
    /* 简化：扫描PCI总线查找Mass Storage Controller */
    uint64_t hba_phys = 0;

    /* 扫描PCI总线（简化版，只扫描bus 0） */
    for (int dev = 0; dev < 32; dev++) {
        for (int func = 0; func < 8; func++) {
            uint32_t vendor = pci_config_read32(0, dev, func, 0);
            if (vendor == 0xFFFFFFFF) continue;
            uint32_t class_code = pci_config_read32(0, dev, func, 8);
            uint8_t base_class = (class_code >> 24) & 0xFF;
            uint8_t sub_class = (class_code >> 16) & 0xFF;
            if (base_class == 0x01 && sub_class == 0x06) {
                /* 记录 PCI 位置与中断线/引脚（中断路由用） */
                ahci_pci_bus  = 0;
                ahci_pci_dev  = (uint8_t)dev;
                ahci_pci_func = (uint8_t)func;
                ahci_irq_line = pci_config_read8(0, (uint8_t)dev, (uint8_t)func, 0x3C);
                ahci_irq_pin  = pci_config_read8(0, (uint8_t)dev, (uint8_t)func, 0x3D);
                /* 使能 Memory Space(bit1) + Bus Master(bit2)，清 INTx Disable(bit10)，
                 * 否则 MMIO/DMA 不通且控制器中断被 PCI 命令位屏蔽。 */
                uint32_t pcmd = pci_config_read32(0, dev, func, 0x04);
                pcmd |= 0x07;
                pcmd &= ~(1u << 10);
                pci_config_write32(0, dev, func, 0x04, pcmd);

                /* 扫描 BAR0..5，取第一个内存型(bit0=0)非零 BAR 作为 ABAR
                 * （不再写死 BAR5；不同控制器 ABAR 落点可能不同） */
                for (int bar_off = 0x10; bar_off <= 0x24; bar_off += 4) {
                    uint32_t bar_val = pci_config_read32(0, dev, func, bar_off);
                    if ((bar_val & 0x01) == 0 && (bar_val & ~0x0FU)) {
                        hba_phys = bar_val & ~0x0FULL;
                        break;
                    }
                }
                vga_puts("[AHCI64] Found AHCI at PCI 0:");
                char numstr[8];
                int n = dev, i = 0;
                char tmp[8];
                do { tmp[i++] = n % 10 + '0'; } while ((n /= 10) > 0);
                int j = 0;
                while (i > 0) numstr[j++] = tmp[--i];
                numstr[j] = 0;
                vga_puts(numstr);
                vga_puts(".");
                n = func; i = 0;
                do { tmp[i++] = n % 10 + '0'; } while ((n /= 10) > 0);
                j = 0;
                while (i > 0) numstr[j++] = tmp[--i];
                numstr[j] = 0;
                vga_puts(numstr);
                vga_puts("\n");
                break;
            }
        }
        if (hba_phys) break;
    }

    if (!hba_phys) {
        vga_puts("[AHCI64] No AHCI controller found\n");
        ahci_initialized = 1;
        return;
    }

    /* V0.13.63: 真机 AHCI BAR5 通常在 0xF0000000+ (PCI MMIO 高区),
     * 不在 vmm 身份映射范围(0~2GB) — 必须先把 BAR 所在 2MB 页映射进页表,
     * 否则访问寄存器即页错误崩溃 (QEMU 无 AHCI 故未暴露此 bug) */
    {
        extern uint64_t vmm64_get_pml4(void);
        extern int vmm64_map_page(uint64_t pml4_addr, uint64_t virt, uint64_t phys, uint64_t flags);
        uint64_t bar_base = hba_phys & ~0x1FFFFFULL; /* 2MB 对齐 */
        vmm64_map_page(vmm64_get_pml4(), bar_base, bar_base,
                       PAGE_PRESENT | PAGE_WRITE | (1 << 7));  /* 2MB 大页 */
    }
    ahci_hba_t *hba = (ahci_hba_t *)hba_phys;
    int count = ahci64_detect(hba);
    /* 控制器与端口就绪后再挂中断：此时 HBA IE / 端口 IE 已使能，
     * IOAPIC 在 apic64_switch_to_apic_mode() 阶段已初始化。 */
    ahci64_register_irq();
    if (count > 0) {
        vga_puts("[AHCI64] Initialized: ");
        char numstr[8];
        int n = count, i = 0;
        char tmp[8];
        do { tmp[i++] = n % 10 + '0'; } while ((n /= 10) > 0);
        int j = 0;
        while (i > 0) numstr[j++] = tmp[--i];
        numstr[j] = 0;
        vga_puts(numstr);
        vga_puts(" SATA device(s)\n");
    } else {
        vga_puts("[AHCI64] No SATA devices found\n");
    }

    ahci64_selftest();

    ahci_initialized = 1;
}
