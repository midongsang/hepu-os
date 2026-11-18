/* ============================================================================
 * 赫普OS - 蓝屏诊断界面 (BSOD / Diagnostic Report)
 *
 * 设计目标（不只是"屏幕变蓝"，而是可诊断）：
 *   1. STOP CODE : 标准形如 0x0000000E，对应异常号，便于检索与对账
 *   2. 现场快照  : EIP/CS/EFLAGS/ESP/ERR/CR2 —— 定位到指令与地址
 *   3. 近似回溯  : 从崩溃栈扫出落在内核文本区的返回地址
 *   4. 落盘      : 同时写 /crash.log，重启后 crashlog 命令可查
 *   5. 双通道    : 图形(320x200 蓝底白字) + VGA 文本降级 + 串口
 *
 * 显示约定：低分辨率点阵字体不含中文，界面一律用英文，避免乱码。
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include "bsod.h"
#include "vga.h"
#include "vga_gfx.h"
#include "serial.h"
#include "crashlog.h"

static const char *bsod_exception_names[] = {
    "Division By Zero", "Debug", "NMI Interrupt", "Breakpoint",
    "Overflow", "Bound Range Exceeded", "Invalid Opcode",
    "Device Not Available", "Double Fault", "Coprocessor Overrun",
    "Invalid TSS", "Segment Not Present", "Stack Fault",
    "General Protection Fault", "Page Fault", "Reserved",
    "x87 FPU Error", "Alignment Check", "Machine Check",
    "SIMD FPU Exception", "Virtualization Exception",
    "Reserved", "Reserved", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved"
};

/* 诊断提示：每个异常一句"该查什么" */
static const char *bsod_hint(uint32_t n)
{
    switch (n) {
    case 0:  return "divide by zero / bad divisor";
    case 1:  return "debug exception (#DB)";
    case 6:  return "invalid opcode: corrupted code or bad jump";
    case 8:  return "double fault: exception inside handler";
    case 11: return "bad segment selector";
    case 12: return "stack overflow or corrupted stack";
    case 13: return "GPF: bad selector / bad ptr / priv violation";
    case 14: return "page fault: CR2 holds faulting address";
    case 17: return "misaligned access on SIMD data";
    default: return "kernel exception";
    }
}

static void bsod_hex32(uint32_t v, char out[11])
{
    static const char digits[] = "0123456789ABCDEF";
    out[0] = '0'; out[1] = 'x';
    for (int i = 0; i < 8; i++)
        out[2 + i] = digits[(v >> (28 - i * 4)) & 0xF];
    out[10] = '\0';
}

static void bsod_stop_code(uint32_t isr, char out[11])
{
    static const char digits[] = "0123456789ABCDEF";
    out[0] = '0'; out[1] = 'x';
    out[2] = '0'; out[3] = '0'; out[4] = '0'; out[5] = '0';
    out[6] = '0'; out[7] = '0';
    out[8] = digits[(isr >> 4) & 0xF];
    out[9] = digits[isr & 0xF];
    out[10] = '\0';
}

static int bsod_walk_stack(uint32_t esp, uint32_t *out, int max)
{
    if (esp < 0x00100000u || esp >= 0x10000000u) return 0;
    int n = 0;
    const uint32_t *sp = (const uint32_t *)(uintptr_t)esp;
    for (int i = 0; i < 96 && n < max; i++) {
        uint32_t v = sp[i];
        if (v >= 0x00010000u && v < 0x001F0000u) out[n++] = v;
    }
    return n;
}

