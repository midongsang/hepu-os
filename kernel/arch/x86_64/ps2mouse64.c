/* ============================================================================
 * 赫普OS 64位 - PS/2 鼠标驱动
 * 从32位移植，8042辅助设备完整初始化 + 轮询读包
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>

#define PS2_DATA_PORT    0x60
#define PS2_STATUS_PORT  0x64
#define PS2_CMD_PORT     0x64

/* 8042 命令 */
#define PS2_CMD_ENABLE_AUX    0xA8
#define PS2_CMD_READ_CONFIG   0x20
#define PS2_CMD_WRITE_CONFIG  0x60
#define PS2_CMD_WRITE_AUX     0xD4

/* 鼠标命令 */
#define MOUSE_CMD_RESET       0xFF
#define MOUSE_CMD_ENABLE      0xF4
#define MOUSE_CMD_DISABLE     0xF5
#define MOUSE_CMD_READ_ID     0xF2
#define MOUSE_ACK             0xFA

typedef struct {
    int      dx;
    int      dy;
    int      wheel;    /* V0.13.9: 滚轮 (IntelliMouse 4字节包, 有符号每格) */
    uint8_t  left;
    uint8_t  right;
    uint8_t  middle;
    uint8_t  valid;
} ps2_mouse_event_t;

extern void vga_puts(const char *s);

/* V0.13.9: IntelliMouse 4字节滚轮模式 (采样率 200→100→80 探测) */
static int ps2_wheel_mode = 0;

