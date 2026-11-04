# 0 "hepboot.S"
# 0 "<built-in>"
# 0 "<command-line>"

# 1 "/usr/include/stdc-predef.h" 1 3 4

# 17 "/usr/include/stdc-predef.h" 3 4



















# 45 "/usr/include/stdc-predef.h" 3 4

# 55 "/usr/include/stdc-predef.h" 3 4









# 1 "<command-line>" 2
# 1 "hepboot.S"

# 14 "hepboot.S"


# 33 "hepboot.S"

# 43 "hepboot.S"














                                     





    .code16
    .section .text
    .globl _start
_start:
    cli
    
    xorw %ax, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw $0x7C00, %sp       
    movb %dl, boot_drive        
    

    xorw %ax, %ax
    movw %ax, %ds
    movw %ax, 0x4000
    movw %ax, 0x4002
    movb %al, 0x4004

# 96 "hepboot.S"
    movb $0, vga_row
    movb $0, vga_col

    
    pushw %es
    movw $0xB800, %ax
    movw %ax, %es
    
    xorw %di, %di
    movw $240, %cx
    movw $0x1F20, %ax  
    rep stosw
    
    movw $0, %di
    movw $0x1F48, %ax  
    stosw
    movw $0x1F65, %ax  
    stosw
    movw $0x1F70, %ax  
    stosw
    movw $0x1F75, %ax  
    stosw
    movw $0x1F42, %ax  
    stosw
    movw $0x1F6F, %ax  
    stosw
    movw $0x1F6F, %ax  
    stosw
    movw $0x1F74, %ax  
    stosw
    popw %es
    movb $1, vga_row
    movb $0, vga_col

    
    movw $msg_boot, %si
    call print_str
    
    call check_f8_and_menu
    


    
    movw $msg_gdt, %si
    call print_str
    
    movw $msg_a20, %si
    call print_str
    
    movw $msg_reading, %si
    call print_str
    
    movw $msg_kernel_lba, %si
    call print_str
    movl $21, %eax
    movw %ax, %cx
    shrl $16, %eax
    call print_hex16
    movw %cx, %ax
    call print_hex16

    
    call read_kernel
    testb %al, %al
    jne kernel_read_ok
    
    movw $0xB800, %ax
    movw %ax, %es
    movw $160*2, %di
    movw $0x4F20, %ax  
    movw $80, %cx
    rep stosw
    movw $160*2+2*10, %di
    movw $0x4F4B, %ax  
    stosw
    movw $0x4F45, %ax  
    stosw
    movw $0x4F52, %ax  
    stosw
    movw $0x4F4E, %ax  
    stosw
    movw $0x4F45, %ax  
    stosw
    movw $0x4F4C, %ax  
    stosw
    movw $0x4F20, %ax  
    stosw
    movw $0x4F52, %ax  
    stosw
    movw $0x4F45, %ax  
    stosw
    movw $0x4F41, %ax  
    stosw
    movw $0x4F44, %ax  
    stosw
    movw $0x4F20, %ax  
    stosw
    movw $0x4F45, %ax  
    stosw
    movw $0x4F52, %ax  
    stosw
    movw $0x4F52, %ax  
    stosw
    movw $0x4F4F, %ax  
    stosw
    movw $0x4F52, %ax  
    stosw
    xorw %ax, %ax
    movw %ax, %es
    
    movb $4, vga_row
    movb $0, vga_col
    movw $msg_press_reset, %si
    call print_str
    
    sti
kernel_read_fail_loop:
    movb $0x01, %ah
    int $0x16
    jz kernel_read_fail_loop
    movb $0x00, %ah
    int $0x16
    movb $0xFE, %al
    outb %al, $0x64
kernel_read_fail_halt:
    hlt
    jmp kernel_read_fail_halt
kernel_read_ok:
    
    pushw %es
    movw $0xB800, %ax
    movw %ax, %es
    movw $0x600, %di
    movw $('R' | 0x0F00), %es:(%di)
    movw $('1' | 0x0F00), %es:2(%di)
    popw %es
    
# 243 "hepboot.S"
    







    
    
    movw $0xB800, %ax
    movw %ax, %es
    movw $('E' | 0x0F00), %es:160*0+2*12
    xorw %ax, %ax
    movw %ax, %es
    

    
    
    call collect_e820
    



    
# 281 "hepboot.S"
    
    call build_mbinfo

    



    movw $0x2401, %ax
    int $0x15
    inb $0x92, %al
    orb $0x02, %al
    outb %al, $0x92

# 307 "hepboot.S"
    
    movw $msg_prot, %si
    call print_str
    
    pushw %es
    movw $0xB800, %ax
    movw %ax, %es
    movw $0x600, %di
    movw $('G' | 0x0F00), %es:(%di)
    movw $('O' | 0x0F00), %es:2(%di)
    popw %es
    



    
    
    call enter_protected
    
    movw $0xB800, %ax
    movw %ax, %es
    movw $160*4, %di
    movw $0x4F20, %ax
    movw $80, %cx
    rep stosw
    movw $160*4+2*8, %di
    movw $0x4F42, %ax  
    stosw
    movw $0x4F4F, %ax  
    stosw
    movw $0x4F4F, %ax  
    stosw
    movw $0x4F54, %ax  
    stosw
    movw $0x4F20, %ax  
    stosw
    movw $0x4F46, %ax  
    stosw
    movw $0x4F41, %ax  
    stosw
    movw $0x4F49, %ax  
    stosw
    movw $0x4F4C, %ax  
    stosw
    
    movw $160*5, %di
    movw $0x4F20, %ax
    movw $80, %cx
    rep stosw
    movw $160*5+2*8, %di
    movw $0x4F45, %ax  
    stosw
    movw $0x4F52, %ax  
    stosw
    movw $0x4F52, %ax  
    stosw
    movw $0x4F3A, %ax  
    stosw
    movb %al, %bl  
    call print_error_code_vga
    xorw %ax, %ax
    movw %ax, %es
    
    cli
