/* ============================================================================
 * 赫普OS - 中断描述符表 (IDT)
 * ========================================================================== */

#include <stdint.h>
#include "idt.h"
#include "constants.h"
#include "vga.h"
#include "paging.h"
#include "syscall.h"
#include "process.h"
#include "serial.h"
#include "bsod.h"

#define IDT_ENTRIES 256

typedef struct {
    uint16_t base_low;
    uint16_t sel;
    uint8_t  always0;
    uint8_t  flags;
    uint16_t base_high;
} __attribute__((packed)) idt_entry_t;

typedef struct {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed)) idt_ptr_t;

static idt_entry_t idt_entries[IDT_ENTRIES];
static idt_ptr_t   idt_ptr;

/* 汇编中断处理入口 */
extern void isr0(void);
extern void isr1(void);
extern void isr2(void);
extern void isr3(void);
extern void isr4(void);
extern void isr5(void);
extern void isr6(void);
extern void isr7(void);
extern void isr8(void);
extern void isr9(void);
extern void isr10(void);
extern void isr11(void);
extern void isr12(void);
extern void isr13(void);
extern void isr14(void);
extern void isr15(void);
extern void isr16(void);
extern void isr17(void);
extern void isr18(void);
extern void isr19(void);
extern void isr20(void);
extern void isr31(void);
extern void isr128(void);  /* 系统调用 */
extern void idt_load(uint32_t idt_ptr);

/* IRQ 硬件中断入口 */
extern void irq0(void);  extern void irq1(void);  extern void irq2(void);
extern void irq3(void);  extern void irq4(void);  extern void irq5(void);
extern void irq6(void);  extern void irq7(void);  extern void irq8(void);
extern void irq9(void);  extern void irq10(void); extern void irq11(void);
extern void irq12(void); extern void irq13(void); extern void irq14(void);
extern void irq15(void);

static void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags)
{
    idt_entries[num].base_low  = base & IDT_BASE_LOW_MASK;
    idt_entries[num].base_high = (base >> 16) & IDT_BASE_HIGH_MASK;
    idt_entries[num].sel       = sel;
    idt_entries[num].always0   = 0;
    idt_entries[num].flags     = flags;
}

/* 异常名称 */
static const char *exception_messages[] = {
    "Division By Zero",
    "Debug",
    "Non Maskable Interrupt",
    "Breakpoint",
    "Into Detected Overflow",
    "Out of Bounds",
    "Invalid Opcode",
    "No Coprocessor",
    "Double Fault",
    "Coprocessor Segment Overrun",
    "Bad TSS",
    "Segment Not Present",
    "Stack Fault",
    "General Protection Fault",
    "Page Fault",
    "Unknown Interrupt",
    "Coprocessor Fault",
    "Alignment Check",
    "Machine Check",
    "Reserved", "Reserved", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved",
    "Reserved"
};

/* 中断处理C函数
 * 注意：对于系统调用(int 0x80)，参数在寄存器中
 * isr_number=14 是页错误，需要特殊处理 */
