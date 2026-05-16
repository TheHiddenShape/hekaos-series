#include "proc_test.h"
#include "syscall.h"
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

/* pid 1 — init: future zombie reaper; for now a simple heartbeat counter */
__attribute__ ((noinline)) void
pid1_fn (void)
{
    volatile uint32_t tick = 0;
    while (1)
    {
        tick++;
    }
}

__attribute__ ((noinline)) void
pid1_fn_end (void)
{
}

/* pid 2 — busy compute: tight arithmetic loop, high CPU pressure */
__attribute__ ((noinline)) void
pid2_fn (void)
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
pid2_fn_end (void)
{
}

/* pid 3 — memory writer: sequential writes to a local buffer */
__attribute__ ((noinline)) void
pid3_fn (void)
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
pid3_fn_end (void)
{
}

/* pid 4 — memory reader: alternating read/write, cache pressure */
__attribute__ ((noinline)) void
pid4_fn (void)
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
pid4_fn_end (void)
{
}

/* pid 5 — slow counter: increments at a coarser granularity; useful to
 * observe a process that does less work per quantum */
__attribute__ ((noinline)) void
pid5_fn (void)
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
pid5_fn_end (void)
{
}

/* pid 6 — idle probe: near-empty loop; models a low-priority background task */
__attribute__ ((noinline)) void
pid6_fn (void)
{
    volatile uint32_t idle = 0;
    while (1)
    {
        idle++;
    }
}

__attribute__ ((noinline)) void
pid6_fn_end (void)
{
}

/* fork driver: .rodata strings work in both pgdirs because pd[0] is shared */
__attribute__ ((noinline)) void
forker_fn (void)
{
    static const char msg_parent[] = "fork: parent\n";
    static const char msg_child[] = "fork: child\n";
    static const char msg_fail[] = "fork: failed\n";

    int32_t pid = trigger_syscall (SYS_FORK, 0, 0, 0);
    if (pid == 0)
    {
        trigger_syscall (SYS_WRITE, 1, (uint32_t)msg_child,
                         sizeof (msg_child) - 1);
    }
    else if (pid > 0)
    {
        trigger_syscall (SYS_WRITE, 1, (uint32_t)msg_parent,
                         sizeof (msg_parent) - 1);
        trigger_syscall (SYS_WAITPID, 0, 0, 0);
    }
    else
    {
        trigger_syscall (SYS_WRITE, 1, (uint32_t)msg_fail,
                         sizeof (msg_fail) - 1);
    }
    trigger_syscall (SYS_EXIT, 0, 0, 0);
}

__attribute__ ((noinline)) void
forker_fn_end (void)
{
}
