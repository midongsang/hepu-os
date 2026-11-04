/* ============================================================================
 * 赫普OS 64位 - USB 1.1 UHCI 主机控制器驱动 + HID 鼠标/键盘
 * V0.1.0: UHCI (Intel PIIX3/PIIX4) 初始化 + 枚举 + HID 中断传输
 * 参考: Linux drivers/usb/host/uhci-hcd.c 结构 (队列头QH/传输描述符TD)
 * 用法: usb64_init() 在桌面启动前调用; usb64_poll() 在主循环调用
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>

extern void *memset(void *s, int c, unsigned long n);
extern void vga_puts(const char *s);
extern int ehci64_init(void);
extern int ehci64_present(void);

/* V0.13.8 P5: 内部扩展键码 (与 PS/2 统一; 修复 USB 方向键 0xE0-0xE3 前缀冲突) */
#define KEY_EXT_UP      0x90
#define KEY_EXT_DOWN    0x91
#define KEY_EXT_LEFT    0x92
#define KEY_EXT_RIGHT   0x93
#define KEY_EXT_PGUP    0x94
#define KEY_EXT_PGDN    0x95
#define KEY_EXT_HOME    0x96
#define KEY_EXT_END     0x97
#define KEY_EXT_CAPS    0x98
#define KEY_EXT_INS     0x99
#define KEY_EXT_NUM     0x9A

/* ---- UHCI 寄存器偏移 (I/O 空间, 基址 = PCI BAR4; Intel PIIX3 规范) ---- */
#define UHCI_CMD        0x00
#define UHCI_STS        0x02
#define UHCI_INTEN      0x04
#define UHCI_FRNUM      0x06
#define UHCI_FRBASEADD  0x08
#define UHCI_SOF        0x0C
#define UHCI_PORTSC1    0x10
#define UHCI_PORTSC2    0x12

/* UHCI 命令寄存器位 */
#define UHCI_CMD_RUN    0x0001
#define UHCI_CMD_HCRESET 0x0002
#define UHCI_CMD_GRESUME 0x0004
#define UHCI_CMD_CF     0x0080
#define UHCI_CMD_MAXP   0x0100
/* V0.13.64: EHCI DMA 用 4096 对齐全局缓冲 (避免页内偏移/栈地址问题) */
static uint8_t usb64_devbuf_g[64] __attribute__((aligned(4096)));

/* UHCI 状态寄存器位 */
#define UHCI_STS_HCHALTED 0x0020
#define UHCI_STS_USBINT   0x0001
#define UHCI_STS_RD       0x0002
#define UHCI_STS_ERR      0x0004
#define UHCI_STS_HSE      0x0008

/* UHCI 端口状态寄存器位 */
#define UHCI_PORTSC_CCS   0x0001  /* 连接状态变化 */
#define UHCI_PORTSC_CSC   0x0002  /* 连接状态变化标志 */
#define UHCI_PORTSC_PE    0x0004  /* 端口使能 */
#define UHCI_PORTSC_PED   0x0008  /* 使能状态变化 */
#define UHCI_PORTSC_LSDA  0x0100  /* 低速设备 */
#define UHCI_PORTSC_RESET 0x0200  /* 端口复位 */

/* ---- USB PID 类型 (QEMU UHCI token位0-7 存完整PID字节) ---- */
#define USB_PID_SETUP  0x2D
#define USB_PID_IN     0x69
#define USB_PID_OUT    0xE1
#define USB_PID_SOF    0xA5
#define USB_PID_DATA0  0xC3
#define USB_PID_DATA1  0x4B
#define USB_PID_ACK    0xD2
#define USB_PID_NAK    0x5A

/* USB 请求类型 (bmRequestType) */
#define USB_REQ_TYPE_STANDARD 0x00
#define USB_REQ_TYPE_CLASS    0x20
#define USB_REQ_DIR_IN        0x80

/* USB 标准请求 */
#define USB_REQ_GET_DESCRIPTOR 0x06
#define USB_REQ_SET_ADDRESS    0x05
#define USB_REQ_SET_CONFIG     0x09
#define USB_REQ_GET_STATUS     0x00

/* USB 描述符类型 */
#define USB_DT_DEVICE      0x01
#define USB_DT_CONFIG      0x02
#define USB_DT_STRING      0x03
#define USB_DT_INTERFACE   0x04
#define USB_DT_ENDPOINT    0x05
#define USB_DT_HID         0x21

/* USB 类代码 */
#define USB_CLASS_HID 0x03

/* HID 协议 */
#define USB_HID_PROTO_MOUSE  0x02
#define USB_HID_PROTO_KEYB   0x01

/* HID 报告描述符解析状态 */
#define HID_REPORT_MOUSE     1
#define HID_REPORT_KEYBOARD  2

/* ---- 传输描述符 TD (32字节, 4字节对齐) ---- */
typedef struct {
    uint32_t link;        /* 0x00 链接指针 (bit0=0: TD, bit0=1: QH) */
    uint32_t status;      /* 0x04 控制/状态 */
    uint32_t token;       /* 0x08 令牌 */
    uint32_t buffer;      /* 0x0C 数据缓冲物理地址 */
    uint32_t reserved[4]; /* 0x10-0x1C */
} __attribute__((packed)) uhci_td_t;

/* ---- 队列头 QH (16字节, 16字节对齐) ---- */
typedef struct {
    uint32_t link;        /* 0x00 水平链接 */
    uint32_t el_link;     /* 0x04 队列首TD (QEMU读取此字段) */
    uint32_t token;       /* 0x08 队列标识 (QEMU匹配用) */
    uint32_t qh_curr;     /* 0x0C 当前TD (软件维护, QEMU不读) */
} __attribute__((packed)) uhci_qh_t;

/* TD 控制/状态位 (QEMU v6.2.0 hcd-uhci.c uhci-regs.h 定义) */
#define TD_STATUS_ACTIVE  0x00800000   /* bit23 TD_CTRL_ACTIVE */
#define TD_STATUS_NAK     0x00080000   /* bit19 TD_CTRL_NAK */
#define TD_STATUS_STALLED 0x00400000   /* bit22 TD_CTRL_STALL */
#define TD_STATUS_BABBLE  0x00100000   /* bit20 TD_CTRL_BABBLE */
#define TD_STATUS_TIMEOUT 0x00040000   /* bit18 TD_CTRL_TIMEOUT */
#define TD_STATUS_IOC     0x01000000   /* bit24 TD_CTRL_IOC */
#define TD_STATUS_LS      0x20000000   /* bit29 TD_CTRL_SPD (低速设备) */

/* TD 令牌位 (QEMU UHCI 布局) */
#define TD_TOKEN_PID_SHIFT     0        /* bit0-7  完整PID字节 */
#define TD_TOKEN_DEV_SHIFT     8        /* bit8-14 设备地址 */
#define TD_TOKEN_EP_SHIFT      15       /* bit15-18 端点 */
#define TD_TOKEN_MAXLEN_SHIFT  21       /* bit21-31 maxlen-1 (11位) */
#define TD_TOKEN_MAXLEN_MASK   0x7FF

/* 构造TD token (QEMU布局): pid | addr<<8 | ep<<15 | (maxlen-1)<<21 */
static uint32_t usb_make_token(uint8_t pid, uint8_t dev, uint8_t ep, uint16_t maxlen)
{
    return (uint32_t)pid |
           (((uint32_t)dev & 0x7F) << TD_TOKEN_DEV_SHIFT) |
           (((uint32_t)ep & 0x0F) << TD_TOKEN_EP_SHIFT) |
           ((((uint32_t)maxlen - 1) & TD_TOKEN_MAXLEN_MASK) << TD_TOKEN_MAXLEN_SHIFT);
}

extern void vga_puts(const char *s);
extern void vga_put_hex(uint64_t v);
extern uint16_t pci_find_device(uint16_t vendor_id, uint16_t device_id);
extern uint16_t pci_config_read16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset);

