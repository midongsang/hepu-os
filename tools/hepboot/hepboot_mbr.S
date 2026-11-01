/* ============================================================================
 * 赫普OS - MBR引导桩 (512字节, GAS / AT&T) —— 稳定版(重入式)
 *
 * 注意: 构建以本文件编译产物为准(build_hepu_images.sh / build_combo.sh),
 *       并用 tools/mbr_legacy.bin 做字节级回归校验.
 *       tools/mbr_stub_selfcopy.S = 自复制实验版(0x8000残留干扰内核早期,
 *       已知问题, 不用于交付镜像).
 *
 * 工作机制(重入式):
 *   1. MBR 在 0x7C00 执行, 打印 "H1"(引导日志, 无条件)
 *   2. INT13 AH=42 读 stage2(LBA34-39) 到 0x8000
 *   3. INT13 返回后 CPU 从 0x7C00+重入点 继续执行(重入点是 stage2 代码)
 *   4. 该重入模式已实测稳定: stage2 的 V2.09 重入入口修复(检测 0x20000
 *      内核头魔数, 缺失则补调 read_kernel) 保证无论从开头还是重入点
 *      进入都能完成内核加载.
 *
 * 历史(V3.80 引导修复排查记录):
 *   - 根因: 正式版(无H1)MBR 重入点与 DEBUG 版不同(代码长度差12B),
 *     正式版落点碰巧在 stage2 有害指令→黑屏回BIOS; DEBUG 版落点无害→可跑.
 *   - 因此正式镜像统一用本稳定版(带H1日志), 重入点固定且已验证.
 *   - 自复制方案(彻底避免重入)实验失败: 复制体残留在 0x8000-0x81FF,
 *     内核早期使用该区(未初始化预期)时读到垃圾→启动崩溃.
 * ========================================================================== */

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
    movb %dl, boot_drive        /* 保存引导驱动器号 */

    /* 引导日志 "H1" (VGA直写, 无条件——长度决定重入点, 不可删!) */
    movw $0xB800, %ax
    movw %ax, %es
    xorw %di, %di
    movw $0x0731, %ax
    stosw
    movw $0x0748, %ax
    stosw

    /* ---- 读stage2: LBA 34起, 32块(16384B), 读到 0x8000 ---- */
    movw $0x7B00, %si
    movb $0x10, %ds:(%si)
    movb $0x00, %ds:1(%si)
    movw $32, %ds:2(%si)       /* 块数=32(16384B=完整stage2) */
    movw $0x8000, %ds:4(%si)    /* 偏移 */
    movw $0x0000, %ds:6(%si)    /* 段 */
    movl $34, %ds:8(%si)        /* LBA=34 */
    movl $0, %ds:12(%si)

    xorw %ax, %ax
    movw %ax, %es
    movw $0x7B00, %si
    movb boot_drive, %dl
    movb $0x42, %ah
    int $0x13
    jc read_err
    ljmp $0x0000, $0x8000       /* 跳 stage2 @0x8000 */

read_err:
    /* VGA直写 "E!" + 滚动打印错误消息, 然后停机 */
    movw $0xB800, %ax
    movw %ax, %es
    movw $160, %di
    movw $0x0721, %ax
    stosw
    movw $0x0745, %ax
    stosw
    movw $err_msg, %si
1:
    lodsb
    testb %al, %al
    jz 2f
    movb $0x0E, %ah
    movw $0x0007, %bx
    int $0x10
    jmp 1b
2:
    /* 开中断+轮询键盘: 按任意键重启 */
    sti
    movb $0x01, %ah
    int $0x16
    jz 2b
    movb $0x00, %ah
    int $0x16
    movb $0xFE, %al
    outb %al, $0x64
3:
    hlt
    jmp 3b

err_msg:
    .asciz "MBR READ ERR"

boot_drive:
    .byte 0

    .org 510
    .word 0xAA55
