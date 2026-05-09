#include "syscall.h"
#include "interrupts.h"
#include "trap_frame.h"
#include "vga.h"
#include <stddef.h>
#include <stdint.h>

#define SYSCALL_TABLE_SIZE 256

typedef int32_t (*syscall_fn_t) (uint32_t, uint32_t, uint32_t);

static int32_t
sys_exit (uint32_t status, uint32_t unused1, uint32_t unused2)
{
    (void)status;
    (void)unused1;
    (void)unused2;
    disable_interrupts_and_halt ();
    return 0;
}

static int32_t
sys_write (uint32_t fd, uint32_t buf, uint32_t count)
{
    if (fd != 1 && fd != 2)
    {
        return -1;
    }
    const char *s = (const char *)buf;
    for (uint32_t i = 0; i < count; i++)
    {
        terminal_putchar (s[i]);
    }
    return (int32_t)count;
}

static const syscall_fn_t syscall_table[SYSCALL_TABLE_SIZE] = {
    [SYS_EXIT] = sys_exit,
    [SYS_WRITE] = sys_write,
};

void
syscall_dispatch (struct trap_frame *frame)
{
    uint32_t num = frame->eax;

    if (num >= SYSCALL_TABLE_SIZE || !syscall_table[num])
    {
        frame->eax = (uint32_t)-1;
        return;
    }

    frame->eax
        = (uint32_t)syscall_table[num](frame->ebx, frame->ecx, frame->edx);
}
