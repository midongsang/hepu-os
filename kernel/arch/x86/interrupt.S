/* ============================================================================
 * 赫普OS - x86 中断处理 (GAS / AT&T 语法)
 * GDT/IDT加载 + 32个异常ISR入口 + 通用处理
 * ========================================================================== */

.section .data
/* E-44: 异常帧快照——isr_common_stub 记录 iret 目标(EIP/CS/EFLAGS),
 * 供 idt_handler 内核态异常诊断打印(栈被破坏时定位 iret 去向). */
.global idt_last_eip
.global idt_last_cs
.global idt_last_eflags
.global idt_last_esp
idt_last_eip:    .long 0
idt_last_cs:     .long 0
idt_last_eflags: .long 0
idt_last_esp:    .long 0

.section .text

/* ----------------------------------------------------------------------------
 * GDT 加载
 * -------------------------------------------------------------------------- */
.global gdt_flush
gdt_flush:
    mov 4(%esp), %eax
    lgdt (%eax)
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    mov %ax, %ss
    ljmp $0x08, $.flush
.flush:
    ret

/* ----------------------------------------------------------------------------
 * IDT 加载
 * -------------------------------------------------------------------------- */
.global idt_load
idt_load:
    mov 4(%esp), %eax
    lidt (%eax)
    ret

/* ----------------------------------------------------------------------------
 * TSS 加载 (ltr)
 * -------------------------------------------------------------------------- */
.global tss_flush
tss_flush:
    mov $0x28, %ax    /* TSS选择子 = 5 * 8 = 0x28, RPL=0 */
    ltr %ax
    ret

/* ----------------------------------------------------------------------------
 * ISR 宏定义
 * 无错误码：压入 dummy 0
 * 有错误码(8,10-14)：CPU自动压入错误码
 * -------------------------------------------------------------------------- */
.macro ISR_NOERRCODE num
.global isr\num
isr\num:
    cli
    pushl $0
    pushl $\num
    jmp isr_common_stub
.endm

.macro ISR_ERRCODE num
.global isr\num
isr\num:
    cli
    pushl $\num
    jmp isr_common_stub
.endm

/* 前32个异常入口 */
ISR_NOERRCODE 0
ISR_NOERRCODE 1
ISR_NOERRCODE 2
ISR_NOERRCODE 3
ISR_NOERRCODE 4
ISR_NOERRCODE 5
ISR_NOERRCODE 6
ISR_NOERRCODE 7
ISR_ERRCODE   8    /* Double Fault */
ISR_NOERRCODE 9
ISR_ERRCODE   10   /* Bad TSS */
ISR_ERRCODE   11   /* Segment Not Present */
ISR_ERRCODE   12   /* Stack Fault */
ISR_ERRCODE   13   /* General Protection Fault */
ISR_ERRCODE   14   /* Page Fault */
ISR_NOERRCODE 15
ISR_NOERRCODE 16
ISR_NOERRCODE 17
ISR_NOERRCODE 18
ISR_NOERRCODE 19
ISR_NOERRCODE 20
ISR_NOERRCODE 21
ISR_NOERRCODE 22
ISR_NOERRCODE 23
ISR_NOERRCODE 24
ISR_NOERRCODE 25
ISR_NOERRCODE 26
ISR_NOERRCODE 27
ISR_NOERRCODE 28
ISR_NOERRCODE 29
ISR_NOERRCODE 30
ISR_NOERRCODE 31

/* ----------------------------------------------------------------------------
 * IRQ 硬件中断入口 (中断号 32-47)
 * IRQ0-7 -> 32-39, IRQ8-15 -> 40-47
 * -------------------------------------------------------------------------- */
.macro IRQ_HANDLER num
.global irq\num
irq\num:
    cli
    pushl $0
    pushl $\num
    jmp irq_common_stub
.endm

IRQ_HANDLER 0
IRQ_HANDLER 1
IRQ_HANDLER 2
IRQ_HANDLER 3
IRQ_HANDLER 4
IRQ_HANDLER 5
IRQ_HANDLER 6
IRQ_HANDLER 7
IRQ_HANDLER 8
IRQ_HANDLER 9
IRQ_HANDLER 10
IRQ_HANDLER 11
IRQ_HANDLER 12
IRQ_HANDLER 13
IRQ_HANDLER 14
IRQ_HANDLER 15

/* IRQ 通用处理 */
.extern irq_handler
.extern need_resched
.extern schedule
.extern current
irq_common_stub:
    pusha
    mov %ds, %ax
    pushl %eax
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    movl 36(%esp), %eax   /* irq_number */
    pushl %eax
    call irq_handler
    addl $4, %esp

    /* 检查是否需要调度 */
    cmpl $0, need_resched
    je .irq_no_sched

    /* 保存当前esp到current->kernel_esp (偏移52) */
    movl current, %eax
    test %eax, %eax
    jz .irq_do_sched
    movl %esp, 52(%eax)

