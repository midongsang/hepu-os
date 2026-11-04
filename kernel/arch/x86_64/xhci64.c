/* ============================================================================
 * 赫普OS - XHCI (eXtensible Host Controller Interface, USB 3.0) 驱动
 * V0.13.66: 完成现代主板 USB3 通路 —— qemu-xhci 下鼠标/键盘枚举+输入全链路
 *   - 控制器初始化: 能力探测 / 复位 / 命令环 / 事件环 / DCBAAP / 中断器
 *   - 命令提交(串行): Enable Slot / Address Device / Configure Endpoint
 *   - 事件处理: 命令完成(槽位号取 field2>>24) / 端口变化 / 传输完成
 *   - 控制传输: 生产者索引环(环尾 LINK+TC 回绕), 不再每次从 ring[0] 重写,
 *     消除 STATUS 事件返回时 LINK 未跨过的时序竞争; GET_DESCRIPTOR wValue
 *     字节序(高字节=类型/低字节=索引)
 *   - HID 中断端点: boot 协议鼠标/键盘, 完成后自动重提交
 *   - 复合 HID 设备(无线接收器): 解析配置描述符中全部 HID 接口及各自的
 *     中断 IN 端点(EP1..EP15 IN, DCI=3..31), 每端点独立环/缓冲/DCI,
 *     一次 Configure Endpoint 全部 Add, 按 DCI 路由鼠标/键盘报告
 *   - 验证(QEMU 7.2 qemu-xhci): 鼠标 VID=0627/PID=0001(proto=2)、键盘
 *     (proto=1) 均 state=2, sendkey/mouse_move 报告正确(trace 见 ep=3)
 * 恒等映射物理地址直用 (与 e1000/ehci 一致)
 * ========================================================================== */

#include <stdint.h>
#ifndef NULL
#define NULL ((void *)0)
#endif

/* ---- 能力寄存器 (MMIO+0) ---- */
#define XHCI_CAPLENGTH     0x00
#define XHCI_HCSPARAMS1    0x04
#define XHCI_HCCPARAMS1    0x10
#define XHCI_DBOFF         0x14
#define XHCI_RTSOFF        0x18
#define XHCI_HCS1_MAXSLOTS(v)  ((v) & 0xFF)
#define XHCI_HCS1_MAXINTRS(v)  (((v) >> 8) & 0x3FF)
#define XHCI_HCS1_MAXPORTS(v)  (((v) >> 24) & 0xFF)
#define XHCI_HCC_AC64         (1u << 0)

/* ---- 操作寄存器 ---- */
#define XHCI_USBCMD      0x00
#define XHCI_USBSTS      0x04
#define XHCI_CRCR        0x18
#define XHCI_DCBAAP      0x30
#define XHCI_CONFIG      0x38
#define XHCI_CMD_RS      (1u << 0)
#define XHCI_CMD_HCRST   (1u << 1)
#define XHCI_CMD_INTE    (1u << 2)
#define XHCI_STS_HCH     (1u << 0)
#define XHCI_STS_PCD     (1u << 4)

/* ---- Runtime: 中断器 0 (interrupter 从 RTSOFF+0x20 开始) ---- */
#define XHCI_IMAN        0x20
#define XHCI_ERSTSZ      0x28
#define XHCI_ERSTBA      0x30
#define XHCI_ERDP        0x38
#define XHCI_IMAN_IP     (1u << 0)
#define XHCI_IMAN_IE     (1u << 1)

/* ---- TRB 类型 ---- */
#define XHCI_TRB_TYPE(x)  ((uint32_t)((x) << 10))
#define XHCI_TRB_LINK     6
#define XHCI_TRB_NORMAL   1
#define XHCI_TRB_SETUP    2
#define XHCI_TRB_DATA     3
#define XHCI_TRB_STATUS   4
#define XHCI_TRB_ENABLE_SLOT   9
#define XHCI_TRB_ADDRESS_DEV   11
#define XHCI_TRB_CONFIGURE_EP  12
#define XHCI_TRB_EVT_TRANSFER  32
#define XHCI_TRB_EVT_CMD_COMP  33
#define XHCI_TRB_EVT_PORT_CHG  34

#define XHCI_TRB_CYCLE    (1u << 0)
#define XHCI_TRB_TC       (1u << 1)
#define XHCI_TRB_IOC      (1u << 5)
#define XHCI_TRB_IDT      (1u << 6)   /* Immediate Data: SETUP TRB 必须置位 */
#define XHCI_TRB_DIR_IN   (1u << 16)
#define XHCI_TRB_BSR      (1u << 9)
#define XHCI_TRB_SLOT_ID(v)  ((uint32_t)((v) << 24))
#define XHCI_CC_SUCCESS  1
/* xHCI EP 上下文 EP Type 字段 (DW1 bits 3-5), 不是 USB 描述符传输类型 */
#define XHCI_EPT_CTRL      4   /* 控制端点 (EP0) */
#define XHCI_EPT_INTR_OUT  3   /* 中断 OUT */
#define XHCI_EPT_INTR_IN   7   /* 中断 IN (HID 鼠标/键盘) */
#define XHCI_DCI_EP0       1   /* EP0 设备上下文索引 / 门铃值 */
#define XHCI_DCI_EP1_IN    3   /* EP1 IN  DCI = 2*1 + 1 = 3 */
#define XHCI_SPEED_HS    2

#define XHCI_MAX_SLOTS   8
#define XHCI_CMD_RING_SZ 32
#define XHCI_EVT_RING_SZ 16
/* 每环 N 槽: 0..N-2 为普通 TRB, N-1 固定为 LINK TRB(回环首+TC)。 */
#define XHCI_EP0_RING_SZ 32
#define XHCI_INT_RING_SZ 8

/* xHCI TRB = 16 字节: DW0/1(Parameter) + DW2(Status) + DW3(Control)。
 * 注意: 不得再加第四个 uint32 —— packed+aligned(16) 会把 20 字节向上取整成 32,
 * 导致软件环按 32 字节跨步而控制器按 16 字节 DMA, 事件槽整体错位。 */
typedef struct {
    uint64_t field0;
    uint32_t field1;
    uint32_t field2;
} __attribute__((packed, aligned(16))) xhci_trb_t;

typedef struct {
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;
    uint32_t e;
    uint32_t f;
    uint32_t g;
    uint32_t h;
} __attribute__((packed)) xhci_ep_ctx_t;

typedef struct {
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;
    uint32_t e;
    uint32_t f;
    uint32_t g;
    uint32_t h;
} __attribute__((packed)) xhci_slot_ctx_t;

/* 每个设备最多跟踪的中断 IN 端点(复合无线接收器: 键盘/鼠标/多媒体多接口)。
 * 上下文 index 覆盖到 DCI=31 (EP15 IN), 共 31 个 EP 上下文槽。 */
#define XHCI_MAX_DCI      31
#define XHCI_MAX_INT_EPS  8

