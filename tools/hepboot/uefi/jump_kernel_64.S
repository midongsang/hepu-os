/*
 * HepuBoot UEFI - 64位长模式直接跳转内核
 * 参数: rdi=kernel_entry, rsi=multiboot_info_addr, rdx=stack_ptr
 *
 * 版本: v0.02 (修复寄存器保存：使用r12-r14避免被rep movsb破坏)
 * 日期: 2026-10-10
 */

.section .text
.global jump_to_kernel_64
.type jump_to_kernel_64, @function

jump_to_kernel_64:
    cli
    
    /* 保存参数到被调用者保存的寄存器（不会被rep movsb破坏） */
    mov %rdi, %r12      /* r12 = kernel_entry */
    mov %rsi, %r13      /* r13 = multiboot_info_addr */
    mov %rdx, %r14      /* r14 = stack_ptr (低内存目标地址) */

    /* 拷贝切换代码到低内存 (identity mapping区域) */
    mov %r14, %rdi      /* 目标地址 = stack_ptr */
    leaq low_mem_code_64(%rip), %rsi  /* 源地址 */
    mov $(low_mem_code_64_end - low_mem_code_64), %rcx  /* 长度 */
    rep movsb
    
    /* 跳转到低内存代码 */
    jmp *%r14

low_mem_code_64:
    .code64
    /* 计算GDT地址（当前指令位置 + 偏移） */
    call 1f
1:  pop %rax
    add $(gdt_data_64 - 1b), %rax
    mov %rax, %rbp

    /* 加载64位GDT */
    leaq gdt_desc_64 - gdt_data_64(%rbp), %rcx
    movq %rbp, 2(%rcx)
    lgdt (%rcx)

    /* 切换栈到低内存 */
    movq %r14, %rsp

    /* 设置数据段(0x10) */
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw %ax, %fs
    movw %ax, %gs

    /* 重新加载 CS 为本 GDT 的 0x08。
     * UEFI 固件通常用 CS=0x38, 若直接跳入内核, 中断 iretq 用内核 GDT
     * 恢复 0x38(无效选择子) 会触发 #GP(0x38)。 */
    leaq 1f(%rip), %rax
    pushq $0x08
    pushq %rax
    lretq
1:

    /* 设置multiboot约定: eax=0x2BADB002(magic), ebx=multiboot_info_addr */
    movl $0x2BADB002, %eax
    movq %r13, %rbx      /* rbx = multiboot_info_addr */

    /* 直接跳转到64位内核入口 */
    jmp *%r12
    hlt

    .align 16
gdt_data_64:
    .quad 0x0000000000000000     /* 0x00: null */
    .quad 0x00AF9A000000FFFF     /* 0x08: 64位代码段 (L=1, D=0) */
    .quad 0x00AF92000000FFFF     /* 0x10: 64位数据段 */
gdt_desc_64:
    .word 23
    .long 0
    .long 0
low_mem_code_64_end:
