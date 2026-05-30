#include "signal.h"
#include "klib.h"
#include "kpanic.h"
#include "printk.h"
#include "syscall.h"
#include "task.h"
#include "trap_frame.h"
#include <stddef.h>

/*
 * default kernel actions for each signal.
 * TERM: mark the task as zombie (terminate).
 * STOP: suspend the task (job control); resumed by SIGCONT.
 * IGN: silently discard.
 */
static void
sig_default_action (struct task *t, int signum)
{
    switch (signum)
    {
        /* terminate */
        case SIGHUP:
        case SIGINT:
        case SIGQUIT:
        case SIGILL:
        case SIGABRT:
        case SIGBUS:
        case SIGFPE:
        case SIGKILL:
        case SIGSEGV:
        case SIGPIPE:
        case SIGALRM:
        case SIGTERM:
            /* 128 + signum: shell convention so wait() can tell the task was
             * killed by signum rather than exited cleanly */
            do_exit (t, 128 + signum);
            break;

        /* stop (job control): suspend rather than terminate. A terminate-class
         * signal of lower number is dispatched first, so never override a task
         * that already went ZOMBIE in the same dispatch pass. The scheduler
         * switches away once the state is no longer RUNNING; SIGCONT (handled
         * in kernel_signal_send) resumes the task. */
        case SIGSTOP:
        case SIGTSTP:
            if (t->state != TASK_ZOMBIE)
            {
                t->state = TASK_STOPPED;
            }
            break;

        /* ignore by default */
        case SIGUSR1:
        case SIGUSR2:
        case SIGCHLD:
        case SIGCONT:
        default:
            break;
    }
}

void
kernel_signal_register (struct task *t, int signum, sig_handler_t handler)
{
    if (signum <= 0 || signum >= NSIG)
    {
        return;
    }

    /* SIGKILL and SIGSTOP disposition is immutable */
    if (SIGMASK (signum) & SIG_IMMUTABLE_MASK)
    {
        return;
    }

    t->signal_handlers[signum] = handler;
}

void
kernel_signal_send (struct task *t, int signum)
{
    if (signum <= 0 || signum >= NSIG)
    {
        return;
    }

    /* job-control interplay (POSIX): SIGCONT discards any pending stop and
     * resumes a stopped task immediately; a stop signal discards a pending
     * SIGCONT. The resume must happen here, not in dispatch — a stopped task is
     * never scheduled, so its dispatch would never run. */
    if (signum == SIGCONT)
    {
        t->pending_signals &= ~SIG_STOP_MASK;
        if (t->state == TASK_STOPPED)
        {
            t->state = TASK_RUNNABLE;
        }
    }
    else if (SIGMASK (signum) & SIG_STOP_MASK)
    {
        t->pending_signals &= ~SIGMASK (SIGCONT);
    }

    t->pending_signals |= SIGMASK (signum);

    /* wake a BLOCKED waiter (wait, future sleep) so the scheduler can dispatch.
     * Only SIGKILL and SIGCONT act on a stopped task; SIGKILL must rouse it so
     * the default terminate action can run (other signals stay pending until a
     * SIGCONT resumes the task). */
    if (t->state == TASK_BLOCKED
        || (t->state == TASK_STOPPED && signum == SIGKILL))
    {
        t->state = TASK_RUNNABLE;
    }
}

void
kernel_signal_dispatch (struct task *t)
{
    sigset_t pending = t->pending_signals;

    while (pending)
    {
        /* a task that just terminated or stopped must not keep draining: the
         * remaining signals stay pending until it is reaped or resumed */
        if (t->state == TASK_ZOMBIE || t->state == TASK_STOPPED)
        {
            break;
        }

        /* isolate lowest set bit */
        sigset_t bit = pending & (~pending + 1);
        pending &= ~bit;

        /* recover signum from bit position (bit = 1 << (signum - 1)) */
        int signum = 1;
        sigset_t tmp = bit >> 1;
        while (tmp)
        {
            signum++;
            tmp >>= 1;
        }

        /* immutable signals always take the default action */
        if (bit & SIG_IMMUTABLE_MASK)
        {
            t->pending_signals &= ~bit;
            sig_default_action (t, signum);
            continue;
        }

        sig_handler_t handler = t->signal_handlers[signum];

        if (handler == SIG_IGN)
        {
            t->pending_signals &= ~bit;
            continue;
        }

        if (handler == SIG_DFL)
        {
            t->pending_signals &= ~bit;
            sig_default_action (t, signum);
            continue;
        }

        if (t->is_userspace)
        {
            continue;
        }

        t->pending_signals &= ~bit;
        handler (signum);
    }
}