.irq_do_sched:
    /* 调用调度器 (更新current) */
    call schedule

    /* V3.81 修复: current==NULL(内核主线无进程)时禁止切栈!
     * 此前无条件读 current+52/64(NULL偏移=IVT区垃圾)当栈/页目录,
     * 一旦 syscall 演示置 need_resched=1 且时钟中断恰在窗口触发,
     * 就切到垃圾栈 → pusha/pop 写坏 .data(syscall_table 被破坏)
     * → 后续 syscall 分发 jmpl 0x460 → #GP 崩溃(与 serial_puts 竞态同源) */
    movl current, %eax
    test %eax, %eax
    jz .irq_no_sched

    /* V2.22+V3.86: 先切页目录(新进程page_dir, 偏移68), 再切内核栈.
     * 否则用户态进程切换时, 新进程内核栈在其自身页目录才有映射,
     * 旧页目录(前一用户进程)下无映射 → 切栈后 #PF/垃圾. */
    movl current, %eax
    movl 68(%eax), %ebx
    test %ebx, %ebx
    jz 9f
    movl %ebx, %cr3
9:
    /* 从新current->kernel_esp恢复栈 (偏移52) */
    movl 52(%eax), %esp

.irq_no_sched:
    popl %eax
    /* V3.81 韧性: 段值校验(同 isr_common_stub, 防损坏栈导致 #GP 循环) */
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    popa
    addl $8, %esp
    sti
    iret

/* ----------------------------------------------------------------------------
 * 系统调用入口 (int 0x80)
 * 约定: eax=调用号, ebx=arg1, ecx=arg2, edx=arg3, esi=arg4
 * 返回值放在 eax
 * -------------------------------------------------------------------------- */
.global isr128
.extern syscall_handler
isr128:
    cli
    /* V2.22统一帧布局: 补两个dummy字(irq_number+error_code), 使本入口帧与
     * process_create 构造的初始帧一致(ds+pusha+dummy+dummy+iret帧).
     * 这样尾部 addl $8 + iret 在"普通syscall返回"与"调度切换首次进入新进程"
     * 两种路径下都正确; 不加则 addl $8 会吃掉iret帧的EIP → 卡死/乱跳. */
    pushl $0                /* dummy irq_number */
    pushl $0                /* dummy error_code */
    pusha                   /* 保存所有寄存器 */

    mov %ds, %ax
    pushl %eax
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs

    /* V6.65: 保存当前esp到current->kernel_esp，供fork使用
     * current->kernel_esp只在调度切换时更新，fork时需要当前系统调用的栈帧
     * 此时esp指向ds位置，栈布局: ds,pusha(32B),dummy_irq,dummy_err,eip,cs,eflags,useresp,ss */
    movl current, %eax
    test %eax, %eax
    jz 1f
    movl %esp, 52(%eax)      /* 52 = kernel_esp在PCB中的偏移 */
1:

    /* V6.47: int 0x80入口调试——已临时禁用(偏移量错误可能破坏栈) */

    /* 调用 syscall_handler(eax, ebx, ecx, edx, esi)
     * 正确栈布局: pusha压入顺序EAX,ECX,EDX,EBX,ESP,EBP,ESI,EDI,
     * 因栈向下增长, 从低地址到高地址为: EDI,ESI,EBP,ESP,EBX,EDX,ECX,EAX.
     * push ds后esp=ds位置:
     *   ds@esp+0, edi@esp+4, esi@esp+8, ebp@esp+12, esp@esp+16,
     *   ebx@esp+20, edx@esp+24, ecx@esp+28, eax@esp+32.
     * 逐参数 push 时 esp 递减4, 偏移量需同步补偿:
     */
    pushl 8(%esp)           /* arg4=esi   (esp+8) */
    pushl 28(%esp)          /* arg3=edx   (原esp+24, push后+4=28) */
    pushl 36(%esp)          /* arg2=ecx   (原esp+28, push后+8=36) */
    pushl 32(%esp)          /* arg1=ebx   (原esp+20, push后+12=32) */
    pushl 48(%esp)          /* 调用号=eax (原esp+32, push后+16=48) */
    call syscall_handler
    addl $20, %esp          /* 清理5个参数 */

    /* 把返回值写入 pusha 保存的 eax 槽:
     * 调用syscall_handler后esp=S(S=ds位置), eax槽@S+32,
     * 所以偏移量=32(%esp)=S+32.
     * V6.64修复: 原偏移量36是错误的, 导致返回值写入eax槽后4字节(ecx槽),
     *            eax槽保持系统调用号不变, 用户态看到的返回值是系统调用号本身.
     *            正确偏移量是32. */
    mov %eax, 32(%esp)      /* 修改pusha保存的eax */

    /* V2.22修复: 调度检查必须在 pop ds 之前! 此时栈上还有 pusha+ds 帧.
     * 保存的 esp 与 process_create 构造的初始帧布局一致:
     *   [低] ds, pusha(32B), error_code, irq_number, [高] eip/cs/eflags/(ss,esp)
     * 切回路径: pop ds → popa → addl $8 → iret, 与 irq_common_stub 完全一致.
     * 之前把调度检查放 pop ds 之后, 导致与构造帧错位 → 切到 procb 时
     * popa 把 ds 当 edi, iret 把 error_code 当 eip → 除零异常. */
    cmpl $0, need_resched
    je .syscall_no_sched
    movl current, %eax
    test %eax, %eax
    jz .syscall_no_sched
    movl %esp, 52(%eax)      /* 保存当前esp(含pusha+ds帧) → current->kernel_esp */
    call schedule            /* 选next, 更新current, 设TSS.esp0 */
    movl current, %eax
    movl 68(%eax), %ebx      /* 切页目录(偏移68,V3.86修正) */
    movl 52(%eax), %ecx      /* V6.66: 切换CR3前先保存kernel_esp，避免CR3切换后访问current读到错误值 */
    test %ebx, %ebx
    jz 9f
    movl %ebx, %cr3
