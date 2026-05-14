#include "io.h"
#include <stdint.h>

/* pic ports, each chip (master and slave) has a command port and a data port.
   when no command is issued, the data port allows us to access the interrupt
   mask of the 8259 PIC. */

#define PIC1_COMMAND 0x20
#define PIC1_DATA (PIC1_COMMAND + 1)

#define PIC2_COMMAND 0xA0
#define PIC2_DATA (PIC2_COMMAND + 1)

#define PIC2_SHIFT_OFFSET 0x28
#define PIC1_SHIFT_OFFSET 0x20

#define PIC_EOI 0x20 /* end of interrupt */

#define ICW1_ICW4 0x01
#define ICW1_INIT 0x10
#define ICW4_8086 0x01

/* remap the PIC: by default, IRQ 0-7 map to INT 0x08-0x0F but INT 0x08-0x0F are
   reserved for CPU exceptions. so we remap: IRQ 0-15 → INT 32-47 */

void
pic_remap (void)
{
    outb (PIC1_COMMAND, ICW1_INIT | ICW1_ICW4);
    io_wait ();

    outb (PIC2_COMMAND, ICW1_INIT | ICW1_ICW4);
    io_wait ();

    /* ICW2 shift vector offsets (re-definition) */
    outb (PIC1_DATA, PIC1_SHIFT_OFFSET); /* master PIC: IRQ 0-7  → INT 32-39 */
    io_wait ();
    outb (PIC2_DATA, PIC2_SHIFT_OFFSET); /* slave PIC:  IRQ 8-15 → INT 40-47 */
    io_wait ();

    /* ICW3 cascade topology configuration: */
    outb (PIC1_DATA, 4);
    io_wait ();

    /* tell slave PIC its cascade identity */
    outb (PIC2_DATA, 2);
    io_wait ();

    /* ICW4, set 8086 mode */
    outb (PIC1_DATA, ICW4_8086);
    io_wait ();
    outb (PIC2_DATA, ICW4_8086);
    io_wait ();

    /* mask all interrupts except IRQ0 (PIT) and IRQ1 (keyboard) */
    outb (PIC1_DATA, 0xFC);
    outb (PIC2_DATA, 0xFF); /* mask all slave PIC interrupts */

    /* end of sequence */
}

void
pic_send_eoi (uint8_t irq)
{
    if (irq >= 8)
    {
        outb (PIC2_COMMAND, PIC_EOI);
    }
    outb (PIC1_COMMAND, PIC_EOI);
}
