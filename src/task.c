#include "klib.h"
#include "kmalloc.h"
#include "kmem_dyn_alloc.h"
#include "kpanic.h"
#include "paging.h"
#include "printk.h"
#include "sched.h"
#include "signal.h"
#include "trap_frame.h"
#include "vga.h"
#include <stddef.h>
#include <stdint.h>

/* kernel stack VA base: one slot per process, just below the vmalloc zone */
#define KSTACK_VA_BASE 0xEF000000

/* boot-reserved PID for the userland init / reaper (see exec_init_fn). Sits
 * between init_task (PID 0) and kthreadd (PID 2); never handed out by
 * pid_alloc. */
#define INIT_PID 1

uint32_t task_counter = 0;
struct task *task_list_head = NULL;
struct task *current_task = NULL;
struct task *init_proc = NULL; /* PID 1: userland init / reaper */

struct task init_task = {
    .state = TASK_RUNNING,
    .pid = 0,
    .uid = 0,
    .euid = 0,
    .quantum = PROC_QUANTUM,
    .time_left = PROC_QUANTUM,
};

/* PID 2: sentinel parent of kthreads. Never enters task_list_head, never
 * scheduled; state is set defensively but never read. PID 1 is reserved for
 * the Ring 3 init (INIT_PID), brought up at boot by exec_init_fn. */
struct task kthreadd_task = {
    .state = TASK_BLOCKED,
    .pid = 2,
    .uid = 0,
    .euid = 0,
    .quantum = PROC_QUANTUM,
    .time_left = PROC_QUANTUM,
};

void
task_init (void)
{
    task_list_head = &init_task;
    current_task = &init_task;
    /* inherit the currently loaded kernel pgdir so the asm switch can safely
     * reload CR3 when other tasks switch back to init_task */
    init_task.mm.pgdir = read_cr3 ();

    /* graft kthreadd under init in the genealogy tree; pid_alloc starts above
     * the reserved range, so the first dynamic task gets PID 3 (PID 1 is
     * reserved for init, PID 2 is kthreadd) */
    task_add_child (&init_task, &kthreadd_task);
    task_counter = 2;
}

/* sole authority over task_counter. PIDs 0/1/2 are boot-reserved (init_task,
 * init, kthreadd); every dynamic task gets its PID from here. Returns -1 when
 * the table is full. pid_release rolls a claim back on a failed spawn. */
static int32_t
pid_alloc (void)
{
    if (task_counter >= MAX_PROC - 1)
    {
        return -1;
    }
    return (int32_t)++task_counter;
}

static void
pid_release (void)
{
    --task_counter;
}

void
task_log_putchar (struct task *t, char c)
{
    if (t->log_buf == NULL)
    {
        t->log_buf = kmem_alloc (TASK_LOG_SIZE);
        if (t->log_buf == NULL)
        {
            return;
        }
    }
    t->log_buf[t->log_head] = c;
    t->log_head = (t->log_head + 1) & (TASK_LOG_SIZE - 1);
    if (t->log_len < TASK_LOG_SIZE)
    {
        t->log_len++;
    }
}

void
task_dump_log (uint32_t pid)
{
    struct task *t = NULL;
    for (struct task *it = task_list_head; it != NULL; it = it->next)
    {
        if (it->pid == pid)
        {
            t = it;
            break;
        }
    }
    if (t == NULL)
    {
        terminal_writestring ("plog: no such pid\n");
        return;
    }
    if (t->log_buf == NULL || t->log_len == 0)
    {
        return;
    }

    uint32_t start = (t->log_len < TASK_LOG_SIZE) ? 0 : t->log_head;
    for (uint32_t i = 0; i < t->log_len; i++)
    {
        terminal_putchar (t->log_buf[(start + i) & (TASK_LOG_SIZE - 1)]);
    }
}

uint32_t
task_input_read (struct task *t, char *dst, uint32_t count)
{
    uint32_t n = 0;
    while (n < count && t->in_buf != NULL && t->in_head != t->in_tail)
    {
        dst[n] = t->in_buf[t->in_tail];
        t->in_tail = (t->in_tail + 1) & (TASK_INPUT_SIZE - 1);
        n++;
    }
    return n;
}

static void
task_input_push (struct task *t, char c)
{
    uint32_t next = (t->in_head + 1) & (TASK_INPUT_SIZE - 1);
    if (next == t->in_tail)
    {
        return;
    }
    t->in_buf[t->in_head] = c;
    t->in_head = next;
}

