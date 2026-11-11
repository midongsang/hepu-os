/* ============================================================================
 * 赫普OS 64位 - PIT可编程间隔定时器
 * 提供系统时钟中断，用于调度器时间片和计时
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>

extern void vga_puts(const char *s);
extern void vga_put_hex(uint64_t val);

/* PIT端口 */
#define PIT_CHANNEL0    0x40
#define PIT_CHANNEL1    0x41
#define PIT_CHANNEL2    0x42
#define PIT_COMMAND     0x43

/* PIT命令字 */
#define PIT_BCD         0x01  /* BCD模式 */
#define PIT_MODE0       0x00  /* 中断结束计数 */
#define PIT_MODE2       0x04  /* 速率发生器 */
#define PIT_MODE3       0x06  /* 方波发生器 */
#define PIT_RL_LOHI     0x30  /* 先低字节后高字节 */
#define PIT_CHANNEL0_SEL 0x00 /* 选择通道0 */

/* PIT基础频率 */
#define PIT_BASE_FREQ   1193182  /* Hz */

/* 目标频率 */
#define PIT_TARGET_FREQ 1000    /* 1ms中断一次 */

/* 全局滴答计数 */
static volatile uint64_t pit_ticks = 0;
static volatile uint64_t pit_seconds = 0;
static int pit_initialized = 0;

/* 端口IO */
static inline void outb(uint16_t port, uint8_t val)
{
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/* PIT中断处理函数 */
void pit64_interrupt_handler(void)
{
    pit_ticks++;
    if (pit_ticks % PIT_TARGET_FREQ == 0) {
        pit_seconds++;
    }
}

/* 初始化PIT */
void pit64_init(void)
{
    /* 计算分频值 */
    uint16_t divisor = (uint16_t)(PIT_BASE_FREQ / PIT_TARGET_FREQ);

    /* 设置命令字：通道0，先低后高，模式3（方波） */
    outb(PIT_COMMAND, PIT_CHANNEL0_SEL | PIT_RL_LOHI | PIT_MODE3);

    /* 设置分频值（先低字节，后高字节） */
    outb(PIT_CHANNEL0, (uint8_t)(divisor & 0xFF));
    outb(PIT_CHANNEL0, (uint8_t)((divisor >> 8) & 0xFF));

    pit_ticks = 0;
    pit_seconds = 0;
    pit_initialized = 1;

    vga_puts("[PIT64] Programmable Interval Timer initialized\n");
    vga_puts("[PIT64] Frequency: ");
    char buf[16];
    int freq = PIT_TARGET_FREQ;
    int i = 0;
    if (freq == 0) buf[i++] = '0';
    while (freq > 0) { buf[i++] = '0' + freq % 10; freq /= 10; }
    buf[i] = 0;
    for (int j = 0; j < i / 2; j++) {
        char t = buf[j]; buf[j] = buf[i-1-j]; buf[i-1-j] = t;
    }
    vga_puts(buf);
    vga_puts(" Hz (1ms tick)\n");
}

/* 获取当前滴答数 */
uint64_t pit64_get_ticks(void)
{
    return pit_ticks;
}

/* 保存最近用户态RIP (供看门狗调试) */
uint64_t g_last_user_rip = 0;

/* PIT中断处理函数（从IRQ0调用） */
void pit64_interrupt(void)
{
    pit_ticks++;
    if (pit_ticks % 1000 == 0) {
        pit_seconds++;
    }

    /* V0.55.2: 删除 V0.20.99 遗留的调试栈转储 —— 它在 tick 5001~5009 无条件
     * 打印 9 行 24 字的栈快照, 每次启动污染串口日志且无诊断价值。 */

    /* TCP 重传/超时定时器（原调用点 irq64.c 未参与构建，导致定时器永不触发） */
    extern void net64_tcp_timer(void);
    net64_tcp_timer();

    /* 接入调度器：时间片递减 */
    extern void process64_tick(void);
    process64_tick();
}

/* 获取当前秒数 */
uint64_t pit64_get_seconds(void)
{
    return pit_seconds;
}

/* 获取毫秒数 */
uint64_t pit64_get_ms(void)
{
    return pit_ticks;  /* 1 tick = 1ms */
}

/* 简单延时（毫秒） */
void pit64_delay_ms(uint64_t ms)
{
    uint64_t start = pit_ticks;
    while (pit_ticks - start < ms) {
        __asm__ volatile ("hlt");  /* 等待中断 */
    }
}

/* 简单延时（秒） */
void pit64_delay_s(uint64_t seconds)
{
    pit64_delay_ms(seconds * 1000);
}

/* 获取系统运行时间（秒） */
uint64_t pit64_get_uptime(void)
{
    return pit_seconds;
}

/* 重置定时器 */
void pit64_reset(void)
{
    pit_ticks = 0;
    pit_seconds = 0;
}

/* 检查PIT是否已初始化 */
int pit64_is_initialized(void)
{
    return pit_initialized;
}