1:
    hlt
    jmp 1b


print_error_code_vga:
    pushw %ax
    pushw %cx
    movb %bl, %al
    movb $0x0F, %ah
    
    movb %al, %cl
    shrb $4, %cl
    andb $0x0F, %cl
    cmpb $10, %cl
    jb 1f
    addb $7, %cl
1:
    addb $0x30, %cl
    movw %cx, %es:(%di)
    addw $2, %di
    
    movb %al, %cl
    andb $0x0F, %cl
    cmpb $10, %cl
    jb 2f
    addb $7, %cl
2:
    addb $0x30, %cl
    movw %cx, %es:(%di)
    addw $2, %di
    popw %cx
    popw %ax
    ret

halt_loop:
    
    pushw %es
    movw $0xB800, %ax
    movw %ax, %es
    movw $160*6, %di
    movw $0x4F20, %ax
    movw $80, %cx
    rep stosw
    movw $160*6+2*10, %di
    movw $0x4F45, %ax  
    stosw
    movw $0x4F52, %ax  
    stosw
    movw $0x4F52, %ax  
    stosw
    movw $0x4F4F, %ax  
    stosw
    movw $0x4F52, %ax  
    stosw
    movw $0x4F20, %ax  
    stosw
    movw $0x4F4F, %ax  
    stosw
    movw $0x4F43, %ax  
    stosw
    movw $0x4F43, %ax  
    stosw
    movw $0x4F55, %ax  
    stosw
    movw $0x4F52, %ax  
    stosw
    movw $0x4F52, %ax  
    stosw
    movw $0x4F45, %ax  
    stosw
    movw $0x4F44, %ax  
    stosw
    popw %es
    
    sti
1:
    movb $0x01, %ah
    int $0x16
    jz 1b
    movb $0x00, %ah
    int $0x16
    movb $0xFE, %al
    outb %al, $0x64
2:
    hlt
    jmp 2b





print_str:
    pusha
    push %es
    movw $0xB800, %ax
    movw %ax, %es
1:
    lodsb
    testb %al, %al
    jz 2f
    cmpb $'\n', %al
    je 3f
    cmpb $'\r', %al
    je 1b
    
    movb %al, %cl
    
    movzbl vga_col, %edx
    movzbl vga_row, %eax
    imull $80, %eax, %eax
    addl %edx, %eax
    shll $1, %eax
    movl %eax, %edi
    movb $0x07, %ch          
    movw %cx, %es:(%edi)
    incb vga_col
    cmpb $80, vga_col
    jb 1b
    movb $0, vga_col
    incb vga_row
    jmp 1b
3:
    movb $0, vga_col
    incb vga_row
    jmp 1b
2:
    pop %es
    popa
    ret





print_hex8:
    pusha
    push %es
    movw $0xB800, %dx
    movw %dx, %es
    



    movb %al, %cl
    movb %cl, %ah
    
    movb %ah, %dl
    shrb $4, %dl
    call hex_digit
    
    movb %cl, %ah
    movb %ah, %dl
    andb $0x0F, %dl
    call hex_digit
    pop %es
    popa
    ret
hex_digit:
    cmpb $10, %dl
    jb 1f
    addb $('A' - '0' - 10), %dl
1:
    addb $'0', %dl
    movzbl vga_col, %edi
    movzbl vga_row, %eax
    imull $80, %eax, %eax
    addl %edi, %eax
    shll $1, %eax
    movb %dl, %es:(%eax)
    movb $0x07, %es:1(%eax)
    incb vga_col
    cmpb $80, vga_col
    jb 2f
    movb $0, vga_col
    incb vga_row
2:
    ret





print_hex16:
    push %eax
    movb %ah, %al
    call print_hex8
    pop %eax
    push %eax
    movb %al, %ah
    
    call print_hex8
    pop %eax
    ret






nibble_to_char:
    andb $0x0F, %al
    cmpb $10, %al
    jb 1f
    addb $('A' - '0' - 10), %al
1:
    addb $'0', %al
    ret





print_dec32:
    
    pusha
    push %es
    movl %eax, %edi          
    movw $0xB800, %ax
    movw %ax, %es
    movl $1000000000, %ebp   
    xorl %ecx, %ecx          
1:
    movl %edi, %eax
    xorl %edx, %edx
    divl %ebp                
    movl %edx, %ebx          
    testl %eax, %eax
    jnz 2f
    testl %ecx, %ecx
    jnz 2f
    cmpl $1, %ebp            
    je 2f
    jmp 3f
2:
    movb $1, %cl             
    addb $'0', %al
    pushl %eax
    
    movzbl vga_col, %edx
    movzbl vga_row, %eax
    imull $80, %eax, %eax
    addl %edx, %eax
    shll $1, %eax
    movl %eax, %edi
    popl %eax
    movb %al, %cl
    movb $0x07, %ch
    movw %cx, %es:(%edi)
    incb vga_col
    cmpb $80, vga_col
    jb 3f
    movb $0, vga_col
    incb vga_row
