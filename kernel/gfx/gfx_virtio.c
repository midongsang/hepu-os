/* ============================================================================
 * HepuOS 可插拔图形子系统 - virtio-gpu后端
 * virtio-gpu 2D硬件加速后端，支持QEMU virtio-gpu设备
 * 参考: Virtio GPU Spec v1.0
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "graphics.h"
#include "serial.h"
#include "pci.h"

/* virtio-gpu PCI IDs */
#define VIRTIO_VENDOR_ID       0x1AF4
#define VIRTIO_GPU_DEVICE_ID   0x1050

/* virtio PCI配置空间偏移 */
#define VIRTIO_PCI_HOST_FEATURES   0x00
#define VIRTIO_PCI_GUEST_FEATURES  0x04
#define VIRTIO_PCI_QUEUE_PFN       0x08
#define VIRTIO_PCI_QUEUE_NUM       0x0C
#define VIRTIO_PCI_QUEUE_SEL       0x0E
#define VIRTIO_PCI_QUEUE_NOTIFY    0x10
#define VIRTIO_PCI_STATUS          0x12
#define VIRTIO_PCI_ISR             0x13
#define VIRTIO_PCI_CONFIG          0x14

/* virtio状态位 */
#define VIRTIO_STATUS_ACKNOWLEDGE  0x01
#define VIRTIO_STATUS_DRIVER       0x02
#define VIRTIO_STATUS_DRIVER_OK    0x04
#define VIRTIO_STATUS_FEATURES_OK  0x08
#define VIRTIO_STATUS_FAILED       0x80

/* virtio-gpu命令类型 */
#define VIRTIO_GPU_CMD_GET_DISPLAY_INFO   0x0100
#define VIRTIO_GPU_CMD_RESOURCE_CREATE_2D 0x0101
#define VIRTIO_GPU_CMD_RESOURCE_UNREF     0x0102
#define VIRTIO_GPU_CMD_SET_SCANOUT        0x0103
#define VIRTIO_GPU_CMD_RESOURCE_FLUSH     0x0104
#define VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D 0x0105
#define VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING 0x0106
#define VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING 0x0107
#define VIRTIO_GPU_CMD_UPDATE_CURSOR      0x0300
#define VIRTIO_GPU_CMD_MOVE_CURSOR        0x0301

/* virtio-gpu响应类型 */
#define VIRTIO_GPU_RESP_OK_NODATA         0x1100
#define VIRTIO_GPU_RESP_OK_DISPLAY_INFO   0x1101
#define VIRTIO_GPU_RESP_ERR_UNSPEC        0x1200
#define VIRTIO_GPU_RESP_ERR_OUT_OF_MEMORY 0x1201
#define VIRTIO_GPU_RESP_ERR_INVALID_SCANOUT_ID 0x1202

/* virtio-gpu格式 */
#define VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM  67
#define VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM  68

/* virtio-gpu头部 */
typedef struct {
    uint32_t type;
    uint32_t flags;
    uint64_t fence_id;
    uint32_t ctx_id;
    uint32_t padding;
} virtio_gpu_ctrl_hdr_t;

/* virtio-gpu矩形 */
typedef struct {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
} virtio_gpu_rect_t;

/* virtio-gpu 2D资源创建 */
typedef struct {
    virtio_gpu_ctrl_hdr_t hdr;
    uint32_t resource_id;
    uint32_t format;
    uint32_t width;
    uint32_t height;
} virtio_gpu_resource_create_2d_t;

/* virtio-gpu设置扫描输出 */
typedef struct {
    virtio_gpu_ctrl_hdr_t hdr;
    uint32_t scanout_id;
    uint32_t resource_id;
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
} virtio_gpu_set_scanout_t;

/* virtio-gpu传输到主机 */
typedef struct {
    virtio_gpu_ctrl_hdr_t hdr;
    virtio_gpu_rect_t r;
    uint64_t offset;
    uint32_t resource_id;
    uint32_t padding;
} virtio_gpu_transfer_to_host_2d_t;

/* virtio-gpu刷新 */
typedef struct {
    virtio_gpu_ctrl_hdr_t hdr;
    virtio_gpu_rect_t r;
    uint32_t resource_id;
    uint32_t padding;
} virtio_gpu_resource_flush_t;

/* virtio-gpu附加后备存储 */
typedef struct {
    virtio_gpu_ctrl_hdr_t hdr;
    uint32_t resource_id;
    uint32_t num_entries;
} virtio_gpu_attach_backing_t;

/* virtio-gpu后备存储条目 */
typedef struct {
    uint64_t addr;
    uint32_t length;
    uint32_t padding;
} virtio_gpu_mem_entry_t;

