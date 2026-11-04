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

/* AHCI端口寄存器 */
typedef struct {
    uint64_t clb;
    uint64_t fb;
    uint32_t is;
    uint32_t ie;
    uint32_t cmd;
    uint32_t reserved1;
    uint64_t tfd;
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

/* AHCI命令表 */
typedef struct {
    uint8_t  cfis[64];
    uint8_t  acmd[16];
    uint8_t  reserved[48];
    ahci_prdt_t prdt[1];
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

extern void vga_puts(const char *s);
extern uint64_t pmm64_alloc_page(void);
extern uint32_t pci_config_read32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset);

static ahci_hba_t *ahci_hba = NULL;
static ahci_port_info_t ahci_ports[AHCI_MAX_PORTS];
static int ahci_initialized = 0;
static int ahci_port_count = 0;

static uint64_t ahci_cmd_list_pages[AHCI_MAX_PORTS];
static uint64_t ahci_fis_pages[AHCI_MAX_PORTS];
static uint64_t ahci_cmd_table_pages[AHCI_MAX_PORTS];

static inline uint32_t ahci_readl(volatile const void *reg)
{
    return *(volatile const uint32_t *)reg;
}

static inline void ahci_writel(volatile void *reg, uint32_t val)
{
    *(volatile uint32_t *)reg = val;
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
        if (tfd & 0x01) return -1;
    }
    return -1;
}

static int ahci_stop_cmd_engine(ahci_port_t *port)
{
    uint32_t cmd = ahci_readl(&port->cmd);
    cmd &= ~0x01;
    ahci_writel(&port->cmd, cmd);
    int timeout = 100000;
    while (timeout-- > 0) {
        if (!(ahci_readl(&port->cmd) & 0x8000)) return 0;
    }
    return -1;
}

static int ahci_start_cmd_engine(ahci_port_t *port)
{
    uint32_t cmd = ahci_readl(&port->cmd);
    cmd |= 0x01;
    ahci_writel(&port->cmd, cmd);
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

    cmd_hdr->cfl = 5;
    cmd_hdr->w = is_write ? 1 : 0;
    cmd_hdr->prdtl = 1;
    cmd_hdr->ctba = ahci_cmd_table_pages[port_num] + slot * 128;

    uint8_t *cfis = cmd_tbl->cfis;
    cfis[0] = 0x27;
    cfis[1] = 0x80;
    cfis[2] = command;
    cfis[3] = (uint8_t)(lba & 0xFF);
    cfis[4] = (uint8_t)((lba >> 8) & 0xFF);
    cfis[5] = (uint8_t)((lba >> 16) & 0xFF);
    cfis[6] = 0x40 | (uint8_t)((lba >> 24) & 0x0F);
    cfis[7] = (uint8_t)((lba >> 32) & 0xFF);
    cfis[8] = (uint8_t)((lba >> 40) & 0xFF);
    cfis[9] = 0;
    cfis[10] = (uint8_t)(count & 0xFF);
    cfis[11] = (uint8_t)((count >> 8) & 0xFF);

    cmd_tbl->prdt[0].dba = (uint64_t)buffer;
    cmd_tbl->prdt[0].dbc = (count * 512) - 1;
    cmd_tbl->prdt[0].i = 1;

    ahci_writel(&port->is, 0xFFFFFFFF);
    ahci_writel(&port->ci, 1 << slot);

    int result = ahci_wait_cmd_complete(port, slot, 1000000);
    uint32_t tfd = ahci_readl(&port->tfd);
    if (tfd & 0x01) return -1;
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
    return count;
}

ahci_port_t *ahci64_get_first_port(void)
{
    for (int i = 0; i < AHCI_MAX_PORTS; i++) {
        if (ahci_ports[i].present) return ahci_ports[i].regs;
    }
    return NULL;
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
                /* 找到AHCI控制器 */
                uint32_t bar5 = pci_config_read32(0, dev, func, 0x24);
                hba_phys = bar5 & ~0x0F;
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

    ahci_initialized = 1;
}