int32_t
task_feed (uint32_t pid, const char *s)
{
    struct task *t = NULL;
    for (struct task *it = task_list_head; it != NULL; it = it->next)
    {
        if (it->pid == pid)
        {
            t = it;
            break;
        }
    }
    if (t == NULL)
    {
        return -1;
    }
    if (t->in_buf == NULL)
    {
        t->in_buf = kmem_alloc (TASK_INPUT_SIZE);
        if (t->in_buf == NULL)
        {
            return -1;
        }
    }
    for (uint32_t i = 0; s[i] != '\0'; i++)
    {
        task_input_push (t, s[i]);
    }
    task_input_push (t, '\n');
    return 0;
}

void
task_add_child (struct task *parent, struct task *child)
{
    child->parent = parent;
    child->sibling = parent->children;
    parent->children = child;
}

void
task_remove_child (struct task *parent, struct task *child)
{
    struct task *cur = parent->children;

    if (cur == child)
    {
        parent->children = child->sibling;
    }
    else
    {
        while (cur != NULL && cur->sibling != child)
        {
            cur = cur->sibling;
        }
        if (cur != NULL)
        {
            cur->sibling = child->sibling;
        }
    }

    child->sibling = NULL;
    child->parent = NULL;
}

void
exec_fn (uint32_t *addr, uint32_t *function, uint32_t size)
{
    if (size == 0)
    {
        return;
    }

    int32_t pid = pid_alloc ();
    if (pid < 0)
    {
        return;
    }

    struct task *t = kmalloc (sizeof (struct task));
    if (!t)
    {
        pid_release ();
        return;
    }
    memset (t, 0, sizeof (struct task));

    t->pid = (uint32_t)pid;

    /* one slot of KSTACK_PAGES per pid in the dedicated VA zone */
    uint32_t kstack_va = KSTACK_VA_BASE + t->pid * KSTACK_PAGES * PAGE_SIZE;
    for (uint32_t p = 0; p < KSTACK_PAGES; p++)
    {
        void *va = (void *)(kstack_va + p * PAGE_SIZE);
        if (!alloc_page (va, PAGE_PRESENT | PAGE_RW))
        {
            for (uint32_t f = 0; f < p; f++)
            {
                free_page ((void *)(kstack_va + f * PAGE_SIZE));
            }
            kfree (t);
            pid_release ();
            return;
        }
    }
    uint32_t kstack_top = kstack_va + KSTACK_PAGES * PAGE_SIZE;

    kstack_top -= sizeof (struct trap_frame);
    struct trap_frame *tf = (struct trap_frame *)kstack_top;
    memset (tf, 0, sizeof (struct trap_frame));

    tf->eip = (uint32_t)addr;
    tf->cs = 0x08;
    tf->eflags = 0x202; /* IF=1, reserved bit set */
    tf->ds = 0x10;

    t->uid = current_task->uid;
    t->euid = current_task->euid;
    t->quantum = PROC_QUANTUM;
    t->time_left = PROC_QUANTUM;
    t->kstack = (void *)kstack_va;
    t->thread.tf = tf;
    t->thread.esp = kstack_top;
    t->mm.pgdir = paging_proc_init (t->pid);
    t->mm.code_start = (uint32_t)addr;
    t->mm.code_end = (uint32_t)addr + size;
    /* kthreads always descend from kthreadd, regardless of who invoked
     * exec_fn; fork() preserves the forker as parent (handled in task_fork) */
    t->parent = &kthreadd_task;

    task_add_child (&kthreadd_task, t);

    t->next = task_list_head;
    task_list_head = t;

    /* byte-granularity: x86 instructions are variable-length (1–15 bytes) */
    uint8_t *dst = (uint8_t *)addr;
    uint8_t *src = (uint8_t *)function;
    for (uint32_t i = 0; i < size; i++)
    {
        dst[i] = src[i];
    }

    t->state = TASK_RUNNABLE;
}

/* Core Ring 3 spawner: builds a task with the given pid and inserts it into the
 * scheduler list. Pure with respect to task_counter — the caller owns PID
 * policy (and rollback on failure). Returns the new task, or NULL. */
static struct task *
spawn_user (uint32_t pid, uint32_t *function, uint32_t size,
            struct task *parent)
{
    if (size == 0 || size > PAGE_SIZE || parent == NULL
        || parent == &kthreadd_task || task_counter >= MAX_PROC - 1)
    {
        return NULL;
    }

    struct task *t = kmalloc (sizeof (struct task));
    if (!t)
    {
        return NULL;
    }
    memset (t, 0, sizeof (struct task));

    t->pid = pid;

    uint32_t kstack_va = KSTACK_VA_BASE + t->pid * KSTACK_PAGES * PAGE_SIZE;
    for (uint32_t p = 0; p < KSTACK_PAGES; p++)
    {
        void *va = (void *)(kstack_va + p * PAGE_SIZE);
        if (!alloc_page (va, PAGE_PRESENT | PAGE_RW))
        {
            for (uint32_t f = 0; f < p; f++)
            {
                free_page ((void *)(kstack_va + f * PAGE_SIZE));
            }
            kfree (t);
            return NULL;
        }
    }
    uint32_t kstack_top = kstack_va + KSTACK_PAGES * PAGE_SIZE;

    t->mm.pgdir = paging_proc_init (t->pid);
    if (paging_load_user_image (t->pid, function, size) != 0)
    {
        for (uint32_t f = 0; f < KSTACK_PAGES; f++)
        {
            free_page ((void *)(kstack_va + f * PAGE_SIZE));
        }
        paging_proc_teardown (t->pid);
        kfree (t);
        return NULL;
    }

    kstack_top -= sizeof (struct trap_frame);
    struct trap_frame *tf = (struct trap_frame *)kstack_top;
    memset (tf, 0, sizeof (struct trap_frame));

    /* Ring 3 startup frame: iret pops 5 words (eip, cs, eflags, user_esp,
     * user_ss) because cs has RPL=3. ds (also restored into es/fs/gs by the
     * stubs) is the user data selector. */
    tf->eip = USER_CODE_BASE;
    tf->cs = 0x23;      /* GDT entry 4 (user code), RPL=3 */
    tf->eflags = 0x202; /* IF=1, reserved bit set */
    tf->ds = 0x2B;      /* GDT entry 5 (user data), RPL=3 */
    tf->user_esp = USER_STACK_TOP;
    tf->user_ss = 0x2B;

    t->uid = current_task->uid;
    t->euid = current_task->euid;
    t->quantum = PROC_QUANTUM;
    t->time_left = PROC_QUANTUM;
    t->kstack = (void *)kstack_va;
    t->thread.tf = tf;
    t->thread.esp = kstack_top;
    t->is_userspace = true; /* tells sched.c to update TSS.esp0 on switch-in */

    t->mm.code_start = USER_CODE_BASE;
    t->mm.code_end = USER_CODE_BASE + size;
    t->mm.stack_top = USER_STACK_TOP;

    t->parent = parent;
    task_add_child (parent, t);

    t->next = task_list_head;
    task_list_head = t;

    t->state = TASK_RUNNABLE;
    return t;
}

void
exec_user_fn (uint32_t *function, uint32_t size, struct task *parent)
{
    int32_t pid = pid_alloc ();
    if (pid < 0)
    {
        return;
    }
    if (spawn_user ((uint32_t)pid, function, size, parent) == NULL)
    {
        pid_release (); /* claim rolled back on failure */
    }
}

/* Spawn the Ring 3 init at the reserved INIT_PID, child of init_task (PID 0).
 * INIT_PID lives outside pid_alloc's range, so it never shifts the sequence. */
struct task *
exec_init_fn (uint32_t *function, uint32_t size)
{
    init_proc = spawn_user (INIT_PID, function, size, &init_task);
    return init_proc;
}

void
task_reparent (struct task *task, struct task *new_parent)
{
    if (task->parent != NULL)
    {
        task_remove_child (task->parent, task);
    }
    task_add_child (new_parent, task);
}

void
do_exit (struct task *t, int32_t code)
{
    t->exit_code = code;

    /* orphan live children onto init (PID 1); fall back to init_task pre-boot
     */
    struct task *reaper = init_proc ? init_proc : &init_task;
    if (t->children != NULL && reaper != t)
    {
        kernel_signal_send (reaper, SIGCHLD);
    }
    while (t->children != NULL)
    {
        task_reparent (t->children, reaper);
    }

    t->state = TASK_ZOMBIE;

    if (t->parent != NULL)
    {
        kernel_signal_send (t->parent, SIGCHLD);
    }
}

int32_t
task_fork (struct trap_frame *frame)
{
    if (current_task->kstack == NULL)
    {
        return -1;
    }

    int32_t pid = pid_alloc ();
    if (pid < 0)
    {
        return -1;
    }

    struct task *child = kmalloc (sizeof (struct task));
    if (!child)
    {
        pid_release ();
        return -1;
    }
    memset (child, 0, sizeof (struct task));

    child->pid = (uint32_t)pid;

    /* allocate in parent's pgdir; the kstack PT is shared via paging_proc_init
     */
    uint32_t kstack_va = KSTACK_VA_BASE + child->pid * KSTACK_PAGES * PAGE_SIZE;
    for (uint32_t p = 0; p < KSTACK_PAGES; p++)
    {
        void *va = (void *)(kstack_va + p * PAGE_SIZE);
        if (!alloc_page (va, PAGE_PRESENT | PAGE_RW))
        {
            for (uint32_t f = 0; f < p; f++)
            {
                free_page ((void *)(kstack_va + f * PAGE_SIZE));
            }
            kfree (child);
            pid_release ();
            return -1;
        }
    }
    uint32_t kstack_top = kstack_va + KSTACK_PAGES * PAGE_SIZE;

    child->mm.pgdir = paging_proc_init (child->pid);
    if (paging_fork_copy (child->pid) != 0)
    {
        for (uint32_t f = 0; f < KSTACK_PAGES; f++)
        {
            free_page ((void *)(kstack_va + f * PAGE_SIZE));
        }
        kfree (child);
        pid_release ();
        return -1;
    }

    /* copy kstack byte-for-byte: tf + call chain land at the same offset */
    uint32_t parent_kstack_va = (uint32_t)current_task->kstack;
    uint8_t *src = (uint8_t *)parent_kstack_va;
    uint8_t *dst = (uint8_t *)kstack_va;
    for (uint32_t b = 0; b < KSTACK_PAGES * PAGE_SIZE; b++)
    {
        dst[b] = src[b];
    }

    uint32_t parent_kstack_top = parent_kstack_va + KSTACK_PAGES * PAGE_SIZE;
    uint32_t frame_offset = parent_kstack_top - (uint32_t)frame;
    struct trap_frame *child_tf
        = (struct trap_frame *)(kstack_top - frame_offset);
    /* fork() returns 0 in the child; parent's return is written by
     * syscall_dispatch */
    child_tf->eax = 0;

    child->state = TASK_RUNNABLE;
    child->thread.tf = child_tf;
    child->thread.esp = (uint32_t)child_tf;
    child->kstack = (void *)kstack_va;
    child->parent = current_task;
    child->uid = current_task->uid;
    child->euid = current_task->euid;
    child->quantum = current_task->quantum;
    child->time_left = current_task->quantum;

    child->mm.code_start = current_task->mm.code_start;
    child->mm.code_end = current_task->mm.code_end;
    child->mm.data_start = current_task->mm.data_start;
    child->mm.data_end = current_task->mm.data_end;
    child->mm.stack_top = current_task->mm.stack_top;
    child->mm.heap_brk = current_task->mm.heap_brk;

    /* inherit ring: a Ring 3 fork yields a Ring 3 child that needs TSS.esp0
     * updated on switch-in (sched.c gates this on is_userspace). */
    child->is_userspace = current_task->is_userspace;

    for (int s = 0; s < NSIG; s++)
    {
        child->signal_handlers[s] = current_task->signal_handlers[s];
    }
    /* pending_signals stays zero per POSIX fork(2) */

    task_add_child (current_task, child);
    child->next = task_list_head;
    task_list_head = child;

    return (int32_t)child->pid;
}

void
task_reap (struct task *zombie)
{
    if (zombie->parent != NULL)
    {
        task_remove_child (zombie->parent, zombie);
    }

    /* unlink from the scheduler list */
    if (task_list_head == zombie)
    {
        task_list_head = zombie->next;
    }
    else
    {
        struct task *prev = task_list_head;
        while (prev != NULL && prev->next != zombie)
        {
            prev = prev->next;
        }
        if (prev != NULL)
        {
            prev->next = zombie->next;
        }
    }

    if (zombie->kstack != NULL)
    {
        uint32_t base = (uint32_t)zombie->kstack;
        for (uint32_t p = 0; p < KSTACK_PAGES; p++)
        {
            free_page ((void *)(base + p * PAGE_SIZE));
        }
    }

    if (zombie->log_buf != NULL)
    {
        kmem_free (zombie->log_buf);
    }

    if (zombie->in_buf != NULL)
    {
        kmem_free (zombie->in_buf);
    }

    paging_proc_teardown (zombie->pid);

    kfree (zombie);
}

