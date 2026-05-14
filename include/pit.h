#ifndef PIT_H
#define PIT_H

#include <stdint.h>

/*
 * 8253/8254 Programmable Interval Timer (channel 0 → IRQ 0).
 *
 * Input clock: 1.193182 MHz (NTSC heritage: 14.31818 MHz / 12).
 * Reload value (16-bit) divides this clock; tick frequency = 1193182 / reload.
 */

#define PIT_BASE_HZ 1193182

#define PIT_CHANNEL0 0x40 /* counter for IRQ 0 */
#define PIT_CHANNEL1 0x41 /* legacy DRAM refresh (unused) */
#define PIT_CHANNEL2 0x42 /* PC speaker */
#define PIT_COMMAND 0x43  /* mode/command register */

#define PIT_TICK_HZ 100 /* scheduler tick frequency: one IRQ 0 every 10 ms */

void pit_init (uint32_t hz);
void timer_tick (void);

#endif /* PIT_H */