void idt_handler(uint32_t isr_number, uint32_t error_code,
                 uint32_t ebx, uint32_t ecx, uint32_t edx, uint32_t esi)
{
    (void)ebx; (void)ecx; (void)edx; (void)esi;
    if (isr_number == 14) {
        /* 页错误 */
        extern uint32_t paging_read_cr2(void);
        uint32_t fault_addr = paging_read_cr2();
        /* V6.80 PAE分支: 根据PAE是否启用调用不同处理函数 */
        extern int paging_pae_is_enabled(void);
        if (paging_pae_is_enabled()) {
            extern void paging_pae_fault_handler(uint32_t, uint32_t);
            paging_pae_fault_handler(fault_addr, error_code);
        } else {
            page_fault_handler(fault_addr, error_code);
        }
        return;
    }

    if (isr_number == 128) {
        /* 系统调用: eax=num, ebx=arg1, ecx=arg2, edx=arg3, esi=arg4
         * 返回值在 eax 中，由汇编入口处理 */
        /* 实际处理在 syscall_entry.S 中完成 */
        return;
    }

    if (isr_number < 32) {
        /* V3.85: 用户态异常隔离——应用崩溃只杀进程, 不拖垮内核.
         * 此前任何<32异常一律停机; 用户进程除零/非法指令等缺陷会把
         * 整个系统打死. 现在识别 current 特权级:
         *   - 用户态(current非空且privilege==PRIV_USER): 打印崩溃详情,
         *     走 sys_exit 清理路径(冻结其他进程+释放崩溃进程+启动内核Shell),
         *     shell_main 为无限循环不返回→异常帧永不弹回→系统继续可用.
         *   - 内核态: 维持原停机行为(内核bug必须停). */
        extern process_t *current;
        extern uint32_t sys_exit(uint32_t, uint32_t, uint32_t, uint32_t);
        if (current != NULL && current->privilege == PRIV_USER) {
            /* V3.85: 先切回内核页目录再打印——异常发生时 CR3 仍是用户
             * 页目录, VGA 显存(0xB8000)未必映射, vga_printf 写显存会
             * 页错误/静默丢失 → 崩溃详情看不到. */
            extern page_directory_t *paging_get_kernel_dir(void);
            extern void paging_switch_dir(page_directory_t *);
            extern void serial_puts(uint16_t, const char *);
            paging_switch_dir(paging_get_kernel_dir());
            /* 异常详情同时打串口, 便于 QEMU 日志定位(VGA 可能被应用清屏) */
            serial_puts(0x3F8, "[UEXC] ");
            serial_puts(0x3F8, exception_messages[isr_number]);
            serial_puts(0x3F8, "\n");
            vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
            vga_printf("\n[HEPU] user exception: %s(ISR %d, err=0x%x)\n",
                       exception_messages[isr_number], isr_number, error_code);
            /* V3.87: 打印异常时 EIP/CS/ESP——崩溃点定位. current->kernel_esp
             * 指向统一中断帧 ds 处(process_create/调度保存):
             * ds@0 pusha@4-32 irq@36 err@40 eip@44 cs@48 eflags@52
             * user_esp@56 ss@60 */
            {
                uint32_t *fr = (uint32_t *)current->kernel_esp;
                vga_printf("[HEPU] crash frame: eip=0x%x cs=0x%x esp=0x%x\n",
                           fr[11], fr[12], fr[14]);
                /* V6.47 BUG-20验证: 崩溃帧完整信息输出到串口(权威) */
                {
                    extern void serial_puts(uint16_t, const char *);
                    extern void serial_puthex(uint16_t, uint32_t);
                    serial_puts(0x3F8, "[CRASH-FRAME] ");
                    serial_puts(0x3F8, "eip="); serial_puthex(0x3F8, fr[11]);
                    serial_puts(0x3F8, " cs="); serial_puthex(0x3F8, fr[12]);
                    serial_puts(0x3F8, " eflags="); serial_puthex(0x3F8, fr[13]);
                    serial_puts(0x3F8, " esp="); serial_puthex(0x3F8, fr[14]);
                    serial_puts(0x3F8, " ss="); serial_puthex(0x3F8, fr[15]);
                    serial_puts(0x3F8, " err="); serial_puthex(0x3F8, fr[10]);
                    serial_puts(0x3F8, " eax="); serial_puthex(0x3F8, fr[8]);
                    serial_puts(0x3F8, " ebx="); serial_puthex(0x3F8, fr[5]);
                    serial_puts(0x3F8, " ecx="); serial_puthex(0x3F8, fr[7]);
                    serial_puts(0x3F8, " edx="); serial_puthex(0x3F8, fr[6]);
                    serial_puts(0x3F8, "\n");
                    /* V6.47 BUG-20验证: 崩溃时用户栈内容(前16个dword) */
                    {
                        extern uint32_t paging_get_phys(page_directory_t *,
                                                        uint32_t);
                        uint32_t usp = fr[14];
                        serial_puts(0x3F8, "[USTACK] ");
                        serial_puthex(0x3F8, usp);
                        serial_puts(0x3F8, ":");
                        for (uint32_t j = 0; j < 16; j++) {
                            uint32_t va = usp + j * 4;
                            uint32_t phys = paging_get_phys(current->page_dir,
                                                            va);
                            uint32_t val = phys ?
                                *(const uint32_t *)(uintptr_t)phys : 0xDEADBEEF;
                            serial_puts(0x3F8, " ");
                            serial_puthex(0x3F8, val);
                        }
                        serial_puts(0x3F8, "\n");
                    }
                }
                /* V3.87 阶段B-2/B-3 诊断: 崩时读用户 IAT 槽 (PE32).
                 * B-3: 生成器 .idata 8 导入, IAT 首槽 = ImageBase+0x2048
                 * (IID.FirstThunk), 勿再硬编码旧 6 槽 0x2040.
                 * 同步打串口 (serial), VGA 截图 OCR 不可靠. */
                if (current->compat_abi == 2) {
                    extern uint32_t paging_get_phys(page_directory_t *,
                                                    uint32_t);
                    static const char *names[8] = {
                        "WCA", "GStdH", "GCmdL", "GTick",
                        "Sleep", "Exit", "GModF", "RdF"
                    };
                    uint32_t i;
                    for (i = 0; i < 8; i++) {
                        uint32_t va = 0x402048 + i * 4;
                        uint32_t phys = paging_get_phys(current->page_dir,
                                                        va);
                        uint32_t val = phys ? *(const uint32_t *)(uintptr_t)phys : 0;
                        vga_printf("[HEPU] iat[%u] %s va=0x%x phys=0x%x "
                                   "val=0x%x\n", i, names[i], va, phys, val);
                        serial_puts(0x3F8, "[IAT] ");
                        serial_puts(0x3F8, names[i]);
                        serial_puts(0x3F8, "=");
                        serial_puthex(0x3F8, val);
                        serial_puts(0x3F8, "\n");
                    }
                    /* 崩时读 stub 页关键 stub 字节 */
                    {
                        uint32_t phys = paging_get_phys(current->page_dir,
                                                        0x403000);
                        if (phys) {
                            const uint8_t *p =
                                (const uint8_t *)(uintptr_t)phys;
                            vga_printf("[HEPU] stub page phys=0x%x\n", phys);
                            serial_puts(0x3F8, "[STUBP] ");
                            serial_puthex(0x3F8, phys);
                            serial_puts(0x3F8, "\n");
                            serial_puts(0x3F8, "[STUB0] ");
                            for (i = 0; i < 0x40; i++) {
                                serial_puthex(0x3F8, p[i]);
                                serial_puts(0x3F8, " ");
                            }
                            serial_puts(0x3F8, "\n");
                            serial_puts(0x3F8, "[STUB4] ");
                            for (i = 0x40; i < 0x80; i++) {
                                serial_puthex(0x3F8, p[i]);
                                serial_puts(0x3F8, " ");
                            }
                            serial_puts(0x3F8, "\n");
                            serial_puts(0x3F8, "[STUB8] ");
                            for (i = 0x80; i < 0xC0; i++) {
                                serial_puthex(0x3F8, p[i]);
                                serial_puts(0x3F8, " ");
                            }
                            serial_puts(0x3F8, "\n");
                            serial_puts(0x3F8, "[STUBC] ");
                            for (i = 0xC0; i < 0x100; i++) {
                                serial_puthex(0x3F8, p[i]);
                                serial_puts(0x3F8, " ");
                            }
                            serial_puts(0x3F8, "\n");
                        }
                    }
                }
            }
            vga_printf("[HEPU] app crash: pid=%d (%s), killing...\n",
                       current->pid, current->name);
            /* V3.89-E20: 崩溃帧落盘到数据盘 /crash.log(保留最近8条),
             * 真机黑屏后可挂载数据盘或 `crashlog` 命令查看历史. */
            {
                uint32_t *fr2 = (uint32_t *)current->kernel_esp;
                extern int crashlog_append(const char *, uint32_t,
                                           uint32_t, uint32_t,
                                           uint32_t, uint32_t, uint32_t);
                crashlog_append(current->name, (uint32_t)current->pid,
                                (uint32_t)isr_number, error_code,
                                fr2[11], fr2[12], fr2[14]);
            }
            sys_exit(1, 0, 0, 0);
            /* 正常不会到达: sys_exit 首次调用启动Shell(不返回),
             * 已启动则内部 hlt. 保险兜底. */
            for (;;) { __asm__ volatile ("hlt"); }
        }
        vga_set_color(VGA_COLOR_RED, VGA_COLOR_BLACK);
        vga_printf("\n[HEPU] kernel exception: %s(ISR %d, err=0x%x)\n",
                   exception_messages[isr_number], isr_number, error_code);
        /* E-44: 内核态异常打印 iret 目标(EIP/CS/EFLAGS)——由 isr_common_stub
         * 的 idt_last_* 快照提供; 栈被破坏时显示将弹出的 EIP 定位崩溃去向. */
        {
            extern uint32_t idt_last_eip, idt_last_cs, idt_last_eflags;
            extern uint32_t idt_last_esp;
            extern void serial_puts(uint16_t, const char *);
            extern void serial_puthex(uint16_t, uint32_t);
            vga_printf("[HEPU] iret-target: eip=0x%x cs=0x%x eflags=0x%x\n",
                       idt_last_eip, idt_last_cs, idt_last_eflags);
            serial_puthex(0x3F8, idt_last_eip);
            serial_puts(0x3F8, " cs=0x");
            serial_puthex(0x3F8, idt_last_cs);
            serial_puts(0x3F8, " eflags=0x");
            serial_puthex(0x3F8, idt_last_eflags);
            serial_puts(0x3F8, " esp=0x");
            serial_puthex(0x3F8, idt_last_esp);
            serial_puts(0x3F8, "\n");
            {
                extern void *tss_get_raw(void);
                extern uint32_t tss_get_esp0(void);
                extern char __stack_top[];
                extern process_t *current;
                serial_puthex(0x3F8, tss_get_esp0());
                serial_puts(0x3F8, " stack_top=0x");
                serial_puthex(0x3F8, (uint32_t)__stack_top);
                serial_puts(0x3F8, " cur=0x");
                serial_puthex(0x3F8, (uint32_t)current);
                serial_puts(0x3F8, "\n");
                extern uint32_t mainline_jmp_buf[6];
                serial_puthex(0x3F8, mainline_jmp_buf[1]);
                serial_puts(0x3F8, " ml_eip=0x");
                serial_puthex(0x3F8, mainline_jmp_buf[0]);
                serial_puts(0x3F8, "\n");
            }
        }
        /* V4.15-E60: iret-target 非法(栈损坏/垃圾帧, 如 eip=0xA0000 显存)
         * ——恢复回 Shell 而非 halt: 重置调度器 + TSS.ESP0 + longjmp 主线.
         * 仅当 eip 明显不在内核文本区时恢复; 正常内核地址崩溃仍 halt 以便定位. */
        {
            extern uint32_t idt_last_eip;
            extern uint32_t mainline_jmp_buf[6];
            extern void hepu_longjmp(uint32_t *, int);
            extern process_t *current;
            extern void tss_set_kernel_stack(uint32_t);
            extern char __stack_top[];
            int eip_ok = (idt_last_eip >= 0x10000 && idt_last_eip < 0x9F000);
            if (!eip_ok) {
                vga_puts("\n[HEPU] iret-target invalid -> recovering to shell...\n");
                serial_puts(0x3F8, "[HEPU] recovery: iret-target invalid, longjmp shell\n");
                __asm__ volatile ("cli");
                current = NULL;
                tss_set_kernel_stack((uint32_t)__stack_top);
                hepu_longjmp(mainline_jmp_buf, 1);
            }
        }
        /* V3.90: 显示蓝屏死机界面（图形模式优先，失败自动降级文本模式）。
         * bsod_show 内部最终 HLT，不返回。同时输出到串口便于日志分析。 */
        {
            extern uint32_t idt_last_eip, idt_last_cs, idt_last_eflags;
            extern uint32_t idt_last_esp;
            bsod_show("Kernel Exception", isr_number, error_code,
                      idt_last_eip, idt_last_cs, idt_last_esp);
        }
        /* 不会到达这里 */
        vga_puts(" system halted . please reboot .\n");
        for (;;) { __asm__ volatile ("hlt"); }
    }
}

void idt_init(void)
{
    idt_ptr.limit = sizeof(idt_entry_t) * IDT_ENTRIES - 1;
    idt_ptr.base  = (uint32_t)&idt_entries;

    /* 初始化前32个异常处理 */
    idt_set_gate(0,  (uint32_t)isr0,  GDT_KERNEL_CODE, IDT_FLAGS_INT_GATE);
    idt_set_gate(1,  (uint32_t)isr1,  GDT_KERNEL_CODE, IDT_FLAGS_INT_GATE);
    idt_set_gate(2,  (uint32_t)isr2,  GDT_KERNEL_CODE, IDT_FLAGS_INT_GATE);
    idt_set_gate(3,  (uint32_t)isr3,  GDT_KERNEL_CODE, IDT_FLAGS_INT_GATE);
    idt_set_gate(4,  (uint32_t)isr4,  GDT_KERNEL_CODE, IDT_FLAGS_INT_GATE);
    idt_set_gate(5,  (uint32_t)isr5,  GDT_KERNEL_CODE, IDT_FLAGS_INT_GATE);
    idt_set_gate(6,  (uint32_t)isr6,  GDT_KERNEL_CODE, IDT_FLAGS_INT_GATE);
    idt_set_gate(7,  (uint32_t)isr7,  GDT_KERNEL_CODE, IDT_FLAGS_INT_GATE);
    idt_set_gate(8,  (uint32_t)isr8,  GDT_KERNEL_CODE, IDT_FLAGS_INT_GATE);
    idt_set_gate(9,  (uint32_t)isr9,  GDT_KERNEL_CODE, IDT_FLAGS_INT_GATE);
    idt_set_gate(10, (uint32_t)isr10, GDT_KERNEL_CODE, 0x8E);
    idt_set_gate(11, (uint32_t)isr11, GDT_KERNEL_CODE, 0x8E);
    idt_set_gate(12, (uint32_t)isr12, GDT_KERNEL_CODE, 0x8E);
    idt_set_gate(13, (uint32_t)isr13, GDT_KERNEL_CODE, 0x8E);
    idt_set_gate(14, (uint32_t)isr14, GDT_KERNEL_CODE, 0x8E);
    idt_set_gate(15, (uint32_t)isr15, GDT_KERNEL_CODE, 0x8E);
    idt_set_gate(16, (uint32_t)isr16, GDT_KERNEL_CODE, 0x8E);
    idt_set_gate(17, (uint32_t)isr17, GDT_KERNEL_CODE, 0x8E);
    idt_set_gate(18, (uint32_t)isr18, GDT_KERNEL_CODE, 0x8E);
    idt_set_gate(19, (uint32_t)isr19, GDT_KERNEL_CODE, 0x8E);
    idt_set_gate(20, (uint32_t)isr20, GDT_KERNEL_CODE, 0x8E);
    idt_set_gate(31, (uint32_t)isr31, GDT_KERNEL_CODE, 0x8E);

    /* 系统调用中断 0x80 */
    idt_set_gate(IDT_SYSCALL_VECTOR, (uint32_t)isr128, GDT_KERNEL_CODE, IDT_FLAGS_SYSCALL_GATE);

    /* IRQ 硬件中断 (32-47) */
    idt_set_gate(32, (uint32_t)irq0,  GDT_KERNEL_CODE, IDT_FLAGS_INT_GATE);
    idt_set_gate(33, (uint32_t)irq1,  GDT_KERNEL_CODE, IDT_FLAGS_INT_GATE);
    idt_set_gate(34, (uint32_t)irq2,  GDT_KERNEL_CODE, IDT_FLAGS_INT_GATE);
    idt_set_gate(35, (uint32_t)irq3,  GDT_KERNEL_CODE, IDT_FLAGS_INT_GATE);
    idt_set_gate(36, (uint32_t)irq4,  GDT_KERNEL_CODE, IDT_FLAGS_INT_GATE);
    idt_set_gate(37, (uint32_t)irq5,  GDT_KERNEL_CODE, IDT_FLAGS_INT_GATE);
    idt_set_gate(38, (uint32_t)irq6,  GDT_KERNEL_CODE, IDT_FLAGS_INT_GATE);
    idt_set_gate(39, (uint32_t)irq7,  GDT_KERNEL_CODE, IDT_FLAGS_INT_GATE);
    idt_set_gate(40, (uint32_t)irq8,  GDT_KERNEL_CODE, IDT_FLAGS_INT_GATE);
    idt_set_gate(41, (uint32_t)irq9,  GDT_KERNEL_CODE, IDT_FLAGS_INT_GATE);
    idt_set_gate(42, (uint32_t)irq10, GDT_KERNEL_CODE, 0x8E);
    idt_set_gate(43, (uint32_t)irq11, GDT_KERNEL_CODE, 0x8E);
    idt_set_gate(44, (uint32_t)irq12, GDT_KERNEL_CODE, 0x8E);
    idt_set_gate(45, (uint32_t)irq13, GDT_KERNEL_CODE, 0x8E);
    idt_set_gate(46, (uint32_t)irq14, GDT_KERNEL_CODE, 0x8E);
    idt_set_gate(47, (uint32_t)irq15, GDT_KERNEL_CODE, 0x8E);

    idt_load((uint32_t)&idt_ptr);
}
