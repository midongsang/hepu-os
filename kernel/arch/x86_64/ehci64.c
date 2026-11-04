/* ============================================================================
 * 赫普OS 64位 - EHCI (USB 2.0) 主机控制器驱动
 * V0.13.64: 现代主板 USB2.0 控制器支持 (真机无 UHCI 时回退路径)
 * 结构: 寄存器(MMIO BAR0) + 周期帧列表 + 异步列表(QH/qTD) + 控制传输
 * 注意: QEMU 7.2 usb-ehci 异步列表不执行(模拟器限制, 见 REAL_HARDWARE_TEST.md)
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include "paging.h"

/* ---- EHCI 寄存器 (MMIO, 基址 = PCI BAR0) ---- */
#define EHCI_CAPLENGTH      0x00  /* byte: 寄存器长度 */
#define EHCI_HCSPARAMS      0x04  /* 结构参数 */
#define EHCI_HCCPARAMS      0x08  /* 能力参数 (bit0=64位寻址) */
/* V0.13.64: operational 寄存器为 caplen 相对偏移 (Intel EHCI 规范) */
#define EHCI_USBCMD         0x00  /* 命令 */
#define EHCI_USBSTS         0x04  /* 状态 */
#define EHCI_USBINTR        0x08  /* 中断使能 */
#define EHCI_FRINDEX        0x0C  /* 帧索引 */
#define EHCI_CTRLDSSEG      0x10  /* 控制段 (64位) */
#define EHCI_PERIODICLIST   0x14  /* 周期列表基址 */
#define EHCI_ASYNCLIST      0x18  /* 异步列表基址 */
/* V0.13.64 实测(QEMU): CONFIGFLAG 相对 0x20(绝对0x40), PORTSC 相对 0x44(绝对0x64) */
#define EHCI_CONFIGFLAG     0x20  /* 配置标志 */
#define EHCI_PORTSC         0x44  /* 端口状态 (每端口4字节, 实测偏移) */

/* USBCMD 位 */
#define EHCI_CMD_RUN        0x0001
#define EHCI_CMD_HCRESET    0x0002
#define EHCI_CMD_ASEN       0x0010  /* 异步调度使能 */
#define EHCI_CMD_PSEN       0x0020  /* 周期调度使能 */

/* USBSTS 位 */
#define EHCI_STS_HCHALTED   0x1000
#define EHCI_STS_PCD        0x0001  /* 端口连接变化 */

/* PORTSC 位 */
#define EHCI_PORT_CCS       0x0001  /* 当前连接状态 */
#define EHCI_PORT_PED       0x0002  /* 端口使能 */
#define EHCI_PORT_OCA       0x0008
#define EHCI_PORT_RESET     0x0100  /* 端口复位 */
#define EHCI_PORT_ENABLED   0x0200  /* 端口使能状态(读回) */
#define EHCI_PORT_LS        0x0C00  /* 速度 (bit11-10) */

/* ---- QH / qTD 结构 (EHCI 规范) ---- */
typedef struct __attribute__((packed)) {
    uint32_t dword[16];   /* 32字节 QH 头 + 8 dword 覆盖区 (共48字节, 留余量到64) */
} ehci_qh_t;

typedef struct __attribute__((packed)) {
    uint32_t next_qtd;    /* 下一 qTD 指针 */
    uint32_t token;       /* 令牌 */
    uint32_t buf[5];      /* 缓冲区指针 (最多5个, 页对齐切分) */
    uint32_t reserved;
} ehci_qtd_t;

/* ---- 全局状态 ---- */
static uint32_t ehci_mmio_base = 0;      /* BAR0 物理地址 */
static uint8_t  *ehci_regs = 0;          /* 映射后寄存器指针 (1:1) */
static int      ehci_initialized = 0;
static int      ehci_ports = 0;
static int      ehci_64bit = 0;          /* 64位寻址EHCI */

