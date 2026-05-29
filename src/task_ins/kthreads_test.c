#include "kthreads_test.h"
#include <stdint.h>

/*
 * Test process functions for scheduler validation: each is an infinite loop
 * with a distinct workload profile.
 *
 * - size passed to exec_fn = (pid_fn_end - pid_fn); sentinels rely on TU
 *   declaration order being preserved (true at default optimisation).
 * - noinline keeps each fn as a standalone, addressable block so the size
 *   computation is meaningful.
 * - volatile counters prevent dead-store elimination.
 */

/* heartbeat: a simple counter loop */
__attribute__ ((noinline)) void
kth_heartbeat_fn (void)
{
    volatile uint32_t tick = 0;
    while (1)
    {
        tick++;
    }
}

__attribute__ ((noinline)) void
kth_heartbeat_fn_end (void)
{
}

/* compute: tight arithmetic loop, high CPU pressure */
__attribute__ ((noinline)) void
kth_compute_fn (void)
{
    volatile uint32_t a = 1;
    volatile uint32_t b = 1;
    while (1)
    {
        uint32_t c = a + b;
        a = b;
        b = c;
        /* wrap to avoid overflow stalls */
        if (b > 0xFFFF)
        {
            a = 1;
            b = 1;
        }
    }
}

__attribute__ ((noinline)) void
kth_compute_fn_end (void)
{
}

/* memwrite: sequential writes to a local buffer */
__attribute__ ((noinline)) void
kth_memwrite_fn (void)
{
    volatile uint8_t buf[64];
    volatile uint32_t i = 0;
    while (1)
    {
        buf[i & 63] = (uint8_t)(i & 0xFF);
        i++;
    }
}

__attribute__ ((noinline)) void
kth_memwrite_fn_end (void)
{
}

/* memread: alternating read/write, cache pressure */
__attribute__ ((noinline)) void
kth_memread_fn (void)
{
    volatile uint32_t buf[16];
    volatile uint32_t sum = 0;
    volatile uint32_t i = 0;
    while (1)
    {
        buf[i & 15] = i;
        sum += buf[i & 15];
        i++;
        if (i >= 0xFFFF)
        {
            i = 0;
            sum = 0;
        }
    }
}

__attribute__ ((noinline)) void
kth_memread_fn_end (void)
{
}

/* slow: increments at a coarser granularity; useful to observe a process that
 * does less work per quantum */
__attribute__ ((noinline)) void
kth_slow_fn (void)
{
    volatile uint32_t tick = 0;
    while (1)
    {
        volatile uint32_t spin = 0;
        while (spin < 1000)
        {
            spin++;
        }
        tick++;
    }
}

__attribute__ ((noinline)) void
kth_slow_fn_end (void)
{
}

/* idle: near-empty loop; models a low-priority background task */
__attribute__ ((noinline)) void
kth_idle_fn (void)
{
    volatile uint32_t idle = 0;
    while (1)
    {
        idle++;
    }
}

__attribute__ ((noinline)) void
kth_idle_fn_end (void)
{
}
