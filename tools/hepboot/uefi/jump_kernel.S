/*
 * HepuBoot UEFI - x64长模式切换到32位保护模式并跳转内核
 * 参数: rdi=kernel_entry, rsi=stack_ptr, rdx=low_mem_addr
 *
 * 关键技术点(经过大量调试验证):
 *   1. lgdt后CS隐藏缓存仍有效, 可直接lretq加载32位代码段, 无需先重载64位CS
 *   2. 必须在64位模式下禁用长模式(LME=0), 然后立即lretq切32位
 *      (在兼容模式下wrmsr禁用长模式会失败)
 *   3. 禁用长模式后立即lretq, 中间不能有其他指令
 *   4. 切换代码必须拷贝到低内存(identity mapping区域)执行
 *   5. 禁用分页前栈必须在低内存
 */

.section .text
.global jump_to_kernel_32
.type jump_to_kernel_32, @function

jump_to_kernel_32:
    cli
    mov %edi, %ebx      /* kernel_entry */
    mov %esi, %ecx      /* stack_ptr */
    mov %rdx, %rdi      /* low_mem_addr */

    /* 拷贝切换代码到低内存 */
    leaq low_mem_code(%rip), %rsi
    mov $(low_mem_code_end - low_mem_code), %rcx
    rep movsb
    jmp *%rdx

low_mem_code:
    .code64
    /* 计算GDT地址 */
    call 1f
1:  pop %rax
    add $(gdt_data - 1b), %rax
    mov %rax, %rbp

    /* 加载GDT */
    leaq gdt_desc - gdt_data(%rbp), %rcx
    movq %rbp, 2(%rcx)
    lgdt (%rcx)

    /* 切换栈到低内存 (0x80000-0x8FFFF区域，避免与0x90000的multiboot_info冲突) */
    movq $0x8F000, %rsp

    /* 在64位模式下禁用长模式(LME=0) */
    mov $0xC0000080, %ecx
    rdmsr
    and $~0x00000100, %eax
    wrmsr

    /* 立即lretq切换到32位代码段(0x08) - wrmsr后无其他指令 */
    pushq $0x08
    leaq entry_32(%rip), %rax
    pushq %rax
    lretq

entry_32:
    .code32
    /* 禁用分页 */
    mov %cr0, %eax
    and $~0x80000000, %eax
    mov %eax, %cr0

    /* 禁用PAE */
    mov %cr4, %eax
    and $~0x00000020, %eax
    mov %eax, %cr4

    /* 设置数据段(0x10) */
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw %ax, %fs
    movw %ax, %gs

    /* 设置栈指针 (0x8F000，避免与0x90000的multiboot_info冲突) */
    movl $0x8F000, %esp

    /* 设置multiboot约定: eax=0x2BADB002(magic), ebx=0x90000(multiboot info) */
    movl $0x2BADB002, %eax
    movl $0x90000, %ebx

    /* 跳转到内核入口 _start (0x101000, 4K对齐的.text段起始)
       注意: 不是0x100000! 0x100000是multiboot头和引导存根,
       _start在0x101000(由内核链接脚本.text ALIGN(4K)决定) */
    movl $0x101000, %ecx
    jmp *%ecx
    hlt

    .align 16
gdt_data:
    .quad 0x0000000000000000     /* 0x00: null */
    .quad 0x00CF9A000000FFFF     /* 0x08: 32位代码段 */
    .quad 0x00CF92000000FFFF     /* 0x10: 32位数据段 */
gdt_desc:
    .word 23
    .long 0
    .long 0
low_mem_code_end:
