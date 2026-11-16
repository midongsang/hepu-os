/* ============================================================================
 * 赫普OS - 64位PCI配置空间访问 + 总线枚举
 * V0.10.6: 初始实现 (CF8/CFC 配置读写 + class/vendor 查找)
 * V0.59.x: 完整枚举
 *   - 递归遍历 PCI-PCI 桥 (header type=1, secondary/subordinate 寄存器
 *     0x19/0x1A), 带总线防环 (visited 位图)
 *   - 识别 multifunction (header type bit7), 扫描 func 1..7
 *   - 枚举表记录 bus/dev/func/vendor/device/class/header/IRQ 与 6 个 BAR
 *   - 64 位 MMIO BAR 正确拼接低/高两段; BAR 大小用「写全 1 再读回」探测
 *
 * 向后兼容: 下列旧导出符号签名与语义保持不变, 仅供老驱动使用:
 *   pci_config_read32 / pci_config_read16 / pci_config_read8 /
 *   pci_config_write32 / pci_find_class_bar0 / pci_find_device
 * 新代码请优先使用 pci64_find_by_class / pci64_find_by_vendor +
 * pci64_bar_addr / pci64_bar_size 取 64 位安全结果。
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>

/* PCI配置端口 */
#define PCI_CONFIG_ADDRESS  0xCF8
#define PCI_CONFIG_DATA     0xCFC

extern void vga_puts(const char *s);
extern void vga_put_hex(uint64_t v);

/* ---- 配置空间寄存器偏移 ---- */
#define PCI_VENDOR_ID      0x00
#define PCI_DEVICE_ID      0x02
#define PCI_COMMAND        0x04
#define PCI_CLASS_REV      0x08
#define PCI_HEADER_TYPE    0x0E
#define PCI_BAR0           0x10
#define PCI_PRIMARY_BUS    0x18
#define PCI_SECONDARY_BUS  0x19
#define PCI_SUBORD_BUS     0x1A
#define PCI_IRQ_LINE       0x3C
#define PCI_IRQ_PIN        0x3D

#define PCI_HDR_TYPE_MASK  0x7F
#define PCI_HDR_BRIDGE     0x01
#define PCI_HDR_MULTIFUNC  0x80

/* ---- 枚举表 ---- */
#define PCI64_MAX_DEVS     64

typedef struct {
    uint8_t  bus, dev, func;
    uint16_t vendor, device;
    uint8_t  base_class, sub_class, prog_if, revision;
    uint8_t  header_type;      /* 原始 header type (含 multifunction 位) */
    uint8_t  multifunction;
    uint32_t bar_raw[6];
    uint8_t  bar_is_io[6];
    uint8_t  bar_is_64[6];
    uint8_t  bar_pair_hi[6];   /* 1 = 该 BAR 是 64 位 BAR 的高半段, 不独立计数 */
    uint64_t bar_addr[6];
    uint64_t bar_size[6];
    uint8_t  irq_line, irq_pin;
    uint8_t  present;
} pci64_dev_t;

static pci64_dev_t g_pci_devs[PCI64_MAX_DEVS];
static int  g_pci_dev_count = 0;
static int  g_enum_done = 0;
static uint8_t g_bus_visited[32];   /* 256 总线位图, 防环 */

/* I/O端口访问 */
static inline uint32_t pci_inl(uint16_t port)
{
    uint32_t val;
    __asm__ volatile("inl %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static inline void pci_outl(uint16_t port, uint32_t val)
{
    __asm__ volatile("outl %0, %1" : : "a"(val), "Nd"(port));
}

/* 读取PCI配置空间的32位字 */
uint32_t pci_config_read32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset)
{
    uint32_t address = (uint32_t)((bus << 16) | (dev << 11) | (func << 8) |
                                  (offset & 0xFC) | 0x80000000);
    pci_outl(PCI_CONFIG_ADDRESS, address);
    return pci_inl(PCI_CONFIG_DATA);
}

/* 读取PCI配置空间的16位字 */
uint16_t pci_config_read16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset)
{
    uint32_t val = pci_config_read32(bus, dev, func, offset);
    return (uint16_t)((val >> ((offset & 2) * 8)) & 0xFFFF);
}

/* 读取PCI配置空间的8位字 */
uint8_t pci_config_read8(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset)
{
    uint32_t val = pci_config_read32(bus, dev, func, offset);
    return (uint8_t)((val >> ((offset & 3) * 8)) & 0xFF);
}

/* 写入PCI配置空间的32位字 */
void pci_config_write32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint32_t val)
{
    uint32_t address = (uint32_t)((bus << 16) | (dev << 11) | (func << 8) |
                                  (offset & 0xFC) | 0x80000000);
    pci_outl(PCI_CONFIG_ADDRESS, address);
    pci_outl(PCI_CONFIG_DATA, val);
}