static void
setup_user_sigframe (struct task *t, struct trap_frame *frame, int signum,
                     sig_handler_t handler)
{
    (void)t;

    uint32_t usp = frame->user_esp - sizeof (struct sigframe);
    usp &= ~0xFu;
    usp -= 4;

    struct sigframe *sf = (struct sigframe *)usp;

    sf->saved = *frame;
    sf->sig = signum;
    sf->pretcode = usp + offsetof (struct sigframe, trampoline);

    sf->trampoline[0] = 0xb8;
    sf->trampoline[1] = (uint8_t)(SYS_SIGRETURN & 0xff);
    sf->trampoline[2] = (uint8_t)((SYS_SIGRETURN >> 8) & 0xff);
    sf->trampoline[3] = (uint8_t)((SYS_SIGRETURN >> 16) & 0xff);
    sf->trampoline[4] = (uint8_t)((SYS_SIGRETURN >> 24) & 0xff);
    sf->trampoline[5] = 0xcd;
    sf->trampoline[6] = 0x80;
    sf->trampoline[7] = 0x90;

    frame->eip = (uint32_t)handler;
    frame->user_esp = usp;
    frame->eflags &= ~(1u << 10);
}

void
signal_check_and_deliver (struct trap_frame *frame)
{
    if ((frame->cs & 3) != 3)
    {
        return;
    }

    struct task *t = current_task;
    sigset_t pending = t->pending_signals;

    while (pending)
    {
        sigset_t bit = pending & (~pending + 1);
        pending &= ~bit;

        int signum = 1;
        sigset_t tmp = bit >> 1;
        while (tmp)
        {
            signum++;
            tmp >>= 1;
        }

        if (bit & SIG_IMMUTABLE_MASK)
        {
            continue;
        }

        sig_handler_t handler = t->signal_handlers[signum];
        if (handler == SIG_DFL || handler == SIG_IGN)
        {
            continue;
        }

        t->pending_signals &= ~bit;
        setup_user_sigframe (t, frame, signum, handler);
        return;
    }
}

/* test-only recording handlers: capture delivery so kernel_signal_dispatch can
 * be asserted without a live scheduler */
static int test_fired;
static int test_last;
static int test_order[2];
static int test_order_n;

static void
test_count_handler (int signo)
{
    test_fired++;
    test_last = signo;
}

static void
test_order_handler (int signo)
{
    if (test_order_n < 2)
    {
        test_order[test_order_n++] = signo;
    }
}

