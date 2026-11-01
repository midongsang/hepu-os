/* ============================================================================
 * 赫普OS - PS/2 键盘驱动 (IRQ1)
 * 功能: 扫描码转换、输入缓冲区、readline
 * ========================================================================== */

#include <stdint.h>
#include "keyboard.h"
#include "pic.h"
#include "vga.h"
#include "power.h"

#define KEYBOARD_DATA_PORT 0x60
#define KEYBOARD_STATUS_PORT 0x64
#define KEYBOARD_CMD_PORT 0x64

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

/* 输入缓冲区 */
static char keyboard_buffer[KEYBOARD_BUFFER_SIZE];
static int  buffer_head = 0;  /* 写入位置 */
static int  buffer_tail = 0;  /* 读取位置 */
static int  buffer_count = 0;

/* Shift键状态 */
static int shift_pressed = 0;
static int ctrl_pressed = 0;
static int alt_pressed = 0;
static int caps_lock = 0;
static int extended_key = 0;   /* V2.23: 0xE0 扩展标记状态 */

/* 扫描码 -> ASCII 转换表 (Set 1, 无Shift) */
static const char scancode_ascii[] = {
    0,    0x1B, '1',  '2',  '3',  '4',  '5',  '6',  /* 0x00-0x07  [1]=ESC V2.45 */
    '7',  '8',  '9',  '0',  '-',  '=',  0x08, 0x09, /* 0x08-0x0F */
    'q',  'w',  'e',  'r',  't',  'y',  'u',  'i',  /* 0x10-0x17 */
    'o',  'p',  '[',  ']',  0x0A, 0,    'a',  's',  /* 0x18-0x1F */
    'd',  'f',  'g',  'h',  'j',  'k',  'l',  ';',  /* 0x20-0x27 */
    '\'', '`',  0,    '\\', 'z',  'x',  'c',  'v',  /* 0x28-0x2F */
    'b',  'n',  'm',  ',',  '.',  '/',  0,    '*',  /* 0x30-0x37 */
    0,    ' ',  0,    0,    0,    0,    0,    0,    /* 0x38-0x3F */
    0,    0,    0,    0,    0,    0,    0,    '7',  /* 0x40-0x47 */
    '8',  '9',  '-',  '4',  '5',  '6',  '+',  '1',  /* 0x48-0x4F */
    '2',  '3',  '0',  '.',  0,    0,    0,    0,    /* 0x50-0x57 */
};

/* Shift 状态下的扫描码表 */
static const char scancode_ascii_shift[] = {
    0,    0x1B, '!',  '@',  '#',  '$',  '%',  '^',
    '&',  '*',  '(',  ')',  '_',  '+',  0x08, 0x09,
    'Q',  'W',  'E',  'R',  'T',  'Y',  'U',  'I',
    'O',  'P',  '{',  '}',  0x0A, 0,    'A',  'S',
    'D',  'F',  'G',  'H',  'J',  'K',  'L',  ':',
    '"', '~', 0, '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0, '*', 0, ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '7', '8', '9', '-', '4', '5', '6', '+', '1', '2', '3', '0', '.', 0, 0, 0, 0, }; /* 向 buffer region write 一 字符 */ static void buffer_put(char c) { if(buffer_count >= KEYBOARD_BUFFER_SIZE) return; keyboard_buffer[buffer_head] = c; buffer_head =(buffer_head + 1) % KEYBOARD_BUFFER_SIZE; buffer_count++; } /* V2.22: 8042 控制器等待辅助(真机必须完整初始化, 否则 BIOS 引导后 8042 特殊状态
 * 导致 IRQ1 不产生/输出缓冲残留 → 键盘无法输入; QEMU 下无害) */
static void kb_wait_input_empty(void)
{
    for (int i = 0; i < 100000; i++) {
        if (!(inb(KEYBOARD_STATUS_PORT) & 0x02)) return;  /* bit1=0 输入缓冲空 */
    }
}
static void kb_flush_output(void)
{
    for (int i = 0; i < 100; i++) {
        if (inb(KEYBOARD_STATUS_PORT) & 0x01) {           /* bit0=1 输出缓冲满 */
            inb(KEYBOARD_DATA_PORT);                       /* 丢弃残留scancode */
        } else {
            return;
        }
    }
}
/* V3.87-E8: 原始扫描码环形缓冲——顶半部只收数据, 转换在底半部(softirq) */
#define RAW_BUF_SIZE 64
static uint8_t raw_buf[RAW_BUF_SIZE];
static volatile int raw_head = 0, raw_tail = 0, raw_count = 0;

static void raw_put(uint8_t sc)
{
    if (raw_count >= RAW_BUF_SIZE) return;  /* 满则丢(顶半部必须快) */
    raw_buf[raw_head] = sc;
    raw_head = (raw_head + 1) % RAW_BUF_SIZE;
    raw_count++;
}

