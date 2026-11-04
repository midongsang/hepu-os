/* ============================================================================
 * 赫普OS - AHCI/SATA驱动 (V6.88完整版)
 * 支持AHCI主机总线适配器和SATA设备，实现DMA读写
 * ========================================================================== */

#include <stdint.h>
#include "ahci.h"
#include "vga.h"
#include "string.h"
#include "pci.h"
#include "mm.h"
#include "paging.h"

#define AHCI_DEBUG 1

static ahci_hba_t *ahci_hba = NULL;
static ahci_port_info_t ahci_ports[AHCI_MAX_PORTS];
static int ahci_initialized = 0;
static int ahci_port_count = 0;

/* 命令列表和FIS的物理内存（每页4KB） */
static uint32_t ahci_cmd_list_pages[AHCI_MAX_PORTS];
static uint32_t ahci_fis_pages[AHCI_MAX_PORTS];
static uint32_t ahci_cmd_table_pages[AHCI_MAX_PORTS];

static inline uint32_t ahci_readl(volatile const void *reg)
{
    return *(volatile const uint32_t *)reg;
}

static inline void ahci_writel(volatile void *reg, uint32_t val)
{
    *(volatile uint32_t *)reg = val;
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

int ahci_port_init(ahci_port_t *port, int port_num)
{
    if (!port) return -1;

    uint32_t ssts = ahci_readl(&port->ssts);
    uint32_t det = ssts & 0x0F;
    uint32_t ipm = (ssts >> 8) & 0x0F;
    if (det != 3 || ipm != 1) return -1;

    if (ahci_stop_cmd_engine(port) != 0) return -1;

    uint32_t cl_page = mm_alloc_page();
    if (!cl_page) return -1;
    ahci_cmd_list_pages[port_num] = cl_page;
    memset((void *)cl_page, 0, 4096);

    uint32_t fis_page = mm_alloc_page();
    if (!fis_page) return -1;
    ahci_fis_pages[port_num] = fis_page;
    memset((void *)fis_page, 0, 4096);

    uint32_t ct_page = mm_alloc_page();
    if (!ct_page) return -1;
    ahci_cmd_table_pages[port_num] = ct_page;
    memset((void *)ct_page, 0, 4096);

    ahci_writel(&port->clb, cl_page);
    ahci_writel((volatile uint32_t *)&port->clb + 1, 0);
    ahci_writel(&port->fb, fis_page);
    ahci_writel((volatile uint32_t *)&port->fb + 1, 0);
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

    if (sig == 0x00000101) strcpy(ahci_ports[port_num].model, "SATA HDD");
    else if (sig == 0xEB140101) strcpy(ahci_ports[port_num].model, "ATAPI CD/DVD");
    else strcpy(ahci_ports[port_num].model, "SATA Device");
    strcpy(ahci_ports[port_num].serial, "UNKNOWN");
    strcpy(ahci_ports[port_num].firmware, "1.0");

    if (AHCI_DEBUG)
        vga_printf("[AHCI] port %d: %s (sig=0x%x)\n", port_num, ahci_ports[port_num].model, sig);
    return 0;
}

int ahci_detect(ahci_hba_t *hba)
{
    if (!hba) return -1;
    ahci_hba = hba;

    uint32_t cap = ahci_readl(&hba->cap);
    uint32_t pi = ahci_readl(&hba->pi);

    if (AHCI_DEBUG)
        vga_printf("[AHCI] HBA cap=0x%x ports=0x%x maxports=%d slots=%d\n",
                   cap, pi, (cap & 0x1F) + 1, ((cap >> 8) & 0x1F) + 1);

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
            if (ahci_port_init(port, i) == 0) ahci_port_count++;
        }
    }

    if (AHCI_DEBUG) vga_printf("[AHCI] found %d SATA device(s)\n", ahci_port_count);
    return ahci_port_count;
}

