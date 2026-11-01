/* ============================================================================
 * 赫普OS - 输入输出单元测试
 * 覆盖：VGA文本/键盘扫描码/串口IO/端口读写/环形缓冲区IO
 * ========================================================================== */

#include <stdint.h>
#include "kunit.h"
#include "vga.h"
#include "string.h"

/* === VGA文本缓冲区测试 === */
#define VGA_BUFFER_ADDR 0xB8000
#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define VGA_COLOR(fg, bg) ((fg) | ((bg) << 4))

static kunit_result_t test_vga_dimensions(void)
{
    KUNIT_ASSERT_EQ(VGA_WIDTH, 80, "VGA 宽度 =80");
    KUNIT_ASSERT_EQ(VGA_HEIGHT, 25, "VGA high 度 =25");
    KUNIT_ASSERT_EQ(VGA_WIDTH * VGA_HEIGHT, 2000, "VGA 字符 count =2000");
    return KUNIT_PASS;
}

static kunit_result_t test_vga_color(void)
{
    /* 黑底白字 */
    u8 color = VGA_COLOR(0xF, 0x0);
    KUNIT_ASSERT_EQ(color, 0x0F, " 黑底白字 =0x0F");

    /* 蓝底黄字 */
    color = VGA_COLOR(0xE, 0x1);
    KUNIT_ASSERT_EQ(color, 0x1E, " 蓝底黄字 =0x1E");

    /* 颜色分量提取 */
    u8 fg = color & 0x0F;
    u8 bg = (color >> 4) & 0x0F;
    KUNIT_ASSERT_EQ(fg, 0xE, " prev 景色 = 黄 ");
    KUNIT_ASSERT_EQ(bg, 0x1, " 背景色 = 蓝 ");
    return KUNIT_PASS;
}

static kunit_result_t test_vga_entry(void)
{
    /* VGA字符项: 低字节ASCII, 高字节颜色 */
    u16 entry = ((u16)'A' & 0xFF) | ((u16)0x0F << 8);
    u8 ch = entry & 0xFF;
    u8 color = (entry >> 8) & 0xFF;

    KUNIT_ASSERT_EQ(ch, 'A', " 字符 ='A'");
    KUNIT_ASSERT_EQ(color, 0x0F, " 颜色 = 黑底白字 ");
    return KUNIT_PASS;
}

static kunit_result_t test_vga_cursor_pos(void)
{
    /* 光标位置计算: row * width + col */
    u32 row = 10, col = 20;
    u32 pos = row * VGA_WIDTH + col;
    KUNIT_ASSERT_EQ(pos, 820, " 光标 bit 置 =820");

    /* 位置反算 */
    u32 r = pos / VGA_WIDTH;
    u32 c = pos % VGA_WIDTH;
    KUNIT_ASSERT_EQ(r, 10, " 行 =10");
    KUNIT_ASSERT_EQ(c, 20, " 列 =20");
    return KUNIT_PASS;
}

/* === 键盘扫描码测试 === */
#define KEY_ESC 0x01
#define KEY_1 0x02
#define KEY_A 0x1E
#define KEY_SPACE 0x39
#define KEY_ENTER 0x1C
#define KEY_BACKSPACE 0x0E
#define KEY_LSHIFT 0x2A
#define KEY_RSHIFT 0x36
#define KEY_RELEASE 0x80

static kunit_result_t test_key_scancodes(void)
{
    KUNIT_ASSERT_EQ(KEY_ESC, 0x01, "ESC scan 码 ");
    KUNIT_ASSERT_EQ(KEY_1, 0x02, "1 键 scan 码 ");
    KUNIT_ASSERT_EQ(KEY_A, 0x1E, "A 键 scan 码 ");
    KUNIT_ASSERT_EQ(KEY_SPACE, 0x39, " empty bars scan 码 ");
    KUNIT_ASSERT_EQ(KEY_ENTER, 0x1C, " 回车 scan 码 ");
    KUNIT_ASSERT_EQ(KEY_BACKSPACE, 0x0E, " 退 bars scan 码 ");
    return KUNIT_PASS;
}