/* keyboard irq handling —— 顶半部: 只读尽扫描码入 raw 缓冲 + 触发底半部 */
void keyboard_handler(void)
{
    extern void iopolicy_irq(int);
    iopolicy_irq(0);   /* IO_DEV_KBD: 中断计数 (E-47) */
    /* V2.25: 循环读尽 0x60 所有待处理扫描码, 防止键盘控制器FIFO锁死.
     * 只读一个就返回会丢后续扫描码, 且控制器持续置输出缓冲满.
     * V3.89-E19: 唤醒不在中断上下文做(pm_wakeup 会写 VGA/回调, 中断里
     * 可能死锁)——由 softirq_kbd_poll 底半部在主循环执行唤醒. */
    /* V3.87-E5: 超时保护——8042 异常(状态位常1)时防止中断里永久忙等 */
    uint32_t kb_guard = 0;
    while (inb(KEYBOARD_STATUS_PORT) & 0x01) {
        if (++kb_guard > 100000) break;
        uint8_t scancode = inb(KEYBOARD_DATA_PORT);
        raw_put(scancode);
        if (raw_count == 1) {
            extern void softirq_raise(int);
            softirq_raise(0);   /* V3.87-E8: 底半部待执行 */
        }
    }
}

/* V3.87-E8: 底半部——扫描码→ASCII 状态机(原 keyboard_handler 转换逻辑,
 * 从中断上下文移出, 由 softirq_run 在主循环执行) */
static void softirq_kbd_poll(void)
{
    /* V3.89-E19: 底半部在主循环执行——先处理唤醒, 避免中断上下文
     * 调用 pm_wakeup(VGA 写/回调)造成死锁. */
    extern int pm_get_state(void);
    extern void pm_wakeup_reason(int);
    if (pm_get_state() != 0) {   /* PM_STATE_ON == 0 */
        pm_wakeup_reason(1);     /* V4.49-E94: 键盘唤醒原因 */
    }
    while (raw_count > 0) {
        uint8_t scancode = raw_buf[raw_tail];
        raw_tail = (raw_tail + 1) % RAW_BUF_SIZE;
        raw_count--;

        /* 0xE0 扩展标记: 下一个是扩展键(方向键/编辑键) */
        if (scancode == 0xE0) { extended_key = 1; continue; }
        if (extended_key) {
            extended_key = 0;
            /* 方向键: Up=0x48, Down=0x50, Left=0x4B, Right=0x4D */
            if (scancode == 0x48) buffer_put(0x91);  /* Up  */
            else if (scancode == 0x50) buffer_put(0x92); /* Down */
            continue;
        }

        /* release 键(0x80 起) */
        if (scancode & 0x80) {
            uint8_t key = scancode & 0x7F;
            if (key == 0x2A || key == 0x36) shift_pressed = 0;
            else if (key == 0x1D) ctrl_pressed = 0;
            else if (key == 0x38) alt_pressed = 0;
            continue;
        }

        /* 按下键 */
        switch (scancode) {
            case 0x2A: case 0x36: shift_pressed = 1; continue;
            case 0x1D: ctrl_pressed = 1; continue;
            case 0x38: alt_pressed = 1; continue;
            case 0x3A: caps_lock = !caps_lock; continue;
            case 0x0E: buffer_put(0x08); continue;
            case 0x53:
                if (ctrl_pressed && alt_pressed) {
                    vga_puts("[KEYBOARD] Ctrl+Alt+Del -> reboot\n");
                    machine_reboot();
                    continue;
                }
                break;
        }

        /* 转换 scan 码 */
        char c;
        if (shift_pressed) {
            c = (scancode < sizeof(scancode_ascii_shift)) ? scancode_ascii_shift[scancode] : 0;
        } else {
            c = (scancode < sizeof(scancode_ascii)) ? scancode_ascii[scancode] : 0;
        }
        if (caps_lock && c >= 'a' && c <= 'z') c = c - 'a' + 'A';
        else if (caps_lock && c >= 'A' && c <= 'Z' && !shift_pressed) c = c - 'A' + 'a';
        if (c) buffer_put(c);
    }
}
/* V4.04-E49: 清空键盘残留(raw_buf + 输入缓冲)——pm_sleep 进入
 * 深度睡眠前调用, 防止睡眠前的按键释放码/残留扫描码立即唤醒. */
void keyboard_flush_raw(void)
{
    raw_head = 0; raw_tail = 0; raw_count = 0;
    buffer_head = 0; buffer_tail = 0; buffer_count = 0;
}

