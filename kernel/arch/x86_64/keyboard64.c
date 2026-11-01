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

    /* 启用键盘 */
    outb(KEYBOARD_COMMAND_PORT, 0xAE);  /* 启用第一个PS/2端口 */

    /* 等待 */
    while (inb(KEYBOARD_STATUS_PORT) & KEYBOARD_STATUS_INPUT_FULL) {}

    /* 设置LED（关闭所有LED） */
    outb(KEYBOARD_DATA_PORT, 0xED);
    while (inb(KEYBOARD_STATUS_PORT) & KEYBOARD_STATUS_INPUT_FULL) {}
    outb(KEYBOARD_DATA_PORT, 0x00);

    vga_puts("[KEYBOARD64] Keyboard initialized\n");
}

/* 键盘中断处理 */
volatile int g_kbd_irq_count = 0;

void keyboard64_interrupt(void)
{
    g_kbd_irq_count++;
    uint8_t scancode = inb(KEYBOARD_DATA_PORT);

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
        /* F1-F10等功能键：直接传递扫描码 */
        keyboard_buffer[keyboard_buffer_head] = scancode;
        keyboard_buffer_head = (keyboard_buffer_head + 1) % KEYBOARD_BUFFER_SIZE;
    }
}

/* 检查是否有字符可用 */
int keyboard64_haschar(void)
{
    return keyboard_buffer_head != keyboard_buffer_tail;
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