/* virtio-gpu显示信息 */
typedef struct {
    virtio_gpu_rect_t rect;
    uint32_t enabled;
    uint32_t flags;
} virtio_gpu_display_one_t;

typedef struct {
    virtio_gpu_ctrl_hdr_t hdr;
    virtio_gpu_display_one_t modes[4];
} virtio_gpu_resp_display_info_t;

/* virtqueue描述符 */
#define VIRTQ_DESC_F_NEXT   1
#define VIRTQ_DESC_F_WRITE  2
#define VIRTQ_DESC_F_INDIRECT 4

typedef struct {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} virtq_desc_t;

typedef struct {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[256];
} virtq_avail_t;

typedef struct {
    uint32_t id;
    uint32_t len;
} virtq_used_elem_t;

typedef struct {
    uint16_t flags;
    uint16_t idx;
    virtq_used_elem_t ring[256];
} virtq_used_t;

/* 后端状态 */
static int virtio_initialized = 0;
static int virtio_pci_index = -1;
static uint16_t virtio_io_base = 0;
static uint32_t virtio_width = 0;
static uint32_t virtio_height = 0;
static uint32_t virtio_bpp = 32;
static uint8_t *virtio_framebuffer = NULL;
static uint32_t virtio_resource_id = 1;

/* V8.93增强：前向声明 */
static void virtio_shutdown(void);
static int virtio_gpu_set_resolution(uint32_t width, uint32_t height);

/* virtqueue */
#define VIRTQ_SIZE 256
static virtq_desc_t *ctrl_vq_desc = NULL;
static virtq_avail_t *ctrl_vq_avail = NULL;
static virtq_used_t *ctrl_vq_used = NULL;
static uint16_t ctrl_vq_last_used = 0;

