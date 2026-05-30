#ifndef SIGNAL_H
#define SIGNAL_H

#include "trap_frame.h"
#include <stdint.h>

/*
 * kernel-internal signal definitions for hekaOS.
 *
 * Signal numbers follow the Linux i386 ABI so that future userland
 * compatibility is straightforward. Only kernel-visible definitions
 * live here; no sigaction, no ucontext, no SA_* flags until userland
 * and a scheduler exist.
 *
 * delivery model (future):
 *   1. Any kernel path calls signal_send(task, sig).
 *   2. The scheduler checks task->pending_signals before returning to
 *      userland and dispatches accordingly.
 *   3. ISR handlers (isr.c) map CPU exceptions to their canonical signal
 *      via signal_from_exception(int_no).
 */

/* signal numbers (Linux i386 ABI) */
#define SIGHUP 1   /* hangup — controlling terminal closed */
#define SIGINT 2   /* interrupt — Ctrl-C */
#define SIGQUIT 3  /* quit — Ctrl-\, generates core */
#define SIGILL 4   /* illegal instruction  (ISR #6) */
#define SIGTRAP 5  /* breakpoint / single-step  (ISR #1, #3) */
#define SIGABRT 6  /* abort — abnormal termination */
#define SIGBUS 7   /* bus error / alignment fault  (ISR #17) */
#define SIGFPE 8   /* floating-point / divide-by-zero  (ISR #0, #16) */
#define SIGKILL 9  /* unconditional kill — cannot be caught or ignored */
#define SIGUSR1 10 /* user-defined signal 1 */
#define SIGSEGV 11 /* segmentation fault  (ISR #14) */
#define SIGUSR2 12 /* user-defined signal 2 */
#define SIGPIPE 13 /* broken pipe — write to closed pipe */
#define SIGALRM 14 /* alarm clock expired */
#define SIGTERM 15 /* graceful termination request */
#define SIGSTKFLT                                                              \
    16             /* stack fault on coprocessor (unused in Linux, reserved) */
#define SIGCHLD 17 /* child stopped or terminated */
#define SIGCONT 18 /* continue if stopped */
#define SIGSTOP 19 /* stop — cannot be caught or ignored */
#define SIGTSTP 20 /* terminal stop — Ctrl-Z */

#define NSIG 32 /* total number of signals (bitmask width) */

typedef uint32_t sigset_t;

#define SIGMASK(sig) ((sigset_t)1u << ((sig) - 1))
#define SIGSET_FULL ((sigset_t)~0u)
#define SIGSET_EMPTY ((sigset_t)0u)
#define SIG_IMMUTABLE_MASK (SIGMASK (SIGKILL) | SIGMASK (SIGSTOP))

typedef void (*sig_handler_t) (int signo);

#define SIG_DFL ((sig_handler_t)0)    /* default kernel action */
#define SIG_IGN ((sig_handler_t)1)    /* explicitly ignored */
#define SIG_ERR ((sig_handler_t) - 1) /* error return value */

/*
 * maps an ISR number (int_no from struct trap_frame) to the
 * corresponding POSIX signal.
 */
static inline int
signal_from_exception (uint32_t int_no)
{
    switch (int_no)
    {
        case 0:
            return SIGFPE;
        case 1:
            return SIGTRAP;
        case 3:
            return SIGTRAP;
        case 4:
            return SIGSEGV; /* #OF overflow (INTO) → exc_overflow sends SIGSEGV
                             */
        case 5:
            return SIGSEGV; /* #BR BOUND range exceeded → SEGV_BNDERR */
        case 6:
            return SIGILL;
        case 8:
            return SIGABRT;
        case 10:
            return SIGSEGV; /* #TS invalid TSS → force_sig(SIGSEGV) in Linux */
        case 11:
            return SIGBUS;
        case 12:
            return SIGBUS;
        case 13:
            return SIGSEGV;
        case 14:
            return SIGSEGV;
        case 16:
            return SIGFPE;
        case 17:
            return SIGBUS;
        case 19:
            return SIGFPE;
        default:
            return 0;
    }
}

struct task;

struct sigframe
{
    uint32_t pretcode;
    int32_t sig;
    struct trap_frame saved;
    uint8_t trampoline[8];
} __attribute__ ((packed));

void kernel_signal_register (struct task *t, int signum, sig_handler_t handler);
void kernel_signal_send (struct task *t, int signum);
void kernel_signal_dispatch (struct task *t);

void signal_check_and_deliver (struct trap_frame *frame);

/* boot-time self-test of register/send/dispatch, default actions, immutability,
 * dispatch order and the exception-to-signal map */
void signal_test (void);

#endif