static kunit_result_t test_key_release(void)
{
    /* 释放码 = 按下码 | 0x80 */
    u8 press = KEY_A;
    u8 release = press | KEY_RELEASE;
    KUNIT_ASSERT_EQ(release, 0x9E, "A release 码 =0x9E");

    /* 判断是否释放 */
    KUNIT_ASSERT(release & KEY_RELEASE, " release 标志 bit ");
    KUNIT_ASSERT(!(press & KEY_RELEASE), " 按下 no release 标志 ");
    return KUNIT_PASS;
}

static kunit_result_t test_key_shift(void)
{
    KUNIT_ASSERT_EQ(KEY_LSHIFT, 0x2A, " 左 Shift");
    KUNIT_ASSERT_EQ(KEY_RSHIFT, 0x36, " 右 Shift");

    /* Shift状态 */
    u8 shift_state = 0;
    shift_state |= (1 << 0);  /* 左Shift按下 */
    KUNIT_ASSERT(shift_state & 1, " 左 Shift state ");
    KUNIT_ASSERT(!(shift_state & 2), " 右 Shift not 按下 ");
    return KUNIT_PASS;
}

/* === 扫描码转ASCII测试 === */
static char scancode_to_ascii(u8 scancode, u8 shift)
{
    /* 简化映射表 */
    static const char lower[] = {
        0, 0, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', 0, 0,
        'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', 0, 0,
        'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\',
        'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' '
    };
    if (scancode >= sizeof(lower)) return 0;
    char ch = lower[scancode];
    if (shift && ch >= 'a' && ch <= 'z') ch -= 32;
    return ch;
}

static kunit_result_t test_scancode_to_ascii(void)
{
    KUNIT_ASSERT_EQ(scancode_to_ascii(KEY_A, 0), 'a', "A 键小写 ");
    KUNIT_ASSERT_EQ(scancode_to_ascii(KEY_A, 1), 'A', "A 键大写 ");
    KUNIT_ASSERT_EQ(scancode_to_ascii(KEY_SPACE, 0), ' ', " empty bars ");
    KUNIT_ASSERT_EQ(scancode_to_ascii(KEY_1, 0), '1', " count 字 1");
    return KUNIT_PASS;
}

/* === 串口IO测试 === */
#define COM1_PORT 0x3F8
#define SERIAL_THR 0  /* 发送保持寄存器 */
#define SERIAL_RBR 0  /* 接收缓冲寄存器 */
#define SERIAL_LSR 5  /* 线状态寄存器 */
#define LSR_DATA_READY 0x01
#define LSR_THR_EMPTY 0x20

static kunit_result_t test_serial_ports(void)
{
    KUNIT_ASSERT_EQ(COM1_PORT, 0x3F8, "COM1=0x3F8");
    KUNIT_ASSERT_EQ(SERIAL_LSR, 5, "LSR 偏移 =5");
    KUNIT_ASSERT_EQ(LSR_DATA_READY, 0x01, " data ready bit ");
    KUNIT_ASSERT_EQ(LSR_THR_EMPTY, 0x20, " send empty bit ");
    return KUNIT_PASS;
}

static kunit_result_t test_serial_status_check(void)
{
    /* 模拟状态寄存器 */
    u8 lsr = LSR_DATA_READY | LSR_THR_EMPTY;
    KUNIT_ASSERT(lsr & LSR_DATA_READY, " has data 可读 ");
    KUNIT_ASSERT(lsr & LSR_THR_EMPTY, " 可以 send ");

    lsr = 0;
    KUNIT_ASSERT(!(lsr & LSR_DATA_READY), " no data ");
    KUNIT_ASSERT(!(lsr & LSR_THR_EMPTY), " sender忙 ");
    return KUNIT_PASS;
}

/* === 端口读写测试 === */
static u8 mock_port[0x10000];

static void mock_outb(u16 port, u8 val) { mock_port[port] = val; }
static u8 mock_inb(u16 port) { return mock_port[port]; }