static void bsod_serial_output(const char *reason, uint32_t isr_number,
                               uint32_t error_code, uint32_t eip, uint32_t cs,
                               uint32_t esp, uint32_t cr2, uint32_t eflags)
{
    char hexbuf[11];
    char stopbuf[11];
    uint32_t frames[4];
    int nf;

    bsod_stop_code(isr_number, stopbuf);
    serial_puts(0x3F8, "\r\n=== HEPU OS CRASH (BSOD) ===\r\n");
    serial_puts(0x3F8, "Reason: "); serial_puts(0x3F8, reason);
    serial_puts(0x3F8, "\r\nSTOP: "); serial_puts(0x3F8, stopbuf);
    serial_puts(0x3F8, "  Exception: ");
    serial_puts(0x3F8, isr_number < 32 ? bsod_exception_names[isr_number] : "Unknown");
    serial_puts(0x3F8, "\r\n");
    bsod_hex32(error_code, hexbuf); serial_puts(0x3F8, "ERR="); serial_puts(0x3F8, hexbuf);
    bsod_hex32(cr2, hexbuf); serial_puts(0x3F8, " CR2="); serial_puts(0x3F8, hexbuf);
    bsod_hex32(eip, hexbuf); serial_puts(0x3F8, " EIP="); serial_puts(0x3F8, hexbuf);
    bsod_hex32(cs, hexbuf); serial_puts(0x3F8, " CS="); serial_puts(0x3F8, hexbuf);
    bsod_hex32(eflags, hexbuf); serial_puts(0x3F8, " EFLAGS="); serial_puts(0x3F8, hexbuf);
    bsod_hex32(esp, hexbuf); serial_puts(0x3F8, " ESP="); serial_puts(0x3F8, hexbuf);
    serial_puts(0x3F8, "\r\nHint: "); serial_puts(0x3F8, bsod_hint(isr_number));
    serial_puts(0x3F8, "\r\n");
    nf = bsod_walk_stack(esp, frames, 4);
    serial_puts(0x3F8, "Stack(near):");
    for (int i = 0; i < nf; i++) {
        bsod_hex32(frames[i], hexbuf);
        serial_puts(0x3F8, " "); serial_puts(0x3F8, hexbuf);
    }
    if (!nf) serial_puts(0x3F8, " (unavailable)");
    serial_puts(0x3F8, "\r\n=== end ===\r\n");
}


/* 强制切回 VGA 80x25 文本模式。
 * 崩溃现场 VGA 可能停在 0x13 图形模式，此时写 0xB8000 完全不显示；
 * 因此蓝屏先无条件重建文本模式(标准 mode 3 寄存器表)，再写 0xB8000。
 * 这条路径不依赖帧缓冲、调色板、pitch —— 只依赖 VGA 端口。 */
static void bsod_force_text_mode(void)
{
    static const uint8_t seq_regs[5]  = {0x03,0x00,0x03,0x00,0x02};
    static const uint8_t crtc_regs[25] = {
        0x5F,0x4F,0x50,0x82,0x55,0x81,0xBF,0x1F,0x00,0x4F,
        0x0D,0x0E,0x00,0x00,0x00,0x00,0x9C,0x8E,0x8F,0x28,
        0x1F,0x96,0xB9,0xA3,0xFF
    };
    static const uint8_t gc_regs[9]   = {0x00,0x00,0x00,0x00,0x00,0x10,0x0E,0x00,0xFF};

#define BO(p,v) __asm__ volatile ("outb %0, %1" :: "a"((uint8_t)(v)), "Nd"((uint16_t)(p)))
#define BI(p)   ({ uint8_t _v; __asm__ volatile ("inb %1, %0" : "=a"(_v) : "Nd"((uint16_t)(p))); _v; })

    /* 0. 先关 Bochs/QEMU VBE：DISPI enable=0，否则传统 VGA 寄存器被 VBE 接管，
     *    切了文本模式也看不到任何字(QEMU -vga std 默认 1920x1080 VBE)。 */
    __asm__ volatile ("outw %0, %%dx" :: "a"((uint16_t)4), "d"((uint16_t)0x1CE));
    __asm__ volatile ("outw %0, %%dx" :: "a"((uint16_t)0), "d"((uint16_t)0x1CF));

    /* 1. 关断 video 输出，复位序列器 */
    BI(0x3DA);
    BO(0x3C4, 0x00); BO(0x3C5, 0x01);
    BO(0x3C4, 0x01); BO(0x3C5, 0x01);

    /* 2. Misc output 选 80x25 彩色文本(0x67) */
    BO(0x3C2, 0x67);

    /* 3. 序列器 */
    for (int i = 0; i < 5; i++) { BO(0x3C4, (uint8_t)i); BO(0x3C5, seq_regs[i]); }

    /* 4. CRTC：先清 CR11(解锁)，再写全表，最后恢复 CR11 */
    BO(0x3D4, 0x11); BO(0x3D5, 0x00);
    for (int i = 0; i < 25; i++) { BO(0x3D4, (uint8_t)i); BO(0x3D5, crtc_regs[i]); }

    /* 5. 图形控制器 */
    for (int i = 0; i < 9; i++) { BO(0x3CE, (uint8_t)i); BO(0x3CF, gc_regs[i]); }

    /* 6. 属性控制器：复位 flip-flop 后写 16 色 + 模式控制 */
    {
        BI(0x3DA);
        for (int i = 0; i < 16; i++) { BO(0x3C0, (uint8_t)i); BO(0x3C0, (uint8_t)i); }
        BO(0x3C0, 0x10); BO(0x3C0, 0x0C);   /* graphics mode off, blink off */
        BO(0x3C0, 0x20);                     /* enable palette */
    }

    /* 7. 恢复序列器，打开 video */
    for (int i = 0; i < 5; i++) { BO(0x3C4, (uint8_t)i); BO(0x3C5, seq_regs[i]); }

#undef BO
#undef BI
}