static int ahci_exec_cmd(ahci_port_t *port, int port_num,
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

    memset(cmd_hdr, 0, sizeof(ahci_cmd_header_t));
    memset(cmd_tbl, 0, sizeof(ahci_cmd_table_t));

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

    cmd_tbl->prdt[0].dba = (uint32_t)buffer;
    cmd_tbl->prdt[0].dbc = (count * 512) - 1;
    cmd_tbl->prdt[0].i = 1;

    ahci_writel(&port->is, 0xFFFFFFFF);
    ahci_writel(&port->ci, 1 << slot);

    int result = ahci_wait_cmd_complete(port, slot, 1000000);
    uint32_t tfd = ahci_readl(&port->tfd);
    if (tfd & 0x01) {
        if (AHCI_DEBUG) vga_printf("[AHCI] cmd 0x%x error tfd=0x%x\n", command, tfd);
        return -1;
    }
    return result;
}

int ahci_read_sectors(ahci_port_t *port, uint64_t lba, uint8_t *buffer, uint32_t count)
{
    if (!port || !buffer || count == 0) return -1;
    int port_num = -1;
    for (int i = 0; i < AHCI_MAX_PORTS; i++) {
        if (ahci_ports[i].present && ahci_ports[i].regs == port) { port_num = i; break; }
    }
    if (port_num < 0) return -1;
    int result = ahci_exec_cmd(port, port_num, 0x25, lba, buffer, count, 0);
    if (result != 0) return -1;
    return count;
}

int ahci_write_sectors(ahci_port_t *port, uint64_t lba, const uint8_t *buffer, uint32_t count)
{
    if (!port || !buffer || count == 0) return -1;
    int port_num = -1;
    for (int i = 0; i < AHCI_MAX_PORTS; i++) {
        if (ahci_ports[i].present && ahci_ports[i].regs == port) { port_num = i; break; }
    }
    if (port_num < 0) return -1;
    int result = ahci_exec_cmd(port, port_num, 0x35, lba, (uint8_t *)buffer, count, 1);
    if (result != 0) return -1;
    return count;
}

int ahci_port_list(ahci_port_info_t *ports, int max)
{
    if (!ports || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < AHCI_MAX_PORTS && count < max; i++) {
        if (ahci_ports[i].present) ports[count++] = ahci_ports[i];
    }
    return count;
}

ahci_port_t *ahci_get_first_port(void)
{
    for (int i = 0; i < AHCI_MAX_PORTS; i++) {
        if (ahci_ports[i].present) return ahci_ports[i].regs;
    }
    return NULL;
}

void ahci_init(void)
{
    if (ahci_initialized) return;
    memset(ahci_ports, 0, sizeof(ahci_ports));
    ahci_port_count = 0;

    vga_puts("[AHCI] Initializing SATA host adapter...\n");

    int dev_idx = pci_find_by_class(0x01, 0x06);
    if (dev_idx < 0) {
        vga_puts("[AHCI] No AHCI controller found\n");
        ahci_initialized = 1;
        return;
    }

    pci_device_t *pdev = &pci_devices[dev_idx];
    if (!pdev) {
        vga_puts("[AHCI] PCI device lookup failed\n");
        ahci_initialized = 1;
        return;
    }

    vga_printf("[AHCI] Found at PCI %x:%x.%d vend=%x dev=%x\n",
               pdev->bus, pdev->dev, pdev->func, pdev->vendor_id, pdev->device_id);

    uint32_t hba_phys = pdev->bar[5] & ~0x0F;
    if (!hba_phys) {
        vga_puts("[AHCI] BAR5 is zero\n");
        ahci_initialized = 1;
        return;
    }

    vga_printf("[AHCI] HBA MMIO at 0x%x\n", hba_phys);

    ahci_hba_t *hba = (ahci_hba_t *)hba_phys;
    if (hba_phys >= 0x10000000) {
        extern page_directory_t *paging_get_kernel_dir(void);
        page_directory_t *kdir = paging_get_kernel_dir();
        for (uint32_t p = hba_phys; p < hba_phys + 0x10000; p += 4096) {
            paging_map(kdir, p, p, 0x3);
        }
        __asm__ volatile("mov %%cr3, %%eax; mov %%eax, %%cr3" ::: "eax");
    }

    int count = ahci_detect(hba);
    if (count > 0) vga_printf("[AHCI] Initialized: %d SATA device(s)\n", count);
    else vga_puts("[AHCI] No SATA devices found\n");

    ahci_initialized = 1;
}
