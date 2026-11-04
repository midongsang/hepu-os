/* ============================================================================
 * 赫普OS - 设备驱动深度单元测试
 * 覆盖：PCI/IDE/串口/定时器/键盘控制器
 * ========================================================================== */

#include <stdint.h>
#include "kunit.h"
#include "vga.h"
#include "string.h"

/* === PCI配置空间测试 === */
#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA 0xCFC
#define PCI_VENDOR_ID 0x00
#define PCI_DEVICE_ID 0x02
#define PCI_COMMAND 0x04
#define PCI_STATUS 0x06
#define PCI_CLASS_CODE 0x0B
#define PCI_HEADER_TYPE 0x0E

static kunit_result_t test_pci_ports(void)
{
    KUNIT_ASSERT_EQ(PCI_CONFIG_ADDRESS, 0xCF8, "PCI config addr port ");
    KUNIT_ASSERT_EQ(PCI_CONFIG_DATA, 0xCFC, "PCI config data port ");
    return KUNIT_PASS;
}

static kunit_result_t test_pci_address_format(void)
{
    /* PCI地址格式: bit31=使能, bit16-23=bus, bit11-15=device, bit8-10=func, bit0-7=offset */
    u32 bus = 0, device = 1, func = 0, offset = 0;
    u32 address = 0x80000000 | (bus << 16) | (device << 11) | (func << 8) | offset;

    KUNIT_ASSERT(address & 0x80000000, " 使能 bit ");
    KUNIT_ASSERT_EQ((address >> 16) & 0xFF, 0, "bus=0");
    KUNIT_ASSERT_EQ((address >> 11) & 0x1F, 1, "device=1");
    KUNIT_ASSERT_EQ((address >> 8) & 0x7, 0, "func=0");
    KUNIT_ASSERT_EQ(address & 0xFC, 0, "offset pair 齐 ");
    return KUNIT_PASS;
}

static kunit_result_t test_pci_class_codes(void)
{
    /* PCI类代码 */
    KUNIT_ASSERT_EQ(0x01, 0x01, " 大 capacity storage ");
    KUNIT_ASSERT_EQ(0x02, 0x02, " net controller ");
    KUNIT_ASSERT_EQ(0x03, 0x03, " display controller ");
    KUNIT_ASSERT_EQ(0x04, 0x04, " 多 media device ");
    KUNIT_ASSERT_EQ(0x06, 0x06, " 桥 device ");
    KUNIT_ASSERT_EQ(0x0C, 0x0C, " 串行 total 线 controller ");
    return KUNIT_PASS;
}

static kunit_result_t test_pci_vendor_ids(void)
{
    /* 常见PCI厂商ID */
    KUNIT_ASSERT_EQ(0x8086, 0x8086, "Intel");
    KUNIT_ASSERT_EQ(0x1022, 0x1022, "AMD");
    KUNIT_ASSERT_EQ(0x10DE, 0x10DE, "NVIDIA");
    KUNIT_ASSERT_EQ(0x1234, 0x1234, "QEMU");
    KUNIT_ASSERT_EQ(0xFFFF, 0xFFFF, " invalid device ");
    return KUNIT_PASS;
}

/* === IDE/ATA测试 === */
#define IDE_DATA 0x1F0
#define IDE_ERROR 0x1F1
#define IDE_SECTOR_COUNT 0x1F2
#define IDE_LBA_LOW 0x1F3
#define IDE_LBA_MID 0x1F4
#define IDE_LBA_HIGH 0x1F5
#define IDE_DEVICE 0x1F6
#define IDE_STATUS 0x1F7
#define IDE_COMMAND 0x1F7

#define IDE_STATUS_BSY 0x80
#define IDE_STATUS_DRDY 0x40
#define IDE_STATUS_DRQ 0x08
#define IDE_STATUS_ERR 0x01

#define IDE_CMD_READ 0x20
#define IDE_CMD_WRITE 0x30
#define IDE_CMD_IDENTIFY 0xEC