/* 输入上下文(对照 QEMU/SeaBIOS):
 *   +0   32B 输入控制上下文: DW0=Drop(禁用)标志, DW1=Add(使能)标志
 *   +32  Slot 上下文 (index 0)
 *   +32+32*i  EP 上下文 (index i: 1=EP0, 2=EP1 OUT, 3=EP1 IN, ..., 31=EP15 IN)
 * 注意 Add/Drop 字段顺序; QEMU 用 ictl_ctx[0] 做 drop、ictl_ctx[1] 做 add。
 * ep[] 数组按下标 (index-1) 访问, 即 ep[0]=index1(EP0)。 */
typedef struct {
    uint32_t drop_flags;     /* DW0: Drop Context Flags */
    uint32_t add_flags;      /* DW1: Add Context Flags  */
    uint32_t rsvd0[6];
    xhci_slot_ctx_t slot;    /* +32   index 0 */
    xhci_ep_ctx_t ep[XHCI_MAX_DCI]; /* +64 起, index 1..31 */
} __attribute__((packed, aligned(64))) xhci_in_ctx_t;

/* 输出(设备)上下文: DCBAAP[slot] 指向, 布局与输入相同但无控制头 */
typedef struct {
    xhci_slot_ctx_t slot;    /* +0   index 0 */
    xhci_ep_ctx_t ep[XHCI_MAX_DCI]; /* +32 起, index 1..31 */
} __attribute__((packed, aligned(64))) xhci_dev_ctx_t;

/* 一个中断 IN 端点的运行时状态 (复合设备可有多个, 各属一个 HID 接口) */
typedef struct {
    int      active;
    uint8_t  dci;          /* DCI = 2*ep_num + 1 (IN) */
    uint8_t  ep_num;
    uint8_t  proto;        /* bInterfaceProtocol: 1=键盘 2=鼠标 0=其他 */
    uint8_t  int_len;      /* 中断传输最大长度 */
    uint8_t  interval_exp; /* bInterval (指数, 高速单位为微帧) */
    xhci_trb_t *ring;
    uint32_t ring_index;
    uint32_t ring_cycle;
    uint8_t  *buf;
} xhci_int_ep_t;

typedef struct {
    uint32_t slot_id;
    uint32_t port;
    uint32_t speed;
    uint32_t state;         /* 0=未寻址 1=已寻址 2=已配置 */
    xhci_dev_ctx_t *dev_ctx;
    xhci_in_ctx_t *in_ctx;
    xhci_trb_t *ep0_ring;
    uint32_t ep0_cycle;
    uint32_t ep0_index;
    xhci_int_ep_t int_eps[XHCI_MAX_INT_EPS]; /* 多中断端点(复合设备) */
    uint32_t int_ep_count;
    uint32_t iface_class;
    uint32_t iface_proto;
    uint32_t is_kbd;
    uint32_t is_mouse;
    uint32_t vid, pid;
    uint8_t kbd_report[8];
    uint32_t mouse_dx, mouse_dy, mouse_btn;
    uint32_t kbd_new;
} xhci_dev_t;

/* 静态池 (1:1 映射) */
static xhci_dev_ctx_t xhci_dev_ctx_pool[XHCI_MAX_SLOTS] __attribute__((aligned(64)));
static xhci_in_ctx_t xhci_in_ctx_pool[XHCI_MAX_SLOTS] __attribute__((aligned(64)));
static xhci_trb_t xhci_ep0_ring_pool[XHCI_MAX_SLOTS][XHCI_EP0_RING_SZ] __attribute__((aligned(64)));
static xhci_trb_t xhci_int_ring_pool[XHCI_MAX_SLOTS][XHCI_MAX_INT_EPS][XHCI_INT_RING_SZ] __attribute__((aligned(64)));
static uint8_t xhci_int_buf_pool[XHCI_MAX_SLOTS][XHCI_MAX_INT_EPS][64] __attribute__((aligned(64)));

static volatile uint32_t *xhci_op = 0;      /* 操作寄存器基 = MMIO+caplen */
static volatile uint32_t *xhci_mmio_abs = 0; /* MMIO 绝对基 */
static uint32_t xhci_dboff = 0;
static uint32_t xhci_rts_off = 0;
static uint32_t xhci_max_slots = 0;
static uint32_t xhci_max_ports = 0;
static int xhci_initialized = 0;
static int xhci_present_flag = 0;
static uint32_t xhci_pending_port = 0;   /* 待探测端口(事件循环外处理) */
static uint64_t xhci_mmio_phys = 0;

static xhci_trb_t xhci_cmd_ring[XHCI_CMD_RING_SZ] __attribute__((aligned(64)));
static uint32_t xhci_cmd_idx = 0;
static uint32_t xhci_cmd_cycle = 1;

static xhci_trb_t xhci_evt_ring[XHCI_EVT_RING_SZ] __attribute__((aligned(64)));
static uint32_t xhci_evt_idx = 0;
static uint32_t xhci_evt_cycle = 1;
typedef struct {
    uint64_t addr;
    uint32_t size;
    uint32_t rsvd;
} __attribute__((packed)) xhci_erst_t;
static xhci_erst_t xhci_erst[1] __attribute__((aligned(16)));

static uint64_t xhci_dcbaap[XHCI_MAX_SLOTS] __attribute__((aligned(64)));
static xhci_dev_t xhci_devs[XHCI_MAX_SLOTS];

/* Enable Slot 命令完成事件带回的槽位号 */
static uint32_t xhci_evt_slot_id = 0;
static uint32_t xhci_last_cmd_cc = 0;
static int xhci_cmd_done = 0;
static int xhci_cmd_pending = 0;
int xhci_ctrl_done = 0;
int xhci_ctrl_ok = 0;

extern void vga_puts(const char *s);
extern void vga_put_hex(uint64_t v);
extern uint32_t pci_find_class_bar0(uint8_t base, uint8_t sub, uint8_t prog);
extern void *usb64_dma_alloc(uint32_t size);

static inline uint32_t xhci_read32(uint32_t off) { return xhci_op[off / 4]; }
static inline void xhci_write32(uint32_t off, uint32_t val) { xhci_op[off / 4] = val; }
/* 绝对 MMIO 偏移读写（runtime/门铃区用, 不叠加 caplen） */
static inline uint32_t xhci_r32_abs(uint32_t off) { return xhci_mmio_abs[off / 4]; }
static inline void xhci_w32_abs(uint32_t off, uint32_t val) { xhci_mmio_abs[off / 4] = val; }

static void xhci_memzero(void *d, uint32_t n) {
    uint8_t *p = (uint8_t *)d;
    for (uint32_t i = 0; i < n; i++) p[i] = 0;
}
/* 敲门铃: 寄存器偏移 = DBOFF + slotid*4, 写入值 = 端点 DCI。
 * 命令环 slot=0、dci=0; EP0 写 dci=1; EP1 IN 写 dci=3。
 * (对照 QEMU xhci_doorbell_write: reg 为 slotid, val 低字节为 epid;
 *  SeaBIOS xhci_doorbell: db[slotid] = value)。 */
