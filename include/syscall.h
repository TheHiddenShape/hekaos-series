#ifndef SYSCALL_H
#define SYSCALL_H

#include "trap_frame.h"
#include <stdint.h>

/* syscall numbers (Linux i386 ABI) */
#define SYS_EXIT 1
#define SYS_FORK 2
#define SYS_WRITE 4
#define SYS_WAITPID 7
#define SYS_KILL 37
#define SYS_SIGNAL 48
#define SYS_SIGRETURN 119
#define SYS_GETUID 199
#define SYS_GETEUID 201

void syscall_dispatch (struct trap_frame *frame);

/* int $0x80 trampoline for in-kernel callers (kthreads_test) */
int32_t trigger_syscall (uint32_t num, uint32_t arg0, uint32_t arg1,
                         uint32_t arg2);

#endif
