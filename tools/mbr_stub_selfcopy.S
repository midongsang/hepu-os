// 注意: 此文件为参考源码(实验版), 实际构建使用 tools/mbr_legacy.bin
// (已验证重入式MBR, 见 build_combo.sh).
/* ============================================================================
// 注意: 此文件为参考源码(实验版), 实际构建使用 tools/mbr_legacy.bin
// (已验证重入式MBR, 见 build_combo.sh).
 * 赫普OS - MBR引导桩 (512字节, GAS / AT&T)
// 注意: 此文件为参考源码(实验版), 实际构建使用 tools/mbr_legacy.bin
// (已验证重入式MBR, 见 build_combo.sh).
 * 功能：用INT13 AH=42 把stage2(引导器主体)从LBA 1-3读到 0x7C00，
 *       然后跳转到 0x7C00 继续执行。
 * stage2位于内核bin之前(LBA 1-3)，紧跟MBR。
 *
 * V3.80 关键修复（自复制到0x0600执行）:
 *   原实现直接在0x7C00执行读stage2——INT13把stage2写入0x7C00会覆盖
 *   正在执行的MBR代码, INT13返回后CPU从"被覆盖位置"继续执行:
 *     - DEBUG版MBR代码较长, 返回点恰落在stage2无害指令上→侥幸能跑;
 *     - 正式版(非DEBUG)MBR代码较短, 返回点落在stage2垃圾/有害指令上
 *       → 卡死(屏幕停在 SeaBIOS "Booting from Hard Disk...")。
 *   这正是真机刷正式版镜像黑屏回BIOS的根因之一。
 *   修复: MBR先把自己512字节复制到0x0600-0x07FF, 跳过去继续执行,
 *   读stage2覆盖0x7C00区不影响0x0600的执行体, INT13返回后正常ljmp。
 * ========================================================================== */

    .code16
    .section .text
    .globl _start
_start:
    cli
    cld
    xorw %ax, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw $0x7C00, %sp
    movb %dl, 0x0500          /* V3.80: 存到0x0500(BIOS数据区之后, 不被stage2覆盖) */
#ifdef DEBUG_BOOT
    /* 诊断模式: VGA直写打印 "H1" 表示MBR已执行
       V3.80: 打印前后保存/恢复ES——否则 ES=0xB800 会让后续
       rep movsw 自复制把512B写进显存而非0x8000(卡死根因) */
    pushw %es
    movw $0xB800, %ax
    movw %ax, %es
    xorw %di, %di
    movw $0x0731, %ax
    stosw
    movw $0x0748, %ax
    stosw
    popw %es
#endif

    /* ---- V3.80: 自复制 512B 到 0x8000 后继续执行 ----
     * 目标选 0x8000: 高于 stage2 覆盖区(0x7C00-0x7FFF), 避开 BIOS
     * 低内存工作区(0x0400-0x07FF), 不与其他引导组件冲突 */
    movw $0x7C00, %si
    movw $0x8000, %di
    movw $256, %cx            /* 512B = 256 words */
    rep movsw
    /* 跳到复制体: 0x8000 + (reloc_cont - _start)
     * lret 弹出顺序: 先 IP 后 CS —— 故先压 CS(0) 再压 IP(bx) */
    movw $reloc_cont, %bx
    subw $_start, %bx
    addw $0x8000, %bx
    pushw $0x0000
    pushw %bx
    lret

reloc_cont:
    /* 以下代码在 0x8000+ 执行 (0x7C00区可安全被stage2覆盖) */

    /* ---- 读stage2: LBA 1起到 0x7C00 ---- */
    /* DAP构造 (LBA/块数可由-D覆盖) */
#ifndef STAGE2_LBA
#define STAGE2_LBA 1
#endif
#ifndef STAGE2_BLOCKS
#define STAGE2_BLOCKS 3
#endif
    movw $0x7B00, %si
    movb $0x10, %ds:(%si)
    movb $0x00, %ds:1(%si)
    movw $STAGE2_BLOCKS, %ds:2(%si)   /* 块数(512B) */
    movw $0x7C00, %ds:4(%si)          /* 偏移 */
    movw $0x0000, %ds:6(%si)          /* 段 */
    movl $STAGE2_LBA, %ds:8(%si)      /* LBA */
    movl $0, %ds:12(%si)

    /* ES:DI = 0x7C00 (DS=ES=0) */
    movw $0x7C00, %di
    xorw %ax, %ax
    movw %ax, %es
    movw $0x7B00, %si
    movb 0x0500, %dl
    movb $0x42, %ah
    int $0x13
    jc read_err

    /* 跳转stage2 */
    ljmp $0x0000, $0x7C00

read_err:
    /* VGA直写打印 "E!" 表示MBR读stage2失败 */
    movw $0xB800, %ax
    movw %ax, %es
    movw $160, %di
    movw $0x0721, %ax
    stosw
    movw $0x0745, %ax
    stosw
    /* 打印错误并停机 */
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
    cli
    hlt
    jmp 2b

err_msg:
    .asciz "MBR READ ERR"

    .org 510
    .word 0xAA55