static void xhci_ring_doorbell(uint32_t slot, uint32_t dci)
{
    volatile uint32_t *db = (volatile uint32_t *)((uintptr_t)xhci_mmio_abs + xhci_dboff);
    db[slot] = dci;
}

static void xhci64_poll_events(void);

/* ---- 提交命令 (串行) ---- */
static int xhci_submit_cmd(xhci_trb_t *trb, uint32_t *cc_out)
{
    if (xhci_cmd_pending) return -1;

    xhci_cmd_ring[xhci_cmd_idx] = *trb;
    xhci_cmd_ring[xhci_cmd_idx].field2 |= (xhci_cmd_cycle ? XHCI_TRB_CYCLE : 0);

    xhci_cmd_idx++;
    if (xhci_cmd_idx == XHCI_CMD_RING_SZ - 1) {
        xhci_trb_t *lk = &xhci_cmd_ring[XHCI_CMD_RING_SZ - 1];
        xhci_memzero(lk, sizeof(*lk));
        lk->field0 = (uint64_t)(uintptr_t)&xhci_cmd_ring[0];
        lk->field2 = XHCI_TRB_TYPE(XHCI_TRB_LINK) | XHCI_TRB_TC |
                     (xhci_cmd_cycle ? XHCI_TRB_CYCLE : 0);
        xhci_cmd_idx = 0;
        xhci_cmd_cycle ^= 1;
    }

    xhci_cmd_done = 0;
    xhci_cmd_pending = 1;
    xhci_ring_doorbell(0, 0);

    for (int w = 0; w < 3000000 && !xhci_cmd_done; w++) {
        xhci64_poll_events();
        if (xhci_cmd_done) break;
        for (volatile int d = 0; d < 300; d++);
    }
    xhci_cmd_pending = 0;
    if (cc_out) *cc_out = xhci_last_cmd_cc;
    if (!xhci_cmd_done) {
        vga_puts("[XHCI] CMD TIMEOUT type=");
        vga_put_hex((trb->field2 >> 10) & 0x3F);
        vga_puts(" sts=");
        vga_put_hex(xhci_read32(XHCI_USBSTS));
        vga_puts("\n");
    }
    return xhci_cmd_done ? 0 : -1;
}

/* ---- 事件: 命令完成 ---- */
static void xhci_handle_cmd_completion(xhci_trb_t *evt)
{
    /* TRB 布局(对照 QEMU xhci_write_event):
     *   field1 = length | ccode<<24          -> 完成码在 field1 高字节
     *   field2 = slotid<<24 | epid<<16|type -> 槽位号在 field2 最高字节
     * 旧代码误从 field1>>16 取槽位号(恒为 0), 导致设备初始化被跳过。 */
    xhci_last_cmd_cc = (evt->field1 >> 24) & 0xFF;
    xhci_evt_slot_id = (evt->field2 >> 24) & 0xFF;
    xhci_cmd_done = 1;
}

/* ---- 事件: 端口变化 ---- */
static void xhci_handle_port_chg(xhci_trb_t *evt);
/* ---- 事件: 传输完成 ---- */
static void xhci_handle_transfer_evt(xhci_trb_t *evt);

/* 事件环 = 单段循环队列(ERST 段表, 无 LINK TRB; QEMU 写满段后自动回绕并翻转
 * er_pcs)。软件作为消费者: 从 dequeue 位起连续处理 cycle 位匹配的事件,
 * 一旦 cycle 不匹配(队列已空)立即停止; 跨过段尾回绕时翻转消费者 cycle。
 * 严禁扫描整环——旧事件 cycle 位仍为 1, 会被当成新事件反复重处理。 */
static void xhci64_poll_events(void)
{
    if (!xhci_initialized) return;

    for (;;) {
        xhci_trb_t *trb = &xhci_evt_ring[xhci_evt_idx];
        uint32_t expected = xhci_evt_cycle ? 1u : 0u;
        if ((trb->field2 & 1u) != expected) break;   /* 队列空, 停止 */

        uint32_t type = (trb->field2 >> 10) & 0x3F;

        if (type == XHCI_TRB_EVT_CMD_COMP) {
            xhci_handle_cmd_completion(trb);
        } else if (type == XHCI_TRB_EVT_PORT_CHG) {
            xhci_handle_port_chg(trb);
        } else if (type == XHCI_TRB_EVT_TRANSFER) {
            xhci_handle_transfer_evt(trb);
        }
        /* 其它类型: 丢弃并继续 */

        /* 推进 dequeue, 跨段尾回绕并翻转消费者 cycle */
        xhci_evt_idx++;
        if (xhci_evt_idx == XHCI_EVT_RING_SZ) {
            xhci_evt_idx = 0;
            xhci_evt_cycle ^= 1;
        }

        /* 写 ERDP(新 dequeue 位置, 含当前 DCS bit0) */
        uint64_t erdp = (uint64_t)(uintptr_t)&xhci_evt_ring[xhci_evt_idx];
        erdp |= (xhci_evt_cycle ? 1u : 0u);
        xhci_w32_abs(xhci_rts_off + XHCI_ERDP, (uint32_t)(erdp & 0xFFFFFFFF));
        xhci_w32_abs(xhci_rts_off + XHCI_ERDP + 4, (uint32_t)(erdp >> 32));
    }

    /* 清中断 IP 位 (写 1 清零) */
    if (xhci_r32_abs(xhci_rts_off + XHCI_IMAN) & XHCI_IMAN_IP) {
        xhci_w32_abs(xhci_rts_off + XHCI_IMAN, XHCI_IMAN_IP);
    }
}

/* ---- 端口寄存器 ---- */
#define XHCI_PORTSC_BASE 0x400
#define XHCI_PORTSC(port) (XHCI_PORTSC_BASE + ((port) - 1) * 16)
#define XHCI_PS_CCS      (1u << 0)
#define XHCI_PS_PED      (1u << 1)
#define XHCI_PS_PR       (1u << 4)
#define XHCI_PS_SPEED(v) (((v) >> 10) & 0xF)
#define XHCI_PS_CSC      (1u << 17)
#define XHCI_PS_PEC      (1u << 18)

static int xhci_port_reset(uint32_t port)
{
    uint32_t ps = xhci_read32(XHCI_PORTSC(port));
    xhci_write32(XHCI_PORTSC(port), ps | XHCI_PS_PR);
    for (int w = 0; w < 3000000; w++) {
        ps = xhci_read32(XHCI_PORTSC(port));
        if (!(ps & XHCI_PS_PR)) break;
        for (volatile int d = 0; d < 300; d++);
    }
    return (ps & XHCI_PS_PED) ? 0 : -1;
}