void keyboard_init(void) { buffer_head = 0; buffer_tail = 0; buffer_count = 0; shift_pressed = 0; caps_lock = 0; ctrl_pressed = 0; alt_pressed = 0; extended_key = 0; /* V2.22: 完整8042初始化——真机BIOS引导后键盘可能被禁用/缓冲残留, 必须: 等待空闲→禁用→清缓冲→启用→发0xF4启用扫描 */ kb_wait_input_empty(); outb(KEYBOARD_CMD_PORT, 0xAD); /* 禁用键盘 */ kb_wait_input_empty(); kb_flush_output(); /* 清残留 */ outb(KEYBOARD_CMD_PORT, 0xAE); /* 启用键盘 */ kb_wait_input_empty(); outb(KEYBOARD_DATA_PORT, 0xF4); /* 键盘命令: 启用扫描 */ kb_wait_input_empty(); for (int i = 0; i < 100000; i++) { /* 等ACK(0xFA), 超时保护 */ if (inb(KEYBOARD_STATUS_PORT) & 0x01) { (void)inb(KEYBOARD_DATA_PORT); break; } } /* registered IRQ1 */ extern void irq_register_handler(uint8_t, void(*)(void)); irq_register_handler(IRQ_KEYBOARD, keyboard_handler); /* V3.87-E8: 注册键盘底半部 */ extern void softirq_register(int, void (*)(void)); extern void softirq_kbd_poll(void); softirq_register(0, softirq_kbd_poll); vga_puts("[KEYBOARD] PS/2 keyboard driver init done(IRQ1, 8042 reset)\n");
}

/* V2.60: 键盘活动 → 重置电源空闲计时 + 唤醒休眠系统 */
extern void pm_reset_idle_timer(void);
extern void pm_wakeup(void);

char keyboard_getchar(void)
{
    while (buffer_count == 0) {
        extern void wd_feed(void);
        wd_feed();  /* V3.87-E6: 等输入=系统活跃, 喂狗防误触发 */
        extern void softirq_run(void);
        softirq_run();  /* V3.87-E8: 等待输入时执行底半部 */
        /* V4.07-E52: 低电量自动深度睡眠——输入空闲时消费标志进入
         * SUSPEND(深度睡眠等待循环, RTC 闹钟/按键可唤醒). 唤醒后
         * 回到此处继续等输入. */
        extern int pm_consume_lowbatt_pending(void);
        if (pm_consume_lowbatt_pending()) {
            extern void pm_sleep(int);
            vga_puts("\n[PM] low battery: auto suspend ...\n");
            pm_sleep(3);   /* PM_STATE_SUSPEND */
            vga_puts("[PM] resumed after low-battery sleep\n");
        }
        __asm__ volatile ("hlt");   /* V2.60: 空闲 HLT 省电, PIT 中断唤醒后复查 */
    }
    char c = keyboard_buffer[buffer_tail];
    buffer_tail = (buffer_tail + 1) % KEYBOARD_BUFFER_SIZE;
    buffer_count--;
    pm_reset_idle_timer();
    pm_wakeup();
    return c;
}

int keyboard_try_getchar(char *c)
{
    if (buffer_count == 0) return 0;
    *c = keyboard_buffer[buffer_tail];
    buffer_tail = (buffer_tail + 1) % KEYBOARD_BUFFER_SIZE;
    buffer_count--;
    pm_reset_idle_timer();
    pm_wakeup();
    return 1;
}

int keyboard_has_data(void)
{
    return buffer_count > 0;
}

/* 命令历史 (最多 16 条, 每条 128B) */
#define HISTORY_SIZE 16
#define HISTORY_LEN  128
static char history_buf[HISTORY_SIZE][HISTORY_LEN];
static int history_count = 0;
static int history_pos = 0;   /* 当前浏览位置 (-1=正在输入新命令) */