3:
    movl %ebx, %edi          
    movl %ebp, %eax
    xorl %edx, %edx          
    movl $10, %esi           
    divl %esi                
    movl %eax, %ebp
    testl %ebp, %ebp
    jnz 1b
    pop %es
    popa
    ret




delay:
    pusha
    movl $600000, %ecx
1:
    decl %ecx
    jnz 1b
    popa
    ret










# 667 "hepboot.S"





check_int13_ext:
    pushal
    movb boot_drive, %dl
    orb $0x80, %dl
    movb $0x41, %ah
    movw $0x55AA, %bx
    int $0x13
    jc 1f
    
    movw $msg_int13_ok, %si
    call print_str
    jmp 2f
1:
    
    movw $msg_int13_no, %si
    call print_str
2:
    popal
    ret

read_kernel:
    
    call check_int13_ext
    
    movl $0x00010000, %edi      
    movl $1000, %ecx        
    movl $21, %ebp      
    




    call read_slice
    ret

read_seg2:
    
    movl $0x00010000, %edi
    movl $((214) - (1000)), %ecx
    movl $((21) + (1000)), %ebp
    call read_slice              
    ret

read_slice:
    .code16                 
    


    xorw %ax, %ax
    movw %ax, %ds
    cmpb $1, 0x4004
    jne 1f
    movb $0, 0x4004
    movw %ax, %ss              
    movw 0x4002, %sp            
    pushw $0x0800
    pushw 0x4000                 
    lret
1:
    pushal
    cli
    
    movb $1, 0x5000
    
    movb $0x10, dap_packet
    movw $1, dap_sectors
    
3:
    cli
    
    movb $2, 0x5000
    # V0.10.5: 每64扇区执行一次磁盘重置 - 临时禁用调试
    #pushl %eax
    #pushl %ecx
    #movl %ecx, %eax
    #andl $0x3F, %eax        # 每64次
    #cmpl $0, %eax
    #jne rs_skip_reset
    # 磁盘重置: AH=00H, DL=驱动器号
    #movb boot_drive, %dl
    #orb $0x80, %dl
    #movb $0x00, %ah
    #sti
    #int $0x13
    #cli
#rs_skip_reset:
    #popl %ecx
    #popl %eax
    


    movl %edi, %eax
    movl %eax, %edx
    andl $0xF, %edx
    movw %dx, dap_offset
    shrl $4, %eax
    movw %ax, dap_segment
    
    movl %ebp, dap_lba
    movl $0, dap_lba+4
    
    xorw %ax, %ax
    movw %ax, %ds
    movw $dap_packet, %si
    
    movb boot_drive, %dl
    orb $0x80, %dl
    movb $0x42, %ah
    
    pushl %ecx
    pushl %ebp
    pushl %edi
    pushl %ebx
    pushl %esi
    pushl %edx
    pushw %gs
    pushw %fs
    pushw %es
    pushw %ds
    
    movw %sp, 0x4002
    movw $int13_ret - 0x8000, 0x4000
    movb $1, 0x4004
    
    movb $4, 0x5000
    sti              
    int $0x13
int13_ret:
    cli
    movb $0, 0x4004
    
    movb $5, 0x5000
    
    pushfw                  
    popw %ax
    popw %ds
    popw %es
    popw %fs
    popw %gs
    popl %edx
    popl %esi
    popl %ebx
    popl %edi
    popl %ebp
    popl %ecx
    jc 1f                       
    jmp 2f
1:
    
    call read_sector_chs
    jc read_fail               
2:
    cli
    movl $1, %ebx
    addl %ebx, %ebp
    movl %ebx, %eax
    shll $9, %eax
    addl %eax, %edi
    subl %ebx, %ecx
    
    movb $6, 0x5000
    
    jnz 3b
2:
    sti                      
    
    pushw %es
    movw $0xB800, %ax
    movw %ax, %es
    movw $0x600, %di
    movw $('R' | 0x0F00), %es:(%di)
    movw $('S' | 0x0F00), %es:2(%di)
    popw %es
    popal
    
    movb $7, 0x5000
    movb $1, %al
    ret






read_sector_chs:
    .code16
    pushal
    pushw %ds
    pushw %es

    
    xorw %ax, %ax
    movw %ax, %ds
    movb boot_drive, %dl
    orb $0x80, %dl
    movb $0x08, %ah
    xorw %di, %di
    xorw %ax, %ax; movw %ax, %es
    int $0x13
    jc chs_fail

    

    movb %dh, %bl              
    incb %bl                   
    movb %cl, %bh
    andb $0x3F, %bh            

    



    movl %ebp, %eax            
    xorl %edx, %edx
    movzbl %bh, %ecx           
    divl %ecx                  
    movb %dl, %cl              
    xorl %edx, %edx
    movzbl %bl, %ecx           
    divl %ecx                  

    



    movb %al, %ch              
    movb %ah, %al              
    shlb $6, %al
    orb %al, %cl               
    addb $1, %cl               
    movb %dl, %dh              
    movb boot_drive, %dl
    orb $0x80, %dl
    movb $1, %al               
    movb $0x02, %ah            

    
    movl %edi, %eax
    shrl $4, %eax
    movw %ax, %es
    movw %di, %bx
    andw $0xF, %bx

    
    int $0x13
    jc chs_fail

    
    addl $1, %ebp
    addl $512, %edi
    subl $1, %ecx              
    clc                        
    jmp chs_done