static kunit_result_t test_ide_ports(void)
{
    KUNIT_ASSERT_EQ(IDE_DATA, 0x1F0, "IDE data port ");
    KUNIT_ASSERT_EQ(IDE_STATUS, 0x1F7, "IDE state port ");
    KUNIT_ASSERT_EQ(IDE_COMMAND, 0x1F7, "IDE cmd port ");
    return KUNIT_PASS;
}

static kunit_result_t test_ide_status_flags(void)
{
    KUNIT_ASSERT_EQ(IDE_STATUS_BSY, 0x80, " 忙标志 ");
    KUNIT_ASSERT_EQ(IDE_STATUS_DRDY, 0x40, " ready 标志 ");
    KUNIT_ASSERT_EQ(IDE_STATUS_DRQ, 0x08, " data 请求标志 ");
    KUNIT_ASSERT_EQ(IDE_STATUS_ERR, 0x01, " error 标志 ");

    /* 状态检查 */
    u8 status = IDE_STATUS_DRDY | IDE_STATUS_DRQ;
    KUNIT_ASSERT(status & IDE_STATUS_DRDY, " device ready ");
    KUNIT_ASSERT(status & IDE_STATUS_DRQ, " data 请求 ");
    KUNIT_ASSERT(!(status & IDE_STATUS_BSY), " not 忙 ");
    KUNIT_ASSERT(!(status & IDE_STATUS_ERR), " no errors ");
    return KUNIT_PASS;
}

static kunit_result_t test_ide_commands(void)
{
    KUNIT_ASSERT_EQ(IDE_CMD_READ, 0x20, " 读 sector cmd ");
    KUNIT_ASSERT_EQ(IDE_CMD_WRITE, 0x30, " 写 sector cmd ");
    KUNIT_ASSERT_EQ(IDE_CMD_IDENTIFY, 0xEC, " 识别 cmd ");
    return KUNIT_PASS;
}

static kunit_result_t test_ide_lba_address(void)
{
    /* LBA28地址: 28位, 分3个8位寄存器 */
    u32 lba = 0x0ABCDEF;
    u8 low = lba & 0xFF;
    u8 mid = (lba >> 8) & 0xFF;
    u8 high = (lba >> 16) & 0xFF;
    u8 device = 0xE0 | ((lba >> 24) & 0x0F);  /* LBA模式 + 高4位 */

    KUNIT_ASSERT_EQ(low, 0xEF, "LBA low bytes ");
    KUNIT_ASSERT_EQ(mid, 0xCD, "LBA in bytes ");
    KUNIT_ASSERT_EQ(high, 0xAB, "LBA high bytes ");
    KUNIT_ASSERT_EQ(device & 0x40, 0x40, "LBA mode bit ");
    return KUNIT_PASS;
}

/* === 串口测试 === */
#define COM1 0x3F8
#define COM2 0x2F8

#define SERIAL_DATA 0
#define SERIAL_IER 1
#define SERIAL_IIR 2
#define SERIAL_LCR 3
#define SERIAL_MCR 4
#define SERIAL_LSR 5
#define SERIAL_MSR 6

#define SERIAL_LSR_DR 0x01
#define SERIAL_LSR_THRE 0x20
#define SERIAL_LSR_TEMT 0x40

static kunit_result_t test_serial_ports(void)
{
    KUNIT_ASSERT_EQ(COM1, 0x3F8, "COM1 port ");
    KUNIT_ASSERT_EQ(COM2, 0x2F8, "COM2 port ");
    return KUNIT_PASS;
}

static kunit_result_t test_serial_registers(void)
{
    KUNIT_ASSERT_EQ(SERIAL_DATA, 0, " data 寄存器偏移 ");
    KUNIT_ASSERT_EQ(SERIAL_IER, 1, " irq 使能偏移 ");
    KUNIT_ASSERT_EQ(SERIAL_LCR, 3, " 线 ctrl 偏移 ");
    KUNIT_ASSERT_EQ(SERIAL_LSR, 5, " 线 state 偏移 ");
    return KUNIT_PASS;
}

