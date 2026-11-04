#ifndef TSS_H
#define TSS_H

#include <stdint.h>

/* x86 TSS (Task State Segment) 结构
 * 用于用户态->内核态切换时自动加载内核栈 */
typedef struct {
    uint32_t prev_tss;      /* 前一个TSS选择子 */
    uint32_t esp0;          /* 内核栈指针 (ring 0) */
    uint32_t ss0;           /* 内核栈段选择子 */
    uint32_t esp1;          /* ring 1 栈 */
    uint32_t ss1;
    uint32_t esp2;          /* ring 2 栈 */
    uint32_t ss2;
    uint32_t cr3;           /* 页目录 */
    uint32_t eip;           /* 指令指针 */
    uint32_t eflags;        /* 标志寄存器 */
    uint32_t eax, ecx, edx, ebx;
    uint32_t esp, ebp, esi, edi;
    uint32_t es, cs, ss, ds, fs, gs;
    uint32_t ldt;           /* 局部描述符表 */
    uint16_t trap;          /* 调试陷阱标志 */
    uint16_t iomap_base;    /* I/O映射表基址 */
} __attribute__((packed)) tss_t;

/* 初始化TSS */
void tss_init(void);

/* 设置当前进程的内核栈 */
void tss_set_kernel_stack(uint32_t esp0);

/* 获取TSS指针 */
tss_t *tss_get(void);

#endif /* TSS_H */
extern uint32_t tss_get_esp0(void);