chs_fail:
    stc                        
chs_done:
    popw %es
    popw %ds
    popal
    ret

read_fail:
    
    pushw %es
    pushw %ax
    movw $0xB800, %ax
    movw %ax, %es
    movb $('F' | 0x0F), %es:0x504
    popw %ax
    popw %es
    
    pushw %es
    movw $0xB800, %bx
    movw %bx, %es
    movb $0, vga_row
    movb $0, vga_col
    movw $msg_err_code, %si
    call print_str
    movb $('0'), %al
    call print_char
    movb $('3'), %al
    call print_char
    movb $(' '), %al
    call print_char
    popw %es
    




    pushw %es
    movw $0xB800, %bx
    movw %bx, %es
    movb $1, vga_row
    movb $0, vga_col
    
    movw $msg_err_ah, %si
    call print_str
    movb %ah, %al
    call print_hex8
    movw $msg_err_dl, %si
    call print_str
    movb %dl, %al
    call print_hex8
    movw $msg_err_lba, %si
    call print_str
    movl %ebp, %eax
    call print_hex16
    
    movw $msg_err_c, %si
    call print_str
    movb %ch, %al
    call print_hex8
    movw $msg_err_h, %si
    call print_str
    movb %dh, %al
    call print_hex8
    movw $msg_err_s, %si
    call print_str
    movb %cl, %al
    call print_hex8
    
    movw $msg_err_buf, %si
    call print_str
    movl %edi, %eax
    call print_hex16
    movw $msg_err_rem, %si
    call print_str
    movl %ecx, %eax
    call print_hex16
    popw %es
    
    movb $4, vga_row
    movb $0, vga_col
    movw $msg_press_reset, %si
    call print_str
    
    sti
1:
    movb $0x01, %ah      
    int $0x16
    jz 1b                 
    movb $0x00, %ah      
    int $0x16
    
    movb $0xFE, %al
    outb %al, $0x64
2:
    hlt
    jmp 2b

read_done:
    movw $1, %ax
read_ret:
    movb %al, read_retval
    popa
    movb read_retval, %al
    ret

read_retval:
    .byte 0


retry_count:
    .word 3





collect_e820:
    pusha
    
    xorw %ax, %ax
    movw %ax, %es
    xorl %ebx, %ebx
    movw $0, map_count
    movl $0, mem_upper
e820_loop:
    
    movl $0x00009000, %edi
    movl $0x534D4150, %edx   
    movl $24, %ecx
    movl $0xE820, %eax       
    int $0x15
    jc e820_done
    cmpl $0x534D4150, %eax
    jne e820_done

    
    movl $map_count, %eax
    movl $20, %ebp
    mull %ebp
    movl $0x00009400, %edi
    addl %eax, %edi
    movl $20, %ecx
    movw %cx, %es:(%edi)          
    
    movl $0x00009000, %esi
    movl %es:0(%esi), %eax
    movl %eax, %es:4(%edi)        
    movl %es:4(%esi), %eax
    movl %eax, %es:8(%edi)        
    movl %es:8(%esi), %eax
    movl %eax, %es:12(%edi)       
    movl %es:12(%esi), %eax
    movl %eax, %es:16(%edi)       
    movl %es:16(%esi), %eax
    movl %eax, %es:20(%edi)       

    
    cmpl $1, %es:16(%esi)
    jne e820_next
    cmpl $0x00100000, %es:0(%esi)
    jb e820_next
    movl %es:0(%esi), %eax
    addl %es:8(%esi), %eax
    shrl $10, %eax
    cmpl mem_upper, %eax
    jbe e820_next
    movl %eax, mem_upper
e820_next:
    incw map_count
    cmpl $32, map_count
    jae e820_done            
    cmpl $0, %ebx
    jne e820_loop
e820_done:
    

    cmpl $0, mem_upper
    jne e820_ret
    
    xorw %bx, %bx
    movw $0xE801, %ax
    int $0x15
    jc e801_fail
    movzwl %ax, %eax
    movzwl %bx, %edx
    shll $6, %edx
    addl %edx, %eax
    testl %eax, %eax
    jz e801_fail
    movl %eax, mem_upper
    jmp e820_ret
e801_fail:
    movb $0x88, %ah
    int $0x15
    jc e820_ret
    movzwl %ax, %eax
    testl %eax, %eax
    jz e820_ret
    movl %eax, mem_upper
e820_ret:
    popa
    ret

map_count:
    .word 0
mem_upper:
    .long 0






find_acpi_rsdp:
    pusha
    push %ds
    push %es
    xorw %ax, %ax
    movw %ax, %ds
    movw %ax, %es
    
    movw 0x40E, %ax
    shlw $4, %ax
    movl %eax, %edi
    
    movl $1024, %ecx
    call rsdp_search_range
    cmpl $0, %eax
    jne far_found
    
    movl $0xE0000, %edi
    movl $0x20000, %ecx  
    call rsdp_search_range
far_found:
    movl %eax, %esi  
    pop %es
    pop %ds
    popa
    movl %esi, %eax  
    ret




