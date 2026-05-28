#ifndef INTERRUPTS_H
#define INTERRUPTS_H

#include <stdint.h>

void enable_interrupts (void);
void disable_interrupts (void);
void halt_cpu (void);
void disable_interrupts_and_halt (void);
uint32_t get_esp (void);

/* the current task is terminating: park the CPU with interrupts enabled so the
 * timer tick can schedule it out. cli+hlt would deadlock. never returns. */
__attribute__ ((noreturn)) void cpu_park_dying (void);

#endif
