/* ============================================================================
 * 赫普OS - 主线 Shell 入口重锚 (V4.02-E47 根治偶发 ISR 6/13)
 *
 * 背景: 主线 Shell 原在"触发首次 exit 的进程(init/procb)内核栈"上启动,
 * 该栈位于 kheap 堆内——procb/init 退出后块虽保留, 但 kheap 空闲区域仍
 * 与主线栈地址空间重叠, 后续进程(winargs/macho_demo/schtest) kmalloc
 * 分到主线栈区域 → 主线长期在"被复用/被释放"的栈上运行, 偶发 iret 弹
 * 垃圾帧(0x6B2xxxx) → ISR 6/13.
 *
 * 修复: 进入 shell_main 前把 esp 硬切到链接器保留栈 __stack_top(内核
 * 映像 .stack 段, 在 kheap 管理区之外, 永不被 kmalloc 复用). setjmp/
 * longjmp 随后保存/恢复的 esp 全部锚定在 __stack_top 区域, 与堆彻底隔离.
 *
 * 注意: 本入口只用 call(不返回), shell_main 为常驻主循环; 若意外返回
 * 则 hlt 兜底, 避免继续用新栈执行 C 调用者代码.
 * ========================================================================== */

.section .text
.extern __stack_top

.global hepu_enter_mainline
hepu_enter_mainline:
    movl 4(%esp), %eax        /* arg0 = shell_main 函数指针 */
    movl $__stack_top, %esp   /* 切到链接器保留栈(最高地址) */
    subl $4096, %esp          /* 锚定到距栈顶 4KB 处: 中断帧压栈于
                               * ESP0=__stack_top 顶部, 主线 esp 与中断帧
                               * 保持 4KB 隔离, 防弹栈覆盖中断帧 → 偶发
                               * ISR 6/13(iret 弹垃圾 EIP). 向下仍有 12KB. */
    call *%eax                /* 进入主线 Shell 主循环(不返回) */
1:  hlt                       /* 兜底: 意外返回则停机 */
    jmp 1b