rsdp_search_range:
    pushl %ecx
    pushl %edi
rsdp_loop:
    
    cmpl $0x20525352, %es:(%edi)    
    jne rsdp_next
    cmpl $0x52545020, %es:4(%edi)   
    jne rsdp_next
    
    pushl %edi
    pushl %ecx
    xorl %eax, %eax
    xorl %ecx, %ecx
    movb $20, %cl
rsdp_checksum:
    addb %es:(%edi), %al
    incl %edi
    loop rsdp_checksum
    popl %ecx
    popl %edi
    testb %al, %al
    jnz rsdp_next
    
    movl %edi, %eax
    popl %edi
    popl %ecx
    ret
rsdp_next:
    addl $16, %edi  
    subl $16, %ecx
    ja rsdp_loop
    xorl %eax, %eax  
    popl %edi
    popl %ecx
    ret

acpi_rsdp_addr:
    .long 0







VBE_INFO_BUFFER = 0x7E00

VBE_MODE_BUFFER = 0x8000


vbe_mode:
    .word 0          
vbe_framebuffer:
    .long 0          
vbe_pitch:
    .word 0          
vbe_width:
    .word 0          
vbe_height:
    .word 0          
vbe_bpp:
    .byte 0          





detect_vbe:
    pusha
    push %es
    push %di
    xorw %ax, %ax
    movw %ax, %es
    movw $VBE_INFO_BUFFER, %di
    
    movl $0x32454256, %es:(%di)  
    movw $0x4F00, %ax
    int $0x10
    pop %di
    pop %es
    popa
    
    cmpw $0x004F, %ax
    je dvbe_supported
    stc
    ret
dvbe_supported:
    clc
    ret






set_vbe_mode:
    pusha
    
    
    movw $0x118, %bx
    call try_vbe_mode
    jnc svm_done
    movw $0x115, %bx
    call try_vbe_mode
    jnc svm_done
    movw $0x112, %bx
    call try_vbe_mode
    jnc svm_done
    
    stc
    jmp svm_ret
svm_done:
    clc
svm_ret:
    popa
    ret






try_vbe_mode:
    pusha
    push %es
    push %di
    
    xorw %ax, %ax
    movw %ax, %es
    movw $VBE_MODE_BUFFER, %di
    movw $0x4F01, %ax
    
    movw %bx, %cx
    int $0x10
    cmpw $0x004F, %ax
    jne tvm_fail
    
    movw %es:0(%di), %ax
    testw $0x009B, %ax  
    jz tvm_fail
    
    movb %es:25(%di), %al
    cmpb $15, %al
    jb tvm_fail
    
    movw %bx, vbe_mode
    movl %es:40(%di), %eax  
    movl %eax, vbe_framebuffer
    movw %es:16(%di), %ax   
    movw %ax, vbe_pitch
    movw %es:18(%di), %ax   
    movw %ax, vbe_width
    movw %es:20(%di), %ax   
    movw %ax, vbe_height
    movb %es:25(%di), %al   
    movb %al, vbe_bpp
    
    movw %bx, %bx
    orw $0x4000, %bx  
    movw $0x4F02, %ax
    int $0x10
    cmpw $0x004F, %ax
    jne tvm_fail
    pop %di
    pop %es
    popa
    clc
    ret
tvm_fail:
    pop %di
    pop %es
    popa
    stc
    ret





build_mbinfo:
    pusha
    
    xorw %ax, %ax
    movw %ax, %es
    movl $0x00009700, %edi
    
    movl $0x1845, %es:0(%edi)        
    movl $640, %es:4(%edi)         
    movl mem_upper, %eax
    movl %eax, %es:8(%edi)         
    movl $0, %es:12(%edi)          
    
    cmpb $1, boot_mode
    je bmi_safe
    cmpb $2, boot_mode
    je bmi_recovery
    movl $cmdline_normal, %eax
    jmp bmi_cmdline_done
bmi_safe:
    movl $cmdline_safe, %eax
    jmp bmi_cmdline_done
bmi_recovery:
    movl $cmdline_recovery, %eax
bmi_cmdline_done:
    movl %eax, %es:16(%edi)        
    movl $0, %es:20(%edi)          
    movl $0, %es:24(%edi)          
    movl $0, %es:28(%edi)          
    movl $0, %es:32(%edi)
    movl $0, %es:36(%edi)
    movl $0, %es:40(%edi)
    movw map_count, %ax
    movw %ax, %cx
    movl $20, %ebp
    mull %ebp
    movl %eax, %es:44(%edi)        
    movl $0x00009400, %eax
    movl %eax, %es:48(%edi)        
    
    movl $0, %es:52(%edi)          
    movl $0, %es:56(%edi)          
    
    movl acpi_rsdp_addr, %eax
    movl %eax, %es:60(%edi)        
    movl $0, %es:64(%edi)          
    movl $0, %es:68(%edi)          
    
    movl $VBE_INFO_BUFFER, %eax
    movl %eax, %es:72(%edi)        
    movl $VBE_MODE_BUFFER, %eax
    movl %eax, %es:76(%edi)        
    movw vbe_mode, %ax
    movw %ax, %es:80(%edi)         
    movw $0, %es:82(%edi)          
    movw $0, %es:84(%edi)          
    movw $0, %es:86(%edi)          
    
    movl vbe_framebuffer, %eax
    movl %eax, %es:88(%edi)        
    movl $0, %es:92(%edi)          
    movw vbe_pitch, %ax
    movw %ax, %es:96(%edi)         
    movw vbe_width, %ax
    movw %ax, %es:98(%edi)         
    movw vbe_height, %ax
    movw %ax, %es:100(%edi)        
    movb vbe_bpp, %al
    movb %al, %es:102(%edi)        
    movb $1, %es:103(%edi)         
    popa
    ret




