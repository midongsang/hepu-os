#ifndef HEPU_APIC_H
#define HEPU_APIC_H

#include <stdint.h>

/* Local APIC寄存器偏移 */
#define LAPIC_ID            0x020
#define LAPIC_VERSION       0x030
#define LAPIC_TPR           0x080
#define LAPIC_APR           0x090
#define LAPIC_PPR           0x0A0
#define LAPIC_EOI           0x0B0
#define LAPIC_RRD           0x0C0
#define LAPIC_LDR           0x0D0
#define LAPIC_DFR           0x0E0
#define LAPIC_SVR           0x0F0
#define LAPIC_ISR_BASE      0x100
#define LAPIC_TMR_BASE      0x180
#define LAPIC_IRR_BASE      0x200
#define LAPIC_ESR           0x280
#define LAPIC_ICR_LOW       0x300
#define LAPIC_ICR_HIGH      0x310
#define LAPIC_LVT_TIMER     0x320
#define LAPIC_LVT_THERMAL   0x330
#define LAPIC_LVT_PERF      0x340
#define LAPIC_LVT_LINT0     0x350
#define LAPIC_LVT_LINT1     0x360
#define LAPIC_LVT_ERROR     0x370
#define LAPIC_TIMER_ICR     0x380
#define LAPIC_TIMER_CCR     0x390
#define LAPIC_TIMER_DCR     0x3E0

/* IO APIC寄存器 */
#define IOAPIC_REGSEL       0x00
#define IOAPIC_WINDOW       0x10
#define IOAPIC_ID           0x00
#define IOAPIC_VER          0x01
#define IOAPIC_ARB          0x02
#define IOAPIC_REDTBL_BASE  0x10

/* IO APIC重定向表项标志 */
#define IOAPIC_INT_MASKED   0x00010000
#define IOAPIC_TRIGGER_LEVEL 0x00008000
#define IOAPIC_TRIGGER_EDGE  0x00000000
#define IOAPIC_POLARITY_LOW  0x00002000
#define IOAPIC_POLARITY_HIGH 0x00000000
#define IOAPIC_DEST_PHYSICAL 0x00000000
#define IOAPIC_DEST_LOGICAL  0x00000800
#define IOAPIC_DELIVERY_FIXED    0x00000000
#define IOAPIC_DELIVERY_LOWPRI   0x00000100
#define IOAPIC_DELIVERY_SMI      0x00000200
#define IOAPIC_DELIVERY_NMI      0x00000400
#define IOAPIC_DELIVERY_INIT     0x00000500
#define IOAPIC_DELIVERY_EXTINT   0x00000700

/* APIC初始化 */
int apic_init(void);
int apic_available(void);

/* Local APIC */
uint32_t lapic_read(uint32_t reg);
void lapic_write(uint32_t reg, uint32_t val);
void lapic_send_eoi(void);
uint32_t lapic_get_id(void);

/* IO APIC */
uint32_t ioapic_read(uint32_t reg);
void ioapic_write(uint32_t reg, uint32_t val);
void ioapic_set_irq(uint8_t irq, uint8_t vector, uint8_t dest_apic_id, int masked);
void ioapic_mask_irq(uint8_t irq);
void ioapic_unmask_irq(uint8_t irq);

/* 中断路由 */
int apic_route_irq(uint8_t irq, uint8_t vector);

#endif /* HEPU_APIC_H */