static kunit_result_t test_port_rw(void)
{
    memset(mock_port, 0, sizeof(mock_port));

    mock_outb(0x3F8, 'H');
    KUNIT_ASSERT_EQ(mock_inb(0x3F8), 'H', " port write read ");

    mock_outb(0x3F8 + 5, 0x20);
    KUNIT_ASSERT_EQ(mock_inb(0x3F8 + 5), 0x20, " state port ");
    return KUNIT_PASS;
}

/* === 环形缓冲区IO测试 === */
#define RING_SIZE 256
typedef struct {
    u8  buf[RING_SIZE];
    u32 head;
    u32 tail;
    u32 count;
} ring_io_t;

static void ring_io_init(ring_io_t *r)
{
    r->head = r->tail = r->count = 0;
}
static int ring_io_putc(ring_io_t *r, u8 ch)
{
    if (r->count >= RING_SIZE) return -1;
    r->buf[r->tail] = ch;
    r->tail = (r->tail + 1) % RING_SIZE;
    r->count++;
    return 0;
}
static int ring_io_getc(ring_io_t *r, u8 *ch)
{
    if (r->count <= 0) return -1;
    *ch = r->buf[r->head];
    r->head = (r->head + 1) % RING_SIZE;
    r->count--;
    return 0;
}

static kunit_result_t test_ring_io(void)
{
    ring_io_t ring;
    ring_io_init(&ring);

    /* 写入字符串 */
    const char *msg = "HEPU";
    for (int i = 0; msg[i]; i++) {
        KUNIT_ASSERT_EQ(ring_io_putc(&ring, msg[i]), 0, " write 字符 ");
    }
    KUNIT_ASSERT_EQ(ring.count, 4, " buffer region has 4 字符 ");

    /* 读取验证 */
    u8 ch;
    for (int i = 0; i < 4; i++) {
        KUNIT_ASSERT_EQ(ring_io_getc(&ring, &ch), 0, " read 字符 ");
        KUNIT_ASSERT_EQ(ch, msg[i], " 字符正确 ");
    }
    KUNIT_ASSERT_EQ(ring.count, 0, " buffer region empty ");
    KUNIT_ASSERT_EQ(ring_io_getc(&ring, &ch), -1, " empty buffer region read failed ");
    return KUNIT_PASS;
}

static kunit_result_t test_ring_io_overflow(void)
{
    ring_io_t ring;
    ring_io_init(&ring);

    for (int i = 0; i < RING_SIZE; i++) {
        KUNIT_ASSERT_EQ(ring_io_putc(&ring, (u8)i), 0, " 填 full buffer region ");
    }
    KUNIT_ASSERT_EQ(ring_io_putc(&ring, 0xFF), -1, " full buffer region write failed ");
    return KUNIT_PASS;
}

/* === 测试套件注册 === */
static const kunit_test_t io_tests[] = {
    {"VGA 尺寸 ", test_vga_dimensions, "io"},
    {"VGA 颜色 ", test_vga_color, "io"},
    {"VGA 字符 ", test_vga_entry, "io"},
    {"VGA 光标 bit 置 ", test_vga_cursor_pos, "io"},
    {" keyboard scan 码 ", test_key_scancodes, "io"},
    {" keyboard release 码 ", test_key_release, "io"},
    {" keyboard Shift", test_key_shift, "io"},
    {" scan 码转 ASCII", test_scancode_to_ascii, "io"},
    {" 串口 port ", test_serial_ports, "io"},
    {" 串口 state ", test_serial_status_check, "io"},
    {" port 读写 ", test_port_rw, "io"},
    {" ring IO", test_ring_io, "io"},
    {" ring IO 溢出 ", test_ring_io_overflow, "io"},
};
static const kunit_suite_t kunit_io_suite = {
    " I/O ", io_tests, 13
};

void kunit_io_register(void)
{
    kunit_register_suite(&kunit_io_suite);
    vga_puts("[KUNIT] I/O test suite registered(13 cases)\n");
}