int keyboard_readline(char *buf, int max_len)
{
    int pos = 0;
    int u8n = 0;             /* V4.26-E71: UTF-8 收包状态 */
    char u8t[4];
    history_pos = -1;        /* 新命令 */

    while (pos < max_len - 1) {
        char c = keyboard_getchar();

        if (c == '\n' || c == '\r') {
            vga_putc('\n');
            break;
        }
        if (c == 0x08) {  /* Backspace: vga_putc 已内置"光标后退+清除字符" */
            if (u8n > 0) { u8n = 0; }        /* 取消未完成 UTF-8 包 */
            else if (pos > 0) {
                pos--;
                vga_putc(0x08);
            }
            continue;
        }
        /* 命令历史: Up=0x91, Down=0x92 */
        if (c == 0x91 || c == 0x92) {
            if (history_count == 0) continue;
            /* 清当前行 */
            for (int i = 0; i < pos; i++) vga_putc(0x08);
            for (int i = 0; i < pos; i++) vga_putc(' ');
            for (int i = 0; i < pos; i++) vga_putc(0x08);
            if (c == 0x91) {  /* Up: 上一条 */
                if (history_pos == -1) history_pos = history_count - 1;
                else if (history_pos > 0) history_pos--;
            } else {          /* Down: 下一条 */
                if (history_pos == -1) continue;
                history_pos++;
                if (history_pos >= history_count) history_pos = -1;
            }
            if (history_pos >= 0) {
                /* 复制历史命令到行 */
                pos = 0;
                while (history_buf[history_pos][pos] && pos < max_len - 1) {
                    buf[pos] = history_buf[history_pos][pos];
                    vga_putc(buf[pos]);
                    pos++;
                }
            } else {
                pos = 0;
            }
            continue;
        }
        if ((unsigned char)c >= 0x80) {
            /* V4.26-E71: 3 字节 UTF-8 汉字收包 (IME 注入/未来真键盘) */
            if (u8n == 0) {
                if (((unsigned char)c & 0xF0) == 0xE0) { u8t[0] = c; u8n = 1; }
                /* 其他 UTF-8 起始(2字节/4字节)暂忽略 */
                continue;
            }
            u8t[u8n++] = c;
            if (u8n == 3) {
                u8t[3] = '\0';
                if (pos + 3 < max_len - 1) {
                    buf[pos] = u8t[0]; buf[pos+1] = u8t[1]; buf[pos+2] = u8t[2];
                    pos += 3;
                    vga_puts(u8t);       /* CJK 管线渲染 */
                }
                u8n = 0;
            }
            continue;
        }
        u8n = 0;             /* ASCII 打断 UTF-8 包 */
        if (c == 0x09) {    /* V4.27-E72 + V4.28-E73: TAB = 内嵌 IME 拼音候选
                               * V4.28-E73: 改用旧 IME 完整拼音表(词组+简拼+单字+频率) */
            extern int ime_query_py(const char *);
            extern int ime_candidate_get(int, char *, int);
            int wstart = pos;
            while (wstart > 0) {
                char cc = buf[wstart-1];
                if ((cc >= 'a' && cc <= 'z') || (cc >= 'A' && cc <= 'Z')) wstart--;
                else break;
            }
            int wlen = pos - wstart;
            if (wlen > 0 && wlen < 16) {
                char word[17];
                for (int i = 0; i < wlen; i++) word[i] = buf[wstart + i];
                word[wlen] = '\0';
                int cnt = ime_query_py(word);
                if (cnt > 0) {
                    /* V4.31-E76: 候选翻页——0 键下一页, 1-9 选字 */
                    extern int ime_page_next(void);
                    extern int ime_page_count(void);
                    extern int ime_page_index(void);
                    while (1) {
                        vga_puts("\n  IME: ");
                        for (int i = 0; i < 9; i++) {
                            char u8[16];
                            if (ime_candidate_get(i, u8, 16) == 0) {
                                vga_printf("%d:", i + 1);
                                vga_puts(u8);
                                vga_putc(' ');
                            }
                        }
                        if (ime_page_count() > 1) {
                            vga_puts(" 0:pg");
                            vga_printf("(%d/%d)", ime_page_index() + 1, ime_page_count());
                        }
                        vga_puts("\n  ? ");
                        char sel = keyboard_getchar();
                        if (sel == '0' && ime_page_count() > 1) {
                            ime_page_next();
                            continue;
                        }
                        int n = sel - '0';
                        if (n >= 1 && n <= 9) {
                            char u8[16];
                            if (ime_candidate_get(n - 1, u8, 16) == 0) {
                                int ulen = 0;
                                while (u8[ulen]) ulen++;
                                for (int i = 0; i < wlen; i++) vga_putc(0x08);
                                for (int i = 0; i < ulen; i++) buf[wstart + i] = u8[i];
                                pos = wstart + ulen;
                                buf[pos] = '\0';
                                vga_puts(u8);
                                break;
                            }
                        }
                    }
                    vga_puts("\r  ");
                    vga_puts(buf);
                    continue;
                }
            }
            vga_putc('\t');   /* 无候选: 原样 */
            continue;
        }
        if (c >= ' ' && c < 127) {
            buf[pos++] = c;
            vga_putc(c);
        }
    }
    buf[pos] = '\0';
    /* 保存非空命令到历史 */
    if (pos > 0) {
        int idx = history_count % HISTORY_SIZE;
        int i;
        for (i = 0; i < pos && i < HISTORY_LEN - 1; i++) history_buf[idx][i] = buf[i];
        history_buf[idx][i] = '\0';
        if (history_count < HISTORY_SIZE) history_count++;
    }
    return pos;
}

/* V2.22: 注入测试字符串到键盘缓冲区(供Shell自检/无硬件键盘的headless环境验证)
 * 仅用于验证 readline+execute 路径; 真机上由 IRQ1 中断经 keyboard_handler 填buffer */
void keyboard_inject(const char *s)
{
    while (s && *s) {
        buffer_put(*s);
        s++;
    }
}
