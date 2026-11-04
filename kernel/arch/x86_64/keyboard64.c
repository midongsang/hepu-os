/* ============================================================================
 * 赫普OS - 64位键盘驱动（简化版）
 * V0.10.6: 初始实现（PS/2键盘，中断驱动）
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>

/* 键盘端口 */
#define KEYBOARD_DATA_PORT    0x60
#define KEYBOARD_STATUS_PORT  0x64
#define KEYBOARD_COMMAND_PORT 0x64

/* 键盘状态位 */
#define KEYBOARD_STATUS_OUTPUT_FULL 0x01
#define KEYBOARD_STATUS_INPUT_FULL  0x02

/* 扫描码 */
#define KEY_ESC      0x01
#define KEY_ENTER    0x1C
#define KEY_BACKSPACE 0x0E
#define KEY_TAB      0x0F
#define KEY_SPACE    0x39
#define KEY_LSHIFT   0x2A
#define KEY_RSHIFT   0x36
#define KEY_CAPS     0x3A
#define KEY_CTRL     0x1D
#define KEY_ALT      0x38

/* 键盘缓冲区 */
#define KEYBOARD_BUFFER_SIZE 256
static uint8_t keyboard_buffer[KEYBOARD_BUFFER_SIZE];
static int keyboard_buffer_head = 0;
static int keyboard_buffer_tail = 0;

/* 键盘状态 */
static int shift_pressed = 0;
static int caps_lock = 0;
static int ctrl_pressed = 0;
static int alt_pressed = 0;
/* V0.13.46: Ctrl+Alt+K 强制结束未响应进程标志（键盘IRQ直通，不依赖GUI主循环） */
static volatile int g_force_kill_key = 0;
static int win_pressed = 0;  /* V0.13.9: Win/Meta 键 (0xE0 0x5B/0x5C) */
static int pending_e0 = 0;   /* V0.13.7 P4: 0xE0 扩展键前缀 */

/* V0.13.7 P4: 扩展键内部码 (方向键/翻页/行首尾; 0x90-0x96 不与 ASCII 冲突) */
#define KEY_EXT_UP      0x90
#define KEY_EXT_DOWN    0x91
#define KEY_EXT_LEFT    0x92
#define KEY_EXT_RIGHT   0x93
#define KEY_EXT_PGUP    0x94
#define KEY_EXT_PGDN    0x95
#define KEY_EXT_HOME    0x96
#define KEY_EXT_END     0x97
#define KEY_EXT_DEL     0x98
#define KEY_EXT_INS     0x99

/* 外部函数 */
extern void vga_puts(const char *str);
extern void vga_putchar(char c);
extern void vga_put_hex(uint64_t val);