enter_protected:
    
    pushw %es
    movw $0xB800, %ax
    movw %ax, %es
    movw $0x700, %di
    movw $('E' | 0x0F00), %es:(%di)
    movw $('P' | 0x0F00), %es:2(%di)
    popw %es
    
    pusha
    xorw %ax, %ax
    movw %ax, %ds
    movw %ax, %es
    popa
    
    movl $0x00009800, %edi
    movl $0, %es:(%edi)
    movl $0, %es:4(%edi)
    movl $0x0000FFFF, %es:8(%edi)
    movl $0x00CF9A00, %es:12(%edi)
    movl $0x0000FFFF, %es:16(%edi)
    movl $0x00CF9200, %es:20(%edi)
    
    movl $0x0000FFFF, %es:24(%edi)
    movl $0x00009A00, %es:28(%edi)
    
    movl $0x00009908, %esi
    movw $31, %ax
    movw %ax, %ds:(%esi)
    movl $0x00009800, %eax
    movl %eax, %ds:2(%esi)
    lgdtw %ds:(%esi)
    cli
    

    .byte 0x66
    xor %eax, %eax
    .byte 0x66
    mov %cr0, %eax
    .byte 0x66
    or $1, %eax
    .byte 0x66
    mov %eax, %cr0
    jmp 1f
1:
    ljmp $0x0008, $protected_mode

    .code32
protected_mode:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss
    movl $0x00008F00, %esp
    
    movl $0xB8000+1280, %edi
    movw $('P' | 0x0F00), %es:(%edi)
    movw $('0' | 0x0F00), %es:2(%edi)
    
    movl $0x00010000, %esi
    movl $0x00100000, %edi
    movl $1000, %eax
    shll $(11-2), %eax
    movl %eax, %ecx
    cld
    rep movsl
    
    movl $0xB8000+1440, %edi
    movw $('M' | 0x0F00), %es:(%edi)
    movw $('1' | 0x0F00), %es:2(%edi)
    
    cli
    
    
    .byte 0xEA
    .long code16_switch
    .word 0x0018
    .code16
code16_switch:
    
    movl %cr0, %eax
    andl $0xFFFFFFFE, %eax
    movl %eax, %cr0
    

    .byte 0xEA
    .word rm_read_seg2 - 0x8000
    .word 0x0800
    .code16
rm_read_seg2:
    xorw %ax, %ax
    movw %ax, %ss              
    movw $0x7C00, %sp       
    

    xorl %eax, %eax
    movw %sp, %ax
    movl %eax, %esp           
    pushfw
    popw %ax
    pushw %ax
    popfw                      
    xorw %ax, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs              
    movw %ax, %gs
    
    movb $0, 0x4004
    
    movw $0x03FF, 0x9910
    movl $0x00000000, 0x9912
    lidt 0x9910
    
    #movb $0x00, %ah
    #movb boot_drive, %dl
    #orb $0x80, %dl
    #int $0x13
    cli
    
    movw $0xB800, %ax
    movw %ax, %es
    movw $0x820, %di
    movw $('R' | 0x0F00), %es:(%di)
    movw $('1' | 0x0F00), %es:2(%di)
    xorw %ax, %ax
    movw %ax, %es
    

    movl $((214) - (1000)), 0x4010          
    movl $((21) + (1000)), 0x4014          
    movl $(0x00100000 + ((1000) << 11)), 0x4018         
chunk_loop:
    movl 0x4010, %eax
    testl %eax, %eax
    jz chunk_done
    cmpl $336, %eax                
    jae 1f
    movl %eax, %ecx
    jmp 2f
1:
    movl $336, %ecx
2:
    movl %ecx, 0x401C          
    
    movb $0x00, %ah
    movb boot_drive, %dl
    orb $0x80, %dl
    sti
    int $0x13
    cli
    
    movw $0xB800, %ax
    movw %ax, %es
    movw $0x860, %di
    movw $('S' | 0x0F00), %es:(%di)
    movw $('2' | 0x0F00), %es:2(%di)
    xorw %ax, %ax
    movw %ax, %es
    
    movl $0x00010000, %edi
    movl 0x4014, %ebp
    call read_slice
    testb %al, %al
    jz halt_loop
    
    movb $13, 0x5000
    
    cli
    movl %cr0, %eax
    orl $1, %eax
    movl %eax, %cr0
    
    movb $14, 0x5000
    jmp 1f
1:
    
    .byte 0xEA
    .long pm_chunk_copy
    .word 0x0008

    .code32
pm_chunk_copy:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss
    movl $0x00008F00, %esp
    
    movb $8, 0x5000
    
    movl $0x00010000, %esi
    movl 0x4018, %edi
    movl %ecx, %eax
    shll $(11-2), %eax
    movl %eax, %ecx
    cld
    rep movsl
    
    movb $9, 0x5000
    
    cli
    .byte 0xEA
    .long code16_switch_chunk
    .word 0x0018
    .code16