static inline uint8_t usb_inb(uint16_t port)
{
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
static inline uint16_t usb_inw(uint16_t port)
{
    uint16_t ret;
    __asm__ volatile ("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
static inline uint32_t usb_inl(uint16_t port)
{
    uint32_t ret;
    __asm__ volatile ("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
static inline void usb_outb(uint16_t port, uint8_t val)
{
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline void usb_outw(uint16_t port, uint16_t val)
{
    __asm__ volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}
static inline void usb_outl(uint16_t port, uint32_t val)
{
    __asm__ volatile ("outl %0, %1" : : "a"(val), "Nd"(port));
}

static inline void *usb_virt_to_phys(void *p)
{
    return p;  /* 内核运行在1:1映射 */
}

/* ---- UHCI 控制器状态 ---- */
static uint16_t uhci_io_base = 0;
static int uhci_initialized = 0;
static int usb64_inited = 0;               /* usb64_init 幂等: 早期启动已枚举时禁止二次复位 */
static uint32_t *uhci_frame_list = 0;      /* 帧列表 (1024 x 32位) */
static uhci_qh_t *uhci_periodic_qh = 0;    /* 周期QH (中断传输挂载点) */
static int usb_low_speed = 0;              /* 连接的设备是否为低速设备 */

/* ---- HID 设备状态 ---- */
/* V0.13.62: 多 HID 支持 — 2.4G 无线接收器是复合设备(1 物理设备 = 多个 HID 接口,
 * 如罗技 Unifying: 键盘+鼠标+多媒体键), 且可同时插多只鼠标/平板;
 * 每个 HID 接口独立 TD+QH 挂周期水平链, 全部可同时工作 */
#define USB_MAX_MICE    4
#define USB_MAX_TABLETS 2
typedef struct {
    int found;
    uint8_t port, addr, ep;
    uhci_td_t *td;
    uhci_qh_t *qh;
    int ehci_slot;          /* V0.13.64: EHCI 周期端点槽 (-1=UHCI) */
    uint8_t report[16];
    uint8_t report_len;
    int x, y, left, right, middle, wheel;
} usb_mouse_t;
static usb_mouse_t g_usb_mice[USB_MAX_MICE];
static int g_usb_mice_count = 0;

/* V0.13.8 P6: USB 绝对定位 HID (触控屏/tablet) — 报告带 Report ID 2:
 * b0=ReportID(2) b1=buttons b2-3=X(16bit LE 0-32767) b4-5=Y */
typedef struct {
    int found;
    uint8_t port, addr, ep;
    uhci_td_t *td;
    uhci_qh_t *qh;
    int ehci_slot;          /* V0.13.64: EHCI 周期端点槽 (-1=UHCI) */
    uint8_t report[16];
    uint8_t report_len;
    int abs_x, abs_y, left, right;
} usb_tablet_t;
static usb_tablet_t g_usb_tablets[USB_MAX_TABLETS];
static int g_usb_tablets_count = 0;
/* V0.13.26: USB 设备注册表 + 热插拔检测 */
#define USB_MAX_DEVS 8
typedef struct {
    uint8_t port;
    uint16_t vid, pid;
    uint8_t iface_class, proto;
    uint8_t addr, ep;
    int found;
    char kind[16];
} usb_dev_t;
static void usb64_dev_register(uint8_t port, uint16_t vid, uint16_t pid,
                               uint8_t cls, uint8_t proto, uint8_t addr,
                               uint8_t ep, const char *kind);
static void usb_storage_probe(void);

/* V0.13.35: USB 海量存储 (Mass Storage / BOT) */
#define USB_CLASS_MASS 0x08
#define USB_BOT_CBW_SIG 0x43425355u
#define USB_BOT_CSW_SIG 0x53425355u
#define USB_BOT_CBW_LEN 31
#define USB_BOT_CSW_LEN 13
#define USB_DIR_IN  0x80
typedef struct {
    int found;
    uint8_t dev_addr;
    uint8_t ep_out;
    uint8_t ep_in;
    uint8_t lun;
    uint16_t maxp;
    uint32_t tag;
} usb_storage_t;
static usb_storage_t g_usb_storage;
static char g_storage_vendor[16];
static char g_storage_model[24];
static uint32_t g_storage_blocks = 0;
static uint32_t g_storage_block_size = 512;
static void usb64_hotplug_check(void);
static usb_dev_t g_usb_devs[USB_MAX_DEVS];
static int g_usb_dev_count = 0;
static int g_usb_hotplug_pending = 0;
static uint16_t g_port_conn_prev[2] = {0, 0};
static int g_usb_hotplug_ticks = 0;
static int usb_kbd_found = 0;

/* ---- USB 键盘状态 (单键盘; 复合接收器的键盘接口优先注册) ---- */
static uint8_t usb_kbd_addr = 0;
static uint8_t usb_kbd_ep = 0;
static uhci_td_t *usb_kbd_td = 0;
static uhci_qh_t *usb_kbd_qh = 0;
static int usb_kbd_ehci_slot = -1;   /* V0.13.64: EHCI 键盘周期槽 */
static uint8_t usb_kbd_report[8] = {0};
static uint8_t usb_kbd_report_len = 8;
static uint8_t usb_kbd_last_keys[6] = {0};  /* 上次报告按下的键 */
static uint8_t usb_kbd_last_mod = 0;

/* USB 键盘事件队列 (环形) */
#define USB_KBD_QUEUE_SIZE 32
static uint8_t usb_kbd_queue[USB_KBD_QUEUE_SIZE];
static int usb_kbd_q_head = 0;
static int usb_kbd_q_tail = 0;
static int usb_kbd_q_count = 0;

/* ---- UHCI 内存池 (静态分配, 物理连续) ---- */
static uint8_t usb_dma_pool[65536] __attribute__((aligned(4096)));  /* V0.13.8 P6: 8K→64K, 多设备枚举不再耗尽 */

/* 简单物理分配器: 从 DMA 池切块 */
static uint32_t usb_pool_offset = 0;
/* V0.13.64: 供 ehci64.c 等外部驱动复用 DMA 池 */
static void *usb_dma_alloc(uint32_t size);
void *usb64_dma_alloc(uint32_t size)
{
    return usb_dma_alloc(size);
}

static void *usb_dma_alloc(uint32_t size)
{
    usb_pool_offset = (usb_pool_offset + 31) & ~31;  /* 32字节对齐 (TD/QH要求) */
    if (usb_pool_offset + size > sizeof(usb_dma_pool)) {
        vga_puts("[USB64] DMA pool exhausted!\n");
        return 0;
    }
    void *p = &usb_dma_pool[usb_pool_offset];
    usb_pool_offset += size;
    for (uint32_t i = 0; i < size; i++) ((uint8_t*)p)[i] = 0;
    return p;
}

/* 由于使用I/O端口读取, 封装 */
static uint16_t uhci_read_sts(void)
{
    return usb_inw(uhci_io_base + UHCI_STS);
}
static uint16_t uhci_read_port(uint16_t port_off)
{
    return usb_inw(uhci_io_base + port_off);
}
static void uhci_write_port(uint16_t port_off, uint16_t val)
{
    usb_outw(uhci_io_base + port_off, val);
}

void usb64_enumerate(void);

/* ---- UHCI 控制器初始化 ---- */
int usb64_init(void)
{
    /* 早期启动(无SATA盘/USB引导)已初始化并枚举过设备时, 桌面阶段不得再次复位控制器,
     * 否则旧帧列表/设备QH 失效, 控制传输超时, HID 平板/鼠标位置停止更新 */
    if (usb64_inited) {
        vga_puts("[USB64] already initialized, skip re-init\n");
        return 0;
    }
    /* 找 UHCI 控制器: PIIX3 0x7020, PIIX4 0x7112 (Intel 8086) */
    uhci_io_base = pci_find_device(0x8086, 0x7020);
    if (!uhci_io_base) uhci_io_base = pci_find_device(0x8086, 0x7112);
    if (!uhci_io_base) {
        vga_puts("[USB64] No UHCI controller found\n");
        /* V0.13.64: 现代主板无 UHCI, 尝试 EHCI (USB 2.0) */
        if (ehci64_init() == 0) {
            /* EHCI 路径: 控制器已启动, 直接枚举 (usb_control_transfer 走 EHCI) */
            vga_puts("[USB64] Using EHCI host controller\n");
            extern void usb64_enumerate(void);
            usb64_enumerate();
            usb64_inited = 1;
            return 0;
        }
        /* V0.13.66: 现代新主板只有 XHCI (USB 3.0), EHCI/UHCI 均不存在 */
        {
            extern int xhci64_init(void);
            if (xhci64_init() == 0) {
                vga_puts("[USB64] Using XHCI host controller\n");
                usb64_inited = 1;
                return 0;
            }
        }
        return -1;
    }
    vga_puts("[USB64] UHCI at I/O ");
    vga_put_hex(uhci_io_base);
    vga_puts("\n");

    /* 1. 全局复位控制器 */
    usb_outw(uhci_io_base + UHCI_CMD, UHCI_CMD_HCRESET);
    for (int i = 0; i < 100000; i++) {
        if ((usb_inw(uhci_io_base + UHCI_CMD) & UHCI_CMD_HCRESET) == 0) break;
    }

    /* 2. 分配帧列表 (4KB对齐, 内核1:1映射物理地址连续) */
    uhci_frame_list = (uint32_t *)usb_dma_alloc(4096);
    if (!uhci_frame_list) return -1;
    for (int i = 0; i < 1024; i++) uhci_frame_list[i] = 1;  /* 终止指针 (bit0=1) */

    /* 3. 分配周期QH (挂中断传输) */
    uhci_periodic_qh = (uhci_qh_t *)usb_dma_alloc(sizeof(uhci_qh_t));
    if (!uhci_periodic_qh) return -1;
    uhci_periodic_qh->link = 1;                    /* 终止 */
    uhci_periodic_qh->el_link = 1;                  /* 队列空闲 */
    uhci_periodic_qh->qh_curr = 1;
    /* 所有1024帧都指向周期QH (每帧都会调度中断传输; QEMU标记: bit1=1 表示QH) */
    {
        uint32_t qh_ptr = (uint32_t)(uintptr_t)usb_virt_to_phys(uhci_periodic_qh) | 2;
        for (int i = 0; i < 1024; i++) uhci_frame_list[i] = qh_ptr;
    }

    /* 4. 设置帧列表基址 (FRBASEADD 是32位寄存器@0x0A, 4KB对齐; 分两次16位写提高兼容性) */
    usb_outw(uhci_io_base + UHCI_FRNUM, 0);
    {
        uint32_t frbase = (uint32_t)(uintptr_t)usb_virt_to_phys(uhci_frame_list);
        usb_outw(uhci_io_base + UHCI_FRBASEADD, (uint16_t)(frbase & 0xFFFF));
        usb_outw(uhci_io_base + UHCI_FRBASEADD + 2, (uint16_t)(frbase >> 16));
    }

    /* 5. 端口复位 (2个根端口) */
    for (int port = 0; port < 2; port++) {
        uint16_t psc = uhci_read_port(UHCI_PORTSC1 + port * 2);
        if (!(psc & UHCI_PORTSC_CCS)) continue;  /* 无设备连接 */
        uhci_write_port(UHCI_PORTSC1 + port * 2, psc | UHCI_PORTSC_RESET);
        for (int i = 0; i < 100000; i++) { /* 等待复位完成 */ }
        psc = uhci_read_port(UHCI_PORTSC1 + port * 2);
        uhci_write_port(UHCI_PORTSC1 + port * 2, psc & ~UHCI_PORTSC_RESET);
        for (int i = 0; i < 10000; i++) { /* 复位恢复时间 */ }
        psc = uhci_read_port(UHCI_PORTSC1 + port * 2);
        vga_puts("[USB64] Port ");
        vga_put_hex(port);
        if (psc & UHCI_PORTSC_CCS) {
            /* 使能端口 (PE=0x0004): QEMU 的 uhci_find_device 只扫描已使能端口 */
            uhci_write_port(UHCI_PORTSC1 + port * 2, UHCI_PORTSC_PE);
            for (int i = 0; i < 10000; i++) { /* 等待使能生效 */ }
            psc = uhci_read_port(UHCI_PORTSC1 + port * 2);
            vga_puts(": device connected (");
            vga_put_hex(psc);
            vga_puts(")");
            if (psc & UHCI_PORTSC_LSDA) {
                vga_puts(" low-speed");
                usb_low_speed = 1;
            }
            vga_puts("\n");
        } else {
            vga_puts(": no device\n");
        }
    }

    /* 6. 启动控制器 */
    usb_outw(uhci_io_base + UHCI_CMD, UHCI_CMD_RUN | UHCI_CMD_CF);
    usb_outw(uhci_io_base + UHCI_INTEN, UHCI_STS_USBINT);
    for (int i = 0; i < 10000; i++) {
        if (!(uhci_read_sts() & UHCI_STS_HCHALTED)) break;
    }
    vga_puts("[USB64] Controller running\n");
    /* 验证: 读回关键寄存器 */
    vga_puts("[USB64] sts=");
    vga_put_hex(uhci_read_sts());
    vga_puts(" cmd=");
    vga_put_hex(usb_inw(uhci_io_base + UHCI_CMD));
    vga_puts(" frbase=");
    vga_put_hex(usb_inl(uhci_io_base + UHCI_FRBASEADD));
    vga_puts(" fr0=");
    vga_put_hex(uhci_frame_list[0]);
    vga_puts(" pool=");
    vga_put_hex((uint32_t)(uintptr_t)usb_dma_pool);
    vga_puts("\n");
    uhci_initialized = 1;

    /* 7. 枚举设备 */
    usb64_enumerate();

    /* 8. 重新填充帧列表: 全部指向周期主QH — V0.13.62 起所有 HID 设备
     *    (鼠标/键盘/平板) 的 QH 都水平串在主 QH 水平链上, 每帧调度全部中断端点;
     *    UHCI 硬件对 NAK 的中断端点自动跳过, 无性能问题 */
    {
        uint32_t main_qh_ptr = (uint32_t)(uintptr_t)usb_virt_to_phys(uhci_periodic_qh) | 2;
        for (int i = 0; i < 1024; i++) uhci_frame_list[i] = main_qh_ptr;
    }
    usb64_inited = 1;
    return 0;
}

/* ---- USB 控制传输 (默认地址0 或 指定地址) ----
 * 通过周期QH挂一次性控制传输 (简化: 同一TD链每次重建)
 * 注意: 这里使用"直接插入帧列表"的方式 (非异步队列), 适合引导枚举
 */
static uint8_t usb_max_packet0 = 8;  /* 控制端点0最大包长 (枚举初期默认8) */

static int usb_bot_cmd(uint8_t lun, const uint8_t *cb, uint8_t cblen,
                       uint8_t *data, uint16_t dlen, uint8_t dir_in);
static int usb_bot_reset(void);

/* V0.13.36: 批量传输。数据缓冲若不在DMA池内(栈/全局), 先拷贝到scratch保证物理可写
 * V0.13.42: 修复USB写路径数据错位: 共享scratch被后续命令覆盖导致TD读错数据,
 *           改用8深轮转独立缓冲, 连续命令互不覆盖 */
static uint8_t usb_bulk_scratch_pool[8][512] __attribute__((aligned(32)));
static int usb_scratch_rot = 0;
static int usb_bulk_transfer(uint8_t dev_addr, uint8_t ep, uint8_t *data,
                             uint16_t len, uint8_t dir_in)
{
    uint16_t maxp = 2047;  /* V0.13.36: 单TD大传输(UHCI maxlen上限) */
    int scratch_used = 0;
    uint8_t *orig_data = data;
    if (data && len > 0) {
        uint8_t *pool_s = usb_dma_pool, *pool_e = usb_dma_pool + sizeof(usb_dma_pool);
        if (data < pool_s || data >= pool_e) {
            if (len > 512) return -1;
            uint8_t *s = usb_bulk_scratch_pool[(usb_scratch_rot++) & 7];
            if (!dir_in) { for (int i = 0; i < len; i++) s[i] = data[i]; }
            data = s;
            scratch_used = 1;
        }
    }
    uhci_td_t *first = 0, *prev = 0;
    uint16_t remaining = len, off = 0;
    while (remaining > 0) {
        uint16_t chunk = (remaining > maxp) ? maxp : remaining;
        uhci_td_t *td = (uhci_td_t *)usb_dma_alloc(sizeof(uhci_td_t));
        if (!td) return -1;
        td->token = usb_make_token(dir_in ? USB_PID_IN : USB_PID_OUT,
                                   dev_addr, ep, chunk);
        td->buffer = (uint32_t)(uintptr_t)usb_virt_to_phys(data + off);
        td->status = TD_STATUS_ACTIVE;
        td->link = 1;
        if (prev) prev->link = (uint32_t)(uintptr_t)usb_virt_to_phys(td);
        else first = td;
        prev = td;
        remaining -= chunk;
        off += chunk;
    }
    if (!first) return -1;
    uint32_t td_ptr = (uint32_t)(uintptr_t)usb_virt_to_phys(first);
    /* V0.13.40: 帧同步 - 等待UHCI帧推进, 避免TD挂载与帧循环扫描竞态(偶发TD未执行)
     * V0.13.41: 等2次变化, WRITE后给设备更多帧时间 */
    {
        uint16_t f0 = usb_inw(uhci_io_base + UHCI_FRNUM);
        int nchg = 0;
        for (int fs = 0; fs < 40000 && nchg < 2; fs++) {
            uint16_t f1 = usb_inw(uhci_io_base + UHCI_FRNUM);
            if (f1 != f0) { f0 = f1; nchg++; }
        }
    }
    for (int f = 0; f < 1024; f++) uhci_frame_list[f] = td_ptr;
    /* 轮询最后一个TD: 链式顺序执行, 末TD完成即整链完成 */
    for (int i = 0; i < 4000000; i++) {
        if (!(prev->status & TD_STATUS_ACTIVE)) break;
        if ((i & 0x3FFF) == 0) (void)usb_inw(uhci_io_base + UHCI_FRNUM);
    }
    {
        /* V0.13.40: 帧同步后再恢复QH, 确保QEMU已扫过TD链 */
        /* V0.13.41: 等2次变化 */
        uint16_t f0 = usb_inw(uhci_io_base + UHCI_FRNUM);
        int nchg2 = 0;
        for (int fs = 0; fs < 40000 && nchg2 < 2; fs++) {
            uint16_t f1 = usb_inw(uhci_io_base + UHCI_FRNUM);
            if (f1 != f0) { f0 = f1; nchg2++; }
        }
        uint32_t qh_ptr = (uint32_t)(uintptr_t)usb_virt_to_phys(uhci_periodic_qh) | 2;
        for (int f = 0; f < 1024; f++) uhci_frame_list[f] = qh_ptr;
    }
    if (prev->status & TD_STATUS_ACTIVE) return -1;
    /* V0.13.36: 检查TD错误位(STALL/BABBLE/TIMEOUT/IO错误): 有错则不拷回, 防止陈旧数据 */
    if (prev->status & (TD_STATUS_STALLED | TD_STATUS_BABBLE | TD_STATUS_TIMEOUT | 0x00020000)) {
        vga_puts("[USB64] bulk TD err=");
        vga_put_hex(prev->status);
        vga_puts("\n");
        return -1;
    }
    if (scratch_used && dir_in && orig_data) {
        uint8_t *s = usb_bulk_scratch_pool[(usb_scratch_rot - 1) & 7];
        for (int i = 0; i < len; i++) orig_data[i] = s[i];
    }
    return 0;
}

int usb_storage_read_sectors(uint32_t lba, uint8_t count, uint8_t *buf);
int usb_storage_write_sectors(uint32_t lba, uint8_t count, const uint8_t *buf);

/* V0.13.37: 挂载USB海量存储到ext2 (干净挂载: 重建shared ctx, 无写测试副作用) */
int usb64_mount_usb(void)
{
    extern void storage_hal_set_device(int sel);
    extern int ext2_64_mount(int (*rd)(uint32_t, uint8_t, uint8_t *), uint32_t off);
    extern int ext2_64_mkdir(const char *path);
    extern int ext2_64_rename(const char *oldp, const char *newp);
    if (!g_usb_storage.found) return -1;
    storage_hal_set_device(1);
    int rc = ext2_64_mount(usb_storage_read_sectors, 0);
    if (rc == 0) {
        /* V0.13.41: 挂载后写路径自检: mkdir + rename 全链路 (USB写可靠性验证) */
        int m = ext2_64_mkdir("/ndir");
        int r = ext2_64_rename("/ndir", "/ndir2");
        vga_puts("[USB64] write self-test mkdir=");
        vga_put_hex((uint64_t)m);
        vga_puts(" rename=");
        vga_put_hex((uint64_t)r);
        vga_puts("\n");
    }
    return rc;
}

/* V0.13.40: 检测USB存储是否在位 (供桌面自动挂载判断) */
int usb64_storage_present(void)
{
    return g_usb_storage.found;
}

/* V0.13.37: 卸载USB并恢复IDE系统盘 (系统盘分区偏移13000，V0.54.6 布局) */
int usb64_umount_usb(void)
{
    extern void storage_hal_set_device(int sel);
    extern int ext2_64_mount(int (*rd)(uint32_t, uint8_t, uint8_t *), uint32_t off);
    extern int ide64_read_sectors(uint32_t lba, uint8_t count, uint8_t *buf);
    storage_hal_set_device(0);
    return ext2_64_mount(ide64_read_sectors, 13000);
}

/* V0.13.36: 扇区读写 (SCSI READ10/WRITE10, 512B/扇区) */
int usb_storage_read_sectors(uint32_t lba, uint8_t count, uint8_t *buf)
{
    /* V0.13.36: 按单扇区循环(规避QEMU大TD跨帧异常) */
    /* V0.13.40: WRITE10后CSW偶发超时, 命令级重试 */
    for (int i = 0; i < count; i++) {
        int tried = 0, rc = -1;
        while (tried++ < 4) {
            uint8_t cb[16] = {0x28, 0, 0, 0, 0, 0, 0, 0, 0, 0};
            uint32_t sec = lba + (uint32_t)i;
            cb[2] = (sec >> 24) & 0xFF; cb[3] = (sec >> 16) & 0xFF;
            cb[4] = (sec >> 8) & 0xFF;  cb[5] = sec & 0xFF;
            cb[7] = 1;
            rc = usb_bot_cmd(0, cb, 10, buf + i * 512, 512, 1);
            if (rc == 0) break;
        }
        if (rc != 0) return -1;
    }
    return 0;
}

int usb_storage_write_sectors(uint32_t lba, uint8_t count, const uint8_t *buf)
{
    /* V0.13.40: WRITE10 CSW偶发超时, 命令级重试 */
    for (int i = 0; i < count; i++) {
        int tried = 0, rc = -1;
        uint32_t sec = lba + (uint32_t)i;
        while (tried++ < 4) {
            uint8_t cb[16] = {0x2A, 0, 0, 0, 0, 0, 0, 0, 0, 0};
            cb[2] = (sec >> 24) & 0xFF; cb[3] = (sec >> 16) & 0xFF;
            cb[4] = (sec >> 8) & 0xFF;  cb[5] = sec & 0xFF;
            cb[7] = 1;
            rc = usb_bot_cmd(0, cb, 10, (uint8_t *)buf + i * 512, 512, 0);
            if (rc == 0) break;
        }
        if (rc != 0) {
            vga_puts("[USB64] WRITE fail lba="); vga_put_hex(sec); vga_puts(" rc="); vga_put_hex((uint64_t)rc); vga_puts("\n");
            return -1;
        }
    }
    return 0;
}

/* V0.13.41: BOT 恢复: Mass Storage Reset (标准复位CBW发往bulk-out),
 * 设备级状态卡死(CSW超时/数据阶段残留)时的最终兜底 */
static int usb_bot_reset(void)
{
    uint8_t reset[31];
    memset(reset, 0, sizeof(reset));
    reset[0] = 0xFF; reset[1] = 0xFF; reset[2] = 0xFF; reset[3] = 0xFF;
    reset[4] = 0xAA; reset[5] = 0xAA; reset[6] = 0xAA; reset[7] = 0xAA;
    reset[14] = 1;  /* bCBWCBLength=1 */
    reset[15] = 0xFF;  /* Mass Storage Reset 命令 */
    if (usb_bulk_transfer(g_usb_storage.dev_addr, g_usb_storage.ep_out,
                          reset, 31, 0) != 0) return -1;
    /* 帧同步等设备清空FIFO */
    uint16_t f0 = usb_inw(uhci_io_base + UHCI_FRNUM);
    for (int fs = 0; fs < 2000; fs++) {
        uint16_t f1 = usb_inw(uhci_io_base + UHCI_FRNUM);
        if (f1 != f0) break;
    }
    return 0;
}

/* V0.13.35: BOT 命令: CBW + 数据 + CSW */
static int usb_bot_cmd(uint8_t lun, const uint8_t *cb, uint8_t cblen,
                       uint8_t *data, uint16_t dlen, uint8_t dir_in)
{
    if (!g_usb_storage.found) return -1;
    uint8_t *cbw = (uint8_t *)usb_dma_alloc(USB_BOT_CBW_LEN);
    uint8_t *csw = (uint8_t *)usb_dma_alloc(USB_BOT_CSW_LEN);
    if (!cbw || !csw) return -1;
    uint32_t tag = ++g_usb_storage.tag;
    /* CBW: sig|tag|tlen|flags|lun|cblen|cb[16] */
    cbw[0] = USB_BOT_CBW_SIG & 0xFF; cbw[1] = (USB_BOT_CBW_SIG >> 8) & 0xFF;
    cbw[2] = (USB_BOT_CBW_SIG >> 16) & 0xFF; cbw[3] = (USB_BOT_CBW_SIG >> 24) & 0xFF;
    cbw[4] = tag & 0xFF; cbw[5] = (tag >> 8) & 0xFF;
    cbw[6] = (tag >> 16) & 0xFF; cbw[7] = (tag >> 24) & 0xFF;
    cbw[8] = dlen & 0xFF; cbw[9] = (dlen >> 8) & 0xFF;
    cbw[10] = (dlen >> 16) & 0xFF; cbw[11] = (dlen >> 24) & 0xFF;
    cbw[12] = dir_in ? USB_DIR_IN : 0;
    cbw[13] = lun;
    cbw[14] = cblen;
    for (int i = 0; i < 16; i++) cbw[15 + i] = (i < cblen) ? cb[i] : 0;

    if (usb_bulk_transfer(g_usb_storage.dev_addr, g_usb_storage.ep_out,
                          cbw, USB_BOT_CBW_LEN, 0) != 0) return -1;
    if (dlen > 0 && data) {
        if (usb_bulk_transfer(g_usb_storage.dev_addr,
                              dir_in ? g_usb_storage.ep_in : g_usb_storage.ep_out,
                              data, dlen, dir_in) != 0) {
            vga_puts("[USB64] data stage failed\n");
            return -1;
        }
    }
    if (usb_bulk_transfer(g_usb_storage.dev_addr, g_usb_storage.ep_in,
                          csw, USB_BOT_CSW_LEN, 1) != 0) {
        /* V0.13.40: 设备可能已发CSW但TD未完成(WRITE后常见): 重读CSW清除设备状态, 避免卡死 */
        memset(csw, 0, USB_BOT_CSW_LEN);
        if (usb_bulk_transfer(g_usb_storage.dev_addr, g_usb_storage.ep_in,
                              csw, USB_BOT_CSW_LEN, 1) != 0) {
            /* V0.13.41: 重读仍失败 -> 执行 Mass Storage Reset 恢复设备 */
            usb_bot_reset();
            vga_puts("[USB64] CSW failed, BOT reset done\n");
            return -1;
        }
    }
    /* V0.13.42: 命令间帧间隔 - 确保设备完成上一条命令(清空流水线), 消除连续命令数据错位 */
    {
        uint16_t f0 = usb_inw(uhci_io_base + UHCI_FRNUM);
        int chg = 0;
        for (int fs = 0; fs < 50000 && chg < 10; fs++) {
            uint16_t f1 = usb_inw(uhci_io_base + UHCI_FRNUM);
            if (f1 != f0) { f0 = f1; chg++; }
        }
    }
    /* 校验 CSW */
    if (csw[0] != (USB_BOT_CSW_SIG & 0xFF) || csw[1] != ((USB_BOT_CSW_SIG >> 8) & 0xFF) ||
        csw[2] != ((USB_BOT_CSW_SIG >> 16) & 0xFF) || csw[3] != ((USB_BOT_CSW_SIG >> 24) & 0xFF)) {
        /* V0.13.41: CSW签名错 = 设备状态错乱, 复位兜底 */
        usb_bot_reset();
        vga_puts("[USB64] CSW sig bad, BOT reset done\n");
        return -2;
    }
    if (csw[12] != 0) return -3;   /* bCSWStatus 非0 = 命令失败 */
    /* V0.13.36: 校验CSW tag 与命令匹配, 防滞后CSW */
    uint32_t csw_tag = ((uint32_t)csw[4]) | ((uint32_t)csw[5] << 8) |
                       ((uint32_t)csw[6] << 16) | ((uint32_t)csw[7] << 24);
    if (csw_tag != tag) {
        /* V0.13.41: tag不匹配 = 残留CSW, 复位兜底 */
        usb_bot_reset();
        vga_puts("[USB64] CSW tag mismatch, BOT reset done\n");
        return -4;
    }
    return 0;
}

/* V0.13.35: 存储设备探测: INQUIRY + READ CAPACITY */
static void usb_storage_probe(void)
{
    vga_puts("[USB64] Mass storage probe\n");
    /* INQUIRY (36字节响应: 8厂商 + 16产品) */
    uint8_t *inq = (uint8_t *)usb_dma_alloc(36);
    uint8_t cb1[16] = {0x12, 0, 0, 0, 36, 0};
    if (usb_bot_cmd(0, cb1, 6, inq, 36, 1) != 0) {
        vga_puts("[USB64] INQUIRY failed\n");
        return;
    }
    int i;
    for (i = 0; i < 8 && inq[8 + i] > 32 && inq[8 + i] < 127; i++)
        g_storage_vendor[i] = (char)inq[8 + i];
    g_storage_vendor[i] = 0;
    for (i = 0; i < 16 && inq[16 + i] > 32 && inq[16 + i] < 127; i++)
        g_storage_model[i] = (char)inq[16 + i];
    g_storage_model[i] = 0;
    vga_puts("[USB64] INQUIRY: ");
    vga_puts(g_storage_vendor);
    vga_puts(" / ");
    vga_puts(g_storage_model);
    vga_puts("\n");

    /* READ CAPACITY (8字节响应: LBA+1 块数, 块大小) */
    uint8_t *rcap = (uint8_t *)usb_dma_alloc(8);
    uint8_t cb2[16] = {0x25, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    if (usb_bot_cmd(0, cb2, 10, rcap, 8, 1) != 0) {
        vga_puts("[USB64] READ CAPACITY failed\n");
        return;
    }
    g_storage_blocks = ((uint32_t)rcap[0] << 24) | ((uint32_t)rcap[1] << 16) |
                       ((uint32_t)rcap[2] << 8) | rcap[3];
    g_storage_block_size = ((uint32_t)rcap[4] << 24) | ((uint32_t)rcap[5] << 16) |
                           ((uint32_t)rcap[6] << 8) | rcap[7];
    g_storage_blocks++;   /* 最后一块LBA → 块数 */
    vga_puts("[USB64] Capacity: ");
    /* 十进制容量 (MB) */
    {
        uint64_t bytes = (uint64_t)g_storage_blocks * g_storage_block_size;
        uint64_t mb = bytes / (1024 * 1024);
        char nb[16]; int p2 = 0;
        if (mb == 0) nb[p2++] = '0';
        while (mb && p2 < 14) { nb[p2++] = '0' + mb % 10; mb /= 10; }
        for (int k = 0; k < p2 / 2; k++) { char c = nb[k]; nb[k] = nb[p2-1-k]; nb[p2-1-k] = c; }
        nb[p2] = 0;
        vga_puts(nb);
        vga_puts(" MB (");
        {
            char nb2[16]; int p3 = 0;
            uint64_t bl = g_storage_blocks;
            if (bl == 0) nb2[p3++] = '0';
            while (bl && p3 < 14) { nb2[p3++] = '0' + bl % 10; bl /= 10; }
            for (int k = 0; k < p3 / 2; k++) { char c = nb2[k]; nb2[k] = nb2[p3-1-k]; nb2[p3-1-k] = c; }
            nb2[p3] = 0;
            vga_puts(nb2);
        }
        vga_puts(" blocks)\n");
    }

}

static int usb_control_transfer(uint8_t dev_addr, uint8_t bmreq, uint8_t breq,
                                uint16_t wval, uint16_t windex, uint16_t wlen,
                                uint8_t *data, uint8_t dir_in)
{
    /* V0.13.64: EHCI 控制器路径 (现代主板无 UHCI) */
    {
        extern int ehci64_present(void);
        extern int ehci64_control_transfer(uint8_t dev_addr, const uint8_t *setup,
                                           uint8_t *data, uint16_t len, int dir);
        if (ehci64_present()) {
            uint8_t *setup = (uint8_t *)usb_dma_alloc(8);
            if (!setup) return -1;
            setup[0] = bmreq; setup[1] = breq;
            setup[2] = wval & 0xFF; setup[3] = wval >> 8;
            setup[4] = windex & 0xFF; setup[5] = windex >> 8;
            setup[6] = wlen & 0xFF; setup[7] = wlen >> 8;
            return ehci64_control_transfer(dev_addr, setup, data, wlen, dir_in);
        }
    }

    /* 分配TD链: SETUP(8字节) + DATA(可选) + STATUS */
    uhci_td_t *td_setup = (uhci_td_t *)usb_dma_alloc(sizeof(uhci_td_t));
    uhci_td_t *td_status = (uhci_td_t *)usb_dma_alloc(sizeof(uhci_td_t));
    uhci_td_t *td_data = 0;
    if (!td_setup || !td_status) return -1;

    /* 构造 setup 包 (8字节: bmreq,breq,wval,windex,wlen) */
    uint8_t *setup = (uint8_t *)usb_dma_alloc(8);
    setup[0] = bmreq;
    setup[1] = breq;
    setup[2] = wval & 0xFF;
    setup[3] = wval >> 8;
    setup[4] = windex & 0xFF;
    setup[5] = windex >> 8;
    setup[6] = wlen & 0xFF;
    setup[7] = wlen >> 8;


    /* SETUP TD (PID=SETUP 0x2D, 包长8, 方向OUT) */
    td_setup->token = usb_make_token(USB_PID_SETUP, dev_addr, 0, 8);
    td_setup->buffer = (uint32_t)(uintptr_t)usb_virt_to_phys(setup);
    td_setup->status = TD_STATUS_ACTIVE | (usb_low_speed ? TD_STATUS_LS : 0);
    td_setup->link = 1;  /* 暂时终止, 后面链 */

    /* DATA 阶段 (IN或OUT): 按 maxpacket 拆成多个TD, UHCI每TD只传一个USB包
     * 注意: wlen 为总长度, 每个TD的包长 = min(maxpacket, 剩余) */
    if (wlen > 0 && data) {
        uint8_t maxp = usb_max_packet0;
        uint16_t remaining = wlen;
        uint16_t off = 0;
        uhci_td_t *prev_td = td_setup;
        while (remaining > 0) {
            uint16_t chunk = (remaining > maxp) ? maxp : remaining;
            uhci_td_t *td = (uhci_td_t *)usb_dma_alloc(sizeof(uhci_td_t));
            if (!td) return -1;
            td->token = usb_make_token(dir_in ? USB_PID_IN : USB_PID_OUT,
                                        dev_addr, 0, chunk);
            td->buffer = (uint32_t)(uintptr_t)usb_virt_to_phys(data + off);
            td->status = TD_STATUS_ACTIVE | (usb_low_speed ? TD_STATUS_LS : 0);
            td->link = 1;
            prev_td->link = (uint32_t)(uintptr_t)usb_virt_to_phys(td);
            prev_td = td;
            td_data = td;   /* 记录最后一个data TD */
            remaining -= chunk;
            off += chunk;
        }
    }

    /* STATUS TD (方向与DATA相反: IN传输出OUT, OUT传输入IN) */
    /* STATUS TD 方向: 与DATA相反; 无DATA阶段时STATUS是IN (USB规范:
     * SETUP总是OUT, 零数据控制传输如SET_ADDRESS/SET_CONFIG的STATUS为IN) */
    uint32_t stat_pid;
    if (wlen > 0 && data) {
        stat_pid = dir_in ? USB_PID_OUT : USB_PID_IN;
    } else {
        stat_pid = USB_PID_IN;
    }
    td_status->token = usb_make_token(stat_pid, dev_addr, 0, 0);
    td_status->status = TD_STATUS_ACTIVE | (usb_low_speed ? TD_STATUS_LS : 0);
    td_status->buffer = 0;
    td_status->link = 1;
    if (td_data) td_data->link = (uint32_t)(uintptr_t)usb_virt_to_phys(td_status);
    else td_setup->link = (uint32_t)(uintptr_t)usb_virt_to_phys(td_status);

    /* 挂到全部1024帧 (覆盖当前frnum任意位置; TD完成(ACTIVE清0)后QEMU自动停止) */
    {
        uint32_t td_ptr = (uint32_t)(uintptr_t)usb_virt_to_phys(td_setup);
        for (int f = 0; f < 1024; f++) uhci_frame_list[f] = td_ptr;
    }

    /* 等待完成: 轮询所有TD状态直到非ACTIVE。
     * 注意: 纯内存轮询会饿死QEMU事件循环(帧定时器不触发), 必须周期性做I/O读让出 */
    for (int i = 0; i < 4000000; i++) {
        if (!(td_setup->status & TD_STATUS_ACTIVE)) {
            /* 链上所有TD应已完成; 检查NAK/错误 */
            if (td_status->status & TD_STATUS_ACTIVE) continue;
            break;
        }
        if ((i & 0x3FFF) == 0) {
            (void)usb_inw(uhci_io_base + UHCI_FRNUM);  /* I/O读让出QEMU事件循环 */
        }
    }
    /* 恢复帧列表 */
    {
        uint32_t qh_ptr = (uint32_t)(uintptr_t)usb_virt_to_phys(uhci_periodic_qh) | 2;
        for (int f = 0; f < 1024; f++) uhci_frame_list[f] = qh_ptr;
    }

    /* 检查状态 */

    if (td_status->status & TD_STATUS_ACTIVE) {
        vga_puts("[USB64] CT timeout (req=");
        vga_put_hex(breq);
        vga_puts(")\n");
        return -1;
    }

    return 0;
}

/* ---- USB 标准控制请求封装 ---- */
static int usb_get_descriptor(uint8_t dev, uint8_t type, uint8_t index, uint16_t len, uint8_t *buf)
{
    return usb_control_transfer(dev, USB_REQ_TYPE_STANDARD | USB_REQ_DIR_IN,
                                USB_REQ_GET_DESCRIPTOR, (uint16_t)((type << 8) | index),
                                0, len, buf, 1);
}
static int usb_set_address(uint8_t dev, uint8_t addr)
{
    return usb_control_transfer(dev, USB_REQ_TYPE_STANDARD, USB_REQ_SET_ADDRESS,
                                addr, 0, 0, 0, 0);
}
static int usb_set_config(uint8_t dev, uint8_t cfg)
{
    return usb_control_transfer(dev, USB_REQ_TYPE_STANDARD, USB_REQ_SET_CONFIG,
                                cfg, 0, 0, 0, 0);
}

/* ---- 中断传输TD (轮询读HID报告) ---- */
static uhci_td_t *usb_make_interrupt_td(uint8_t dev, uint8_t ep, uint8_t *buf, uint16_t maxlen)
{
    uhci_td_t *td = (uhci_td_t *)usb_dma_alloc(sizeof(uhci_td_t));
    if (!td) return 0;
    td->token = usb_make_token(USB_PID_IN, dev, ep, maxlen);
    td->buffer = (uint32_t)(uintptr_t)usb_virt_to_phys(buf);
    td->status = TD_STATUS_ACTIVE;
    td->link = 1;
    return td;
}

/* ---- USB 设备枚举 (单个端口, 返回接口协议: 2=鼠标 1=键盘 0=失败) ---- */
/* ============ V0.13.62: 多 HID 设备/复合接口支持 ============ */

/* 水平链: 把新设备 QH 串到周期主 QH 链尾 (每帧轮询所有中断端点) */
static void usb_chain_add_qh(uhci_qh_t *qh)
{
    uhci_qh_t *p = uhci_periodic_qh;
    while (!(p->link & 1)) {
        p = (uhci_qh_t *)(uintptr_t)(p->link & ~0xF);
    }
    qh->link = 1;
    p->link = (uint32_t)(uintptr_t)usb_virt_to_phys(qh) | 2;
}

/* 注册一个 HID 鼠标接口 (普通 USB 鼠标 / 2.4G 无线接收器鼠标接口) */
static void usb_hid_register_mouse(uint8_t port, uint8_t dev_addr, uint8_t ep_in,
                                   uint8_t ep_maxlen, uint16_t vid, uint16_t pid)
{
    if (g_usb_mice_count >= USB_MAX_MICE) {
        vga_puts("[USB64] USB mouse slots full\n");
        return;
    }
    usb_mouse_t *m = &g_usb_mice[g_usb_mice_count];
    m->port = port; m->addr = dev_addr; m->ep = ep_in;
    m->report_len = (ep_maxlen > 16) ? 16 : ep_maxlen;
    if (m->report_len < 4) m->report_len = 4;
    m->ehci_slot = -1;
    /* V0.13.64: EHCI 周期端点 (现代主板) */
    {
        extern int ehci64_present(void);
        extern int ehci64_int_ep_register(uint8_t dev_addr, uint8_t ep,
                                          uint16_t maxlen, uint8_t *buf);
        if (ehci64_present()) {
            int slot = ehci64_int_ep_register(dev_addr, ep_in, m->report_len, m->report);
            if (slot < 0) return;
            m->ehci_slot = slot;
            m->found = 1;
            goto mouse_done;
        }
    }
    m->td = usb_make_interrupt_td(dev_addr, ep_in, m->report, m->report_len);
    if (!m->td) return;
    m->qh = (uhci_qh_t *)usb_dma_alloc(sizeof(uhci_qh_t));
    if (!m->qh) return;
    m->qh->link = 1;
    m->qh->el_link = (uint32_t)(uintptr_t)usb_virt_to_phys(m->td);
    m->qh->qh_curr = (uint32_t)(uintptr_t)usb_virt_to_phys(m->td);
    usb_chain_add_qh(m->qh);
    m->found = 1;
mouse_done:
    g_usb_mice_count++;
    usb64_dev_register(port, vid, pid, USB_CLASS_HID, USB_HID_PROTO_MOUSE,
                       dev_addr, ep_in, "mouse");
    vga_puts("[USB64] USB mouse ready (iface ep=");
    vga_put_hex(ep_in);
    vga_puts(" len=");
    vga_put_hex(m->report_len);
    vga_puts(")\n");
}

/* 注册一个绝对定位 HID (触控屏/数位板/proto=0 无线平板) */
static void usb_hid_register_tablet(uint8_t port, uint8_t dev_addr, uint8_t ep_in,
                                    uint8_t ep_maxlen, uint16_t vid, uint16_t pid)
{
    if (g_usb_tablets_count >= USB_MAX_TABLETS) {
        vga_puts("[USB64] USB tablet slots full\n");
        return;
    }
    usb_tablet_t *t = &g_usb_tablets[g_usb_tablets_count];
    t->port = port; t->addr = dev_addr; t->ep = ep_in;
    t->report_len = (ep_maxlen > 16) ? 16 : ep_maxlen;
    if (t->report_len < 6) t->report_len = 6;
    t->ehci_slot = -1;
    /* V0.13.64: EHCI 周期端点 */
    {
        extern int ehci64_present(void);
        extern int ehci64_int_ep_register(uint8_t dev_addr, uint8_t ep,
                                          uint16_t maxlen, uint8_t *buf);
        if (ehci64_present()) {
            int slot = ehci64_int_ep_register(dev_addr, ep_in, t->report_len, t->report);
            if (slot < 0) return;
            t->ehci_slot = slot;
            t->found = 1;
            goto tablet_done;
        }
    }
    t->td = usb_make_interrupt_td(dev_addr, ep_in, t->report, t->report_len);
    if (!t->td) return;
    t->qh = (uhci_qh_t *)usb_dma_alloc(sizeof(uhci_qh_t));
    if (!t->qh) return;
    t->qh->link = 1;
    t->qh->el_link = (uint32_t)(uintptr_t)usb_virt_to_phys(t->td);
    t->qh->qh_curr = (uint32_t)(uintptr_t)usb_virt_to_phys(t->td);
    usb_chain_add_qh(t->qh);
    t->found = 1;
tablet_done:
    g_usb_tablets_count++;
    usb64_dev_register(port, vid, pid, USB_CLASS_HID, 0,
                       dev_addr, ep_in, "tablet");
    vga_puts("[USB64] USB tablet/abs-HID ready (absolute pointer)\n");
}

static uint8_t usb64_enumerate_one(uint8_t dev_addr, uint8_t port)
{
    uint8_t *devbuf = usb64_devbuf_g;

    /* 第一步: 只读8字节设备描述符 (低速设备控制端点maxpacket=8, 单包) */
    if (usb_get_descriptor(0, USB_DT_DEVICE, 0, 8, devbuf) != 0) {
        /* 可能低速设备第一包失败, 重试 */
        if (usb_get_descriptor(0, USB_DT_DEVICE, 0, 8, devbuf) != 0) {
            vga_puts("[USB64] Get device descriptor (8B) failed\n");
            return 0;
        }
    }
    /* devbuf[7] = bMaxPacketSize0, 更新拆包长度 */
    if (devbuf[7] != 0) usb_max_packet0 = devbuf[7];

    /* 第二步: 读完整设备描述符 (18字节, 按maxpacket自动拆包) */
    if (usb_get_descriptor(0, USB_DT_DEVICE, 0, 18, devbuf) != 0) {
        vga_puts("[USB64] Get full device descriptor failed\n");
        return 0;
    }
    uint8_t num_cfg = devbuf[17];
    vga_puts("[USB64] Device port=");
    vga_put_hex(port);
    vga_puts(" VID=");
    vga_put_hex(((uint16_t)devbuf[9] << 8) | devbuf[8]);
    vga_puts(" PID=");
    vga_put_hex(((uint16_t)devbuf[11] << 8) | devbuf[10]);
    vga_puts(" cfgs=");
    vga_put_hex(num_cfg);
    vga_puts("\n");

    /* 分配地址 */
    if (usb_set_address(0, dev_addr) != 0) {
        vga_puts("[USB64] Set address failed\n");
        return 0;
    }
    for (int i = 0; i < 1000; i++) { /* 地址切换稳定时间 */ }

    /* 读配置描述符 (直接读完整配置, 绕过9字节先读) */
    for (int i = 0; i < 64; i++) devbuf[i] = 0xFF;
    if (usb_get_descriptor(dev_addr, USB_DT_CONFIG, 0, 64, devbuf) != 0) {
        vga_puts("[USB64] Get config descriptor failed\n");
        return 0;
    }
    uint16_t total_len = (uint16_t)devbuf[2] | ((uint16_t)devbuf[3] << 8);

    /* 解析配置描述符: 收集所有 HID 接口 + 存储端点 (V0.13.62:
     * 2.4G 无线接收器是复合设备, 一个物理设备含多个 HID 接口, 全部注册) */
    int offset = 0;
    uint8_t iface_class = 0, iface_proto = 0;
    uint8_t ep_in = 0, ep_maxlen = 0;
    uint8_t bulk_out = 0, bulk_in = 0;
    uint8_t hid_ifaces[6][3];   /* 每 HID 接口: [0]=proto [1]=ep [2]=maxlen */
    int hid_count = 0;
    int cur_hid_slot = -1;      /* 当前正扫描的 HID 接口槽 */
    while (offset < total_len) {
        uint8_t dlen = devbuf[offset];
        uint8_t dtype = devbuf[offset + 1];
        if (dlen == 0) break;
        if (dtype == USB_DT_INTERFACE && dlen >= 9) {
            iface_class = devbuf[offset + 5];
            iface_proto = devbuf[offset + 7];
            ep_in = 0; ep_maxlen = 0;
            bulk_out = 0; bulk_in = 0;
            if (iface_class == USB_CLASS_HID && hid_count < 6) {
                cur_hid_slot = hid_count;
                hid_ifaces[hid_count][0] = iface_proto;
                hid_ifaces[hid_count][1] = 0;
                hid_ifaces[hid_count][2] = 0;
            } else {
                cur_hid_slot = -1;
            }
        } else if (dtype == USB_DT_ENDPOINT && dlen >= 7) {
            uint8_t ep_addr = devbuf[offset + 2];
            uint8_t ep_attrs = devbuf[offset + 3];
            uint16_t maxp = (uint16_t)devbuf[offset + 4] | ((uint16_t)devbuf[offset + 5] << 8);
            if ((ep_addr & 0x80) && (ep_attrs & 0x03) == 0x03) {  /* 中断IN端点 */
                ep_in = ep_addr & 0x0F;
                ep_maxlen = (maxp > 8) ? 8 : (uint8_t)maxp;
                if (cur_hid_slot >= 0) {
                    hid_ifaces[cur_hid_slot][1] = ep_in;
                    hid_ifaces[cur_hid_slot][2] = ep_maxlen;
                    /* 该接口已定, 提交 */
                    hid_count++;
                    cur_hid_slot = -1;
                }
            } else if ((ep_attrs & 0x03) == 0x02) {  /* V0.13.35: 批量端点 */
                if (ep_addr & 0x80) bulk_in = ep_addr & 0x0F;
                else bulk_out = ep_addr & 0x0F;
            }
        }
        offset += dlen;
    }

    /* V0.20.101: debug dump first interface class */
    vga_puts("[USB64] cfg total_len=");
    vga_put_hex(total_len);
    vga_puts(" if0_class=");
    vga_put_hex(devbuf[5]);
    vga_puts(" if0_prot=");
    vga_put_hex(devbuf[7]);
    vga_puts("\n");

    /* V0.13.35: Mass Storage 分支 (BOT) */
    if (iface_class == USB_CLASS_MASS && bulk_in && bulk_out) {
        if (usb_set_config(dev_addr, 1) != 0) {
            vga_puts("[USB64] Set config failed (storage)\n");
            return 0;
        }
        g_usb_storage.found = 1;
        g_usb_storage.dev_addr = dev_addr;
        g_usb_storage.ep_out = bulk_out;
        g_usb_storage.ep_in = bulk_in;
        g_usb_storage.lun = 0;
        g_usb_storage.maxp = 64;
        g_usb_storage.tag = 0;
        usb64_dev_register(port, (uint16_t)(((uint16_t)devbuf[9] << 8) | devbuf[8]),
                           (uint16_t)(((uint16_t)devbuf[11] << 8) | devbuf[10]),
                           iface_class, iface_proto, dev_addr, bulk_in, "storage");
        usb_storage_probe();
        return 0;
    }

    if (hid_count == 0) {
        vga_puts("[USB64] No HID interface with interrupt IN endpoint\n");
        return 0;
    }

    /* 设置配置 1 (一次, 所有接口共享) */
    if (usb_set_config(dev_addr, 1) != 0) {
        vga_puts("[USB64] Set config failed\n");
        return 0;
    }

    uint16_t vid = (uint16_t)(((uint16_t)devbuf[9] << 8) | devbuf[8]);
    uint16_t pid = (uint16_t)(((uint16_t)devbuf[11] << 8) | devbuf[10]);
    vga_puts("[USB64] HID device port=");
    vga_put_hex(port);
    vga_puts(" ifaces=");
    vga_put_hex(hid_count);
    vga_puts("\n");

    /* 逐个 HID 接口注册 (复合接收器: 鼠标+键盘+平板全部生效) */
    for (int i = 0; i < hid_count; i++) {
        uint8_t proto = hid_ifaces[i][0];
        uint8_t ep = hid_ifaces[i][1];
        uint8_t mlen = hid_ifaces[i][2];
        if (ep == 0) continue;
        if (proto == USB_HID_PROTO_MOUSE) {
            usb_hid_register_mouse(port, dev_addr, ep, mlen, vid, pid);
        } else if (proto == USB_HID_PROTO_KEYB) {
            /* USB键盘 (单槽, 复合接收器键盘接口优先注册) */
            if (usb_kbd_found) {
                vga_puts("[USB64] USB keyboard slot busy, skip extra\n");
                continue;
            }
            usb_kbd_addr = dev_addr;
            usb_kbd_ep = ep;
            usb_kbd_report_len = (mlen > 8) ? 8 : mlen;
            usb_kbd_ehci_slot = -1;
            /* V0.13.64: EHCI 周期端点 */
            {
                extern int ehci64_present(void);
                extern int ehci64_int_ep_register(uint8_t dev_addr, uint8_t ep,
                                                  uint16_t maxlen, uint8_t *buf);
                if (ehci64_present()) {
                    int slot = ehci64_int_ep_register(dev_addr, ep,
                                                      usb_kbd_report_len, usb_kbd_report);
                    if (slot < 0) break;
                    usb_kbd_ehci_slot = slot;
                    usb_kbd_found = 1;
                    usb64_dev_register(port, vid, pid,
                                       USB_CLASS_HID, proto, dev_addr, ep, "keyboard");
                    vga_puts("[USB64] USB keyboard ready (EHCI interrupt IN)\n");
                    continue;
                }
            }
            usb_kbd_td = usb_make_interrupt_td(dev_addr, ep,
                                               usb_kbd_report, usb_kbd_report_len);
            usb_kbd_qh = (uhci_qh_t *)usb_dma_alloc(sizeof(uhci_qh_t));
            if (usb_kbd_td && usb_kbd_qh) {
                usb_kbd_qh->link = 1;
                usb_kbd_qh->el_link = (uint32_t)(uintptr_t)usb_virt_to_phys(usb_kbd_td);
                usb_kbd_qh->qh_curr = (uint32_t)(uintptr_t)usb_virt_to_phys(usb_kbd_td);
                usb_chain_add_qh(usb_kbd_qh);   /* V0.13.62: 挂水平链 */
                usb_kbd_found = 1;
                usb64_dev_register(port, vid, pid,
                                   USB_CLASS_HID, proto, dev_addr, ep, "keyboard");
                vga_puts("[USB64] USB keyboard ready (interrupt IN)\n");
            }
        } else {
            /* proto=0 或其他: 绝对定位 HID (触控屏/数位板/无线平板) */
            usb_hid_register_tablet(port, dev_addr, ep, mlen, vid, pid);
        }
    }
    return 1;
}

/* ---- USB HUB 支持 (QEMU 自动在根端口挂hub, 下游设备经hub枚举) ---- */
#define USB_DT_HUB 0x29
#define USB_CLASS_HUB 0x09
#define PORT_STAT_CONNECTION 0x0001
#define PORT_STAT_ENABLE     0x0002

/* 配置hub: SET_ADDRESS → 读hub描述符 → SET_CONFIG */
static int usb64_setup_hub(uint8_t hub_addr)
{
    if (usb_set_address(0, hub_addr) != 0) {
        vga_puts("[USB64] hub set address failed\n");
        return -1;
    }
    for (int i = 0; i < 1000; i++) { }
    uint8_t hdesc[9];
    if (usb_get_descriptor(hub_addr, USB_DT_HUB, 0, 9, hdesc) != 0) {
        vga_puts("[USB64] hub get descriptor failed\n");
        return -1;
    }
    if (usb_set_config(hub_addr, 1) != 0) {
        vga_puts("[USB64] hub set config failed\n");
        return -1;
    }
    vga_puts("[USB64] USB hub ready, addr=");
    vga_put_hex(hub_addr);
    vga_puts(" ports=");
    vga_put_hex(hdesc[2]);
    vga_puts("\n");
    return 0;
}

/* 枚举hub下游设备 (QEMU hub默认8端口, 静态拓扑无热插拔) */
static void usb64_enumerate_hub_ports(uint8_t hub_addr, uint8_t *next_addr)
{
    for (int p = 1; p <= 8; p++) {
        uint8_t st[4];
        /* GET_PORT_STATUS: wPortStatus bit0=连接 */
        if (usb_control_transfer(hub_addr, 0xA3, 0x00, 0x0000, p, 4, st, 1) != 0) continue;
        if (!(st[0] & PORT_STAT_CONNECTION)) continue;

        /* PORT_RESET (QEMU: 复位下游设备并自动置ENABLE) */
        usb_control_transfer(hub_addr, 0x23, 0x03, 0x0004, p, 0, 0, 0);
        for (int r = 0; r < 10; r++) {
            if (usb_control_transfer(hub_addr, 0xA3, 0x00, 0x0000, p, 4, st, 1) != 0) break;
            if (st[0] & PORT_STAT_ENABLE) break;
        }

        /* 下游设备完整枚举 (地址0广播, hub转发) */
        uint8_t dev_addr = (*next_addr)++;
        vga_puts("[USB64] Hub port ");
        vga_put_hex((uint8_t)p);
        vga_puts(" -> downstream\n");
        usb64_enumerate_one(dev_addr, 100 + p);
    }
}

/* ---- USB 设备枚举 (遍历所有根端口) ---- */
/* V0.20.101: exported for boot-time EHCI fallback */
void usb64_enumerate(void)
{
    uint8_t next_addr = 1;
    /* V0.13.64: EHCI 路径 (端口来自 EHCI PORTSC) */
    extern int ehci64_present(void);
    extern int ehci64_port_ccs(int port);
    extern int ehci64_port_count(void);
    int is_ehci = ehci64_present();
    int nports = is_ehci ? ehci64_port_count() : 2;
    for (int port = 0; port < nports; port++) {
        int conn = is_ehci ? ehci64_port_ccs(port)
                           : (uhci_read_port(UHCI_PORTSC1 + port * 2) & UHCI_PORTSC_CCS);
        if (!conn) continue;

        /* 先读12字节识别设备类型 (含VID/PID; QEMU hub VID=0x0409 PID=0x55aa) */
        uint8_t dbuf[12];
        if (usb_get_descriptor(0, USB_DT_DEVICE, 0, 12, dbuf) != 0) {
            if (usb_get_descriptor(0, USB_DT_DEVICE, 0, 12, dbuf) != 0) continue;
        }
        uint16_t vid = ((uint16_t)dbuf[9] << 8) | dbuf[8];
        uint16_t pid = ((uint16_t)dbuf[11] << 8) | dbuf[10];
        if (vid == 0x0409 && pid == 0x55aa) {
            /* hub: 分配地址+配置, 然后枚举下游 */
            uint8_t hub_addr = next_addr++;
            if (usb64_setup_hub(hub_addr) != 0) continue;
            usb64_enumerate_hub_ports(hub_addr, &next_addr);
        } else {
            /* 普通设备: 完整枚举 */
            usb64_enumerate_one(next_addr++, port);
        }
    }
}

/* ---- 轮询HID报告 (主循环调用) ---- */
/* ---- USB键盘轮询 (usb64_poll内部) ---- */
/* V0.13.8 P5.1: 清空 USB 键盘队列(枚举噪声), 主循环就绪后调用 */
void usb64_kbd_flush(void)
{
    usb_kbd_q_head = 0;
    usb_kbd_q_tail = 0;
    usb_kbd_q_count = 0;
}

static void usb_kbd_poll(void);

void usb64_poll(void)
{
    /* V0.13.66: XHCI 事件轮询 (无 UHCI/EHCI 时的 USB 通路) */
    {
        extern int xhci64_present(void);
        extern void xhci64_poll(void);
        extern void xhci64_kbd_flush(void);
        if (xhci64_present()) {
            xhci64_poll();
            /* 把 XHCI 键盘 HID 报告转交给统一键盘事件队列 (early return 之前) */
            xhci64_kbd_flush();
        }
    }

    if (!uhci_initialized && !ehci64_present()) return;
    /* V0.13.62: 鼠标轮询 — 遍历全部鼠标设备 (多鼠标/无线接收器多接口) */
    for (int mi = 0; mi < g_usb_mice_count; mi++) {
        usb_mouse_t *m = &g_usb_mice[mi];
        if (!m->found) continue;
        /* V0.13.64: EHCI 周期端点路径 */
        if (m->ehci_slot >= 0) {
            extern int ehci64_int_ep_poll(int idx);
            if (!ehci64_int_ep_poll(m->ehci_slot)) continue;
            if (m->report_len >= 3) {
                uint8_t buttons = m->report[0];
                int8_t dx = (int8_t)m->report[1];
                int8_t dy = (int8_t)m->report[2];
                if (m->report_len >= 5) {
                    if (m->report[4] & 0x01) buttons |= 0x08;
                    if (m->report[4] & 0x02) buttons |= 0x10;
                }
                m->left = buttons & 0x01;
                m->right = buttons & 0x02;
                m->middle = buttons & 0x04;
                m->x += dx;
                m->y -= dy;
                if (m->x < 0) m->x = 0;
                if (m->x > 1023) m->x = 1023;
                if (m->y < 0) m->y = 0;
                if (m->y > 767) m->y = 767;
                if (m->report_len >= 4)
                    m->wheel += (int8_t)m->report[3];
            }
            continue;
        }
        if (!m->td) continue;
        if (m->td->status & TD_STATUS_ACTIVE) continue;
        if (!(m->td->status & (TD_STATUS_STALLED | TD_STATUS_BABBLE))) {
            /* 解析 HID 鼠标报告: byte0=buttons byte1=dx byte2=dy byte3=wheel
             * 5字节扩展: byte4 低2位=侧键1/2; 6字节: byte5=水平滚轮 */
            if (m->report_len >= 3) {
                uint8_t buttons = m->report[0];
                int8_t dx = (int8_t)m->report[1];
                int8_t dy = (int8_t)m->report[2];
                if (m->report_len >= 5) {          /* 侧键 → 按钮位4/5 */
                    if (m->report[4] & 0x01) buttons |= 0x08;
                    if (m->report[4] & 0x02) buttons |= 0x10;
                }
                m->left = buttons & 0x01;
                m->right = buttons & 0x02;
                m->middle = buttons & 0x04;
                m->x += dx;
                m->y -= dy;  /* USB Y向下为正, 屏幕Y向下为正 → 取反 */
                if (m->x < 0) m->x = 0;
                if (m->x > 1023) m->x = 1023;
                if (m->y < 0) m->y = 0;
                if (m->y > 767) m->y = 767;
                if (m->report_len >= 4)
                    m->wheel += (int8_t)m->report[3];
            }
        }
        /* 重新激活 TD 继续轮询 (QEMU 完成 TD 后清 QH.el_link, 必须恢复) */
        m->td->status = TD_STATUS_ACTIVE;
        if (m->qh) {
            m->qh->el_link = (uint32_t)(uintptr_t)usb_virt_to_phys(m->td);
            m->qh->qh_curr = (uint32_t)(uintptr_t)usb_virt_to_phys(m->td);
        }
    }
    /* V0.13.8 P6: tablet 绝对定位轮询 (报告: [id=2?] btn X(16) Y(16)) */
    for (int ti = 0; ti < g_usb_tablets_count; ti++) {
        usb_tablet_t *t = &g_usb_tablets[ti];
        if (!t->found) continue;
        /* V0.13.64: EHCI 周期端点路径 */
        if (t->ehci_slot >= 0) {
            extern int ehci64_int_ep_poll(int idx);
            if (!ehci64_int_ep_poll(t->ehci_slot)) continue;
            int off = 0;
            if (t->report_len >= 6 && t->report[0] == 2) off = 1;
            if (t->report_len >= off + 5) {
                uint8_t buttons = t->report[off];
                int ax = t->report[off + 1] | (t->report[off + 2] << 8);
                int ay = t->report[off + 3] | (t->report[off + 4] << 8);
                /* V0.54.7: HID 16 位绝对坐标一律按 0-32767 归一映射到 1024x768
                 * (QEMU usb-tablet 实际就是该范围; 旧 ">1024 才缩放" 会把
                 *  左上角 32px 内的坐标当成像素值, 导致点击落到错误的 x*32 处) */
                ax = ax * 1024 / 32768;
                ay = ay * 768 / 32768;
                t->abs_x = ax;
                t->abs_y = ay;
                if (t->abs_x > 1023) t->abs_x = 1023;
                if (t->abs_y > 767) t->abs_y = 767;
                t->left = buttons & 0x01;
                t->right = buttons & 0x02;
            }
            continue;
        }
        if (!t->td) continue;
        if (t->td->status & TD_STATUS_ACTIVE) continue;
        if (!(t->td->status & (TD_STATUS_STALLED | TD_STATUS_BABBLE))) {
            int off = 0;
            if (t->report_len >= 6 && t->report[0] == 2) off = 1;
            if (t->report_len >= off + 5) {
                uint8_t buttons = t->report[off];
                int ax = t->report[off + 1] | (t->report[off + 2] << 8);
                int ay = t->report[off + 3] | (t->report[off + 4] << 8);
                /* V0.54.7: 16 位绝对坐标统一按 0-32767 归一 (同 EHCI 分支) */
                ax = ax * 1024 / 32768;
                ay = ay * 768 / 32768;
                t->abs_x = ax;
                t->abs_y = ay;
                if (t->abs_x > 1023) t->abs_x = 1023;
                if (t->abs_y > 767) t->abs_y = 767;
                t->left = buttons & 0x01;
                t->right = buttons & 0x02;
            }
        }
        t->td->status = TD_STATUS_ACTIVE;
        if (t->qh) {
            t->qh->el_link = (uint32_t)(uintptr_t)usb_virt_to_phys(t->td);
            t->qh->qh_curr = (uint32_t)(uintptr_t)usb_virt_to_phys(t->td);
        }
    }
    /* 键盘轮询 */
    usb_kbd_poll();

    /* V0.13.26: 热插拔检测 (每 50 帧 ≈ 500ms) */
    g_usb_hotplug_ticks++;
    if (g_usb_hotplug_ticks >= 50) {
        g_usb_hotplug_ticks = 0;
        usb64_hotplug_check();
    }
}

/* ---- USB 键盘: HID usage → 系统键值映射 ---- */
static int usb_kbd_hid2key(uint8_t usage)
{
    /* 标准HID键盘usage: 0x04-0x1D=a-z, 0x1E-0x27=1-0, 0x28=Enter,
     * 0x29=Esc, 0x2A=Backspace, 0x2C=Space, 0x4F/0x50/0x51/0x52=右左上下 */
    if (usage >= 0x04 && usage <= 0x1D) return 'a' + (usage - 0x04);
    if (usage >= 0x1E && usage <= 0x26) return '1' + (usage - 0x1E);
    if (usage == 0x27) return '0';
    if (usage == 0x28) return '\n';
    if (usage == 0x29) return 27;   /* ESC */
    if (usage == 0x2A) return '\b';
    if (usage == 0x2B) return '\t';  /* Tab */
    if (usage == 0x2C) return ' ';
    if (usage == 0x2D) return '-';
    if (usage == 0x2E) return '=';
    if (usage == 0x2F) return '[';
    if (usage == 0x30) return ']';
    if (usage == 0x31) return '\\';
    if (usage == 0x33) return ';';
    if (usage == 0x34) return '\'';
    if (usage == 0x35) return '`';
    if (usage == 0x36) return ',';
    if (usage == 0x37) return '.';
    if (usage == 0x38) return '/';
    /* V0.13.26: F1-F12 功能键 (映射扩展码 0xA0-0xAB) */
    if (usage >= 0x3A && usage <= 0x45) return 0xA0 + (usage - 0x3A);
    /* CapsLock / Insert / Delete / Home-End 组 */
    if (usage == 0x39) return KEY_EXT_CAPS;
    if (usage == 0x49) return KEY_EXT_INS;
    if (usage == 0x4C) return 0x7F;   /* Delete */
    if (usage == 0x53) return KEY_EXT_NUM;
    if (usage == 0x5F) return KEY_EXT_CAPS;  /* 防止无关键 */

    /* V0.13.8 P5: 方向键/翻页统一映射内部扩展码 0x90-0x97 (与 PS/2 一致,
     * 修复 0xE0-0xE3 与 PS/2 扩展前缀冲突 → 方向键在 USB 路径不可用) */
    if (usage == 0x4F) return KEY_EXT_RIGHT; /* 右 */
    if (usage == 0x50) return KEY_EXT_LEFT;  /* 左 */
    if (usage == 0x51) return KEY_EXT_DOWN;  /* 下 */
    if (usage == 0x52) return KEY_EXT_UP;    /* 上 */
    if (usage == 0x4B) return KEY_EXT_PGUP;  /* PageUp */
    if (usage == 0x4E) return KEY_EXT_PGDN;  /* PageDown */
    if (usage == 0x4A) return KEY_EXT_HOME;  /* Home */
    if (usage == 0x4D) return KEY_EXT_END;   /* End */
    return 0;
}

static void usb_kbd_push(int key)
{
    if (usb_kbd_q_count >= USB_KBD_QUEUE_SIZE) return;  /* 队列满丢弃 */
    usb_kbd_queue[usb_kbd_q_head] = (uint8_t)key;
    usb_kbd_q_head = (usb_kbd_q_head + 1) % USB_KBD_QUEUE_SIZE;
    usb_kbd_q_count++;
}

/* V0.13.5: 修饰键状态机 — 按当前 modifier 变换键值 (Shift 大小写/符号) */
static int usb_kbd_apply_mod(int key, uint8_t mod)
{
    int shift = (mod & 0x02) || (mod & 0x20);   /* LShift/RShift */
    if (!shift) return key;
    if (key >= 'a' && key <= 'z') return key - 'a' + 'A';
    switch (key) {
        case '1': return '!'; case '2': return '@'; case '3': return '#';
        case '4': return '$'; case '5': return '%'; case '6': return '^';
        case '7': return '&'; case '8': return '*'; case '9': return '(';
        case '0': return ')';
        default: return key;
    }
}

static int usb_kbd_in_last(uint8_t k)
{
    for (int i = 0; i < 6; i++)
        if (usb_kbd_last_keys[i] == k) return 1;
    return 0;
}

/* 登记设备到注册表 */
static void usb64_dev_register(uint8_t port, uint16_t vid, uint16_t pid,
                               uint8_t cls, uint8_t proto, uint8_t addr,
                               uint8_t ep, const char *kind)
{
    if (g_usb_dev_count >= USB_MAX_DEVS) return;
    usb_dev_t *d = &g_usb_devs[g_usb_dev_count];
    d->port = port; d->vid = vid; d->pid = pid;
    d->iface_class = cls; d->proto = proto;
    d->addr = addr; d->ep = ep; d->found = 1;
    int n = 0;
    while (kind[n] && n < 15) { d->kind[n] = kind[n]; n++; }
    d->kind[n] = 0;
    g_usb_dev_count++;
}

/* 查询设备计数 */
int usb64_dev_count(void) { return g_usb_dev_count; }

/* 查询设备信息 */
int usb64_dev_info(int idx, uint8_t *port, uint16_t *vid, uint16_t *pid,
                   char *kind)
{
    if (idx < 0 || idx >= g_usb_dev_count) return -1;
    usb_dev_t *d = &g_usb_devs[idx];
    if (port) *port = d->port;
    if (vid) *vid = d->vid;
    if (pid) *pid = d->pid;
    if (kind) {
        int n = 0;
        while (d->kind[n] && n < 15) { kind[n] = d->kind[n]; n++; }
        kind[n] = 0;
    }
    return 0;
}

/* 热插拔: 是否有端口连接变化事件 */
int usb64_hotplug_pending(void)
{
    int p = g_usb_hotplug_pending;
    g_usb_hotplug_pending = 0;
    return p;
}

/* 热插拔检测(usb64_poll 内部, 约每 500ms 调用) */
static void usb64_hotplug_check(void)
{
    if (!uhci_initialized) return;  /* EHCI 热插拔待后续 (真机固定设备足够) */
    for (int port = 0; port < 2; port++) {
        uint16_t psc = uhci_read_port(UHCI_PORTSC1 + port * 2);
        uint16_t conn = (psc & UHCI_PORTSC_CCS) ? 1 : 0;
        if (conn != g_port_conn_prev[port]) {
            g_port_conn_prev[port] = conn;
            g_usb_hotplug_pending = 1;
            vga_puts("[USB64] Hotplug: port ");
            vga_put_hex(port);
            vga_puts(conn ? " connected\n" : " disconnected\n");
        }
    }
}

/* 解析标准8字节键盘报告: [0]=modifier, [1]=reserved, [2..7]=6键usage */
static void usb_kbd_proc_report(void)
{
    uint8_t *keys = &usb_kbd_report[2];
    uint8_t mod = usb_kbd_report[0];
    /* 新按下的键 → 事件 (报告是状态快照, 检测出现的新键; 修饰键即时应用) */
    for (int i = 0; i < 6; i++) {
        uint8_t k = keys[i];
        if (k == 0) continue;
        if (!usb_kbd_in_last(k)) {
            int key = usb_kbd_hid2key(k);
            if (key) usb_kbd_push(usb_kbd_apply_mod(key, mod));
        }
    }
    /* 记录状态快照供差分与修饰键查询 */
    for (int i = 0; i < 6; i++) usb_kbd_last_keys[i] = keys[i];
    usb_kbd_last_mod = mod;
}

/* V0.13.66: XHCI 键盘报告注入 (xhci64.c 调用) */
void usb64_kbd_report_inject(uint8_t *report, uint8_t len)
{
    if (len > 8) len = 8;
    for (int i = 0; i < len; i++) usb_kbd_report[i] = report[i];
    usb_kbd_report_len = len;
    usb_kbd_found = 1;
    usb_kbd_proc_report();
}

/* ---- USB键盘轮询 (usb64_poll内部) ---- */
static void usb_kbd_poll(void)
{
    /* V0.13.64: EHCI 键盘路径 */
    if (usb_kbd_ehci_slot >= 0 && usb_kbd_found) {
        extern int ehci64_int_ep_poll(int idx);
        if (ehci64_int_ep_poll(usb_kbd_ehci_slot)) usb_kbd_proc_report();
        return;
    }
    if (!usb_kbd_td || !usb_kbd_qh || !usb_kbd_found) return;
    if (usb_kbd_td->status & TD_STATUS_ACTIVE) return;
    if (!(usb_kbd_td->status & (TD_STATUS_STALLED | TD_STATUS_BABBLE))) {
        if (usb_kbd_report_len >= 8) usb_kbd_proc_report();
        else if (usb_kbd_report_len >= 2) usb_kbd_proc_report();
    }
    /* 重新激活TD + 恢复QH.el_link */
    usb_kbd_td->status = TD_STATUS_ACTIVE;
    usb_kbd_qh->el_link = (uint32_t)(uintptr_t)usb_virt_to_phys(usb_kbd_td);
}

/* ---- 对外接口: USB键盘 (供桌面/Shell) ---- */
int usb64_kbd_available(void)
{
    if (usb_kbd_found) return 1;
    {
        extern int xhci64_present(void);
        extern int xhci64_kbd_available(void);
        if (xhci64_present() && xhci64_kbd_available()) return 1;
    }
    return 0;
}
int usb64_kbd_haschar(void) { return usb_kbd_q_count > 0; }
int usb64_kbd_getchar(void)
{
    if (usb_kbd_q_count <= 0) return 0;
    int c = usb_kbd_queue[usb_kbd_q_tail];
    usb_kbd_q_tail = (usb_kbd_q_tail + 1) % USB_KBD_QUEUE_SIZE;
    usb_kbd_q_count--;
    return c;
}
/* V0.13.5: 修饰键查询 (bit0=LCtrl bit1=LShift bit2=LAlt bit4=RCtrl bit5=RShift bit6=RAlt) */
int usb64_kbd_get_mod(void)
{
    /* V0.13.9: HID 修饰键字节 → 统一位定义 (与 PS/2 keyboard64 对齐:
     * bit0=Ctrl bit1=Shift bit2=Alt bit3=Win; HID: bit0-3=左 4-7=右) */
    uint8_t b = usb_kbd_last_mod;
    int mod = 0;
    if (b & 0x11) mod |= 0x01;   /* Ctrl: L(bit0)|R(bit4) */
    if (b & 0x22) mod |= 0x02;   /* Shift: L(bit1)|R(bit5) */
    if (b & 0x44) mod |= 0x04;   /* Alt: L(bit2)|R(bit6) */
    if (b & 0x88) mod |= 0x08;   /* Win: L(bit3)|R(bit7) */
    return mod;
}

/* ---- 对外接口: USB鼠标状态 (供桌面/驱动合并) ---- */
/* V0.13.62: 聚合 API — 多鼠标合成统一指针 (桌面用最近插入的鼠标) */
static usb_mouse_t *usb64_primary_mouse(void)
{
    if (g_usb_mice_count <= 0) return 0;
    return &g_usb_mice[g_usb_mice_count - 1];   /* 最后注册 = 最近插入 */
}

int usb64_mouse_available(void) { return g_usb_mice_count > 0; }
void usb64_mouse_get(int *x, int *y, int *left, int *right)
{
    usb_mouse_t *m = usb64_primary_mouse();
    if (!m) { if (x) *x = 512; if (y) *y = 384; if (left) *left = 0; if (right) *right = 0; return; }
    if (x) *x = m->x;
    if (y) *y = m->y;
    if (left) *left = m->left;
    if (right) *right = m->right;
}

/* V0.13.9: USB 鼠标滚轮累积值 (消费式 — 读后清零, 聚合所有鼠标) */
int usb64_mouse_get_wheel(void)
{
    int w = 0;
    for (int i = 0; i < g_usb_mice_count; i++) {
        w += g_usb_mice[i].wheel;
        g_usb_mice[i].wheel = 0;
    }
    return w;
}

/* V0.13.8 P6: tablet 绝对定位 API (hsinput 优先使用; 多平板取最近插入) */
int usb64_tablet_available(void) { return g_usb_tablets_count > 0; }
void usb64_tablet_get(int *x, int *y, int *left, int *right)
{
    usb_tablet_t *t = (g_usb_tablets_count > 0) ?
        &g_usb_tablets[g_usb_tablets_count - 1] : 0;
    if (!t) { if (x) *x = 512; if (y) *y = 384; if (left) *left = 0; if (right) *right = 0; return; }
    if (x) *x = t->abs_x;
    if (y) *y = t->abs_y;
    if (left) *left = t->left;
    if (right) *right = t->right;
}
