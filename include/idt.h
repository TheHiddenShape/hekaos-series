#ifndef IDT_H
#define IDT_H

#include <stdint.h>

/*
 * https://wiki.osdev.org/Interrupt_Descriptor_Table
 * Intel SDM Vol. 3A 6.11 "IDT Descriptors"
 */

struct idt_entry
{
    uint16_t base_low;
    uint16_t selector;
    uint8_t zero;
    uint8_t type_attr;
    uint16_t base_high;
} __attribute__ ((packed));

struct idt_ptr
{
    uint16_t limit;
    uint32_t base;
} __attribute__ ((packed));

/* CPU exceptions */
extern void isr0 (void);  /* divide by zero */
extern void isr1 (void);  /* debugger */
extern void isr2 (void);  /* NMI */
extern void isr3 (void);  /* breakpoint */
extern void isr4 (void);  /* overflow */
extern void isr5 (void);  /* bounds */
extern void isr6 (void);  /* invalid opcode */
extern void isr7 (void);  /* coprocessor not available */
extern void isr8 (void);  /* double fault */
extern void isr9 (void);  /* coprocessor segment overrun */
extern void isr10 (void); /* invalid TSS */
extern void isr11 (void); /* segment not present */
extern void isr12 (void); /* stack fault */
extern void isr13 (void); /* general protection fault */
extern void isr14 (void); /* page fault */
extern void isr15 (void); /* reserved */
extern void isr16 (void); /* math fault */
extern void isr17 (void); /* alignment check */
extern void isr18 (void); /* machine check */
extern void isr19 (void); /* SIMD floating-point exception */

/* software interrupts */
extern void isr66 (void);  /* trap frame test (0x42) */
extern void isr128 (void); /* syscall (int 0x80) */

/* hardware interrupts (IRQs) */
extern void irq1 (void); /* keyboard (IRQ 1) */

void idt_init (void);
void idt_set_gate (uint8_t num, uint32_t base, uint16_t selector,
                   uint8_t flags);

#endif