/* ---- 控制传输完成标志 (供传输事件回调) ---- */
static int xhci_ctrl_ep0_done = 0;
static int xhci_ctrl_ep0_ok = 0;

/* 在环的生产者位置写一个 TRB; 0..ring_sz-2 为普通槽, ring_sz-1 为 LINK。
 * 生产者单调推进, 到环尾自动写 LINK(当前 cycle, TC) 回环首并翻转 cycle。
 * 同步传输每次等待完成, 环尾之后的环首槽保证已被控制器消费, 可安全复用。 */
static void xhci_ring_queue(xhci_trb_t *ring, uint32_t ring_sz,
                            uint32_t *index, uint32_t *cycle,
                            uint64_t f0, uint32_t f1, uint32_t type_flags)
{
    uint32_t i = *index;
    if (i == ring_sz - 1) {
        uint32_t lc = *cycle ? XHCI_TRB_CYCLE : 0;
        ring[i].field0 = (uint64_t)(uintptr_t)ring;
        ring[i].field1 = 0;
        ring[i].field2 = XHCI_TRB_TYPE(XHCI_TRB_LINK) | XHCI_TRB_TC | lc;
        i = 0;
        *cycle ^= 1;
    }
    uint32_t cyc = *cycle ? XHCI_TRB_CYCLE : 0;
    ring[i].field0 = f0;
    ring[i].field1 = f1;
    ring[i].field2 = type_flags | cyc;
    *index = i + 1;
}

/* ---- 控制传输 (EP0: Setup+Data+Status) ---- */
static int xhci_control_transfer(uint32_t slot, const uint8_t *setup8,
                                 uint8_t *data, uint16_t len, int dir_in)
{
    xhci_dev_t *d = &xhci_devs[slot];
    if (!d->state) return -1;

    /* SETUP TRB: field0 = 8 字节 Setup 包(立即数据), field1 = 长度 8,
     * field2 = SETUP | IDT(bit6) | TRT(bit16-17: 无数据=0, OUT=2, IN=3)。 */
    uint64_t sp = 0;
    for (int i = 0; i < 8; i++) sp |= ((uint64_t)setup8[i]) << (8 * i);
    uint32_t trt = (len == 0) ? 0u : (dir_in ? 3u : 2u);
    xhci_ring_queue(d->ep0_ring, XHCI_EP0_RING_SZ, &d->ep0_index, &d->ep0_cycle,
                    sp, 8,
                    XHCI_TRB_TYPE(XHCI_TRB_SETUP) | XHCI_TRB_IDT | (trt << 16));

    /* DATA TRB (仅当有数据): field0=数据缓冲, field1=长度, IN 置 DIR */
    if (len > 0 && data) {
        xhci_ring_queue(d->ep0_ring, XHCI_EP0_RING_SZ, &d->ep0_index,
                        &d->ep0_cycle, (uint64_t)(uintptr_t)data, len,
                        XHCI_TRB_TYPE(XHCI_TRB_DATA) |
                        (dir_in ? XHCI_TRB_DIR_IN : 0));
    }

    /* STATUS TRB: 方向与 DATA 相反 (IN 数据 -> 无 DIR; OUT/无数据 -> DIR 位) */
    xhci_ring_queue(d->ep0_ring, XHCI_EP0_RING_SZ, &d->ep0_index, &d->ep0_cycle,
                    0, 0,
                    XHCI_TRB_TYPE(XHCI_TRB_STATUS) | XHCI_TRB_IOC |
                    (dir_in ? 0 : XHCI_TRB_DIR_IN));

    xhci_ctrl_ep0_done = 0;
    xhci_ctrl_ep0_ok = 0;
    xhci_ring_doorbell(slot, XHCI_DCI_EP0);   /* EP0: 门铃寄存器 slot, 值=1 */

    for (int w = 0; w < 4000000 && !xhci_ctrl_ep0_done; w++) {
        xhci64_poll_events();
        if (xhci_ctrl_ep0_done) break;
        for (volatile int dd = 0; dd < 300; dd++);
    }
    return (xhci_ctrl_ep0_done && xhci_ctrl_ep0_ok) ? (int)len : -1;
}

static int xhci_get_desc(uint32_t slot, uint8_t type, uint8_t index,
                         uint16_t lang, uint8_t *buf, uint16_t len)
{
    uint8_t setup[8];
    setup[0] = 0x80; setup[1] = 6;
    /* GET_DESCRIPTOR 的 wValue: 高字节(byte3)=描述符类型, 低字节(byte2)=索引。
     * 早期写反成 byte2=type, 导致设备读到类型=0(无效)而 STALL。 */
    setup[2] = index; setup[3] = type;
    setup[4] = lang & 0xFF; setup[5] = lang >> 8;
    setup[6] = len & 0xFF; setup[7] = len >> 8;
    return xhci_control_transfer(slot, setup, buf, len, 1);
}

static int xhci_set_config(uint32_t slot, uint8_t cfg)
{
    uint8_t setup[8];
    setup[0] = 0x00; setup[1] = 9;
    setup[2] = cfg; setup[3] = 0;
    setup[4] = 0; setup[5] = 0;
    setup[6] = 0; setup[7] = 0;
    return xhci_control_transfer(slot, setup, NULL, 0, 0);
}

/* ---- 解析配置描述符: 枚举所有 HID 接口及其中断 IN 端点 (复合设备) ----
 * 描述符顺序: 接口(bType=4) 后紧跟其端点(bType=5)。每遇到一个 HID 接口,
 * 记录其 protocol; 随后的中断 IN 端点归属该接口, 建一个 int_eps 条目。
 * 无线接收器(键盘+鼠标+多媒体键)据此得到多个端点。 */
