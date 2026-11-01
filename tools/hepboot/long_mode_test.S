/* ============================================================================
 * 赫普OS - 64位长模式切换测试
 * 独立测试程序，验证从32位保护模式切换到64位长模式
 * ========================================================================== */

.code32
.section .text
.global _start

/* Multiboot头在0x100000（内核加载地址） */
.align 4
mb_header:
    .long 0x1BADB002              /* magic */
    .long 0x00000000              /* flags */
    .long -(0x1BADB002 + 0x00000000)  /* checksum */

/* 填充到0x101000（内核入口地址，相对于段起始0x100000偏移0x1000） */
.org 0x1000

_start:
    /* 此时在32位保护模式下，由引导器跳转过来 */

    /* 调试：显示"LM0"表示进入测试程序 */
    movl $0xB8000 + 160*3, %edi   /* 第4行 */
    movb $'L', %al
    movb $0x1F, %ah               /* 蓝底白字 */
    stosw
    movb $'M', %al
    stosw
    movb $'0', %al
    stosw

    /* 1. 检测CPU是否支持长模式 */
    movl $0x80000000, %eax
    cpuid
    cmpl $0x80000001, %eax
    jb no_long_mode

    movl $0x80000001, %eax
    cpuid
    testl $(1 << 29), %edx      /* LM位 */
    jz no_long_mode

    /* 调试：显示"LM1"表示CPU支持长模式 */
    movl $0xB8000 + 160*3 + 6, %edi
    movb $'1', %al
    movb $0x1F, %ah
    stosw

    /* 2. 构建初始4级页表（PML4） */
    /* 页表放在0x200000（2MB）处，共4页（16KB），避免覆盖内核代码 */
    movl $0x200000, %edi
    movl $0, %eax
    movl $4096, %ecx
    rep stosl                   /* 清零16KB */

    /* PML4[0] -> PDPT (0x201000) */
    movl $0x201000, %eax
    orl $0x03, %eax             /* PRESENT | WRITABLE */
    movl %eax, 0x200000

    /* PDPT[0] -> PD (0x202000) */
    movl $0x202000, %eax
    orl $0x03, %eax
    movl %eax, 0x201000

    /* PD[0] -> PT (0x203000) */
    movl $0x203000, %eax
    orl $0x03, %eax
    movl %eax, 0x202000

    /* PT: 映射前2MB物理内存到虚拟地址0x00000000 */
    movl $0x203000, %edi
    movl $0x00000003, %eax      /* 第一个页：0x00000000, PRESENT|WRITABLE */
    movl $512, %ecx
1:
    movl %eax, (%edi)
    addl $0x1000, %eax          /* 下一页 */
    addl $8, %edi
    loop 1b

    /* 同时映射前2MB到高地址0xFFFFFFFF80000000（内核高半核） */
    /* PML4[511] -> PDPT (0x204000) */
    movl $0x204000, %eax
    orl $0x03, %eax
    movl %eax, 0x200000 + 511*8

    /* PDPT[511] -> PD (0x205000) */
    movl $0x205000, %eax
    orl $0x03, %eax
    movl %eax, 0x204000 + 511*8

    /* PD[511] -> PT (0x206000) */
    movl $0x206000, %eax
    orl $0x03, %eax
    movl %eax, 0x205000 + 511*8

    /* PT: 映射前2MB到0xFFFFFFFF80000000 */
    movl $0x206000, %edi
    movl $0x00000003, %eax
    movl $512, %ecx
2:
    movl %eax, (%edi)
    addl $0x1000, %eax
    addl $8, %edi
    loop 2b

    /* 3. 加载PML4到CR3 */
    movl $0x200000, %eax
    movl %eax, %cr3

    /* 调试：显示"LM2"表示页表已加载 */
    movl $0xB8000 + 160*3 + 12, %edi
    movb $'2', %al
    movb $0x1F, %ah
    stosw

    /* 4. 启用PAE（CR4.PAE） */
    movl %cr4, %eax
    orl $(1 << 5), %eax         /* PAE位 */
    movl %eax, %cr4

    /* 调试：显示"LM3"表示PAE已启用 */
    movl $0xB8000 + 160*3 + 18, %edi
    movb $'3', %al
    movb $0x1F, %ah
    stosw

    /* 5. 启用长模式（EFER.LME） */
    movl $0xC0000080, %ecx      /* EFER MSR */
    rdmsr
    orl $(1 << 8), %eax         /* LME位 */
    wrmsr

    /* 调试：显示"LM4"表示LME已启用 */
    movl $0xB8000 + 160*3 + 24, %edi
    movb $'4', %al
    movb $0x1F, %ah
    stosw

    /* 6. 启用分页（CR0.PG），此时进入长模式兼容模式 */
    movl %cr0, %eax
    orl $(1 << 31), %eax        /* PG位 */
    movl %eax, %cr0

    /* 调试：显示"LM5"表示分页已启用 */
    movl $0xB8000 + 160*3 + 30, %edi
    movb $'5', %al
    movb $0x1F, %ah
    stosw

    /* 7. 加载64位GDT */
    lgdt gdt64_ptr

    /* 8. 远跳转到64位代码段 */
    ljmp $0x08, $long_mode_entry

no_long_mode:
    /* CPU不支持长模式，显示错误 */
    movl $0xB8000, %edi
    movb $'E', %al
    movb $0x4F, %ah
    stosw
    movb $'R', %al
    stosw
    movb $'R', %al
    stosw
1:
    hlt
    jmp 1b

.code64
long_mode_entry:
    /* 成功进入64位长模式！ */
    /* 设置段寄存器 */
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss

    /* 设置栈 */
    movq $0x208000, %rsp
    movq %rsp, %rbp

    /* 显示"LM OK"到VGA文本模式 */
    movq $0xB8000, %rdi
    movb $'L', %al
    movb $0x2F, %ah             /* 绿底白字 */
    stosw
    movb $'M', %al
    stosw
    movb $' ', %al
    stosw
    movb $'O', %al
    stosw
    movb $'K', %al
    stosw

    /* 验证长模式（读EFER） */
    movl $0xC0000080, %ecx
    rdmsr
    testl $(1 << 10), %eax      /* LMA位（长模式激活） */
    jz long_mode_error

    /* 显示"LMA OK" */
    movq $0xB8000 + 160, %rdi   /* 第二行 */
    movb $'L', %al
    movb $0x2F, %ah
    stosw
    movb $'M', %al
    stosw
    movb $'A', %al
    stosw
    movb $' ', %al
    stosw
    movb $'O', %al
    stosw
    movb $'K', %al
    stosw

    /* 停机 */
1:
    hlt
    jmp 1b

long_mode_error:
    movq $0xB8000 + 160, %rdi
    movb $'E', %al
    movb $0x4F, %ah
    stosw
    movb $'R', %al
    stosw
    movb $'R', %al
    stosw
1:
    hlt
    jmp 1b

/* 64位GDT */
.align 16
gdt64:
    .quad 0                     /* 空描述符 */
    .quad 0x00AF9A000000FFFF   /* 代码段：64位，DPL=0，可执行 */
    .quad 0x00AF92000000FFFF   /* 数据段：64位，DPL=0，可写 */

gdt64_ptr:
    .word gdt64_ptr - gdt64 - 1
    .long gdt64                 /* 32位地址（低内存运行） */
    .long 0                     /* 高32位为0 */
