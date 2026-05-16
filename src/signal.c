#include "signal.h"
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
            t->state = TASK_ZOMBIE;
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