static void xhci_parse_config_desc(xhci_dev_t *d, uint8_t *cfg, uint32_t len)
{
    uint8_t cur_proto = 0;
    int cur_hid = 0;
    d->int_ep_count = 0;
    d->is_kbd = 0;
    d->is_mouse = 0;
    d->iface_class = 0;
    d->iface_proto = 0;

    uint32_t i = 0;
    while (i + 2 < len) {
        uint8_t bLen = cfg[i];
        uint8_t bType = cfg[i + 1];
        if (bLen == 0) break;

        if (bType == 4 && i + 9 <= len) {       /* INTERFACE */
            uint8_t cls = cfg[i + 5];
            uint8_t proto = cfg[i + 7];
            cur_hid = (cls == 3);               /* HID */
            cur_proto = proto;
            /* 记录首个 HID 接口类型 (用于设备级日志/兼容) */
            if (cur_hid && d->iface_class == 0) {
                d->iface_class = cls;
                d->iface_proto = proto;
            }
            if (cur_hid && proto == 1) d->is_kbd = 1;
            if (cur_hid && proto == 2) d->is_mouse = 1;
        } else if (bType == 5 && cur_hid && i + 7 <= len) {  /* ENDPOINT */
            uint8_t a = cfg[i + 2];
            uint8_t attr = cfg[i + 3];
            uint8_t interval = cfg[i + 6];
            uint16_t maxp = cfg[i + 4] | (cfg[i + 5] << 8);
            int is_in = (a & 0x80) != 0;
            int is_intr = ((attr & 0x03) == 3);
            uint8_t ep_num = a & 0x0F;
            if (is_in && is_intr && ep_num >= 1 && ep_num <= 15 &&
                d->int_ep_count < XHCI_MAX_INT_EPS) {
                /* 跳过已存在同 DCI 的端点(描述符重复) */
                uint8_t dci = (uint8_t)(2 * ep_num + 1);
                int dup = 0;
                for (uint32_t k = 0; k < d->int_ep_count; k++)
                    if (d->int_eps[k].active && d->int_eps[k].dci == dci) dup = 1;
                if (!dup) {
                    xhci_int_ep_t *e = &d->int_eps[d->int_ep_count++];
                    e->active = 1;
                    e->dci = dci;
                    e->ep_num = ep_num;
                    e->proto = cur_proto;
                    e->int_len = (maxp > 64) ? 64 : (maxp > 0 ? maxp : 8);
                    e->interval_exp = interval ? interval : 6;
                    e->ring = &xhci_int_ring_pool[d->slot_id][d->int_ep_count - 1][0];
                    e->ring_index = 0;
                    e->ring_cycle = 1;
                    e->buf = &xhci_int_buf_pool[d->slot_id][d->int_ep_count - 1][0];
                }
            }
        }
        i += bLen;
    }
}

/* 按 DCI 查找设备的中断端点 */
static xhci_int_ep_t *xhci_find_int_ep(xhci_dev_t *d, uint32_t dci)
{
    for (uint32_t i = 0; i < d->int_ep_count; i++)
        if (d->int_eps[i].active && d->int_eps[i].dci == dci)
            return &d->int_eps[i];
    return 0;
}

/* ---- 提交某个中断 IN 端点 (生产者索引, 环尾 LINK 自动回绕) ---- */
static void xhci_submit_int_ep(xhci_dev_t *d, xhci_int_ep_t *e)
{
    xhci_ring_queue(e->ring, XHCI_INT_RING_SZ, &e->ring_index, &e->ring_cycle,
                    (uint64_t)(uintptr_t)e->buf, e->int_len,
                    XHCI_TRB_TYPE(XHCI_TRB_NORMAL) | XHCI_TRB_IOC);
    xhci_ring_doorbell(d->slot_id, e->dci);
}

/* ---- 传输事件: EP0 完成 / 各中断 IN 端点数据 ---- */
static void xhci_handle_transfer_evt(xhci_trb_t *evt)
{
    uint32_t slot = (evt->field2 >> 24) & 0xFF;
    uint32_t ep_id = (evt->field2 >> 16) & 0xFF;
    uint32_t cc = (evt->field1 >> 24) & 0xFF;
    if (slot >= XHCI_MAX_SLOTS) return;
    xhci_dev_t *d = &xhci_devs[slot];
    if (!d->state) return;

    if (ep_id == 1) {   /* EP0 (control), DCI=1 */
        xhci_ctrl_ep0_done = 1;
        xhci_ctrl_ep0_ok = (cc == XHCI_CC_SUCCESS);
        return;
    }

    /* 其余: 中断 IN 端点 (奇数 DCI), 按 DCI 找到具体端点 */
    if (ep_id & 1) {
        xhci_int_ep_t *e = xhci_find_int_ep(d, ep_id);
        if (e && e->active) {
            if (cc == XHCI_CC_SUCCESS) {
                uint8_t *r = e->buf;
                if (e->proto == 2 && e->int_len >= 3) {   /* 鼠标 */
                    d->mouse_btn = r[0] & 0x07;
                    d->mouse_dx = (int8_t)r[1];
                    d->mouse_dy = (int8_t)r[2];
                } else if (e->proto == 1 && e->int_len >= 8) { /* 键盘 */
                    for (int i = 0; i < 8; i++) d->kbd_report[i] = r[i];
                    d->kbd_new = 1;
                }
            }
            xhci_submit_int_ep(d, e);   /* 成功/错误都重提交, 保持持续轮询 */
        }
    }
}

/* ---- 配置端点 (Address Device 后) ---- */
static int xhci_configure_ep(uint32_t slot, uint8_t maxp0, uint32_t speed,
                             uint32_t port)
{
    xhci_dev_t *d = &xhci_devs[slot];
    xhci_in_ctx_t *in = d->in_ctx;
    (void)maxp0;   /* EP0 已在地址阶段配置, 此处不再使用 */

    xhci_memzero(in, sizeof(*in));
    /* Add: bit0=Slot, 每个中断端点置 bit(DCI); bit1(EP0) 必须为 0(地址阶段已
     * 使能, QEMU 要求 add 低 2 位恰为 0x1)。Drop 全 0。 */
    in->drop_flags = 0;
    in->add_flags = 0x01u;
    uint32_t highest_dci = 1;
    for (uint32_t i = 0; i < d->int_ep_count; i++) {
        xhci_int_ep_t *e = &d->int_eps[i];
        in->add_flags |= (1u << e->dci);
        if (e->dci > highest_dci) highest_dci = e->dci;
    }

    xhci_slot_ctx_t *sc = &in->slot;
    sc->a = (speed << 20) | (highest_dci << 27); /* Context Entries = 最高 DCI */
    sc->b = (port << 16);
    sc->c = 0;                                   /* Interrupter 0 */

    /* 每个中断端点的 EP 上下文 (输入上下文 index = DCI, 即 ep[DCI-1])。
     * ctx[0]=bInterval(bits16-23); ctx[1]=CErr|EP type|MaxPacket;
     * ctx[2/3]=TR 出队指针 (bit0=DCS); ctx[4]=平均 TRB 长度。 */
    for (uint32_t i = 0; i < d->int_ep_count; i++) {
        xhci_int_ep_t *e = &d->int_eps[i];
        xhci_ep_ctx_t *ec = &in->ep[e->dci - 1];
        uint64_t ring_addr = (uint64_t)(uintptr_t)e->ring;
        ec->a = ((uint32_t)e->interval_exp << 16);
        ec->b = (3u << 1) | (XHCI_EPT_INTR_IN << 3) | ((uint32_t)e->int_len << 16);
        ec->c = (uint32_t)(ring_addr & 0xFFFFFFFFu) | 1u; /* DCS=1 */
        ec->d = (uint32_t)(ring_addr >> 32);
        ec->e = (uint32_t)e->int_len << 16;
    }

    xhci_trb_t cmd;
    xhci_memzero(&cmd, sizeof(cmd));
    cmd.field0 = (uint64_t)(uintptr_t)in;
    cmd.field2 = XHCI_TRB_TYPE(XHCI_TRB_CONFIGURE_EP) | XHCI_TRB_SLOT_ID(slot);
    uint32_t cc = 0;
    if (xhci_submit_cmd(&cmd, &cc) != 0 || cc != XHCI_CC_SUCCESS) {
        vga_puts("[XHCI] ConfigEP fail cc=");
        vga_put_hex(cc);
        vga_puts("\n");
        return -1;
    }

    /* 输出(设备)上下文由 QEMU 在 Configure 时根据输入上下文自行写入,
     * 软件无需也不应手动拷贝。 */

    d->state = 2;
    /* 各中断环复位: 清空 + 生产者 index=0 + cycle=1(匹配 EP 上下文 DCS=1),
     * 随后逐个提交首个 NORMAL TRB 开始持续轮询。 */
    for (uint32_t i = 0; i < d->int_ep_count; i++) {
        xhci_int_ep_t *e = &d->int_eps[i];
        xhci_memzero(e->ring, sizeof(xhci_trb_t) * XHCI_INT_RING_SZ);
        e->ring_index = 0;
        e->ring_cycle = 1;
        xhci_submit_int_ep(d, e);
    }
    return 0;
}