static int bus_visited(uint8_t bus)
{
    return (g_bus_visited[bus >> 3] >> (bus & 7)) & 1;
}

static void bus_mark(uint8_t bus)
{
    g_bus_visited[bus >> 3] |= (uint8_t)(1u << (bus & 7));
}

/* BAR 大小探测开关: 1=对非显示类设备走「写全 1 读回」(标准 PCI BAR 探测);
 * 0=枚举全程只读配置空间, 不写任何 BAR。显示类设备的豁免见 decode_bar 内注释。 */
#define PCI64_PROBE_BAR_WRITES  1

/* ---- BAR 解码 + 大小探测 (写全 1 读回, 随后恢复原值) ---- */
static void pci64_decode_bar(pci64_dev_t *d, int idx)
{
    uint8_t bus = d->bus, dev = d->dev, func = d->func;
    uint32_t raw = d->bar_raw[idx];
    d->bar_is_io[idx] = (raw & 1u) ? 1 : 0;
    d->bar_is_64[idx] = 0;
    d->bar_pair_hi[idx] = 0;
    d->bar_addr[idx] = 0;
    d->bar_size[idx] = 0;

    /* 未实现 BAR: 读回全 0 或全 1 (如 VGA/网卡的保留 BAR6) */
    if (raw == 0 || raw == 0xFFFFFFFFu) return;

    uint8_t off = (uint8_t)(PCI_BAR0 + idx * 4);
    uint32_t saved = raw;

    /* 显示控制器 (class 0x03) 的帧缓冲/MMIO  aperture 已由 bootloader 交接给
     * gfx 栈在役使用; 对其 BAR 做「写全 1 读回」会扰动在役显存/寄存器窗口,
     * 实测导致桌面帧缓冲下半段不落地 (NONBLACK 回退)。故仅对非显示类设备
     * 做大小探测; 显示类只做只读地址解码。 */
    int do_probe = PCI64_PROBE_BAR_WRITES && d->base_class != 0x03;

    if (d->bar_is_io[idx]) {
        /* I/O BAR: 位 0=1, 位 1 保留, 地址位 31..2 */
        if (do_probe) {
            pci_config_write32(bus, dev, func, off, 0xFFFFFFFFu);
            uint32_t sz_raw = pci_config_read32(bus, dev, func, off);
            pci_config_write32(bus, dev, func, off, saved);
            d->bar_addr[idx] = raw & 0xFFFFFFFCu;
            uint32_t mask = sz_raw & 0xFFFFFFFCu;
            d->bar_size[idx] = (uint64_t)(~mask + 1u) & 0xFFFFFFFFu;
        } else {
            d->bar_addr[idx] = raw & 0xFFFFFFFCu;
            d->bar_size[idx] = 0;
        }
        return;
    }

    /* 内存 BAR: 位 0=0; 位[2:1] 类型: 00=32位, 10=64位 (01 保留) */
    uint32_t type = (raw >> 1) & 3u;
    uint8_t off_hi = (uint8_t)(PCI_BAR0 + (idx + 1) * 4);

    if (type == 2 && idx < 5) {
        uint32_t raw_hi = d->bar_raw[idx + 1];
        uint32_t saved_hi = raw_hi;
        uint32_t sz_lo = 0, sz_hi = 0;
        if (do_probe) {
            pci_config_write32(bus, dev, func, off, 0xFFFFFFFFu);
            sz_lo = pci_config_read32(bus, dev, func, off);
            pci_config_write32(bus, dev, func, off_hi, 0xFFFFFFFFu);
            sz_hi = pci_config_read32(bus, dev, func, off_hi);
            pci_config_write32(bus, dev, func, off, saved);
            pci_config_write32(bus, dev, func, off_hi, saved_hi);
        }

        d->bar_is_64[idx] = 1;
        d->bar_pair_hi[idx + 1] = 1;
        uint64_t addr = ((uint64_t)raw_hi << 32) | (uint64_t)(raw & 0xFFFFFFF0u);
        d->bar_addr[idx] = addr;
        if (do_probe) {
            uint64_t size = (~((uint64_t)sz_hi << 32 | (sz_lo & 0xFFFFFFF0u))) + 1u;
            d->bar_size[idx] = size;
        }
        d->bar_addr[idx + 1] = 0;
        d->bar_size[idx + 1] = 0;
    } else {
        if (do_probe) {
            pci_config_write32(bus, dev, func, off, 0xFFFFFFFFu);
            uint32_t sz_raw = pci_config_read32(bus, dev, func, off);
            pci_config_write32(bus, dev, func, off, saved);
            d->bar_addr[idx] = raw & 0xFFFFFFF0u;
            uint32_t mask = sz_raw & 0xFFFFFFF0u;
            d->bar_size[idx] = (uint64_t)(~mask + 1u) & 0xFFFFFFFFu;
        } else {
            d->bar_addr[idx] = raw & 0xFFFFFFF0u;
            d->bar_size[idx] = 0;
        }
    }
}