code16_switch_chunk:
    
    movb $10, 0x5000
    movl %cr0, %eax
    andl $0xFFFFFFFE, %eax
    movl %eax, %cr0
    .byte 0xEA
    .word chunk_return - 0x8000
    .word 0x0800
    .code16
chunk_return:
    
    movb $11, 0x5000
    xorw %ax, %ax
    movw %ax, %ss              
    movw $0x7C00, %sp
    xorl %eax, %eax
    movw %sp, %ax
    movl %eax, %esp
    xorw %ax, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    
    movl 0x401C, %ecx
    movl 0x4010, %eax
    subl %ecx, %eax
    movl %eax, 0x4010
    movl 0x4014, %eax
    addl %ecx, %eax
    movl %eax, 0x4014
    movl %ecx, %eax
    shll $11, %eax
    addl 0x4018, %eax
    movl %eax, 0x4018
    
    movb $12, 0x5000
    jmp chunk_loop
chunk_done:
    
    cli
    movl %cr0, %eax
    orl $1, %eax
    movl %eax, %cr0
    jmp 1f
1:
    
    .byte 0xEA
    .long pm_final
    .word 0x0008

    .code32
pm_final:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss
    movl $0x00008F00, %esp
    
    movl $0xB8000+1920, %edi
    movw $('P' | 0x0F00), %es:(%edi)
    movw $('2' | 0x0F00), %es:2(%edi)
    
    movl $0x100000, %edi
    movl (%edi), %eax
    cmpl $0x1BADB002, %eax
    jne halt_loop
    
    movl $0x2BADB002, %eax
    movl $0x00009700, %ebx
    
    .byte 0xEA
    .long KERNEL_ENTRY
    .word 0x0008



# 1726 "hepboot.S"
enable_a20_pm:
    pusha
    
    inb $0x92, %al
    orb $0x02, %al
    outb %al, $0x92
    
    movb $0x01, %al
    outb %al, $0xEE
    popa
    ret


    
.section .rodata
msg_int13_ok:
    .asciz "[INT13] LBA ext OK\n"
msg_int13_no:
    .asciz "[INT13] LBA ext NOT supported!\n"
msg_boot:
    .asciz "H2"
msg_gdt:
    .asciz "G"
msg_a20:
    .asciz "A"
msg_reading:
    .asciz "R"
msg_kernel_lba:
    .asciz " LBA="
msg_readok:
    .asciz "K"
msg_move:
    .asciz "M"
msg_prot:
    .asciz "P"

msg_err_code:
    .asciz "ERR "
msg_err_ah:
    .asciz "E AH="
msg_err_dl:
    .asciz " D="
msg_err_lba:
    .asciz " LBA="
msg_err_c:
    .asciz "\nC="
msg_err_h:
    .asciz " H="
msg_err_s:
    .asciz " S="
msg_err_buf:
    .asciz "\nBUF="
msg_err_rem:
    .asciz " REM="
# 1806 "hepboot.S"

menu_title:
    .asciz "=== HepuBoot 引导菜单 ==="
menu_opt1:
    .asciz " 1. 正常模式 (默认)"
menu_opt2:
    .asciz " 2. 安全模式 (VGA 640x480)"
menu_opt3:
    .asciz " 3. 恢复模式 (文本 80x25)"
menu_prompt:
    .asciz "按 1/2/3 选择，Enter 默认，ESC 取消: "
menu_safe_msg:
    .asciz "[安全模式] "
menu_recovery_msg:
    .asciz "[恢复模式] "

cmdline_normal:
    .asciz ""
cmdline_safe:
    .asciz "hepu.safe=1"
cmdline_recovery:
    .asciz "hepu.recovery=1"

    .section .bss
boot_drive:
    .byte 0
vga_row:
    .byte 0
vga_col:
    .byte 0


dap_packet:
    .byte 0                 
    .byte 0                 
dap_sectors:
    .word 0                 
dap_offset:
    .word 0                 
dap_segment:
    .word 0                 
dap_lba:
    .quad 0                 
saved_ret_ip:
    .word 0                 
saved_sp:
    .word 0                 

orig_int13_ip:
    .word 0
orig_int13_cs:
    .word 0
wrapper_saved_sp:
    .word 0


int13_ret_ip:
    .word 0
int13_saved_sp:
    .word 0
in_int13:
    .byte 0

boot_mode:
    .byte 0                 


.section .text



check_f8_and_menu:
    pusha
    push %ds
    push %es
    xorw %ax, %ax
    movw %ax, %ds
    movw %ax, %es
    
    movb $0x01, %ah
    int $0x16
    jz cf8_done
    
    movb $0x00, %ah
    int $0x16
    
    cmpb $0x42, %ah
    jne cf8_done
    
    call show_boot_menu
cf8_done:
    pop %es
    pop %ds
    popa
    ret




show_boot_menu:
    pusha
    
    movw $0xB800, %ax
    movw %ax, %es
    xorw %di, %di
    movw $2000, %cx
    movw $0x0720, %ax
    rep stosw
    movb $0, vga_row
    movb $0, vga_col
    
    movw $menu_title, %si
    call print_str
    movb $'\n', %al
    call print_char
    movb $'\n', %al
    call print_char
    
    movw $menu_opt1, %si
    call print_str
    movb $'\n', %al
    call print_char
    
    movw $menu_opt2, %si
    call print_str
    movb $'\n', %al
    call print_char
    
    movw $menu_opt3, %si
    call print_str
    movb $'\n', %al
    call print_char
    movb $'\n', %al
    call print_char
    
    movw $menu_prompt, %si
    call print_str
    
    movb $0x00, %ah
    int $0x16
    
    cmpb $'1', %al
    je sbm_normal
    cmpb $'2', %al
    je sbm_safe
    cmpb $'3', %al
    je sbm_recovery
    cmpb $0x1B, %al
    je sbm_normal
