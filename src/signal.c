#include "signal.h"
#include "klib.h"
#include "kpanic.h"
#include "printk.h"
#include "task.h"

/*
 * default kernel actions for each signal.
 * TERM: mark the task as zombie (terminate).
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
        case SIGSTOP:
            /* 128 + signum: shell convention so wait() can tell the task was
             * killed by signum rather than exited cleanly */
            do_exit (t, 128 + signum);
            break;

        /* ignore by default */
        case SIGUSR1:
        case SIGUSR2:
        case SIGCHLD:
        case SIGCONT:
        case SIGTSTP:
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

    t->pending_signals |= SIGMASK (signum);

    /* wake a BLOCKED waiter (wait, future sleep) so the scheduler can dispatch
     */
    if (t->state == TASK_BLOCKED)
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
        /* isolate lowest set bit */
        sigset_t bit = pending & (~pending + 1);
        pending &= ~bit;
        t->pending_signals &= ~bit;

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
            sig_default_action (t, signum);
            continue;
        }

        sig_handler_t handler = t->signal_handlers[signum];

        if (handler == SIG_IGN)
        {
            continue;
        }

        if (handler != SIG_DFL)
        {
            handler (signum);
        }
        else
        {
            sig_default_action (t, signum);
        }
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

    pr_info ("signal test passed\n\n");
}
