/* ============================================================================
 * 赫普OS 64位 - SMAP 感知的用户页访问助手
 *
 * 背景（本文件修复的真实缺陷）：
 *   早期实现直接在 copy_from_user / copy_to_user / copy_str_from_user 里内联
 *   执行 `stac` / `clac`。但 SMAP（Supervisor Mode Access Prevention）在 x86-64
 *   规范里是**可选**特性：
 *     - CPUID.(EAX=7,ECX=0):EBX[20] == 0 时，CR4.SMAP 无法置位；
 *     - 此时执行 STAC 会触发 #UD（无效操作码）。
 *   后果：在不支持 SMAP 的 CPU 上（QEMU 默认 qemu64 就不暴露 SMAP/SMAP，
 *   相当一部分真机 CPU 也一样），内核一进入第一个用户程序就 #UD 死机，
 *   系统根本起不来。主机端安全回归 uaccess_test 也因同一条指令直接 SIGILL。
 *
 * 本文件的契约：
 *   1. 「能不能访问用户页」由**硬件真实状态 CR4.SMAP** 判定，而不是一个可能
 *      与硬件失同步的软件开关 —— 真正的开关是硬件。
 *   2. SMAP 未开启时，助手退化为「不执行 STAC/CLAC」的普通访问，语义仍然正确
 *      （此时 ring0 本来就能访问用户页）。
 *   3. 嵌套安全：进入时保存 CR4.AC 原值，退出时原样归还，不会误清外层的 AC。
 *
 * 与 PHILOSOPHY.md「确定性优先于丰富性」一致：行为只取决于硬件真实状态，
 * 不存在「有时候这样、有时候那样」的隐含语义。
 * ========================================================================== */
#ifndef HEPU_SMAP_H
#define HEPU_SMAP_H

#include <stdint.h>

/* CR4 位定义 */
#define HEPU_CR4_AC   (1ULL << 18)   /* Alignment Control */
#define HEPU_CR4_SMAP (1ULL << 21)   /* Supervisor Mode Access Prevention */
#define HEPU_CR4_SMEP (1ULL << 20)   /* Supervisor Mode Execution Prevention */

/* 返回值：进入用户页访问区之前的 CR4.AC 状态 */
#define HEPU_AC_PREV_CLEAR  0   /* 原为 0：助手置了 AC，退出时需要 CLAC */
#define HEPU_AC_PREV_SET    1   /* 原为 1：助手没有动 AC，退出时不要 CLAC */
#define HEPU_AC_SMAP_OFF  (-1)  /* SMAP 未启用：助手什么都没做 */

#ifdef HEPU_UACCESS_HOST_TEST
/* 主机端测试跑在用户态：CR4.SMAP 必然为 0，且 MOV %cr4 在 ring3 会 #GP。
 * 主机端语义就是「ring0 可直接访问用户页」，与 SMAP 关闭时一致。 */
static inline int hepu_smap_active(void) { return 0; }
#else
static inline int hepu_smap_active(void)
{
    uint64_t cr4;
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    return (cr4 & HEPU_CR4_SMAP) ? 1 : 0;
}
#endif

/* 进入用户页访问区（允许 ring0 访问 ring3 页）。
 * 返回进入前的 AC 状态（HEPU_AC_PREV_*），退出时必须原样传给 hepu_user_access_exit。 */
static inline int hepu_user_access_enter(void)
{
    if (!hepu_smap_active()) {
        return HEPU_AC_SMAP_OFF;
    }
    uint64_t cr4;
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    int ac_prev = (cr4 & HEPU_CR4_AC) ? HEPU_AC_PREV_SET : HEPU_AC_PREV_CLEAR;
    if (ac_prev == HEPU_AC_PREV_CLEAR) {
        __asm__ volatile("stac" ::: "memory");
    }
    return ac_prev;
}

/* 退出用户页访问区，恢复进入前的 AC 状态。 */
static inline void hepu_user_access_exit(int ac_prev)
{
    if (ac_prev == HEPU_AC_PREV_CLEAR) {
        __asm__ volatile("clac" ::: "memory");
    }
    /* ac_prev == HEPU_AC_PREV_SET：外层已经开着 AC，保持不动
     * ac_prev == HEPU_AC_SMAP_OFF：SMAP 没开，从未置过 AC，不动
     */
}

/* ---------------------------------------------------------------------------
 * SMEP（监督模式执行保护）的对称处理
 *
 * 问题：SMEP 开启时，**内核态不能取指用户页**。
 *   IRELATIVE 重定位的 resolver 是**用户代码**（ifunc 解析器），
 *   内核必须在用户页表下调用它 ⇒ SMEP=on 时必然 #PF。
 *   证据（--cpu max，SMEP=on/SMAP=on）：
 *     [EXCEPTION] vector=0xE error=0x11 rip=0x50030830
 *   error 码解码：bit0=1（页存在但保护违规）、bit4=1（**取指**违规）、
 *   bit2=0（**内核态**发起）⇒ 正是 SMEP 拦截。
 *
 * 处理：与 SMAP 完全对称 —— 「能不能在用户页上取指」由**硬件真实状态
 * CR4.SMEP** 判定，进入时清掉 SMEP，退出时原样恢复。
 * 嵌套安全：保存进入前的 CR4，退出时按原值归还。
 *
 * 注意：SMEP 与 SMAP 是**不同**的机制，不能互相替代 ——
 *   SMAP 管数据访问（AC 标志），SMEP 管取指（CR4 位）。
 *   只做 STAC 不解除取指限制，所以两个都要管。
 * ------------------------------------------------------------------------- */
#ifdef HEPU_UACCESS_HOST_TEST
/* 主机端测试跑在用户态：ring3 无法读 CR4（会 #GP），SMEP 必然为 0。 */
static inline int hepu_smep_active(void) { return 0; }
#else
static inline int hepu_smep_active(void)
{
    uint64_t cr4;
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    return (cr4 & HEPU_CR4_SMEP) ? 1 : 0;
}
#endif

/* 进入「可在用户页取指」的状态。
 * 返回进入前的 CR4（退出时原样归还）。 */
static inline uint64_t hepu_user_exec_enter(void)
{
#ifdef HEPU_UACCESS_HOST_TEST
    return 0;
#else
    uint64_t cr4;
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    if (cr4 & HEPU_CR4_SMEP) {
        cr4 &= ~HEPU_CR4_SMEP;
        __asm__ volatile("mov %0, %%cr4" : : "r"(cr4) : "memory");
    }
    return cr4;   /* 返回的是**进入前**的 CR4 */
#endif
}

/* 退出：恢复进入前的 CR4（SMEP 位按原值归还）。 */
static inline void hepu_user_exec_exit(uint64_t saved_cr4)
{
#ifdef HEPU_UACCESS_HOST_TEST
    (void)saved_cr4;
#else
    __asm__ volatile("mov %0, %%cr4" : : "r"(saved_cr4) : "memory");
#endif
}

#endif /* HEPU_SMAP_H */