void
signal_test (void)
{
    pr_info ("#### signal test ####\n");

    struct task t;

    /* 1. a registered handler runs on dispatch and the pending bit clears */
    {
        memset (&t, 0, sizeof (t));
        test_fired = 0;
        test_last = 0;
        kernel_signal_register (&t, SIGUSR1, test_count_handler);
        kernel_signal_send (&t, SIGUSR1);
        if (!(t.pending_signals & SIGMASK (SIGUSR1)))
        {
            kpanic ("signal test: send did not set pending");
        }
        kernel_signal_dispatch (&t);
        if (test_fired != 1 || test_last != SIGUSR1)
        {
            kpanic ("signal test: handler did not fire");
        }
        if (t.pending_signals != 0)
        {
            kpanic ("signal test: pending not cleared after dispatch");
        }
        pr_info ("signal: register + dispatch fires handler ok\n");
    }

    /* 2. SIG_IGN silently discards: no handler call, no state change */
    {
        memset (&t, 0, sizeof (t));
        test_fired = 0;
        kernel_signal_register (&t, SIGUSR1, SIG_IGN);
        kernel_signal_send (&t, SIGUSR1);
        kernel_signal_dispatch (&t);
        if (test_fired != 0)
        {
            kpanic ("signal test: SIG_IGN should not fire a handler");
        }
        if (t.state == TASK_ZOMBIE)
        {
            kpanic ("signal test: SIG_IGN terminated the task");
        }
        pr_info ("signal: SIG_IGN discards ok\n");
    }

    /* 3. SIG_DFL terminate: a TERM-class signal turns the task ZOMBIE with exit
     * code 128 + signum. children/parent left NULL so no live tree is touched
     */
    {
        memset (&t, 0, sizeof (t));
        kernel_signal_send (&t, SIGTERM);
        kernel_signal_dispatch (&t);
        if (t.state != TASK_ZOMBIE || t.exit_code != 128 + SIGTERM)
        {
            kpanic ("signal test: SIGTERM default action wrong");
        }
        pr_info ("signal: SIG_DFL terminate (SIGTERM) ok\n");
    }

    /* 4. SIG_DFL ignore: an IGN-class signal (SIGCHLD) does nothing */
    {
        memset (&t, 0, sizeof (t));
        kernel_signal_send (&t, SIGCHLD);
        kernel_signal_dispatch (&t);
        if (t.state == TASK_ZOMBIE)
        {
            kpanic ("signal test: SIGCHLD default should be ignore");
        }
        pr_info ("signal: SIG_DFL ignore (SIGCHLD) ok\n");
    }

    /* 5. SIGKILL disposition is immutable: register is a no-op and the default
     * terminate fires regardless of the requested handler */
    {
        memset (&t, 0, sizeof (t));
        test_fired = 0;
        kernel_signal_register (&t, SIGKILL, test_count_handler);
        if (t.signal_handlers[SIGKILL] != SIG_DFL)
        {
            kpanic ("signal test: SIGKILL handler was mutated");
        }
        kernel_signal_send (&t, SIGKILL);
        kernel_signal_dispatch (&t);
        if (test_fired != 0)
        {
            kpanic ("signal test: SIGKILL ran a custom handler");
        }
        if (t.state != TASK_ZOMBIE || t.exit_code != 128 + SIGKILL)
        {
            kpanic ("signal test: SIGKILL did not terminate");
        }
        pr_info ("signal: SIGKILL immutable + terminate ok\n");
    }

    /* 6. out-of-range signums are rejected by register and send */
    {
        memset (&t, 0, sizeof (t));
        kernel_signal_register (&t, 0, test_count_handler);
        kernel_signal_register (&t, NSIG, test_count_handler);
        kernel_signal_send (&t, 0);
        kernel_signal_send (&t, NSIG);
        if (t.pending_signals != 0)
        {
            kpanic ("signal test: out-of-range send set a pending bit");
        }
        pr_info ("signal: out-of-range signum guarded ok\n");
    }

    /* 7. dispatch order is lowest-signum-first: SIGINT (2) is delivered before
     * SIGUSR1 (10) even though both are pending at once. custom handlers keep
     * the TERM-class SIGINT from terminating the task */
    {
        memset (&t, 0, sizeof (t));
        test_order_n = 0;
        test_order[0] = 0;
        test_order[1] = 0;
        kernel_signal_register (&t, SIGINT, test_order_handler);
        kernel_signal_register (&t, SIGUSR1, test_order_handler);
        kernel_signal_send (&t, SIGUSR1);
        kernel_signal_send (&t, SIGINT);
        kernel_signal_dispatch (&t);
        if (test_order_n != 2 || test_order[0] != SIGINT
            || test_order[1] != SIGUSR1)
        {
            kpanic ("signal test: dispatch not lowest-signum-first");
        }
        pr_info ("signal: dispatch order lowest-first ok\n");
    }

    /* 8. signal_from_exception maps CPU vectors to their canonical signal */
    {
        if (signal_from_exception (0) != SIGFPE
            || signal_from_exception (6) != SIGILL
            || signal_from_exception (13) != SIGSEGV
            || signal_from_exception (14) != SIGSEGV
            || signal_from_exception (17) != SIGBUS
            || signal_from_exception (99) != 0)
        {
            kpanic ("signal test: exception-to-signal map wrong");
        }
        pr_info ("signal: exception-to-signal map ok\n");
    }

    /* 9. job control: SIGSTOP suspends instead of terminating, a non-KILL
     * signal stays pending while stopped, and SIGCONT resumes the task while
     * discarding the pending stop */
    {
        memset (&t, 0, sizeof (t));
        kernel_signal_send (&t, SIGSTOP);
        kernel_signal_dispatch (&t);
        if (t.state != TASK_STOPPED)
        {
            kpanic ("signal test: SIGSTOP did not stop the task");
        }

        /* SIGTERM sent while stopped must stay pending, not be delivered */
        kernel_signal_send (&t, SIGTERM);
        kernel_signal_dispatch (&t);
        if (t.state != TASK_STOPPED || !(t.pending_signals & SIGMASK (SIGTERM)))
        {
            kpanic ("signal test: stopped task drained a pending signal");
        }

        /* SIGCONT resumes the task and clears the pending stop bit */
        kernel_signal_send (&t, SIGCONT);
        if (t.state != TASK_RUNNABLE || (t.pending_signals & SIG_STOP_MASK))
        {
            kpanic ("signal test: SIGCONT did not resume / clear stop");
        }
        pr_info ("signal: SIGSTOP suspends + SIGCONT resumes ok\n");
    }

    pr_info ("signal test passed\n\n");
}