static inline uint8_t inb(uint16_t port)
{
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outb(uint16_t port, uint8_t val)
{
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

/* 等待8042输入缓冲空闲 (bit1=0 可写) */
static int ps2_wait_input(void)
{
    for (int i = 0; i < 100000; i++) {
        if (!(inb(PS2_STATUS_PORT) & 0x02)) return 1;
    }
    return 0;
}

/* 等待8042输出缓冲有数据 (bit0=1) */
static int ps2_wait_output(void)
{
    for (int i = 0; i < 100000; i++) {
        if (inb(PS2_STATUS_PORT) & 0x01) return 1;
    }
    return 0;
}

/* 向鼠标发送命令 (0xD4前缀) */
int ps2mouse_command(uint8_t cmd)
{
    if (!ps2_wait_input()) return -1;
    outb(PS2_CMD_PORT, PS2_CMD_WRITE_AUX);
    if (!ps2_wait_input()) return -1;
    outb(PS2_DATA_PORT, cmd);
    if (!ps2_wait_output()) return -1;
    uint8_t ack = inb(PS2_DATA_PORT);
    return (ack == MOUSE_ACK) ? 0 : -1;
}

/* 初始化PS/2鼠标（参考Linux/BSD标准序列：flush→disable→reset→读ID→enable）
 * 轮询模式驱动不依赖aux IRQ，因此8042配置字节读取失败不影响后续 */
int ps2mouse_init(void)
{
    /* 0. 清空8042输出缓冲残留字节，避免污染后续命令流 */
    for (int i = 0; i < 16; i++) {
        if (inb(PS2_STATUS_PORT) & 0x01) (void)inb(PS2_DATA_PORT);
    }

    /* 1. 启用辅助设备接口 */
    if (!ps2_wait_input()) { vga_puts("[MOUSE64] step1 wait_input failed\n"); return -1; }
    outb(PS2_CMD_PORT, PS2_CMD_ENABLE_AUX);
    if (!ps2_wait_input()) { vga_puts("[MOUSE64] step1b wait_input failed\n"); return -1; }

    /* 2. 读命令字节, 置位辅助设备IRQ使能(bit1), 清时钟禁用(bit5)
     * 读取超时容错：轮询模式不依赖aux IRQ，直接用QEMU默认配置继续 */
    outb(PS2_CMD_PORT, PS2_CMD_READ_CONFIG);
    if (ps2_wait_output()) {
        uint8_t cfg = inb(PS2_DATA_PORT);
        cfg |= 0x02;      /* enable aux IRQ */
        cfg &= ~0x20;     /* enable aux clock */
        if (ps2_wait_input()) {
            outb(PS2_CMD_PORT, PS2_CMD_WRITE_CONFIG);
            if (ps2_wait_input()) outb(PS2_DATA_PORT, cfg);
        }
    } else {
        vga_puts("[MOUSE64] read_config timeout, using raw polling mode\n");
    }

    /* 3. 复位鼠标 (0xFF): ACK + 自检0xAA + 设备ID */
    if (ps2mouse_command(MOUSE_CMD_RESET) == 0) {
        /* 读掉复位后额外字节：0xAA（自检通过）+ 设备ID，避免残留污染后续命令 */
        for (int extra = 0; extra < 2; extra++) {
            if (ps2_wait_output()) (void)inb(PS2_DATA_PORT);
        }
    } else {
        vga_puts("[MOUSE64] reset command failed\n");
    }

    /* 3.5 V0.13.9: IntelliMouse 滚轮探测 — 采样率 200→100→80 后读设备 ID 确认
     * (参考 Linux psmouse 标准序列; ID=3 才启用 4 字节包, 否则保持 3 字节标准模式) */
    if (ps2mouse_command(0xF3) == 0 && ps2mouse_command(200) == 0 &&
        ps2mouse_command(0xF3) == 0 && ps2mouse_command(100) == 0 &&
        ps2mouse_command(0xF3) == 0 && ps2mouse_command(80) == 0) {
        if (ps2mouse_command(0xF2) == 0 && ps2_wait_output()) {
            uint8_t dev_id = inb(PS2_DATA_PORT);
            if (dev_id == 3) {
                ps2_wheel_mode = 1;
                vga_puts("[MOUSE64] IntelliMouse 4-byte wheel mode enabled\n");
            } else {
                vga_puts("[MOUSE64] Standard 3-byte mouse mode\n");
            }
        }
    }

    /* 4. 启用数据报告 (0xF4) */
    if (ps2mouse_command(MOUSE_CMD_ENABLE) != 0) {
        vga_puts("[MOUSE64] PS/2 mouse enable report failed\n");
        return -1;
    }

    vga_puts("[MOUSE64] PS/2 mouse ready (aux device, IRQ12)\n");
    return 0;
}

/* 轮询读取3字节标准PS/2鼠标包 */
int ps2mouse_poll(ps2_mouse_event_t *ev)
{
    if (!ev) return 0;

    /* 检查8042输出缓冲有辅助设备数据 */
    uint8_t st = inb(PS2_STATUS_PORT);
    if (!(st & 0x01)) return 0;        /* 输出缓冲空 */
    if (!(st & 0x20)) return 0;        /* 不是辅助设备数据 */

    /* 读首字节 (含同步位 bit3=1) */
    uint8_t b0 = inb(PS2_DATA_PORT);
    if (!(b0 & 0x08)) return 0;        /* 丢包/未同步, 丢弃 */

    /* 等待并读后续2字节 */
    if (!ps2_wait_output()) return 0;
    uint8_t b1 = inb(PS2_DATA_PORT);
    if (!ps2_wait_output()) return 0;
    uint8_t b2 = inb(PS2_DATA_PORT);

    /* 带符号移动量 (9位补码) */
    int dx = (int)(int8_t)b1;
    int dy = (int)(int8_t)b2;
    /* 溢出位 */
    if (b0 & 0x40) dx = (dx > 0) ? 127 : -127;
    if (b0 & 0x80) dy = (dy > 0) ? 127 : -127;

    ev->dx = dx;
    ev->dy = -dy;      /* PS/2 Y轴向上为正, 屏幕坐标需翻转 */
    ev->left   = (b0 & 0x01) ? 1 : 0;
    ev->right  = (b0 & 0x02) ? 1 : 0;
    ev->middle = (b0 & 0x04) ? 1 : 0;
    ev->wheel  = 0;
    /* V0.13.9: 4字节包滚轮 (IntelliMouse; 第4字节有符号每格; 短超时防无滚轮设备卡顿) */
    if (ps2_wheel_mode) {
        int spins = 0;
        while (spins++ < 200 && !(inb(PS2_STATUS_PORT) & 0x01)) {}
        if (inb(PS2_STATUS_PORT) & 0x01) {
            uint8_t b3 = inb(PS2_DATA_PORT);
            ev->wheel = (int)(int8_t)b3;
        }
    }
    ev->valid  = 1;
    return 1;
}

/* 鼠标状态（用于桌面） */
static int mouse_x = 0;
static int mouse_y = 0;
static int mouse_left = 0;
static int mouse_right = 0;
static int mouse_wheel = 0;     /* V0.13.9: 滚轮累积 (消费式) */
static int mouse_initialized = 0;
static int mouse_prev_left = 0;
static int mouse_click_latch = 0;  /* 快速点击: 同批"按下+释放"时保留按下沿, 防止点击丢失 */

/* 更新鼠标状态（在主循环中调用） */
void mouse64_update(int screen_width, int screen_height)
{
    if (!mouse_initialized) return;

    ps2_mouse_event_t ev;
    while (ps2mouse_poll(&ev)) {
        mouse_x += ev.dx;
        mouse_y += ev.dy;
        if (mouse_x < 0) mouse_x = 0;
        if (mouse_x >= screen_width) mouse_x = screen_width - 1;
        if (mouse_y < 0) mouse_y = 0;
        if (mouse_y >= screen_height) mouse_y = screen_height - 1;
        /* 一次轮询内若同时收到按下与释放, 上层边沿检测会漏掉这次点击 */
        if (ev.left && !mouse_prev_left) mouse_click_latch = 1;
        mouse_prev_left = ev.left;
        mouse_left = ev.left;
        mouse_right = ev.right;
        mouse_wheel += ev.wheel;
    }
}

/* 消费一次被批处理合并的点击 (返回 1 表示本批发生过按下沿) */
int mouse64_take_click(void)
{
    int v = mouse_click_latch;
    mouse_click_latch = 0;
    return v;
}

/* V0.13.9: 获取滚轮累积值 (消费式 — 读后清零) */
int mouse64_get_wheel(void)
{
    int w = mouse_wheel;
    mouse_wheel = 0;
    return w;
}

/* 获取鼠标位置 */
void mouse64_get_pos(int *x, int *y)
{
    if (x) *x = mouse_x;
    if (y) *y = mouse_y;
}

/* 获取鼠标按键状态 */
int mouse64_get_left(void) { return mouse_left; }
int mouse64_get_right(void) { return mouse_right; }

/* 初始化鼠标（带屏幕尺寸） */
int mouse64_init(int screen_width, int screen_height)
{
    /* 尽力初始化；失败也启用裸轮询（ps2mouse_poll 只读状态端口，无阻塞） */
    int ret = ps2mouse_init();
    if (ret != 0) {
        vga_puts("[MOUSE64] PS/2 init incomplete, enabling raw polling\n");
    } else {
        vga_puts("[MOUSE64] PS/2 mouse ready\n");
    }
    mouse_x = screen_width / 2;
    mouse_y = screen_height / 2;
    mouse_initialized = 1;
    return 0;
}
