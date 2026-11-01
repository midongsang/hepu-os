; ============================================================================
; 赫普OS - 极简引导扇区 (MBR)
; 功能: 从磁盘读取内核到 0x100000，进入保护模式，跳转执行
; 大小: 512字节 (一个扇区)，以 0xAA55 结束
; ============================================================================

[BITS 16]
[ORG 0x7C00]

    ; 初始化段寄存器
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00

    ; 保存启动驱动器号
    mov [boot_drive], dl

    ; 打印启动信息
    mov si, msg_loading
    call print_string

    ; ---- 读取内核到内存 ----
    ; 内核紧跟在引导扇区后面，从 LBA 1 开始
    ; 先读到 0x10000 (低内存)，然后移动到 0x100000
    mov ax, 0x1000
    mov es, ax
    xor bx, bx          ; ES:BX = 0x1000:0x0000 = 0x10000

    mov ah, 0x02        ; BIOS 读扇区
    mov al, 127         ; 读 127 个扇区 (约 64KB，足够内核)
    mov ch, 0           ; 柱面 0
    mov cl, 2           ; 扇区 2 (LBA 1)
    mov dh, 0           ; 磁头 0
    mov dl, [boot_drive]
    int 0x13
    jc disk_error

    ; ---- 进入保护模式 ----
    cli
    lgdt [gdt_descriptor]

    mov eax, cr0
    or eax, 1
    mov cr0, eax

    ; 远跳转刷新 CS
    jmp 0x08:protected_mode

[BITS 32]
protected_mode:
    ; 设置数据段
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x90000

    ; 把内核从 0x10000 移动到 0x100000
    mov esi, 0x10000
    mov edi, 0x100000
    mov ecx, 65536 / 4   ; 复制 64KB (16384 个双字)
    rep movsd

    ; 设置 Multiboot 信息 (简化版)
    mov eax, 0x2BADB002   ; Multiboot 启动魔数
    xor ebx, ebx          ; multiboot_info 指针 (NULL，原型阶段)

    ; 跳转到内核入口
    call 0x100000

    ; 内核返回后停机
    cli
.hang:
    hlt
    jmp .hang

[BITS 16]
; ---- 子函数 ----
print_string:
    lodsb
    or al, al
    jz .done
    mov ah, 0x0E
    int 0x10
    jmp print_string
.done:
    ret

disk_error:
    mov si, msg_disk_error
    call print_string
    cli
    hlt

; ---- 数据 ----
boot_drive:     db 0
msg_loading:    db 'HEPU OS Boot... ', 0
msg_disk_error: db 'DISK ERROR!', 0

; ---- GDT ----
gdt_start:
    dq 0x0000000000000000  ; 空描述符
gdt_code:
    dw 0xFFFF              ; limit
    dw 0x0000              ; base low
    db 0x00                ; base middle
    db 0x9A                ; access: code, present, ring0
    db 0xCF                ; granularity: 4KB, 32-bit
    db 0x00                ; base high
gdt_data:
    dw 0xFFFF
    dw 0x0000
    db 0x00
    db 0x92                ; access: data, present, ring0
    db 0xCF
    db 0x00
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

; ---- 填充到 510 字节，加启动签名 ----
times 510 - ($ - $$) db 0
dw 0xAA55
