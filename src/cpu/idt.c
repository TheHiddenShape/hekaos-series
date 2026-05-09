#include "idt.h"

#define IDT_ENTRIES 256

struct idt_entry idt[IDT_ENTRIES];
struct idt_ptr idp;

extern void idt_load (uint32_t);

void
idt_set_gate (uint8_t num, uint32_t base, uint16_t selector, uint8_t flags)
{
    idt[num].base_low = base & 0xFFFF;
    idt[num].base_high = (base >> 16) & 0xFFFF;

    idt[num].selector = selector;
    idt[num].zero = 0;
    idt[num].type_attr = flags;
}

void
idt_init (void)
{
    idp.limit = (sizeof (struct idt_entry) * IDT_ENTRIES) - 1;
    idp.base = (uint32_t)&idt;

    for (int i = 0; i < IDT_ENTRIES; i++)
    {
        idt_set_gate (i, 0, 0, 0);
    }

    idt_set_gate (0, (uint32_t)isr0, 0x08, 0x8E);
    idt_set_gate (1, (uint32_t)isr1, 0x08, 0x8E);
    idt_set_gate (2, (uint32_t)isr2, 0x08, 0x8E);
    idt_set_gate (3, (uint32_t)isr3, 0x08, 0x8E);
    idt_set_gate (4, (uint32_t)isr4, 0x08, 0x8E);
    idt_set_gate (5, (uint32_t)isr5, 0x08, 0x8E);
    idt_set_gate (6, (uint32_t)isr6, 0x08, 0x8E);
    idt_set_gate (7, (uint32_t)isr7, 0x08, 0x8E);
    idt_set_gate (8, (uint32_t)isr8, 0x08, 0x8E);
    idt_set_gate (9, (uint32_t)isr9, 0x08, 0x8E);
    idt_set_gate (10, (uint32_t)isr10, 0x08, 0x8E);
    idt_set_gate (11, (uint32_t)isr11, 0x08, 0x8E);
    idt_set_gate (12, (uint32_t)isr12, 0x08, 0x8E);
    idt_set_gate (13, (uint32_t)isr13, 0x08, 0x8E);
    idt_set_gate (14, (uint32_t)isr14, 0x08, 0x8E);
    idt_set_gate (15, (uint32_t)isr15, 0x08, 0x8E);
    idt_set_gate (16, (uint32_t)isr16, 0x08, 0x8E);
    idt_set_gate (17, (uint32_t)isr17, 0x08, 0x8E);
    idt_set_gate (18, (uint32_t)isr18, 0x08, 0x8E);
    idt_set_gate (19, (uint32_t)isr19, 0x08, 0x8E);
    idt_set_gate (33, (uint32_t)irq1, 0x08, 0x8E);
    idt_set_gate (66, (uint32_t)isr66, 0x08, 0x8E);
    idt_set_gate (0x80, (uint32_t)isr128, 0x08, 0xEF); /* trap gate, DPL=3 */

    idt_load ((uint32_t)&idp);
}
