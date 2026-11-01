#ifndef IDT_H
#define IDT_H

#include <stdint.h>

void idt_init(void);
void idt_handler(uint32_t isr_number, uint32_t error_code,
                 uint32_t ebx, uint32_t ecx, uint32_t edx, uint32_t esi);

#endif