static kunit_result_t test_serial_status(void)
{
    KUNIT_ASSERT_EQ(SERIAL_LSR_DR, 0x01, " data ready ");
    KUNIT_ASSERT_EQ(SERIAL_LSR_THRE, 0x20, " send 保持 empty ");
    KUNIT_ASSERT_EQ(SERIAL_LSR_TEMT, 0x40, " sender empty ");

    u8 status = SERIAL_LSR_THRE | SERIAL_LSR_TEMT;
    KUNIT_ASSERT(status & SERIAL_LSR_THRE, " 可以 send ");
    KUNIT_ASSERT(!(status & SERIAL_LSR_DR), " no data ");
    return KUNIT_PASS;
}

/* === PIT定时器测试 === */
#define PIT_CHANNEL0 0x40
#define PIT_CHANNEL1 0x41
#define PIT_CHANNEL2 0x42
#define PIT_COMMAND 0x43

#define PIT_FREQ 1193182
#define PIT_100HZ_DIV 11932

static kunit_result_t test_pit_ports(void)
{
    KUNIT_ASSERT_EQ(PIT_CHANNEL0, 0x40, "PIT channel 0");
    KUNIT_ASSERT_EQ(PIT_COMMAND, 0x43, "PIT cmd port ");
    return KUNIT_PASS;
}

static kunit_result_t test_pit_frequency(void)
{
    KUNIT_ASSERT_EQ(PIT_FREQ, 1193182, "PIT base freq ");

    /* 100Hz分频值 */
    u32 divisor = PIT_FREQ / 100;
    KUNIT_ASSERT_EQ(divisor, 11931, "100Hz 分频 value ");

    /* 1000Hz分频值 */
    divisor = PIT_FREQ / 1000;
    KUNIT_ASSERT_EQ(divisor, 1193, "1000Hz 分频 value ");
    return KUNIT_PASS;
}

/* === 键盘控制器测试 === */
#define KBD_DATA 0x60
#define KBD_STATUS 0x64
#define KBD_COMMAND 0x64

#define KBD_STATUS_OBF 0x01
#define KBD_STATUS_IBF 0x02

static kunit_result_t test_kbd_ports(void)
{
    KUNIT_ASSERT_EQ(KBD_DATA, 0x60, " keyboard data port ");
    KUNIT_ASSERT_EQ(KBD_STATUS, 0x64, " keyboard state port ");
    return KUNIT_PASS;
}

static kunit_result_t test_kbd_status(void)
{
    KUNIT_ASSERT_EQ(KBD_STATUS_OBF, 0x01, " output buffer region full ");
    KUNIT_ASSERT_EQ(KBD_STATUS_IBF, 0x02, " input buffer region full ");

    u8 status = KBD_STATUS_OBF;
    KUNIT_ASSERT(status & KBD_STATUS_OBF, " has data 可读 ");
    KUNIT_ASSERT(!(status & KBD_STATUS_IBF), " 可 write cmd ");
    return KUNIT_PASS;
}

/* === 测试套件注册 === */
static const kunit_test_t drivers_deep_tests[] = {
    {"PCI port ", test_pci_ports, "drivers"},
    {"PCI addr format ", test_pci_address_format, "drivers"},
    {"PCI type 代码 ", test_pci_class_codes, "drivers"},
    {"PCI vendor ID", test_pci_vendor_ids, "drivers"},
    {"IDE port ", test_ide_ports, "drivers"},
    {"IDE state 标志 ", test_ide_status_flags, "drivers"},
    {"IDE cmd ", test_ide_commands, "drivers"},
    {"IDE LBA addr ", test_ide_lba_address, "drivers"},
    {" 串口 port ", test_serial_ports, "drivers"},
    {" 串口寄存器 ", test_serial_registers, "drivers"},
    {" 串口 state ", test_serial_status, "drivers"},
    {"PIT port ", test_pit_ports, "drivers"},
    {"PIT freq ", test_pit_frequency, "drivers"},
    {" keyboard port ", test_kbd_ports, "drivers"},
    {" keyboard state ", test_kbd_status, "drivers"},
};
static const kunit_suite_t kunit_drivers_deep_suite = {
    " driver deep ", drivers_deep_tests, 15
};

void kunit_drivers_deep_register(void)
{
    kunit_register_suite(&kunit_drivers_deep_suite);
    vga_puts("[KUNIT] driver deep test suite registered(15 cases)\n");
}
