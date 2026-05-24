#include "sched.h"
#include "paging.h"
#include "printk.h"
#include "signal.h"
#include "tss.h"
#include <stddef.h>
#include <stdint.h>

/* These offsets are consumed by the asm in irq_common_stub to swap kernel
 * stacks and reload CR3. If struct task layout changes, the asserts fail
 * loudly at build time and the asm must be updated to match. */
_Static_assert (offsetof (struct task, thread.esp) == 8,
                "irq_common_stub expects thread.esp at +8");
_Static_assert (offsetof (struct task, mm.pgdir) == 32,
                "irq_common_stub expects mm.pgdir at +32");

volatile uint32_t need_resched = 0;
struct task *next_task = NULL;

/* lap tracking: lap_anchor is the first task picked since boot (or reset);
 * each time we cycle back to it, a full round-robin lap has completed.
 * blocked_marker / unblocked_marker ensure the test transitions fire at most
 * once each. */
#define LAP_BLOCK_TRIGGER 3
#define LAP_UNBLOCK_TRIGGER (LAP_BLOCK_TRIGGER + 1)
static struct task *lap_anchor = NULL;
static uint32_t lap_num = 0;
static int blocked_marker = 0;
static int unblocked_marker = 0;

static void
maybe_block_faulty_target (struct task *next)
{
    if (blocked_marker || lap_num != LAP_BLOCK_TRIGGER)
    {
        return;
    }

    /* find the highest-pid RUNNABLE (or currently RUNNING) task, excluding
     * init (pid 0) and the task we're about to switch into */
    struct task *faulty_target = NULL;
    for (struct task *t = task_list_head; t != NULL; t = t->next)
    {
        if (t == next || t == lap_anchor || t->pid == 0)
        {
            continue;
        }
        if (t->state != TASK_RUNNABLE && t->state != TASK_RUNNING)
        {
            continue;
        }
        if (!faulty_target || t->pid > faulty_target->pid)
        {
            faulty_target = t;
        }
    }
    if (!faulty_target)
    {
        return;
    }

    faulty_target->state = TASK_BLOCKED;
    blocked_marker = 1;
    pr_info ("sched: PID %u -> BLOCKED (test trigger at lap %u)\n",
             faulty_target->pid, lap_num);
}

/* symmetric counterpart to maybe_block_faulty_target: at LAP_UNBLOCK_TRIGGER,
 * flip every BLOCKED task back to RUNNABLE so it re-enters the rotation on the
 * next pick. fires once. */
static void
maybe_unblock_faulty_target (void)
{
    if (unblocked_marker || lap_num != LAP_UNBLOCK_TRIGGER)
    {
        return;
    }

    for (struct task *t = task_list_head; t != NULL; t = t->next)
    {
        if (t->state != TASK_BLOCKED)
        {
            continue;
        }
        t->state = TASK_RUNNABLE;
        pr_info ("sched: PID %u -> RUNNABLE (test unblock at lap %u)\n", t->pid,
                 lap_num);
    }
    unblocked_marker = 1;
}

/* O(n) round-robin walk over the flat task_list_head chain (task->next).
 * Starts right after current_task and wraps; skips non-RUNNABLE entries and
 * current_task itself. Returns current_task when nothing else is eligible. */
struct task *
pick_next_task (void)
{
    struct task *t = current_task->next;
    if (!t)
    {
        t = task_list_head;
    }

    struct task *start = t;
    while (1)
    {
        if (t != current_task && t->state == TASK_RUNNABLE)
        {
            return t;
        }
        t = t->next ? t->next : task_list_head;
        if (t == start)
        {
            break;
        }
    }

    return current_task;
}

void
schedule (void)
{
    /* drain pending signals first: default action may flip state to ZOMBIE,
     * which the check below must observe to force an immediate switch */
    if (current_task->pending_signals)
    {
        kernel_signal_dispatch (current_task);
    }

    /* if current_task is no longer RUNNING (got marked BLOCKED/ZOMBIE between
     * ticks), force a switch now instead of letting it burn the rest of its
     * quantum; the on-CPU task must always be in state RUNNING */
    const char *reason;
    if (current_task->state != TASK_RUNNING)
    {
        reason = (current_task->state == TASK_ZOMBIE) ? "zombie" : "blocked";
    }
    else if (--current_task->time_left > 0)
    {
        return;
    }
    else
    {
        reason = "quantum";
    }

    /* snapshot the consumed slice before refilling time_left */
    uint32_t consumed = current_task->quantum - current_task->time_left;
    current_task->time_left = current_task->quantum;

    struct task *next = pick_next_task ();
    if (next == current_task)
    {
        /* nothing else eligible, keep running */
        return;
    }

    if (current_task->state == TASK_RUNNING)
    {
        current_task->state = TASK_RUNNABLE;
    }
    next->state = TASK_RUNNING;

    if (next->is_userspace)
    {
        tss_set_esp0 ((uint32_t)next->kstack + KSTACK_PAGES * PAGE_SIZE);
    }

    next_task = next;
    need_resched = 1;

    pr_info ("sched: %u -> %u (used %u/%u, %s)\n", current_task->pid, next->pid,
             consumed, current_task->quantum, reason);

    /* lap accounting: first pick arms the anchor; each return to it = one lap
     */
    if (lap_anchor == NULL)
    {
        lap_anchor = next;
    }
    else if (next == lap_anchor)
    {
        lap_num++;
        pr_info ("sched: round-robin lap %u complete\n", lap_num);
        maybe_block_faulty_target (next);
        maybe_unblock_faulty_target ();
    }
}