#include "syscall.h"
#include "interrupts.h"
#include "signal.h"
#include "task.h"
#include "trap_frame.h"
#include "vga.h"
#include <stddef.h>
#include <stdint.h>

#define SYSCALL_TABLE_SIZE 256

typedef int32_t (*syscall_fn_t) (uint32_t, uint32_t, uint32_t);

static int32_t
sys_exit (uint32_t status, uint32_t unused1, uint32_t unused2)
{
    (void)unused1;
    (void)unused2;

    do_exit (current_task, (int32_t)status);

    /* sti+hlt so the timer tick can switch us out (cli+hlt would deadlock) */
    while (1)
    {
        enable_interrupts ();
        halt_cpu ();
    }

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

static int32_t
sys_getuid (uint32_t unused0, uint32_t unused1, uint32_t unused2)
{
    (void)unused0;
    (void)unused1;
    (void)unused2;
    return (int32_t)current_task->uid;
}

static int32_t
sys_geteuid (uint32_t unused0, uint32_t unused1, uint32_t unused2)
{
    (void)unused0;
    (void)unused1;
    (void)unused2;
    return (int32_t)current_task->euid;
}

/* returns previous handler; SIGKILL/SIGSTOP silently kept, bad signum ->
 * SIG_ERR */
static int32_t
sys_signal (uint32_t signum_u, uint32_t handler_u, uint32_t unused2)
{
    (void)unused2;
    int signum = (int)signum_u;
    sig_handler_t handler = (sig_handler_t)handler_u;

    if (signum <= 0 || signum >= NSIG)
    {
        return (int32_t)(uintptr_t)SIG_ERR;
    }

    sig_handler_t previous = current_task->signal_handlers[signum];
    kernel_signal_register (current_task, signum, handler);
    return (int32_t)(uintptr_t)previous;
}

/* euid==0 or euid matches target uid/euid; sig==0 is a permission probe */
static int32_t
sys_kill (uint32_t pid_u, uint32_t signum_u, uint32_t unused2)
{
    (void)unused2;
    uint32_t pid = pid_u;
    int signum = (int)signum_u;

    if (signum < 0 || signum >= NSIG)
    {
        return -1;
    }

    struct task *target = NULL;
    for (struct task *t = task_list_head; t != NULL; t = t->next)
    {
        if (t->pid == pid)
        {
            target = t;
            break;
        }
    }
    if (!target)
    {
        return -1;
    }

    int allowed = (current_task->euid == 0)
                  || (current_task->euid == target->uid)
                  || (current_task->euid == target->euid);
    if (!allowed)
    {
        return -1;
    }

    if (signum == 0)
    {
        return 0;
    }

    kernel_signal_send (target, signum);

    /* self-signal: dispatch synchronously so the effect is visible on return */
    if (target == current_task)
    {
        kernel_signal_dispatch (current_task);
    }

    return 0;
}

/* block until any child becomes ZOMBIE; -1 if no children. SIGCHLD wakes us. */
static int32_t
sys_wait (uint32_t wstatus_u, uint32_t unused1, uint32_t unused2)
{
    (void)unused1;
    (void)unused2;
    int32_t *wstatus = (int32_t *)(uintptr_t)wstatus_u;

    /* A normal process with no children gets -1 (POSIX ECHILD). init (PID 1)
     * instead parks until a child is orphaned to it — it must never spin. */
    if (current_task->children == NULL && current_task != init_proc)
    {
        return -1;
    }

    while (1)
    {
        struct task *zombie = NULL;
        struct task *c;
        task_for_each_child (current_task, c)
        {
            if (c->state == TASK_ZOMBIE)
            {
                zombie = c;
                break;
            }
        }

        if (zombie != NULL)
        {
            int32_t pid = (int32_t)zombie->pid;
            if (wstatus != NULL)
            {
                *wstatus = zombie->exit_code;
            }
            task_reap (zombie);
            return pid;
        }

        /* yield until a signal wakes us (kernel_signal_send flips
         * BLOCKED->RUNNABLE) */
        current_task->state = TASK_BLOCKED;
        while (current_task->state == TASK_BLOCKED)
        {
            enable_interrupts ();
            halt_cpu ();
        }
    }
}

static int32_t
sys_fork (uint32_t unused0, uint32_t unused1, uint32_t unused2)
{
    (void)unused0;
    (void)unused1;
    (void)unused2;
    return task_fork (current_task->thread.tf);
}

static const syscall_fn_t syscall_table[SYSCALL_TABLE_SIZE] = {
    [SYS_EXIT] = sys_exit,     [SYS_FORK] = sys_fork,
    [SYS_WRITE] = sys_write,   [SYS_WAITPID] = sys_wait,
    [SYS_KILL] = sys_kill,     [SYS_SIGNAL] = sys_signal,
    [SYS_GETUID] = sys_getuid, [SYS_GETEUID] = sys_geteuid,
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

    /* expose the active trap frame for handlers that need it (e.g. fork clone)
     */
    current_task->thread.tf = frame;

    frame->eax
        = (uint32_t)syscall_table[num](frame->ebx, frame->ecx, frame->edx);
}