/* ---- 构造 Address Device 输入上下文 (两次寻址共用) ----
 * 对照 QEMU xhci_address_slot 校验: ictl[0](Drop)=0, ictl[1](Add)=0x3
 * (Slot + EP0); Slot 上下文 entries=1; EP0 上下文 type=4, DCS=1。 */
static void xhci_build_address_ctx(uint32_t slot, uint8_t maxp0,
                                   uint32_t speed, uint32_t port)
{
    xhci_dev_t *d = &xhci_devs[slot];
    xhci_in_ctx_t *in = d->in_ctx;
    xhci_memzero(in, sizeof(*in));

    /* Address Device 会把 EP0 出队指针重置到 ring[0] DCS=1,
     * 软件生产者状态必须同步复位(二次寻址时尤为重要, 此时环已前进)。 */
    xhci_memzero(d->ep0_ring, sizeof(xhci_trb_t) * XHCI_EP0_RING_SZ);
    d->ep0_index = 0;
    d->ep0_cycle = 1;

    in->drop_flags = 0;
    in->add_flags = 0x03u;             /* bit0 Slot + bit1 EP0 */

    xhci_slot_ctx_t *sc = &in->slot;
    sc->a = (speed << 20) | (1u << 27);   /* Context Entries = 1 */
    sc->b = (port << 16);
    sc->c = 0;                            /* Interrupter 0 */

    uint64_t ring = (uint64_t)(uintptr_t)d->ep0_ring;
    xhci_ep_ctx_t *ep = &in->ep[0];     /* index 1 (EP0) = ep[0] */
    ep->a = 0;                                                 /* bInterval 0 */
    ep->b = (3u << 1) | (XHCI_EPT_CTRL << 3) | (maxp0 << 16);
    ep->c = (uint32_t)(ring & 0xFFFFFFFFu) | 1u;              /* DCS=1 */
    ep->d = (uint32_t)(ring >> 32);
    ep->e = maxp0 << 16;                                       /* Avg TRB Length */
}

/* ---- 提交 Address Device 命令 ---- */
static int xhci_send_address(uint32_t slot, uint32_t *cc)
{
    xhci_dev_t *d = &xhci_devs[slot];
    xhci_trb_t cmd;
    xhci_memzero(&cmd, sizeof(cmd));
    cmd.field0 = (uint64_t)(uintptr_t)d->in_ctx;
    cmd.field2 = XHCI_TRB_TYPE(XHCI_TRB_ADDRESS_DEV) | XHCI_TRB_SLOT_ID(slot);
    return xhci_submit_cmd(&cmd, cc);
}

/* ---- 设备初始化: Address + Descriptors + Config ---- */
static void xhci_device_init(uint32_t slot, uint32_t port, uint32_t speed)
{
    xhci_dev_t *d = &xhci_devs[slot];
    /* Address Device #1 (maxp0=64 猜测) */
    xhci_build_address_ctx(slot, 64, speed, port);
    uint32_t cc = 0;
    if (xhci_send_address(slot, &cc) != 0 || cc != XHCI_CC_SUCCESS) {
        vga_puts("[XHCI] Address fail cc=");
        vga_put_hex(cc);
        vga_puts("\n");
        return;
    }
    xhci_devs[slot].state = 1;

    /* 设备描述符 -> maxp0 */
    uint8_t buf[8];
    xhci_memzero(buf, 8);
    if (xhci_get_desc(slot, 1, 0, 0, buf, 8) < 0) {
        vga_puts("[XHCI] GetDesc fail\n");
        return;
    }
    uint8_t maxp0 = buf[7] ? buf[7] : 64;

    if (maxp0 != 64) {
        /* 二次寻址更新 maxp0 */
        xhci_build_address_ctx(slot, maxp0, speed, port);
        if (xhci_send_address(slot, &cc) != 0 || cc != XHCI_CC_SUCCESS) return;
    }

    if (xhci_set_config(slot, 1) < 0) {
        vga_puts("[XHCI] SetConfig fail\n");
        return;
    }

    /* 配置描述符: 头部 9 字节 -> total */
    uint8_t cfg_hdr[9];
    xhci_memzero(cfg_hdr, 9);
    if (xhci_get_desc(slot, 2, 0, 0, cfg_hdr, 9) < 0) return;
    uint16_t total = cfg_hdr[2] | (cfg_hdr[3] << 8);
    if (total < 9 || total > 256) return;
    uint8_t *cfg = (uint8_t *)usb64_dma_alloc(total);
    if (!cfg) return;
    if (xhci_get_desc(slot, 2, 0, 0, cfg, total) < 0) return;

    xhci_parse_config_desc(d, cfg, total);

    /* VID/PID */
    uint8_t dd18[18];
    if (xhci_get_desc(slot, 1, 0, 0, dd18, 18) >= 0) {
        d->vid = dd18[8] | (dd18[9] << 8);
        d->pid = dd18[10] | (dd18[11] << 8);
    }

    vga_puts("[XHCI] Slot ");
    vga_put_hex(slot);
    vga_puts(" vid=");
    vga_put_hex(d->vid);
    vga_puts(" pid=");
    vga_put_hex(d->pid);
    vga_puts(" class=");
    vga_put_hex(d->iface_class);
    vga_puts(" proto=");
    vga_put_hex(d->iface_proto);
    vga_puts("\n");

    if (d->is_kbd || d->is_mouse) {
        if (xhci_configure_ep(slot, maxp0, speed, port) == 0) {
            vga_puts("[XHCI] HID ready (slot ");
            vga_put_hex(slot);
            vga_puts(") ");
            if (d->is_mouse) vga_puts("mouse");
            if (d->is_mouse && d->is_kbd) vga_puts("+");
            if (d->is_kbd) vga_puts("keyboard");
            vga_puts(" ep=");
            vga_put_hex((uint8_t)d->int_ep_count);
            vga_puts("\n");
        }
    }
}

