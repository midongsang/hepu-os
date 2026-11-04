/* ============================================================================
 * 赫普OS - 32位Multiboot入口点
 * V0.10.6: 从Multiboot启动，切换到64位长模式
 * ========================================================================== */

.section .text.startup
.code32

/* Multiboot魔数 */
.set MULTIBOOT2_MAGIC, 0x36D76289
.set MULTIBOOT_MAGIC, 0x2BADB002

/* 页表位置（在0x1000处，避免覆盖内核） */
.set PML4_ADDR, 0x1000
.set PDPT_ADDR, 0x2000
.set PD_ADDR,   0x3000

/* ============================================================================
 * Multiboot入口点（32位保护模式）
 * 输入：EAX = magic, EBX = multiboot_info指针
 * ========================================================================== */
.global multiboot_entry
multiboot_entry:
    /* 保存Multiboot信息 */
    movl %eax, %esi          /* ESI = magic */
    movl %ebx, %edi          /* EDI = mb_info */

    /* 检查是否支持长模式 */
    movl $0x80000001, %eax
    cpuid
    testl $(1 << 29), %edx   /* LM位 */
    jz no_long_mode

    /* 设置基本页表：身份映射前2MB */
    movl $PML4_ADDR, %eax
    movl %eax, %cr3

    /* 清空页表 */
    movl $PML4_ADDR, %edi
    movl $0, %eax
    movl $1024, %ecx
    rep stosl

    /* PML4[0] -> PDPT */
    movl $PML4_ADDR, %eax
    movl $(PDPT_ADDR | 0x3), (%eax)  /* present + writable */

    /* PDPT[0] -> PD */
    movl $PDPT_ADDR, %eax
    movl $(PD_ADDR | 0x3), (%eax)

    /* PD[0..7] -> 2MB大页（身份映射0-16MB，覆盖8MB KHEAP bss） */
    movl $PD_ADDR, %edi
    movl $0x83, %eax      /* present + writable + large page */
    movl $8, %ecx
1:
    movl %eax, (%edi)
    addl $0x200000, %eax
    addl $8, %edi
    loop 1b

    /* 启用PAE */
    movl %cr4, %eax
    orl $(1 << 5), %eax      /* PAE位 */
    movl %eax, %cr4

    /* 启用长模式 */
    movl $0xC0000080, %ecx   /* EFER MSR */
    rdmsr
    orl $(1 << 8), %eax      /* LME位 */
    wrmsr

    /* 启用分页（同时启用长模式） */
    movl %cr0, %eax
    orl $(1 << 31), %eax     /* PG位 */
    movl %eax, %cr0

    /* 加载64位GDT */
    lgdt gdt64_ptr

    /* 远跳转到64位代码段 */
    ljmp $0x08, $long_mode_entry

no_long_mode:
    /* CPU不支持长模式，显示错误并停机 */
    movl $0xB8000, %edi
    movl $'L' | (0x4F << 8), (%edi)
    movl $'M' | (0x4F << 8), 2(%edi)
    movl $' ' | (0x4F << 8), 4(%edi)
    movl $'N' | (0x4F << 8), 6(%edi)
    movl $'O' | (0x4F << 8), 8(%edi)
1:
    hlt
    jmp 1b

/* ============================================================================
 * 64位长模式入口
 * ========================================================================== */
.code64
long_mode_entry:
    /* 设置段寄存器 */
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss

    /* 设置内核栈 */
    movq $kernel_stack_top, %rsp
    movq %rsp, %rbp

    /* 传递Multiboot信息给64位入口 */
    movl %esi, %eax          /* EAX = magic */
    movq %rdi, %rbx          /* RBX = mb_info */

    /* 跳转到64位内核主入口 */
    jmp x86_64_entry

/* ============================================================================
 * 64位GDT
 * ========================================================================== */
.section .rodata
.align 16
gdt64:
    /* 0x00: 空描述符 */
    .quad 0
    /* 0x08: 内核代码段（64位） */
    .quad 0x00209A0000000000
    /* 0x10: 内核数据段 */
    .quad 0x0000920000000000

gdt64_ptr:
    .word gdt64_ptr - gdt64 - 1
    .quad gdt64

/* 内核栈（在kernel_main_64.c中定义） */
.extern kernel_stack_top
.extern x86_64_entry

/* ============================================================================
 * PVH ELF Note（用于QEMU -kernel加载64位内核）
 * type=13 (XEN_ELFNOTE_PHYS32_ENTRY), desc=32位入口点
 * ========================================================================== */
.section .note.Xen, "", @note
.balign 4
note_start:
    .long note_name_end - note_name_start  /* namesz = 4 ("Xen\0") */
    .long note_desc_end - note_desc_start  /* descsz = 4 */
    .long 13                               /* type = XEN_ELFNOTE_PHYS32_ENTRY */
note_name_start:
    .asciz "Xen"
note_name_end:
    .balign 4
note_desc_start:
    .long multiboot_entry                  /* 32位入口点 */
note_desc_end:
    .balign 4