9:
    movl %ecx, %esp          /* 切到新进程保存的esp(含其pusha+ds帧) */
.syscall_no_sched:
    popl %eax               /* 恢复 ds */
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    popa                    /* 恢复寄存器，eax=返回值 */
    addl $8, %esp           /* 跳过 error_code + irq_number */
    sti
    iret

/* ----------------------------------------------------------------------------
 * 通用中断处理：保存上下文，调用C函数
 * 栈布局(从低到高): isr_number, error_code, eax, ecx, edx, ebx, esp, ebp, esi, edi, ds
 * -------------------------------------------------------------------------- */
.extern idt_handler
isr_common_stub:
    pusha               /* 保存 edi,esi,ebp,esp,ebx,edx,ecx,eax */
    mov %ds, %ax
    pushl %eax

    mov $0x10, %ax      /* 内核数据段 */
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs

    /* 调用 idt_handler(isr_number, error_code, ebx, ecx, edx, esi)
     * V3.87 B-2 修复: pusha+push ds 后实际栈布局(esp=A-4, A=pusha基址):
     *   ds@A-4  edi@A  esi@A+4  ebp@A+8  esp@A+12  ebx@A+16
     *   edx@A+20 ecx@A+24 eax@A+28 isr@A+32 err@A+36 EIP@A+40
     * 旧偏移按"无ds"布局算, 未补偿4字节 → isr_number/error_code/寄存器
     * 全部错位 → #PF 被误判为 ISR 1、err 读到用户 ecx 等. 现按
     * isr128 同款 +4 补偿(逐参数 push 时 esp 递减4, 偏移同步+4):
     *   arg5=esi@A+4        → 首次 push 时 8(%esp)=A+4
     *   arg4=edx@A+20       → 二次 push 时 28(%esp)=A+20
     *   arg3=ecx@A+24       → 三次 push 时 36(%esp)=A+24
     *   arg2=ebx@A+16       → 四次 push 时 32(%esp)=A+16
     *   arg1=error_code@A+36 → 五次 push 时 56(%esp)=A+36
     *   arg0=isr_number@A+32 → 六次 push 时 56(%esp)=A+32
     */
    pushl 8(%esp)        /* arg5=esi        (A+4)   */
    pushl 28(%esp)       /* arg4=edx        (A+20)  */
    pushl 36(%esp)       /* arg3=ecx        (A+24)  */
    pushl 32(%esp)       /* arg2=ebx        (A+16)  */
    pushl 56(%esp)       /* arg1=error_code (A+36)  */
    pushl 56(%esp)       /* arg0=isr_number (A+32)  */
    /* E-44: 记录 iret 帧——内核态异常(无特权切换) EIP@A+40/CS@A+44/EFLAGS@A+48;
     * 此时 esp=A-28, 故偏移 +68/+72/+76. 栈被破坏时也能看到将弹出的目标. */
    movl 68(%esp), %eax
    movl %eax, idt_last_eip
    movl 72(%esp), %eax
    movl %eax, idt_last_cs
    movl 76(%esp), %eax
    movl %eax, idt_last_eflags
    /* V4.00-E45: 记录 pusha 保存的异常时 ESP(@A+12)——区分"栈指针坏"
     * (esp≈低1MB/VGA) 与"栈内容坏"(esp 高地址但 iret 帧 EIP 被踩). */
    movl 40(%esp), %eax
    movl %eax, idt_last_esp
    call idt_handler
    addl $24, %esp       /* 清理6个参数 */

    popl %eax           /* 恢复 ds */
    /* V3.81 韧性: 段值校验——若保存的 ds 已损坏(栈/内存被破坏时常见),
     * 加载无效 selector 会 #GP → isr 恢复路径反复 #GP 死循环.
     * 当前无 ring3(用户态 TBD), 强制内核数据段 0x10 安全.
     * 未来实现用户态时, 需改为仅校验低16位=合法GDT索引, 非法才回落 0x10. */
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    popa                /* 恢复通用寄存器 */
    addl $8, %esp       /* 清理 isr_number 和 error_code */
    sti
    iret