/* ---- 端口探测: 复位 + Enable Slot + 设备初始化 ---- */
static void xhci_port_probe(uint32_t port_id)
{
    uint32_t ps = xhci_read32(XHCI_PORTSC(port_id));
    if (!(ps & XHCI_PS_CCS)) return;   /* 无设备 */
    /* 幂等: 端口已有已枚举设备则不重复复位/寻址 */
    for (int i = 1; i < XHCI_MAX_SLOTS; i++) {
        if (xhci_devs[i].state && xhci_devs[i].port == port_id) return;
    }

    /* 清 CSC/事件位 */
    xhci_write32(XHCI_PORTSC(port_id), ps | 0x1E00u);

    if (xhci_port_reset(port_id) != 0) return;
    ps = xhci_read32(XHCI_PORTSC(port_id));
    uint32_t speed = XHCI_PS_SPEED(ps);
    vga_puts("[XHCI] Port ");
    vga_put_hex(port_id);
    vga_puts(" enabled\n");

    /* Enable Slot */
    xhci_trb_t cmd;
    xhci_memzero(&cmd, sizeof(cmd));
    cmd.field2 = XHCI_TRB_TYPE(XHCI_TRB_ENABLE_SLOT);
    uint32_t cc = 0;
    if (xhci_submit_cmd(&cmd, &cc) != 0 || cc != XHCI_CC_SUCCESS) {
        vga_puts("[XHCI] EnableSlot fail cc=");
        vga_put_hex(cc);
        vga_puts("\n");
        return;
    }
    uint32_t slot = xhci_evt_slot_id;
    if (slot == 0 || slot >= XHCI_MAX_SLOTS) return;

    xhci_dev_t *d = &xhci_devs[slot];
    xhci_memzero(d, sizeof(*d));
    d->slot_id = slot;
    d->port = port_id;
    d->speed = speed;
    d->dev_ctx = &xhci_dev_ctx_pool[slot];
    d->in_ctx = &xhci_in_ctx_pool[slot];
    d->ep0_ring = &xhci_ep0_ring_pool[slot][0];
    /* EP0 环复位: 清空 + 生产者 index=0 + cycle=1(匹配地址阶段 EP0 上下文 DCS=1)。 */
    xhci_memzero(d->ep0_ring, sizeof(xhci_trb_t) * XHCI_EP0_RING_SZ);
    d->ep0_index = 0;
    d->ep0_cycle = 1;
    /* 中断端点在 parse_config_desc 里发现时才分配环/缓冲并绑定指针 */
    d->int_ep_count = 0;
    for (int k = 0; k < XHCI_MAX_INT_EPS; k++) {
        xhci_memzero(&d->int_eps[k], sizeof(d->int_eps[k]));
        xhci_memzero(&xhci_int_ring_pool[slot][k][0],
                     sizeof(xhci_trb_t) * XHCI_INT_RING_SZ);
    }
    xhci_memzero(d->dev_ctx, sizeof(xhci_dev_ctx_t));
    xhci_dcbaap[slot] = (uint64_t)(uintptr_t)d->dev_ctx;
    xhci_device_init(slot, port_id, speed);
}

/* ---- 端口变化处理: 只标记, 由 xhci64_poll 顶层处理(避免事件内递归提交命令) ---- */
static void xhci_handle_port_chg(xhci_trb_t *evt)
{
    uint32_t port_id = (evt->field0 >> 24) & 0xFF;
    if (port_id == 0 || port_id > xhci_max_ports) return;
    /* 已枚举设备: 忽略重复端口事件, 防止复位风暴打断枚举 */
    for (int i = 1; i < XHCI_MAX_SLOTS; i++) {
        if (xhci_devs[i].state && xhci_devs[i].port == port_id) return;
    }
    xhci_pending_port = port_id;
}

