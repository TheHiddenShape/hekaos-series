#ifndef SYSCALL_H
#define SYSCALL_H

#include "trap_frame.h"
#include <stdint.h>

/* syscall numbers (Linux i386 ABI) */
#define SYS_EXIT 1
#define SYS_WRITE 4

void syscall_dispatch (struct trap_frame *frame);

#endif