/* IO端口读写 */
static inline uint8_t virtio_inb(uint16_t port) {
    uint8_t val;
    __asm__ volatile ("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static inline uint16_t virtio_inw(uint16_t port) {
    uint16_t val;
    __asm__ volatile ("inw %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static inline uint32_t virtio_inl(uint16_t port) {
    uint32_t val;
    __asm__ volatile ("inl %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static inline void virtio_outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline void virtio_outw(uint16_t port, uint16_t val) {
    __asm__ volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}

static inline void virtio_outl(uint16_t port, uint32_t val) {
    __asm__ volatile ("outl %0, %1" : : "a"(val), "Nd"(port));
}

/* 读取PCI配置空间 */
static uint32_t virtio_pci_read(int bus, int dev, int func, int offset) {
    uint32_t addr = 0x80000000 | (bus << 16) | (dev << 11) | (func << 8) | (offset & ~3);
    virtio_outl(0xCF8, addr);
    return virtio_inl(0xCFC);
}

/* 写入PCI配置空间 */
static void virtio_pci_write(int bus, int dev, int func, int offset, uint32_t val) {
    uint32_t addr = 0x80000000 | (bus << 16) | (dev << 11) | (func << 8) | (offset & ~3);
    virtio_outl(0xCF8, addr);
    virtio_outl(0xCFC, val);
}

/* 探测virtio-gpu设备 */
static int virtio_gpu_probe(void)
{
    serial_puts(0x3F8, "[GFX-VIRTIO] probing for virtio-gpu device...\n");

    /* 扫描PCI总线查找virtio-gpu */
    for (int bus = 0; bus < 256; bus++) {
        for (int dev = 0; dev < 32; dev++) {
            uint32_t vendor_dev = virtio_pci_read(bus, dev, 0, 0);
            uint16_t vendor = vendor_dev & 0xFFFF;
            uint16_t device = (vendor_dev >> 16) & 0xFFFF;

            if (vendor == VIRTIO_VENDOR_ID && device == VIRTIO_GPU_DEVICE_ID) {
                serial_puts(0x3F8, "[GFX-VIRTIO] found virtio-gpu at bus=");
                extern void serial_puthex(uint16_t, uint32_t);
                serial_puthex(0x3F8, bus);
                serial_puts(0x3F8, " dev=");
                serial_puthex(0x3F8, dev);
                serial_puts(0x3F8, "\n");

                /* 读取BAR0获取IO基地址 */
                uint32_t bar0 = virtio_pci_read(bus, dev, 0, 0x10);
                virtio_io_base = bar0 & ~0x3;
                serial_puts(0x3F8, "[GFX-VIRTIO] IO base=0x");
                serial_puthex(0x3F8, virtio_io_base);
                serial_puts(0x3F8, "\n");

                /* 启用PCI设备（IO空间+总线主控） */
                uint32_t cmd = virtio_pci_read(bus, dev, 0, 0x04);
                cmd |= 0x01 | 0x04; /* IO空间 + 总线主控 */
                virtio_pci_write(bus, dev, 0, 0x04, cmd);

                virtio_pci_index = bus * 32 + dev;
                return 0;
            }
        }
    }

    serial_puts(0x3F8, "[GFX-VIRTIO] no virtio-gpu device found\n");
    return -1;
}

/* 初始化virtqueue */
static int virtio_init_vq(uint16_t queue_sel, void **desc_out, void **avail_out, void **used_out)
{
    /* 选择队列 */
    virtio_outw(virtio_io_base + VIRTIO_PCI_QUEUE_SEL, queue_sel);

    /* 读取队列大小 */
    uint16_t queue_size = virtio_inw(virtio_io_base + VIRTIO_PCI_QUEUE_NUM);
    if (queue_size == 0 || queue_size > 256) {
        serial_puts(0x3F8, "[GFX-VIRTIO] invalid queue size\n");
        return -1;
    }

    /* 分配队列内存（4KB对齐） */
    uint32_t vq_size = sizeof(virtq_desc_t) * queue_size + sizeof(virtq_avail_t) + sizeof(virtq_used_t);
    /* 简化：使用静态内存 */
    static uint8_t vq_mem[4][4096] __attribute__((aligned(4096)));
    static int vq_used = 0;

    if (vq_used >= 4) return -1;
    void *vq_ptr = vq_mem[vq_used++];
    memset(vq_ptr, 0, 4096);

    *desc_out = vq_ptr;
    *avail_out = (uint8_t *)vq_ptr + sizeof(virtq_desc_t) * queue_size;
    *used_out = (uint8_t *)*avail_out + sizeof(virtq_avail_t);

    /* 设置队列PFN */
    virtio_outl(virtio_io_base + VIRTIO_PCI_QUEUE_PFN, (uint32_t)vq_ptr >> 12);

    serial_puts(0x3F8, "[GFX-VIRTIO] vq ");
    extern void serial_puthex(uint16_t, uint32_t);
    serial_puthex(0x3F8, queue_sel);
    serial_puts(0x3F8, " size=");
    serial_puthex(0x3F8, queue_size);
    serial_puts(0x3F8, " pfn=0x");
    serial_puthex(0x3F8, (uint32_t)vq_ptr >> 12);
    serial_puts(0x3F8, "\n");

    return 0;
}

/* 发送控制命令（完整virtqueue实现） */
static int virtio_gpu_send_cmd(void *cmd, uint32_t cmd_len, void *resp, uint32_t resp_len)
{
    if (!ctrl_vq_desc || !virtio_initialized) return -1;

    /* 获取当前可用环索引 */
    uint16_t avail_idx = ctrl_vq_avail->idx;

    /* 描述符0：命令（设备只读） */
    uint16_t desc_idx0 = avail_idx % VIRTQ_SIZE;
    ctrl_vq_desc[desc_idx0].addr = (uint64_t)(uintptr_t)cmd;
    ctrl_vq_desc[desc_idx0].len = cmd_len;
    ctrl_vq_desc[desc_idx0].flags = VIRTQ_DESC_F_NEXT;
    ctrl_vq_desc[desc_idx0].next = (desc_idx0 + 1) % VIRTQ_SIZE;

    /* 描述符1：响应（设备可写） */
    uint16_t desc_idx1 = (desc_idx0 + 1) % VIRTQ_SIZE;
    ctrl_vq_desc[desc_idx1].addr = (uint64_t)(uintptr_t)resp;
    ctrl_vq_desc[desc_idx1].len = resp_len;
    ctrl_vq_desc[desc_idx1].flags = VIRTQ_DESC_F_WRITE;
    ctrl_vq_desc[desc_idx1].next = 0;

    /* 添加到可用环 */
    ctrl_vq_avail->ring[avail_idx % VIRTQ_SIZE] = desc_idx0;
    ctrl_vq_avail->idx = avail_idx + 1;

    /* 通知设备 */
    virtio_outw(virtio_io_base + VIRTIO_PCI_QUEUE_NOTIFY, 0);

    /* 等待响应（轮询已用环） */
    int timeout = 1000000;
    while (ctrl_vq_used->idx == ctrl_vq_last_used && timeout-- > 0) {
        /* 短延迟 */
        for (volatile int i = 0; i < 100; i++);
    }

    if (timeout <= 0) {
        serial_puts(0x3F8, "[GFX-VIRTIO] command timeout\n");
        return -1;
    }

    /* 处理响应 */
    uint16_t used_idx = ctrl_vq_used->idx - 1;
    uint32_t resp_id = ctrl_vq_used->ring[used_idx % VIRTQ_SIZE].id;
    ctrl_vq_last_used = ctrl_vq_used->idx;

    /* 检查响应类型 */
    if (resp && resp_len >= sizeof(virtio_gpu_ctrl_hdr_t)) {
        virtio_gpu_ctrl_hdr_t *resp_hdr = (virtio_gpu_ctrl_hdr_t *)resp;
        if (resp_hdr->type == VIRTIO_GPU_RESP_ERR_UNSPEC ||
            resp_hdr->type == VIRTIO_GPU_RESP_ERR_OUT_OF_MEMORY) {
            serial_puts(0x3F8, "[GFX-VIRTIO] command error: 0x");
            extern void serial_puthex(uint16_t, uint32_t);
            serial_puthex(0x3F8, resp_hdr->type);
            serial_puts(0x3F8, "\n");
            return -1;
        }
    }

    return 0;
}

/* 创建2D资源 */
static int virtio_gpu_create_resource_2d(uint32_t resource_id, uint32_t width, uint32_t height)
{
    virtio_gpu_resource_create_2d_t cmd;
    virtio_gpu_ctrl_hdr_t resp;

    memset(&cmd, 0, sizeof(cmd));
    cmd.hdr.type = VIRTIO_GPU_CMD_RESOURCE_CREATE_2D;
    cmd.resource_id = resource_id;
    cmd.format = VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM;
    cmd.width = width;
    cmd.height = height;

    return virtio_gpu_send_cmd(&cmd, sizeof(cmd), &resp, sizeof(resp));
}

/* 附加后备存储 */
static int virtio_gpu_attach_backing(uint32_t resource_id, uint64_t addr, uint32_t length)
{
    /* 命令头 + num_entries + 一个mem_entry */
    struct {
        virtio_gpu_attach_backing_t hdr;
        virtio_gpu_mem_entry_t entry;
    } cmd;
    virtio_gpu_ctrl_hdr_t resp;

    memset(&cmd, 0, sizeof(cmd));
    cmd.hdr.hdr.type = VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING;
    cmd.hdr.resource_id = resource_id;
    cmd.hdr.num_entries = 1;
    cmd.entry.addr = addr;
    cmd.entry.length = length;

    return virtio_gpu_send_cmd(&cmd, sizeof(cmd), &resp, sizeof(resp));
}

/* 设置扫描输出 */
static int virtio_gpu_set_scanout(uint32_t scanout_id, uint32_t resource_id,
                                   uint32_t x, uint32_t y, uint32_t width, uint32_t height)
{
    virtio_gpu_set_scanout_t cmd;
    virtio_gpu_ctrl_hdr_t resp;

    memset(&cmd, 0, sizeof(cmd));
    cmd.hdr.type = VIRTIO_GPU_CMD_SET_SCANOUT;
    cmd.scanout_id = scanout_id;
    cmd.resource_id = resource_id;
    cmd.x = x;
    cmd.y = y;
    cmd.width = width;
    cmd.height = height;

    return virtio_gpu_send_cmd(&cmd, sizeof(cmd), &resp, sizeof(resp));
}

/* 传输到主机 */
static int virtio_gpu_transfer_to_host_2d(uint32_t resource_id, uint32_t x, uint32_t y,
                                           uint32_t width, uint32_t height, uint64_t offset)
{
    virtio_gpu_transfer_to_host_2d_t cmd;
    virtio_gpu_ctrl_hdr_t resp;

    memset(&cmd, 0, sizeof(cmd));
    cmd.hdr.type = VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D;
    cmd.r.x = x;
    cmd.r.y = y;
    cmd.r.width = width;
    cmd.r.height = height;
    cmd.offset = offset;
    cmd.resource_id = resource_id;

    return virtio_gpu_send_cmd(&cmd, sizeof(cmd), &resp, sizeof(resp));
}

/* 刷新资源 */
static int virtio_gpu_resource_flush(uint32_t resource_id, uint32_t x, uint32_t y,
                                      uint32_t width, uint32_t height)
{
    virtio_gpu_resource_flush_t cmd;
    virtio_gpu_ctrl_hdr_t resp;

    memset(&cmd, 0, sizeof(cmd));
    cmd.hdr.type = VIRTIO_GPU_CMD_RESOURCE_FLUSH;
    cmd.r.x = x;
    cmd.r.y = y;
    cmd.r.width = width;
    cmd.r.height = height;
    cmd.resource_id = resource_id;

    return virtio_gpu_send_cmd(&cmd, sizeof(cmd), &resp, sizeof(resp));
}

/* 获取显示信息 */
static int virtio_gpu_get_display_info(virtio_gpu_resp_display_info_t *info)
{
    virtio_gpu_ctrl_hdr_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.type = VIRTIO_GPU_CMD_GET_DISPLAY_INFO;

    return virtio_gpu_send_cmd(&cmd, sizeof(cmd), info, sizeof(*info));
}

/* ============================================================================
 * 后端接口实现
 * ========================================================================== */

static int virtio_init(void)
{
    if (virtio_initialized) return 0;

    serial_puts(0x3F8, "[GFX-VIRTIO] initializing virtio-gpu backend...\n");

    /* 探测设备 */
    if (virtio_gpu_probe() != 0) {
        return -1;
    }

    /* 读取设备特性 */
    uint32_t host_features = virtio_inl(virtio_io_base + VIRTIO_PCI_HOST_FEATURES);
    serial_puts(0x3F8, "[GFX-VIRTIO] host features=0x");
    extern void serial_puthex(uint16_t, uint32_t);
    serial_puthex(0x3F8, host_features);
    serial_puts(0x3F8, "\n");

    /* 设置驱动特性（简化：不设置任何特性） */
    virtio_outl(virtio_io_base + VIRTIO_PCI_GUEST_FEATURES, 0);

    /* 初始化控制队列 */
    if (virtio_init_vq(0, (void**)&ctrl_vq_desc, (void**)&ctrl_vq_avail, (void**)&ctrl_vq_used) != 0) {
        serial_puts(0x3F8, "[GFX-VIRTIO] failed to init control vq\n");
        return -1;
    }

    /* 设置状态 */
    virtio_outb(virtio_io_base + VIRTIO_PCI_STATUS,
        VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_FEATURES_OK);
    virtio_outb(virtio_io_base + VIRTIO_PCI_STATUS,
        VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_FEATURES_OK | VIRTIO_STATUS_DRIVER_OK);

    /* 分配帧缓冲（默认1024x768x32） */
    virtio_width = 1024;
    virtio_height = 768;
    virtio_bpp = 32;

    /* 简化：使用静态帧缓冲 */
    static uint8_t fb_mem[1024 * 768 * 4] __attribute__((aligned(4096)));
    virtio_framebuffer = fb_mem;
    memset(virtio_framebuffer, 0, sizeof(fb_mem));

    virtio_initialized = 1;

    serial_puts(0x3F8, "[GFX-VIRTIO] backend initialized (");
    extern void serial_puthex(uint16_t, uint32_t);
    serial_puthex(0x3F8, virtio_width);
    serial_puts(0x3F8, "x");
    serial_puthex(0x3F8, virtio_height);
    serial_puts(0x3F8, "@");
    serial_puthex(0x3F8, virtio_bpp);
    serial_puts(0x3F8, "bpp)\n");

    /* 发送virtio-gpu初始化命令 */
    serial_puts(0x3F8, "[GFX-VIRTIO] creating 2D resource...\n");
    if (virtio_gpu_create_resource_2d(virtio_resource_id, virtio_width, virtio_height) != 0) {
        serial_puts(0x3F8, "[GFX-VIRTIO] failed to create resource, using software mode\n");
        return 0;  /* 软件模式仍然可用 */
    }

    serial_puts(0x3F8, "[GFX-VIRTIO] attaching backing store...\n");
    uint32_t fb_size = virtio_width * virtio_height * (virtio_bpp / 8);
    if (virtio_gpu_attach_backing(virtio_resource_id, (uint64_t)(uintptr_t)virtio_framebuffer, fb_size) != 0) {
        serial_puts(0x3F8, "[GFX-VIRTIO] failed to attach backing, using software mode\n");
        return 0;
    }

    serial_puts(0x3F8, "[GFX-VIRTIO] setting scanout...\n");
    if (virtio_gpu_set_scanout(0, virtio_resource_id, 0, 0, virtio_width, virtio_height) != 0) {
        serial_puts(0x3F8, "[GFX-VIRTIO] failed to set scanout, using software mode\n");
        return 0;
    }

    serial_puts(0x3F8, "[GFX-VIRTIO] hardware acceleration enabled\n");

    return 0;
}

static int virtio_set_mode(uint32_t w, uint32_t h, uint32_t bpp)
{
    /* V8.93: 支持动态分辨率切换 */
    if (virtio_initialized && (w != virtio_width || h != virtio_height)) {
        return virtio_gpu_set_resolution(w, h);
    }
    virtio_width = w;
    virtio_height = h;
    virtio_bpp = bpp;
    return 0;
}

static void virtio_putpixel(uint32_t x, uint32_t y, gfx_color_t color)
{
    if (!virtio_framebuffer || x >= virtio_width || y >= virtio_height) return;
    uint32_t *fb = (uint32_t *)virtio_framebuffer;
    fb[y * virtio_width + x] = color;
}

static gfx_color_t virtio_getpixel(uint32_t x, uint32_t y)
{
    if (!virtio_framebuffer || x >= virtio_width || y >= virtio_height) return 0;
    uint32_t *fb = (uint32_t *)virtio_framebuffer;
    return fb[y * virtio_width + x];
}

static void virtio_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, gfx_color_t color)
{
    if (!virtio_framebuffer) return;
    for (uint32_t j = y; j < y + h && j < virtio_height; j++) {
        for (uint32_t i = x; i < x + w && i < virtio_width; i++) {
            uint32_t *fb = (uint32_t *)virtio_framebuffer;
            fb[j * virtio_width + i] = color;
        }
    }
}

static void virtio_draw_char(uint32_t x, uint32_t y, char c, gfx_color_t fg, gfx_color_t bg)
{
    if (!virtio_framebuffer) return;
    /* 简化：使用8x8字体 */
    static const uint8_t font8x8[128][8] = {
        ['A'] = {0x18,0x24,0x42,0x42,0x7E,0x42,0x42,0x00},
        ['B'] = {0x7C,0x42,0x42,0x7C,0x42,0x42,0x7C,0x00},
        ['C'] = {0x3C,0x42,0x40,0x40,0x40,0x42,0x3C,0x00},
        ['H'] = {0x42,0x42,0x42,0x7E,0x42,0x42,0x42,0x00},
        ['e'] = {0x00,0x00,0x3C,0x40,0x7C,0x40,0x3C,0x00},
        ['l'] = {0x18,0x08,0x08,0x08,0x08,0x08,0x1C,0x00},
        ['o'] = {0x00,0x00,0x3C,0x42,0x42,0x42,0x3C,0x00},
        ['p'] = {0x00,0x7C,0x42,0x42,0x7C,0x40,0x40,0x00},
        ['u'] = {0x00,0x00,0x42,0x42,0x42,0x46,0x3A,0x00},
        [' '] = {0,0,0,0,0,0,0,0},
    };

    if ((uint8_t)c >= 128) c = '?';
    const uint8_t *glyph = font8x8[(uint8_t)c];

    for (int row = 0; row < 8; row++) {
        uint8_t line = glyph[row];
        for (int col = 0; col < 8; col++) {
            if (line & (0x80 >> col)) {
                virtio_putpixel(x + col, y + row, fg);
            } else if (bg != 0) {
                virtio_putpixel(x + col, y + row, bg);
            }
        }
    }
}

static void virtio_draw_string(uint32_t x, uint32_t y, const char *str, gfx_color_t fg, gfx_color_t bg)
{
    while (*str) {
        virtio_draw_char(x, y, *str, fg, bg);
        x += 8;
        str++;
    }
}

static void virtio_clear(gfx_color_t color)
{
    if (!virtio_framebuffer) return;
    uint32_t *fb = (uint32_t *)virtio_framebuffer;
    for (uint32_t i = 0; i < virtio_width * virtio_height; i++) {
        fb[i] = color;
    }
}

static void virtio_present(void)
{
    if (!virtio_initialized || !virtio_framebuffer) return;

    /* 传输帧缓冲到主机 */
    uint32_t fb_size = virtio_width * virtio_height * (virtio_bpp / 8);
    virtio_gpu_transfer_to_host_2d(virtio_resource_id, 0, 0, virtio_width, virtio_height, 0);

    /* 刷新资源到屏幕 */
    virtio_gpu_resource_flush(virtio_resource_id, 0, 0, virtio_width, virtio_height);
}

static void* virtio_get_fb_vaddr(void)
{
    return virtio_framebuffer;
}

static uint32_t virtio_get_fb_paddr(void)
{
    return (uint32_t)virtio_framebuffer;
}

static int virtio_has_double_buffer(void)
{
    return 1; /* virtio-gpu始终有双缓冲（主机端） */
}

static uint32_t virtio_get_width(void) { return virtio_width; }
static uint32_t virtio_get_height(void) { return virtio_height; }
static uint32_t virtio_get_bpp(void) { return virtio_bpp; }
static uint32_t virtio_get_pitch(void) { return virtio_width * (virtio_bpp / 8); }

/* ============================================================================
 * 后端注册
 * ========================================================================== */

static gfx_backend_t virtio_backend = {
    .type = GFX_BACKEND_VIRTIO,
    .name = "virtio-gpu",
    .init = virtio_init,
    .shutdown = virtio_shutdown,
    .set_mode = virtio_set_mode,
    .get_mode = NULL,
    .fill_rect = virtio_fill_rect,
    .put_pixel = virtio_putpixel,
    .draw_char = virtio_draw_char,
    .blit = NULL,
    .get_fb_vaddr = virtio_get_fb_vaddr,
    .get_fb_paddr = virtio_get_fb_paddr,
    .present = virtio_present,
    .has_double_buffer = virtio_has_double_buffer,
};

void gfx_virtio_register(void)
{
    extern int gfx_register_backend(gfx_backend_t *backend);
    gfx_register_backend(&virtio_backend);
    serial_puts(0x3F8, "[GFX-VIRTIO] backend registered\n");
}

/* ============================================================================
 * V8.93增强：光标支持 + 动态分辨率 + 多扫描输出
 * ========================================================================== */

/* 光标队列 */
static void *cursor_vq_desc = NULL;
static void *cursor_vq_avail = NULL;
static void *cursor_vq_used = NULL;
static int cursor_vq_initialized = 0;

/* 光标状态 */
static int cursor_enabled = 0;
static uint32_t cursor_x = 0;
static uint32_t cursor_y = 0;
static uint32_t cursor_resource_id = 2;  /* 光标用资源ID 2 */

/* 光标命令结构 */
typedef struct {
    virtio_gpu_ctrl_hdr_t hdr;
    uint32_t resource_id;
    uint32_t hot_x;
    uint32_t hot_y;
    uint32_t width;
    uint32_t height;
} __attribute__((packed)) virtio_gpu_update_cursor_t;

typedef struct {
    virtio_gpu_ctrl_hdr_t hdr;
    uint32_t scanout_id;
    uint32_t x;
    uint32_t y;
    uint32_t padding;
} __attribute__((packed)) virtio_gpu_move_cursor_t;

/* 初始化光标队列 */
static int virtio_cursor_init(void)
{
    if (cursor_vq_initialized) return 0;

    if (virtio_init_vq(1, &cursor_vq_desc, &cursor_vq_avail, &cursor_vq_used) != 0) {
        serial_puts(0x3F8, "[GFX-VIRTIO] failed to init cursor vq\n");
        return -1;
    }

    cursor_vq_initialized = 1;
    serial_puts(0x3F8, "[GFX-VIRTIO] cursor queue initialized\n");
    return 0;
}

/* 发送光标命令 */
static int virtio_gpu_send_cursor_cmd(void *cmd, uint32_t cmd_len)
{
    if (!cursor_vq_initialized) return -1;

    /* 简化：复用控制队列发送光标命令 */
    /* 实际应该使用光标队列，但控制队列也能处理 */
    virtio_gpu_ctrl_hdr_t resp;
    memset(&resp, 0, sizeof(resp));
    return virtio_gpu_send_cmd(cmd, cmd_len, &resp, sizeof(resp));
}

/* 更新光标 */
int virtio_gpu_update_cursor(uint32_t x, uint32_t y)
{
    if (!virtio_initialized) return -1;

    cursor_x = x;
    cursor_y = y;

    if (!cursor_enabled) {
        /* 第一次：创建光标资源 */
        static uint32_t cursor_data[64 * 64] __attribute__((aligned(4096)));
        memset(cursor_data, 0xFF, sizeof(cursor_data));  /* 白色光标 */

        virtio_gpu_create_resource_2d(cursor_resource_id, 64, 64);
        virtio_gpu_attach_backing(cursor_resource_id,
                                  (uint64_t)(uintptr_t)cursor_data,
                                  sizeof(cursor_data));

        virtio_gpu_update_cursor_t cmd;
        memset(&cmd, 0, sizeof(cmd));
        cmd.hdr.type = VIRTIO_GPU_CMD_UPDATE_CURSOR;
        cmd.resource_id = cursor_resource_id;
        cmd.hot_x = 0;
        cmd.hot_y = 0;
        cmd.width = 64;
        cmd.height = 64;
        virtio_gpu_send_cursor_cmd(&cmd, sizeof(cmd));

        cursor_enabled = 1;
    }

    /* 移动光标 */
    virtio_gpu_move_cursor_t move;
    memset(&move, 0, sizeof(move));
    move.hdr.type = VIRTIO_GPU_CMD_MOVE_CURSOR;
    move.scanout_id = 0;
    move.x = x;
    move.y = y;
    virtio_gpu_send_cursor_cmd(&move, sizeof(move));

    return 0;
}

/* 隐藏光标 */
int virtio_gpu_hide_cursor(void)
{
    if (!cursor_enabled) return 0;

    /* 发送空光标 */
    virtio_gpu_update_cursor_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.hdr.type = VIRTIO_GPU_CMD_UPDATE_CURSOR;
    cmd.resource_id = 0;  /* 0表示禁用光标 */
    virtio_gpu_send_cursor_cmd(&cmd, sizeof(cmd));

    cursor_enabled = 0;
    return 0;
}

/* 动态切换分辨率 */
int virtio_gpu_set_resolution(uint32_t width, uint32_t height)
{
    if (!virtio_initialized) return -1;
    if (width == virtio_width && height == virtio_height) return 0;

    serial_puts(0x3F8, "[GFX-VIRTIO] switching resolution to ");
    extern void serial_puthex(uint16_t, uint32_t);
    serial_puthex(0x3F8, width);
    serial_puts(0x3F8, "x");
    serial_puthex(0x3F8, height);
    serial_puts(0x3F8, "\n");

    /* 卸载旧资源 */
    virtio_gpu_ctrl_hdr_t unref_cmd;
    memset(&unref_cmd, 0, sizeof(unref_cmd));
    unref_cmd.type = VIRTIO_GPU_CMD_RESOURCE_UNREF;
    unref_cmd.flags = 0;
    unref_cmd.fence_id = 0;
    unref_cmd.ctx_id = 0;
    /* REURCE_UNREF需要resource_id在hdr后面 */
    uint32_t *res_id = (uint32_t*)((uint8_t*)&unref_cmd + sizeof(unref_cmd));
    *res_id = virtio_resource_id;
    virtio_gpu_ctrl_hdr_t resp;
    virtio_gpu_send_cmd(&unref_cmd, sizeof(unref_cmd) + 4, &resp, sizeof(resp));

    /* 更新尺寸 */
    virtio_width = width;
    virtio_height = height;

    /* 重新分配帧缓冲 */
    static uint8_t fb_mem_large[1920 * 1080 * 4] __attribute__((aligned(4096)));
    virtio_framebuffer = fb_mem_large;
    memset(virtio_framebuffer, 0, width * height * 4);

    /* 创建新资源 */
    virtio_gpu_create_resource_2d(virtio_resource_id, width, height);
    virtio_gpu_attach_backing(virtio_resource_id,
                              (uint64_t)(uintptr_t)virtio_framebuffer,
                              width * height * 4);
    virtio_gpu_set_scanout(0, virtio_resource_id, 0, 0, width, height);

    serial_puts(0x3F8, "[GFX-VIRTIO] resolution switched\n");
    return 0;
}

/* 获取显示信息（支持的分辨率列表） */
int virtio_gpu_get_modes(uint32_t *widths, uint32_t *heights, int max_modes)
{
    if (!virtio_initialized || max_modes <= 0) return 0;

    virtio_gpu_resp_display_info_t info;
    if (virtio_gpu_get_display_info(&info) != 0) return 0;

    int count = 0;
    for (int i = 0; i < 4 && count < max_modes; i++) {
        if (info.modes[i].enabled && info.modes[i].rect.width > 0) {
            widths[count] = info.modes[i].rect.width;
            heights[count] = info.modes[i].rect.height;
            count++;
        }
    }

    /* 至少返回默认分辨率 */
    if (count == 0) {
        widths[0] = 1024;
        heights[0] = 768;
        count = 1;
    }

    return count;
}

/* 关闭后端 */
static void virtio_shutdown(void)
{
    if (!virtio_initialized) return;

    /* 隐藏光标 */
    virtio_gpu_hide_cursor();

    /* 卸载资源 */
    virtio_gpu_ctrl_hdr_t unref_cmd;
    memset(&unref_cmd, 0, sizeof(unref_cmd));
    unref_cmd.type = VIRTIO_GPU_CMD_RESOURCE_UNREF;
    uint32_t *res_id = (uint32_t*)((uint8_t*)&unref_cmd + sizeof(unref_cmd));
    *res_id = virtio_resource_id;
    virtio_gpu_ctrl_hdr_t resp;
    virtio_gpu_send_cmd(&unref_cmd, sizeof(unref_cmd) + 4, &resp, sizeof(resp));

    /* 重置设备状态 */
    virtio_outb(virtio_io_base + VIRTIO_PCI_STATUS, 0);

    virtio_initialized = 0;
    serial_puts(0x3F8, "[GFX-VIRTIO] backend shutdown\n");
}