/* 登记一个功能号 */
static void pci64_probe_function(uint8_t bus, uint8_t dev, uint8_t func)
{
    if (g_pci_dev_count >= PCI64_MAX_DEVS) return;

    uint32_t vd = pci_config_read32(bus, dev, func, PCI_VENDOR_ID);
    uint16_t vendor = (uint16_t)(vd & 0xFFFFu);
    if (vendor == 0xFFFFu) return;

    pci64_dev_t *d = &g_pci_devs[g_pci_dev_count];
    for (int i = 0; i < 6; i++) { d->bar_raw[i] = 0; d->bar_addr[i] = 0; d->bar_size[i] = 0; }

    d->bus = bus; d->dev = dev; d->func = func;
    d->vendor = vendor;
    d->device = (uint16_t)(vd >> 16);

    uint32_t class_rev = pci_config_read32(bus, dev, func, PCI_CLASS_REV);
    d->revision   = (uint8_t)(class_rev & 0xFFu);
    d->prog_if    = (uint8_t)((class_rev >> 8) & 0xFFu);
    d->sub_class  = (uint8_t)((class_rev >> 16) & 0xFFu);
    d->base_class = (uint8_t)((class_rev >> 24) & 0xFFu);

    d->header_type = pci_config_read8(bus, dev, func, PCI_HEADER_TYPE);
    d->multifunction = (d->header_type & PCI_HDR_MULTIFUNC) ? 1 : 0;

    for (int i = 0; i < 6; i++)
        d->bar_raw[i] = pci_config_read32(bus, dev, func, PCI_BAR0 + i * 4);

    d->irq_line = pci_config_read8(bus, dev, func, PCI_IRQ_LINE);
    d->irq_pin  = pci_config_read8(bus, dev, func, PCI_IRQ_PIN);

    for (int i = 0; i < 6; i++) pci64_decode_bar(d, i);

    d->present = 1;
    g_pci_dev_count++;
}

/* 递归扫描一条总线; 桥的次级总线继续下钻, 位图防环 */
static void pci64_scan_bus(uint8_t bus)
{
    if (bus_visited(bus)) return;
    bus_mark(bus);

    for (int dev = 0; dev < 32; dev++) {
        uint16_t vendor = pci_config_read16(bus, (uint8_t)dev, 0, PCI_VENDOR_ID);
        if (vendor == 0xFFFFu) continue;

        uint8_t hdr = pci_config_read8(bus, (uint8_t)dev, 0, PCI_HEADER_TYPE);
        uint8_t htype = hdr & PCI_HDR_TYPE_MASK;
        uint8_t multi = hdr & PCI_HDR_MULTIFUNC;

        pci64_probe_function(bus, (uint8_t)dev, 0);

        if (multi) {
            for (int func = 1; func < 8; func++) {
                uint16_t v = pci_config_read16(bus, (uint8_t)dev, (uint8_t)func,
                                               PCI_VENDOR_ID);
                if (v != 0xFFFFu)
                    pci64_probe_function(bus, (uint8_t)dev, (uint8_t)func);
            }
        }

        /* PCI-PCI 桥: 下钻次级总线 */
        if (htype == PCI_HDR_BRIDGE) {
            uint8_t secondary = pci_config_read8(bus, (uint8_t)dev, 0, PCI_SECONDARY_BUS);
            uint8_t subordinate = pci_config_read8(bus, (uint8_t)dev, 0, PCI_SUBORD_BUS);
            if (secondary && secondary <= subordinate && secondary != bus) {
                for (uint8_t b = secondary; b <= subordinate; b++)
                    pci64_scan_bus(b);
            }
        }
    }
}

/* 串口转储枚举表 (与 QEMU monitor `info pci` 对账用) */
static void pci64_dump(void)
{
    vga_puts("[PCI64] enumerate count=");
    vga_put_hex((uint64_t)g_pci_dev_count);
    vga_puts("\n");
    for (int i = 0; i < g_pci_dev_count; i++) {
        pci64_dev_t *d = &g_pci_devs[i];
        vga_puts("[PCI64] bus=");   vga_put_hex(d->bus);
        vga_puts(" dev=");          vga_put_hex(d->dev);
        vga_puts(" func=");         vga_put_hex(d->func);
        vga_puts(" vend=");         vga_put_hex(d->vendor);
        vga_puts(" devid=");        vga_put_hex(d->device);
        vga_puts(" class=");        vga_put_hex(d->base_class);
        vga_puts("/");              vga_put_hex(d->sub_class);
        vga_puts("/");              vga_put_hex(d->prog_if);
        vga_puts(" hdr=");          vga_put_hex(d->header_type);
        vga_puts(" bar0=");         vga_put_hex(d->bar_addr[0]);
        vga_puts("\n");
    }
}