sbm_normal:
    movb $0, boot_mode
    jmp sbm_done
sbm_safe:
    movb $1, boot_mode
    jmp sbm_done
sbm_recovery:
    movb $2, boot_mode
sbm_done:
    
    movw $0xB800, %ax
    movw %ax, %es
    xorw %di, %di
    movw $2000, %cx
    movw $0x0720, %ax
    rep stosw
    movb $0, vga_row
    movb $0, vga_col
    
    cmpb $1, boot_mode
    jne sbm_check_rec
    movw $menu_safe_msg, %si
    call print_str
    jmp sbm_finish
sbm_check_rec:
    cmpb $2, boot_mode
    jne sbm_finish
    movw $menu_recovery_msg, %si
    call print_str
sbm_finish:
    popa
    ret




print_char:
    pusha
    push %es
    movw $0xB800, %ax
    movw %ax, %es
    cmpb $'\n', %al
    je pc_newline
    
    movb %al, %cl
    movzbl vga_col, %edx
    movzbl vga_row, %eax
    imull $80, %eax, %eax
    addl %edx, %eax
    shll $1, %eax
    movl %eax, %edi
    movb $0x07, %ch
    movw %cx, %es:(%edi)
    incb vga_col
    cmpb $80, vga_col
    jb pc_done
    movb $0, vga_col
    incb vga_row
    jmp pc_done
pc_newline:
    movb $0, vga_col
    incb vga_row
pc_done:
    pop %es
    popa
    ret


# 2031 "hepboot.S"
check_boot_fail_count:
    pusha
    push %ds
    push %es
    xorw %ax, %ax
    movw %ax, %ds
    movw %ax, %es
    
    movw 0x4020, %ax
    cmpw $0x4850, %ax  
    je cbfc_have_magic
    
    movw $0x4850, 0x4020
    movb $0, 0x4022
    jmp cbfc_done
cbfc_have_magic:
    
    movb 0x4022, %al
    cmpb $3, %al
    jb cbfc_increment
    
    movb $2, boot_mode
    
    movb $0, 0x4022
    
    movw $0xB800, %ax
    movw %ax, %es
    movw $160*3+2*0, %di
    movw $0x4F20, %ax  
    movw $80, %cx
    rep stosw
    movw $160*3+2*5, %di
    movw $0x4F5B, %ax  
    stosw
    movw $0x4F53, %ax  
    stosw
    movw $0x4F41, %ax  
    stosw
    movw $0x4F46, %ax  
    stosw
    movw $0x4F45, %ax  
    stosw
    movw $0x4F5D, %ax  
    stosw
    movw $0x4F20, %ax
    stosw
    movw $0x4F41, %ax  
    stosw
    movw $0x4F55, %ax  
    stosw
    movw $0x4F54, %ax  
    stosw
    movw $0x4F4F, %ax  
    stosw
    movw $0x4F2D, %ax  
    stosw
    movw $0x4F52, %ax  
    stosw
    movw $0x4F45, %ax  
    stosw
    movw $0x4F43, %ax  
    stosw
    movw $0x4F4F, %ax  
    stosw
    movw $0x4F56, %ax  
    stosw
    movw $0x4F45, %ax  
    stosw
    movw $0x4F52, %ax  
    stosw
    movw $0x4F59, %ax  
    stosw
    jmp cbfc_done
cbfc_increment:
    
    incb %al
    movb %al, 0x4022
cbfc_done:
    pop %es
    pop %ds
    popa
    ret






read_single_sector:
    pusha
    pushl %edi
    pushl %ebp
    push %ds
    push %es
    
    xorw %ax, %ax
    movw %ax, %ds
    movb $0x10, dap_packet
    movw $1, dap_sectors
    
    movl %edi, %eax
    movl %eax, %edx
    andl $0xF, %edx
    movw %dx, dap_offset
    shrl $4, %eax
    movw %ax, dap_segment
    
    movl %ebp, dap_lba
    movl $0, dap_lba+4
    
    movw $dap_packet, %si
    movb boot_drive, %dl
    orb $0x80, %dl
    movb $0x42, %ah
    int $0x13
    pop %es
    pop %ds
    popl %ebp
    popl %edi
    popa
    ret






write_single_sector:
    pusha
    pushl %edi
    pushl %ebp
    push %ds
    push %es
    
    xorw %ax, %ax
    movw %ax, %ds
    movb $0x10, dap_packet
    movw $1, dap_sectors
    
    movl %edi, %eax
    movl %eax, %edx
    andl $0xF, %edx
    movw %dx, dap_offset
    shrl $4, %eax
    movw %ax, dap_segment
    
    movl %ebp, dap_lba
    movl $0, dap_lba+4
    
    movw $dap_packet, %si
    movb boot_drive, %dl
    orb $0x80, %dl
    movb $0x43, %ah
    int $0x13
    pop %es
    pop %ds
    popl %ebp
    popl %edi
    popa
    ret
