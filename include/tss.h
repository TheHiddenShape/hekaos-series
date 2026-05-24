#ifndef TSS_H
#define TSS_H

#include <stdint.h>

/*
 * Task State Segment.
 * Intel SDM Vol. 3A 7.2.1. Single CPU-wide instance: with software task
 * switching, only ss0:esp0 (and iomap_base) are alive. The other ~23 fields
 * exist because the hardware layout requires them and stay zero forever.
 */
struct tss_entry
{
    uint32_t prev_tss;
    uint32_t esp0;
    uint32_t ss0;
    uint32_t esp1, ss1, esp2, ss2;
    uint32_t cr3, eip, eflags;
    uint32_t eax, ecx, edx, ebx, esp, ebp, esi, edi;
    uint32_t es, cs, ss, ds, fs, gs, ldt;
    uint16_t trap;
    uint16_t iomap_base;
} __attribute__ ((packed));

extern struct tss_entry tss;

void tss_init (void);

/* Write the kernel stack top of the task that will return to Ring 3. Called
 * from schedule() on every switch to a user task; ignored by the CPU until
 * the next Ring3 -> Ring0 transition. */
void tss_set_esp0 (uint32_t esp0);

/* Defined in tss_flush.s: ltr on selector 0x38 (GDT entry 7). */
void tss_flush (void);

#endif