/* 异步列表 QH 池 (控制传输) */
#define EHCI_MAX_ASYNC_QH 8
static ehci_qh_t *ehci_async_qh[EHCI_MAX_ASYNC_QH];
static ehci_qtd_t *ehci_ctrl_qtd[EHCI_MAX_ASYNC_QH][3]; /* SETUP/DATA/STATUS */

/* 周期列表: 1024 帧 */
static uint32_t *ehci_periodic_frames = 0;

/* 中断端点 QH 池 */
#define EHCI_MAX_INT_QH 8
typedef struct {
    int         in_use;
    uint8_t     dev_addr;
    uint8_t     ep;
    uint16_t    maxlen;
    uint8_t    *buf;
    ehci_qh_t  *qh;
    ehci_qtd_t *qtd;
    int         last_complete;   /* 上次完成标志 */
} ehci_int_ep_t;
static ehci_int_ep_t ehci_int_eps[EHCI_MAX_INT_QH];
static int ehci_int_count = 0;

/* ---- DMA 辅助 (复用 usb64.c 的分配器) ---- */
extern void *usb64_dma_alloc(int size);
extern void vga_puts(const char *s);
extern void vga_put_hex(uint64_t v);
extern uint32_t pci_find_class_bar0(uint8_t base, uint8_t sub, uint8_t prog_if);
static inline uint32_t e_phys(void *p) { return (uint32_t)(uintptr_t)p; }

static inline uint32_t ehci_rd(uint32_t off)
{
    return *(volatile uint32_t *)((uint8_t *)ehci_regs + off);
}
static inline void ehci_wr(uint32_t off, uint32_t val)
{
    *(volatile uint32_t *)((uint8_t *)ehci_regs + off) = val;
}

/* ---- 中断传输端点注册 (供 usb64.c 使用) ---- */
int ehci64_int_ep_register(uint8_t dev_addr, uint8_t ep, uint16_t maxlen,
                           uint8_t *buf)
{
    if (!ehci_initialized) return -1;
    if (ehci_int_count >= EHCI_MAX_INT_QH) return -1;

    /* 分配 QH + qTD (必须32字节对齐, usb_dma_alloc 返回页对齐) */
    ehci_qh_t *qh = (ehci_qh_t *)usb64_dma_alloc(128);
    ehci_qtd_t *qtd = (ehci_qtd_t *)usb64_dma_alloc(128);
    if (!qh || !qtd) return -1;

    /* QH 端点特征: 中断(bit0=1) | dev_addr | ep | max_packet */
    qh->dword[0] = (1 << 0) | ((uint32_t)dev_addr << 8) |
                   ((uint32_t)(ep & 0x0F) << 16) | ((uint32_t)maxlen << 22);
    /* 能力: 调度掩码 S-mask=0x01 (每帧轮询), C-mask=0 */
    qh->dword[1] = (0x01 << 0);
    qh->dword[2] = 0;   /* 当前 qTD */
    qh->dword[3] = 1;   /* 下一个 QH = 终止 */

    /* qTD: 中断入传输, 总字节 maxlen, toggle=0, 缓冲区 */
    qtd->next_qtd = 1;  /* 终止 */
    qtd->token = 0x80000000 | ((uint32_t)maxlen << 16) | 0x80 | (3 << 12);
    uint32_t phys = e_phys(buf);
    qtd->buf[0] = phys;
    for (int i = 1; i < 5; i++) qtd->buf[i] = 0;

    qh->dword[2] = e_phys(qtd);

    ehci_int_eps[ehci_int_count].in_use = 1;
    ehci_int_eps[ehci_int_count].dev_addr = dev_addr;
    ehci_int_eps[ehci_int_count].ep = ep;
    ehci_int_eps[ehci_int_count].maxlen = maxlen;
    ehci_int_eps[ehci_int_count].buf = buf;
    ehci_int_eps[ehci_int_count].qh = qh;
    ehci_int_eps[ehci_int_count].qtd = qtd;
    ehci_int_eps[ehci_int_count].last_complete = 0;

    /* 挂到所有 1024 帧 */
    uint32_t qh_ptr = e_phys(qh) | 2;  /* QH 类型 bit1 */
    for (int i = 0; i < 1024; i++) ehci_periodic_frames[i] = qh_ptr;
    ehci_int_count++;
    return 0;
}