void bsod_text_mode(const char *reason, uint32_t isr_number,
                    uint32_t error_code, uint32_t eip,
                    uint32_t cs, uint32_t esp)
{
    bsod_text_mode_ex(reason, isr_number, error_code, eip, cs, esp, 0, 0);
}

void bsod_text_mode_ex(const char *reason, uint32_t isr_number,
                       uint32_t error_code, uint32_t eip,
                       uint32_t cs, uint32_t esp, uint32_t cr2, uint32_t eflags)
{
    char hexbuf[11];
    char stopbuf[11];
    uint32_t frames[4];
    int nf;
    (void)reason;

    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    vga_clear();
    vga_gotoxy(1, 2);
    vga_puts("HEPU OS DIAGNOSTIC REPORT\n\n");
    bsod_stop_code(isr_number, stopbuf);
    vga_puts("  STOP: "); vga_puts(stopbuf); vga_puts("\n");
    vga_puts("  Exception: ");
    vga_puts(isr_number < 32 ? bsod_exception_names[isr_number] : "Unknown");
    vga_puts("\n\n  Fault context:\n");
    vga_puts("    ERR = "); bsod_hex32(error_code, hexbuf); vga_puts(hexbuf); vga_puts("\n");
    vga_puts("    EIP = "); bsod_hex32(eip, hexbuf); vga_puts(hexbuf); vga_puts("\n");
    vga_puts("    CS  = "); bsod_hex32(cs, hexbuf); vga_puts(hexbuf); vga_puts("\n");
    vga_puts("    ESP = "); bsod_hex32(esp, hexbuf); vga_puts(hexbuf); vga_puts("\n");
    vga_puts("    CR2 = "); bsod_hex32(cr2, hexbuf); vga_puts(hexbuf); vga_puts("\n");
    vga_puts("    EFL = "); bsod_hex32(eflags, hexbuf); vga_puts(hexbuf); vga_puts("\n\n");
    vga_puts("  Hint: "); vga_puts(bsod_hint(isr_number)); vga_puts("\n\n");
    nf = bsod_walk_stack(esp, frames, 4);
    vga_puts("  Stack (near): ");
    for (int i = 0; i < nf; i++) {
        bsod_hex32(frames[i], hexbuf); vga_puts(hexbuf); vga_puts(" ");
    }
    if (!nf) vga_puts("(unavailable)");
    vga_puts("\n\n  Log saved to /crash.log -- reboot then run: crashlog\n");
    vga_puts("  System halted. Please reboot.\n");
}


void bsod_show(const char *reason, uint32_t isr_number,
               uint32_t error_code, uint32_t eip,
               uint32_t cs, uint32_t esp, uint32_t cr2)
{
    uint32_t eflags = 0;
    __asm__ volatile ("pushf; pop %0" : "=r"(eflags));

    if (cr2 == 0 && isr_number == 14)
        __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));

    bsod_serial_output(reason, isr_number, error_code, eip, cs, esp, cr2, eflags);
    crashlog_append("bsod", isr_number, isr_number, error_code, eip, cs, esp);

    /* 崩溃现场统一走 VGA 文本模式：先重建 80x25 模式，再写 0xB8000。
     * 理由：崩溃时 VGA 可能停在 0x13 图形模式/寄存器已损坏，图形路径
     * (back_buffer+flip) 依赖 pitch/调色板，实测会丢字；文本模式只依赖
     * VGA 端口，是最可靠的"最后一块屏"。 */
    bsod_force_text_mode();
    bsod_text_mode_ex(reason, isr_number, error_code, eip, cs, esp, cr2, eflags);

    for (;;) __asm__ volatile ("cli; hlt");
}