/* ---- 初始化 ---- */
int xhci64_init(void)
{
    if (xhci_initialized) return 0;

    uint32_t bar0 = pci_find_class_bar0(0x0C, 0x03, 0x30);
    if (!bar0) {
        vga_puts("[XHCI] Controller not found\n");
        return -1;
    }
    xhci_mmio_phys = bar0 & 0xFFFFFFF0;

    extern int vmm64_map_page(uint64_t pml4_addr, uint64_t virt, uint64_t phys, uint64_t flags);
    extern uint64_t vmm64_get_pml4(void);
    uint64_t cr3 = vmm64_get_pml4();
    for (uint64_t off = 0; off < 0x1000000; off += 0x1000) {
        vmm64_map_page(cr3, xhci_mmio_phys + off, xhci_mmio_phys + off, 0x03 | 0x10);
    }
    xhci_op = (volatile uint32_t *)(uintptr_t)xhci_mmio_phys;
    xhci_mmio_abs = (volatile uint32_t *)(uintptr_t)xhci_mmio_phys;

    uint32_t caplen = xhci_read32(XHCI_CAPLENGTH) & 0xFF;
    uint32_t hcs1 = xhci_read32(XHCI_HCSPARAMS1);
    uint32_t hcc1 = xhci_read32(XHCI_HCCPARAMS1);
    xhci_dboff = xhci_read32(XHCI_DBOFF) & ~3u;
    xhci_rts_off = xhci_read32(XHCI_RTSOFF) & ~3u;
    xhci_max_slots = XHCI_HCS1_MAXSLOTS(hcs1);
    xhci_max_ports = XHCI_HCS1_MAXPORTS(hcs1);
    if (xhci_max_slots > XHCI_MAX_SLOTS) xhci_max_slots = XHCI_MAX_SLOTS;

    vga_puts("[XHCI] caplen=");
    vga_put_hex(caplen);
    vga_puts(" dboff=");
    vga_put_hex(xhci_dboff);
    vga_puts(" rts=");
    vga_put_hex(xhci_rts_off);
    vga_puts("\n");
    vga_puts("[XHCI] MMIO ");
    vga_put_hex(xhci_mmio_phys);
    vga_puts(" slots=");
    vga_put_hex(xhci_max_slots);
    vga_puts(" ports=");
    vga_put_hex(xhci_max_ports);
    vga_puts(" AC64=");
    vga_put_hex((hcc1 & XHCI_HCC_AC64) ? 1 : 0);
    vga_puts("\n");

    xhci_op = (volatile uint32_t *)((uintptr_t)xhci_mmio_phys + caplen);

    /* 控制器复位 */
    xhci_write32(XHCI_USBCMD, XHCI_CMD_HCRST);
    for (int w = 0; w < 1000000; w++) {
        if (!(xhci_read32(XHCI_USBCMD) & XHCI_CMD_HCRST)) break;
        for (volatile int d = 0; d < 100; d++);
    }
    for (int w = 0; w < 1000000; w++) {
        if (xhci_read32(XHCI_USBSTS) & XHCI_STS_HCH) break;
        for (volatile int d = 0; d < 100; d++);
    }

    for (uint32_t i = 0; i < XHCI_MAX_SLOTS; i++) {
        xhci_dcbaap[i] = 0;
        xhci_memzero(&xhci_devs[i], sizeof(xhci_dev_t));
    }
    xhci_write32(XHCI_DCBAAP, (uint32_t)(uintptr_t)xhci_dcbaap);
    xhci_write32(XHCI_DCBAAP + 4, (uint32_t)((uint64_t)(uintptr_t)xhci_dcbaap >> 32));

    for (int i = 0; i < XHCI_CMD_RING_SZ; i++) xhci_memzero(&xhci_cmd_ring[i], sizeof(xhci_trb_t));
    xhci_cmd_idx = 0;
    xhci_cmd_cycle = 1;

    for (int i = 0; i < XHCI_EVT_RING_SZ; i++) xhci_memzero(&xhci_evt_ring[i], sizeof(xhci_trb_t));
    xhci_evt_idx = 0;
    xhci_evt_cycle = 1;
    xhci_erst[0].addr = (uint64_t)(uintptr_t)xhci_evt_ring;
    xhci_erst[0].size = XHCI_EVT_RING_SZ;

    xhci_write32(XHCI_CRCR, (uint32_t)(uintptr_t)xhci_cmd_ring | 1);
    xhci_write32(XHCI_CRCR + 4, 0);

    xhci_write32(XHCI_USBCMD, XHCI_CMD_RS | XHCI_CMD_INTE);
    for (int w = 0; w < 1000000; w++) {
        if (!(xhci_read32(XHCI_USBSTS) & XHCI_STS_HCH)) break;
        for (volatile int d = 0; d < 100; d++);
    }

    /* 中断器 0 */
    xhci_w32_abs(xhci_rts_off + XHCI_ERSTSZ, 1);
    xhci_w32_abs(xhci_rts_off + XHCI_ERSTBA, (uint32_t)(uintptr_t)xhci_erst);
    xhci_w32_abs(xhci_rts_off + XHCI_ERSTBA + 4, 0);
    xhci_w32_abs(xhci_rts_off + XHCI_ERDP, (uint32_t)(uintptr_t)xhci_evt_ring);
    xhci_w32_abs(xhci_rts_off + XHCI_ERDP + 4, 0);
    xhci_w32_abs(xhci_rts_off + XHCI_IMAN, XHCI_IMAN_IE | XHCI_IMAN_IP);

    xhci_write32(XHCI_CONFIG, xhci_max_slots);

    /* 重置事件环写指针: QEMU 在复位后(事件环未配置时)已把端口事件写到物理0x0,
     * 写指针推进到 idx1; 重写 ERSTBA 触发控制器重读 ERST, 写指针回 idx0 */
    xhci_w32_abs(xhci_rts_off + XHCI_ERSTSZ, 0);
    xhci_w32_abs(xhci_rts_off + XHCI_ERSTSZ, 1);
    xhci_w32_abs(xhci_rts_off + XHCI_ERSTBA, (uint32_t)(uintptr_t)xhci_erst);
    xhci_w32_abs(xhci_rts_off + XHCI_ERSTBA + 4, 0);
    xhci_w32_abs(xhci_rts_off + XHCI_ERDP, (uint32_t)(uintptr_t)xhci_evt_ring);
    xhci_w32_abs(xhci_rts_off + XHCI_ERDP + 4, 0);

    xhci_initialized = 1;
    xhci_present_flag = 1;
    vga_puts("[XHCI] ADDR cmd=");
    vga_put_hex((uint64_t)(uintptr_t)xhci_cmd_ring);
    vga_puts(" evt=");
    vga_put_hex((uint64_t)(uintptr_t)xhci_evt_ring);
    vga_puts(" erst=");
    vga_put_hex((uint64_t)(uintptr_t)xhci_erst);
    vga_puts(" dcbaap=");
    vga_put_hex((uint64_t)(uintptr_t)xhci_dcbaap);
    vga_puts("\n");
    vga_puts("[OK] XHCI initialized\n");

    /* 主动扫描已有端口 (QEMU 设备插入事件在事件环配置前已发生) */
    for (uint32_t p = 1; p <= xhci_max_ports; p++) {
        xhci_port_probe(p);
    }
    return 0;
}

int xhci64_present(void) { return xhci_present_flag; }

void xhci64_poll(void)
{
    if (!xhci_initialized) return;
    xhci64_poll_events();
    /* 事件循环外处理待探测端口, 避免命令重入 */
    while (xhci_pending_port) {
        uint32_t p = xhci_pending_port;
        xhci_pending_port = 0;
        xhci_port_probe(p);
        xhci64_poll_events();
    }
}

/* ---- 鼠标读取 ---- */
int xhci64_mouse_available(void)
{
    if (!xhci_initialized) return 0;
    for (int i = 1; i < (int)XHCI_MAX_SLOTS; i++)
        if (xhci_devs[i].is_mouse && xhci_devs[i].state == 2) return 1;
    return 0;
}

void xhci64_mouse_get(int *x, int *y, int *left, int *right)
{
    int dx = 0, dy = 0, lb = 0, rb = 0;
    for (int i = 1; i < (int)XHCI_MAX_SLOTS; i++) {
        if (xhci_devs[i].is_mouse && xhci_devs[i].state == 2) {
            dx += (int)xhci_devs[i].mouse_dx;
            dy += (int)xhci_devs[i].mouse_dy;
            lb |= (xhci_devs[i].mouse_btn & 1) ? 1 : 0;
            rb |= (xhci_devs[i].mouse_btn & 2) ? 1 : 0;
            xhci_devs[i].mouse_dx = 0;
            xhci_devs[i].mouse_dy = 0;
        }
    }
    if (x) *x = dx;
    if (y) *y = dy;
    if (left) *left = lb;
    if (right) *right = rb;
}

/* ---- 键盘读取 ---- */
int xhci64_kbd_available(void)
{
    if (!xhci_initialized) return 0;
    for (int i = 1; i < (int)XHCI_MAX_SLOTS; i++)
        if (xhci_devs[i].is_kbd && xhci_devs[i].state == 2) return 1;
    return 0;
}

void xhci64_kbd_flush(void)
{
    if (!xhci_initialized) return;
    for (int i = 1; i < (int)XHCI_MAX_SLOTS; i++) {
        xhci_dev_t *d = &xhci_devs[i];
        if (d->is_kbd && d->state == 2 && d->kbd_new) {
            d->kbd_new = 0;
            extern void usb64_kbd_report_inject(uint8_t *report, uint8_t len);
            usb64_kbd_report_inject(d->kbd_report, 8);
        }
    }
}