/* ---- 中断传输轮询 (返回 1=有完成, 0=无) ---- */
int ehci64_int_ep_poll(int idx)
{
    if (!ehci_initialized || idx < 0 || idx >= ehci_int_count) return 0;
    ehci_int_ep_t *e = &ehci_int_eps[idx];
    uint32_t t = e->qtd->token;
    if (t & 0x80000000) return 0;   /* ACTIVE */
    if (t & 0x40000000) {            /* HALTED: 复位后重新激活 */
        e->qtd->token = 0x80000000 | ((uint32_t)e->maxlen << 16) | 0x80 | (3 << 12);
        return 0;
    }
    /* 完成: 重新激活 qTD 继续轮询 */
    e->qtd->token = 0x80000000 | ((uint32_t)e->maxlen << 16) | 0x80 | (3 << 12);
    return 1;
}

/* ---- 控制传输 (异步调度) ----
 * 执行 SETUP + DATA + STATUS, 复用 usb64.c 的请求构造层
 * setup[8] 已含请求; data=数据缓冲; dir: 0=OUT, 1=IN */
int ehci64_control_transfer(uint8_t dev_addr, const uint8_t *setup,
                            uint8_t *data, uint16_t len, int dir)
{
    if (!ehci_initialized) return -1;

    /* 找空闲异步 QH 槽 */
    int slot = -1;
    for (int i = 0; i < EHCI_MAX_ASYNC_QH; i++) {
        if (ehci_async_qh[i] == 0) { slot = i; break; }
    }
    if (slot < 0) return -1;

    ehci_qh_t *qh = (ehci_qh_t *)usb64_dma_alloc(128);
    if (!qh) return -1;
    ehci_async_qh[slot] = qh;

    /* QH: 普通批量型 (bit31=0, 控制传输 qTD 链), 设备地址 bit7-0, max_packet=64 */
    qh->dword[0] = ((uint32_t)dev_addr & 0x7F) | ((uint32_t)64 << 16);
    qh->dword[1] = 0;      /* 控制: S-mask=0, C-mask=0x1C 在 qTD 完成时处理 */
    qh->dword[2] = 0;
    qh->dword[3] = 1;      /* 链表尾 */

    /* 构建 3 个 qTD: SETUP(8B OUT) -> DATA(len, dir) -> STATUS(0B 反方向) */
    ehci_qtd_t *qtds[3];
    for (int i = 0; i < 3; i++) {
        qtds[i] = (ehci_qtd_t *)usb64_dma_alloc(128);
        if (!qtds[i]) return -1;
        for (int j = 0; j < 5; j++) qtds[i]->buf[j] = 0;
    }
    ehci_ctrl_qtd[slot][0] = qtds[0];
    ehci_ctrl_qtd[slot][1] = qtds[1];
    ehci_ctrl_qtd[slot][2] = qtds[2];

    /* SETUP: 出方向, 8字节, toggle=0 */
    qtds[0]->next_qtd = e_phys(qtds[1]);
    qtds[0]->token = 0x80000000 | ((uint32_t)8 << 16) | (3 << 12);
    qtds[0]->buf[0] = e_phys((void *)setup);

    /* DATA */
    if (len > 0 && data) {
        qtds[1]->next_qtd = e_phys(qtds[2]);
        qtds[1]->token = 0x80000000 | ((uint32_t)len << 16) | (dir ? 0x80 : 0) | (3 << 12);
        qtds[1]->buf[0] = e_phys(data);
    } else {
        /* 无 DATA 阶段: SETUP 直连 STATUS */
        qtds[0]->next_qtd = e_phys(qtds[2]);
        qtds[1]->next_qtd = 1;   /* 占位终止 */
        qtds[1]->token = 0x40000000;  /* HALTED 不可执行 */
    }

    /* STATUS: ACTIVE, 反方向, 0字节, CERR=3 */
    int st_dir = (dir == 1) ? 0 : 1;
    qtds[2]->next_qtd = 1;
    qtds[2]->token = 0x80000000 | ((uint32_t)0 << 16) | (st_dir ? 0x80 : 0) | (3 << 12);

    /* 挂载 QH 到异步列表头, 指向第一个 qTD */
    qh->dword[2] = e_phys(qtds[0]);
    uint32_t prev = ehci_rd(EHCI_ASYNCLIST);
    if (prev & 1) prev = 0;   /* 若原列表终止则直接替换 */
    qh->dword[3] = prev ? prev : 1;
    /* V0.13.64: QEMU 运行时拒绝写 ASYNCLISTADDR; 停止控制器 -> 写 -> 重启 */
    {
        uint8_t cl = *(volatile uint8_t *)ehci_regs;
        uint32_t cur_cmd = ehci_rd(cl + EHCI_USBCMD);
        ehci_wr(cl + EHCI_USBCMD, cur_cmd & ~EHCI_CMD_RUN);  /* RUN=0 停止 */
        for (int i = 0; i < 1000000; i++) {
            if (ehci_rd(cl + EHCI_USBSTS) & (1u << 12)) break;  /* HCH=1 已停止 */
        }
        ehci_wr(cl + EHCI_ASYNCLIST, e_phys(qh));         /* 更新列表头 */
        ehci_wr(cl + EHCI_USBCMD, 0x31);                  /* RUN+ASEN+PSEN 重启 */
        /* V0.13.64: QEMU 需 IAAD 门铃 (USBCMD bit6) 触发异步列表推进 */
        ehci_wr(cl + EHCI_USBCMD, 0x31 | (1u << 6));
        ehci_wr(cl + EHCI_USBCMD, 0x31);
    }

    /* 等待完成 (轮询 STATUS qTD) */
    for (int i = 0; i < 5000000; i++) {
        uint32_t t = qtds[2]->token;
        if (!(t & 0x80000000)) break;
    }

    int rc = 0;
    uint32_t t2 = qtds[2]->token;
    if (t2 & 0x40000000) rc = -1;   /* 传输错误 */
    if (qtds[0]->token & 0x40000000) rc = -1;

    /* 卸下 QH (列表回终止) */
    {
        uint8_t cl = *(volatile uint8_t *)ehci_regs;
        uint32_t cmd = ehci_rd(cl + EHCI_USBCMD);
        ehci_wr(cl + EHCI_USBCMD, cmd & ~EHCI_CMD_ASEN);
        for (int i = 0; i < 1000000; i++) {
            if (!(ehci_rd(cl + EHCI_USBSTS) & (1u << 15))) break;
        }
        ehci_wr(cl + EHCI_ASYNCLIST, 1);
        ehci_wr(cl + EHCI_USBCMD, cmd | EHCI_CMD_ASEN);
    }
    ehci_async_qh[slot] = 0;
    return rc;
}

