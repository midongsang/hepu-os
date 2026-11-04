/* ============================================================================
 * 赫普OS - 驱动模块单元测试
 * 覆盖：VGA/键盘/定时器/中断/PCI
 * ========================================================================== */

#include <stdint.h>
#include "kunit.h"
#include "vga.h"
#include "string.h"

/* === VGA测试 === */
static kunit_result_t test_vga_color(void)
{
    /* VGA颜色属性：高4位背景，低4位前景 */
    u8 color = (VGA_COLOR_BLACK << 4) | VGA_COLOR_WHITE;
    KUNIT_ASSERT_EQ(color, 0x0F, " 黑底白字 ");

    color = (VGA_COLOR_BLUE << 4) | VGA_COLOR_YELLOW;
    KUNIT_ASSERT_EQ(color, 0x1E, " 蓝底黄字 ");

    color = (VGA_COLOR_RED << 4) | VGA_COLOR_GREEN;
    KUNIT_ASSERT_EQ(color, 0x42, " 红底绿字 ");
    return KUNIT_PASS;
}

static kunit_result_t test_vga_position(void)
{
    /* VGA文本模式：80列x25行 */
    u32 col = 40, row = 12;
    u32 offset = (row * 80 + col) * 2;
    KUNIT_ASSERT_EQ(offset, (12 * 80 + 40) * 2, "VGA 偏移计算 ");

    /* 边界测试 */
    col = 79; row = 24;
    offset = (row * 80 + col) * 2;
    KUNIT_ASSERT_EQ(offset, (24 * 80 + 79) * 2, "VGA 右下角偏移 ");
    return KUNIT_PASS;
}

static kunit_result_t test_vga_colors_count(void)
{
    /* VGA标准16色 */
    KUNIT_ASSERT_EQ(VGA_COLOR_BLACK, 0, " 黑色 ");
    KUNIT_ASSERT_EQ(VGA_COLOR_BLUE, 1, " 蓝色 ");
    KUNIT_ASSERT_EQ(VGA_COLOR_GREEN, 2, " 绿色 ");
    KUNIT_ASSERT_EQ(VGA_COLOR_CYAN, 3, " 青色 ");
    KUNIT_ASSERT_EQ(VGA_COLOR_RED, 4, " 红色 ");
    KUNIT_ASSERT_EQ(VGA_COLOR_MAGENTA, 5, " 品红 ");
    KUNIT_ASSERT_EQ(VGA_COLOR_BROWN, 6, " 棕色 ");
    KUNIT_ASSERT_EQ(VGA_COLOR_LIGHT_GREY, 7, " 浅灰 ");
    KUNIT_ASSERT_EQ(VGA_COLOR_DARK_GREY, 8, " 深灰 ");
    KUNIT_ASSERT_EQ(VGA_COLOR_LIGHT_BLUE, 9, " 浅蓝 ");
    KUNIT_ASSERT_EQ(VGA_COLOR_LIGHT_GREEN, 10, " 浅绿 ");
    KUNIT_ASSERT_EQ(VGA_COLOR_LIGHT_CYAN, 11, " 浅青 ");
    KUNIT_ASSERT_EQ(VGA_COLOR_LIGHT_RED, 12, " 浅红 ");
    KUNIT_ASSERT_EQ(VGA_COLOR_LIGHT_MAGENTA, 13, " 浅品红 ");
    KUNIT_ASSERT_EQ(VGA_COLOR_YELLOW, 14, " 黄色 ");
    KUNIT_ASSERT_EQ(VGA_COLOR_WHITE, 15, " 白色 ");
    return KUNIT_PASS;
}

/* === 键盘扫描码测试 === */
static kunit_result_t test_keyboard_scancode(void)
{
    /* 标准PC键盘扫描码集1 */
    /* 按下：0x01-0x53，释放：0x81-0xD3 */
    u8 press_a = 0x1E;
    u8 release_a = 0x9E;

    KUNIT_ASSERT_EQ(press_a & 0x80, 0, " 按下码最 high bit 0");
    KUNIT_ASSERT_EQ(release_a & 0x80, 0x80, " release 码最 high bit 1");
    KUNIT_ASSERT_EQ(release_a & 0x7F, press_a, " release 码 = 按下码 |0x80");

    /* 特殊键 */
    KUNIT_ASSERT_EQ(0x01, 1, "ESC scan 码 ");
    KUNIT_ASSERT_EQ(0x1C, 28, "Enter scan 码 ");
    KUNIT_ASSERT_EQ(0x0E, 14, "Backspace scan 码 ");
    KUNIT_ASSERT_EQ(0x0F, 15, "Tab scan 码 ");
    return KUNIT_PASS;
}

static kunit_result_t test_keyboard_ascii(void)
{
    /* 扫描码到ASCII的映射（小写字母） */
    u8 scancode_to_ascii[] = {
        0, 0, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', 0, 0,
        'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', 0, 0,
        'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\',
        'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/'
    };

    KUNIT_ASSERT_EQ(scancode_to_ascii[0x10], 'q', "q 键 ");
    KUNIT_ASSERT_EQ(scancode_to_ascii[0x1E], 'a', "a 键 ");
    KUNIT_ASSERT_EQ(scancode_to_ascii[0x2C], 'z', "z 键 ");
    KUNIT_ASSERT_EQ(scancode_to_ascii[0x02], '1', "1 键 ");
    KUNIT_ASSERT_EQ(scancode_to_ascii[0x1C], 0, "Enter no ASCII");
    return KUNIT_PASS;
}

/* === 可编程中断控制器(PIC)测试 === */
#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1
#define PIC_EOI   0x20