/* 枚举入口 (幂等) */
void pci64_enumerate(void)
{
    if (g_enum_done) return;
    g_pci_dev_count = 0;
    for (int i = 0; i < 32; i++) g_bus_visited[i] = 0;
    pci64_scan_bus(0);
    g_enum_done = 1;
    pci64_dump();
}

/* 启动期枚举 (供驱动框架注册调用; 枚举本身幂等) */
void pci64_init(void)
{
    pci64_enumerate();
}

/* ---- 新查找 API ---- */
int pci64_device_count(void)
{
    pci64_enumerate();
    return g_pci_dev_count;
}

const pci64_dev_t *pci64_device_at(int idx)
{
    pci64_enumerate();
    if (idx < 0 || idx >= g_pci_dev_count) return 0;
    return &g_pci_devs[idx];
}

/* 按 class+vendor 查设备; prog_if=0xFF 不匹配协议接口号 */
const pci64_dev_t *pci64_find_by_class(uint8_t base_class, uint8_t sub_class,
                                       uint8_t prog_if)
{
    pci64_enumerate();
    for (int i = 0; i < g_pci_dev_count; i++) {
        pci64_dev_t *d = &g_pci_devs[i];
        if (d->base_class == base_class && d->sub_class == sub_class &&
            (prog_if == 0xFF || d->prog_if == prog_if))
            return d;
    }
    return 0;
}

const pci64_dev_t *pci64_find_by_vendor(uint16_t vendor_id, uint16_t device_id)
{
    pci64_enumerate();
    for (int i = 0; i < g_pci_dev_count; i++) {
        pci64_dev_t *d = &g_pci_devs[i];
        if (d->vendor == vendor_id && d->device == device_id)
            return d;
    }
    return 0;
}

uint8_t pci64_bus(const pci64_dev_t *d)  { return d ? d->bus : 0; }
uint8_t pci64_dev(const pci64_dev_t *d)  { return d ? d->dev : 0; }
uint8_t pci64_func(const pci64_dev_t *d) { return d ? d->func : 0; }

uint64_t pci64_bar_addr(const pci64_dev_t *d, int idx)
{
    if (!d || idx < 0 || idx >= 6) return 0;
    return d->bar_addr[idx];
}

uint64_t pci64_bar_size(const pci64_dev_t *d, int idx)
{
    if (!d || idx < 0 || idx >= 6) return 0;
    return d->bar_size[idx];
}

/* ============================================================================
 * 向后兼容旧接口 (语义不变, 仅供老驱动使用)
 * ========================================================================== */

/* 按Vendor/Device ID查找设备，返回I/O端口基地址（BAR0） */
/* V0.13.64: 按 class 找设备, 返回 BAR0 值 (0=未找到)
 * base_class/sub_class/prog_if (prog_if=0xFF 不匹配) */
uint32_t pci_find_class_bar0(uint8_t base_class, uint8_t sub_class, uint8_t prog_if)
{
    const pci64_dev_t *d = pci64_find_by_class(base_class, sub_class, prog_if);
    if (!d) return 0;
    return d->bar_raw[0] & ~0x0Fu;
}

uint16_t pci_find_device(uint16_t vendor_id, uint16_t device_id)
{
    const pci64_dev_t *d = pci64_find_by_vendor(vendor_id, device_id);
    if (!d) return 0;

    /* 扫描所有BAR (0x10-0x24)，取第一个I/O空间型(bit0=1)非零基址 */
    uint16_t io_base = 0;
    for (int bar_off = 0x10; bar_off <= 0x24; bar_off += 4) {
        uint32_t bar = pci_config_read32(d->bus, d->dev, d->func, bar_off);
        if ((bar & 0x01) && (bar & 0xFFFFFFFC)) {   /* I/O型且非零 */
            io_base = (uint16_t)(bar & 0xFFFC);
            break;
        }
    }

    /* 启用总线主控和I/O访问 */
    uint16_t command = pci_config_read16(d->bus, d->dev, d->func, PCI_COMMAND);
    command |= 0x07;  /* I/O空间 + 内存空间 + 总线主控 */
    pci_config_write32(d->bus, d->dev, d->func, PCI_COMMAND, command);

    return io_base;
}
