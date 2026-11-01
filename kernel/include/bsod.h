#ifndef BSOD_H
#define BSOD_H

#include <stdint.h>

/*
 * 赫普OS - 蓝屏死机 (Blue Screen of Death) 界面
 *
 * 功能：内核崩溃或桌面崩溃时显示友好的错误界面
 * 显示逻辑：
 *   1. 优先尝试图形模式（320x200 256色），蓝色背景+白色文字
 *   2. 若图形模式初始化失败，降级到VGA文本模式（蓝色背景+白色文字）
 *   3. 显示崩溃原因、错误代码、寄存器信息
 *   4. 同时输出到串口，便于日志记录
 *
 * 调用时机：
 *   - 内核态异常（idt_handler 中 isr<32 且非用户态）
 *   - 桌面/窗口管理器不可恢复崩溃
 */

/* 显示蓝屏死机界面
 * 参数：
 *   reason      - 崩溃原因描述（如 "Page Fault" / "Kernel Exception"）
 *   isr_number  - 异常号（0-31）
 *   error_code  - 错误码（部分异常有，无则为0）
 *   eip         - 崩溃时指令指针
 *   cs          - 代码段寄存器
 *   esp         - 栈指针
 * 返回：不返回（最终HLT）
 */
void bsod_show(const char *reason, uint32_t isr_number,
               uint32_t error_code, uint32_t eip,
               uint32_t cs, uint32_t esp);

/* 文本模式降级显示（图形模式失败时自动调用，也可手动调用） */
void bsod_text_mode(const char *reason, uint32_t isr_number,
                     uint32_t error_code, uint32_t eip,
                     uint32_t cs, uint32_t esp);

#endif /* BSOD_H */