/* 端口读写 */
static inline uint8_t inb(uint16_t port)
{
    uint8_t val;
    __asm__ volatile("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static inline void outb(uint16_t port, uint8_t val)
{
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

/* 扫描码转ASCII（无Shift） */
static const char scancode_to_ascii[] = {
    0, 0, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b', '\t',
    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', 0, 'a', 's',
    'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\', 'z', 'x', 'c', 'v',
    'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' ', 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, '7', '8', '9', '-', '4', '5', '6', '+', '1',
    '2', '3', '0', '.', 0, 0, 0, 0, 0
};

/* 扫描码转ASCII（有Shift） */
static const char scancode_to_ascii_shift[] = {
    0, 0, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b', '\t',
    'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n', 0, 'A', 'S',
    'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~', 0, '|', 'Z', 'X', 'C', 'V',
    'B', 'N', 'M', '<', '>', '?', 0, '*', 0, ' ', 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, '7', '8', '9', '-', '4', '5', '6', '+', '1',
    '2', '3', '0', '.', 0, 0, 0, 0, 0
};

/* 初始化键盘 */
/* 键盘中断处理前向声明 */
void keyboard64_interrupt(void);

void keyboard64_init(void)
{
    vga_puts("[KEYBOARD64] Initializing PS/2 keyboard...\n");

    /* 清空缓冲区 */
    keyboard_buffer_head = 0;
    keyboard_buffer_tail = 0;

    /* 清掉 8042 输出缓冲里的残留字节（复位/固件遗留），否则会被当成按键 */
    for (int i = 0; i < 16; i++) {
        if (inb(KEYBOARD_STATUS_PORT) & 0x01) (void)inb(KEYBOARD_DATA_PORT);
    }

    /* 启用第一个PS/2端口 */
    while (inb(KEYBOARD_STATUS_PORT) & KEYBOARD_STATUS_INPUT_FULL) {}
    outb(KEYBOARD_COMMAND_PORT, 0xAE);

    /* 读-改-写 8042 配置字节：显式打开键盘中断(bit0)、清键盘时钟禁用(bit4)。
     * 旧代码只做了 0xAE，没有碰配置字节 —— 如果 bit0 是 0（QEMU/部分固件
     * 复位后就是），IRQ1 永远不来，键值只能堆在 8042 输出缓冲里，
     * shell / 桌面都读不到：表现为键盘完全无响应，而轮询的鼠标正常。 */
    while (inb(KEYBOARD_STATUS_PORT) & KEYBOARD_STATUS_INPUT_FULL) {}
    outb(KEYBOARD_COMMAND_PORT, 0x20);          /* READ_CONFIG */
    for (int i = 0; i < 100000 && !(inb(KEYBOARD_STATUS_PORT) & 0x01); i++) {}
    if (inb(KEYBOARD_STATUS_PORT) & 0x01) {
        uint8_t cfg = inb(KEYBOARD_DATA_PORT);
        cfg |= 0x01;      /* enable keyboard IRQ1 */
        cfg &= ~0x10;     /* enable keyboard clock */
        while (inb(KEYBOARD_STATUS_PORT) & KEYBOARD_STATUS_INPUT_FULL) {}
        outb(KEYBOARD_COMMAND_PORT, 0x60);      /* WRITE_CONFIG */
        while (inb(KEYBOARD_STATUS_PORT) & KEYBOARD_STATUS_INPUT_FULL) {}
        outb(KEYBOARD_DATA_PORT, cfg);
    }

    /* 打开扫描并把 ACK 读掉，避免残留字节被当成按键 */
    while (inb(KEYBOARD_STATUS_PORT) & KEYBOARD_STATUS_INPUT_FULL) {}
    outb(KEYBOARD_DATA_PORT, 0xF4);
    for (int i = 0; i < 100000 && !(inb(KEYBOARD_STATUS_PORT) & 0x01); i++) {}
    if (inb(KEYBOARD_STATUS_PORT) & 0x01) (void)inb(KEYBOARD_DATA_PORT);

    vga_puts("[KEYBOARD64] Keyboard initialized\n");
}

/* 键盘中断处理 */
volatile int g_kbd_irq_count = 0;

void keyboard64_interrupt(void)
{
    g_kbd_irq_count++;
    uint8_t scancode = inb(KEYBOARD_DATA_PORT);

    /* V0.13.7 P4: 扩展键前缀 (0xE0 = 方向键/Home/End等) */
    if (scancode == 0xE0) {
        pending_e0 = 1;
        return;
    }
    if (pending_e0) {
        pending_e0 = 0;
        /* V0.13.9: Win/Meta 键 (0x5B=左 0x5C=右; 释放=+0x80) */
        if (scancode == 0x5B || scancode == 0x5C) { win_pressed = 1; return; }
        if (scancode == 0xDB || scancode == 0xDC) { win_pressed = 0; return; }
        if (scancode < 0x80) {
            /* 扩展键按下: 映射为内部扩展码 (0x90-0x93, 不与ASCII冲突) */
            uint8_t ext = 0;
            switch (scancode) {
                case 0x48: ext = KEY_EXT_UP;    break;
                case 0x50: ext = KEY_EXT_DOWN;  break;
                case 0x4B: ext = KEY_EXT_LEFT;  break;
                case 0x4D: ext = KEY_EXT_RIGHT; break;
                case 0x49: ext = KEY_EXT_PGUP;  break;
                case 0x51: ext = KEY_EXT_PGDN;  break;
                case 0x47: ext = KEY_EXT_HOME;  break;
                case 0x4F: ext = KEY_EXT_END;   break;
                case 0x53: ext = KEY_EXT_DEL;   break;
                case 0x52: ext = KEY_EXT_INS;   break;
                default: return;
            }
            keyboard_buffer[keyboard_buffer_head] = ext;
            keyboard_buffer_head = (keyboard_buffer_head + 1) % KEYBOARD_BUFFER_SIZE;
        }
        return;
    }

    /* V0.13.46: Ctrl+Alt+K = 强制结束当前用户进程（未响应时唯一可用通道） */
    if (scancode == 0x25 && (scancode < 0x80) && ctrl_pressed && alt_pressed) {
        g_force_kill_key = 1;
    }

    /* 处理特殊键 */
    switch (scancode) {
        case KEY_LSHIFT:
        case KEY_RSHIFT:
            shift_pressed = 1;
            return;
        case KEY_LSHIFT + 0x80:
        case KEY_RSHIFT + 0x80:
            shift_pressed = 0;
            return;
        case KEY_CAPS:
            caps_lock = !caps_lock;
            return;
        case KEY_CTRL:
            ctrl_pressed = 1;
            return;
        case KEY_CTRL + 0x80:
            ctrl_pressed = 0;
            return;
        case KEY_ALT:
            alt_pressed = 1;
            return;
        case KEY_ALT + 0x80:
            alt_pressed = 0;
            return;
    }

    /* 释放键（扫描码 >= 0x80） */
    if (scancode >= 0x80) {
        return;
    }

    /* 转换为ASCII */
    char c = 0;
    if (scancode < sizeof(scancode_to_ascii)) {
        if (shift_pressed) {
            c = scancode_to_ascii_shift[scancode];
        } else {
            c = scancode_to_ascii[scancode];
        }

        /* Caps Lock处理 */
        if (caps_lock && c >= 'a' && c <= 'z') {
            c = c - 'a' + 'A';
        } else if (caps_lock && c >= 'A' && c <= 'Z') {
            c = c - 'A' + 'a';
        }
    }

    /* 加入缓冲区 */
    if (c != 0) {
        keyboard_buffer[keyboard_buffer_head] = (uint8_t)c;
        keyboard_buffer_head = (keyboard_buffer_head + 1) % KEYBOARD_BUFFER_SIZE;
    } else if (scancode >= 0x3B && scancode <= 0x44) {
        /* F1-F10功能键：直接传递扫描码 */
        keyboard_buffer[keyboard_buffer_head] = scancode;
        keyboard_buffer_head = (keyboard_buffer_head + 1) % KEYBOARD_BUFFER_SIZE;
    } else if (scancode == 0x57 || scancode == 0x58) {
        /* F11/F12 (set1): 0x57/0x58 直接传递 */
        keyboard_buffer[keyboard_buffer_head] = scancode;
        keyboard_buffer_head = (keyboard_buffer_head + 1) % KEYBOARD_BUFFER_SIZE;
    } else if (scancode == 0x46 || scancode == 0x45 || scancode == 0x54) {
        /* ScrollLock(0x46) / NumLock(0x45) / SysRq(0x54): 直接传递 */
        keyboard_buffer[keyboard_buffer_head] = scancode;
        keyboard_buffer_head = (keyboard_buffer_head + 1) % KEYBOARD_BUFFER_SIZE;
    }
}

/* 检查是否有字符可用 */
int keyboard64_haschar(void)
{
    return keyboard_buffer_head != keyboard_buffer_tail;
}


/* V0.13.6 P3: 修饰键查询 (与USB HID同一定义: bit0=LCtrl bit1=LShift bit2=LAlt bit5=RShift) */
int keyboard64_get_mod(void)
{
    int mod = 0;
    if (ctrl_pressed)  mod |= 0x01;
    if (shift_pressed) mod |= 0x02;
    if (alt_pressed)   mod |= 0x04;
    if (win_pressed)   mod |= 0x08;
    return mod;
}

/* 读取一个字符（非阻塞） */
int keyboard64_getchar(void)
{
    if (keyboard_buffer_head == keyboard_buffer_tail) {
        return -1;
    }

    uint8_t c = keyboard_buffer[keyboard_buffer_tail];
    keyboard_buffer_tail = (keyboard_buffer_tail + 1) % KEYBOARD_BUFFER_SIZE;
    return c;
}

/* 读取一个字符（阻塞） */
/* V0.13.8 P5.1: 清空键盘缓冲 — 丢弃 PS/2 控制器/设备初始化期间的噪声字节
 * (如 0x41 被误当 F7 关闭侧栏面板), 主循环就绪后调用 */
void keyboard64_flush(void)
{
    keyboard_buffer_head = 0;
    keyboard_buffer_tail = 0;
}

char keyboard64_getchar_blocking(void)
{
    int c;
    while ((c = keyboard64_getchar()) == -1) {
        __asm__ volatile("hlt");
    }
    return (char)c;
}

/* 测试键盘 */
void keyboard64_test(void)
{
    vga_puts("[KEYBOARD64] Test: type something...\n");

    /* 简单的回显测试（限时） */
    for (int i = 0; i < 100; i++) {
        int c = keyboard64_getchar();
        if (c != -1) {
            vga_putchar((char)c);
        }
    }
    vga_puts("\n[KEYBOARD64] Test complete\n");
}

/* V0.13.46: 查询并清除强制结束标志（irq handler 调用） */
int keyboard64_force_kill_triggered(void)
{
    int v = g_force_kill_key;
    g_force_kill_key = 0;
    return v;
}
