#include "pit.h"
#include "io.h"
#include "sched.h"

/*
 * PIT mode/command byte for channel 0:
 *   bit 0      : 0   = 16-bit binary counter (vs BCD)
 *   bits 1-3   : 011 = mode 3, square wave generator
 *   bits 4-5   : 11  = access mode lobyte/hibyte (write low, then high)
 *   bits 6-7   : 00  = select channel 0
 *
 * → 0b00110110 = 0x36
 */
#define PIT_CMD_CH0_SQUARE 0x36

void
pit_init (uint32_t hz)
{
    uint32_t divisor = PIT_BASE_HZ / hz;

    outb (PIT_COMMAND, PIT_CMD_CH0_SQUARE);
    outb (PIT_CHANNEL0, (uint8_t)(divisor & 0xFF));
    outb (PIT_CHANNEL0, (uint8_t)((divisor >> 8) & 0xFF));
}

void
timer_tick (void)
{
    schedule ();
}