/* ---- 初始化 ---- */
int ehci64_init(void)
{
    if (ehci_initialized) return 0;
    uint32_t bar0 = 0;

    /* PCI 找 EHCI (class 0x0C03, prog_if 0x20) */
    bar0 = (uint32_t)pci_find_class_bar0(0x0C, 0x03, 0x20);
    if (!bar0) {
        vga_puts("[EHCI64] No EHCI controller found\n");
        return -1;
    }
    bar0 &= ~0x0F;
    vga_puts("[EHCI64] EHCI at MMIO ");
    vga_put_hex(bar0);
    vga_puts("\n");

    /* 映射 BAR (真机 MMIO 高区 0xF0000000+ 不在 vmm 0~2GB 映射, 必须先映射) */
    {
        extern uint64_t vmm64_get_pml4(void);
        extern int vmm64_map_page(uint64_t pml4_addr, uint64_t virt, uint64_t phys, uint64_t flags);
        uint64_t base = bar0 & ~0xFFFULL;  /* 4KB 对齐 */
        /* V0.13.64: 4KB 页映射 BAR 区 1MB (大页在 -cpu max 下有保留位异常,
         * 4KB 逐页映射规避) */
        int mr = 0;
        for (uint64_t a = base; a < base + 0x100000ULL; a += 0x1000) {
            mr = vmm64_map_page(vmm64_get_pml4(), a, a,
                                PAGE_PRESENT | PAGE_WRITE);
            if (mr != 0) break;
        }
        vga_puts("[EHCI64] map base=");
        vga_put_hex(base);
        vga_puts(" rc=");
        vga_put_hex((uint64_t)mr);
        vga_puts("\n");
    }
    ehci_mmio_base = bar0;
    ehci_regs = (uint8_t *)(uintptr_t)bar0;

    uint8_t caplen = *(volatile uint8_t *)ehci_regs;
    uint32_t hcc = ehci_rd(EHCI_HCCPARAMS);
    ehci_64bit = (hcc & 0x1);
    uint32_t hcsp = ehci_rd(EHCI_HCSPARAMS);
    ehci_ports = (hcsp >> 0) & 0x0F;

    /* 全局复位 */
    ehci_wr(caplen + EHCI_USBCMD, EHCI_CMD_HCRESET);
    for (int i = 0; i < 1000000; i++) {
        if ((ehci_rd(caplen + EHCI_USBCMD) & EHCI_CMD_HCRESET) == 0) break;
    }

    /* 分配周期帧列表 (4KB) + 异步列表 */
    ehci_periodic_frames = (uint32_t *)usb64_dma_alloc(4096);
    if (!ehci_periodic_frames) return -1;
    for (int i = 0; i < 1024; i++) ehci_periodic_frames[i] = 1;

    if (ehci_64bit) ehci_wr(caplen + EHCI_CTRLDSSEG, 0);  /* 高位地址0 */
    ehci_wr(caplen + EHCI_PERIODICLIST, e_phys(ehci_periodic_frames));
    ehci_wr(caplen + EHCI_ASYNCLIST, 1);   /* 空异步列表 */

    /* 配置标志: 路由所有端口到EHCI */
    ehci_wr(caplen + EHCI_CONFIGFLAG, 1);
    (void)ehci_ports;

    /* 端口复位与使能 */
    for (int p = 0; p < ehci_ports && p < 8; p++) {
        uint32_t off = caplen + EHCI_PORTSC + p * 4;
        uint32_t ps = ehci_rd(off);
        if (!(ps & EHCI_PORT_CCS)) continue;  /* 无设备 */
        ehci_wr(off, ps | EHCI_PORT_RESET);
        /* 复位至少 50ms: 用空循环 (真机时序宽松, 轮询 reset 位清除) */
        for (int i = 0; i < 1000000; i++) {
            if ((ehci_rd(off) & EHCI_PORT_RESET) == 0) break;
        }
        ps = ehci_rd(off);
        vga_puts("[EHCI64] Port ");
        vga_put_hex(p);
        vga_puts(ps & EHCI_PORT_CCS ? ": device connected\n" : ": empty\n");
    }

    /* 启动: RUN + 异步 + 周期 */
    ehci_wr(caplen + EHCI_USBCMD, EHCI_CMD_RUN | EHCI_CMD_ASEN | EHCI_CMD_PSEN);
    for (int i = 0; i < 1000000; i++) {
        if (!(ehci_rd(caplen + EHCI_USBSTS) & EHCI_STS_HCHALTED)) break;
    }

    ehci_initialized = 1;
    vga_puts("[EHCI64] Initialized\n");
    return 0;
}

/* ---- 供 usb64.c 查询 ---- */
int ehci64_present(void)
{
    return ehci_initialized;
}

/* 端口是否有设备连接 (供枚举) */
int ehci64_port_ccs(int port)
{
    if (!ehci_initialized || port < 0 || port >= ehci_ports || port >= 8) return 0;
    uint8_t caplen = *(volatile uint8_t *)ehci_regs;
    uint32_t ps = ehci_rd(caplen + EHCI_PORTSC + port * 4);
    return (ps & EHCI_PORT_CCS) ? 1 : 0;
}

int ehci64_port_count(void)
{
    return ehci_initialized ? ehci_ports : 0;
}