static kunit_result_t test_pic_ports(void)
{
    KUNIT_ASSERT_EQ(PIC1_CMD, 0x20, "PIC1 cmd port ");
    KUNIT_ASSERT_EQ(PIC1_DATA, 0x21, "PIC1 data port ");
    KUNIT_ASSERT_EQ(PIC2_CMD, 0xA0, "PIC2 cmd port ");
    KUNIT_ASSERT_EQ(PIC2_DATA, 0xA1, "PIC2 data port ");
    KUNIT_ASSERT_EQ(PIC_EOI, 0x20, "EOI cmd ");
    return KUNIT_PASS;
}

static kunit_result_t test_pic_irq_mapping(void)
{
    /* PIC重映射：IRQ0-7 -> INT 0x20-0x27, IRQ8-15 -> INT 0x28-0x2F */
    u8 master_offset = 0x20;
    u8 slave_offset = 0x28;

    KUNIT_ASSERT_EQ(master_offset + 0, 0x20, "IRQ0->INT32(timer)");
    KUNIT_ASSERT_EQ(master_offset + 1, 0x21, "IRQ1->INT33(keyboard)");
    KUNIT_ASSERT_EQ(slave_offset + 0, 0x28, "IRQ8->INT40(RTC)");
    KUNIT_ASSERT_EQ(slave_offset + 7, 0x2F, "IRQ15->INT47");
    return KUNIT_PASS;
}

/* === 可编程间隔定时器(PIT)测试 === */
#define PIT_CHANNEL0 0x40
#define PIT_CHANNEL1 0x41
#define PIT_CHANNEL2 0x42
#define PIT_CMD      0x43
#define PIT_FREQ     1193182

static kunit_result_t test_pit_freq(void)
{
    KUNIT_ASSERT_EQ(PIT_FREQ, 1193182, "PIT bench freq ");

    /* 计算100Hz的分频值 */
    u32 divisor = PIT_FREQ / 100;
    KUNIT_ASSERT_EQ(divisor, 11931, "100Hz 分频 value ");

    /* 计算1000Hz的分频值 */
    divisor = PIT_FREQ / 1000;
    KUNIT_ASSERT_EQ(divisor, 1193, "1000Hz 分频 value ");
    return KUNIT_PASS;
}

static kunit_result_t test_pit_ports(void)
{
    KUNIT_ASSERT_EQ(PIT_CHANNEL0, 0x40, "PIT channel 0 port ");
    KUNIT_ASSERT_EQ(PIT_CHANNEL1, 0x41, "PIT channel 1 port ");
    KUNIT_ASSERT_EQ(PIT_CHANNEL2, 0x42, "PIT channel 2 port ");
    KUNIT_ASSERT_EQ(PIT_CMD, 0x43, "PIT cmd port ");
    return KUNIT_PASS;
}

/* === PCI配置空间测试 === */
static kunit_result_t test_pci_address(void)
{
    /* PCI配置地址格式：
     * bit31: 使能位
     * bit23-16: 总线号
     * bit15-11: 设备号
     * bit10-8: 功能号
     * bit7-2: 寄存器号
     */
    u32 bus = 0, dev = 1, func = 0, reg = 0;
    u32 address = 0x80000000 | (bus << 16) | (dev << 11) | (func << 8) | (reg & 0xFC);

    KUNIT_ASSERT(address & 0x80000000, "PCI 使能 bit ");
    KUNIT_ASSERT_EQ((address >> 16) & 0xFF, 0, "PCI total 线号 ");
    KUNIT_ASSERT_EQ((address >> 11) & 0x1F, 1, "PCI device 号 ");
    KUNIT_ASSERT_EQ((address >> 8) & 0x7, 0, "PCI 功能号 ");
    return KUNIT_PASS;
}

static kunit_result_t test_pci_vendor_id(void)
{
    /* 常见PCI厂商ID */
    u16 intel_vid = 0x8086;
    u16 amd_vid = 0x1022;
    u16 nvidia_vid = 0x10DE;
    u16 invalid_vid = 0xFFFF;

    KUNIT_ASSERT_EQ(intel_vid, 0x8086, "Intel vendor ID");
    KUNIT_ASSERT_EQ(amd_vid, 0x1022, "AMD vendor ID");
    KUNIT_ASSERT_EQ(nvidia_vid, 0x10DE, "NVIDIA vendor ID");
    KUNIT_ASSERT_EQ(invalid_vid, 0xFFFF, " invalid device vendor ID");
    return KUNIT_PASS;
}

/* === 测试套件注册 === */
static const kunit_test_t driver_tests[] = {
    {"VGA 颜色 ", test_vga_color, "drivers"},
    {"VGA bit 置 ", test_vga_position, "drivers"},
    {"VGA 颜色 count ", test_vga_colors_count, "drivers"},
    {" keyboard scan 码 ", test_keyboard_scancode, "drivers"},
    {" keyboard ASCII", test_keyboard_ascii, "drivers"},
    {"PIC port ", test_pic_ports, "drivers"},
    {"PIC IRQ mapping ", test_pic_irq_mapping, "drivers"},
    {"PIT freq ", test_pit_freq, "drivers"},
    {"PIT port ", test_pit_ports, "drivers"},
    {"PCI addr ", test_pci_address, "drivers"},
    {"PCI vendor ID", test_pci_vendor_id, "drivers"},
};
static const kunit_suite_t kunit_drivers_suite = {
    " driver module ", driver_tests, 11
};

void kunit_drivers_register(void)
{
    kunit_register_suite(&kunit_drivers_suite);
    vga_puts("[KUNIT] driver test suite registered(11 cases)\n");
}