/*
 * Runtime test scenarios for fork/wait/exit/kill/signal.
 *
 * task_test (below) is purely structural: it validates that task_init left the
 * tree in the expected state (init/kthreadd genealogy, list head, reserved
 * counter) without touching it any further. Everything else is exercised
 * manually from the shell and watched in eyeproc / dmesg / momentum: the
 * helpers (add_child/remove_child/reparent), do_exit bookkeeping, and the
 * mechanisms that need a live scheduler and ring0->ring3 transitions (blocking
 * wait, SIGCHLD wakeup, reaping through wait, reparenting of a *live* orphan,
 * ring 3 fork). Each scenario below lists the steps and the expected result.
 *
 * scenarii 1: fork + wait + exit over int 0x80 (ring 3).
 *   steps:
 *     1. spawntsk -u 1   (ufork: child exit(1), parent waitpid then exit(0))
 *     2. eyeproc         (watch the two new pids), then ESC
 *     3. dmesg
 *   expect: two ring 3 pids appear; the child reaches ZOMBIE then is reaped by
 *     the parent's wait; the parent then exits and is reaped by init (PID 1).
 *     Validates the ring 3 fork path and is_userspace inheritance. (fork/wait/
 *     exit is demonstrated in ring 3 only: kthreads are a flat pool under
 *     kthreadd and have no kernel reaper, so they must not fork.)
 *
 * scenarii 2: orphan reparenting onto init (PID 1).
 *   needs payload: a ring 3 proc that forks a long-lived child then exits
 *     without waiting (e.g. parent exit(0) immediately, child spins).
 *   steps:
 *     1. spawn the orphan-maker
 *     2. eyeproc
 *   expect: the parent goes ZOMBIE and is reaped by init; the child's parent
 *     pointer flips to PID 1 and it keeps running, proving do_exit reparents
 *     live children to the reaper.
 *
 * scenarii 3: SIGKILL terminates and cannot be caught.
 *   steps:
 *     1. spawntsk -u 3   (uspin: never exits, visible RUNNING)
 *     2. eyeproc         (read the uspin pid), then ESC
 *     3. signal <pid> 9  (attempt to install a handler on SIGKILL)
 *     4. kill <pid> 9
 *   expect: the signal command is accepted but the disposition stays default
 *     (SIGKILL is immutable); on kill the task turns ZOMBIE and is reaped by
 *     init. No "signal handler fired" line in dmesg.
 *
 * scenarii 4: catchable signal runs the installed handler.
 *   steps:
 *     1. spawntsk -k 1   (heartbeat kthread, never exits)
 *     2. eyeproc         (read the pid), then ESC
 *     3. signal <pid> 10 (install debug handler for SIGUSR1)
 *     4. kill <pid> 10
 *     5. dmesg
 *   expect: dmesg shows "signal handler fired: sig=10". Without the handler,
 *     SIGUSR1 defaults to ignore, so the contrast proves dispatch honours the
 *     registered handler over the default action.
 *
 * scenarii 5: kill rejects an unknown pid.
 *   steps:
 *     1. kill 9999 15
 *   expect: "kill: pid not found"; nothing else changes. Validates the task
 *     lookup before delivery.
 */
void
task_test (void)
{
    pr_info ("#### task test ####\n");

    /* 1. task_init invariants: init_task is the running list head, kthreadd is
     * grafted under it, task_counter sits at the reserved ceiling (2) */
    {
        if (init_task.pid != 0 || init_task.state != TASK_RUNNING)
        {
            kpanic ("task test: init_task pid/state wrong");
        }
        if (task_list_head != &init_task || current_task != &init_task)
        {
            kpanic ("task test: init_task not list head / current");
        }
        if (init_task.mm.pgdir == 0)
        {
            kpanic ("task test: init_task pgdir not loaded");
        }
        if (kthreadd_task.pid != 2 || kthreadd_task.parent != &init_task)
        {
            kpanic ("task test: kthreadd pid/parent wrong");
        }

        bool found = false;
        struct task *c;
        task_for_each_child (&init_task, c)
        {
            if (c == &kthreadd_task)
            {
                found = true;
            }
        }
        if (!found)
        {
            kpanic ("task test: kthreadd not a child of init_task");
        }
        if (task_counter != 2)
        {
            kpanic ("task test: task_counter not at reserved ceiling");
        }
        pr_info ("task: init/kthreadd genealogy ok, counter=%u\n",
                 task_counter);
    }

    pr_info ("task test passed\n\n");
}
